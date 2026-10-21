//----------------------------------------------------------------------------------------------------------------------------------
// spectrum.c - on-scope spectrum analyser (protocol builds only, see spectrum.h).
//   fw 0.2.27: FFT_BENCH (C2). fw 0.2.28: the spectrum display mode (S1).
//
// Layout of the trace area while the mode is on (x 2..729, y 48..479; fw 0.2.71, ux-makeover.md "Spectrum slider"):
//   y  50..402  plot, 8 divisions of 44 px (x 6..726); warnings at its top middle (fw 0.2.75: the waterfall's and
//               density's colour legend removed, owner)
//               vertical grid lines at the 1-2-5 frequency steps of the span, labelled (bode_ui.h bu_fgrid, the grid
//               rule of ui-guidelines.md section 3); horizontal at the labelled dB divisions
//   y 406       the frequency labels under their lines
//   y 426..447  the cards (fw 0.2.56; chosen through MEASURES since 0.2.65, ui_meas.h): peak, level, THD, noise per
//               channel, RBW, span, centre, the waterfall's time span
//   y 452..471  the slider: the shown span as a window over the axis 0 .. min(fs/2, 25 MHz) (the Scope's construction);
//               a drag pans the centre, the span kept; fw 0.2.73: the whole axis's spectrum in the track (slider_ov.h)
// The parameters are on the CTRL page (ctrl_page.c: Display, Analyse); the strip and the header line went in fw 0.2.56.
// Integer only (no FPU): levels in centi-dB, frequencies in mHz (uint64).
//----------------------------------------------------------------------------------------------------------------------------------

#include "touch_pick.h"
#include "spectrum.h"
#include "fx_fft.h"
#include "timer.h"
#include "variables.h"
#include "display_lib.h"
#include "touchpanel.h"
#include "fnirsi_1013d_scope.h"
#include "spec_fmt.h"
#include "pc_hal.h"
#include "fpga_control.h"
#include "scope_functions.h"
#include "ui_ctrl.h"
#include "fpga_bus.h"
#include "menu.h"
#include "pc_hooks.h"
#include "ui_ctrl.h"
#include "ui_fmt.h"
#include "ui_meas.h"
#include "bode_ui.h"
#include "slider_ov_draw.h"
#include "spec_ladder.h"
#include "acq_view.h"

#include <string.h>

extern DISPLAYDATA displaydata;     //display_lib.c (current font for text_width)

//Work memory in DDR (.bss, uncached: the MMU and so the D-cache are off)
static int32 spec_win[FX_FFT_WIN_WORDS(FX_FFT_LOG2_MAX)] __attribute__((aligned(8)));
static int32 spec_buf[FX_FFT_BUF_WORDS(FX_FFT_LOG2_MAX)] __attribute__((aligned(8)));
static int16 spec_cdb[(FX_FFT_N_MAX / 2) + 1];

//On-chip SRAM A (32 KB at 0): at run time only the low vectors (0x00-0x3F) live there (all stacks are in DDR, start.s);
//the view's FFT up to 2048 points (fw 0.2.33) and FFT_BENCH use 0x1000-0x7008. The bootloader's stack (and the
//one ENTER_FEL sets up, 0x7C00) sit at the top. The benchmark borrows 0x1000-0x7008 for N <= 2048 (plan 2 N words +
//buffer N + 2 words; fw 0.2.27 went to 4096 with the half window).
#define SPEC_SRAM_BASE        ((int32 *)0x00001000)
#define SPEC_SRAM_LOG2_MAX    11

//----------------------------------------------------------------------------------------------------------------------------------

uint32 spectrum_us(void)
{
  uint32 t, c, pend;

  //Consistent pair: retry when the tick changed under us
  do
  {
    t    = timer0ticks;
    c    = *TMR0_CUR_VALUE_REG;
    pend = *TMR_IRQ_STA_REG & 1;
  } while(t != timer0ticks);

  //Reloaded but the tick interrupt not yet taken (IRQs masked or just raised): the counter is near the top again
  if(pend && (c > 12000))
    t++;

  return (t * 1000) + ((24000 - c) / 24);
}

static uint32 cp15_control(void)
{
  uint32 v;

  __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(v));
  return v;
}

static void put16(uint8 *p, uint32 v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8 *p, uint32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

//----------------------------------------------------------------------------------------------------------------------------------
//FFT_BENCH. One entry (24 bytes): u16 N, u8 window, u8 memory (0 DDR, 1 SRAM), u32 init us, u32 run min us, u32 run max us,
//u32 dB conversion min us (N/2 + 1 bins), u16 peak bin (DC excluded), i16 peak level (centi-dB re 1 code RMS)

static int spec_plan_dirty(void);

static uint8 *bench_one(uint8 *o, int log2n, int sram, uint32 reps)
{
  FX_FFT  p;
  int32  *win = sram ? SPEC_SRAM_BASE : spec_win;
  int32  *buf = sram ? SPEC_SRAM_BASE + FX_FFT_WIN_WORDS(log2n) : spec_buf;
  uint32  N = 1u << log2n, half = N / 2, i, k, peak = 1;
  uint32  t0, t_init, t_run, run_min = 0xFFFFFFFF, run_max = 0, cdb_min = 0xFFFFFFFF;
  int16   pv;

  t0 = spectrum_us();
  fx_fft_init(&p, log2n, FX_WIN_HANN, win, buf);
  t_init = spectrum_us() - t0;

  for(i = 0; i < reps; i++)
  {
    t0 = spectrum_us();
    fx_fft_run_u8(&p, (const uint8 *)channel1tracebuffer, 128);
    t_run = spectrum_us() - t0;

    if(t_run < run_min) run_min = t_run;
    if(t_run > run_max) run_max = t_run;

    t0 = spectrum_us();
    fx_fft_cdb(&p, 0, half + 1, 0, spec_cdb);
    t_run = spectrum_us() - t0;

    if(t_run < cdb_min) cdb_min = t_run;
  }

  for(k = 2; k <= half; k++)
    if(spec_cdb[k] > spec_cdb[peak])
      peak = k;
  pv = spec_cdb[peak];

  put16(o, N); o[2] = FX_WIN_HANN; o[3] = sram;
  put32(o + 4, t_init); put32(o + 8, run_min); put32(o + 12, run_max); put32(o + 16, cdb_min);
  put16(o + 20, peak); put16(o + 22, (uint16)pv);
  return o + 24;
}

uint32 spectrum_bench(uint8 *out, uint32 reps)
{
  static const uint8 ddr[]  = { 10, 11, 12, 14 };
  static const uint8 sram[] = { 10, 11, 12 };
  uint8  *o = out + 8;
  uint32  i;

  for(i = 0; i < sizeof(ddr); i++)
    o = bench_one(o, ddr[i], 0, reps);

  for(i = 0; i < sizeof(sram); i++)
    if(sram[i] <= SPEC_SRAM_LOG2_MAX)
      o = bench_one(o, sram[i], 1, reps);

  put16(out, (uint32)(o - out - 8) / 24);
  put16(out + 2, reps);
  put32(out + 4, cp15_control());

  //The benchmark used the work buffers: the view's plan has to be set up again
  spec_plan_dirty();
  return (uint32)(o - out);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Spectrum view state

#define SPEC_CH_BOTH          2

#define PLOT_X                6
#define PLOT_W                720
#define PLOT_Y                50
#define PLOT_H                352
#define PLOT_YDIV             8
#define FLABEL_Y              406
#define CARDS_Y               UI_MS_SPEC_Y    //from here to the slider: the cards; touches there do nothing

//fw 0.2.77: the span ladder's axis is the view's axis
typedef char spec_fmax_matches_ladder[(PC_SPECTRUM_FMAX_HZ == SPL_FMAX_HZ) ? 1 : -1];
#define LOG2N_VIEW_MAX        12          //on-scope FFT lengths 1024..4096 (0.2.31 bench: 16384 points take 51 ms)
#define REF_MIN               (-100)
#define REF_MAX               60

static const uint8 spec_scales[] = { 5, 10, 20 };   //dB per division

typedef struct
{
  uint8  active;
  uint8  channel;       //0 CH1, 1 CH2, 2 both
  uint8  log2n;         //requested FFT length; the effective one is limited by the record
  uint8  window;        //FX_WIN_*
  uint8  scale;         //index into spec_scales
  int16  ref;           //level at the top of the plot, dBV
  uint8  fresh;         //trace buffers refilled since the last FFT
  uint8  recompute;     //an FFT setting changed
  uint8  plan_ok;       //plan matches log2n / window
  uint8  have[2];       //spec_db[ch] holds a spectrum
  uint32 frames;        //spectra computed
  uint8  view;          //VIEW_*
  uint8  wait;          //the time base changed: ignore the trace buffers until a new capture
  uint8  autostage;     //Auto in progress: 1 at the full-axis rate, 2 at the chosen rate
  uint8  avg;           //trace averaging: 0 off, 1..3 over 4 / 16 / 64 captures, 4 max-hold
  spl_cell cell;        //fw 0.2.77: the span and centre shown (spec_ladder.h); fw 0.2.81: the rate follows it at each
                        //zoom step and AUTO SET only, a pan keeps the rate
  uint8  cell_set;      //cell holds a choice (else the next entry takes the current rate's whole axis)
  uint8  cell_rate;     //fw 0.2.81: the rate when the cell was last set (a pan leaves a cell the ladder would give
                        //another rate; entry keeps it while the rate is this one)
  uint16 cell_cap;      //fw 0.2.84: the record length (memory depth) the pair was last derived or checked for
  uint8  lad_rate;      //fw 0.2.84: the rate the ladder last set (zoom step, Auto, a depth change), 0xFF none; while
                        //it is the current rate a depth change re-derives it (a pan keeps it, a hand-picked rate not)
} SPECSTATE;

#define VIEW_SPECTRUM         0
#define VIEW_WATERFALL        1
#define VIEW_DENSITY          2

static SPECSTATE spec = { .log2n = 11, .window = FX_WIN_HANN, .scale = 1, .view = VIEW_SPECTRUM, .avg = 1,
                          .cell = { SPL_FULL, SPL_FMAX_MHZ / 2 }, .lad_rate = 0xFF };

//What the last FFT used, per channel (READ_SPECTRUM reports these)
typedef struct
{
  uint8  log2n;
  uint8  window;
  uint8  unit_amps;     //channel in A mode
  uint32 divider;       //the UI time base's 0x0D divider
  uint64 fs_mhz;        //sample rate in mHz: 200 MHz / (divider + 1), or 200 MHz / N with the 0x1534 filter
  uint8  wide;          //1: the 16-bit record of the decimation filter, 0: the 8-bit trace buffer
  uint8  k;             //decimation filter order (0 off, 1 boxcar, 2 CIC2, 3 CIC3)
  int32  gain_cdb;      //centi-dB from "dB re 1 code RMS" to dBV
} SPECFRAME;

static void      spec_cell_from_rate(void);
static void      spec_lad_check(void);
static void      spec_follow_cap(void);
static uint32    spec_cap(void);

static FX_FFT    spec_plan;
static uint8     auto_refused;                          //Auto refused: 1 stopped, 2 single mode (spec_auto_start)
static uint8     spec_untrig;                           //the latest UI record is untriggered (spectrum_capture_info)
static uint32    spec_trig_ms;
static SPECFRAME spec_frame[2];
static int16     spec_db[2][(FX_FFT_N_MAX / 2) + 1];    //centi-dBV as shown (averaged or held)
static int16     spec_raw[2][(FX_FFT_N_MAX / 2) + 1];   //centi-dBV of the latest record (the history views use it)

//Trace averaging (fw 0.2.40): exponential average of the dB levels, x16 fixed point, over a growing count up to
//4 / 16 / 64 captures, or the maximum per bin (max-hold). Restarts whenever a bin's meaning changes (key).
static const uint8 spec_avg_n[] = { 1, 4, 16, 64, 0 };
static int32     spec_acc[2][(FX_FFT_N_MAX / 2) + 1];
static uint32    spec_navg[2], spec_avg_key[2];

//History of the waterfall and density views (section "Waterfall and density views" below)
#define HIST_H                PLOT_H        //rows of waterfall history = levels of the density view
#define DENS_INC              32            //count added per hit (saturating at 255)
#define DENS_TAU              16            //persistence: a count decays by 1/e every 16 captures

static uint16 wf_rows[HIST_H][PLOT_W] __attribute__((aligned(8)));
static uint32 wf_ms[HIST_H];
static uint32 wf_head, wf_count;
static uint8  dens[HIST_H][PLOT_W] __attribute__((aligned(8)));       //hit count as of the cell's last hit
static uint16 dens_last[HIST_H][PLOT_W];                              //capture index of that hit
static uint16 dens_cap;                                               //captures added since the history began
static uint16 dens_dec[256];                                          //exp(-age / DENS_TAU) in Q8, age 0..255
static uint16 spec_pal[256];                                          //waterfall: level colours
static uint16 dens_pal[256];                                          //density: intensity colours
static uint8  spec_pal_ok;
static uint32 hist_key;
static int32  spec_col[PLOT_W];

static int spec_plan_dirty(void)
{
  spec.plan_ok = 0;
  spec.recompute = 1;
  return 0;
}

static PCHANNELSETTINGS spec_ch(int ch)
{
  return ch ? &scopesettings.channel2 : &scopesettings.channel1;
}

static uint32 text_width(const char *s)
{
  PFONTDATA font = displaydata.font;
  uint32    w = 0;

  if(font->type != VARIABLE_WIDTH_FONT)
    return 0;

  while(*s)
  {
    PFONTINFORMATION info = check_char_in_vw_font(font->fontinformation, (uint16)*s);

    if(info)
      w += info->fontmetrics[*s - info->first_char].width;
    s++;
  }

  return w;
}

static void text_right(uint32 xr, uint32 y, char *s)  { display_text(xr - text_width(s), y, (int8 *)s); }
static void text_center(uint32 xc, uint32 y, char *s) { display_text(xc - text_width(s) / 2, y, (int8 *)s); }

//----------------------------------------------------------------------------------------------------------------------------------
// Settings derived from the scope

static uint32 spec_divider(void)
{
  if(scopesettings.samplerate < (sizeof(sample_rate_settings) / sizeof(uint32)))
    return sample_rate_settings[scopesettings.samplerate];

  return 0;
}

//Largest FFT length the record holds
static int spec_log2n_max(void)
{
  uint32 n = scopesettings.samplecount;
  int    l = FX_FFT_LOG2_MIN;

  while((l < FX_FFT_LOG2_MAX) && ((2u << l) <= n))
    l++;

  return l;
}

//fw 0.2.84: the record the FPGA takes per capture (the memory depth), whose capture time the ladder limits
static uint32 spec_cap(void)
{
  return fpgasettings.totalsamples;
}

static int spec_log2n(void)
{
  int m = spec_log2n_max();

  if(m > LOG2N_VIEW_MAX)
    m = LOG2N_VIEW_MAX;
  return (spec.log2n < m) ? spec.log2n : m;
}

//Sample rate of the record in mHz. With the 0x1534 decimation filter on (fw 0.2.30) it is 200 MHz / N, where
//N = 2 (floor(D / 2) + 1) differs from D + 1 for even dividers (pc_decim.h)
static uint64 spec_fs_mhz(uint32 divider, const pc_decim_cfg *dc)
{
  if(dc->k && dc->n)
    return 200000000000ull / dc->n;

  return 200000000000ull / (divider + 1);
}

//Bin width in mHz
static uint64 spec_bin_mhz(uint64 fs_mhz, int log2n)
{
  return fs_mhz >> log2n;
}

//20 log10(v) in centi-dB
static int32 spec_cdb20(uint64 v)
{
  return 2 * fx_cdb_from_q16(fx_log2_q16(v ? v : 1));
}

//Code offset of 0 V and the level term from "dB re 1 code RMS" to dBV (protocol section 8.5 scaling)
static int32 spec_zero_code(PCHANNELSETTINGS s)
{
  if(s->dc_shift_value)
    return 128 - (int32)(((int32)s->dcoffset * 100) / (int32)s->dc_shift_value);

  return 128;
}

static int32 spec_gain_cdb(PCHANNELSETTINGS s)
{
  uint32 r = (s->samplevoltperdiv < 7) ? s->samplevoltperdiv : 6;
  uint32 m = (s->magnification < 7) ? s->magnification : 1;
  int64  ic = s->input_calibration[r];
  uint64 uv_q8;
  const VOLTCALCDATA *v = &volt_calc_data[m][r];

  if(ic <= 0)
    ic = 1;

  //uV per code x 256 = ic * mul * 10 / 2^13; volt_scale 4 counts in units 1000x larger than 3
  uv_q8 = ((uint64)ic * v->mul_factor * 10 * ((v->volt_scale > 3) ? 1000 : 1) + 4096) >> 13;
  if(uv_q8 == 0)
    uv_q8 = 1;

  //20 log10(uv_q8 / 256 * 1e-6) in centi-dB
  return 2 * fx_cdb_from_q16(fx_log2_q16(uv_q8)) - 2 * fx_cdb_from_q16(8 << 16) - 12000;
}

static int spec_ch_on(int ch)
{
  return spec_ch(ch)->enable;
}

//The channel(s) to show, following what is enabled
static void spec_fix_channel(void)
{
  if((spec.channel == SPEC_CH_BOTH) && !(spec_ch_on(0) && spec_ch_on(1)))
    spec.channel = spec_ch_on(0) ? 0 : 1;

  if((spec.channel < 2) && !spec_ch_on(spec.channel) && spec_ch_on(spec.channel ^ 1))
    spec.channel ^= 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// FFT of the trace buffers

static void spec_compute(void)
{
  int    fresh = spec.fresh;
  int    log2n = spec_log2n();
  uint32 half = 1u << (log2n - 1);
  int    ch;

  //Up to 2048 points the plan and the work buffer fit the on-chip SRAM, about twice as fast as the uncached DDR
  //(0.2.31 FFT_BENCH: 2048 points 2.3 ms in SRAM, 4.9 ms in DDR); FFT_BENCH borrows the same space and marks the plan
  int32 *win = (log2n <= SPEC_SRAM_LOG2_MAX) ? SPEC_SRAM_BASE : spec_win;
  int32 *buf = (log2n <= SPEC_SRAM_LOG2_MAX) ? SPEC_SRAM_BASE + FX_FFT_WIN_WORDS(log2n) : spec_buf;

  if(!spec.plan_ok || (spec_plan.log2n != log2n) || (spec_plan.window != spec.window) || (spec_plan.win != win))
  {
    fx_fft_init(&spec_plan, log2n, spec.window, win, buf);
    spec.plan_ok = 1;
  }

  pc_decim_cfg dc;
  int          written;

  //The filter the FPGA ran for this record (fw-decim: pc_dec_cur decides the readout)
  pc_hal_decim_current(&dc, &written);

  for(ch = 0; ch < 2; ch++)
  {
    PCHANNELSETTINGS s = spec_ch(ch);
    SPECFRAME *f = &spec_frame[ch];
    const uint16_t *x16 = 0;
    uint32_t n16 = 0;
    int32  g;

    spec.have[ch] = 0;

    if(!s->enable || ((spec.channel != SPEC_CH_BOTH) && (spec.channel != ch)))
      continue;

    if(dc.k)
      x16 = pc_hal_trace16(ch, &n16);

    //Levels straight into dBV: the volts-per-code term rides along as the dB offset
    g = spec_gain_cdb(s);

    if(x16 && dc.scale_q24 && (n16 >= (2u * half)))
    {
      //16-bit record: one unit is scale / 2^24 codes (out -> code = out scale / 2^24 + corr16 / 16, pc_decim.h; scale =
      //scale_q24 with the readout's gain16, corr16 its zero correction, adc_cal.h), so 0 V = (16 x zero code - corr16) x
      //2^20 / scale
      uint32 scale = acal_scale(dc.scale_q24, s->gain16);
      int32  zero  = (int32)((((int64)16 * spec_zero_code(s) - s->corr16) << 20) / (int64)scale);

      g += spec_cdb20(scale) - spec_cdb20(1ull << 24);
      fx_fft_run_u16(&spec_plan, x16, zero);
      f->wide = 1;
    }
    else
    {
      fx_fft_run_u8(&spec_plan, s->tracebuffer, spec_zero_code(s));
      f->wide = 0;
    }

    fx_fft_cdb(&spec_plan, 0, half + 1, g, spec_raw[ch]);

    f->log2n     = log2n;
    f->window    = spec.window;
    f->unit_amps = s->V_A ? 1 : 0;
    f->divider   = spec_divider();
    f->fs_mhz    = spec_fs_mhz(f->divider, &dc);
    f->k         = dc.k;
    f->gain_cdb  = g;

    //Averaging / max-hold into spec_db: a new record adds to it, a recompute of the same record does not
    {
      uint32 key = 2166136261u, k2, n;
      uint32 parts[7];

      parts[0] = log2n; parts[1] = spec.window; parts[2] = (uint32)f->fs_mhz; parts[3] = f->k | (f->wide << 4);
      parts[4] = (uint32)g; parts[5] = spec.avg; parts[6] = s->V_A;
      for(k2 = 0; k2 < 7; k2++)
        key = (key ^ parts[k2]) * 16777619u;

      if(key != spec_avg_key[ch])
      {
        spec_avg_key[ch] = key;
        spec_navg[ch] = 0;
      }

      if(!spec_navg[ch] || fresh)
      {
        n = spec_avg_n[spec.avg];
        if(spec_navg[ch] < 0xFFFF)
          spec_navg[ch]++;

        for(k2 = 0; k2 <= half; k2++)
        {
          int32 x = (int32)spec_raw[ch][k2] * 16;

          if(spec_navg[ch] == 1)
            spec_acc[ch][k2] = x;
          else if(!n)
          {
            if(x > spec_acc[ch][k2])
              spec_acc[ch][k2] = x;
          }
          else
            spec_acc[ch][k2] += (x - spec_acc[ch][k2]) / (int32)((spec_navg[ch] < n) ? spec_navg[ch] : n);
        }
      }

      for(k2 = 0; k2 <= half; k2++)
        spec_db[ch][k2] = (int16)((spec_acc[ch][k2] + ((spec_acc[ch][k2] < 0) ? -8 : 8)) / 16);
    }

    spec.have[ch] = 1;
  }

  spec.frames++;
  spec.fresh = 0;
  spec.recompute = 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Drawing

typedef struct
{
  uint32 first;         //first bin of the span
  uint32 span;          //bins across the plot width
  uint32 half;          //N / 2
  uint8  whole;         //fw 0.2.77: the bins show the whole cell (else a rate picked by hand cut it to the axis)
  int32  top;           //centi-dBV at the top edge
  int32  cdb_per_div;
} SPECVIEW;

//The displayed axis ends at PC_SPECTRUM_FMAX_HZ or fs/2, whichever is lower (owner: the front end rolls off near
//30 MHz, above 25 MHz there are only artefacts); the span ladder and the Auto band live inside it. The whole axis 0 .. half:
static void spec_view_full(SPECVIEW *v, int log2n, uint64 bin_mhz)
{
  uint64 capbins = bin_mhz ? ((uint64)PC_SPECTRUM_FMAX_HZ * 1000) / bin_mhz : 0;

  v->half = 1u << (log2n - 1);
  if(capbins < v->half)
    v->half = (capbins < 16) ? 16 : (uint32)capbins;
  v->first = 0;
  v->span = v->half;
  v->whole = 0;
  v->top = spec.ref * 100;
  v->cdb_per_div = spec_scales[spec.scale] * 100;
}

//fw 0.2.77: the cell's bins on this axis (spl_bins); while Auto runs, the whole axis of the rate it probes
static void spec_view(SPECVIEW *v, int log2n, uint64 bin_mhz)
{
  spec_view_full(v, log2n, bin_mhz);

  if(!spec.autostage)
    v->whole = (uint8)spl_bins(&spec.cell, bin_mhz, v->half, &v->first, &v->span);
}

static int32 spec_y(const SPECVIEW *v, int32 cdb)
{
  int32 y = PLOT_Y + (int32)(((int64)(v->top - cdb) * (PLOT_H / PLOT_YDIV)) / v->cdb_per_div);

  if(y < PLOT_Y) y = PLOT_Y;
  if(y > PLOT_Y + PLOT_H) y = PLOT_Y + PLOT_H;
  return y;
}

static uint32 spec_grid_color(void)
{
  uint32 c = (scopesettings.gridbrightness * 255) / 100;

  if(c < 0x30)
    c = 0x30;
  return (c << 16) | (c << 8) | c;
}

//fw 0.2.71, the grid rule (bode_ui.h bu_fgrid, linear): the frequencies of the vertical lines and labels
static uint64 spec_fa, spec_fb;

static int32 spec_fx(uint64 f)
{
  return PLOT_X + (int32)(((f - spec_fa) * PLOT_W) / ((spec_fb > spec_fa) ? spec_fb - spec_fa : 1));
}

static void spec_draw_grid(const SPECVIEW *v)
{
  bu_gline g[BU_GRID_MAX];
  char   t[24];
  uint32 i;
  int    n, k;

  display_set_fg_color(spec_grid_color());

  //fw 0.2.56: the dots follow the CTRL page's Grid; the level labels stay (the axis). fw 0.2.71: horizontal at the
  //labelled divisions, vertical at the labelled frequency steps
  if(scopesettings.gridenable)
  {
    for(i = 0; i <= PLOT_YDIV; i++)
      display_draw_horz_dots(PLOT_Y + i * (PLOT_H / PLOT_YDIV), PLOT_X, PLOT_X + PLOT_W, 4);

    n = bu_fgrid(spec_fa, spec_fb, 1, g);
    for(k = 0; k < n; k++)
      display_draw_vert_dots((uint32)spec_fx(g[k].f), PLOT_Y, PLOT_Y + PLOT_H, BU_GRID_DOTS_MAJOR);
  }

  //Level labels under each line
  display_set_font(&font_2);
  display_set_fg_color(0x00A0A0A0);

  for(i = 0; i < PLOT_YDIV; i++)
  {
    char *p = fmt_int(t, (v->top - (int32)i * v->cdb_per_div) / 100);

    if(i == 0)
      fmt_str(p, spec_frame[spec.channel == 1].unit_amps ? " dBA" : " dBV");
    display_text(PLOT_X + 3, PLOT_Y + i * (PLOT_H / PLOT_YDIV) + 2, (int8 *)t);
  }
}

//fw 0.2.71: the labels of the grid's lines (the start / centre / stop labels before), "2.5kHz", clamped inside the plot
static void spec_draw_freq_labels(const SPECVIEW *v, uint64 bin_mhz)
{
  bu_gline g[BU_GRID_MAX];
  char   t[24];
  int    n, k;

  spec_fa = bin_mhz * v->first;
  spec_fb = bin_mhz * (v->first + v->span);

  display_set_font(&font_2);
  display_set_fg_color(0x00A0A0A0);

  n = bu_fgrid(spec_fa, spec_fb, 1, g);

  for(k = 0; k < n; k++)
  {
    int32 x, w;

    if(!g[k].label)
      continue;

    fmt_str(bu_freq_short(t, g[k].f), "Hz");
    x = spec_fx(g[k].f);
    w = (int32)text_width(t);

    if(x - w / 2 < PLOT_X)
      display_text(PLOT_X, FLABEL_Y, (int8 *)t);
    else if(x + w / 2 > PLOT_X + PLOT_W)
      text_right(PLOT_X + PLOT_W, FLABEL_Y, t);
    else
      text_center((uint32)x, FLABEL_Y, t);
  }
}

//fw 0.2.71 (ux-makeover.md "Spectrum slider"): the Scope's slider row (fw 0.2.73: slider_ov.h, the track x 2..727 and
//y 452..471, the window a box, decision 7): the shown span as a window over the axis
static void spec_slider_window(const SPECVIEW *v, int32 *x, int32 *w)
{
  int64 xx = BU_SL_X + ((int64)v->first * BU_SL_W) / v->half, ww = ((int64)v->span * BU_SL_W) / v->half;

  if(ww < 8)
    ww = 8;
  if(ww > BU_SL_W)
    ww = BU_SL_W;
  if(xx + ww > BU_SL_X + BU_SL_W)
    xx = BU_SL_X + BU_SL_W - ww;

  *x = (int32)xx;
  *w = (int32)ww;
}

//fw 0.2.73 (slider-overview.md section 4): the spectrum over the whole axis 0 .. half in the track, from the bins the
//FFT already computed (no extra FFT): each column the highest bin it covers (the plot's rule), on the plot's ref level and
//dB/div, joined as a line; dimmed outside the window wx, ww
static void spec_draw_overview(const SPECVIEW *v, const int16 *db, uint32 full, uint32 dim, int32 wx, int32 ww)
{
  int32  c, prev = -1, lo, hi;
  uint32 k;

  //32-bit maths per column (no divide instruction on the target): half <= 2048
  for(c = 0; c < SOV_W; c++)
  {
    uint32 k0 = ((uint32)c * v->half) / SOV_W, k1 = ((uint32)(c + 1) * v->half) / SOV_W;
    int32  m = db[k0], r;

    for(k = k0 + 1; k < k1; k++)
      if(db[k] > m)
        m = db[k];

    r = sov_row(m, v->top, v->top - PLOT_YDIV * v->cdb_per_div);
    sov_join(prev, r, &lo, &hi);
    sov_draw_run(SOV_X + c, lo, hi, full, dim, wx, ww);
    prev = r;
  }
}

static void spec_draw_slider(const SPECVIEW *v, int hch)
{
  int32 x, w;
  int   ch;

  spec_slider_window(v, &x, &w);
  sov_draw_row();

  //Nothing while waiting for a record at a new rate: the spectrum held belongs to the old axis
  if(!spec.wait)
  {
    if(spec.view == VIEW_SPECTRUM)
    {
      //As shown (averaged or held), CH2 first so CH1 is on top
      for(ch = 1; ch >= 0; ch--)
        if(spec.have[ch])
          spec_draw_overview(v, spec_db[ch], ch ? CHANNEL2_COLOR : CHANNEL1_COLOR, ch ? SOV_CH2_DIM : SOV_CH1_DIM, x, w);
    }
    else if(spec.have[hch])
      spec_draw_overview(v, spec_raw[hch], hch ? CHANNEL2_COLOR : CHANNEL1_COLOR, hch ? SOV_CH2_DIM : SOV_CH1_DIM, x, w);
  }

  sov_draw_window(x, w);
}

//The trace of one channel; returns the bin of its highest level in the span (DC bins skipped)
static uint32 spec_draw_trace(const SPECVIEW *v, const int16 *db, uint32 color)
{
  uint32 peak = 0;
  int32  peakv = -0x7FFFFFFF;
  uint32 k, lo = (v->first < 2) ? 2 : v->first;
  int32  x, y, px = -1, py = 0;

  for(k = lo; k <= v->first + v->span; k++)
    if(db[k] > peakv)
    {
      peakv = db[k];
      peak = k;
    }

  display_set_fg_color(color);

  if(v->span >= PLOT_W)
  {
    //Each column shows the highest bin it covers (narrow lines survive)
    for(x = 0; x < PLOT_W; x++)
    {
      uint32 k0 = v->first + (uint32)(((uint64)x * v->span) / PLOT_W);
      uint32 k1 = v->first + (uint32)(((uint64)(x + 1) * v->span) / PLOT_W);
      int32  m = db[k0];

      for(k = k0 + 1; k < k1; k++)
        if(db[k] > m)
          m = db[k];

      y = spec_y(v, m);
      if(px >= 0)
        display_draw_line(PLOT_X + px, py, PLOT_X + x, y);
      px = x;
      py = y;
    }
  }
  else
  {
    //Fewer bins than columns: straight lines between the bins
    for(k = v->first; k <= v->first + v->span; k++)
    {
      x = (int32)(((k - v->first) * PLOT_W) / v->span);
      y = spec_y(v, db[k]);
      if(px >= 0)
        display_draw_line(PLOT_X + px, py, PLOT_X + x, y);
      px = x;
      py = y;
    }
  }

  //Peak marker: a small triangle above the peak
  if(peak)
  {
    int32 mx = PLOT_X + (int32)(((uint64)(peak - v->first) * PLOT_W) / v->span);
    int32 my = spec_y(v, peakv) - 3;
    int32 i;

    if(my < PLOT_Y + 6) my = PLOT_Y + 6;
    if(mx < PLOT_X + 4) mx = PLOT_X + 4;
    if(mx > PLOT_X + PLOT_W - 4) mx = PLOT_X + PLOT_W - 4;

    display_set_fg_color(WHITE_COLOR);
    for(i = 0; i < 5; i++)
      display_draw_horz_line(my - i, mx - i, mx + i);
  }

  return peak;
}

//The cards in the bottom row (fw 0.2.56, owner; the one-line header before). fw 0.2.65 (Measurements v2): the ones chosen
//in the spectrum's MEASURES list in the chosen order (ui_ms_spec_cards): per shown channel Peak, Level, THD and Noise in
//the channel colour, then RBW, Span, Center, in the waterfall the time its rows span, and the capture time of one
//record (fw 0.2.84) with a neutral chip; six fit, with more the last place reads "+N more". The decimation filter tag is not shown: the filter lives in the acquisition
//menu.
static ui_ms_spec spec_cards = { { { 1, 2, 3, 0, 0 }, { 1, 2, 0, 0, 0 }, { 1, 0, 0, 2, 3 } } };   //ui_ms_spec_defaults

ui_ms_spec *spectrum_cards(void)
{
  return &spec_cards;
}

static void spec_draw_cards(const SPECVIEW *v, const uint32 *peak, int hch, uint64 bin_mhz)
{
  ui_ms_card  c[UI_MS_ROW_CARDS];
  uint8       shown[2];
  char        t[24], more[4];
  const char *name;
  int         n, i, ch;
  int32       r;

  for(ch = 0; ch < 2; ch++)
    shown[ch] = spec.have[ch] && peak[ch] && ((spec.view == VIEW_SPECTRUM) || (ch == hch));

  n = ui_ms_spec_cards(&spec_cards, shown, (spec.view == VIEW_WATERFALL) && (wf_count > 1), c);

  for(i = 0; i < n; i++)
  {
    uint32 color = LIGHTGREY2_COLOR;

    ch   = c[i].ch;
    name = (c[i].kind < UI_MS_SP_KINDS) ? ui_ms_sp_names[c[i].kind] : "";

    if(c[i].kind <= UI_MS_SP_NOISE)
      color = ch ? CHANNEL2_COLOR : CHANNEL1_COLOR;

    switch(c[i].kind)
    {
      case UI_MS_SP_PEAK:
        ui_fmt_mhz(t, bin_mhz * peak[ch]);
        break;

      case UI_MS_SP_LEVEL:
        ui_fmt_cdb(t, spec_db[ch][peak[ch]], spec_frame[ch].unit_amps ? "dBA" : "dBV");
        break;

      //Harmonics 2..10 of the peak over the whole axis (they may lie outside a zoomed span)
      case UI_MS_SP_THD:
        r = ui_ms_thd(spec_db[ch], v->half, peak[ch]);
        if(r == UI_MS_THD_F0_LOW)
          strcpy(t, "f0 too low");
        else if(r == UI_MS_THD_F0_HIGH)
          strcpy(t, "f0 too high");
        else
          ui_fmt_sig4_fixed(t, r, "%");
        break;

      //The latest record's median over the shown span (DC bins skipped), at the RBW
      case UI_MS_SP_NOISE:
        ui_fmt_cdb(t, ui_ms_noise_cdb(spec_raw[ch], (v->first < 2) ? 2 : v->first, v->first + v->span),
                   spec_frame[ch].unit_amps ? "dBA" : "dBV");
        break;

      case UI_MS_SP_RBW:
        ui_fmt_mhz(t, (bin_mhz * fx_enbw_milli(spec.window) + 500) / 1000);
        break;

      //fw 0.2.77: the cell's own span and centre while the bins show all of it (the bins reach at most a bin past each
      //end, so the labels' lines agree), else what the bins show
      case UI_MS_SP_SPAN:
        ui_fmt_mhz(t, v->whole ? spl_span_mhz(spec.cell.span) : bin_mhz * v->span);
        break;

      case UI_MS_SP_CENTER:
        ui_fmt_mhz(t, v->whole ? spec.cell.centre : (bin_mhz * (2 * (uint64)v->first + v->span)) / 2);
        break;

      case UI_MS_SP_TIME:
      {
        uint32 oldest = (wf_head + HIST_H - wf_count) % HIST_H;

        //Hundredths of a second: four significant digits ("12.30s")
        ui_fmt_sig4(t, (int32)((timer0ticks - wf_ms[oldest]) / 10), 4, "s");
        break;
      }

      //fw 0.2.84: the time one record takes at the record's rate (memory depth / fs; the ladder keeps it within 2.048 s)
      case UI_MS_SP_CAPTURE:
      {
        pc_decim_cfg dc;
        int          written;

        pc_hal_decim_current(&dc, &written);
        ui_fmt_record_time(t, spec_cap(), spec_fs_mhz(spec_divider(), &dc));
        break;
      }

      default:
        name = ui_ms_more_text(more, ch);
        strcpy(t, "more");
        break;
    }

    scope_draw_card(ui_ms_card_x(0, i), UI_MS_SPEC_Y, color, name, t);
  }
}

//fw 0.2.51: choosing the Spectrum analyser row in roll mode switched Roll off; the warning line says so for a while
#define ROLL_NOTE_MS          5000
static uint8  roll_note;
static uint32 roll_note_ms;

//fw 0.2.77, "Stopped: press RUN to zoom further": a stopped record's axis or bins ended a zoom the ladder would go on with
#define STOP_NOTE_MS          4000
static uint8  stop_note;
static uint32 stop_note_ms;

void spectrum_note_roll_off(void)
{
  roll_note    = 1;
  roll_note_ms = timer0ticks;
}

//Warnings (owner, fw 0.2.42), only while they apply, centred over the top of the plot in the warning colour, each
//saying the consequence and the action (README "Spectrum view" lists them)
static void spec_draw_warnings(int k, uint64 fs_mhz, int hch)
{
  char   t[160], *p = t;
  int    ch;

  *p = 0;

  if(roll_note && ((timer0ticks - roll_note_ms) < ROLL_NOTE_MS))
    p = fmt_str(p, "Roll off for the spectrum   ");
  else
    roll_note = 0;
  for(ch = 0; ch < 2; ch++)
    if(spec_ch(ch)->enable && spec_ch(ch)->ADoverload &&
       ((spec.view == VIEW_SPECTRUM) ? ((spec.channel == SPEC_CH_BOTH) || (spec.channel == ch)) : (ch == hch)))
      p = fmt_str(p, ch ? "CH2 overrange: increase V/div   " : "CH1 overrange: increase V/div   ");

  if(!k && (fs_mhz < 200000000000ull))
    p = fmt_str(p, pc_glue_ui_has(PC_FEAT_DECIM) ? "Aliasing possible: set Acquisition > Filter   " :
                                                   "Aliasing possible: use a faster time base   ");

  if(!scopesettings.runstate && ((spec.view != VIEW_SPECTRUM) || (!spec.have[0] && !spec.have[1])))
    p = fmt_str(p, "Stopped: press RUN to fill the view   ");
  else if(stop_note && ((timer0ticks - stop_note_ms) < STOP_NOTE_MS) && !scopesettings.runstate)
    p = fmt_str(p, "Stopped: press RUN to zoom further   ");
  else if(auto_refused == 1)
    p = fmt_str(p, "Auto needs RUN: press RUN   ");
  else if(auto_refused == 2)
    p = fmt_str(p, "Auto needs trigger mode Auto or Normal   ");

  if(scopesettings.runstate || ((timer0ticks - stop_note_ms) >= STOP_NOTE_MS))
    stop_note = 0;

  if(spec.wait && !spec.autostage && scopesettings.runstate)
    p = fmt_str(p, "Waiting for a capture at the new rate   ");

  //ui-type: the hint of the trigger type armed (ui_ctrl.c, the Scope's rule)
  if(spec_untrig && scopesettings.runstate && !spec.autostage)
  {
    p = fmt_str(p, ui_no_trigger_hint(fpga_trig.type, fpga_trig.h ? 1 : 0));
    p = fmt_str(p, "   ");
  }

  if(p == t)
    return;

  p[-3] = 0;                                 //no trailing gap
  display_set_font(&font_2);
  display_set_fg_color(ORANGE_COLOR);
  text_center(PLOT_X + PLOT_W / 2, PLOT_Y + 3, t);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Waterfall and density views (fw 0.2.32). Both keep a history of one channel, one plot column per screen column:
//   waterfall: a ring of RGB565 rows (newest at the top), coloured when added, drawn with memcpy (STM bursts)
//   density:   8-bit hit counts per (level, column) cell along each spectrum's line, faded one stripe per capture
// The history is cleared whenever what a column or a level means changes (FFT length, window, span, start, ref,
// scale, sample rate, channel), so it never mixes axes. Costs are kept to word accesses: the DDR is uncached.


static uint16 rgb565(uint32 r, uint32 g, uint32 b)
{
  return (uint16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void spec_ramp(const uint8 (*stop)[4], uint32 nstops, uint16 *pal)
{
  uint32 i, s = 0;

  for(i = 0; i < 256; i++)
  {
    uint32 span, t;

    while((s < nstops - 2) && (i > stop[s + 1][0]))
      s++;

    span = stop[s + 1][0] - stop[s][0];
    t = i - stop[s][0];
    pal[i] = rgb565(stop[s][1] + ((int32)(stop[s + 1][1] - stop[s][1]) * (int32)t) / (int32)span,
                    stop[s][2] + ((int32)(stop[s + 1][2] - stop[s][2]) * (int32)t) / (int32)span,
                    stop[s][3] + ((int32)(stop[s + 1][3] - stop[s][3]) * (int32)t) / (int32)span);
  }
}

//Waterfall levels: black, deep blue, blue, cyan, green-yellow, yellow, red, white. Density intensity: black, dark
//blue, red, yellow, white (white = most frequent). Decay table: exp(-a / 16) in Q8 by repeated multiplication with
//exp(-1/16) = 61565 / 65536 (no libm).
static void spec_palette(void)
{
  static const uint8 wf[][4]   = { { 0, 0, 0, 0 }, { 40, 0, 0, 120 }, { 90, 0, 60, 255 }, { 135, 0, 220, 230 },
                                   { 175, 120, 255, 40 }, { 205, 255, 230, 0 }, { 235, 255, 50, 0 }, { 255, 255, 255, 255 } };
  static const uint8 ds[][4]   = { { 0, 0, 0, 0 }, { 50, 0, 0, 150 }, { 130, 220, 0, 0 }, { 200, 255, 220, 0 },
                                   { 255, 255, 255, 255 } };
  uint32 a, q = 65536;

  spec_ramp(wf, 8, spec_pal);
  spec_ramp(ds, 5, dens_pal);

  for(a = 0; a < 256; a++)
  {
    dens_dec[a] = (uint16)((q + 128) >> 8);
    q = (q * 61565u) >> 16;
  }

  spec_pal_ok = 1;
}

//Level per plot column: the highest bin a column covers, or the nearest bin when zoomed past one bin per column
static void spec_columns(const SPECVIEW *v, const int16 *db, int32 *col)
{
  uint32 x, k;

  for(x = 0; x < PLOT_W; x++)
  {
    if(v->span >= PLOT_W)
    {
      uint32 k0 = v->first + (uint32)(((uint64)x * v->span) / PLOT_W);
      uint32 k1 = v->first + (uint32)(((uint64)(x + 1) * v->span) / PLOT_W);
      int32  m = db[k0];

      for(k = k0 + 1; k < k1; k++)
        if(db[k] > m)
          m = db[k];
      col[x] = m;
    }
    else
      col[x] = db[v->first + (x * v->span + PLOT_W / 2) / PLOT_W];
  }
}

//Palette index of a level: the plot's 8 divisions from the bottom line (0, black) to the top line (255)
static uint32 spec_level_index(const SPECVIEW *v, int32 cdb)
{
  int32 range = PLOT_YDIV * v->cdb_per_div;
  int32 i = ((cdb - (v->top - range)) * 255) / range;

  return (i < 0) ? 0 : (i > 255) ? 255 : (uint32)i;
}

//The history's channel: Analyse's channel, or with CH1 + CH2 the one chosen in the Display fold-out (fw 0.2.63; CH1
//before). spec_hist_key holds it, so a change starts a new history.
static uint8 spec_hist_pick;

static int spec_hist_ch(void)
{
  if((spec.channel == SPEC_CH_BOTH) && spec_ch_on(0) && spec_ch_on(1))
    return spec_hist_pick ? 1 : 0;

  return ((spec.channel == 1) || !spec_ch_on(0)) ? 1 : 0;
}

static void spec_hist_clear(void)
{
  wf_head = wf_count = 0;
  memset(dens, 0, sizeof(dens));
  dens_cap = 0;
}

//What the columns and levels mean; a change clears the history
static uint32 spec_hist_key(const SPECVIEW *v, int log2n, uint64 fs_mhz)
{
  uint32 k = 2166136261u, i;
  uint32 parts[9];

  parts[0] = log2n;      parts[1] = spec.window; parts[2] = spec.view; parts[3] = spec_hist_ch();
  parts[4] = v->first;   parts[5] = v->span;     parts[6] = (uint32)v->top; parts[7] = (uint32)v->cdb_per_div;
  parts[8] = (uint32)(fs_mhz ^ (fs_mhz >> 32));

  for(i = 0; i < 9; i++)
    k = (k ^ parts[i]) * 16777619u;

  return k;
}

//A new capture's spectrum into the history of the current view
static void spec_hist_add(const SPECVIEW *v)
{
  int    ch = spec_hist_ch();
  uint32 x;

  if(!spec.have[ch])
    return;

  if(!spec_pal_ok)
    spec_palette();

  spec_columns(v, spec_raw[ch], spec_col);

  if(spec.view == VIEW_WATERFALL)
  {
    uint32 *row = (uint32 *)wf_rows[wf_head];

    for(x = 0; x < PLOT_W; x += 2)
      *row++ = spec_pal[spec_level_index(v, spec_col[x])] | ((uint32)spec_pal[spec_level_index(v, spec_col[x + 1])] << 16);

    wf_ms[wf_head] = timer0ticks;
    wf_head = (wf_head + 1) % HIST_H;
    if(wf_count < HIST_H)
      wf_count++;
  }
  else if(spec.view == VIEW_DENSITY)
  {
    int32  py = -1;

    //Hits along the line: every cell between the previous column's level and this one's
    for(x = 0; x < PLOT_W; x++)
    {
      int32 y = spec_y(v, spec_col[x]) - PLOT_Y, lo, hi;

      if(y > HIST_H - 1)
        y = HIST_H - 1;

      lo = hi = y;
      if(py >= 0)
      {
        if(py < lo) lo = py;
        if(py > hi) hi = py;
      }

      //Decay the count to now from its last hit, then add this hit (exponential persistence, no fading pass)
      for(; lo <= hi; lo++)
      {
        uint32 c = dens[lo][x], age = (uint16)(dens_cap - dens_last[lo][x]);

        c = (age > 255) ? 0 : (c * dens_dec[age]) >> 8;
        dens[lo][x] = (c > 255 - DENS_INC) ? 255 : c + DENS_INC;
        dens_last[lo][x] = dens_cap;
      }

      py = y;
    }

    dens_cap++;
  }
}

static void spec_draw_waterfall(void)
{
  uint32 r;

  for(r = 0; r < wf_count; r++)
    memcpy(&displaybuffertmp[(PLOT_Y + r) * 800 + PLOT_X], wf_rows[(wf_head + HIST_H - 1 - r) % HIST_H], PLOT_W * 2);
}

//Each cell's count decayed to the latest capture; a cell older than the decay table is cleared (so a 16-bit capture
//index can never wrap back to look fresh). Zero words (4 cells) are skipped: the clear already left them black.
static void spec_draw_density(void)
{
  uint16 now = (uint16)(dens_cap - 1);
  uint32 y, n, b;

  if(!spec_pal_ok)
    spec_palette();

  for(y = 0; y < HIST_H; y++)
  {
    uint32 *src = (uint32 *)dens[y];
    uint16 *dst = &displaybuffertmp[(PLOT_Y + y) * 800 + PLOT_X];

    for(n = 0; n < PLOT_W / 4; n++, dst += 4)
    {
      uint32 v = src[n];

      if(!v)
        continue;

      for(b = 0; b < 4; b++)
      {
        uint32 c = (v >> (8 * b)) & 0xFF, x = 4 * n + b, age;

        if(!c)
          continue;

        age = (uint16)(now - dens_last[y][x]);
        if(age > 255)
        {
          dens[y][x] = 0;
          continue;
        }

        c = (c * dens_dec[age]) >> 8;
        if(c)
          dst[b] = dens_pal[40 + ((c * 215) >> 8)];
      }
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Parameters as the CTRL page's Analyse fold-out shows them (fw 0.2.56; the strip before): value names from ui_ctrl.h

void spectrum_param_text(int p, char *t)
{
  char *q;

  switch(p)
  {
    case SPEC_P_CHANNEL:
      fmt_str(t, ui_spec_chan_names[spec.channel % 3]);
      break;

    //fw 0.2.77: "(record)" when the record holds fewer samples than the length chosen
    case SPEC_P_FFT:
      q = fmt_uint(t, 1u << spec_log2n());
      if(spec.log2n > spec_log2n())
        fmt_str(q, " (record)");
      break;

    case SPEC_P_WINDOW:
      fmt_str(t, ui_spec_window_names[spec.window % 3]);
      break;

    case SPEC_P_AVG:
      fmt_str(t, ui_spec_avg_names[spec.avg % 5]);
      break;

    case SPEC_P_SCALE:
      q = fmt_uint(t, spec_scales[spec.scale]);
      fmt_str(q, " dB/div");
      break;

    case SPEC_P_REF:
      q = fmt_int(t, spec.ref);
      fmt_str(q, spec_frame[spec.channel == 1].unit_amps ? " dBA" : " dBV");
      break;

    default:
      t[0] = 0;
      break;
  }
}

int spectrum_view(void)
{
  return spec.view;
}

void spectrum_set_view(int v)
{
  if((v >= VIEW_SPECTRUM) && (v <= VIEW_DENSITY))
    spec.view = (uint8)v;
}

int spectrum_hist_channel(void)
{
  return spec_hist_ch();
}

int spectrum_hist_can_choose(int ch)
{
  return (ch >= 0) && (ch <= 1) && spec_ch_on(ch);
}

void spectrum_set_hist_channel(int ch)
{
  if(!spectrum_hist_can_choose(ch))
    return;

  //Like any setting in the fold-outs it ends a running Auto
  spec.autostage = 0;
  auto_refused = 0;
  spec_hist_pick = (uint8)ch;

  if(spec.channel != SPEC_CH_BOTH)
  {
    spec.channel   = (uint8)ch;
    spec.recompute = 1;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

int spectrum_mode(void)
{
  return spec.active ? SPECTRUM_MODE_SPECTRUM + spec.view : SPECTRUM_MODE_SCOPE;
}

int spectrum_available(void)
{
  return !scopesettings.long_mode && !scopesettings.waveviewmode;
}

//AUTO SET's label follows the view's Auto ("Auto..." while it runs); auto_shown is what the button shows
static uint8 auto_shown;

static void spec_autoset_button(void)
{
  DISPLAYDATA save = displaydata;

  auto_shown = spec.active && spec.autostage;

  //Only on page one: on the CTRL page the slot holds Analyse (fw 0.2.56), in the waveform viewer Next wave; page one
  //redraws the label when it returns
  if((scopesettings.rightmenustate == 0) && !scopesettings.waveviewmode)
  {
    display_set_screen_buffer((uint16 *)maindisplaybuffer);
    scope_auto_set_button(0);
  }

  displaydata = save;
}

void spectrum_set_active(int on)
{
  //Leaving (or entering) ends a running Auto
  spec.autostage = 0;
  if(auto_shown)
    spec_autoset_button();

  spec.active = (on && spectrum_available()) ? 1 : 0;
  spec.recompute = 1;

  //fw 0.2.77: the cell kept from the last visit while the rate is still the one the ladder gives it (fw 0.2.81: or
  //the one it had when the cell was last set, as after a pan); otherwise the current rate's whole axis (no rate change
  //on entering)
  if(spec.active && (!spec.cell_set || ((scopesettings.samplerate != spec.cell_rate) &&
     (spl_rate(spl_stop(&spec.cell), spec_cap(), sample_rate, ACQ_RATES) != (int32)scopesettings.samplerate))))
    spec_cell_from_rate();

  //fw 0.2.84: no rate change on entering, also when the memory depth changed in another mode (the next zoom step
  //takes the ladder's rate for it); a depth change from here on follows while the rate is the ladder's for the cell
  if(spec.active)
    spec_lad_check();
  spec.have[0] = spec.have[1] = 0;
  spec_hist_clear();
}

void spectrum_new_data(void)
{
  spec.fresh = 1;
}

//Untriggered records (fw 0.2.41): with a periodic signal that normally triggers, an occasional auto time-out record
//(no edge in it) would flash a different spectrum (the owner's bimodal flicker), so it is dropped while a triggered
//record came within the last second. A signal that stops triggering for longer is shown, with a warning.
static uint8  spec_untrig;
static uint32 spec_trig_ms;

void spectrum_capture_info(int triggered)
{
  spec_untrig = triggered ? 0 : 1;
  if(triggered)
    spec_trig_ms = timer0ticks;
}

void spectrum_service(void)
{
  if(spec.active && !spectrum_available())
    spectrum_set_active(0);
}

//Black trace area with STM bursts (display_fill_rect writes one pixel at a time)
static void spec_clear(void)
{
  uint32 y;

  for(y = 48; y < 480; y++)
    memset(&displaybuffertmp[y * 800 + 2], 0, 728 * 2);
}

static uint32 spec_find_peak(const SPECVIEW *v, const int16 *db)
{
  uint32 k, peak = 0, lo = (v->first < 2) ? 2 : v->first;
  int32  pv = -0x7FFFFFFF;

  for(k = lo; k <= v->first + v->span; k++)
    if(db[k] > pv)
    {
      pv = db[k];
      peak = k;
    }

  return peak;
}

static void spec_auto_step(void);

//Re-entry guard: Auto and the span gestures change the time base from inside a draw, and the time base write redraws
//the view (scope_preset_values). The nested call only claims the trace area; the outer one draws.
static uint8 spec_drawing;

static int spectrum_display_body(void);

int spectrum_display(void)
{
  int r;

  if(!spec.active || !spectrum_available())
    return 0;

  if(spec_drawing)
    return 1;

  spec_drawing = 1;
  r = spectrum_display_body();
  spec_drawing = 0;
  return r;
}

//Bin width in mHz of the current rate and FFT length
static uint64 spec_bin_mhz_now(void)
{
  pc_decim_cfg dc;
  int          written;

  pc_hal_decim_current(&dc, &written);
  return spec_bin_mhz(spec_fs_mhz(spec_divider(), &dc), spec_log2n());
}

//The view as the next drawing will compute it (spectrum_display_body's steps, without computing a spectrum)
static void spec_current_view(SPECVIEW *v)
{
  int          log2n = spec_log2n(), written;
  pc_decim_cfg dc;

  pc_hal_decim_current(&dc, &written);
  spec_view(v, log2n, spec_bin_mhz(spec_fs_mhz(spec_divider(), &dc), log2n));
}

static int spectrum_display_body(void)
{
  int     log2n, ch, fresh, hch;
  uint32  divider, peak[2] = { 0, 0 }, key;
  uint64  fs_mhz;
  pc_decim_cfg dc;
  int     written;
  uint64  bin_mhz;
  SPECVIEW v;

  spec_fix_channel();
  spec_follow_cap();

  log2n = spec_log2n();
  fresh = spec.fresh;

  if(fresh && spec_untrig && ((timer0ticks - spec_trig_ms) < 1000) && (spec.have[0] || spec.have[1]))
  {
    spec.fresh = 0;
    fresh = 0;
  }

  //After a time base change only a record at the new rate counts
  if(spec.wait)
  {
    if(fresh)
      spec.wait = 0;
    else
      fresh = 0;
  }

  if(!spec.wait && (spec.fresh || spec.recompute || (spec_frame[0].log2n != log2n && spec.have[0]) ||
     (spec_frame[1].log2n != log2n && spec.have[1])))
  {
    spec_compute();

    //Auto's next stage on this record (may change the time base again, or finish the settings)
    if(fresh && spec.autostage)
    {
      spec_auto_step();
      if(spec.wait)
        fresh = 0;
      log2n = spec_log2n();
    }
  }

  divider = spec_divider();
  pc_hal_decim_current(&dc, &written);
  fs_mhz  = spec_fs_mhz(divider, &dc);
  bin_mhz = spec_bin_mhz(fs_mhz, log2n);
  spec_view(&v, log2n, bin_mhz);
  hch = spec_hist_ch();

  //History views: a new axis starts a new history; each new capture adds one row / one line of hits
  if(spec.view != VIEW_SPECTRUM)
  {
    key = spec_hist_key(&v, log2n, fs_mhz);
    if(key != hist_key)
    {
      spec_hist_clear();
      hist_key = key;
    }

    if(fresh)
      spec_hist_add(&v);
  }

  display_set_screen_buffer(displaybuffertmp);
  spec_clear();
  spec_draw_freq_labels(&v, bin_mhz);

  if(spec.view == VIEW_WATERFALL)
    spec_draw_waterfall();
  else
  {
    if(spec.view == VIEW_DENSITY)
      spec_draw_density();
    spec_draw_grid(&v);
  }

  spec_draw_warnings(dc.k, fs_mhz, hch);

  if(((!spec.have[0] && !spec.have[1]) || ((spec.view == VIEW_WATERFALL) && !wf_count)) && scopesettings.runstate)
  {
    const char *msg = !(spec_ch_on(0) || spec_ch_on(1)) ? "No channel enabled: switch on CH1 or CH2" :
                      spec.autostage ? "Auto..." : "Waiting for a capture";

    display_set_font(&font_3);
    display_set_fg_color(WHITE_COLOR);
    text_center(PLOT_X + PLOT_W / 2, PLOT_Y + PLOT_H / 2 - 8, (char *)msg);
  }

  if(spec.view == VIEW_SPECTRUM)
  {
    //CH2 first, so CH1 is on top where they overlap
    for(ch = 1; ch >= 0; ch--)
      if(spec.have[ch])
        peak[ch] = spec_draw_trace(&v, spec_db[ch], ch ? CHANNEL2_COLOR : CHANNEL1_COLOR);

  }
  else if(spec.have[hch])
  {
    peak[hch] = spec_find_peak(&v, spec_db[hch]);
  }

  spec_draw_cards(&v, peak, hch, bin_mhz);
  spec_draw_slider(&v, hch);

  display_set_source_buffer(displaybuffertmp);
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_copy_rect_to_screen(2, 48, 728, 432);

  //Auto started, finished or cancelled since the button was drawn
  if((spec.autostage != 0) != auto_shown)
    spec_autoset_button();

  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------------------------------
// Span drives the sample rate (fw 0.2.77, owner 2026-10-19 23:09; docs/ui-guidelines.md section 2): the view is a cell of
// the span ladder (spec_ladder.h), a 1-2-5 span about a centre. After each zoom step with the rate free (running or
// Single, acq_view_rate_free) and at the end of AUTO SET the rate is spl_rate of the cell's stop: the slowest whose
// fs / 2 covers it with a capture of the record, memory depth / fs (fw 0.2.84; N / fs before), of at most 2.048 s. A
// memory depth change (ACQ menu, Filter, host) re-derives the rate at once while it is the cell's (spec_follow_cap);
// the FFT length does not enter the rate, only the zoom-in end (16 bins). fw 0.2.81 (owner 2026-10-20 10:24): a pan (plot drag,
// slider drag, tap to jump) never changes the rate; it is clamped to the current rate's axis 0 .. fs / 2 (the slider
// track), so to look above it the user zooms out (the rate rises) and in again. It is the scope's own rate (same setting), written with the Scope ladder's time/div
// for it (acq_view_tb_for_rate, so nothing snaps) through pc_hal_timebase_write, as SET_TIMEBASE. After a change the
// trace buffers still hold the old rate's record, so the view waits for the first new capture. A rate set elsewhere (the
// ACQ menu, the host) holds until the next zoom step; the view shows the cell on that rate's axis as far as it reaches,
// and a pan clamps the cell into that axis.
// Stopped, zoom and pan stay on the record's rate and axis.

#define TB_FIRST              11          //short time bases (Auto's probes; the roll-mode ones would leave the view)

static uint32 tb_divider(int i)
{
  uint8_t  sr;
  uint32_t d;

  return pc_hal_timebase_lookup((uint8)i, &sr, &d) ? 0xFFFFFFFF : d;
}

//The rate write: the view waits for a record at the new rate (anything finishing now was armed before the change). Set
//before the write: its redraw (scope_preset_values -> spectrum_display) then shows the new axis without the old spectra
static void spec_tb_write(pc_hw_timebase *t)
{
  spec.wait = 1;
  spec.fresh = 0;
  spec.have[0] = spec.have[1] = 0;

  pc_hal_timebase_write(t, 0);
}

//Time base index i (Auto's probes at fixed time bases)
static void spec_tb_set(int i)
{
  pc_hw_timebase t;

  if((i < TB_FIRST) || (i > ACQ_TB_LAST) || (i == scopesettings.timeperdiv))
    return;

  t.timeperdiv = (uint8)i;
  if(pc_hal_timebase_lookup((uint8)i, &t.samplerate, &t.divider))
    return;

  spec_tb_write(&t);
}

//Sample rate index r (sample_rate[]) with the Scope ladder's time/div for it
static void spec_rate_set(int32 r)
{
  pc_hw_timebase t;
  int32          tpd;

  if((r < 0) || (r >= ACQ_RATES) || ((r == (int32)scopesettings.samplerate) && !scopesettings.long_mode))
    return;

  tpd = acq_view_tb_for_rate((uint32)r);
  t.timeperdiv = (uint8)((tpd >= 0) ? tpd : scopesettings.timeperdiv);
  t.samplerate = (uint8)r;
  t.divider    = sample_rate_settings[r];

  spec_tb_write(&t);
}

//Sample rate in mHz of a time base index, ignoring the decimation filter's even-divider rounding
static uint64 tb_fs_mhz(int i)
{
  uint32 d = tb_divider(i);

  return (d == 0xFFFFFFFF) ? 0 : 200000000000ull / (d + 1);
}

//The rates the ladder may use now: every short-mode rate while a new record can be taken, else the record's own
static const uint32 *spec_rates(int32 *n)
{
  uint32 r = (scopesettings.samplerate < ACQ_RATES) ? scopesettings.samplerate : 0;

  if(acq_view_rate_free())
  {
    *n = ACQ_RATES;
    return sample_rate;
  }

  *n = 1;
  return &sample_rate[r];
}

//The cell holds a choice made at the current rate (after the rate write, if any)
static void spec_cell_done(void)
{
  spec.cell_set = 1;
  spec.cell_rate = scopesettings.samplerate;
  spec.cell_cap = (uint16)spec_cap();
}

//After a zoom step: the rule's rate for the cell, when the rate is free
static void spec_cell_apply(void)
{
  if(acq_view_rate_free())
  {
    spec_rate_set(spl_rate(spl_stop(&spec.cell), spec_cap(), sample_rate, ACQ_RATES));
    spec.lad_rate = scopesettings.samplerate;
  }

  spec_cell_done();
}

//fw 0.2.84, the pair invariant after a memory depth change (ACQ menu depth or Filter, the host's SET_TRIGGER total or
//SET_DECIM): the rule's rate for the cell at the new record length at once, not at the next zoom step. Only while the
//rate is the one the ladder set (a rate picked by hand holds until the next zoom step) and free (stopped, the check
//waits for RUN); Auto ends on its own pair. Called at the start of every view pass (the ACQ menu is modal: the change
//shows when it closes). The rate write redraws the view from inside; the re-entry guard and cell_cap (set first) keep
//that from coming back here.
static void spec_follow_cap(void)
{
  uint32 cap = spec_cap();
  int    ours;

  if((cap == spec.cell_cap) || spec.autostage || !acq_view_rate_free())
    return;

  ours = spec.cell_set && (scopesettings.samplerate == spec.lad_rate);
  spec.cell_cap = (uint16)cap;

  if(ours)
    spec_cell_apply();
}

//fw 0.2.84: the cell and rate taken as they are (entry, Reset): the depth is the one checked, and the rate counts as
//the ladder's when it is the rule's for the cell at that depth
static void spec_lad_check(void)
{
  spec.cell_cap = (uint16)spec_cap();
  spec.lad_rate = (spl_rate(spl_stop(&spec.cell), spec_cap(), sample_rate, ACQ_RATES) == (int32)scopesettings.samplerate) ?
                  scopesettings.samplerate : 0xFF;
}

//The cell that shows the current rate's whole axis (entry, Reset): no rate change, the rule gives this rate for it
//(below 50 MSa/s; at 100 and 200 MSa/s the whole axis's rate is 50 MSa/s, taken at the next step)
static void spec_cell_from_rate(void)
{
  uint32 r = (scopesettings.samplerate < ACQ_RATES) ? scopesettings.samplerate : 0;

  spl_from_axis(&spec.cell, spl_axis_mhz(&sample_rate[r], 1));
  spec_cell_done();
  spec_lad_check();
}

//Span: one ladder step narrower (in) or wider (out) about the centre; at an end nothing changes
static void spec_zoom(int in)
{
  int32           n;
  const uint32   *r = spec_rates(&n);
  int             log2n = spec_log2n();
  spl_cell        c = spec.cell;

  if(spl_zoom(&spec.cell, in ? 1 : -1, log2n, spec_cap(), r, n))
  {
    spec_cell_apply();
    return;
  }

  if((n == 1) && spl_zoom(&c, in ? 1 : -1, log2n, spec_cap(), sample_rate, ACQ_RATES))
  {
    stop_note = 1;
    stop_note_ms = timer0ticks;
  }
}

//Pan: the cell c0 (where the drag began) with its centre moved by d mHz, clamped to the current rate's axis; the rate
//never changes (fw 0.2.81). 1 when the view changed
static int spec_pan_by(const spl_cell *c0, int64 d)
{
  uint32   r = (scopesettings.samplerate < ACQ_RATES) ? scopesettings.samplerate : 0;
  spl_cell c = *c0;

  spl_pan(&c, d, spl_axis_mhz(&sample_rate[r], 1));
  if((c.span == spec.cell.span) && (c.centre == spec.cell.centre))
    return 0;

  spec.cell = c;
  spec_cell_done();
  return 1;
}

//What the view starts with (Reset): 2048 points, Hann, 10 dB/div, ref 0 dBV, the current rate's whole axis,
//spectrum. The rate is the scope's and stays.
static void spec_defaults(void)
{
  spec.log2n  = 11;
  spec.window = FX_WIN_HANN;
  spec.scale  = 1;
  spec.ref    = 0;
  spec.view   = VIEW_SPECTRUM;
  spec.avg    = 1;
  spec.autostage = 0;
  spec.recompute = 1;
  spec_cell_from_rate();
}

//Peak P (above DC), median floor F and the occupied band lo..hi (bins above P - 40 dB and F + 10 dB) of the shown
//channel(s) on the axis v; returns 0 when there is no spectrum
static int spec_analyse(const SPECVIEW *v, int32 *P, int32 *F, uint32 *lo, uint32 *hi)
{
  static uint16 hist[261];                   //1 dB steps from -200 to +60 dB
  uint32 k, n = 0, cum;
  int32  thr;
  int    ch;

  *P = -0x7FFFFFFF;
  *lo = *hi = 0;
  memset(hist, 0, sizeof(hist));

  for(ch = 0; ch < 2; ch++)
  {
    if(!spec.have[ch])
      continue;

    for(k = 2; k <= v->half; k++)
    {
      int32 d = spec_db[ch][k] / 100 + 200;

      if(spec_db[ch][k] > *P)
        *P = spec_db[ch][k];
      hist[(d < 0) ? 0 : (d > 260) ? 260 : d]++;
      n++;
    }
  }

  if(!n)
    return 0;

  for(k = 0, cum = 0; (k < 260) && (2 * (cum + hist[k]) < n); k++)
    cum += hist[k];
  *F = ((int32)k - 200) * 100;

  thr = *P - 4000;
  if(thr < *F + 1000)
    thr = *F + 1000;

  for(ch = 0; ch < 2; ch++)
    if(spec.have[ch])
      for(k = 2; k <= v->half; k++)
        if(spec_db[ch][k] > thr)
        {
          if(!*lo || (k < *lo)) *lo = k;
          if(k > *hi) *hi = k;
        }

  return 1;
}

//Auto (fw 0.2.42, owner): a fixed probe procedure, independent of the state it starts from, so identical presses on
//the same signal give identical settings (rules in firmware/README.md "Spectrum view", Auto):
//  probe   50 MSa/s, longest FFT up to 4096, Hann, averaging 4, trigger on the analysed channel at 50 %; 3 records
//  measure f0 = fundamental from the time-domain period (rising crossings with hysteresis, 0 when fewer than 3),
//          f_hi = top of the band (highest bin above max(peak - 40 dB, median + 10 dB))
//  derive  fw 0.2.77, on the span ladder (spl_auto): the span >= max(1.25 f_hi, 20 f0) up to the whole axis, from 0 Hz;
//          with f0 > 0 narrower while RBW > f0 / 4, never past the zoom-in end; the rate is the ladder's for that cell
//  refine  2 records at that rate; if f_hi moved by more than 2x or f0 changed the rate, derive once more (3rd stage)
//  final   ref = first scale multiple >= peak + 3 dB, scale from peak - floor, the cell and its rate, averaging 4
//Untriggered records are skipped while a triggered one came (spectrum_capture_info); any touch cancels; it gives up
//after 4 s with what it has. Stopped or in single mode it does not run (warning says what to do).

#define TB_FULL_AXIS          26          //2 us/div, 50 MSa/s: fs/2 = 25 MHz (the probe)
#define AUTO_TIMEOUT_MS       4000

static uint32 auto_t0, auto_count;
static uint8  auto_refused;               //1: stopped, 2: single mode (warning until the next touch or Auto)

//Fundamental of the analysed channel's record in mHz (0 = no periodicity seen)
static uint64 spec_f0_mhz(int ch, uint64 fs_mhz)
{
  PCHANNELSETTINGS s = spec_ch(ch);
  uint32 n = scopesettings.samplecount, i, first = 0, last = 0, rises = 0, hi = s->highlevel, lo = s->lowlevel;
  int    state = (s->tracebuffer[0] > hi) ? 1 : 0;

  if(hi <= lo)
    return 0;

  for(i = 1; i < n; i++)
  {
    uint32 v = s->tracebuffer[i];

    if(!state && (v > hi))
    {
      state = 1;
      if(!rises)
        first = i;
      last = i;
      rises++;
    }
    else if(state && (v < lo))
      state = 0;
  }

  if(rises < 3)
    return 0;

  return (fs_mhz * (rises - 1)) / (last - first);
}

static int spec_auto_ch(void)
{
  return ((spec.channel == 1) || !spec_ch_on(0)) ? 1 : 0;
}

//Measure f0 and f_hi on the record(s) at the current rate (averaged spectrum)
static void spec_auto_measure(uint64 *f0, uint64 *fhi)
{
  pc_decim_cfg dc;
  int     written, log2n = spec_log2n();
  uint64  fs, bin;
  uint32  lo, hi;
  int32   P, F;
  SPECVIEW v;

  pc_hal_decim_current(&dc, &written);
  fs  = spec_fs_mhz(spec_divider(), &dc);
  bin = spec_bin_mhz(fs, log2n);
  spec_view_full(&v, log2n, bin);

  *fhi = (spec_analyse(&v, &P, &F, &lo, &hi) && hi) ? bin * hi : 0;
  *f0  = spec_f0_mhz(spec_auto_ch(), fs);
}

//The ladder cell and its rate (sample_rate[] index) from (f0, f_hi); Auto runs only with the rate free
static int32 spec_auto_derive(uint64 f0, uint64 fhi, spl_cell *c)
{
  int log2n = spec_log2n();

  spl_auto(c, f0, fhi, log2n, spec_cap(), sample_rate, ACQ_RATES);
  return spl_rate(spl_stop(c), spec_cap(), sample_rate, ACQ_RATES);
}

//The level settings from the record at hand, then the cell and its rate (a change waits for a record at that rate)
static void spec_auto_finish(const spl_cell *c, int32 r)
{
  pc_decim_cfg dc;
  int     written, log2n = spec_log2n();
  uint64  fs;
  uint32  lo, hi;
  int32   P, F, range, scale;
  SPECVIEW v;

  spec.autostage = 0;

  pc_hal_decim_current(&dc, &written);
  fs = spec_fs_mhz(spec_divider(), &dc);
  spec_view_full(&v, log2n, spec_bin_mhz(fs, log2n));

  if(spec_analyse(&v, &P, &F, &lo, &hi))
  {
    int32 t, sc;

    range = P - F + 1000;
    scale = (range <= 8 * 500) ? 0 : (range <= 8 * 1000) ? 1 : 2;
    spec.scale = scale;
    t  = P + 300;
    sc = spec_scales[scale] * 100;
    spec.ref = (int16)(((t >= 0) ? ((t + sc - 1) / sc) : -((-t) / sc)) * spec_scales[scale]);
    if(spec.ref < REF_MIN) spec.ref = REF_MIN;
    if(spec.ref > REF_MAX) spec.ref = REF_MAX;
  }

  spec.cell = *c;
  spec_rate_set(r);
  spec.lad_rate = scopesettings.samplerate;
  spec_cell_done();
}

static void spec_auto_start(void)
{
  int ch = spec_auto_ch();

  spec.autostage = 0;
  auto_refused = 0;

  if(!scopesettings.runstate)  { auto_refused = 1; return; }
  if(scopesettings.triggermode == 1) { auto_refused = 2; return; }
  if(!spec_ch_on(ch))
    return;

  spec.log2n  = (spec_log2n_max() < LOG2N_VIEW_MAX) ? spec_log2n_max() : LOG2N_VIEW_MAX;
  spec.window = FX_WIN_HANN;
  spec.avg    = 1;
  spec.recompute = 1;

  //Trigger on the analysed channel (an EXT source stays)
  if(scopesettings.triggerchannel < 2)
  {
    DISPLAYDATA save = displaydata;

    scopesettings.triggerchannel = (uint8)ch;
    fpga_set_trigger_channel();
    display_set_screen_buffer((uint16 *)maindisplaybuffer);
    scope_trigger_settings(0);
    displaydata = save;
  }

  if(scopesettings.timeperdiv != TB_FULL_AXIS)
    spec_tb_set(TB_FULL_AXIS);

  spec.wait   = 1;
  spec.fresh  = 0;
  auto_t0     = timer0ticks;
  auto_count  = 0;
  spec.autostage = 1;
}

//The cell and its rate from (f0, f_hi), then the confirmation records at that rate
static uint64 auto_f0, auto_fhi;

static void spec_auto_go(void)
{
  spl_cell c;
  int32    r = spec_auto_derive(auto_f0, auto_fhi, &c);

  spec.autostage = 2;
  auto_count = 0;
  spec_rate_set(r);
}

//The cell from the latest measurement and the rate the ladder gives it
static void spec_auto_end(uint64 f0, uint64 fhi)
{
  spl_cell c;
  int32    r = spec_auto_derive(f0, fhi, &c);

  spec_auto_finish(&c, r);
}

//After each record transformed while Auto runs. Stages: 1 probe at 50 MSa/s, 4 period search (10x slower per record
//until 3 periods fit or the slowest rate), 2 confirmation at the derived rate, 3 one refinement.
static void spec_auto_step(void)
{
  uint64 f0, fhi;
  int    timeout = (timer0ticks - auto_t0) > AUTO_TIMEOUT_MS;

  auto_count++;

  //First probe record: trigger level to the middle of the signal; the average starts after it
  if((spec.autostage == 1) && (auto_count == 1))
  {
    if(scopesettings.triggerchannel < 2)
    {
      DISPLAYDATA save = displaydata;

      display_set_screen_buffer((uint16 *)maindisplaybuffer);
      scope_do_50_percent_trigger_setup();
      scope_trigger_settings(0);
      displaydata = save;
    }
    spec_navg[0] = spec_navg[1] = 0;
    return;
  }

  if(timeout)
  {
    spec_auto_measure(&f0, &fhi);
    spec_auto_end(f0 ? f0 : auto_f0, (fhi && (spec.autostage == 1)) ? fhi : (auto_fhi ? auto_fhi : fhi));
    return;
  }

  switch(spec.autostage)
  {
    case 1:
      if(auto_count < 4)
        return;

      spec_auto_measure(&auto_f0, &auto_fhi);

      if(!auto_f0 && (scopesettings.timeperdiv - 3 >= TB_FIRST))
      {
        spec.autostage = 4;
        auto_count = 0;
        spec_tb_set(scopesettings.timeperdiv - 3);
        return;
      }

      spec_auto_go();
      return;

    case 4:
      auto_f0 = spec_f0_mhz(spec_auto_ch(), tb_fs_mhz(scopesettings.timeperdiv));

      if(!auto_f0 && (scopesettings.timeperdiv - 3 >= TB_FIRST))
      {
        auto_count = 0;
        spec_tb_set(scopesettings.timeperdiv - 3);
        return;
      }

      spec_auto_go();
      return;

    case 2:
      if(auto_count < 2)
        return;

      spec_auto_measure(&f0, &fhi);
      if(f0)
        auto_f0 = f0;

      //One refinement when the band seen at this rate is less than half the probe's (the probe sees edges and noise)
      if(fhi && (2 * fhi < auto_fhi))
      {
        spl_cell c;
        int32    r = spec_auto_derive(auto_f0, fhi, &c);

        if((r >= 0) && (r != (int32)scopesettings.samplerate))
        {
          auto_fhi = fhi;
          spec.autostage = 3;
          auto_count = 0;
          spec_rate_set(r);
          return;
        }
      }

      spec_auto_end(auto_f0, auto_fhi);
      return;

    default:
      if(auto_count < 2)
        return;

      spec_auto_measure(&f0, &fhi);
      spec_auto_end(f0 ? f0 : auto_f0, auto_fhi);
      return;
  }
}
//----------------------------------------------------------------------------------------------------------------------------------
// Settings that survive a power cycle (fw 0.2.39): FFT length, window, scale and ref level in two spare words of the
// settings sector (709), saved where 1.00o5 saves everything (power-off interrupt). Since fw 0.2.56 the variant (view)
// too, in word 211 (pc_hal_scope.c). Not restored: the display mode (boot into the scope view), channel, span, start.

uint16 spectrum_settings_word(int i)
{
  if(i == 0)
    return (uint16)(0x5E00 | ((spec.window & 3) << 6) | ((spec.scale & 3) << 4) | ((spec.log2n - 8) & 15));

  return (uint16)(((spec.avg & 7) << 12) | ((uint16)spec.ref & 0x0FFF));
}

void spectrum_settings_restore(uint16 w0, uint16 w1)
{
  int16 ref = (int16)(w1 << 4) >> 4;        //12-bit ref; bits 12-14 averaging (fw 0.2.40)
  uint32 avg = (w1 >> 12) & 7;

  if((w0 & 0xFF00) != 0x5E00)
    return;

  if((((w0 >> 6) & 3) < FX_WIN_COUNT) && (((w0 >> 4) & 3) < sizeof(spec_scales)) && ((w0 & 15) + 8 >= 10) &&
     ((w0 & 15) + 8 <= LOG2N_VIEW_MAX) && (ref >= REF_MIN) && (ref <= REF_MAX))
  {
    spec.window = (w0 >> 6) & 3;
    spec.scale  = (w0 >> 4) & 3;
    spec.log2n  = (w0 & 15) + 8;
    spec.ref    = ref;
  }

  if(avg <= 4)
    spec.avg = (uint8)avg;
}

//A tap on a row of the Analyse fold-out (fw 0.2.56; a strip field before): the one-way rows step to the next value, Ref
//level steps by a division down (dir < 0) or up. Like any touch on the view it ends a running Auto.
void spectrum_param_step(int p, int dir)
{
  int log2n = spec_log2n();
  int lmax = (spec_log2n_max() < LOG2N_VIEW_MAX) ? spec_log2n_max() : LOG2N_VIEW_MAX;
  int up = dir > 0;

  spec.autostage = 0;
  auto_refused = 0;

  switch(p)
  {
    case SPEC_P_CHANNEL:
      //Cycle through what is enabled: CH1, CH2, both
      do { spec.channel = (spec.channel + 1) % 3; }
      while(((spec.channel == SPEC_CH_BOTH) && !(spec_ch_on(0) && spec_ch_on(1))) ||
            ((spec.channel < 2) && !spec_ch_on(spec.channel) && (spec_ch_on(0) || spec_ch_on(1))));
      spec.recompute = 1;
      break;

    case SPEC_P_FFT:
      //1024 .. the longest the record holds (at most 4096), then back to 1024
      spec.log2n = (log2n >= lmax) ? ((lmax > 10) ? 10 : lmax) : log2n + 1;
      spec.recompute = 1;
      break;

    case SPEC_P_WINDOW:
      spec.window = (spec.window == FX_WIN_HANN) ? FX_WIN_FLATTOP : (spec.window == FX_WIN_FLATTOP) ? FX_WIN_RECT : FX_WIN_HANN;
      spec.recompute = 1;
      break;

    case SPEC_P_SCALE:
      spec.scale = (spec.scale + 1) % sizeof(spec_scales);
      break;

    case SPEC_P_REF:
      spec.ref += up ? spec_scales[spec.scale] : -spec_scales[spec.scale];
      if(spec.ref < REF_MIN) spec.ref = REF_MIN;
      if(spec.ref > REF_MAX) spec.ref = REF_MAX;
      break;

    case SPEC_P_AVG:
      spec.avg = (spec.avg + 1) % 5;
      spec.recompute = 1;                    //restart the average now, also when stopped
      break;

    case SPEC_P_RESET:
      spec_defaults();
      break;
  }
}

//AUTO SET on the right bar while the view shows (fw 0.2.51; the strip's Auto field before): a tap starts the view's
//Auto, a tap while it runs cancels it. The label change follows from the next pass (spectrum_display_body).
int spectrum_autoset(void)
{
  if(!spec.active || !spectrum_available())
    return 0;

  if(spec.autostage)
  {
    spec.autostage = 0;
    auto_refused = 0;
  }
  else
    spec_auto_start();

  spec_autoset_button();
  return 1;
}

int spectrum_auto_running(void)
{
  return spec.active && spec.autostage;
}

//Touches in the trace area (fw 0.2.56: the cards row below the plot does nothing). Plot: the scope view's gestures (scan_for_touch in
//statemachine.c): a touch that stays within 2 px and ends within 200 ms is a tap, left of the centre = zoom the span
//out, right of it = zoom in, one span of the ladder (fw 0.2.77); a touch that moves more than 2 px is a drag, and a
//horizontal drag pans the centre (the spectrum follows the finger), redrawn while dragging.
int spectrum_touch(void)
{
  if(!spec.active || !spectrum_available())
    return 0;

  //Any touch ends a running Auto (AUTO SET starts a new one) and clears its refusal warning
  spec.autostage = 0;
  auto_refused = 0;

  //fw 0.2.71: the slider row; the window follows the finger (the span kept, the centre panned), as the Scope's record
  //slider. At the whole axis there is nothing to pan. fw 0.2.73, tap to jump: a touch-down outside the window centres
  //the window there first (the window as drawn, sov_jump), then the drag goes on from the new place. fw 0.2.81: a move
  //is a pan, which keeps the rate (the track is the current axis, its scale fixed during the drag) and is clamped to the
  //track's ends.
  if(ytouch >= BU_SL_HIT_Y)
  {
    spl_cell c0 = spec.cell;
    int32    x0 = xtouch, wx, ww, dx;
    uint64   track;
    SPECVIEW v;

    spec_current_view(&v);
    spec_slider_window(&v, &wx, &ww);
    track = spec_bin_mhz_now() * v.half;
    dx = sov_jump((int32)xtouch, (int32)ytouch, wx, ww);

    if(dx)
    {
      if(spec_pan_by(&c0, ((int64)dx * (int64)track) / BU_SL_W))
        spectrum_display();
      c0 = spec.cell;
    }

    while(havetouch)
    {
      if(spec_pan_by(&c0, (((int64)xtouch - x0) * (int64)track) / BU_SL_W))
        spectrum_display();

      tp_i2c_read_status();
    }

    return 1;
  }

  if(ytouch >= CARDS_Y)
  {
    tp_i2c_wait_for_touch_release();
    return 1;
  }

  {
    uint32   t0 = timer0_get_ticks();
    int32    x0 = xtouch, y0 = ytouch, moved = 0;
    spl_cell c0 = spec.cell;
    int64    span = (int64)spl_span_mhz(c0.span);

    while(havetouch)
    {
      tp_i2c_read_status();

      if(havetouch)
      {
        int32 dx = (int32)xtouch - x0, dy = (int32)ytouch - y0;

        //fw 0.2.64: the plot's dead band (touch_pick.h), and Move slow divides the pan by 5 as every other drag
        if(!moved && (((dx < 0) ? -dx : dx) > TOUCH_DEAD_BAND || ((dy < 0) ? -dy : dy) > TOUCH_DEAD_BAND))
          moved = 1;

        //The spectrum follows the finger: dragging right shows lower frequencies
        if(moved && spec_pan_by(&c0, -((int64)(scopesettings.movespeed ? dx / 5 : dx) * span) / PLOT_W))
          spectrum_display();
      }
    }

    if(!moved && ((timer0_get_ticks() - t0) < TOUCH_TAP_MS))
      spec_zoom(x0 >= PLOT_X + PLOT_W / 2);
  }

  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// READ_SPECTRUM header (32 bytes): u8 channel, u8 window, u8 log2n, u8 flags (bit 0 levels in dBA, bit 1 16-bit record,
// bits 2-3 decimation filter K), u32 frame,
// u32 divider, u32 sample rate Hz, u32 bin width mHz, i32 gain cdB (code-dB to dBV, already applied), u16 bins in the
// spectrum (N/2 + 1), u16 first bin returned, u16 count returned, u16 ENBW milli-bins

int32 spectrum_read(int ch, uint32 first, uint32 max_bins, uint8 *hdr, uint8 *levels)
{
  SPECFRAME *f = &spec_frame[ch & 1];
  uint32     total, n, i;
  uint64     bin;

  if(!spec.active || !spec.have[ch & 1])
    return -1;

  total = (1u << (f->log2n - 1)) + 1;
  if(first >= total)
    first = total;
  n = total - first;
  if(n > max_bins)
    n = max_bins;

  bin = spec_bin_mhz(f->fs_mhz, f->log2n);

  hdr[0] = ch & 1;
  hdr[1] = f->window;
  hdr[2] = f->log2n;
  hdr[3] = (uint8)(f->unit_amps | (f->wide << 1) | (f->k << 2) | (spec.avg << 4));
  put32(hdr + 4, spec.frames);
  put32(hdr + 8, f->divider);
  put32(hdr + 12, (uint32)((f->fs_mhz + 500) / 1000));
  put32(hdr + 16, (uint32)bin);
  put32(hdr + 20, (uint32)f->gain_cdb);
  put16(hdr + 24, total);
  put16(hdr + 26, first);
  put16(hdr + 28, n);
  put16(hdr + 30, fx_enbw_milli(f->window));

  for(i = 0; i < n; i++)
    put16(levels + 2 * i, (uint16)spec_db[ch & 1][first + i]);

  return (int32)n;
}
