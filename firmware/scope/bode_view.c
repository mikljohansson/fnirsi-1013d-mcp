//----------------------------------------------------------------------------------------------------------------------------------
// bode_view.c - the Frequency response mode (fw 0.2.66 as "Bode plot", redesigned in fw 0.2.69; see bode_view.h and
// docs/plans/bode-ui.md).
//
// Layout of the trace area while the view shows (x 2..729, y 48..479; geometry and texts in bode_ui.h):
//   y  48..60   the cursor flags C1 / C2 (while T CURSOR is on)
//   y  60..396  one pane, six divisions of 56 px: magnitude (dB, left axis, CH2 cyan), phase (deg, right axis, pink);
//               both scales from the data, the top division kept free for the warning line (y 69, top-middle)
//   x   6..726  the frequency axis, Log (decades with 2 and 5 lines) or Linear (1-2-5 steps); labels at y 400
//   y 342..393  the C1 / C2 readout box at the bottom left (x 10..261)
//   y 426..447  the cards chosen through MEASURES
//   y 452..471  the slider: Start..Stop over 1 Hz .. 10 MHz; a drag pans the span and sweeps it; fw 0.2.73: AUTO SET's
//               overview in the track (slider_ov.h), kept until the next AUTO SET or power-off
// Before the first sweep the pane shows one two-line instruction. Integer only: mdB, mdeg, mHz.
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>

#include "bode_view.h"
#include "bode_dsp.h"
#include "bode_sweep.h"
#include "bode_ui.h"
#include "slider_ov_draw.h"
#include "types.h"
#include "variables.h"
#include "display_lib.h"
#include "touchpanel.h"
#include "fnirsi_1013d_scope.h"
#include "scope_functions.h"
#include "menu.h"
#include "fpga_control.h"
#include "timer.h"
#include "ff.h"
#include "pc_hal.h"
#include "pc_hooks.h"
#include "pc_protocol.h"
#include "pc_cmds.h"
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


#define PHASE_COLOR       0x00FF80FF
#define FLAG_COLOR        0x00707070
#define NOTE_COLOR        0x00A0A0A0
#define CUR_COLOR         0x00606060

#define REDRAW_MS         150         //at most this often while sweeping
#define STEP_BUDGET_MS    20          //engine steps per main-loop pass
#define NOTE_MS           5000

#define NOTE_NONE         0xFF

static const uint8  pts_tab[4] = { 10, 20, 50, 100 };
static const char *const settle_names[BODE_SETTLE_COUNT] = { "Fast", "Normal", "Resonant", "High Q" };
static const char *const ch2_names[2] = { "Auto", "Hold" };

//AUTO SET's overview (bode-ui.md 3.2): 100 Hz .. 10 MHz, 20 points, Settle Fast, CH2 Auto, background on
#define AUTO_START_I      6
#define AUTO_STOP_I       21
#define AUTO_POINTS       20

typedef struct
{
  uint8  active;
  uint8  start_i, stop_i, points_i, settle, ch2;
  uint8  show, axis, cursor_on;
  uint8  trace;                     //fw 0.2.71: BU_TRACE_LINES, BU_TRACE_DOTS (settings word 216)
  uint8  single, was_running, last_run;
  uint8  dirty, auto_shown;
  uint8  drag;                      //the slider is being dragged: the axis follows Start / Stop
  int    cur[2];                   //C1, C2: point indices, -1 = to be placed
  uint32 drawn_ms;
  uint8  note;                      //BU_W_* of a note, NOTE_NONE
  uint32 note_ms;
  uint8  no_response;               //AUTO SET found only the floor (until the next sweep)

  uint32 gen_shown;                 //fw 0.2.71: the tone the generator chip shows (Hz), 0 = the user's generator

  //AUTO SET
  uint8  autoset;                   //the pass running: 1 the overview, 2..BODE_AUTO_PASSES the sharp-tank passes; 0 none
  uint8  as_thru;                   //the thru's Apply before the overview

  //What the view gives back
  uint8  s_run, s_trig, s_gen_en, s_gen_duty, s_disp[2];
  uint32 s_gen_freq;
  pc_hw_channel  s_ch[2];
  pc_hw_timebase s_tb;
} bv_state;

//Defaults: 100 Hz .. 1 MHz, 50 points, Normal settle, CH2 Auto, Overlaid, Log, cursors off
static bv_state bv = { .start_i = 6, .stop_i = 18, .points_i = 2, .settle = BODE_SETTLE_NORMAL, .ch2 = BODE_CH2_AUTO,
                       .cur = { -1, -1 }, .note = NOTE_NONE };

//fw 0.2.71: the table before AUTO SET's overview, given back when it is cancelled (bode-ui.md 3.2 step 6)
static bode_snap as_snap;

//fw 0.2.73 (slider-overview.md section 5, owner 10 F item 3): AUTO SET's overview, drawn in the slider track. `ov` is the
//last completed AUTO SET's (none after power-on or Restore default settings); `ov_new` is the one of the AUTO SET
//running, taken when its overview pass completes and made `ov` when AUTO SET ends (a cancel drops it)
typedef struct
{
  int    n;
  uint64 f[AUTO_POINTS];
  int32  m[AUTO_POINTS], p[AUTO_POINTS];
  uint8  ok[AUTO_POINTS], have[AUTO_POINTS];
} bv_overview;

static bv_overview ov, ov_new;
static uint8       ov_new_ok;

//The MEASURES list (bu_ms_defaults: -3dB, Peak, Q, Level)
static uint8 bv_ms[BU_MS_COUNT] = { 4, 1, 0, 2, 3, 0, 0, 0, 0, 0 };

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

  c->start_mhz  = bode_step_mhz(bv.start_i);
  c->stop_mhz   = bode_step_mhz(bv.stop_i);
  c->points     = pts_tab[bv.points_i & 3];
  c->settle     = bv.settle;
  c->ch2_mode   = bv.ch2;
  c->background = 1;
}

static void note_set(uint8 code)
{
  bv.note    = code;
  bv.note_ms = timer0ticks;
  bv.dirty   = 1;
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

const char *bode_view_unavailable_text(void)
{
  return scopesettings.waveviewmode ? bu_warn[BU_W_NOT_IN_VIEWER] : bu_warn[BU_W_NEEDS_FPGA];
}

void bode_view_note_roll(void)
{
  if(bv.active)
    note_set(BU_W_ROLL);
}

uint8 *bode_view_ms_ranks(void)
{
  return bv_ms;
}

static void bv_draw_bar_button(void)
{
  DISPLAYDATA save = displaydata;

  bv.auto_shown = (uint8)bode_view_auto_running();

  //AUTO SET's label ("Auto..." while the overview runs): only on page one, which redraws it when it returns
  if((scopesettings.rightmenustate == 0) && !scopesettings.waveviewmode)
  {
    display_set_screen_buffer((uint16 *)maindisplaybuffer);
    scope_auto_set_button(0);
  }

  displaydata = save;
}

static void bv_draw_acq_chip(void)
{
  DISPLAYDATA save = displaydata;

  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  scope_acqusition_settings(0);
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
  bv.note = NOTE_NONE;
  if(t.source == 2)
  {
    t.source = 0;
    pc_hal_trigger_write(&t, PC_TRG_SOURCE);
    note_set(BU_W_EXT);
  }

  //Nothing runs until RUN, AUTO SET or Single sweep: the post may be wired to something else
  if(scopesettings.runstate)
    pc_hal_ui_set_run(0);

  bv.active      = 1;
  bv.gen_shown   = 0;
  bv.single      = 0;
  bv.autoset     = 0;
  bv.was_running = 0;
  bv.last_run    = 0;
  bv.no_response = 0;
  bv.cur[0] = bv.cur[1] = -1;
  bv.dirty       = 1;
  bv.drawn_ms    = timer0ticks - 1000;

  //fw 0.2.71: the ACQ chip reads "Set by / the sweep" while the view shows
  bv_draw_acq_chip();
}

void bode_view_close(void)
{
  pc_hw_trigger t;
  int c;

  if(!bv.active)
    return;

  bode_stop(bode_engine(), 1);

  //An overview cut short gives the thru's Apply and the previous table back
  if(bv.autoset)
  {
    bode_thru_apply(bode_engine(), bv.as_thru);
    bode_snap_restore(bode_engine(), &as_snap);
  }

  bv.autoset = 0;
  bv.active  = 0;
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
  bv_draw_acq_chip();

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

static void bv_start_cfg(const bode_cfg *c, int single, int thru)
{
  bode_eng *e = bode_engine();
  const bode_cfg *l = bode_last_cfg(e);
  int r2 = scopesettings.channel2.samplevoltperdiv;

  //Another plan: the cursors are placed again
  if((l->start_mhz != c->start_mhz) || (l->stop_mhz != c->stop_mhz) || (l->points != c->points))
    bv.cur[0] = bv.cur[1] = -1;

  //RUN shows while a sweep runs (single: back to STOP at its end)
  if(!scopesettings.runstate)
    pc_hal_ui_set_run(1);

  bv.last_run    = 1;
  bv.no_response = 0;

  if(bode_start(e, c, thru, (r2 > 5) ? 5 : r2))
  {
    pc_hal_ui_set_run(0);
    bv.last_run = 0;
  }
  else
    bv.was_running = 1;

  bv.single = (uint8)(single || thru);
  bv.dirty  = 1;
}

static void bv_start(int single, int thru)
{
  bode_cfg c;

  cfg_now(&c);
  bv_start_cfg(&c, single, thru);
}

//The overview of AUTO SET
static void bv_auto_start(void)
{
  bode_eng *e = bode_engine();
  bode_cfg c;

  //A sweep in progress ends at once; the overview runs without the thru (it would be stale on this plan anyway)
  if(bode_state(e) == BODE_ST_RUNNING)
    bode_stop(e, 1);

  //The table as it is, for a cancel
  bode_snap_save(e, &as_snap);

  bv.as_thru = e->thru.applied;
  bode_thru_apply(e, 0);

  c.start_mhz  = bode_step_mhz(AUTO_START_I);
  c.stop_mhz   = bode_step_mhz(AUTO_STOP_I);
  c.points     = AUTO_POINTS;
  c.settle     = BODE_SETTLE_FAST;
  c.ch2_mode   = BODE_CH2_AUTO;
  c.background = 1;

  bv.autoset = 1;
  bv_start_cfg(&c, 1, 0);

  if(bode_state(e) != BODE_ST_RUNNING)
  {
    bv.autoset = 0;
    bode_thru_apply(e, bv.as_thru);
  }
}

//A cancelled AUTO SET (a second tap, STOP, leaving): the previous table and the thru's Apply come back, STOP
static void bv_auto_cancel(void)
{
  bode_eng *e = bode_engine();

  bode_stop(e, 1);
  bv.autoset = 0;
  ov_new_ok = 0;
  bv.was_running = 0;
  bv.single = 0;
  bode_thru_apply(e, bv.as_thru);
  bode_snap_restore(e, &as_snap);
  if(scopesettings.runstate)
    pc_hal_ui_set_run(0);
  bv.last_run = 0;
  note_set(BU_W_AUTO_CANCEL);
}

//The points as shown (thru applied when on)
static int      n_pts;
static uint64   p_f[BODE_MAX_POINTS];
static int32    p_m[BODE_MAX_POINTS], p_p[BODE_MAX_POINTS], p_x[BODE_MAX_POINTS];
static uint8    p_ok[BODE_MAX_POINTS], p_have[BODE_MAX_POINTS], p_vis[BODE_MAX_POINTS];
static uint16   p_flags[BODE_MAX_POINTS];

static void gather(void);

//The overview pass has completed: the shown points into ov_new
static void ov_take(void)
{
  int i;

  gather();
  ov_new.n = (n_pts < AUTO_POINTS) ? n_pts : AUTO_POINTS;
  for(i = 0; i < ov_new.n; i++)
  {
    ov_new.f[i]    = p_f[i];
    ov_new.m[i]    = p_m[i];
    ov_new.p[i]    = p_p[i];
    ov_new.ok[i]   = p_ok[i];
    ov_new.have[i] = p_have[i];
  }
  ov_new_ok = 1;
}

//AUTO SET has ended (not cancelled): its overview replaces the old one
static void ov_commit(void)
{
  if(ov_new_ok)
    ov = ov_new;
  ov_new_ok = 0;
}

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

//The overview has ended: span, Settle and CH2 range from it, then RUN (bode-ui.md 3.2)
static void bv_auto_finish(void)
{
  bode_eng *e = bode_engine();
  bode_autospan a;
  bode_cfg zc;
  int r;

  //Stopped (STOP, the host): cancelled, the previous table back. Failed: the settings are kept, the pass's points and
  //its error's warning show.
  if(bode_state(e) == BODE_ST_STOPPED)
  {
    bv_auto_cancel();
    return;
  }

  if(bode_state(e) != BODE_ST_DONE)
  {
    //An error in a sharp-tank pass: the overview pass had completed, so it counts
    ov_commit();
    bv.autoset = 0;
    bode_thru_apply(e, bv.as_thru);
    if(scopesettings.runstate)
      pc_hal_ui_set_run(0);
    return;
  }

  gather();
  if(bv.autoset == 1)
    ov_take();
  r = bode_auto_span(p_f, p_m, p_p, p_flags, p_have, n_pts, bv.start_i, bv.stop_i, &a);

  //fw 0.2.71, the sharp-tank rule: a band narrower than two point spacings is measured again around f0 (at most
  //BODE_AUTO_PASSES passes) before the span is set
  if((r == BODE_AS_SPAN) && bode_auto_zoom(&a, bv.autoset, &zc))
  {
    bv.autoset++;
    bv_start_cfg(&zc, 1, 0);
    if(bode_state(e) == BODE_ST_RUNNING)
      return;
  }

  bv.autoset = 0;
  ov_commit();
  bode_thru_apply(e, bv.as_thru);

  if(r == BODE_AS_NO_RESPONSE)
  {
    bv.no_response = 1;
    if(scopesettings.runstate)
      pc_hal_ui_set_run(0);
    return;
  }

  if(r == BODE_AS_FLAT)
    note_set(BU_W_NO_CORNER);

  //A screen change: the screen's values apply again; Points stay the user's
  host_cfg_valid = 0;
  bv.start_i = a.start_i;
  bv.stop_i  = a.stop_i;
  bv.settle  = a.settle;
  bv.ch2     = BODE_CH2_AUTO;

  //Then RUN, as the Scope's auto setup ends in RUN; the table and the cards come from the first real sweep
  if(!scopesettings.runstate)
    pc_hal_ui_set_run(1);
  bv_start(0, 0);
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

      if(bv.autoset)
      {
        bv.single = 0;
        bv_auto_finish();
      }
      else
      {
        if(bv.single || (st != BODE_ST_DONE))
        {
          if(scopesettings.runstate)
            pc_hal_ui_set_run(0);
        }
        else if(scopesettings.runstate)
          bv_start(0, 0);                     //RUN: the next sweep overwrites point by point

        bv.single = 0;
      }
    }
    else if(scopesettings.runstate && !bv.last_run)
      bv_start(0, 0);                         //RUN pressed (or SET_ACQ_STATE run)
  }

  bv.last_run = scopesettings.runstate;

  if(bv.auto_shown != (uint8)bode_view_auto_running())
    bv_draw_bar_button();

  if((bv.note != NOTE_NONE) && ((timer0ticks - bv.note_ms) > NOTE_MS))
  {
    bv.note  = NOTE_NONE;
    bv.dirty = 1;
  }
}

int bode_view_autoset(void)
{
  if(!bv.active)
    return 0;

  //A second tap cancels: the overview ends at once, the settings were not changed, the previous table comes back
  if(bv.autoset)
    bv_auto_cancel();
  else
    bv_auto_start();

  bv.dirty = 1;
  bv_draw_bar_button();
  return 1;
}

int bode_view_auto_running(void)
{
  return bv.active && bv.autoset && (bode_state(bode_engine()) == BODE_ST_RUNNING);
}

void bode_view_tcursor(void)
{
  bv.cursor_on ^= 1;
  bv.cur[0] = bv.cur[1] = -1;
  bv.dirty = 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Drawing

static void text_at(FONTDATA *f, uint32 x, uint32 y, const char *s)
{
  display_set_font(f);
  display_text(x, y, (int8 *)s);
}

static void text_c(FONTDATA *f, uint32 xc, uint32 y, const char *s)
{
  text_at(f, xc - ui_text_width(f, s) / 2, y, s);
}

static void text_r(FONTDATA *f, uint32 xr, uint32 y, const char *s)
{
  text_at(f, xr - ui_text_width(f, s), y, s);
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
  uint64   f0, f1;      //the axis ends, mHz
  int64    l0, l1;      //their log2, Q32
  uint8    linear;
  bu_scale mag, ph;
} bv_axes;

static int32 x_of(const bv_axes *a, uint64 f)
{
  if(a->linear)
    return bu_x_lin(a->f0, a->f1, f);

  return bu_x_log(a->l0, a->l1, bode_log2_q32(f));
}

static void axes(bv_axes *a)
{
  bode_cfg c;
  int32 mx = -2000000000, mn = 2000000000, px = -2000000000, pn = 2000000000;
  int   i, any = 0, use_ok = 0;

  cfg_now(&c);

  //The axis is the sweep's range: the last sweep's while its points show (also the overview's), else the settings';
  //while the slider is dragged the settings' (the points inside the new span stay drawn)
  if(!bv.drag && bode_count(bode_engine()) && (bode_state(bode_engine()) != BODE_ST_IDLE))
    c = *bode_last_cfg(bode_engine());

  a->f0 = (c.start_mhz < c.stop_mhz) ? c.start_mhz : c.stop_mhz;
  a->f1 = (c.start_mhz < c.stop_mhz) ? c.stop_mhz : c.start_mhz;
  a->l0 = bode_log2_q32(a->f0);
  a->l1 = bode_log2_q32(a->f1);
  a->linear = (uint8)(bv.axis == BU_AXIS_LINEAR);

  for(i = 0; i < n_pts; i++)
    p_vis[i] = (uint8)((p_f[i] >= a->f0) && (p_f[i] <= a->f1));

  //Scales from the valid points (the flagged ones only when nothing is valid: floor points would stretch the axis)
  for(i = 0; i < n_pts; i++)
    if(p_ok[i] && p_vis[i])
      use_ok = 1;

  for(i = 0; i < n_pts; i++)
    if(p_vis[i] && (use_ok ? p_ok[i] : p_have[i]))
    {
      any = 1;
      if(p_m[i] > mx) mx = p_m[i];
      if(p_m[i] < mn) mn = p_m[i];
      if(p_p[i] > px) px = p_p[i];
      if(p_p[i] < pn) pn = p_p[i];
    }

  if(!any)
    bu_scale_empty(&a->mag, &a->ph);
  else
  {
    bu_scale_mag(mx, mn, &a->mag);
    bu_scale_phase(px, pn, &a->ph);
  }

  for(i = 0; i < n_pts; i++)
    p_x[i] = x_of(a, p_f[i]);
}

static void freq_label(char *t, uint64 mhz)
{
  bu_freq_short(t, mhz);
}

static void freq_label_at(int32 x, const char *t)
{
  int32 w = (int32)ui_text_width(&font_2, t);

  display_set_fg_color(NOTE_COLOR);
  if(x - w / 2 < BU_PX)
    text_at(&font_2, BU_PX, BU_FL_Y, t);
  else if(x + w / 2 > BU_PX + BU_PW)
    text_r(&font_2, BU_PX + BU_PW, BU_FL_Y, t);
  else
    text_c(&font_2, (uint32)x, BU_FL_Y, t);
}

static void draw_grid(const bv_axes *a)
{
  char   t[24];
  uint32 i;
  uint32 gc = grid_color();

  if(scopesettings.gridenable)
  {
    display_set_fg_color(gc);
    for(i = 0; i <= BU_NDIV; i++)
      display_draw_horz_dots(BU_Y + i * BU_DIV, BU_PX, BU_PX + BU_PW, 4);
  }

  //fw 0.2.71, the grid rule (bode_ui.h bu_fgrid, ui-guidelines.md section 3): lines at the frequencies the axis names
  {
    bu_gline g[BU_GRID_MAX];
    int      n = bu_fgrid(a->f0, a->f1, a->linear, g), k;

    for(k = 0; k < n; k++)
    {
      int32 x = x_of(a, g[k].f);

      if(scopesettings.gridenable)
      {
        display_set_fg_color(gc);
        display_draw_vert_dots((uint32)x, BU_Y, BU_Y + BU_H, g[k].major ? BU_GRID_DOTS_MAJOR : BU_GRID_DOTS_MINOR);
      }

      if(g[k].label)
      {
        freq_label(t, g[k].f);
        freq_label_at(x, t);
      }
    }
  }

  //Level labels left in the magnitude colour, phase labels right in the phase colour, each top one with its unit
  for(i = 0; i < BU_NDIV; i++)
  {
    char *p;

    if(bv.show != BU_SHOW_PHASE)
    {
      p = fmt_int(t, (a->mag.top - (int32)i * a->mag.div) / 1000);
      if(!i)
        fmt_str(p, " dB");
      display_set_fg_color(CHANNEL2_COLOR);
      text_at(&font_2, BU_LABEL_L_X, BU_Y + i * BU_DIV + 2, t);
    }

    if(bv.show != BU_SHOW_MAG)
    {
      p = fmt_int(t, (a->ph.top - (int32)i * a->ph.div) / 1000);
      if(!i)
        fmt_str(p, " deg");
      display_set_fg_color(PHASE_COLOR);
      text_r(&font_2, BU_LABEL_R_X, BU_Y + i * BU_DIV + 2, t);
    }
  }
}

static void dot(int32 x, int32 y)
{
  display_fill_rect((uint32)(x - 1), (uint32)(y - 1), 3, 3);
}

//Valid points in the axis colour, joined in order (Trace style Lines) or loose (Dots); flagged points always loose grey
//dots (fw 0.2.71, owner: a grey dot is a point that does not count)
static void draw_curve(const bu_scale *s, const int32 *v, uint32 color, int wraps)
{
  int   i, prev = -1;
  int32 px = 0, py = 0;

  //Flagged points grey, no joining line
  display_set_fg_color(FLAG_COLOR);
  for(i = 0; i < n_pts; i++)
    if(p_have[i] && !p_ok[i] && p_vis[i])
      dot(p_x[i], bu_scale_y(s, v[i]));

  //Valid points joined in order; the phase line is not drawn across a wrap
  display_set_fg_color(color);
  for(i = 0; i < n_pts; i++)
  {
    int32 y;

    if(!p_ok[i] || !p_vis[i])
    {
      prev = -1;
      continue;
    }

    y = bu_scale_y(s, v[i]);

    if((bv.trace == BU_TRACE_LINES) && (prev >= 0) &&
       (!wraps || (((v[i] - v[prev]) < 180000) && ((v[prev] - v[i]) < 180000))))
      display_draw_line((uint32)px, (uint32)py, (uint32)p_x[i], (uint32)y);
    dot(p_x[i], y);

    px = p_x[i]; py = y;
    prev = i;
  }
}

static void triangle(int32 x, int32 y)
{
  int32 i;

  if(x < BU_PX + 4) x = BU_PX + 4;
  if(x > BU_PX + BU_PW - 4) x = BU_PX + BU_PW - 4;

  display_set_fg_color(WHITE_COLOR);
  for(i = 0; i < 5; i++)
    display_draw_horz_line((uint32)(y - i), (uint32)(x - i), (uint32)(x + i));
}

static bode_markers mk;

static void draw_markers(const bv_axes *a)
{
  int i;

  if(bv.show != BU_SHOW_PHASE)
  {
    for(i = 0; i < mk.n3db; i++)
      triangle(x_of(a, mk.f3db_mhz[i]), bu_scale_y(&a->mag, mk.ref_mdb - 3000) - 3);

    if(mk.have_peak)
      triangle(x_of(a, mk.peak_mhz), bu_scale_y(&a->mag, mk.peak_mdb) - 3);
  }

  if(bv.show != BU_SHOW_MAG)
    for(i = 0; i < BODE_PH_TARGETS; i++)
      if(mk.ph_mhz[i])
        triangle(x_of(a, mk.ph_mhz[i]), bu_scale_y(&a->ph, bode_wrap_mdeg(bode_ph_targets_mdeg[i])) - 3);
}

//The measured point nearest to column x, -1 = none
static int nearest(int32 x)
{
  int i, best = -1;
  int32 bd = 0x7FFFFFFF;

  for(i = 0; i < n_pts; i++)
    if(p_have[i] && p_vis[i])
    {
      int32 d = p_x[i] - x;

      if(d < 0) d = -d;
      if(d < bd)
      {
        bd = d;
        best = i;
      }
    }

  return best;
}

//Cursors to be placed: at a third and two thirds of the axis, snapped to measured points
static void cursors_place(void)
{
  int k;

  for(k = 0; k < 2; k++)
    if((bv.cur[k] < 0) || (bv.cur[k] >= n_pts) || !p_have[bv.cur[k]] || !p_vis[bv.cur[k]])
      bv.cur[k] = nearest(BU_PX + (BU_PW * (k + 1)) / 3);
}

static int cursors_shown(void)
{
  return bv.cursor_on && (bv.cur[0] >= 0) && (bv.cur[1] >= 0);
}

static void draw_cursors(const bv_axes *a)
{
  static const char *const names[2] = { "C1", "C2" };
  bu_ro_row ro[3];
  int k, i;

  for(k = 0; k < 2; k++)
  {
    int   c = bv.cur[k];
    int32 x = p_x[c];

    display_set_fg_color(CURSORS_COLOR);
    display_draw_vert_dots((uint32)x, BU_Y, BU_Y + BU_H, 3);

    display_set_fg_color(WHITE_COLOR);
    if(bv.show != BU_SHOW_PHASE)
      display_draw_rect((uint32)(x - 3), (uint32)(bu_scale_y(&a->mag, p_m[c]) - 3), 7, 7);
    if(bv.show != BU_SHOW_MAG)
      display_draw_rect((uint32)(x - 3), (uint32)(bu_scale_y(&a->ph, p_p[c]) - 3), 7, 7);

    //The flag: the handle a drag takes
    display_set_fg_color(CURSORS_COLOR);
    display_fill_rounded_rect((uint32)(x - BU_FLAG_W / 2), BU_FLAG_Y, BU_FLAG_W, BU_FLAG_H, 2);
    display_set_fg_color(BLACK_COLOR);
    text_c(&font_2, (uint32)x, BU_FLAG_Y, names[k]);
  }

  //The readout box at the bottom left (owner): C1, C2, C2-C1
  bu_readout(p_f[bv.cur[0]], p_m[bv.cur[0]], p_p[bv.cur[0]], p_flags[bv.cur[0]],
             p_f[bv.cur[1]], p_m[bv.cur[1]], p_p[bv.cur[1]], p_flags[bv.cur[1]], ro);

  display_set_fg_color(GREY_COLOR);
  display_fill_rounded_rect(BU_RO_X, BU_RO_Y, BU_RO_W, BU_RO_H, 2);

  for(i = 0; i < 3; i++)
  {
    uint32 y = BU_RO_Y + BU_RO_ROW0 + BU_RO_PITCH * i;

    display_set_fg_color((i < 2) ? CURSORS_COLOR : WHITE_COLOR);
    text_at(&font_2, BU_RO_X + BU_RO_COL_NAME, y, ro[i].name);
    display_set_fg_color(ro[i].grey ? NOTE_COLOR : WHITE_COLOR);
    text_at(&font_2, BU_RO_X + BU_RO_COL_F, y, ro[i].f);
    display_set_fg_color(ro[i].grey ? NOTE_COLOR : CHANNEL2_COLOR);
    text_at(&font_2, BU_RO_X + BU_RO_COL_DB, y, ro[i].db);
    display_set_fg_color(ro[i].grey ? NOTE_COLOR : PHASE_COLOR);
    text_at(&font_2, BU_RO_X + BU_RO_COL_PH, y, ro[i].ph);
  }
}

//The warning line: consequence then action, the most urgent first; "" when nothing applies
static const char *warning(char *buf)
{
  bode_eng *e = bode_engine();
  int i, unsettled = 0, floor = 0, ch1low = 0, clip2 = 0;
  uint8 st = bode_state(e);

  if(st == BODE_ST_ERROR)
  {
    switch(bode_error(e))
    {
      case BODE_ERR_OVERLOAD: return bu_warn[BU_W_OVERLOAD];
      case BODE_ERR_ARM:      return bu_warn[BU_W_ARM];
      case BODE_ERR_TIMEOUT:  return bu_warn[BU_W_TIMEOUT];
      case BODE_ERR_HOST:     return bu_warn[BU_W_HOST];
      case BODE_ERR_PLAN:     return bu_warn[BU_W_PLAN];
      case BODE_ERR_READOUT:  return bu_warn[BU_W_READOUT];
      default:                break;
    }
  }

  if(bode_is_thru_sweep(e) && (st == BODE_ST_RUNNING))
    return bu_warn[BU_W_THRU_SWEEP];

  if(bv.no_response)
    return bu_warn[BU_W_NO_RESPONSE];

  //What a tap just did (Auto set cancelled, nothing to save, the file not written, no corner found)
  if((bv.note == BU_W_AUTO_CANCEL) || (bv.note == BU_W_NOTHING_SAVED) || (bv.note == BU_W_SAVE_FAILED) ||
     (bv.note == BU_W_NO_CORNER))
    return bu_warn[bv.note];

  for(i = 0; i < n_pts; i++)
  {
    if(!p_have[i])
      continue;
    if(p_flags[i] & BODE_F_UNSETTLED)                       unsettled++;
    if(p_flags[i] & (BODE_F_NEAR_FLOOR | BODE_F_LOW_SNR))   floor++;
    if(p_flags[i] & BODE_F_CH1_LOW)                         ch1low = 1;
    if(p_flags[i] & (BODE_F_CLIPPED | BODE_F_CH2_OVERLOAD)) clip2 = 1;
  }

  if(clip2 && (bv.ch2 == BODE_CH2_HOLD))
    return bu_warn[BU_W_CH2_OVER];

  if(ch1low)
    return bu_warn[BU_W_CH1_LOW];

  if(unsettled)
    return bu_warn_unsettled(buf, unsettled), buf;

  if(floor && !bv.autoset)
    return bu_warn_floor(buf, floor), buf;

  if(e->thru.applied && (bode_thru_state(e) == BODE_THRU_STALE))
    return bu_warn[BU_W_THRU_STALE];

  if(bode_exhausted(e))
    return bu_warn[BU_W_OFFSET];

  if(bv.note != NOTE_NONE)
    return bu_warn[bv.note];

  return "";
}

static int table_state(void)
{
  int i, have = 0;

  for(i = 0; i < n_pts; i++)
    have += p_have[i];

  //fw 0.2.71: a single sweep fills its table anew (the cards read "sweeping" until it ends); RUN's repeats overwrite a
  //complete table point by point
  if(bode_state(bode_engine()) == BODE_ST_RUNNING)
    return ((have < n_pts) || (bv.single && (bode_done_count(bode_engine()) < n_pts))) ? BU_TABLE_FILLING :
                                                                                         BU_TABLE_COMPLETE;

  return have ? BU_TABLE_COMPLETE : BU_TABLE_NONE;
}

static int thru_card_state(void)
{
  bode_eng *e = bode_engine();

  if(!e->thru.measured)
    return BU_THRU_NONE;

  if(!e->thru.applied && !(bv.autoset && bv.as_thru))
    return BU_THRU_OFF;

  return (bode_thru_state(e) == BODE_THRU_STALE) ? BU_THRU_STALE : BU_THRU_APPLIED;
}

//A card (scope_draw_card's shape) with its value in a colour
static void card(uint32 x, uint32 block, const char *name, uint32 vcolor, const char *value)
{
  uint32 cw = ui_card_chip_w(&font_2, name);

  display_set_fg_color(GREY_COLOR);
  display_fill_rounded_rect(x, BU_CARDS_Y, UI_CARD_W, UI_CARD_H, 2);

  display_set_fg_color(block);
  display_fill_rounded_rect(x, BU_CARDS_Y, cw, UI_CARD_H, 2);
  display_fill_rect(x + cw - 2, BU_CARDS_Y, 2, UI_CARD_H);

  display_set_font(&font_2);
  display_set_fg_color(BLACK_COLOR);
  display_text(x + UI_CARD_PAD, BU_CARDS_Y + 4, (int8 *)name);

  display_set_fg_color(vcolor);
  display_text(x + cw + UI_CARD_GAP, BU_CARDS_Y + 4, (int8 *)value);
}

static void draw_cards(int table)
{
  static const uint32 blocks[3] = { CHANNEL2_COLOR, PHASE_COLOR, NOTE_COLOR };
  static const uint32 values[4] = { WHITE_COLOR, NOTE_COLOR, GREEN_COLOR, UI_C_AMBER };
  bode_eng *e = bode_engine();
  uint8 order[UI_MS_LIST_MAX];
  bu_card_in in;
  char t[24], more[4];
  int  n, fit, extra, i;

  in.table   = (uint8)table;
  in.autoset = bv.autoset;
  in.thru    = (uint8)thru_card_state();
  in.done    = bode_done_count(e);
  in.count   = bode_count(e);
  in.mk      = &mk;

  n   = ui_ms_order(bv_ms, BU_MS_COUNT, order);
  fit = ui_ms_fit(n, UI_MS_ROW_CARDS, &extra);

  for(i = 0; i < fit; i++)
  {
    int v = bu_card_text(order[i], &in, t);

    card(ui_ms_card_x(0, i), blocks[bu_ms_block(order[i])], bu_ms_names[order[i]], values[v & 3], t);
  }

  if(extra)
    card(ui_ms_card_x(0, fit), NOTE_COLOR, ui_ms_more_text(more, extra), WHITE_COLOR, "more");
}

//fw 0.2.73: an overview in the track (slider-overview.md section 5): the magnitude, or the phase in Phase only, fitted to
//the track's rows (only the shape counts: the track has no scale); valid points joined as on the chart (not across a
//flagged point, the phase not across a wrap), a flagged point one grey pixel. Columns on the slider's log scale.
static void draw_overview(int n, const uint64 *f, const int32 *v, const uint8 *ok, const uint8 *have, int32 wx, int32 ww)
{
  int    phase = (bv.show == BU_SHOW_PHASE), i, any = 0;
  uint32 full = phase ? PHASE_COLOR : CHANNEL2_COLOR, dim = phase ? SOV_PHASE_DIM : SOV_CH2_DIM;
  int32  vmax = 0, vmin = 0, pc = -1, pr = -1, pv = 0, lo, hi;

  //The fit over the valid points, or over the flagged ones when none is valid
  for(i = 0; i < n; i++)
    if(have[i] && ok[i])
    {
      if(!any || (v[i] > vmax)) vmax = v[i];
      if(!any || (v[i] < vmin)) vmin = v[i];
      any = 1;
    }

  if(!any)
    for(i = 0; i < n; i++)
      if(have[i])
      {
        if(!any || (v[i] > vmax)) vmax = v[i];
        if(!any || (v[i] < vmin)) vmin = v[i];
        any = 1;
      }

  for(i = 0; i < n; i++)
  {
    int32 c, r;

    if(!have[i])
      continue;

    c = sov_col(bu_slider_x_mhz(f[i]));
    r = sov_row_fit(v[i], vmax, vmin);

    if(!ok[i])
    {
      sov_draw_run(c, r, r, FLAG_COLOR, FLAG_COLOR, wx, ww);
      pc = -1;
      continue;
    }

    if((pc >= 0) && (c > pc) && (!phase || (((v[i] - pv) < 180000) && ((pv - v[i]) < 180000))))
    {
      int32 x, prev = pr;

      for(x = pc + 1; x <= c; x++)
      {
        int32 rr = sov_lerp(pc, pr, c, r, x);

        sov_join(prev, rr, &lo, &hi);
        sov_draw_run(x, lo, hi, full, dim, wx, ww);
        prev = rr;
      }
    }
    else
      sov_draw_run(c, r, r, full, dim, wx, ww);

    pc = c;
    pr = r;
    pv = v[i];
  }
}

//The slider row: the line, the overview, the window Start..Stop (the overview's span while AUTO SET runs)
static void draw_slider(void)
{
  int32 x0 = bu_slider_x(bv.start_i), x1 = bu_slider_x(bv.stop_i);
  int   phase = (bv.show == BU_SHOW_PHASE);

  //AUTO SET: the span of the pass running
  if(bv.autoset)
  {
    const bode_cfg *c = bode_last_cfg(bode_engine());

    x0 = bu_slider_x_mhz(c->start_mhz);
    x1 = bu_slider_x_mhz(c->stop_mhz);
  }

  sov_draw_row();

  //The overview pass running: its points so far, as they come; a sharp-tank pass: the overview it started from; else
  //the last completed AUTO SET's (none before the first)
  if(bv.autoset == 1)
    draw_overview(n_pts, p_f, phase ? p_p : p_m, p_ok, p_have, x0, x1 - x0);
  else if(bv.autoset && ov_new_ok)
    draw_overview(ov_new.n, ov_new.f, phase ? ov_new.p : ov_new.m, ov_new.ok, ov_new.have, x0, x1 - x0);
  else if(ov.n)
    draw_overview(ov.n, ov.f, phase ? ov.p : ov.m, ov.ok, ov.have, x0, x1 - x0);

  sov_draw_window(x0, x1 - x0);
}

static void draw_instruction(void)
{
  display_set_fg_color(ORANGE_COLOR);
  text_c(&font_3, BU_WARN_XC, BU_Y + 4, bu_instruction[0]);
  text_c(&font_3, BU_WARN_XC, BU_Y + 24, bu_instruction[1]);
}

//----------------------------------------------------------------------------------------------------------------------------------
// The top bar in this mode (fw 0.2.71, bode-ui.md 7)

int bode_view_gen_tone(uint32 *hz)
{
  bode_eng *e = bode_engine();
  int i;

  if(!bv.active || (bode_state(e) != BODE_ST_RUNNING) || ((i = bode_current(e)) < 0))
    return 0;

  if(hz)
    *hz = (uint32)((bode_plans(e)[i].f_mhz + 500) / 1000);

  return 1;
}

//The generator chip follows the tone (called with the plot's redraw, so at most every REDRAW_MS while sweeping)
static void bv_gen_chip(void)
{
  DISPLAYDATA save;
  uint32 hz = 0;

  if(!bode_view_gen_tone(&hz) || !hz)
    hz = 0;

  if(hz == bv.gen_shown)
    return;

  bv.gen_shown = hz;
  save = displaydata;
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  scope_generator_settings(0);
  displaydata = save;
}

void bode_view_top_note(int which)
{
  const char *t = which ? bu_gen_note : bu_acq_note;
  uint32 w = ui_text_width(&font_3, t) + 2 * BU_NOTE_PAD;
  uint32 x = which ? BU_NOTE_GEN_XR - w : BU_NOTE_ACQ_X;

  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_destination_buffer(displaybuffer1);
  display_copy_rect_from_screen(x, BU_NOTE_Y, w, BU_NOTE_H);

  display_set_fg_color(DARKGREY_COLOR);
  display_fill_rect(x, BU_NOTE_Y, w, BU_NOTE_H);
  display_set_fg_color(LIGHTGREY_COLOR);
  display_draw_rect(x, BU_NOTE_Y, w, BU_NOTE_H);
  display_set_fg_color(WHITE_COLOR);
  text_at(&font_3, x + BU_NOTE_PAD, BU_NOTE_Y + (BU_NOTE_H - 16) / 2, t);

  //Any tap closes it (and does nothing else), as a tap outside a dropdown
  do
    tp_i2c_read_status();
  while(!havetouch);
  tp_i2c_wait_for_touch_release();

  display_set_source_buffer(displaybuffer1);
  display_copy_rect_to_screen(x, BU_NOTE_Y, w, BU_NOTE_H);
}

static void bv_clear(void)
{
  uint32 y;

  for(y = 48; y < 480; y++)
    memset(&displaybuffertmp[y * 800 + 2], 0, 728 * 2);
}

//Everything into displaybuffertmp, then the rectangle x, y, w, h of it to the screen (the whole trace area normally)
static void draw_all_rect(uint32 rx, uint32 ry, uint32 rw, uint32 rh)
{
  bode_eng *e = bode_engine();
  bv_axes a;
  char    wb[80];
  const char *w;
  int     table;

  gather();
  axes(&a);
  table = table_state();

  display_set_screen_buffer(displaybuffertmp);
  bv_clear();
  draw_grid(&a);

  memset(&mk, 0, sizeof(mk));

  if(table != BU_TABLE_NONE)
  {
    if(bv.show != BU_SHOW_PHASE)
      draw_curve(&a.mag, p_m, CHANNEL2_COLOR, 0);
    if(bv.show != BU_SHOW_MAG)
      draw_curve(&a.ph, p_p, PHASE_COLOR, 1);

    //Markers from a complete table only: half a table misleads (a tank's rising skirt reads as a high-pass corner)
    if((table == BU_TABLE_COMPLETE) && !bv.autoset)
    {
      bode_markers_find(p_f, p_m, p_p, p_ok, n_pts, &mk);
      draw_markers(&a);
    }

    //The point being measured: a dotted grey line
    if(bode_current(e) >= 0)
    {
      display_set_fg_color(CUR_COLOR);
      display_draw_vert_dots((uint32)p_x[bode_current(e)], BU_Y, BU_Y + BU_H, 2);
    }

    if(bv.cursor_on)
    {
      cursors_place();
      if(cursors_shown())
        draw_cursors(&a);
    }
  }

  w = warning(wb);
  if(*w)
  {
    display_set_fg_color(ORANGE_COLOR);
    text_c(&font_2, BU_WARN_XC, BU_WARN_Y, w);
  }
  else if((table == BU_TABLE_NONE) && (bode_state(e) != BODE_ST_RUNNING))
    draw_instruction();

  draw_cards(table);
  draw_slider();
  bv_gen_chip();

  display_set_source_buffer(displaybuffertmp);
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_copy_rect_to_screen(rx, ry, rw, rh);

  bv.dirty    = 0;
  bv.drawn_ms = timer0ticks;
}

static void draw_all(void)
{
  draw_all_rect(2, 48, 728, 432);
}

//fw 0.2.71: while the MEASURES menu is open, the cards left of it follow each change
void bode_view_cards_refresh(void)
{
  DISPLAYDATA save;

  if(!bv.active || scopesettings.waveviewmode)
    return;

  save = displaydata;
  draw_all_rect(2, BU_CARDS_Y, UI_MSM_X - 2, UI_CARD_H);
  displaydata = save;
  bv.dirty = 1;
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
// Touch: while T CURSOR is on, a drag on a flag moves that cursor, a tap or drag elsewhere in the plot the nearer one;
// both snap to measured points

//The slider (bode-ui.md section 2): Start and Stop move together by whole steps while the axis follows; on release a
//sweep in progress restarts at the new span (the point in progress dropped, the table cleared), and while stopped one
//sweep runs there. Ignored during AUTO SET's overview.
static void slider_drag(void)
{
  bode_eng *e = bode_engine();
  int    s0 = bv.start_i, e0 = bv.stop_i, sh = 0, n;
  int32  x0 = (int32)xtouch, jx;

  if(bv.autoset)
  {
    tp_i2c_wait_for_touch_release();
    return;
  }

  //fw 0.2.73, tap to jump: a touch-down outside the window starts the drag as if the window had been dragged to centre
  //on it (to the nearest steps); the release then sweeps the new span as after a drag
  jx = sov_jump(x0, (int32)ytouch, bu_slider_x(s0), bu_slider_x(e0) - bu_slider_x(s0));

  bv.drag = 1;

  while(havetouch)
  {
    n = bu_slider_shift(s0, e0, jx + (int32)xtouch - x0);

    if(n != sh)
    {
      sh = n;
      bv.start_i = (uint8)(s0 + n);
      bv.stop_i  = (uint8)(e0 + n);
      bode_view_display();
    }

    tp_i2c_read_status();
  }

  bv.drag  = 0;
  bv.dirty = 1;

  if(!sh)
    return;

  //A screen change: the host's exact settings stop applying (a thru for the old span turns stale by its own rule)
  host_cfg_valid = 0;

  if(bode_state(e) == BODE_ST_RUNNING)
  {
    int single = bv.single || !scopesettings.runstate;

    bode_stop(e, 1);
    bv.was_running = 0;
    bv_start(single, 0);
  }
  else
    bv_start(1, 0);
}

int bode_view_touch(void)
{
  bv_axes a;
  int k, flag = 0;

  if(!bv.active)
    return 0;

  if(ytouch >= BU_SL_HIT_Y)
  {
    slider_drag();
    return 1;
  }

  if((ytouch >= BU_CARDS_Y) || !bv.cursor_on)
  {
    tp_i2c_wait_for_touch_release();
    return 1;
  }

  gather();
  axes(&a);
  cursors_place();

  if(!cursors_shown())
  {
    tp_i2c_wait_for_touch_release();
    return 1;
  }

  //The flag under the finger (44 x 44 around it), else the nearer cursor
  {
    int32 x = (int32)xtouch, d0 = p_x[bv.cur[0]] - x, d1 = p_x[bv.cur[1]] - x;

    if(d0 < 0) d0 = -d0;
    if(d1 < 0) d1 = -d1;
    k = (d1 < d0) ? 1 : 0;

    if(ytouch < BU_FLAG_Y + BU_FLAG_H / 2 + BU_FLAG_HIT)
    {
      if(d0 <= BU_FLAG_HIT)
        k = 0, flag = 1;
      else if(d1 <= BU_FLAG_HIT)
        k = 1, flag = 1;
    }
  }

  //fw 0.2.71: Move fast sets the drag step as on every plot. Fast: the cursor follows the finger. Slow: it moves a fifth
  //of the finger's travel from where the drag began (a flag: the cursor's own position; the plot: the touch point, so a
  //tap still moves the nearer cursor there). Either way it snaps to measured points.
  {
    int32 x0 = (int32)xtouch, from = x0;

    if(flag)
      from = p_x[bv.cur[k]];

    while(havetouch)
    {
      int32 dx = (int32)xtouch - x0;
      int   c = nearest(from + (scopesettings.movespeed ? dx / 5 : dx));

      if((c >= 0) && (c != bv.cur[k]))
      {
        bv.cur[k] = c;
        bode_view_display();
      }

      tp_i2c_read_status();
    }
  }

  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// SAVE WAVE: the sweep table as CSV (bode-ui.md 3.3), bode/FR00001.CSV onwards

static char *csv_fixed(char *p, int64 v, int dec)
{
  static const uint32 sc[4] = { 1, 10, 100, 1000 };
  uint64 a = (uint64)((v < 0) ? -v : v);
  uint32 s = sc[dec & 3], f;
  int k;

  if(v < 0)
    *p++ = '-';

  p = fmt_uint(p, (uint32)(a / s));
  if(dec)
  {
    *p++ = '.';
    f = (uint32)(a % s);
    for(k = dec - 1; k >= 0; k--)
    {
      *p++ = (char)('0' + (f / sc[k]) % 10);
    }
    *p = 0;
  }

  return p;
}

static void file_message(const char *l1, const char *l2)
{
  display_save_screen_buffer();
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_destination_buffer(displaybuffertmp);
  display_copy_rect_from_screen(260, 210, 280, 60);

  display_set_fg_color(0x00202020);
  display_fill_rounded_rect(260, 210, 279, 59, 3);
  display_set_fg_color(0x00303030);
  display_draw_rounded_rect(260, 210, 279, 59, 3);

  display_set_fg_color(WHITE_COLOR);
  text_at(&font_3, 270, 220, l1);
  text_at(&font_3, 270, 248, l2);

  //As the scope's file message: a tap when Settings ask for it, else 0.7 s
  if(scopesettings.confirmationmode)
  {
    while(1)
    {
      tp_i2c_read_status();
      if(havetouch)
        break;
    }

    tp_i2c_wait_for_touch_release();
  }
  else
    timer0_delay(700);

  display_set_source_buffer(displaybuffertmp);
  display_copy_rect_to_screen(260, 210, 280, 60);
  display_restore_screen_buffer();
}

void bode_view_save(void)
{
  static FIL  fp;
  static char buf[24576];
  static const char *const flag_names[15] =
  {
    "ch1_clipped", "clipped", "ch2_overload", "ch1_low", "ch2_small", "low_snr", "near_floor", "unsettled",
    "incoherent", "spliced", "fir_off", "no_cal", "cal_range", "cal_flagged", "retried"
  };
  bode_eng *e = bode_engine();
  const bode_cfg *c = bode_last_cfg(e);
  char  name[] = "bode/FR00000.CSV", *p = buf;
  FRESULT r;
  UINT  bw;
  uint32 n, k;
  int   i, any = 0;

  if(!bv.active)
    return;

  gather();
  for(i = 0; i < n_pts; i++)
    any |= p_have[i];

  if(!any || (bode_state(e) == BODE_ST_IDLE))
  {
    note_set(BU_W_NOTHING_SAVED);
    return;
  }

  //Header lines: the firmware, the bitstream, the sweep's settings, the thru, the elapsed time
  p = fmt_str(p, "# FNIRSI 1013D open firmware " PC_FW_VERSION ", FPGA 0x");
  {
    static const char hex[] = "0123456789ABCDEF";
    uint16 v = pc_hal_fpga_version();

    for(k = 0; k < 4; k++)
      *p++ = hex[(v >> (12 - 4 * k)) & 15];
  }
  p = fmt_str(p, "\r\n# Frequency response, H = CH2 / CH1\r\n# start_hz=");
  p = csv_fixed(p, (int64)(c->start_mhz / 10), 2);
  p = fmt_str(p, ", stop_hz=");
  p = csv_fixed(p, (int64)(c->stop_mhz / 10), 2);
  p = fmt_str(fmt_uint(fmt_str(p, ", points="), c->points), ", settle=");
  p = fmt_str(p, settle_names[c->settle % BODE_SETTLE_COUNT]);
  p = fmt_str(fmt_str(p, ", ch2_range="), ch2_names[c->ch2_mode & 1]);
  p = fmt_str(p, ", thru=");
  p = fmt_str(p, !e->thru.measured || !e->thru.applied ? "none" :
                 (bode_thru_state(e) == BODE_THRU_STALE) ? "stale" : "applied");
  p = fmt_str(fmt_uint(fmt_str(p, ", elapsed_ms="), bode_elapsed_ms(e)), "\r\n");
  p = fmt_str(p, "f_hz,mag_db,phase_deg,v1_db,v2_db,range1,range2,flags\r\n");

  //One row per measured point: READ_BODE's fields, the flags as the host's names joined by |
  for(i = 0; i < n_pts; i++)
  {
    bode_point pt;
    int first = 1;

    if(!bode_point_get(e, i, &pt))
      continue;

    p = csv_fixed(p, (int64)p_f[i], 3);
    p = csv_fixed(fmt_str(p, ","), pt.h_mdb, 3);
    p = csv_fixed(fmt_str(p, ","), pt.h_mdeg, 3);
    p = csv_fixed(fmt_str(p, ","), pt.v1_mdb, 3);
    p = csv_fixed(fmt_str(p, ","), pt.v2_mdb, 3);
    p = fmt_uint(fmt_str(p, ","), pt.r1);
    p = fmt_uint(fmt_str(p, ","), pt.r2);
    p = fmt_str(p, ",");

    for(k = 0; k < 15; k++)
      if(pt.flags & (1u << k))
      {
        if(!first)
          *p++ = '|';
        p = fmt_str(p, flag_names[k]);
        first = 0;
      }

    p = fmt_str(p, "\r\n");
  }

  //The next free number, as the diagnostics report numbers its files
  r = f_mkdir("bode");

  if((r == FR_OK) || (r == FR_EXIST))
  {
    for(n = 1; n < 100000; n++)
    {
      uint32 v = n;

      for(k = 0; k < 5; k++, v /= 10)
        name[11 - k] = (char)('0' + v % 10);

      r = f_open(&fp, name, FA_CREATE_NEW | FA_WRITE);
      if(r != FR_EXIST)
        break;
    }
  }

  if(r == FR_OK)
  {
    r = f_write(&fp, buf, (UINT)(p - buf), &bw);
    if((r == FR_OK) && (bw != (UINT)(p - buf)))
      r = FR_DENIED;
    if((f_close(&fp) != FR_OK) && (r == FR_OK))
      r = FR_DISK_ERR;
  }

  if(r == FR_OK)
    file_message("Sweep table saved", name);
  else
  {
    file_message("Failed to save the sweep table", name);
    note_set(BU_W_SAVE_FAILED);
  }

  bv.dirty = 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// CTRL page (bode-ui.md 4): Display (y 65), Sweep (y 125), Thru (y 185), Trace style (y 245, fw 0.2.71), Grid (y 305);
// the rest of the stepper and the 50 % slots stay
// empty (the sweep ranges both channels itself; CH2's Hold range is the CH2 menu's V/div)

#define CP_XC             (UI_BAR_X + UI_BAR_W / 2)
#define CP_EDGE_COLOR     0x00505050
#define CP_SAVE_X         (UI_FOLD_RIGHT - 228)

#define CP_DISPLAY_Y      65
#define CP_SWEEP_Y        125
#define CP_THRU_Y         185
#define CP_TRACE_Y        245         //fw 0.2.71: Trace style (the Scope's construction), Grid moved down a slot
#define CP_GRID_Y         305

#define ROW_CHOICE        0
#define ROW_SWITCH        1
#define ROW_VALUE         2
#define ROW_VALUE2        3
#define ROW_ACTION        4
#define ROW_ACTION2       5           //an action with a grey second line

#define FOLD_DISPLAY      0
#define FOLD_SWEEP        1
#define FOLD_THRU         2
#define FOLD_TRACE        3

typedef struct
{
  uint8       kind, on, dim, icon;
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

static void display_button(int active)
{
  cp_value_button(CP_DISPLAY_Y, "Display", bu_show_values[bv.show % BU_SHOW_COUNT], active);
}

static void sweep_button(int active)
{
  char t[12];

  fmt_str(fmt_uint(t, pts_tab[bv.points_i & 3]), " pt");
  cp_value_button(CP_SWEEP_Y, "Sweep", t, active);
}

static void thru_button(int active)
{
  cp_value_button(CP_THRU_Y, "Thru", thru_text(), active);
}

//Trace style as the Scope's button: "Trace" over "style", the value under them
static void trace_button(int active)
{
  cp_button(CP_TRACE_Y, active);
  display_set_fg_color(active ? BLACK_COLOR : WHITE_COLOR);
  text_c(&font_2, CP_XC, CP_TRACE_Y + 4, "Trace");
  text_c(&font_2, CP_XC, CP_TRACE_Y + 17, "style");
  display_set_fg_color(active ? BLACK_COLOR : PALEYELLOW_COLOR);
  text_c(&font_2, CP_XC, CP_TRACE_Y + 32, bu_trace_names[bv.trace % BU_TRACE_COUNT]);
}

//Grid as in the other modes (fw 0.2.63): a whole-button toggle, green on, grey off
static void grid_button(int active)
{
  uint32 fill = active ? ITEM_ACTIVE_COLOR : scopesettings.gridenable ? GREEN_COLOR : GREY_COLOR;

  display_set_fg_color(fill);
  display_fill_rounded_rect(UI_BAR_X, CP_GRID_Y, UI_BAR_W, UI_BTN_H, 2);
  display_set_fg_color(CP_EDGE_COLOR);
  display_draw_rounded_rect(UI_BAR_X, CP_GRID_Y, UI_BAR_W, UI_BTN_H, 2);
  display_set_fg_color((active || scopesettings.gridenable) ? BLACK_COLOR : WHITE_COLOR);
  text_c(&font_3, CP_XC, CP_GRID_Y + 17, "Grid");
}

void bode_ctrl_draw(void)
{
  display_button(0);
  sweep_button(0);
  thru_button(0);
  trace_button(0);
  grid_button(0);
}

static bv_row *add(bv_fold *d, int kind, const char *label)
{
  bv_row *r = &d->r[d->n++];

  r->kind  = (uint8)kind;
  r->label = label;
  r->on = r->dim = r->icon = 0;
  r->value[0] = 0;
  return r;
}

static void build(bv_fold *d)
{
  bode_eng *e = bode_engine();
  uint32 sep = 0, anchor = CP_DISPLAY_Y, w = 206, i;
  bv_row *r;

  memset(d->r, 0, sizeof(d->r));
  d->n = 0;

  if(d->id == FOLD_DISPLAY)
  {
    d->title = "Display";
    w = 228;

    for(i = 0; i < BU_SHOW_COUNT; i++)
    {
      r = add(d, ROW_CHOICE, bu_show_names[i]);
      r->on   = (uint8)(bv.show == i);
      r->icon = (uint8)(i + 1);
    }

    sep = d->n;
    fmt_str(add(d, ROW_VALUE, bu_axis_row)->value, bu_axis_names[bv.axis & 1]);
  }
  else if(d->id == FOLD_SWEEP)
  {
    d->title = "Sweep";
    anchor = CP_SWEEP_Y;
    f_step_text(add(d, ROW_VALUE2, "Start")->value, bv.start_i);
    f_step_text(add(d, ROW_VALUE2, "Stop")->value, bv.stop_i);
    fmt_uint(add(d, ROW_VALUE, "Points")->value, pts_tab[bv.points_i & 3]);
    fmt_str(add(d, ROW_VALUE, "Settle")->value, settle_names[bv.settle % BODE_SETTLE_COUNT]);
    fmt_str(add(d, ROW_VALUE, "CH2 range")->value, ch2_names[bv.ch2 & 1]);
    sep = d->n;
    r = add(d, ROW_ACTION2, "Single sweep");

    if((bode_state(e) == BODE_ST_RUNNING) && bv.single && !bv.autoset)
      fmt_str(r->value, bu_single_running);
    else
    {
      bode_cfg c;

      cfg_now(&c);
      bu_estimate_text(r->value, bode_estimate_ms(&c));
    }
  }
  else if(d->id == FOLD_TRACE)
  {
    d->title = "Trace style";
    anchor = CP_TRACE_Y;

    for(i = 0; i < BU_TRACE_COUNT; i++)
      add(d, ROW_CHOICE, bu_trace_names[i])->on = (uint8)(bv.trace == i);
  }
  else
  {
    uint8 ts = bode_thru_state(e);

    d->title = "Thru: both tips on the input";
    anchor = CP_THRU_Y;
    add(d, ROW_ACTION, "Measure thru");
    r = add(d, ROW_SWITCH, "Apply");
    r->on  = (uint8)(e->thru.measured && e->thru.applied);
    r->dim = (uint8)!e->thru.measured;
    fmt_str(r->value, (ts == BODE_THRU_VALID) ? "matches this sweep" : (ts == BODE_THRU_STALE) ? "other sweep settings" :
                      "none measured");
    add(d, ROW_ACTION, "Clear");
  }

  ui_fold_layout(&d->f, w, d->n, sep, anchor);
}

//The Display rows' icons: both curves, the magnitude, the phase (dotted)
static void row_icon(int icon, uint32 x, uint32 y)
{
  static const uint8 s[13] = { 4, 4, 4, 5, 6, 8, 10, 12, 14, 15, 16, 16, 16 };
  uint32 i;

  if((icon == 1) || (icon == 2))
  {
    display_set_fg_color(CHANNEL2_COLOR);
    display_draw_horz_line(y + 4, x, x + 10);
    display_draw_line(x + 10, y + 4, x + 24, y + 18);
  }

  if((icon == 1) || (icon == 3))
  {
    display_set_fg_color(PHASE_COLOR);
    for(i = 0; i < 13; i += 1)
      if(!(i & 1))
        display_fill_rect(x + 2 * i, y + s[i] + ((icon == 1) ? 0 : -2), 2, 2);
  }
}

static void row_draw(const bv_row *r, uint32 rx, uint32 ry, uint32 rw)
{
  int    chosen = (r->kind == ROW_CHOICE) && r->on;
  uint32 ty = ry + (UI_FOLD_ROW_H - 16) / 2;

  display_set_fg_color(chosen ? GREEN_COLOR : LIGHTGREY_COLOR);
  display_fill_rounded_rect(rx, ry, rw, UI_FOLD_ROW_H, 3);

  switch(r->kind)
  {
    case ROW_CHOICE:
      row_icon(r->icon, rx + 10, ry + 12);
      display_set_fg_color(chosen ? BLACK_COLOR : WHITE_COLOR);
      text_at(&font_3, rx + (r->icon ? 46 : 14), ty, r->label);
      break;

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

    case ROW_ACTION2:
      display_set_fg_color(WHITE_COLOR);
      text_c(&font_3, rx + rw / 2, ry + 5, r->label);
      display_set_fg_color(NOTE_COLOR);
      text_c(&font_2, rx + rw / 2, ry + 25, r->value);
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

  if(d->id == FOLD_DISPLAY)
  {
    //A variant: chosen, the panel closes; the frequency axis toggles in place
    if(row < BU_SHOW_COUNT)
    {
      bv.show = (uint8)row;
      return 1;
    }

    bv.axis ^= 1;
    return 0;
  }

  if(d->id == FOLD_TRACE)
  {
    bv.trace = (uint8)row;
    return 1;
  }

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
        else if((dir > 0) && (bv.start_i < BODE_START_MAX))
          bv.start_i++;
        if(bv.stop_i <= bv.start_i)
          bv.stop_i = (uint8)(bv.start_i + 1);
        return 0;

      case 1:
        if((dir < 0) && (bv.stop_i > BODE_STOP_MIN))
          bv.stop_i--;
        else if((dir > 0) && (bv.stop_i < BODE_STEPS - 1))
          bv.stop_i++;
        if(bv.start_i >= bv.stop_i)
          bv.start_i = (uint8)(bv.stop_i - 1);
        return 0;

      case 2: bv.points_i = (uint8)((bv.points_i + 1) & 3);                return 0;
      case 3: bv.settle = (uint8)((bv.settle + 1) % BODE_SETTLE_COUNT);    return 0;
      case 4: bv.ch2 ^= 1;                                                 return 0;

      default:
        //Single sweep: when stopped one sweep; while RUN repeats, the sweep in progress becomes the last one
        if(bv.autoset)
          return 1;
        if(bode_state(e) == BODE_ST_RUNNING)
          bv.single = 1;
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

  //Hit areas from the middle of the gap above a button to the middle of the gap below it (as ui_ctrl_hit_top)
  if((ytouch >= 58) && (ytouch < 120))
    fold_run(FOLD_DISPLAY, display_button);
  else if((ytouch >= 120) && (ytouch < 180))
    fold_run(FOLD_SWEEP, sweep_button);
  else if((ytouch >= 180) && (ytouch < 240))
    fold_run(FOLD_THRU, thru_button);
  else if((ytouch >= 240) && (ytouch < 300))
    fold_run(FOLD_TRACE, trace_button);
  else if((ytouch >= 300) && (ytouch < 360))
  {
    grid_button(1);
    tp_i2c_wait_for_touch_release();
    scopesettings.gridenable ^= 1;
    grid_button(0);
    bv.dirty = 1;
  }
  else
    tp_i2c_wait_for_touch_release();
}

//----------------------------------------------------------------------------------------------------------------------------------
// Settings sector words 219 (0xB000 | start << 7 | stop << 2 | points), 220 (0xB100 | settle | CH2 range << 2 | Display
// variant << 3 | cursors << 5 | frequency axis << 6), 216 (fw 0.2.71: 0xB200 | Trace style) and the MEASURES words
// 213..215 (bode_ui.h)

uint16 bode_view_setting_word(int i)
{
  if(i == 2)
    return (uint16)(0xB200 | (bv.trace & 1));

  if(i == 0)
    return (uint16)(0xB000 | ((bv.start_i & 0x1F) << 7) | ((bv.stop_i & 0x1F) << 2) | (bv.points_i & 3));

  return (uint16)(0xB100 | (bv.settle & 3) | ((bv.ch2 & 1) << 2) | ((bv.show & 3) << 3) | ((bv.cursor_on & 1) << 5) |
                  ((bv.axis & 1) << 6));
}

void bode_view_settings_restore(uint16 w0, uint16 w1, uint16 w2)
{
  //Word 216 (fw 0.2.71): 0xB200 | Trace style; bits 1..7 clear
  bv.trace = ((w2 & 0xFFFE) == 0xB200) ? (uint8)(w2 & 1) : BU_TRACE_LINES;

  if((w0 & 0xF000) == 0xB000)
  {
    uint8 a = (uint8)((w0 >> 7) & 0x1F), b = (uint8)((w0 >> 2) & 0x1F);

    if((a <= BODE_START_MAX) && (b >= BODE_STOP_MIN) && (b < BODE_STEPS) && (a < b))
    {
      bv.start_i = a;
      bv.stop_i  = b;
      bv.points_i = (uint8)(w0 & 3);
    }
  }

  //Bit 7 is never set (an erased word is 0xFFFF)
  if(((w1 & 0xFF00) == 0xB100) && !(w1 & 0x80))
  {
    bv.settle    = (uint8)(w1 & 3);
    bv.ch2       = (uint8)((w1 >> 2) & 1);
    bv.show      = (uint8)((w1 >> 3) & 3);
    bv.cursor_on = (uint8)((w1 >> 5) & 1);
    bv.axis      = (uint8)((w1 >> 6) & 1);

    if(bv.show >= BU_SHOW_COUNT)
      bv.show = BU_SHOW_OVERLAY;
  }
}

uint16 bode_view_ms_word(int i)
{
  return bu_ms_word(bv_ms, i);
}

void bode_view_ms_restore(const uint16 *w3)
{
  bu_ms_restore(bv_ms, w3);
  ui_ms_normalise(bv_ms, BU_MS_COUNT);
}

void bode_view_defaults(void)
{
  bv.start_i   = 6;
  bv.stop_i    = 18;
  bv.points_i  = 2;
  bv.settle    = BODE_SETTLE_NORMAL;
  bv.ch2       = BODE_CH2_AUTO;
  bv.show      = BU_SHOW_OVERLAY;
  bv.axis      = BU_AXIS_LOG;
  bv.cursor_on = 0;
  bv.trace     = BU_TRACE_LINES;
  host_cfg_valid = 0;
  ov.n = 0;                               //fw 0.2.73: no AUTO SET overview after Restore default settings
  bu_ms_defaults(bv_ms);
  bv.dirty = 1;
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

  for(i = 0; i < BODE_STEPS; i++)
  {
    int64 d = bode_log2_q32(bode_step_mhz(i)) - bode_log2_q32(mhz);

    if(d < 0) d = -d;
    if(d < bd)
    {
      bd = d;
      best = i;
    }
  }

  return (uint8)best;
}

//The host takes over: an AUTO SET overview in progress ends, the thru's Apply given back
static void bv_auto_drop(void)
{
  if(bv.autoset)
  {
    bv.autoset = 0;
    bode_thru_apply(bode_engine(), bv.as_thru);
  }
}

//The READ_BODE header. fw 0.2.71: before the first sweep since boot (sweep id 0) its settings fields (settle, CH2 range
//mode, start, stop, requested points) are the settings in effect (cfg_now), the ones BODE_CTRL's estimate and the Single
//sweep row's "about N s" describe; after a sweep they are that sweep's (bench 2026-10-19 20:51: the engine's defaults
//showed beside an estimate for other settings)
static void bv_header(uint8 *p)
{
  bode_eng *e = bode_engine();

  bode_proto_header(e, (uint8)((bv.active ? 1 : 0) | ((scopesettings.runstate && !bv.single) ? 2 : 0)), p);

  if(!bode_sweep_id(e))
  {
    bode_cfg c;

    cfg_now(&c);
    bode_proto_settings(&c, p);
  }
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
    if(bv.start_i > BODE_START_MAX) bv.start_i = BODE_START_MAX;
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

      bv_auto_drop();
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
      bv_auto_drop();
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

  bv_header(resp);
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

  bv_header(resp);
  return BODE_HDR_BYTES + BODE_ROW_BYTES * bode_proto_rows(e, first, max_rows, resp + BODE_HDR_BYTES);
}
