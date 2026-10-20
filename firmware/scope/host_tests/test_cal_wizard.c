//----------------------------------------------------------------------------------------------------------------------------------
// test_cal_wizard.c - the calibration wizard's state machine (cal_wiz.c, fw 0.2.59, plan section 6a): the four screens,
// Calibrate turning into Continue, retry after a failure, the stock-bitstream path, Cancel on every screen and during a
// measurement (never a commit), stray events, and the texts.
//----------------------------------------------------------------------------------------------------------------------------------

#include "test_util.h"
#include "cal_wiz.h"
#include "ui_fmt.h"

extern FONTDATA font_2, font_3;

//The result line's room: the 640 px step dialog, 24 px margins, the 28 px icon column (ui_dlg_step_draw)
#define RESULT_W   (640 - 48 - 28)
#define LINE_W     (640 - 48)

#define SPAN_X1    1408         //88 codes at range 2: x1 probe on the post
#define SPAN_X10    141         //8.8 codes: x10 probe (measured at range 5)
#define SPAN_NONE     8         //no probe

static const int      rc_ok[2]    = { ACAL_OK, ACAL_OK };
static const uint32_t match_ok[2] = { 3, 20 };

//Action log of one wizard run
static int  log_act[64];
static int  log_ch[64];
static int  log_n;

static void log_reset(void)
{
  log_n = 0;
}

//Takes the pending action and logs it; returns it
static int take(cal_wiz *w)
{
  uint8_t ch;
  int     a = cw_take(w, &ch);

  if(a != CW_ACT_NONE && log_n < 64)
  {
    log_act[log_n] = a;
    log_ch[log_n]  = ch;
    log_n++;
  }

  return a;
}

static int logged(int act)
{
  int i, n = 0;

  for(i=0;i<log_n;i++)
    n += (log_act[i] == act);

  return n;
}

static void ev(cal_wiz *w, int e)
{
  T_EQ(cw_event(w, e), 1);
}

#define B(e) (1u << (e))

//Exactly the buttons in mask (bit per event) are offered, and every other event is ignored without a change
static void check_buttons(cal_wiz *w, uint32_t mask)
{
  cal_wiz before;
  int     e;

  memcpy(&before, w, sizeof(before));

  for(e=0;e<CW_EVENTS;e++)
  {
    T_EQ(cw_button(w, e), (mask >> e) & 1);

    if(!((mask >> e) & 1))
    {
      T_EQ(cw_event(w, e), 0);
      T_MEM(w, &before, sizeof(*w));
    }
  }

  //Not events at all
  T_EQ(cw_event(w, -1), 0);
  T_EQ(cw_event(w, CW_EV_MEASURING), 0);
  T_EQ(cw_event(w, CW_EVENTS), 0);
  T_MEM(w, &before, sizeof(*w));
}

//Screen 1 through a passed zero step, Continue taken (PREPARE pending)
static void zero_pass(cal_wiz *w)
{
  ev(w, CW_EV_CALIBRATE);
  T_EQ(take(w), CW_ACT_ZERO);
  cw_zero_result(w, rc_ok, match_ok);
  T_EQ(cw_action_button(w), CW_EV_CONTINUE);
  ev(w, CW_EV_CONTINUE);
  T_EQ(take(w), CW_ACT_PREPARE);
}

//The gain screen of the channel in focus through a passed measurement
static void gain_pass(cal_wiz *w)
{
  uint8_t ch = w->ch;

  ev(w, CW_EV_CALIBRATE);
  T_EQ(take(w), CW_ACT_GAIN_LEVEL);
  T_EQ(log_ch[log_n - 1], ch);
  cw_level_result(w, SPAN_X1);
  T_EQ(take(w), CW_ACT_GAIN_MEASURE);
  T_EQ(log_ch[log_n - 1], ch);
  cw_gain_result(w, ACAL_OK);
  T_EQ(take(w), CW_ACT_NONE);
}

static void test_happy_path(void)
{
  cal_wiz w;

  log_reset();
  cw_init(&w, 1);

  //Screen 1: Cancel and Calibrate
  T_EQ(cw_screen(&w), 1);
  T_STR(cw_title(&w), "Zero level");
  T_EQ(cw_action_button(&w), CW_EV_CALIBRATE);
  check_buttons(&w, B(CW_EV_CANCEL) | B(CW_EV_CALIBRATE));

  //Running: "Measuring...", only Cancel
  ev(&w, CW_EV_CALIBRATE);
  T_EQ(cw_running(&w), 1);
  T_EQ(cw_action_button(&w), CW_EV_MEASURING);
  T_EQ(cw_screen(&w), 1);
  check_buttons(&w, B(CW_EV_CANCEL));
  T_EQ(take(&w), CW_ACT_ZERO);
  T_EQ(take(&w), CW_ACT_NONE);

  //Passed: Calibrate becomes Continue
  cw_zero_result(&w, rc_ok, match_ok);
  T_EQ(cw_running(&w), 0);
  T_EQ(w.zero_st[0], CW_Z_PASS);
  T_EQ(w.zero_st[1], CW_Z_PASS);
  T_EQ(w.match16[1], 20);
  check_buttons(&w, B(CW_EV_CANCEL) | B(CW_EV_CONTINUE));

  //Screen 2: CH1 gain
  ev(&w, CW_EV_CONTINUE);
  T_EQ(take(&w), CW_ACT_PREPARE);
  T_EQ(cw_screen(&w), 2);
  T_STR(cw_title(&w), "CH1 gain");
  T_EQ(w.ch, 0);
  check_buttons(&w, B(CW_EV_CANCEL) | B(CW_EV_CALIBRATE));
  gain_pass(&w);
  T_EQ(w.gain_st[0], CW_G_PASS);
  check_buttons(&w, B(CW_EV_CANCEL) | B(CW_EV_CONTINUE));

  //Screen 3: CH2 gain, nothing to run on the way
  ev(&w, CW_EV_CONTINUE);
  T_EQ(take(&w), CW_ACT_NONE);
  T_EQ(cw_screen(&w), 3);
  T_STR(cw_title(&w), "CH2 gain");
  check_buttons(&w, B(CW_EV_CANCEL) | B(CW_EV_CALIBRATE));
  gain_pass(&w);
  T_EQ(w.gain_st[1], CW_G_PASS);

  //Screen 4: Done, Save calibration commits
  ev(&w, CW_EV_CONTINUE);
  T_EQ(cw_screen(&w), 4);
  T_STR(cw_title(&w), "Done");
  T_EQ(cw_action_button(&w), CW_EV_SAVE);
  check_buttons(&w, B(CW_EV_CANCEL) | B(CW_EV_SAVE));
  ev(&w, CW_EV_SAVE);
  T_EQ(take(&w), CW_ACT_COMMIT);
  T_EQ(w.state, CW_END);
  T_EQ(cw_screen(&w), 0);
  T_EQ(cw_action_button(&w), -1);
  check_buttons(&w, 0);

  T_EQ(log_n, 7);
  T_EQ(logged(CW_ACT_COMMIT), 1);
  T_EQ(logged(CW_ACT_DISCARD), 0);
  T_EQ(log_act[0], CW_ACT_ZERO);
  T_EQ(log_act[1], CW_ACT_PREPARE);
  T_EQ(log_act[2], CW_ACT_GAIN_LEVEL);
  T_EQ(log_ch[2], 0);
  T_EQ(log_act[4], CW_ACT_GAIN_LEVEL);
  T_EQ(log_ch[4], 1);
  T_EQ(log_act[5], CW_ACT_GAIN_MEASURE);
  T_EQ(log_ch[5], 1);
}

static void test_zero_fail_retry(void)
{
  cal_wiz w;
  int     rc[2] = { ACAL_OK, ACAL_E_NOISE };
  char    t[96];

  log_reset();
  cw_init(&w, 1);
  ev(&w, CW_EV_CALIBRATE);
  take(&w);
  cw_zero_result(&w, rc, match_ok);

  //One channel failed: Calibrate stays (no Skip, no Continue), both reasons in words
  T_EQ(w.state, CW_ZERO);
  T_EQ(w.zero_st[0], CW_Z_PASS);
  T_EQ(w.zero_st[1], CW_Z_FAIL);
  check_buttons(&w, B(CW_EV_CANCEL) | B(CW_EV_CALIBRATE));
  cw_zero_line(&w, 0, t);
  T_STR(t, "CH1: zero level done");
  cw_zero_line(&w, 1, t);
  T_STR(t, "CH2: too much noise: unplug the probes, move chargers and USB away");

  //Retry measures both channels again and clears the old results while it runs
  ev(&w, CW_EV_CALIBRATE);
  T_EQ(w.zero_st[0], CW_Z_NONE);
  T_EQ(w.zero_st[1], CW_Z_NONE);
  cw_zero_line(&w, 0, t);
  T_STR(t, "");
  T_EQ(take(&w), CW_ACT_ZERO);
  cw_zero_result(&w, rc_ok, match_ok);
  check_buttons(&w, B(CW_EV_CANCEL) | B(CW_EV_CONTINUE));
  T_EQ(logged(CW_ACT_ZERO), 2);
}

//fw 0.2.60: a noise or drift failure names the range, rate and core with the values; without a detail (or for the
//other reasons) the plain words stay. The lines fit the dialog in the 16 px font
static void test_zero_fail_detail(void)
{
  cal_wiz   w;
  int       rc[2] = { ACAL_E_NOISE, ACAL_E_DRIFT };
  acal_fail f0 = { ACAL_GATED, ACAL_CORE_B, 52, 17, ACAL_PT_NONE, 0, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
  acal_fail f1 = { ACAL_FULL, ACAL_CORE_A, -13, 18, ACAL_PT_NONE, 0, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
  acal_fail fw = { ACAL_FULL, ACAL_CORE_B, 1600, 1600, ACAL_PT_NONE, 0, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
  char      t[96];

  cw_init(&w, 1);
  ev(&w, CW_EV_CALIBRATE);
  take(&w);
  cw_zero_detail(&w, 0, 5, &f0);
  cw_zero_detail(&w, 1, 0, &f1);
  cw_zero_result(&w, rc, match_ok);
  cw_zero_line(&w, 0, t);
  T_STR(t, "CH1: too much noise, range 6 at 2 MSa/s: core B 3.3 codes, 1.1 per capture");
  T_TRUE(ui_text_width(&font_3, t) <= RESULT_W);
  //fw 0.2.100: the note names the limit of the rate (the Diagnostics limit) and what helps
  cw_zero_note(&w, 0, t);
  T_STR(t, "Noise RMS, inputs open, limit 1.0 code: move chargers and the USB cable away.");
  T_TRUE(ui_text_width(&font_2, t) <= RESULT_W);
  cw_zero_line(&w, 1, t);
  T_STR(t, "CH2: zero still moving, range 1 at 200 MSa/s: core A 0.8 code");
  T_TRUE(ui_text_width(&font_3, t) <= RESULT_W);

  //The widest numbers: 100.0 codes at 200 MSa/s
  ev(&w, CW_EV_CALIBRATE);
  take(&w);
  cw_zero_detail(&w, 1, 5, &fw);
  rc[1] = ACAL_E_NOISE;
  cw_zero_result(&w, rc, match_ok);
  cw_zero_line(&w, 1, t);
  T_STR(t, "CH2: too much noise, range 6 at 200 MSa/s: core B 100.0 codes, 100.0 per capture");
  T_TRUE(ui_text_width(&font_3, t) <= RESULT_W);
  cw_zero_note(&w, 1, t);
  T_STR(t, "Noise RMS, inputs open, limit 3.0 code: move chargers and the USB cable away.");

  //CH1 got no detail in this run (a Calibrate clears it): the plain words
  cw_zero_line(&w, 0, t);
  T_STR(t, "CH1: too much noise: unplug the probes, move chargers and USB away");

  //Details only outside a running zero step are ignored
  cw_zero_detail(&w, 0, 3, &f0);
  T_EQ(w.zero_det[0], 0);

  //fw 0.2.61: a full-rate capture the encode-phase heal could not repair
  ev(&w, CW_EV_CALIBRATE);
  take(&w);
  rc[0] = ACAL_E_TIMING;
  cw_zero_result(&w, rc, match_ok);
  cw_zero_line(&w, 0, t);
  T_STR(t, "CH1: ADC capture timing: the 200 MSa/s data stays garbled");
  T_TRUE(ui_text_width(&font_3, t) <= RESULT_W);

  //fw 0.2.67: the readout fault near code 128; since 0.2.79 also a 0x153A core with no clean capture stage (fail fast)
  ev(&w, CW_EV_CALIBRATE);
  take(&w);
  rc[0] = ACAL_E_READOUT;
  cw_zero_result(&w, rc, match_ok);
  cw_zero_line(&w, 0, t);
  T_STR(t, "CH1: ADC readout fault near code 128: check the FPGA bitstream");
  T_TRUE(ui_text_width(&font_3, t) <= RESULT_W);

  //Other reasons keep their words
  ev(&w, CW_EV_CALIBRATE);
  take(&w);
  cw_zero_detail(&w, 0, 2, &f0);
  rc[0] = ACAL_E_ZERO;
  cw_zero_result(&w, rc, match_ok);
  cw_zero_line(&w, 0, t);
  T_STR(t, "CH1: the zero level is out of range");
}

//fw 0.2.67: the note under a failed channel: range, rate, point, PWM and the raw per-core numbers (the 2026-10-19 17:55
//failure lines); nothing for a passed channel or without a measured point. Widest case fits the dialog in the 13 px font
static void test_zero_fail_note(void)
{
  cal_wiz   w;
  int       rc[2] = { ACAL_E_NOISE, ACAL_E_CLIP };
  acal_fail f0, f1;
  char      t[128];

  memset(&f0, 0, sizeof(f0));
  f0.cls = ACAL_FULL; f0.core = ACAL_CORE_B; f0.value = 115; f0.within16 = 115;
  f0.point = ACAL_PT_F_ZERO; f0.pwm = 812;
  f0.mean16[0] = 2082; f0.mean16[1] = 2014;
  f0.lo[0] = 126; f0.hi[0] = 135; f0.lo[1] = 101; f0.hi[1] = 152;
  memset(&f1, 0, sizeof(f1));
  f1.cls = ACAL_GATED; f1.core = ACAL_CORE_B; f1.point = ACAL_PT_G_LO; f1.pwm = 1062;
  f1.mean16[0] = 2693; f1.mean16[1] = 2400; f1.lo[0] = 160; f1.hi[0] = 176; f1.lo[1] = 0; f1.hi[1] = 255;
  f1.clipped[1] = 12;

  cw_init(&w, 1);
  ev(&w, CW_EV_CALIBRATE);
  take(&w);
  cw_zero_detail(&w, 0, 0, &f0);
  cw_zero_detail(&w, 1, 2, &f1);
  cw_zero_result(&w, rc, match_ok);

  cw_zero_line(&w, 0, t);
  T_STR(t, "CH1: too much noise, range 1 at 200 MSa/s: core B 7.2 codes, 7.2 per capture");
  cw_zero_note(&w, 0, t);
  T_STR(t, "Range 1, 200 MSa/s, zero point, PWM 812: A 130.1 (126..135), B 125.9 (101..152)");
  T_TRUE(ui_text_width(&font_2, t) <= RESULT_W);
  cw_zero_line(&w, 1, t);
  T_STR(t, "CH2: the input is not at 0 V: unplug the probe");
  cw_zero_note(&w, 1, t);
  T_STR(t, "Range 3, 2 MSa/s, slope point, PWM 1062: A 168.3 (160..176), B 150.0 (0..255, 12 clip)");
  T_TRUE(ui_text_width(&font_2, t) <= RESULT_W);

  //The widest: the highest slope point (ACAL_PWM_MAX + 150), both cores clipped in every sample of a full-rate point
  f1.pwm = ACAL_PWM_MAX + 150; f1.mean16[0] = f1.mean16[1] = 255 * 16; f1.lo[0] = 0; f1.hi[0] = 255;
  f1.clipped[0] = f1.clipped[1] = 32000; f1.cls = ACAL_FULL;
  ev(&w, CW_EV_CALIBRATE);
  take(&w);
  cw_zero_detail(&w, 1, 5, &f1);
  cw_zero_result(&w, rc, match_ok);
  cw_zero_note(&w, 1, t);
  T_TRUE(ui_text_width(&font_2, t) <= RESULT_W);

  //No note: no detail in this run (CH1), a point that was not measured, a passed channel
  cw_zero_note(&w, 0, t);
  T_STR(t, "");
  f1.point = ACAL_PT_NONE;
  rc[0] = ACAL_OK;
  ev(&w, CW_EV_CALIBRATE);
  take(&w);
  cw_zero_detail(&w, 0, 0, &f0);
  cw_zero_detail(&w, 1, 1, &f1);
  cw_zero_result(&w, rc, match_ok);
  cw_zero_note(&w, 0, t);
  T_STR(t, "");
  cw_zero_note(&w, 1, t);
  T_STR(t, "");

  //fw 0.2.79: FPGA 0x153A capture stages. A passed channel that trained them names them; a channel whose training found
  //no clean stage fails with the readout wording and names the cores; without the stages (other bitstreams) no note
  rc[0] = ACAL_OK;
  rc[1] = ACAL_E_READOUT;
  ev(&w, CW_EV_CALIBRATE);
  take(&w);
  cw_cap_result(&w, 0, CW_C_DONE, 0x08E4, 0);
  cw_cap_result(&w, 1, CW_C_FAIL, 0x08E4, 0x0C);
  cw_zero_result(&w, rc, match_ok);
  cw_zero_note(&w, 0, t);
  T_STR(t, "Capture stages: CH1 A4 B4");
  cw_zero_note(&w, 1, t);
  T_STR(t, "Capture stages: no clean stage for CH2 A, CH2 B");
  T_TRUE(ui_text_width(&font_2, t) <= RESULT_W);
  cw_zero_line(&w, 1, t);
  T_STR(t, "CH2: ADC readout fault near code 128: check the FPGA bitstream");
  rc[1] = ACAL_OK;
  ev(&w, CW_EV_CALIBRATE);
  take(&w);
  cw_cap_result(&w, 0, CW_C_NONE, 0, 0);
  cw_cap_result(&w, 1, CW_C_DONE, 0x08E4 | 0x2000, 0);
  cw_zero_result(&w, rc, match_ok);
  cw_zero_note(&w, 0, t);
  T_STR(t, "");
  cw_zero_note(&w, 1, t);
  T_STR(t, "Capture stages: CH2 A3 B4 A-clk");

  //fw 0.2.81: the band training's result on the owner's unit (CH1 A4 garbles at codes 127..135): 0x08E3
  rc[0] = rc[1] = ACAL_OK;
  cw_init(&w, 1);
  ev(&w, CW_EV_CALIBRATE);
  take(&w);
  cw_cap_result(&w, 0, CW_C_DONE, 0x08E3, 0);
  cw_cap_result(&w, 1, CW_C_DONE, 0x08E3, 0);
  cw_zero_result(&w, rc, match_ok);
  cw_zero_note(&w, 0, t);
  T_STR(t, "Capture stages: CH1 A3 B4");
  cw_zero_note(&w, 1, t);
  T_STR(t, "Capture stages: CH2 A3 B4");
}

static void test_gain_nosignal_and_fail(void)
{
  cal_wiz w;
  char    t[96];

  log_reset();
  cw_init(&w, 1);
  zero_pass(&w);

  //No probe on the post: "No signal on CH1: tip on the post?", Calibrate stays, no measurement ran
  ev(&w, CW_EV_CALIBRATE);
  T_EQ(take(&w), CW_ACT_GAIN_LEVEL);
  cw_gain_line(&w, t);
  T_STR(t, "");                                               //nothing while it runs
  cw_level_result(&w, SPAN_NONE);
  T_EQ(take(&w), CW_ACT_NONE);
  T_EQ(w.state, CW_GAIN);
  T_EQ(w.gain_st[0], CW_G_NOSIG);
  check_buttons(&w, B(CW_EV_CANCEL) | B(CW_EV_CALIBRATE));
  cw_gain_line(&w, t);
  T_STR(t, "No signal on CH1: tip on the post?");

  //A x10 probe passes the level check; the measurement then fails: the reason, Calibrate stays
  ev(&w, CW_EV_CALIBRATE);
  T_EQ(w.gain_st[0], CW_G_NONE);
  take(&w);
  cw_level_result(&w, SPAN_X10);
  T_EQ(take(&w), CW_ACT_GAIN_MEASURE);
  cw_gain_result(&w, ACAL_E_GAIN_DRIFT);
  T_EQ(w.gain_st[0], CW_G_FAIL);
  T_EQ(w.gain_rc[0], ACAL_E_GAIN_DRIFT);
  check_buttons(&w, B(CW_EV_CANCEL) | B(CW_EV_CALIBRATE));
  cw_gain_line(&w, t);
  T_STR(t, "CH1: the post signal moved: hold the tip still");

  //Retry passes: Continue to CH2, whose own no-signal line names CH2
  gain_pass(&w);
  cw_gain_line(&w, t);
  T_STR(t, "CH1 gain done");
  ev(&w, CW_EV_CONTINUE);
  ev(&w, CW_EV_CALIBRATE);
  take(&w);
  cw_level_result(&w, 0);
  cw_gain_line(&w, t);
  T_STR(t, "No signal on CH2: tip on the post?");
  T_EQ(w.gain_st[0], CW_G_PASS);                              //CH1's result stays
  T_EQ(logged(CW_ACT_GAIN_MEASURE), 2);
}

static void test_gain_unavailable(void)
{
  cal_wiz w;

  //Stock bitstream: one screen says the gain step is not possible, Continue goes to Done, Save still commits the zeros
  log_reset();
  cw_init(&w, 0);
  zero_pass(&w);
  T_EQ(w.state, CW_GAIN_NA);
  T_EQ(cw_screen(&w), 2);
  T_STR(cw_instruction(&w, 0), "Gain calibration needs the open FPGA bitstream.");
  check_buttons(&w, B(CW_EV_CANCEL) | B(CW_EV_CONTINUE));
  ev(&w, CW_EV_CONTINUE);
  T_EQ(take(&w), CW_ACT_NONE);
  T_EQ(cw_screen(&w), 4);
  ev(&w, CW_EV_SAVE);
  T_EQ(take(&w), CW_ACT_COMMIT);
  T_EQ(logged(CW_ACT_GAIN_LEVEL), 0);
  T_EQ(logged(CW_ACT_GAIN_MEASURE), 0);
}

//Drives a fresh wizard to stop k of a full run (0 screen 1 ... 9 Done); returns 0 past the end
static int drive(cal_wiz *w, int k)
{
  cw_init(w, 1);

  if(k-- == 0) return 1;                  //screen 1
  ev(w, CW_EV_CALIBRATE);
  if(k-- == 0) return 1;                  //zero step running
  take(w);
  cw_zero_result(w, rc_ok, match_ok);
  if(k-- == 0) return 1;                  //screen 1 passed
  ev(w, CW_EV_CONTINUE);
  take(w);
  if(k-- == 0) return 1;                  //screen 2
  ev(w, CW_EV_CALIBRATE);
  if(k-- == 0) return 1;                  //level check running
  take(w);
  cw_level_result(w, SPAN_X1);
  if(k-- == 0) return 1;                  //measurement running
  take(w);
  cw_gain_result(w, ACAL_OK);
  if(k-- == 0) return 1;                  //screen 2 passed
  ev(w, CW_EV_CONTINUE);
  if(k-- == 0) return 1;                  //screen 3
  gain_pass(w);
  ev(w, CW_EV_CONTINUE);
  if(k-- == 0) return 1;                  //Done
  return 0;
}

static void test_cancel_everywhere(void)
{
  cal_wiz w;
  int     k;

  //Cancel on every screen and during every measurement: DISCARD, never COMMIT; late results change nothing
  for(k=0;drive(&w, k);k++)
  {
    cal_wiz after;

    while(cw_take(&w, &(uint8_t){0}) != CW_ACT_NONE)
      ;

    log_reset();
    T_EQ(cw_button(&w, CW_EV_CANCEL), 1);
    ev(&w, CW_EV_CANCEL);
    T_EQ(w.state, CW_END);
    T_EQ(take(&w), CW_ACT_DISCARD);
    T_EQ(take(&w), CW_ACT_NONE);
    T_EQ(logged(CW_ACT_COMMIT), 0);

    memcpy(&after, &w, sizeof(w));
    cw_zero_result(&w, rc_ok, match_ok);
    cw_level_result(&w, SPAN_X1);
    cw_gain_result(&w, ACAL_OK);
    T_MEM(&w, &after, sizeof(w));
    check_buttons(&w, 0);
  }

  T_EQ(k, 9);
}

static void test_stray_results(void)
{
  cal_wiz  w, before;
  int      k;
  int      rc[2] = { ACAL_OK, ACAL_OK };
  uint32_t m[2] = { 1, 1 };

  //Results that were not asked for change nothing (each run state accepts only its own)
  for(k=0;drive(&w, k);k++)
  {
    while(cw_take(&w, &(uint8_t){0}) != CW_ACT_NONE)
      ;

    memcpy(&before, &w, sizeof(w));

    if(w.state != CW_ZERO_RUN)
      cw_zero_result(&w, rc, m);
    if(w.state != CW_GAIN_LEVEL_RUN)
      cw_level_result(&w, SPAN_X1);
    if(w.state != CW_GAIN_MEAS_RUN)
      cw_gain_result(&w, ACAL_OK);

    T_MEM(&w, &before, sizeof(w));
  }
}

static void test_texts(void)
{
  acal_channel c;
  cal_wiz      w;
  char         t[96];
  int          rc;

  //Cores matched: tenths of a code, rounded, at least 0.1
  T_EQ(cw_match_tenths(0), 1);
  T_EQ(cw_match_tenths(2), 1);
  T_EQ(cw_match_tenths(3), 2);
  T_EQ(cw_match_tenths(16), 10);
  T_EQ(cw_match_tenths(160), 100);
  cw_fmt_tenths(t, 0);
  T_STR(t, "0.0");
  cw_fmt_tenths(t, 1005);
  T_STR(t, "100.5");

  //Gain: full class |G_A / G_B - 1| in tenths of a percent; never measured: 0
  memset(&c, 0, sizeof(c));
  c.gain_range = ACAL_GAIN_NONE;
  c.gain[ACAL_FULL][0] = 33000;
  c.gain[ACAL_FULL][1] = 32570;
  T_EQ(cw_gain_tenths(&c), 0);
  c.gain_range = 2;
  T_EQ(cw_gain_tenths(&c), 13);
  c.gain[ACAL_FULL][0] = 32768 + 1081;                      //the bench's 1.033 ratio: 3.3 %
  c.gain[ACAL_FULL][1] = 32768;
  T_EQ(cw_gain_tenths(&c), 33);
  c.gain[ACAL_GATED][0] = 40000;                            //the gated class does not count
  T_EQ(cw_gain_tenths(&c), 33);

  //Level check: x1 and x10 probes pass, an open input does not
  T_EQ(cw_level_ok(SPAN_X1), 1);
  T_EQ(cw_level_ok(SPAN_X10), 1);
  T_EQ(cw_level_ok(64), 1);
  T_EQ(cw_level_ok(63), 0);
  T_EQ(cw_level_ok(SPAN_NONE), 0);
  T_EQ(cw_level_ok(-SPAN_X1), 0);

  cw_progress(t, 0, 2);
  T_STR(t, "CH1: measuring range 3 of 6");
  cw_progress(t, 0, CW_PR_TRAIN);
  T_STR(t, "CH1: training capture stages...");
  cw_progress(t, 1, CW_PR_TRAIN);
  T_STR(t, "CH2: training capture stages...");
  cw_progress(t, 1, 5);
  T_STR(t, "CH2: measuring range 6 of 6");

  //The owner's instructions (plan 6a), one per screen, pre-broken
  cw_init(&w, 1);
  T_STR(cw_instruction(&w, 0), "Disconnect everything from the device: both probes, anything on the");
  T_STR(cw_instruction(&w, 1), "calibration post, and the USB cable. Tap Calibrate.");
  T_TRUE(ui_text_width(&font_3, cw_instruction(&w, 0)) <= LINE_W);         //fw 0.2.60: a wire on the post failed it
  T_TRUE(ui_text_width(&font_3, cw_instruction(&w, 1)) <= LINE_W);
  T_STR(cw_instruction(&w, 2), "");
  w.state = CW_GAIN;
  T_STR(cw_instruction(&w, 0), "Plug the CH1 probe back in and connect it to the calibration post.");
  T_STR(cw_instruction(&w, 1), "Set it to 1X on the handle. Tap Calibrate.");
  w.ch = 1;
  T_STR(cw_instruction(&w, 0), "Plug the CH2 probe in and connect it to the calibration post.");
  w.state = CW_DONE;
  T_STR(cw_instruction(&w, 0), "");

  //Done lines: gain measured, kept or never measured
  cw_init(&w, 1);
  w.match16[0] = 0;
  w.match16[1] = 7;
  w.gain_st[0] = CW_G_PASS;
  c.gain[ACAL_FULL][0] = 33000;
  c.gain[ACAL_FULL][1] = 32570;
  cw_done_line(&w, 0, &c, t);
  T_STR(t, "CH1 calibrated: cores matched to 0.1 code, gain mismatch 1.3 %");
  cw_done_line(&w, 1, &c, t);
  T_STR(t, "CH2 calibrated: cores matched to 0.4 code, earlier gains kept");
  c.gain_range = ACAL_GAIN_NONE;
  cw_done_line(&w, 1, &c, t);
  T_STR(t, "CH2 calibrated: cores matched to 0.4 code, gains not measured");

  //Every failure has words (no "OK" or the short report form)
  for(rc=ACAL_E_RESPONSE;rc<=ACAL_E_GAIN_NONE;rc++)
  {
    T_TRUE(strlen(cw_reason_text(rc)) > 8);
    T_TRUE(strcmp(cw_reason_text(rc), "measurement failed"));
  }
  T_STR(cw_reason_text(CW_RC_CANCELLED), "cancelled");

  //Big match values clamp to 16 bits
  {
    uint32_t big[2] = { 70000, 5 };

    cw_init(&w, 1);
    ev(&w, CW_EV_CALIBRATE);
    take(&w);
    cw_zero_result(&w, rc_ok, big);
    T_EQ(w.match16[0], 0xFFFF);
    T_EQ(w.match16[1], 5);
  }
}

int main(void)
{
  test_happy_path();
  test_zero_fail_retry();
  test_zero_fail_detail();
  test_zero_fail_note();
  test_gain_nosignal_and_fail();
  test_gain_unavailable();
  test_cancel_everywhere();
  test_stray_results();
  test_texts();
  return t_report("test_cal_wizard");
}
