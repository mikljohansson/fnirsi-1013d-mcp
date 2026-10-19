//----------------------------------------------------------------------------------------------------------------------------------
// test_cal_wizard.c - the calibration wizard's state machine (cal_wiz.c, fw 0.2.57, plan section 6a): the four screens,
// Calibrate turning into Continue, retry after a failure, the stock-bitstream path, Cancel on every screen and during a
// measurement (never a commit), stray events, and the texts.
//----------------------------------------------------------------------------------------------------------------------------------

#include "test_util.h"
#include "cal_wiz.h"

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
  T_STR(t, "CH2: too much noise: unplug the probe and USB");

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
  cw_progress(t, 1, 5);
  T_STR(t, "CH2: measuring range 6 of 6");

  //The owner's instructions (plan 6a), one per screen, pre-broken
  cw_init(&w, 1);
  T_STR(cw_instruction(&w, 0), "Fully disconnect both probes and the USB cable from the device.");
  T_STR(cw_instruction(&w, 1), "Tap Calibrate.");
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
  test_gain_nosignal_and_fail();
  test_gain_unavailable();
  test_cancel_everywhere();
  test_stray_results();
  test_texts();
  return t_report("test_cal_wizard");
}
