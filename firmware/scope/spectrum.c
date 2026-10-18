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

extern DISPLAYDATA displaydata;     //display_lib.c (current font for text_width)

//Work memory in DDR (.bss, uncached: the MMU and so the D-cache are off)
static int32 spec_win[FX_FFT_WIN_WORDS(FX_FFT_LOG2_MAX)] __attribute__((aligned(8)));
static int32 spec_buf[FX_FFT_BUF_WORDS(FX_FFT_LOG2_MAX)] __attribute__((aligned(8)));
static int16 spec_cdb[(FX_FFT_N_MAX / 2) + 1];

//On-chip SRAM A (32 KB at 0): at run time only the low vectors (0x00-0x3F) live there; the bootloader's stack (and the
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
#define PLOT_Y                80
#define PLOT_H                336
#define PLOT_XDIV             10
#define PLOT_YDIV             8
#define INFO_Y1               50
#define INFO_Y2               64
#define FLABEL_Y              419
#define STRIP_Y               436
#define STRIP_H               44

#define ZOOM_MAX              6           //span down to 1/64 of the Nyquist range
#define START_STEPS           1024u       //start position in 1/1024 of the Nyquist range
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
  uint16 start;         //span start in 1/1024 of the Nyquist range
  uint8  fresh;         //trace buffers refilled since the last FFT
  uint8  recompute;     //an FFT setting changed
  uint8  plan_ok;       //plan matches log2n / window
  uint8  have[2];       //spec_db[ch] holds a spectrum
  uint32 frames;        //spectra computed
} SPECSTATE;

static SPECSTATE spec = { 0, 0, 12, FX_WIN_HANN, 1, 0, 0, 0, 0, 0, 0, { 0, 0 }, 0 };

//What the last FFT used, per channel (READ_SPECTRUM reports these)
typedef struct
{
  uint8  log2n;
  uint8  window;
  uint8  unit_amps;     //channel in A mode
  uint32 divider;       //fs = 200 MHz / (divider + 1)
  int32  gain_cdb;      //centi-dB from "dB re 1 code RMS" to dBV
} SPECFRAME;

static FX_FFT    spec_plan;
static SPECFRAME spec_frame[2];
static int16     spec_db[2][(FX_FFT_N_MAX / 2) + 1];    //centi-dBV

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

  return (spec.log2n < m) ? spec.log2n : m;
}

//Bin width in mHz: 200e6 Hz / ((divider + 1) N)
static uint64 spec_bin_mhz(uint32 divider, int log2n)
{
  return (200000000000ull / (divider + 1)) >> log2n;
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
  int    log2n = spec_log2n();
  uint32 half = 1u << (log2n - 1);
  int    ch;

  if(!spec.plan_ok || (spec_plan.log2n != log2n) || (spec_plan.window != spec.window))
  {
    fx_fft_init(&spec_plan, log2n, spec.window, spec_win, spec_buf);
    spec.plan_ok = 1;
  }

  for(ch = 0; ch < 2; ch++)
  {
    PCHANNELSETTINGS s = spec_ch(ch);
    SPECFRAME *f = &spec_frame[ch];
    int32  g;

    spec.have[ch] = 0;

    if(!s->enable || ((spec.channel != SPEC_CH_BOTH) && (spec.channel != ch)))
      continue;

    //Levels straight into dBV: the volts-per-code term rides along as the dB offset
    g = spec_gain_cdb(s);
    fx_fft_run_u8(&spec_plan, s->tracebuffer, spec_zero_code(s));
    fx_fft_cdb(&spec_plan, 0, half + 1, g, spec_db[ch]);

    f->log2n     = log2n;
    f->window    = spec.window;
    f->unit_amps = s->V_A ? 1 : 0;
    f->divider   = spec_divider();
    f->gain_cdb  = g;
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

static void spec_view(SPECVIEW *v, int log2n)
{
  uint32 maxfirst;

  v->half = 1u << (log2n - 1);
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

static void spec_draw_peak(uint32 y, int ch, uint32 peak, uint64 bin_mhz)
{
  char t[48], *p;

  if(!peak)
    return;

  display_set_font(&font_2);
  display_set_fg_color(ch ? CHANNEL2_COLOR : CHANNEL1_COLOR);
  p = fmt_str(t, ch ? "CH2 peak " : "CH1 peak ");
  p = fmt_freq(p, bin_mhz * peak);
  p = fmt_str(p, "  ");
  p = fmt_c1(p, spec_db[ch][peak]);
  fmt_str(p, spec_frame[ch].unit_amps ? " dBA" : " dBV");
  text_right(PLOT_X + PLOT_W, y, t);
}

static void spec_draw_info(int log2n, uint32 divider, uint64 bin_mhz)
{
  char t[96], *p;

  display_set_font(&font_2);
  display_set_fg_color(WHITE_COLOR);

  p = fmt_str(t, "fs ");
  p = fmt_freq_units(p, 200000000000ull / (divider + 1), 1);
  p = fmt_str(p, "   N ");
  p = fmt_uint(p, 1u << log2n);
  p = fmt_str(p, "   RBW ");
  p = fmt_freq(p, (bin_mhz * fx_enbw_milli(spec.window) + 500) / 1000);
  display_text(PLOT_X, INFO_Y1, (int8 *)t);

  //Decimated rates skip samples: anything above fs/2 folds into the plot
  if(divider)
  {
    display_set_fg_color(ORANGE_COLOR);
    display_text(PLOT_X, INFO_Y2, (int8 *)"no anti-alias filter: signals above fs/2 fold back");
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Parameter strip: one tap field per setting. "<" / ">" fields step down on the left half and up on the right half.

#define F_CHANNEL   0
#define F_FFT       1
#define F_WINDOW    2
#define F_SCALE     3
#define F_REF       4
#define F_SPAN      5
#define F_START     6
#define F_EXIT      7
#define F_COUNT     8

static const char *spec_field_label[F_COUNT] = { "Channel", "FFT length", "Window", "Scale", "Ref level", "Span", "Start", "" };
static const uint8 spec_field_twoway[F_COUNT] = { 0, 0, 0, 0, 1, 1, 1, 0 };

#define FIELD_W     (PLOT_W / F_COUNT)

static void spec_field_value(int f, char *t, int log2n, uint64 bin_mhz)
{
  static const char *wins[] = { "Rect", "Hann", "Flat top" };
  SPECVIEW v;
  char    *p;

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

    case F_SPAN:
      spec_view(&v, log2n);
      fmt_freq(t, bin_mhz * v.span);
      break;

    case F_START:
      spec_view(&v, log2n);
      fmt_freq(t, bin_mhz * v.first);
      break;

    default:
      fmt_str(t, "Scope");
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
    display_set_fg_color((f == F_EXIT) ? YELLOW_COLOR : WHITE_COLOR);
    text_center(x + FIELD_W / 2, STRIP_Y + ((f == F_EXIT) ? 14 : 20), t);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

int spectrum_mode(void)
{
  return spec.active ? SPECTRUM_MODE_SPECTRUM : SPECTRUM_MODE_SCOPE;
}

int spectrum_available(void)
{
  return !scopesettings.long_mode && !scopesettings.waveviewmode;
}

void spectrum_set_active(int on)
{
  spec.active = (on && spectrum_available()) ? 1 : 0;
  spec.recompute = 1;
  spec.have[0] = spec.have[1] = 0;
}

void spectrum_new_data(void)
{
  spec.fresh = 1;
}

void spectrum_service(void)
{
  if(spec.active && !spectrum_available())
    spectrum_set_active(0);
}

int spectrum_display(void)
{
  int     log2n, ch;
  uint32  divider, peak[2] = { 0, 0 };
  uint64  bin_mhz;
  SPECVIEW v;

  if(!spec.active || !spectrum_available())
    return 0;

  spec_fix_channel();

  log2n = spec_log2n();
  if(spec.fresh || spec.recompute || (spec_frame[0].log2n != log2n && spec.have[0]) ||
     (spec_frame[1].log2n != log2n && spec.have[1]))
    spec_compute();

  divider = spec_divider();
  bin_mhz = spec_bin_mhz(divider, log2n);
  spec_view(&v, log2n);

  display_set_screen_buffer(displaybuffertmp);
  display_set_fg_color(BLACK_COLOR);
  display_fill_rect(2, 48, 728, 432);

  spec_draw_grid(&v);
  spec_draw_freq_labels(&v, bin_mhz);
  spec_draw_info(log2n, divider, bin_mhz);

  if(!spec.have[0] && !spec.have[1])
  {
    display_set_font(&font_3);
    display_set_fg_color(WHITE_COLOR);
    text_center(PLOT_X + PLOT_W / 2, PLOT_Y + PLOT_H / 2 - 8,
                (spec_ch_on(0) || spec_ch_on(1)) ? "Waiting for a capture" : "No channel enabled");
  }

  //CH2 first, so CH1 is on top where they overlap
  for(ch = 1; ch >= 0; ch--)
    if(spec.have[ch])
      peak[ch] = spec_draw_trace(&v, spec_db[ch], ch ? CHANNEL2_COLOR : CHANNEL1_COLOR);

  if(spec.have[0])
    spec_draw_peak(INFO_Y1, 0, peak[0], bin_mhz);
  if(spec.have[1])
    spec_draw_peak(spec.have[0] ? INFO_Y2 : INFO_Y1, 1, peak[1], bin_mhz);

  spec_draw_strip(log2n, bin_mhz);

  display_set_source_buffer(displaybuffertmp);
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_copy_rect_to_screen(2, 48, 728, 432);
  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------

static void spec_step_field(int f, int up)
{
  int log2n = spec_log2n();
  int lmax = spec_log2n_max();

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
      //1024 .. the longest the record holds, then back to 1024
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

    case F_SPAN:
      //Narrower on the right half, wider on the left; the span keeps its centre
      {
        uint32 centre = spec.start + ((START_STEPS >> spec.zoom) / 2);

        if(up && (spec.zoom < ZOOM_MAX)) spec.zoom++;
        else if(!up && spec.zoom) spec.zoom--;

        spec.start = (centre > ((START_STEPS >> spec.zoom) / 2)) ? centre - ((START_STEPS >> spec.zoom) / 2) : 0;
        if(spec.start > START_STEPS - (START_STEPS >> spec.zoom))
          spec.start = START_STEPS - (START_STEPS >> spec.zoom);
      }
      break;

    case F_START:
      //Half a span per tap
      {
        uint32 step = (START_STEPS >> spec.zoom) / 2;

        if(up)
          spec.start = (spec.start + step > START_STEPS - (START_STEPS >> spec.zoom)) ? START_STEPS - (START_STEPS >> spec.zoom) : spec.start + step;
        else
          spec.start = (spec.start > step) ? spec.start - step : 0;
      }
      break;

    case F_EXIT:
      spectrum_set_active(0);
      break;
  }
}

int spectrum_touch(void)
{
  if(!spec.active || !spectrum_available())
    return 0;

  if((ytouch >= STRIP_Y) && (xtouch >= PLOT_X) && (xtouch < PLOT_X + PLOT_W))
  {
    int    f = (xtouch - PLOT_X) / FIELD_W;
    uint32 x = (xtouch - PLOT_X) % FIELD_W;

    if(f < F_COUNT)
      spec_step_field(f, x >= FIELD_W / 2);
  }

  //Touches on the plot do nothing (1.00o5 would move traces and change the time base)
  tp_i2c_wait_for_touch_release();
  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// READ_SPECTRUM header (32 bytes): u8 channel, u8 window, u8 log2n, u8 flags (bit 0 levels in dBA), u32 frame,
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

  bin = spec_bin_mhz(f->divider, f->log2n);

  hdr[0] = ch & 1;
  hdr[1] = f->window;
  hdr[2] = f->log2n;
  hdr[3] = f->unit_amps;
  put32(hdr + 4, spec.frames);
  put32(hdr + 8, f->divider);
  put32(hdr + 12, (200000000u + (f->divider + 1) / 2) / (f->divider + 1));
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
