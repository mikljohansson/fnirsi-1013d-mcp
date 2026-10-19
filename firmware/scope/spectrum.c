//----------------------------------------------------------------------------------------------------------------------------------
// spectrum.c - on-scope spectrum analyser (protocol builds only, see spectrum.h).
//   fw 0.2.27: FFT_BENCH (C2). fw 0.2.28: the spectrum display mode (S1).
//
// Layout of the trace area while the mode is on (x 2..729, y 48..479):
//   y  50, 64   info lines: sample rate, N, window, RBW; peak readouts in the channel colours
//   y  80..416  plot, 10 x 8 divisions of 72 x 42 px (x 6..726)
//   y 419       frequency labels: start, centre, stop
//   y 436..479  parameter strip: one tap field per setting
// Integer only (no FPU): levels in centi-dB, frequencies in mHz (uint64).
//----------------------------------------------------------------------------------------------------------------------------------

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
#include "menu.h"
#include "pc_hooks.h"

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
#define PLOT_Y                66
#define PLOT_H                344
#define PLOT_XDIV             10
#define PLOT_YDIV             8
#define INFO_Y1               50
#define FLABEL_Y              413
#define STRIP_Y               436
#define STRIP_H               44

#define ZOOM_MAX              6           //span down to 1/64 of the Nyquist range
#define START_STEPS           65536u      //start position in 1/65536 of the Nyquist range (fine enough to drag)
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
  uint8  zoom;          //span = Nyquist range >> zoom
  int16  ref;           //level at the top of the plot, dBV
  uint32 start;         //span start in START_STEPS of the Nyquist range
  uint8  fresh;         //trace buffers refilled since the last FFT
  uint8  recompute;     //an FFT setting changed
  uint8  plan_ok;       //plan matches log2n / window
  uint8  have[2];       //spec_db[ch] holds a spectrum
  uint32 frames;        //spectra computed
  uint8  view;          //VIEW_*
  uint8  wait;          //the time base changed: ignore the trace buffers until a new capture
  uint8  autostage;     //Auto in progress: 1 at the full-axis rate, 2 at the chosen rate
  uint8  avg;           //trace averaging: 0 off, 1..3 over 4 / 16 / 64 captures, 4 max-hold
} SPECSTATE;

#define VIEW_SPECTRUM         0
#define VIEW_WATERFALL        1
#define VIEW_DENSITY          2

static SPECSTATE spec = { 0, 0, 11, FX_WIN_HANN, 1, 0, 0, 0, 0, 0, 0, { 0, 0 }, 0, VIEW_SPECTRUM, 0, 0, 1 };

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
  int32  top;           //centi-dBV at the top edge
  int32  cdb_per_div;
} SPECVIEW;

//The displayed axis ends at PC_SPECTRUM_FMAX_HZ or fs/2, whichever is lower (owner: the front end rolls off near
//30 MHz, above 25 MHz there are only artefacts); span, start and the Auto band all live inside it
static void spec_view(SPECVIEW *v, int log2n, uint64 bin_mhz)
{
  uint32 maxfirst;
  uint64 capbins = bin_mhz ? ((uint64)PC_SPECTRUM_FMAX_HZ * 1000) / bin_mhz : 0;

  v->half = 1u << (log2n - 1);
  if(capbins < v->half)
    v->half = (capbins < 16) ? 16 : (uint32)capbins;
  v->span = v->half >> spec.zoom;
  if(v->span < 8)
    v->span = 8;

  v->first = (uint32)(((uint64)spec.start * v->half) / START_STEPS);
  maxfirst = v->half - v->span;
  if(v->first > maxfirst)
    v->first = maxfirst;

  v->top = spec.ref * 100;
  v->cdb_per_div = spec_scales[spec.scale] * 100;
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

static void spec_draw_grid(const SPECVIEW *v)
{
  char   t[24];
  uint32 i;

  display_set_fg_color(spec_grid_color());

  for(i = 0; i <= PLOT_YDIV; i++)
    display_draw_horz_dots(PLOT_Y + i * (PLOT_H / PLOT_YDIV), PLOT_X, PLOT_X + PLOT_W, 4);

  for(i = 0; i <= PLOT_XDIV; i++)
    display_draw_vert_dots(PLOT_X + i * (PLOT_W / PLOT_XDIV), PLOT_Y, PLOT_Y + PLOT_H, 4);

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

static void spec_draw_freq_labels(const SPECVIEW *v, uint64 bin_mhz)
{
  char t[24];

  display_set_font(&font_2);
  display_set_fg_color(0x00A0A0A0);

  fmt_freq(t, bin_mhz * v->first);
  display_text(PLOT_X, FLABEL_Y, (int8 *)t);

  fmt_freq(t, bin_mhz * v->first + (bin_mhz * v->span) / 2);
  text_center(PLOT_X + PLOT_W / 2, FLABEL_Y, t);

  fmt_freq(t, bin_mhz * (v->first + v->span));
  text_right(PLOT_X + PLOT_W, FLABEL_Y, t);
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

//Text at x, returns the x after it
static uint32 spec_text_at(uint32 x, uint32 y, const char *t)
{
  display_text(x, y, (int8 *)t);
  return x + text_width(t);
}

//The peak marker glyph (font_2 has no triangle): 7 px wide, in the current colour; returns its advance
static uint32 spec_glyph_marker(uint32 x, uint32 y)
{
  uint32 r;

  for(r = 0; r < 4; r++)
    display_draw_horz_line(y + 5 + r, x + 3 - r, x + 3 + r);

  return 10;
}

static const char *spec_avg_names[] = { "", "Avg 4", "Avg 16", "Avg 64", "Max" };

//The one header line (owner, fw 0.2.40-0.2.42), left to right: per shown channel its peak "CH1 ^ 977 Hz  +2.7 dBV" in
//the channel colour, then "RBW 732 Hz", the decimation filter tag ("CIC3", "CIC3+FIR" while the FIR is active, no N;
//k bits 1:0 the order, bit 2 the FIR; the filter lives in the acquisition
//menu) and, in the waterfall, the time its rows span. Nothing that repeats a strip field (no fs, N or averaging).
static void spec_draw_header(const uint32 *peak, int hch, int k, uint64 bin_mhz)
{
  char   t[48], *p;
  uint32 x = PLOT_X;
  int    ch;

  display_set_font(&font_2);

  for(ch = 0; ch < 2; ch++)
  {
    if(!spec.have[ch] || !peak[ch] || ((spec.view != VIEW_SPECTRUM) && (ch != hch)))
      continue;

    display_set_fg_color(ch ? CHANNEL2_COLOR : CHANNEL1_COLOR);
    x = spec_text_at(x, INFO_Y1, ch ? "CH2 " : "CH1 ");
    x += spec_glyph_marker(x, INFO_Y1);
    p = fmt_freq(t, bin_mhz * peak[ch]);
    p = fmt_str(p, "  ");
    p = fmt_c1(p, spec_db[ch][peak[ch]]);
    fmt_str(p, spec_frame[ch].unit_amps ? " dBA" : " dBV");
    x = spec_text_at(x, INFO_Y1, t) + 18;
  }

  display_set_fg_color(0x00C0C0C0);
  p = fmt_str(t, "RBW ");
  fmt_freq(p, (bin_mhz * fx_enbw_milli(spec.window) + 500) / 1000);
  x = spec_text_at(x, INFO_Y1, t) + 18;

  if(k & 3)
  {
    static const char *names[] = { "", "boxcar", "CIC2", "CIC3", "", "boxcar+FIR", "CIC2+FIR", "CIC3+FIR" };

    x = spec_text_at(x, INFO_Y1, names[k & 7]) + 12;
  }

  //Waterfall: the time the visible rows span, oldest at the bottom
  if((spec.view == VIEW_WATERFALL) && (wf_count > 1))
  {
    uint32 oldest = (wf_head + HIST_H - wf_count) % HIST_H;

    p = fmt_c1(t, (int32)((timer0ticks - wf_ms[oldest]) / 10));
    fmt_str(p, " s of rows");
    spec_text_at(x + 6, INFO_Y1, t);
  }
}

//fw 0.2.51: choosing the Spectrum analyser row in roll mode switched Roll off; the warning line says so for a while
#define ROLL_NOTE_MS          5000
static uint8  roll_note;
static uint32 roll_note_ms;

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
  else if(auto_refused == 1)
    p = fmt_str(p, "Auto needs RUN: press RUN   ");
  else if(auto_refused == 2)
    p = fmt_str(p, "Auto needs trigger mode Auto or Normal   ");

  if(spec.wait && !spec.autostage && scopesettings.runstate)
    p = fmt_str(p, "Waiting for a capture at the new rate   ");

  if(spec_untrig && scopesettings.runstate && !spec.autostage)
    p = fmt_str(p, "No trigger: check the trigger level   ");

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

static int spec_hist_ch(void)
{
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

//Colour legend in the second info line, right side: bottom level, the palette, top level
static void spec_draw_legend(const SPECVIEW *v, int ch)
{
  char   t[24], *p;
  uint32 x, y, x0 = PLOT_X + PLOT_W - 40 - 128;

  if(!spec_pal_ok)
    spec_palette();

  for(y = INFO_Y1 + 2; y < INFO_Y1 + 11; y++)
    for(x = 0; x < 128; x++)
      displaybuffertmp[y * 800 + x0 + x] = (spec.view == VIEW_DENSITY) ? dens_pal[40 + ((2 * x * 215) >> 8)] : spec_pal[2 * x];

  //Density: rare / old on the left, frequent on the right (white)
  if(spec.view == VIEW_DENSITY)
  {
    display_set_font(&font_2);
    display_set_fg_color(0x00A0A0A0);
    text_right(x0 - 4, INFO_Y1, "rare or old");
    display_text(x0 + 132, INFO_Y1, (int8 *)"frequent");
    return;
  }

  display_set_font(&font_2);
  display_set_fg_color(0x00A0A0A0);
  fmt_int(t, (v->top - PLOT_YDIV * v->cdb_per_div) / 100);
  text_right(x0 - 4, INFO_Y1, t);
  p = fmt_int(t, v->top / 100);
  fmt_str(p, spec_frame[ch].unit_amps ? " dBA" : " dBV");
  display_text(x0 + 132, INFO_Y1, (int8 *)t);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Parameter strip: one tap field per setting. "<" / ">" fields step down on the left half and up on the right half.

#define F_CHANNEL   0
#define F_FFT       1
#define F_WINDOW    2
#define F_SCALE     3
#define F_REF       4
#define F_AVG       5
#define F_RESET     6
#define F_VIEW      7
#define F_COUNT     8               //fw 0.2.51: Auto is AUTO SET on the right bar, Scope the main menu's row

static const char *spec_field_label[F_COUNT] = { "Channel", "FFT length", "Window", "Scale", "Ref level", "Average", "", "View" };
static const uint8 spec_field_twoway[F_COUNT] = { 0, 0, 0, 0, 1, 0, 0, 0 };

#define FIELD_W     (PLOT_W / F_COUNT)

static void spec_field_value(int f, char *t, int log2n, uint64 bin_mhz)
{
  static const char *wins[] = { "Rect", "Hann", "Flat top" };
  char    *p;

  (void)bin_mhz;

  switch(f)
  {
    case F_CHANNEL:
      fmt_str(t, (spec.channel == SPEC_CH_BOTH) ? "CH1 + CH2" : spec.channel ? "CH2" : "CH1");
      break;

    case F_FFT:
      fmt_uint(t, 1u << log2n);
      break;

    case F_WINDOW:
      fmt_str(t, wins[spec.window]);
      break;

    case F_SCALE:
      p = fmt_uint(t, spec_scales[spec.scale]);
      fmt_str(p, " dB/div");
      break;

    case F_REF:
      p = fmt_int(t, spec.ref);
      fmt_str(p, spec_frame[spec.channel == 1].unit_amps ? " dBA" : " dBV");
      break;

    case F_AVG:
      fmt_str(t, spec.avg ? ((spec.avg == 4) ? "Max hold" : spec_avg_names[spec.avg] + 4) : "Off");
      break;

    case F_RESET:
      fmt_str(t, "Reset");
      break;

    default:
      fmt_str(t, (spec.view == VIEW_WATERFALL) ? "Waterfall" : (spec.view == VIEW_DENSITY) ? "Density" : "Spectrum");
      break;
  }
}

static void spec_draw_strip(int log2n, uint64 bin_mhz)
{
  char t[24];
  int  f;

  display_set_fg_color(DARKGREY_COLOR);
  display_fill_rect(2, STRIP_Y, 728, STRIP_H);

  for(f = 0; f < F_COUNT; f++)
  {
    uint32 x = PLOT_X + f * FIELD_W;

    display_set_fg_color(GREY_COLOR);
    display_draw_rounded_rect(x + 1, STRIP_Y + 3, FIELD_W - 3, STRIP_H - 6, 3);

    display_set_font(&font_2);
    display_set_fg_color(0x00A0A0A0);
    text_center(x + FIELD_W / 2, STRIP_Y + 6, (char *)spec_field_label[f]);

    //Two-way fields: the left half steps down, the right half up; the hints sit on the label line
    if(spec_field_twoway[f])
    {
      display_text(x + 6, STRIP_Y + 6, (int8 *)"<");
      text_right(x + FIELD_W - 6, STRIP_Y + 6, ">");
    }

    spec_field_value(f, t, log2n, bin_mhz);
    display_set_font(&font_3);
    display_set_fg_color(WHITE_COLOR);
    text_center(x + FIELD_W / 2, STRIP_Y + (spec_field_label[f][0] ? 20 : 14), t);
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
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  scope_auto_set_button(0);
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

  if(spec.view == VIEW_WATERFALL)
    spec_draw_waterfall();
  else
  {
    if(spec.view == VIEW_DENSITY)
      spec_draw_density();
    spec_draw_grid(&v);
  }

  spec_draw_freq_labels(&v, bin_mhz);
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
    spec_draw_legend(&v, hch);
  }

  spec_draw_header(peak, hch, dc.k | (dc.fir ? 4 : 0), bin_mhz);

  spec_draw_strip(log2n, bin_mhz);

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
// Time base coupling (fw 0.2.39, owner): the span drives the sample rate like a spectrum analyser. It is the scope's
// own time base (same setting), stepped through the short time bases 11..34 (pc_hal_timebase_write, as SET_TIMEBASE).
// After a change the trace buffers still hold the old rate's record, so the view waits for the first new capture.

#define TB_FIRST              11          //short time bases (the roll-mode ones below would leave the view)
#define TB_LAST               ((int)(sizeof(time_per_div_sample_rate) / sizeof(time_per_div_sample_rate[0])) - 1)
//Zooming in at a span of fs/16 or less (N/16 bins: 128 at 2048, 256 at 4096) steps to a slower rate instead

static uint32 tb_divider(int i)
{
  uint8_t  sr;
  uint32_t d;

  return pc_hal_timebase_lookup((uint8)i, &sr, &d) ? 0xFFFFFFFF : d;
}

//Time base index of the next faster (dir 1) or slower (dir -1) sample rate, -1 when there is none
static int spec_tb_next(int dir)
{
  int    i;
  uint32 d0 = tb_divider(scopesettings.timeperdiv);

  for(i = scopesettings.timeperdiv + dir; (i >= TB_FIRST) && (i <= TB_LAST); i += dir)
  {
    uint32 d = tb_divider(i);

    if((dir > 0) ? (d < d0) : (d > d0))
      return i;
  }

  return -1;
}

static void spec_tb_set(int i)
{
  pc_hw_timebase t;

  if((i < TB_FIRST) || (i > TB_LAST) || (i == scopesettings.timeperdiv))
    return;

  t.timeperdiv = (uint8)i;
  if(pc_hal_timebase_lookup((uint8)i, &t.samplerate, &t.divider))
    return;

  //Wait for a record at the new rate (anything finishing now was armed before the change). Set before the write: its
  //redraw (scope_preset_values -> spectrum_display) then shows the new axis without the old spectra
  spec.wait = 1;
  spec.fresh = 0;
  spec.have[0] = spec.have[1] = 0;

  pc_hal_timebase_write(&t, 0);
}

//Sample rate in mHz of a time base index, ignoring the decimation filter's even-divider rounding
static uint64 tb_fs_mhz(int i)
{
  uint32 d = tb_divider(i);

  return (d == 0xFFFFFFFF) ? 0 : 200000000000ull / (d + 1);
}

//Bins across the capped axis for a rate
static uint32 spec_axis_bins(uint64 fs_mhz, int log2n)
{
  SPECVIEW v;
  uint32 z = spec.zoom, s = spec.start;

  spec.zoom = 0;
  spec.start = 0;
  spec_view(&v, log2n, spec_bin_mhz(fs_mhz, log2n));
  spec.zoom = z;
  spec.start = s;
  return v.half;
}

//Show span_mhz about centre_mhz on the axis of rate fs_mhz: the zoom step nearest to that span, clamped to the axis
static void spec_place(uint64 fs_mhz, uint64 centre_mhz, uint64 span_mhz)
{
  int    log2n = spec_log2n(), z;
  uint64 bin = spec_bin_mhz(fs_mhz, log2n);
  uint32 half = spec_axis_bins(fs_mhz, log2n);
  uint64 want = bin ? span_mhz / bin : half;
  int64  c, w, s;

  for(z = 0; (z < ZOOM_MAX) && ((half >> (z + 1)) >= want) && ((half >> (z + 1)) >= 8); z++) {}
  spec.zoom = z;

  w = START_STEPS >> z;
  c = bin ? (int64)(((uint64)centre_mhz * START_STEPS) / ((uint64)bin * half)) : 0;
  s = c - w / 2;
  if(s < 0) s = 0;
  if(s > (int64)START_STEPS - w) s = START_STEPS - w;
  spec.start = (uint32)s;
}

//The current view's centre and span in mHz
static void spec_centre_span(uint64 *centre, uint64 *span)
{
  int     log2n = spec_log2n();
  pc_decim_cfg dc;
  int     written;
  uint64  bin;
  SPECVIEW v;

  pc_hal_decim_current(&dc, &written);
  bin = spec_bin_mhz(spec_fs_mhz(spec_divider(), &dc), log2n);
  spec_view(&v, log2n, bin);
  *span = bin * v.span;
  *centre = bin * v.first + *span / 2;
}

//Span: one zoom step narrower (in) or wider (out) about the centre. Past the widest span (the whole axis) a zoom out
//steps to the next faster rate until fs/2 reaches PC_SPECTRUM_FMAX_HZ; at a span of fs/16 or less a zoom in steps to the next
//slower rate, keeping the centre and halving the span in Hz.
static void spec_zoom(int in)
{
  SPECVIEW v;
  uint64   centre, span;
  int      log2n = spec_log2n(), next;
  pc_decim_cfg dc;
  int      written;

  pc_hal_decim_current(&dc, &written);
  spec_view(&v, log2n, spec_bin_mhz(spec_fs_mhz(spec_divider(), &dc), log2n));

  if(!in && !spec.zoom)
  {
    //Whole axis shown: a faster rate widens it, until the axis is the full PC_SPECTRUM_FMAX_HZ
    if((tb_fs_mhz(scopesettings.timeperdiv) / 2 < (uint64)PC_SPECTRUM_FMAX_HZ * 1000) && ((next = spec_tb_next(1)) >= 0))
      spec_tb_set(next);
    return;
  }

  if(in && (v.span <= ((1u << log2n) / 16)))
  {
    spec_centre_span(&centre, &span);
    next = spec_tb_next(-1);
    if((next >= 0) && (centre + span / 4 < tb_fs_mhz(next) / 2))
    {
      spec_tb_set(next);
      spec_place(tb_fs_mhz(next), centre, span / 2);
      return;
    }
    //No slower rate holds the centre: plain zoom down to the last step
  }

  {
    uint32 centre_u = spec.start + ((START_STEPS >> spec.zoom) / 2);

    if(in && (spec.zoom < ZOOM_MAX)) spec.zoom++;
    else if(!in && spec.zoom) spec.zoom--;

    spec.start = (centre_u > ((START_STEPS >> spec.zoom) / 2)) ? centre_u - ((START_STEPS >> spec.zoom) / 2) : 0;
    if(spec.start > START_STEPS - (START_STEPS >> spec.zoom))
      spec.start = START_STEPS - (START_STEPS >> spec.zoom);
  }
}

//Pan: the spectrum follows the finger, dx pixels from where the drag began (start0)
static void spec_pan(uint32 start0, int32 dx)
{
  int64 s = (int64)start0 - ((int64)dx * (START_STEPS >> spec.zoom)) / PLOT_W;
  int64 smax = START_STEPS - (START_STEPS >> spec.zoom);

  spec.start = (uint32)((s < 0) ? 0 : (s > smax) ? smax : s);
}

//What the view starts with (Reset): 2048 points, Hann, 10 dB/div, ref 0 dBV, full span, spectrum. The time base
//is the scope's and stays.
static void spec_defaults(void)
{
  spec.log2n  = 11;
  spec.window = FX_WIN_HANN;
  spec.scale  = 1;
  spec.ref    = 0;
  spec.zoom   = 0;
  spec.start  = 0;
  spec.view   = VIEW_SPECTRUM;
  spec.avg    = 1;
  spec.autostage = 0;
  spec.recompute = 1;
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
//  derive  span = max(1.25 f_hi, 20 f0) up to 25 MHz; rate = slowest with fs/2 >= 1.25 span; with f0 > 0 slower still
//          until RBW <= f0 / 4 (span then trimmed to the axis)
//  refine  2 records at that rate; if f_hi moved by more than 2x or f0 changed the rate, derive once more (3rd stage)
//  final   ref = first scale multiple >= peak + 3 dB, scale from peak - floor, span from 0, averaging 4
//Untriggered records are skipped while a triggered one came (spectrum_capture_info); any touch cancels; it gives up
//after 4 s with what it has. Stopped or in single mode it does not run (warning says what to do).

#define TB_FULL_AXIS          26          //2 us/div, 50 MSa/s: fs/2 = 25 MHz
#define AUTO_TIMEOUT_MS       4000

static uint32 auto_t0, auto_count;
static uint8  auto_refused;               //1: stopped, 2: single mode (warning while the strip shows it)

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
  uint32  z = spec.zoom, st = spec.start;

  pc_hal_decim_current(&dc, &written);
  fs  = spec_fs_mhz(spec_divider(), &dc);
  bin = spec_bin_mhz(fs, log2n);
  spec.zoom = 0;
  spec.start = 0;
  spec_view(&v, log2n, bin);
  spec.zoom = z;
  spec.start = st;

  *fhi = (spec_analyse(&v, &P, &F, &lo, &hi) && hi) ? bin * hi : 0;
  *f0  = spec_f0_mhz(spec_auto_ch(), fs);
}

//Rate (time base index) and span (mHz) from (f0, f_hi)
static int spec_auto_derive(uint64 f0, uint64 fhi, uint64 *span)
{
  uint64 fmax = (uint64)PC_SPECTRUM_FMAX_HZ * 1000, s = fhi + fhi / 4;
  int    i, log2n = spec_log2n();

  if(20 * f0 > s)
    s = 20 * f0;
  if(!s || (s > fmax))
    s = fmax;

  for(i = TB_FIRST; i < TB_FULL_AXIS; i++)
    if(tb_fs_mhz(i) / 2 >= s + s / 4)
      break;

  //Resolve the fundamental: RBW = 1.5 fs / N <= f0 / 4
  if(f0)
    while((i > TB_FIRST) && ((3 * tb_fs_mhz(i)) / (2u << log2n) > f0 / 4))
      i--;

  if(s > (tb_fs_mhz(i) / 2) * 4 / 5)
    s = (tb_fs_mhz(i) / 2) * 4 / 5;

  *span = s;
  return i;
}

static void spec_auto_finish(uint64 span)
{
  pc_decim_cfg dc;
  int     written, log2n = spec_log2n();
  uint64  fs;
  uint32  lo, hi;
  int32   P, F, range, scale;
  SPECVIEW v;

  spec.autostage = 0;
  spec.zoom = 0;
  spec.start = 0;

  pc_hal_decim_current(&dc, &written);
  fs = spec_fs_mhz(spec_divider(), &dc);
  spec_view(&v, log2n, spec_bin_mhz(fs, log2n));

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

  spec_place(fs, span / 2, span);
  spec.start = 0;
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
  spec.zoom   = 0;
  spec.start  = 0;
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

//Rate and span from (f0, f_hi), then the confirmation records at that rate
static uint64 auto_f0, auto_fhi, auto_span;

static void spec_auto_go(void)
{
  int idx = spec_auto_derive(auto_f0, auto_fhi, &auto_span);

  spec.autostage = 2;
  auto_count = 0;
  if(idx != scopesettings.timeperdiv)
    spec_tb_set(idx);
}

//Span for the rate in use: derived from the latest measurement, trimmed to this rate's axis
static void spec_auto_end(uint64 f0, uint64 fhi)
{
  uint64 span, axis = (tb_fs_mhz(scopesettings.timeperdiv) / 2) * 4 / 5;

  spec_auto_derive(f0, fhi, &span);
  if(span > axis)
    span = axis;
  spec_auto_finish(span);
}

//After each record transformed while Auto runs. Stages: 1 probe at 50 MSa/s, 4 period search (10x slower per record
//until 3 periods fit or the slowest rate), 2 confirmation at the derived rate, 3 one refinement.
static void spec_auto_step(void)
{
  uint64 f0, fhi;
  int    idx, timeout = (timer0ticks - auto_t0) > AUTO_TIMEOUT_MS;

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
        uint64 span;

        idx = spec_auto_derive(auto_f0, fhi, &span);
        if(idx != scopesettings.timeperdiv)
        {
          auto_fhi = fhi;
          spec.autostage = 3;
          auto_count = 0;
          spec_tb_set(idx);
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
// settings sector (709), saved where 1.00o5 saves everything (power-off interrupt). Not restored: the display mode
// (boot into the scope view), channel, span, start, view.

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

static void spec_step_field(int f, int up)
{
  int log2n = spec_log2n();
  int lmax = (spec_log2n_max() < LOG2N_VIEW_MAX) ? spec_log2n_max() : LOG2N_VIEW_MAX;

  switch(f)
  {
    case F_CHANNEL:
      //Cycle through what is enabled: CH1, CH2, both
      do { spec.channel = (spec.channel + 1) % 3; }
      while(((spec.channel == SPEC_CH_BOTH) && !(spec_ch_on(0) && spec_ch_on(1))) ||
            ((spec.channel < 2) && !spec_ch_on(spec.channel) && (spec_ch_on(0) || spec_ch_on(1))));
      spec.recompute = 1;
      break;

    case F_FFT:
      //1024 .. the longest the record holds (at most 4096), then back to 1024
      spec.log2n = (log2n >= lmax) ? ((lmax > 10) ? 10 : lmax) : log2n + 1;
      spec.recompute = 1;
      break;

    case F_WINDOW:
      spec.window = (spec.window == FX_WIN_HANN) ? FX_WIN_FLATTOP : (spec.window == FX_WIN_FLATTOP) ? FX_WIN_RECT : FX_WIN_HANN;
      spec.recompute = 1;
      break;

    case F_SCALE:
      spec.scale = (spec.scale + 1) % sizeof(spec_scales);
      break;

    case F_REF:
      spec.ref += up ? spec_scales[spec.scale] : -spec_scales[spec.scale];
      if(spec.ref < REF_MIN) spec.ref = REF_MIN;
      if(spec.ref > REF_MAX) spec.ref = REF_MAX;
      break;

    case F_AVG:
      spec.avg = (spec.avg + 1) % 5;
      spec.recompute = 1;                    //restart the average now, also when stopped
      break;

    case F_RESET:
      spec_defaults();
      break;

    case F_VIEW:
      spec.view = (spec.view + 1) % 3;
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

//Touches in the trace area. Strip: one tap per field. Plot: the scope view's gestures (scan_for_touch in
//statemachine.c): a touch that stays within 2 px and ends within 200 ms is a tap, left of the centre = zoom the span
//out, right of it = zoom in; a touch that moves more than 2 px is a drag, and a horizontal drag pans the start
//frequency (the spectrum follows the finger), redrawn while dragging.
int spectrum_touch(void)
{
  if(!spec.active || !spectrum_available())
    return 0;

  //Any touch ends a running Auto (AUTO SET starts a new one) and clears its refusal warning
  spec.autostage = 0;
  auto_refused = 0;

  if(ytouch >= STRIP_Y)
  {
    if((xtouch >= PLOT_X) && (xtouch < PLOT_X + PLOT_W))
    {
      int    f = (xtouch - PLOT_X) / FIELD_W;
      uint32 x = (xtouch - PLOT_X) % FIELD_W;

      if(f < F_COUNT)
        spec_step_field(f, x >= FIELD_W / 2);
    }

    tp_i2c_wait_for_touch_release();
    return 1;
  }

  {
    uint32 t0 = timer0_get_ticks(), start0 = spec.start;
    int32  x0 = xtouch, y0 = ytouch, moved = 0;

    while(havetouch)
    {
      tp_i2c_read_status();

      if(havetouch)
      {
        int32 dx = (int32)xtouch - x0, dy = (int32)ytouch - y0;

        if(!moved && (((dx < 0) ? -dx : dx) > 2 || ((dy < 0) ? -dy : dy) > 2))
          moved = 1;

        if(moved && spec.zoom)
        {
          uint32 before = spec.start;

          spec_pan(start0, dx);
          if(spec.start != before)
            spectrum_display();
        }
      }
    }

    if(!moved && ((timer0_get_ticks() - t0) < 200))
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
