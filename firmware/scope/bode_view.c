//----------------------------------------------------------------------------------------------------------------------------------
// bode_view.c - the Bode plot mode (fw 0.2.66, see bode_view.h).
//
// Layout of the trace area while the view shows (x 2..729, y 48..479):
//   y  60..276  magnitude pane, 6 divisions of 36 px, 5 / 10 / 20 dB per division chosen from the data, reference on top
//   y 292..408  phase pane, 4 divisions of 29 px, +180 .. -180 deg
//   x   6..726  log frequency axis from Start to Stop, decade lines with 2 and 5 sub-lines; labels at y 413
//   y  69       the warning line at the top middle (consequence and action, the most urgent only)
//   y 446..467  the cards: the cursor's f, |H|, phase; the -3 dB frequency (or the peak and Q); the sweep's progress; Thru
// Before the first sweep the plot shows the wiring. Integer only: mdB, mdeg, mHz.
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>

#include "bode_view.h"
#include "bode_dsp.h"
#include "bode_sweep.h"
#include "types.h"
#include "variables.h"
#include "display_lib.h"
#include "touchpanel.h"
#include "fnirsi_1013d_scope.h"
#include "scope_functions.h"
#include "menu.h"
#include "fpga_control.h"
#include "pc_hal.h"
#include "pc_hooks.h"
#include "pc_protocol.h"
#include "spec_fmt.h"
#include "ui_ctrl.h"
#include "ui_fmt.h"
#include "ui_meas.h"
#include "spectrum.h"
#include "phosphor.h"
#include "ui_diag_view.h"

extern DISPLAYDATA displaydata;

//pc_hal_scope.c (fw 0.2.66): the capabilities of the running bitstream
uint32 pc_glue_caps(void);

#define PX                6
#define PW                720
#define MAG_Y             60
#define MAG_DIV           36
#define MAG_H             (6 * MAG_DIV)
#define PH_Y              292
#define PH_DIV            29
#define PH_H              (4 * PH_DIV)
#define FL_Y              413
#define CARDS_Y           436
#define WARN_Y            69

#define PHASE_COLOR       0x00FF80FF
#define FLAG_COLOR        0x00707070
#define NOTE_COLOR        0x00A0A0A0
#define CUR_COLOR         0x00606060

#define REDRAW_MS         150         //at most this often while sweeping
#define STEP_BUDGET_MS    20          //engine steps per main-loop pass
#define NOTE_MS           5000

//Start / Stop in 1-2-5 steps: index i is {1, 2, 5}[i % 3] x 10^(i / 3) Hz, 0 = 1 Hz .. 21 = 10 MHz
#define F_STEPS           22
#define START_MAX         20          //5 MHz
#define STOP_MIN          3           //10 Hz

static const uint8  pts_tab[4] = { 10, 20, 50, 100 };
static const char *const settle_names[BODE_SETTLE_COUNT] = { "Fast", "Normal", "Resonant", "High Q" };
static const char *const ch2_names[2] = { "Auto", "Hold" };

typedef struct
{
  uint8  active;
  uint8  start_i, stop_i, points_i, settle, ch2;
  uint8  single, was_running, last_run;
  uint8  dirty, auto_shown;
  int    cursor;                    //point index, -1 = none
  uint32 drawn_ms;
  uint8  note_ext;
  uint32 note_ms;

  //What the view gives back
  uint8  s_run, s_trig, s_gen_en, s_gen_duty, s_disp[2];
  uint32 s_gen_freq;
  pc_hw_channel  s_ch[2];
  pc_hw_timebase s_tb;
} bv_state;

//Defaults: 100 Hz .. 1 MHz, 50 points, Normal settle, CH2 Auto
static bv_state bv = { .start_i = 6, .stop_i = 18, .points_i = 2, .settle = BODE_SETTLE_NORMAL, .ch2 = BODE_CH2_AUTO,
                       .cursor = -1 };

static uint64 f_step_mhz(int i)
{
  static const uint8 m125[3] = { 1, 2, 5 };
  uint64 f = (uint64)m125[i % 3] * 1000;
  int k;

  for(k = 0; k < i / 3; k++)
    f *= 10;

  return f;
}

static void f_step_text(char *t, int i)
{
  static const char *const units[3] = { "Hz", "kHz", "MHz" };
  static const uint8 m125[3] = { 1, 2, 5 };
  int dec = i / 3, u = dec / 3;
  uint32 v = m125[i % 3];
  int k;

  for(k = 0; k < dec % 3; k++)
    v *= 10;

  fmt_str(fmt_uint(t, v), units[(u < 3) ? u : 2]);
}

//The host's exact settings (BODE_CTRL flags bit 0) stay in use until a setting is changed on the screen
static bode_cfg host_cfg;
static uint8    host_cfg_valid;

static void cfg_now(bode_cfg *c)
{
  if(host_cfg_valid)
  {
    *c = host_cfg;
    return;
  }

  c->start_mhz  = f_step_mhz(bv.start_i);
  c->stop_mhz   = f_step_mhz(bv.stop_i);
  c->points     = pts_tab[bv.points_i & 3];
  c->settle     = bv.settle;
  c->ch2_mode   = bv.ch2;
  c->background = 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Mode

int bode_view_active(void)
{
  return bv.active;
}

int bode_view_available(void)
{
  return ((pc_glue_caps() & (PC_CAP_DECIM | PC_CAP_GENERATOR_DDS)) == (PC_CAP_DECIM | PC_CAP_GENERATOR_DDS)) &&
         !scopesettings.waveviewmode;
}

static void bv_draw_bar_button(void)
{
  DISPLAYDATA save = displaydata;

  bv.auto_shown = (uint8)bode_view_auto_running();

  //AUTO SET's label ("Auto..." while a single sweep runs): only on page one, which redraws it when it returns
  if((scopesettings.rightmenustate == 0) && !scopesettings.waveviewmode)
  {
    display_set_screen_buffer((uint16 *)maindisplaybuffer);
    scope_auto_set_button(0);
  }

  displaydata = save;
}

void bode_view_open(void)
{
  pc_hw_trigger t;
  int c;

  if(bv.active || !bode_view_available())
    return;

  //One mode at a time: the spectrum ends; roll mode ends as the main menu's Spectrum row does it (time base 20)
  if(spectrum_mode() != SPECTRUM_MODE_SCOPE)
    spectrum_set_active(0);

  if(scopesettings.long_mode)
  {
    pc_hw_timebase tb;

    tb.timeperdiv = 20;
    tb.samplerate = time_per_div_sample_rate[20];
    tb.divider    = 0;
    pc_hal_timebase_write(&tb, 0);
  }

  //What is given back on leaving
  bv.s_run      = scopesettings.runstate;
  bv.s_gen_en   = fpgasettings.gen_enable;
  bv.s_gen_freq = fpgasettings.gen_freq;
  bv.s_gen_duty = fpgasettings.gen_duty;
  pc_hal_timebase_read(&bv.s_tb);
  pc_hal_trigger_read(&t);
  bv.s_trig = t.source;

  for(c = 0; c < 2; c++)
  {
    pc_hal_channel_read(c, &bv.s_ch[c]);
    bv.s_disp[c] = c ? scopesettings.channel2.displayvoltperdiv : scopesettings.channel1.displayvoltperdiv;
  }

  //The generator drives the calibration pin, which EXT uses as an input: the trigger goes to CH1 (protocol row 41)
  bv.note_ext = 0;
  if(t.source == 2)
  {
    t.source = 0;
    pc_hal_trigger_write(&t, PC_TRG_SOURCE);
    bv.note_ext = 1;
    bv.note_ms  = timer0ticks;
  }

  //Nothing runs until RUN, AUTO SET or Run sweep: the post may be wired to something else
  if(scopesettings.runstate)
    pc_hal_ui_set_run(0);

  bv.active      = 1;
  bv.single      = 0;
  bv.was_running = 0;
  bv.last_run    = 0;
  bv.cursor      = -1;
  bv.dirty       = 1;
  bv.drawn_ms    = timer0ticks - 1000;
}

void bode_view_close(void)
{
  pc_hw_trigger t;
  int c;

  if(!bv.active)
    return;

  bode_stop(bode_engine(), 1);
  bv.active = 0;
  display_set_screen_buffer(ui_diag_scope_buffer());

  //Channels (range, offset, coupling, enable), the display V/div (software zoom), the time base, the trigger source,
  //the generator, RUN / STOP
  for(c = 0; c < 2; c++)
  {
    PCHANNELSETTINGS s = c ? &scopesettings.channel2 : &scopesettings.channel1;

    pc_hal_channel_write(c, &bv.s_ch[c], PC_CH_ENABLE | PC_CH_RANGE | PC_CH_COUPLING | PC_CH_OFFSET);
    s->displayvoltperdiv = bv.s_disp[c];
    scope_channel_settings(s, 0);
  }

  pc_hal_timebase_write(&bv.s_tb, 0);

  //EXT back (it switches the generator off: it was off with EXT anyway)
  pc_hal_trigger_read(&t);
  if(t.source != bv.s_trig)
  {
    t.source = bv.s_trig;
    pc_hal_trigger_write(&t, PC_TRG_SOURCE);
  }

  fpgasettings.gen_freq   = bv.s_gen_freq;
  fpgasettings.gen_duty   = bv.s_gen_duty;
  fpgasettings.gen_enable = bv.s_gen_en;
  fpga_set_generator_duty(fpgasettings.gen_duty);
  fpga_set_generator_freq(fpgasettings.gen_freq);
  fpga_on_off_generator();
  scope_generator_settings(0);

  pc_hal_ui_set_run(bv.s_run);

  if(bv.auto_shown)
    bv_draw_bar_button();

  //The scope view redraws its trace area at the next pass; its grid and pointers now
  scope_preset_values();
}

//----------------------------------------------------------------------------------------------------------------------------------
// Sweeps

static void bv_start(int single, int thru)
{
  bode_cfg c;
  int r2 = scopesettings.channel2.samplevoltperdiv;

  cfg_now(&c);

  //RUN shows while a sweep runs (single: back to STOP at its end)
  if(!scopesettings.runstate)
    pc_hal_ui_set_run(1);

  bv.last_run = 1;

  if(bode_start(bode_engine(), &c, thru, (r2 > 5) ? 5 : r2))
  {
    pc_hal_ui_set_run(0);
    bv.last_run = 0;
  }
  else
    bv.was_running = 1;

  bv.single = (uint8)(single || thru);
  bv.cursor = (bv.cursor < bode_count(bode_engine())) ? bv.cursor : -1;
  bv.dirty  = 1;
}

void bode_view_service(void)
{
  bode_eng *e = bode_engine();
  uint32 t0;
  int run;

  if(!bv.active)
    return;

  //A viewer or roll mode ends the view; Diagnostics stops the sweep (no acquisition while it shows)
  if(!bode_view_available() || scopesettings.long_mode)
  {
    bode_view_close();
    return;
  }

  if(ui_diag_active())
  {
    bode_stop(e, 1);
    return;
  }

  run = scopesettings.runstate;

  if(bode_state(e) == BODE_ST_RUNNING)
  {
    //STOP (button or host): the sweep ends after the point in progress
    if(!run)
      bode_stop(e, 0);

    t0 = timer0ticks;
    do
    {
      if(bode_step(e))
        bv.dirty = 1;
    }
    while((bode_state(e) == BODE_ST_RUNNING) && ((timer0ticks - t0) < STEP_BUDGET_MS));
  }

  if(bode_state(e) != BODE_ST_RUNNING)
  {
    if(bv.was_running)
    {
      uint8 st = bode_state(e);

      bv.was_running = 0;
      bv.dirty = 1;

      if(bv.single || (st != BODE_ST_DONE))
      {
        if(scopesettings.runstate)
          pc_hal_ui_set_run(0);
      }
      else if(scopesettings.runstate)
        bv_start(0, 0);                       //RUN: the next sweep overwrites point by point

      bv.single = 0;
    }
    else if(scopesettings.runstate && !bv.last_run)
      bv_start(0, 0);                         //RUN pressed (or SET_ACQ_STATE run)
  }

  bv.last_run = scopesettings.runstate;

  if(bv.auto_shown != (uint8)bode_view_auto_running())
    bv_draw_bar_button();

  if(bv.note_ext && ((timer0ticks - bv.note_ms) > NOTE_MS))
  {
    bv.note_ext = 0;
    bv.dirty = 1;
  }
}

int bode_view_autoset(void)
{
  bode_eng *e = bode_engine();

  if(!bv.active)
    return 0;

  if(bode_state(e) == BODE_ST_RUNNING)
  {
    bv.single = 1;
    pc_hal_ui_set_run(0);                     //ends after the point in progress
  }
  else
    bv_start(1, 0);

  bv_draw_bar_button();
  return 1;
}

int bode_view_auto_running(void)
{
  return bv.active && bv.single && (bode_state(bode_engine()) == BODE_ST_RUNNING);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Drawing

static uint32 text_w(FONTDATA *f, const char *s)
{
  return ui_text_width(f, s);
}

static void text_at(FONTDATA *f, uint32 x, uint32 y, const char *s)
{
  display_set_font(f);
  display_text(x, y, (int8 *)s);
}

static void text_c(FONTDATA *f, uint32 xc, uint32 y, const char *s)
{
  text_at(f, xc - text_w(f, s) / 2, y, s);
}

static void text_r(FONTDATA *f, uint32 xr, uint32 y, const char *s)
{
  text_at(f, xr - text_w(f, s), y, s);
}

static uint32 grid_color(void)
{
  uint32 c = (scopesettings.gridbrightness * 255) / 100;

  if(c < 0x30)
    c = 0x30;
  return (c << 16) | (c << 8) | c;
}

typedef struct
{
  int64  l0, l1;        //log2 of the axis ends, Q32
  int32  top, div;      //magnitude pane: top line and division, mdB
} bv_axes;

static int32 x_of(const bv_axes *a, uint64 f_mhz)
{
  int64 l = bode_log2_q32(f_mhz);
  int64 x;

  if(a->l1 <= a->l0)
    return PX + PW / 2;

  x = ((l - a->l0) * PW) / (a->l1 - a->l0);
  if(x < 0) x = 0;
  if(x > PW) x = PW;
  return PX + (int32)x;
}

static int32 y_mag(const bv_axes *a, int32 mdb)
{
  int64 y = MAG_Y + (((int64)a->top - mdb) * MAG_H) / (6 * (int64)a->div);

  if(y < MAG_Y) y = MAG_Y;
  if(y > MAG_Y + MAG_H) y = MAG_Y + MAG_H;
  return (int32)y;
}

static int32 y_ph(int32 mdeg)
{
  int64 y = PH_Y + ((180000ll - mdeg) * PH_H) / 360000;

  if(y < PH_Y) y = PH_Y;
  if(y > PH_Y + PH_H) y = PH_Y + PH_H;
  return (int32)y;
}

//The points as shown (thru applied when on)
static int      n_pts;
static uint64   p_f[BODE_MAX_POINTS];
static int32    p_m[BODE_MAX_POINTS], p_p[BODE_MAX_POINTS];
static uint8    p_ok[BODE_MAX_POINTS], p_have[BODE_MAX_POINTS];
static uint16   p_flags[BODE_MAX_POINTS];

static void gather(void)
{
  bode_eng *e = bode_engine();
  const bode_plan *pl = bode_plans(e);
  int i;

  n_pts = bode_count(e);

  for(i = 0; i < n_pts; i++)
  {
    bode_point p;

    p_f[i] = pl[i].f_mhz;
    p_have[i] = (uint8)bode_point_get(e, i, &p);
    p_m[i] = p_have[i] ? p.h_mdb : 0;
    p_p[i] = p_have[i] ? p.h_mdeg : 0;
    p_flags[i] = p_have[i] ? p.flags : 0;
    p_ok[i] = (uint8)(p_have[i] && !(p.flags & BODE_F_BAD));
  }
}

static void axes(bv_axes *a)
{
  bode_cfg c;
  int32 mx = -2000000000, mn = 2000000000;
  int   i, any = 0;
  static const int32 divs[3] = { 5000, 10000, 20000 };

  cfg_now(&c);

  //The axis is the sweep's range: the last sweep's while its points show, else the settings'
  if(bode_count(bode_engine()) && (bode_state(bode_engine()) != BODE_ST_IDLE))
    c = *bode_last_cfg(bode_engine());

  a->l0 = bode_log2_q32((c.start_mhz < c.stop_mhz) ? c.start_mhz : c.stop_mhz);
  a->l1 = bode_log2_q32((c.start_mhz < c.stop_mhz) ? c.stop_mhz : c.start_mhz);

  for(i = 0; i < n_pts; i++)
    if(p_have[i])
    {
      any = 1;
      if(p_m[i] > mx) mx = p_m[i];
      if(p_m[i] < mn) mn = p_m[i];
    }

  a->div = 10000;
  a->top = 0;

  if(any)
  {
    for(i = 0; i < 3; i++)
    {
      int32 d = divs[i];
      int32 top = (mx >= 0) ? ((mx + d - 1) / d) * d : -((-mx) / d) * d;

      a->div = d;
      a->top = top;
      if(top - mn <= 6 * d)
        break;
    }
  }
}

static void freq_label(char *t, uint64 mhz)
{
  //10, 100, 1k, 10k, 100k, 1M, 10M
  uint64 hz = mhz / 1000;

  if(hz >= 1000000)
    fmt_str(fmt_uint(t, (uint32)(hz / 1000000)), "M");
  else if(hz >= 1000)
    fmt_str(fmt_uint(t, (uint32)(hz / 1000)), "k");
  else
    fmt_uint(t, (uint32)hz);
}

static void draw_grid(const bv_axes *a)
{
  char   t[16];
  uint32 i;
  uint64 dec;
  uint32 gc = grid_color();

  display_set_fg_color(gc);

  if(scopesettings.gridenable)
  {
    for(i = 0; i <= 6; i++)
      display_draw_horz_dots(MAG_Y + i * MAG_DIV, PX, PX + PW, 4);

    for(i = 0; i <= 4; i++)
      display_draw_horz_dots(PH_Y + i * PH_DIV, PX, PX + PW, 4);
  }

  //Decades (dots every 3 px) with the 2 and 5 lines (every 8 px), and their labels
  for(dec = 1000; dec <= 10000000000ull; dec *= 10)
  {
    static const uint8 sub[3] = { 1, 2, 5 };
    int k;

    for(k = 0; k < 3; k++)
    {
      uint64 f = dec * sub[k];
      int64  l = bode_log2_q32(f);
      int32  x;

      if((l < a->l0) || (l > a->l1))
        continue;

      x = x_of(a, f);

      if(scopesettings.gridenable)
      {
        display_set_fg_color(gc);
        display_draw_vert_dots(x, MAG_Y, MAG_Y + MAG_H, k ? 8 : 3);
        display_draw_vert_dots(x, PH_Y, PH_Y + PH_H, k ? 8 : 3);
      }

      if(!k)
      {
        freq_label(t, f);
        display_set_fg_color(NOTE_COLOR);
        if(x - (int32)text_w(&font_2, t) / 2 < PX)
          text_at(&font_2, PX, FL_Y, t);
        else if(x + (int32)text_w(&font_2, t) / 2 > PX + PW)
          text_r(&font_2, PX + PW, FL_Y, t);
        else
          text_c(&font_2, x, FL_Y, t);
      }
    }
  }

  //Level and phase labels
  display_set_fg_color(NOTE_COLOR);
  for(i = 0; i < 6; i++)
  {
    char *p = fmt_int(t, (a->top - (int32)i * a->div) / 1000);

    if(!i)
      fmt_str(p, " dB");
    text_at(&font_2, PX + 3, MAG_Y + i * MAG_DIV + 2, t);
  }

  for(i = 0; i < 4; i++)
  {
    char *p = fmt_int(t, 180 - (int32)i * 90);

    if(!i)
      fmt_str(p, " deg");
    text_at(&font_2, PX + 3, PH_Y + i * PH_DIV + 2, t);
  }

  //The panes' separation
  display_set_fg_color(GREY_COLOR);
  display_draw_horz_line((MAG_Y + MAG_H + PH_Y) / 2, PX, PX + PW);
}

static void dot(int32 x, int32 y)
{
  display_fill_rect((uint32)(x - 1), (uint32)(y - 1), 3, 3);
}

static void draw_traces(const bv_axes *a)
{
  int   i, prev;
  int32 px = 0, pm = 0, pp = 0;

  //Flagged points grey, no joining line
  display_set_fg_color(FLAG_COLOR);
  for(i = 0; i < n_pts; i++)
    if(p_have[i] && !p_ok[i])
    {
      int32 x = x_of(a, p_f[i]);

      dot(x, y_mag(a, p_m[i]));
      dot(x, y_ph(p_p[i]));
    }

  //Valid points joined in order; the phase line is not drawn across a wrap
  prev = -1;
  for(i = 0; i < n_pts; i++)
  {
    int32 x, ym, yp;

    if(!p_ok[i])
    {
      prev = -1;
      continue;
    }

    x  = x_of(a, p_f[i]);
    ym = y_mag(a, p_m[i]);
    yp = y_ph(p_p[i]);

    display_set_fg_color(CHANNEL2_COLOR);
    if(prev >= 0)
      display_draw_line((uint32)px, (uint32)pm, (uint32)x, (uint32)ym);
    dot(x, ym);

    display_set_fg_color(PHASE_COLOR);
    if((prev >= 0) && (((p_p[i] - p_p[prev]) < 180000) && ((p_p[prev] - p_p[i]) < 180000)))
      display_draw_line((uint32)px, (uint32)pp, (uint32)x, (uint32)yp);
    dot(x, yp);

    px = x; pm = ym; pp = yp;
    prev = i;
  }
}

static void triangle(int32 x, int32 y)
{
  int32 i;

  if(x < PX + 4) x = PX + 4;
  if(x > PX + PW - 4) x = PX + PW - 4;

  display_set_fg_color(WHITE_COLOR);
  for(i = 0; i < 5; i++)
    display_draw_horz_line((uint32)(y - i), (uint32)(x - i), (uint32)(x + i));
}

static bode_markers mk;

static void draw_markers(const bv_axes *a)
{
  int i;

  bode_markers_find(p_f, p_m, p_p, p_ok, n_pts, &mk);

  for(i = 0; i < mk.n3db; i++)
    triangle(x_of(a, mk.f3db_mhz[i]), y_mag(a, mk.ref_mdb - 3000) - 3);

  if(mk.have_peak)
    triangle(x_of(a, mk.peak_mhz), y_mag(a, mk.peak_mdb) - 3);

  for(i = 0; i < BODE_PH_TARGETS; i++)
    if(mk.ph_mhz[i])
    {
      int32 t = bode_ph_targets_mdeg[i];

      //The unwrapped target drawn where the wrapped phase shows it
      triangle(x_of(a, mk.ph_mhz[i]), y_ph(bode_wrap_mdeg(t)) - 3);
    }
}

static const char *warning(char *buf)
{
  bode_eng *e = bode_engine();
  int i, unsettled = 0, floor = 0, ch1low = 0, clip2 = 0;
  uint8 st = bode_state(e);

  if(st == BODE_ST_ERROR)
  {
    switch(bode_error(e))
    {
      case BODE_ERR_OVERLOAD: return "CH2 saturated by the DUT's DC level: set CH2 range Auto or ground the DUT on the clips";
      case BODE_ERR_ARM:      return "Sweep stopped: the FPGA did not start a capture";
      case BODE_ERR_TIMEOUT:  return "Sweep stopped: no capture came: check the bitstream";
      case BODE_ERR_HOST:     return "Sweep stopped: the PC took over the acquisition";
      case BODE_ERR_PLAN:     return "No frequency in this range: change Start or Stop";
      default:                break;
    }
  }

  if(bode_is_thru_sweep(e) && (st == BODE_ST_RUNNING))
    return "Thru sweep: both probe tips on the DUT input";

  for(i = 0; i < n_pts; i++)
  {
    if(!p_have[i])
      continue;
    if(p_flags[i] & BODE_F_UNSETTLED)                   unsettled++;
    if(p_flags[i] & (BODE_F_NEAR_FLOOR | BODE_F_LOW_SNR)) floor++;
    if(p_flags[i] & BODE_F_CH1_LOW)                     ch1low = 1;
    if(p_flags[i] & (BODE_F_CLIPPED | BODE_F_CH2_OVERLOAD)) clip2 = 1;
  }

  if(clip2 && (bv.ch2 == BODE_CH2_HOLD))
    return "CH2 overrange: set CH2 range Auto or raise its V/div";

  if(ch1low)
    return "CH1 level low: the DUT input loads the 1 kOhm source";

  if(unsettled)
  {
    fmt_str(fmt_uint(buf, (uint32)unsettled), (unsettled > 1) ? " points not settled: choose a longer Settle" :
                                                                " point not settled: choose a longer Settle");
    return buf;
  }

  if(floor)
  {
    fmt_str(fmt_uint(buf, (uint32)floor), (floor > 1) ? " points near the floor: shown grey" :
                                                        " point near the floor: shown grey");
    return buf;
  }

  if(e->thru.applied && (bode_thru_state(e) == BODE_THRU_STALE))
    return "Thru does not match this sweep: measure Thru again";

  if(bode_exhausted(e))
    return "Offset limit reached: the DUT's DC level is beyond the range";

  if(bv.note_ext)
    return "The trigger was EXT (the generator's pin): set to CH1";

  return "";
}

static void draw_cards(void)
{
  bode_eng *e = bode_engine();
  char t[24];
  int  n = 0;

  //The cursor: f, |H| and phase of the point (thru applied when on)
  if((bv.cursor >= 0) && (bv.cursor < n_pts) && p_have[bv.cursor])
  {
    ui_fmt_mhz(t, p_f[bv.cursor]);
    scope_draw_card(ui_ms_card_x(0, n++), UI_MS_SPEC_Y, LIGHTGREY2_COLOR, "f", t);
    ui_fmt_cdb(t, (p_m[bv.cursor] >= 0) ? (p_m[bv.cursor] + 5) / 10 : -((-p_m[bv.cursor] + 5) / 10), "dB");
    scope_draw_card(ui_ms_card_x(0, n++), UI_MS_SPEC_Y, CHANNEL2_COLOR, "|H|", t);
    fmt_str(fmt_c1(t, (p_p[bv.cursor] >= 0) ? (p_p[bv.cursor] + 5) / 10 : -((-p_p[bv.cursor] + 5) / 10)), "deg");
    scope_draw_card(ui_ms_card_x(0, n++), UI_MS_SPEC_Y, PHASE_COLOR, "Phase", t);
  }

  //The automatic markers: -3 dB, or the peak and Q of a resonance
  if(mk.shape == BODE_SHAPE_RESONANT)
  {
    ui_fmt_mhz(t, mk.peak_mhz);
    scope_draw_card(ui_ms_card_x(0, n++), UI_MS_SPEC_Y, LIGHTGREY2_COLOR, "Peak", t);
    if(mk.have_q)
    {
      fmt_c1(t, (int32)mk.q_x100);
      scope_draw_card(ui_ms_card_x(0, n++), UI_MS_SPEC_Y, LIGHTGREY2_COLOR, "Q", t);
    }
  }
  else if(mk.n3db)
  {
    ui_fmt_mhz(t, mk.f3db_mhz[0]);
    scope_draw_card(ui_ms_card_x(0, n++), UI_MS_SPEC_Y, LIGHTGREY2_COLOR, "-3dB", t);
  }

  //Progress
  if(bode_count(e) && (n < UI_MS_ROW_CARDS))
  {
    fmt_uint(fmt_str(fmt_uint(t, (uint32)bode_done_count(e)), "/"), (uint32)bode_count(e));
    scope_draw_card(ui_ms_card_x(0, n++), UI_MS_SPEC_Y, LIGHTGREY2_COLOR, "Sweep", t);
  }

  if(e->thru.measured && e->thru.applied && (n < UI_MS_ROW_CARDS))
    scope_draw_card(ui_ms_card_x(0, n++), UI_MS_SPEC_Y, (bode_thru_state(e) == BODE_THRU_VALID) ? GREEN_COLOR : ORANGE_COLOR,
                    "Thru", (bode_thru_state(e) == BODE_THRU_VALID) ? "applied" : "stale");
}

static void draw_connection(void)
{
  static const char *const lines[5] =
  {
    "Calibration post -> DUT input. CH1 tip on the DUT input, CH2 tip on the DUT output.",
    "The post is a 3.1 Vpp square through 1 kOhm; H = CH2 / CH1 (CH1 must touch the DUT input).",
    "DUT ground on the probe ground clips. Passive DUTs only: nothing that can drive the post.",
    "",
    "RUN: repeated sweeps.  AUTO SET: one sweep.  CTRL: Sweep and Thru.",
  };
  uint32 i;

  display_set_fg_color(WHITE_COLOR);
  for(i = 0; i < 5; i++)
    text_c(&font_3, PX + PW / 2, 150 + i * 26, lines[i]);
}

static void bv_clear(void)
{
  uint32 y;

  for(y = 48; y < 480; y++)
    memset(&displaybuffertmp[y * 800 + 2], 0, 728 * 2);
}

static void draw_all(void)
{
  bode_eng *e = bode_engine();
  bv_axes a;
  char    wb[80];
  const char *w;
  int     any = 0, i;

  gather();
  axes(&a);

  display_set_screen_buffer(displaybuffertmp);
  bv_clear();
  draw_grid(&a);

  for(i = 0; i < n_pts; i++)
    any |= p_have[i];

  if(!any && (bode_state(e) != BODE_ST_RUNNING))
  {
    memset(&mk, 0, sizeof(mk));
    draw_connection();
  }
  else
  {
    draw_traces(&a);
    draw_markers(&a);

    //The point being measured: a thin grey line
    if(bode_current(e) >= 0)
    {
      display_set_fg_color(CUR_COLOR);
      display_draw_vert_dots((uint32)x_of(&a, p_f[bode_current(e)]), MAG_Y, PH_Y + PH_H, 2);
    }

    //The cursor
    if((bv.cursor >= 0) && (bv.cursor < n_pts) && p_have[bv.cursor])
    {
      int32 x = x_of(&a, p_f[bv.cursor]);

      display_set_fg_color(WHITE_COLOR);
      display_draw_vert_dots((uint32)x, MAG_Y, PH_Y + PH_H, 3);
      display_draw_rect((uint32)(x - 3), (uint32)(y_mag(&a, p_m[bv.cursor]) - 3), 7, 7);
      display_draw_rect((uint32)(x - 3), (uint32)(y_ph(p_p[bv.cursor]) - 3), 7, 7);
    }
  }

  w = warning(wb);
  if(*w)
  {
    display_set_fg_color(ORANGE_COLOR);
    text_c(&font_2, 366, WARN_Y, w);
  }

  draw_cards();

  display_set_source_buffer(displaybuffertmp);
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_copy_rect_to_screen(2, 48, 728, 432);

  bv.dirty    = 0;
  bv.drawn_ms = timer0ticks;
}

int bode_view_display(void)
{
  DISPLAYDATA save;

  //A viewer draws its own traces (the view closes when one opens; this is the guard)
  if(!bv.active || scopesettings.waveviewmode)
    return 0;

  save = displaydata;
  draw_all();
  displaydata = save;
  return 1;
}

int bode_view_pass(void)
{
  if(!bv.active || scopesettings.waveviewmode)
    return 0;

  //While sweeping at most every REDRAW_MS; a point, the state or a setting changed: now; idle: once a second
  if((bv.dirty && ((bode_state(bode_engine()) != BODE_ST_RUNNING) || ((timer0ticks - bv.drawn_ms) >= REDRAW_MS))) ||
     ((timer0ticks - bv.drawn_ms) >= 1000))
    bode_view_display();

  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Touch: a tap puts the cursor on the nearest measured point, a drag moves it along the points

static int nearest(int32 x)
{
  bv_axes a;
  int i, best = -1;
  int32 bd = 0x7FFFFFFF;

  gather();
  axes(&a);

  for(i = 0; i < n_pts; i++)
    if(p_have[i])
    {
      int32 d = x_of(&a, p_f[i]) - x;

      if(d < 0) d = -d;
      if(d < bd)
      {
        bd = d;
        best = i;
      }
    }

  return best;
}

int bode_view_touch(void)
{
  if(!bv.active)
    return 0;

  if(ytouch >= CARDS_Y)
  {
    tp_i2c_wait_for_touch_release();
    return 1;
  }

  while(havetouch)
  {
    int c = nearest((int32)xtouch);

    if(c != bv.cursor)
    {
      bv.cursor = c;
      bode_view_display();
    }

    tp_i2c_read_status();
  }

  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// CTRL page: Sweep (the Display slot), Thru (the analysis slot), Grid; the stepper and 50 % slots stay empty (the sweep
// ranges both channels itself; CH2's Hold range is the CH2 menu's V/div)

#define CP_XC             (UI_BAR_X + UI_BAR_W / 2)
#define CP_EDGE_COLOR     0x00505050
#define CP_SAVE_X         (UI_FOLD_RIGHT - 228)

#define ROW_CHOICE        0
#define ROW_SWITCH        1
#define ROW_VALUE         2
#define ROW_VALUE2        3
#define ROW_ACTION        4

#define FOLD_SWEEP        0
#define FOLD_THRU         1

typedef struct
{
  uint8       kind, on, dim;
  const char *label;
  char        value[24];
} bv_row;

typedef struct
{
  int         id;
  const char *title;
  ui_fold     f;
  uint32      n;
  bv_row      r[UI_FOLD_ROWS_MAX];
} bv_fold;

static void cp_button(uint32 y, int active)
{
  display_set_fg_color(active ? ITEM_ACTIVE_COLOR : DARKGREY_COLOR);
  display_fill_rounded_rect(UI_BAR_X, y, UI_BAR_W, UI_BTN_H, 2);
  display_set_fg_color(CP_EDGE_COLOR);
  display_draw_rounded_rect(UI_BAR_X, y, UI_BAR_W, UI_BTN_H, 2);
}

static void cp_value_button(uint32 y, const char *label, const char *value, int active)
{
  cp_button(y, active);
  display_set_fg_color(active ? BLACK_COLOR : WHITE_COLOR);
  text_c(&font_2, CP_XC, y + 8, label);
  display_set_fg_color(active ? BLACK_COLOR : PALEYELLOW_COLOR);
  text_c(&font_2, CP_XC, y + 27, value);
}

static const char *thru_text(void)
{
  bode_eng *e = bode_engine();

  switch(bode_thru_state(e))
  {
    case BODE_THRU_MEASURING: return "Meas.";
    case BODE_THRU_VALID:     return e->thru.applied ? "On" : "Off";
    case BODE_THRU_STALE:     return "Stale";
    default:                  return "None";
  }
}

static void sweep_button(int active)
{
  char t[12];

  fmt_str(fmt_uint(t, pts_tab[bv.points_i & 3]), " pt");
  cp_value_button(UI_CTRL_DISPLAY_Y, "Sweep", t, active);
}

static void thru_button(int active)
{
  cp_value_button(UI_CTRL_ANALYSIS_Y, "Thru", thru_text(), active);
}

//Grid as in the other modes (fw 0.2.63): a whole-button toggle, green on, grey off
static void grid_button(int active)
{
  uint32 fill = active ? ITEM_ACTIVE_COLOR : scopesettings.gridenable ? GREEN_COLOR : GREY_COLOR;

  display_set_fg_color(fill);
  display_fill_rounded_rect(UI_BAR_X, UI_CTRL_GRID_Y, UI_BAR_W, UI_BTN_H, 2);
  display_set_fg_color(CP_EDGE_COLOR);
  display_draw_rounded_rect(UI_BAR_X, UI_CTRL_GRID_Y, UI_BAR_W, UI_BTN_H, 2);
  display_set_fg_color((active || scopesettings.gridenable) ? BLACK_COLOR : WHITE_COLOR);
  text_c(&font_3, CP_XC, UI_CTRL_GRID_Y + 17, "Grid");
}

void bode_ctrl_draw(void)
{
  sweep_button(0);
  thru_button(0);
  grid_button(0);
}

static bv_row *add(bv_fold *d, int kind, const char *label)
{
  bv_row *r = &d->r[d->n++];

  r->kind  = (uint8)kind;
  r->label = label;
  r->on = r->dim = 0;
  r->value[0] = 0;
  return r;
}

static void build(bv_fold *d)
{
  bode_eng *e = bode_engine();
  uint32 sep = 0, anchor = UI_CTRL_DISPLAY_Y;
  bv_row *r;

  memset(d->r, 0, sizeof(d->r));
  d->n = 0;

  if(d->id == FOLD_SWEEP)
  {
    d->title = "Sweep";
    f_step_text(add(d, ROW_VALUE2, "Start")->value, bv.start_i);
    f_step_text(add(d, ROW_VALUE2, "Stop")->value, bv.stop_i);
    fmt_uint(add(d, ROW_VALUE, "Points")->value, pts_tab[bv.points_i & 3]);
    fmt_str(add(d, ROW_VALUE, "Settle")->value, settle_names[bv.settle % BODE_SETTLE_COUNT]);
    fmt_str(add(d, ROW_VALUE, "CH2 range")->value, ch2_names[bv.ch2 & 1]);
    sep = d->n;
    add(d, ROW_ACTION, (bode_state(e) == BODE_ST_RUNNING) ? "Stop sweep" : "Run sweep");
  }
  else
  {
    uint8 ts = bode_thru_state(e);

    d->title = "Thru: both tips on the input";
    anchor = UI_CTRL_ANALYSIS_Y;
    add(d, ROW_ACTION, "Measure thru");
    r = add(d, ROW_SWITCH, "Apply");
    r->on  = (uint8)(e->thru.measured && e->thru.applied);
    r->dim = (uint8)!e->thru.measured;
    fmt_str(r->value, (ts == BODE_THRU_VALID) ? "matches this sweep" : (ts == BODE_THRU_STALE) ? "other sweep settings" :
                      "none measured");
    add(d, ROW_ACTION, "Clear");
  }

  ui_fold_layout(&d->f, 206, d->n, sep, anchor);
}

static void row_draw(const bv_row *r, uint32 rx, uint32 ry, uint32 rw)
{
  uint32 ty = ry + (UI_FOLD_ROW_H - 16) / 2;

  display_set_fg_color(LIGHTGREY_COLOR);
  display_fill_rounded_rect(rx, ry, rw, UI_FOLD_ROW_H, 3);

  switch(r->kind)
  {
    case ROW_SWITCH:
      scope_display_slide_button(rx + 8, ry + 11, r->on, GREEN_COLOR);
      display_set_fg_color(r->dim ? NOTE_COLOR : WHITE_COLOR);
      text_at(&font_3, rx + UI_FOLD_TEXT_X - 30, ry + 5, r->label);
      display_set_fg_color(NOTE_COLOR);
      text_at(&font_2, rx + UI_FOLD_TEXT_X - 30, ry + 25, r->value);
      break;

    case ROW_VALUE:
      display_set_fg_color(WHITE_COLOR);
      text_at(&font_3, rx + UI_FOLD_VALUE_IN, ty, r->label);
      display_set_fg_color(PALEYELLOW_COLOR);
      text_r(&font_3, rx + rw - UI_FOLD_VALUE_IN, ty, r->value);
      break;

    case ROW_VALUE2:
      display_set_fg_color(GREY_COLOR);
      display_fill_rounded_rect(rx + 4, ry + 4, 30, UI_FOLD_ROW_H - 8, 2);
      display_fill_rounded_rect(rx + rw - 34, ry + 4, 30, UI_FOLD_ROW_H - 8, 2);
      display_set_fg_color(WHITE_COLOR);
      text_c(&font_3, rx + 19, ty, "-");
      text_c(&font_3, rx + rw - 19, ty, "+");
      text_at(&font_3, rx + UI_FOLD_TWOWAY_IN, ty, r->label);
      display_set_fg_color(PALEYELLOW_COLOR);
      text_r(&font_3, rx + rw - UI_FOLD_TWOWAY_IN, ty, r->value);
      break;

    default:
      display_set_fg_color(WHITE_COLOR);
      text_c(&font_3, rx + rw / 2, ty, r->label);
      break;
  }
}

static void fold_draw(const bv_fold *d)
{
  const ui_fold *f = &d->f;
  uint32 rx = ui_fold_row_x(f), rw = ui_fold_row_w(f), i;

  display_set_fg_color(DARKGREY_COLOR);
  display_fill_rect(f->x, f->y, f->w, f->h);
  display_set_fg_color(LIGHTGREY_COLOR);
  display_draw_rect(f->x, f->y, f->w, f->h);
  display_set_fg_color(LIGHTGREY2_COLOR);
  text_at(&font_2, f->x + 10, f->y + 9, d->title);

  if(f->sep)
  {
    display_set_fg_color(GREY_COLOR);
    display_draw_horz_line(ui_fold_row_y(f, f->sep) - 9, rx, rx + rw - 1);
  }

  for(i = 0; i < d->n; i++)
    row_draw(&d->r[i], rx, ui_fold_row_y(f, i), rw);
}

//A tap on a row. Returns 1 when the fold-out closes.
static int fold_tap(bv_fold *d, int row, int dir)
{
  bode_eng *e = bode_engine();

  bv.dirty = 1;

  if(d->id == FOLD_SWEEP)
  {
    //A setting changed here: the screen's values apply again
    if(row < 5)
      host_cfg_valid = 0;

    switch(row)
    {
      case 0:
        if((dir < 0) && (bv.start_i > 0))
          bv.start_i--;
        else if((dir > 0) && (bv.start_i < START_MAX))
          bv.start_i++;
        if(bv.stop_i <= bv.start_i)
          bv.stop_i = (uint8)(bv.start_i + 1);
        return 0;

      case 1:
        if((dir < 0) && (bv.stop_i > STOP_MIN))
          bv.stop_i--;
        else if((dir > 0) && (bv.stop_i < F_STEPS - 1))
          bv.stop_i++;
        if(bv.start_i >= bv.stop_i)
          bv.start_i = (uint8)(bv.stop_i - 1);
        return 0;

      case 2: bv.points_i = (uint8)((bv.points_i + 1) & 3);                return 0;
      case 3: bv.settle = (uint8)((bv.settle + 1) % BODE_SETTLE_COUNT);    return 0;
      case 4: bv.ch2 ^= 1;                                                 return 0;

      default:
        if(bode_state(e) == BODE_ST_RUNNING)
        {
          bv.single = 1;
          pc_hal_ui_set_run(0);
        }
        else
          bv_start(1, 0);
        return 1;
    }
  }

  switch(row)
  {
    case 0:
      if(bode_state(e) != BODE_ST_RUNNING)
        bv_start(1, 1);
      return 1;

    case 1:
      if(!d->r[1].dim)
        bode_thru_apply(e, !e->thru.applied);
      return 0;

    default:
      bode_thru_clear(e);
      return 1;
  }
}

static void fold_run(int id, void (*button)(int))
{
  bv_fold d;
  int     hit;

  display_set_screen_buffer((uint16 *)maindisplaybuffer);

  d.id = id;
  build(&d);

  button(1);
  tp_i2c_wait_for_touch_release();

  display_set_destination_buffer(displaybuffer2);
  display_copy_rect_from_screen(CP_SAVE_X, UI_FOLD_TOP_MIN, UI_FOLD_RIGHT - CP_SAVE_X, 480 - UI_FOLD_TOP_MIN);
  display_set_source_buffer(displaybuffer2);

  fold_draw(&d);

  while(1)
  {
    tp_i2c_read_status();

    if(!havetouch)
      continue;

    hit = ui_fold_hit(&d.f, xtouch, ytouch);

    if(hit == UI_FOLD_OUT)
      break;

    if(hit >= 0)
    {
      ui_fold old = d.f;
      uint32  lx = xtouch, ly = ytouch;

      //A row acts on release and only when the finger is still on it (fw 0.2.64, as ctrl_page.c)
      while(havetouch)
      {
        lx = xtouch;
        ly = ytouch;
        tp_i2c_read_status();
      }

      if(ui_fold_hit(&d.f, lx, ly) != hit)
        continue;

      if(fold_tap(&d, hit, ui_fold_dir(&d.f, lx)))
        break;

      build(&d);
      display_set_source_buffer(displaybuffer2);
      display_copy_rect_to_screen(old.x, old.y, old.w, old.h);
      fold_draw(&d);
    }

    tp_i2c_wait_for_touch_release();
  }

  tp_i2c_wait_for_touch_release();

  display_set_source_buffer(displaybuffer2);
  display_copy_rect_to_screen(CP_SAVE_X, UI_FOLD_TOP_MIN, UI_FOLD_RIGHT - CP_SAVE_X, 480 - UI_FOLD_TOP_MIN);

  bode_ctrl_draw();
  bv.dirty = 1;
}

void bode_ctrl_touch(void)
{
  display_set_screen_buffer((uint16 *)maindisplaybuffer);

  switch(ui_ctrl_item_at(ytouch, 1))
  {
    case UI_CTRL_DISPLAY:
      fold_run(FOLD_SWEEP, sweep_button);
      break;

    case UI_CTRL_ANALYSIS:
      fold_run(FOLD_THRU, thru_button);
      break;

    case UI_CTRL_GRID:
      grid_button(1);
      tp_i2c_wait_for_touch_release();
      scopesettings.gridenable ^= 1;
      grid_button(0);
      bv.dirty = 1;
      break;

    default:
      tp_i2c_wait_for_touch_release();
      break;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Settings sector words 219 (0xB000 | start << 7 | stop << 2 | points) and 220 (0xB100 | settle | CH2 range << 2)

uint16 bode_view_setting_word(int i)
{
  if(i == 0)
    return (uint16)(0xB000 | ((bv.start_i & 0x1F) << 7) | ((bv.stop_i & 0x1F) << 2) | (bv.points_i & 3));

  return (uint16)(0xB100 | (bv.settle & 3) | ((bv.ch2 & 1) << 2));
}

void bode_view_settings_restore(uint16 w0, uint16 w1)
{
  if((w0 & 0xF000) == 0xB000)
  {
    uint8 a = (uint8)((w0 >> 7) & 0x1F), b = (uint8)((w0 >> 2) & 0x1F);

    if((a <= START_MAX) && (b >= STOP_MIN) && (b < F_STEPS) && (a < b))
    {
      bv.start_i = a;
      bv.stop_i  = b;
      bv.points_i = (uint8)(w0 & 3);
    }
  }

  if((w1 & 0xFF00) == 0xB100)
  {
    bv.settle = (uint8)(w1 & 3);
    bv.ch2    = (uint8)((w1 >> 2) & 1);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Protocol (docs/protocol.md section 14 row 60)

#define BC_STATUS         0
#define BC_RUN            1
#define BC_REPEAT         2
#define BC_STOP           3
#define BC_ABORT          4
#define BC_THRU_MEASURE   5
#define BC_THRU_ON        6
#define BC_THRU_OFF       7
#define BC_THRU_CLEAR     8
#define BC_OPEN           9
#define BC_CLOSE          10

static uint32 get32(const uint8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32)p[3] << 24); }
static uint32 get16(const uint8 *p) { return p[0] | (p[1] << 8); }

//The nearest 1-2-5 step for a frequency in centi-Hz (the CTRL page shows the steps; a host value off the steps is kept
//exactly for the sweep and shown rounded)
static uint8 step_of(uint64 mhz)
{
  int i, best = 0;
  int64 bd = 0x7FFFFFFFFFFFFFFFll;

  for(i = 0; i < F_STEPS; i++)
  {
    int64 d = bode_log2_q32(f_step_mhz(i)) - bode_log2_q32(mhz);

    if(d < 0) d = -d;
    if(d < bd)
    {
      bd = d;
      best = i;
    }
  }

  return (uint8)best;
}

int bode_view_ctrl(const uint8 *req, uint8 *resp, uint16 *detail)
{
  bode_eng *e = bode_engine();
  uint8  act = req[0], flags = req[1];
  bode_cfg c;
  uint32 i;

  *detail = 0;

  if(act > BC_CLOSE)
    return PC_E_BAD_ARG;

  if(flags & 0x01)
  {
    uint64 a = (uint64)get32(req + 4) * 10, b = (uint64)get32(req + 8) * 10;
    uint32 n = get16(req + 12);

    if(req[2] >= BODE_SETTLE_COUNT) { *detail = 1; return PC_E_BAD_ARG; }
    if(req[3] > 1)                  { *detail = 2; return PC_E_BAD_ARG; }
    if((a < BODE_FMIN_MHZ) || (b < BODE_FMIN_MHZ) || (a > BODE_FMAX_MHZ) || (b > BODE_FMAX_MHZ))
                                    { *detail = 3; return PC_E_BAD_ARG; }
    if((n < 1) || (n > BODE_MAX_POINTS)) { *detail = 4; return PC_E_BAD_ARG; }

    c.start_mhz  = a;
    c.stop_mhz   = b;
    c.points     = (uint16)n;
    c.settle     = req[2];
    c.ch2_mode   = req[3];
    c.background = (uint8)!(flags & 0x02);

    //The screen's settings follow (rounded to its steps); the sweep takes the host's values exactly
    bv.start_i = step_of((a < b) ? a : b);
    bv.stop_i  = step_of((a < b) ? b : a);
    if(bv.start_i > START_MAX) bv.start_i = START_MAX;
    if(bv.stop_i <= bv.start_i) bv.stop_i = (uint8)(bv.start_i + 1);
    for(i = 0; i < 4; i++)
      if(pts_tab[i] <= n)
        bv.points_i = (uint8)i;
    bv.settle = c.settle;
    bv.ch2    = c.ch2_mode;
    host_cfg  = c;
    host_cfg_valid = 1;
  }

  if((act == BC_RUN) || (act == BC_REPEAT) || (act == BC_THRU_MEASURE) || (act == BC_OPEN))
  {
    if(!bode_view_available())
    {
      *detail = 1;
      return PC_E_STATE;
    }

    if(!bv.active)
      bode_view_open();
  }

  switch(act)
  {
    case BC_RUN:
    case BC_REPEAT:
    case BC_THRU_MEASURE:
      if(bode_state(e) == BODE_ST_RUNNING)
      {
        *detail = 2;
        return PC_E_STATE;
      }

      bv_start(act != BC_REPEAT, act == BC_THRU_MEASURE);
      if(bode_state(e) == BODE_ST_ERROR)
      {
        *detail = 3;
        return PC_E_BAD_ARG;
      }
      break;

    case BC_STOP:
      bv.single = 1;
      if(scopesettings.runstate)
        pc_hal_ui_set_run(0);
      bode_stop(e, 0);
      break;

    case BC_ABORT:
      bode_stop(e, 1);
      if(scopesettings.runstate)
        pc_hal_ui_set_run(0);
      break;

    case BC_THRU_ON:
    case BC_THRU_OFF:
      if(!e->thru.measured)
      {
        *detail = 4;
        return PC_E_STATE;
      }
      bode_thru_apply(e, act == BC_THRU_ON);
      break;

    case BC_THRU_CLEAR:
      bode_thru_clear(e);
      break;

    case BC_CLOSE:
      bode_view_close();
      break;

    default:
      break;
  }

  bv.dirty = 1;

  //Header, then the estimated duration of a sweep with the current settings
  cfg_now(&c);

  bode_proto_header(e, (uint8)((bv.active ? 1 : 0) | ((scopesettings.runstate && !bv.single) ? 2 : 0)), resp);
  resp[32] = resp[33] = resp[34] = resp[35] = 0;
  {
    uint32 est = bode_estimate_ms(&c);

    resp[32] = (uint8)est; resp[33] = (uint8)(est >> 8); resp[34] = (uint8)(est >> 16); resp[35] = (uint8)(est >> 24);
  }
  resp[36] = resp[37] = resp[38] = resp[39] = 0;
  return PC_OK;
}

uint32 bode_view_read(uint32 first, uint32 max_rows, uint8 *resp)
{
  bode_eng *e = bode_engine();

  bode_proto_header(e, (uint8)((bv.active ? 1 : 0) | ((scopesettings.runstate && !bv.single) ? 2 : 0)), resp);
  return BODE_HDR_BYTES + BODE_ROW_BYTES * bode_proto_rows(e, first, max_rows, resp + BODE_HDR_BYTES);
}
