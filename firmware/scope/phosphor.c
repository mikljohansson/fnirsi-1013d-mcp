//----------------------------------------------------------------------------------------------------------------------------------
// phosphor.c - digital phosphor view (protocol builds only, see phosphor.h and docs/plans/phosphor-design.md).
//
// Per pass, from scope_display_trace_data (after the grid, where the traces were drawn): rebuild the code-to-row tables
// with 1.00o5's own sample mapping and clear a channel's map when they change; add a new record once; paint the maps
// and decay them for the elapsed time; count the records per second. Everything else on the screen stays 1.00o5's.
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdint.h>

#include "phosphor.h"
#include "phos_map.h"
#include "spectrum.h"
#include "timer.h"
#include "variables.h"
#include "display_lib.h"
#include "fnirsi_1013d_scope.h"
#include "spec_fmt.h"
#include "pc_hal.h"
#include "scope_functions.h"
#include "menu.h"
#include "pc_hooks.h"
#include "ui_ctrl.h"

#include <string.h>

//The maps: 2 x 291,200 B in .bss (uncached DDR, so every hot loop in phos_map.c works on words)
static uint8_t  phos_cells[2][PHOS_W * PHOS_H] __attribute__((aligned(8)));
static uint32_t phos_ext[2][PHOS_W];
static phos_map phos_m[2] = { { phos_cells[0], phos_ext[0] }, { phos_cells[1], phos_ext[1] } };

//Tables looked up per sample and per pixel live in on-chip SRAM A (0x0400-0x0FFF: free at run time, only the vectors
//at 0x00-0x3F are below the spectrum's FFT space at 0x1000): two code-to-row / column tables, four palettes
static uint16_t *phos_sram_base(void)
{
  uint16_t *p = (uint16_t *)0x00000400;

  __asm__("" : "+r"(p));                //an opaque address: GCC's -Warray-bounds takes a literal for a zero-size object
  return p;
}

#define PHOS_SRAM        (phos_sram_base())
#define PHOS_LUT(i)      (PHOS_SRAM + 256 * (i))
#define PHOS_PAL(i)      (PHOS_SRAM + 512 + 256 * (i))
#define PAL_CH1          0
#define PAL_CH2          1
#define PAL_XY           2
#define PAL_HEAT         3

static uint8 phos_ramp[256] __attribute__((aligned(4)));      //codes 0..255, the fake trace buffer for the tables

//fw 0.2.53: no strip. Persistence, Palette, Clear and the wfm/s readout are in the CTRL page's Display fold-out
//(ctrl_page.c); the warning line is the scope view's (pc_glue_scope_warning).

static struct
{
  uint8  active, persist, palette, fresh, untrig, tables;
  uint8  wanted;                    //the Display fold-out's Phosphor switch (saved); active follows it where possible
  uint32 trig_ms, last_ms, rec_ms, interval, seed;
  uint32 key[2], keyx;
  uint32 wfm_n, wfm_ms, wfm_shown;
  uint8  drawn;                     //something was drawn into a map since the last clear (wfm/s is shown)
} phos = { .persist = PHOS_PERSIST_1S, .interval = PHOS_INTERVAL_INIT };

//----------------------------------------------------------------------------------------------------------------------------------

static void phos_tables(void)
{
  uint32 i;

  for(i = 0; i < 256; i++)
    phos_ramp[i] = (uint8)i;

  phos_palette_mono(PHOS_PAL(PAL_CH1), CHANNEL1_COLOR);
  phos_palette_mono(PHOS_PAL(PAL_CH2), CHANNEL2_COLOR);
  phos_palette_mono(PHOS_PAL(PAL_XY), XYMODE_COLOR);
  phos_palette_heat(PHOS_PAL(PAL_HEAT));
  phos.tables = 1;
}

static void phos_clear_all(void)
{
  phos_clear(&phos_m[0]);
  phos_clear(&phos_m[1]);
  phos.seed      = 0;
  phos.interval  = PHOS_INTERVAL_INIT;
  phos.rec_ms    = 0;
  phos.wfm_n     = 0;
  phos.wfm_ms    = timer0ticks;
  phos.wfm_shown = 0;
  phos.drawn     = 0;
}

static PCHANNELSETTINGS phos_ch(int ch)
{
  return ch ? &scopesettings.channel2 : &scopesettings.channel1;
}

static uint32 fnv(uint32 h, uint32 v)
{
  return (h ^ v) * 16777619u;
}

//What 1.00o5's sample mapping (scope_get_y_sample / scope_get_x_sample) reads from the channel, plus the enable and
//coupling: the table is rebuilt and the map cleared only when this changes (the mapping costs ~8 uncached reads a call)
static uint32 phos_inputs(PCHANNELSETTINGS s, uint32 axis)
{
  uint32 h = fnv(2166136261u, axis);

  h = fnv(h, s->enable);
  h = fnv(h, s->coupling);
  h = fnv(h, s->displayvoltperdiv);
  h = fnv(h, s->samplevoltperdiv);
  h = fnv(h, (uint32)s->input_calibration[s->samplevoltperdiv]);
  h = fnv(h, s->traceposition);
  h = fnv(h, (uint32)(int32)s->dcoffset);

  return fnv(h, (uint32)s->dc_shift_size);
}

//Code-to-row (or XY column) table from 1.00o5's own mapping (V/div, calibration, trace position, DC offset), so the
//map, the cursors and the measurements agree by construction
static void phos_lut(PCHANNELSETTINGS s, uint16_t *lut, int xaxis)
{
  uint8  *save = s->tracebuffer, savemath = math_sample;
  uint32  c;

  s->tracebuffer = phos_ramp;
  math_sample    = 0;

  for(c = 0; c < 256; c++)
    lut[c] = (uint16_t)(xaxis ? scope_get_x_sample(s, c) - PHOS_X0 : scope_get_y_sample(s, c) - PHOS_Y0);

  s->tracebuffer = save;
  math_sample    = savemath;
}

//What the columns mean: time base, rate, trigger position, record depth, filter, XY, persistence
static uint32 phos_xkey(int32 dx)
{
  pc_decim_cfg dc;
  int          written;
  uint32       h = 2166136261u;

  pc_hal_decim_current(&dc, &written);

  h = fnv(h, (uint32)dx);
  h = fnv(h, (uint32)disp_xstart);
  h = fnv(h, (uint32)disp_xend);
  h = fnv(h, scopesettings.timeperdiv);
  h = fnv(h, scopesettings.samplerate);
  h = fnv(h, (uint32)scopesettings.triggerhorizontalposition);
  h = fnv(h, fpgasettings.totalsamples);
  h = fnv(h, fpgasettings.settriggerpoint);
  h = fnv(h, dc.k | (dc.fir << 4));
  h = fnv(h, scopesettings.xymodedisplay);

  return fnv(h, phos.persist);
}

//Clear what no longer means the same: per channel on its mapping inputs, both on the column key. In XY mode map 0
//holds CH1 (columns) against CH2 (rows) and depends on both.
static void phos_keys(int xy, int32 dx)
{
  uint32 k[2], kx = phos_xkey(dx);
  int    ch;

  k[0] = phos_inputs(&scopesettings.channel1, xy ? 2 : 0);
  k[1] = phos_inputs(&scopesettings.channel2, xy ? 3 : 1);

  if(xy)
    k[0] = fnv(k[0], k[1]);

  if(kx != phos.keyx)
  {
    phos_clear_all();
    phos.keyx = kx;
  }

  for(ch = 0; ch < 2; ch++)
    if(k[ch] != phos.key[ch])
    {
      phos_lut(phos_ch(ch), PHOS_LUT(ch), xy && !ch);
      phos_clear(&phos_m[ch]);
      phos.key[ch]  = k[ch];
    }
}

//A record counts unless it is an auto time-out while a triggered one came within the last second (a periodic signal's
//occasional miss would paint a ghost that looks like a glitch; the spectrum view drops it the same way)
static int phos_take_record(void)
{
  if(!phos.fresh)
    return 0;

  phos.fresh = 0;

  return !(phos.untrig && ((timer0ticks - phos.trig_ms) < 1000) && phos.drawn);
}

static uint32 phos_record_inc(void)
{
  uint32 now = timer0ticks;

  if(phos.rec_ms)
    phos.interval = phos_interval_update(phos.interval, now - phos.rec_ms);

  phos.rec_ms = now;
  phos.wfm_n++;
  phos.drawn = 1;

  return phos_inc(phos_persist_k1[phos.persist], phos.interval);
}

//Q16 decay for the time since the last pass: nothing while stopped, at most PHOS_DT_MAX
static uint32 phos_pass_k16(void)
{
  uint32 now = timer0ticks, dt = now - phos.last_ms;

  phos.last_ms = now;

  if(!scopesettings.runstate || (phos.persist == PHOS_PERSIST_INF) || !dt)
    return 65536;

  if(dt > PHOS_DT_MAX)
    dt = PHOS_DT_MAX;

  phos.seed = phos_seed_next(phos.seed);

  return phos_k16(phos_k_dt(phos_persist_k1[phos.persist], dt));
}

//----------------------------------------------------------------------------------------------------------------------------------
// The records added per second (the Display fold-out shows it), counted over one-second windows

static void phos_wfm_update(void)
{
  uint32 now = timer0ticks;

  if(now - phos.wfm_ms >= 1000)
  {
    phos.wfm_shown = (phos.wfm_n * 1000 + (now - phos.wfm_ms) / 2) / (now - phos.wfm_ms);
    phos.wfm_n     = 0;
    phos.wfm_ms    = now;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

int phosphor_mode(void)
{
  return phos.active;
}

int phosphor_available(void)
{
  return !scopesettings.long_mode && !scopesettings.waveviewmode;
}

void phosphor_set_active(int on)
{
  phos.active = (on && phosphor_available()) ? 1 : 0;

  if(phos.active && !phos.tables)
    phos_tables();

  phos_clear_all();
  phos.keyx    = 0;
  phos.key[0]  = phos.key[1] = 0;
  phos.fresh   = 0;
  phos.last_ms = timer0ticks;
}

void phosphor_new_data(void)
{
  phos.fresh = 1;
}

void phosphor_capture_info(int triggered)
{
  phos.untrig = triggered ? 0 : 1;

  if(triggered)
    phos.trig_ms = timer0ticks;
}

//Roll mode or a viewer ends the picture; the switch stays, so the picture comes back with the scope view (not while the
//spectrum shows)
void phosphor_service(void)
{
  if(phos.active && !phosphor_available())
    phosphor_set_active(0);
  else if(!phos.active && phos.wanted && phosphor_available() && (spectrum_mode() == SPECTRUM_MODE_SCOPE))
    phosphor_set_active(1);
}

int phosphor_switch(void)
{
  return phos.wanted;
}

void phosphor_set_switch(int on)
{
  phos.wanted = on ? 1 : 0;

  if(spectrum_mode() == SPECTRUM_MODE_SCOPE)
    phosphor_set_active(phos.wanted);
}

void phosphor_param_text(int p, char *t)
{
  fmt_str(t, (p == PHOSPHOR_P_PERSIST) ? ui_persist_names[phos.persist & 3] : ui_palette_names[phos.palette & 1]);
}

void phosphor_param_step(int p)
{
  if(p == PHOSPHOR_P_PERSIST)
  {
    //Counts mean something else at another persistence (increment, decay): start over
    phos.persist = (uint8)((phos.persist + 1) % PHOS_PERSIST_COUNT);
    phos_clear_all();
  }
  else if(p == PHOSPHOR_P_PALETTE)
    phos.palette ^= 1;
  else
    phos_clear_all();
}

void phosphor_wfm_text(char *t)
{
  t[0] = 0;

  if(phos.active && scopesettings.runstate && phos.drawn)
    fmt_str(fmt_uint(t, phos.wfm_shown), " wfm/s");
}

int phosphor_owns_traces(void)
{
  return phos.active && phosphor_available() && !scopesettings.xymodedisplay;
}

void phosphor_draw(void)
{
  int32  dx;
  uint32 k16;
  int    ch;

  if(!phosphor_owns_traces())
    return;

  //Pixels per sample in Q16, as the linear trace steps (disp_xpos_per_sample, computed per pass by 1.00o5)
  dx = (int32)(disp_xpos_per_sample * 65536.0);
  if(dx < 1)
    dx = 1;

  phos_keys(0, dx);

  if(phos_take_record())
  {
    uint32 inc = phos_record_inc(), total = fpgasettings.totalsamples, first = (disp_first_sample > 0) ? disp_first_sample : 0;
    int64  n64 = ((int64)(disp_xend - disp_xstart + 2) << 16) / dx + 2;
    uint32 n;

    if(scopesettings.samplecount < total)
      total = scopesettings.samplecount;

    n = (first < total) ? total - first : 0;
    if(n64 < 0)
      n = 0;
    else if((int64)n > n64)
      n = (uint32)n64;

    for(ch = 0; ch < 2; ch++)
      if(phos_ch(ch)->enable && n)
      {
        phos_add_record(&phos_m[ch], phos_ch(ch)->tracebuffer + first, n, PHOS_LUT(ch), (disp_xstart - PHOS_X0) << 16, dx,
                        disp_xstart - PHOS_X0, disp_xend - PHOS_X0, inc);
      }
  }

  k16 = phos_pass_k16();

  //CH2 over CH1 only where brighter
  if(scopesettings.channel1.enable)
    phos_render(&phos_m[0], 0, &displaybuffertmp[PHOS_Y0 * 800 + PHOS_X0], 800,
                PHOS_PAL(phos.palette ? PAL_HEAT : PAL_CH1), k16, phos.seed);

  if(scopesettings.channel2.enable)
    phos_render(&phos_m[1], scopesettings.channel1.enable ? &phos_m[0] : 0, &displaybuffertmp[PHOS_Y0 * 800 + PHOS_X0], 800,
                PHOS_PAL(phos.palette ? PAL_HEAT : PAL_CH2), k16, phos.seed);

  phos_wfm_update();
}

int phosphor_xy(uint32 index)
{
  uint32 total = fpgasettings.totalsamples;

  if(!phos.active || !phosphor_available())
    return 0;

  phos_keys(1, 65536);

  if(scopesettings.samplecount < total)
    total = scopesettings.samplecount;

  if(phos_take_record() && (index + 731 <= total) && scopesettings.channel1.enable && scopesettings.channel2.enable)
  {
    const uint8 *a = scopesettings.channel1.tracebuffer, *b = scopesettings.channel2.tracebuffer;
    uint32       inc = phos_record_inc(), i;
    int32        x0 = PHOS_LUT(0)[a[index]], y0 = PHOS_LUT(1)[b[index]];

    //1.00o5's 730 points from the trigger index, joined; each segment without its first pixel (the previous end)
    for(i = index + 1; i <= index + 730; i++)
    {
      int32 x1 = PHOS_LUT(0)[a[i]], y1 = PHOS_LUT(1)[b[i]];

      phos_add_line(&phos_m[0], x0, y0, x1, y1, i != index + 1, inc);
      x0 = x1;
      y0 = y1;
    }
  }

  phos_render(&phos_m[0], 0, &displaybuffertmp[PHOS_Y0 * 800 + PHOS_X0], 800, PHOS_PAL(phos.palette ? PAL_HEAT : PAL_XY),
              phos_pass_k16(), phos.seed);
  phos_wfm_update();
  return 1;
}

uint16 phosphor_settings_word(void)
{
  return (uint16)(0x9A00 | ((phos.wanted & 1) << 3) | ((phos.palette & 1) << 2) | (phos.persist & 3));
}

//fw 0.2.53: bit 3 is the Phosphor switch (a word written before 0.2.53 has it clear); the picture starts with the first
//scope pass (phosphor_service)
void phosphor_settings_restore(uint16 w)
{
  if((w & 0xFFF0) != 0x9A00)
    return;

  phos.persist = (uint8)(w & 3);
  phos.palette = (uint8)((w >> 2) & 1);
  phos.wanted  = (uint8)((w >> 3) & 1);
}
