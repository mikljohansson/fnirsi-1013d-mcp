//----------------------------------------------------------------------------------------------------------------------------------
// test_cal_wizard.c - the calibration wizard's state machine (cal_wiz.c): every path, stray events, the action sequence
// (Cancel never commits), what Done commits, and the numbers in the texts.
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
}

#define B(e) (1u << (e))

static void start_zero_pass(cal_wiz *w)
{
  ev(w, CW_EV_START);
  T_EQ(w->state, CW_ZERO_RUN);
  T_EQ(take(w), CW_ACT_ZERO);
  cw_zero_result(w, rc_ok, match_ok);
  T_EQ(w->state, CW_ZERO_RESULT);
  T_EQ(take(w), CW_ACT_NONE);
}

//Measure on the channel in focus with the level span and the measurement result rc
static void measure(cal_wiz *w, int32_t span, int rc)
{
  uint8_t ch = w->ch;

  ev(w, CW_EV_MEASURE);
  T_EQ(w->state, CW_GAIN_LEVEL_RUN);
  T_EQ(w->auto2, 0);
  T_EQ(take(w), CW_ACT_GAIN_LEVEL);
  T_EQ(log_ch[log_n - 1], ch);
  cw_level_result(w, span);

  if(!cw_level_ok(span))
    return;

  T_EQ(w->state, CW_GAIN_MEAS_RUN);
  T_EQ(take(w), CW_ACT_GAIN_MEASURE);
  T_EQ(log_ch[log_n - 1], ch);
  cw_gain_result(w, rc);
}

//----------------------------------------------------------------------------------------------------------------------------------

static void test_happy_path(void)
{
  cal_wiz w;

  log_reset();
  cw_init(&w, 1, 1);
  T_EQ(w.state, CW_ZERO_PROMPT);
  T_EQ(cw_step(&w), 1);
  T_EQ(take(&w), CW_ACT_NONE);
  check_buttons(&w, B(CW_EV_START) | B(CW_EV_SKIP) | B(CW_EV_CANCEL));

  start_zero_pass(&w);
  T_EQ(w.zero_st[0], CW_Z_PASS);
  T_EQ(w.zero_st[1], CW_Z_PASS);
  T_EQ(w.match16[1], 20);
  check_buttons(&w, B(CW_EV_NEXT) | B(CW_EV_RETRY) | B(CW_EV_SKIP) | B(CW_EV_CANCEL));

  ev(&w, CW_EV_NEXT);
  T_EQ(w.state, CW_GAIN_PROMPT);
  T_EQ(cw_step(&w), 2);
  T_EQ(w.zero_new, 1);
  T_EQ(w.ch, 0);
  T_EQ(take(&w), CW_ACT_PREPARE);
  check_buttons(&w, B(CW_EV_MEASURE) | B(CW_EV_SKIP) | B(CW_EV_CANCEL));

  //CH1 passes; CH2 is not on the post yet: the silent check leads to the CH2 prompt without an error
  measure(&w, SPAN_X1, ACAL_OK);
  T_EQ(w.gain_st[0], CW_G_PASS);
  T_EQ(w.state, CW_GAIN_LEVEL_RUN);
  T_EQ(w.auto2, 1);
  T_EQ(w.ch, 1);
  check_buttons(&w, 0);
  T_EQ(take(&w), CW_ACT_GAIN_LEVEL);
  T_EQ(log_ch[log_n - 1], 1);
  cw_level_result(&w, SPAN_NONE);
  T_EQ(w.state, CW_GAIN_PROMPT);
  T_EQ(w.auto2, 0);
  T_EQ(w.ch, 1);
  T_EQ(w.gain_st[1], CW_G_NONE);
  T_EQ(take(&w), CW_ACT_NONE);

  measure(&w, SPAN_X1, ACAL_OK);
  T_EQ(w.gain_st[1], CW_G_PASS);
  T_EQ(w.state, CW_GAIN_RESULT);
  check_buttons(&w, B(CW_EV_NEXT) | B(CW_EV_CANCEL));

  ev(&w, CW_EV_NEXT);
  T_EQ(w.state, CW_SUMMARY);
  T_EQ(cw_step(&w), 3);
  check_buttons(&w, B(CW_EV_DONE) | B(CW_EV_CANCEL));

  ev(&w, CW_EV_DONE);
  T_EQ(w.state, CW_END);
  T_EQ(take(&w), CW_ACT_COMMIT);
  T_EQ(w.commit_zero, 1);
  T_EQ(w.commit_gain, 0);
  check_buttons(&w, 0);

  //The whole run: zero, prepare, CH1 level + measure, CH2 silent level, CH2 level + measure, commit
  T_EQ(log_n, 8);
  T_EQ(log_act[0], CW_ACT_ZERO);
  T_EQ(log_act[1], CW_ACT_PREPARE);
  T_EQ(log_act[4], CW_ACT_GAIN_LEVEL);
  T_EQ(log_act[5], CW_ACT_GAIN_LEVEL);
  T_EQ(log_act[7], CW_ACT_COMMIT);
  T_EQ(logged(CW_ACT_DISCARD), 0);
}

static void test_zero_fail_retry(void)
{
  static const int rc_fail[2] = { ACAL_OK, ACAL_E_NOISE };
  cal_wiz w;
  char    t[64];

  log_reset();
  cw_init(&w, 0, 1);
  check_buttons(&w, B(CW_EV_START) | B(CW_EV_CANCEL));

  ev(&w, CW_EV_START);
  T_EQ(take(&w), CW_ACT_ZERO);
  check_buttons(&w, 0);
  cw_zero_result(&w, rc_fail, match_ok);
  T_EQ(w.state, CW_ZERO_RESULT);
  T_EQ(w.zero_st[0], CW_Z_PASS);
  T_EQ(w.zero_st[1], CW_Z_FAIL);
  cw_zero_line(&w, 1, t);
  T_STR(t, "CH2: noisy: unplug probe");
  cw_zero_line(&w, 0, t);
  T_STR(t, "CH1: cores matched to 0.2 code");

  //No Next (nothing is applied unless both pass), no Skip (nothing to keep)
  check_buttons(&w, B(CW_EV_RETRY) | B(CW_EV_CANCEL));

  ev(&w, CW_EV_RETRY);
  T_EQ(w.state, CW_ZERO_RUN);
  T_EQ(w.zero_st[0], CW_Z_NONE);
  cw_zero_line(&w, 0, t);
  T_STR(t, "CH1: not measured");
  T_EQ(take(&w), CW_ACT_ZERO);
  cw_zero_result(&w, rc_ok, match_ok);
  check_buttons(&w, B(CW_EV_NEXT) | B(CW_EV_RETRY) | B(CW_EV_CANCEL));
  ev(&w, CW_EV_NEXT);
  T_EQ(w.zero_new, 1);
  T_EQ(w.state, CW_GAIN_PROMPT);
  T_EQ(logged(CW_ACT_ZERO), 2);
}

static void test_zero_skip(void)
{
  cal_wiz w;

  //With a prior calibration: Skip at the prompt keeps the old zeros
  log_reset();
  cw_init(&w, 1, 1);
  ev(&w, CW_EV_SKIP);
  T_EQ(w.state, CW_GAIN_PROMPT);
  T_EQ(w.zero_new, 0);
  T_EQ(take(&w), CW_ACT_PREPARE);
  T_EQ(logged(CW_ACT_ZERO), 0);

  //Skip after a passed zero step drops its result
  log_reset();
  cw_init(&w, 1, 1);
  start_zero_pass(&w);
  ev(&w, CW_EV_SKIP);
  T_EQ(w.zero_new, 0);
  T_EQ(w.state, CW_GAIN_PROMPT);

  //Skip after a failed zero step (prior exists)
  {
    static const int rc_fail[2] = { ACAL_E_CLIP, ACAL_E_CLIP };

    cw_init(&w, 1, 1);
    ev(&w, CW_EV_START);
    take(&w);
    cw_zero_result(&w, rc_fail, match_ok);
    check_buttons(&w, B(CW_EV_RETRY) | B(CW_EV_SKIP) | B(CW_EV_CANCEL));
    ev(&w, CW_EV_SKIP);
    T_EQ(w.zero_new, 0);
    T_EQ(w.state, CW_GAIN_PROMPT);
  }

  //Without one: no Skip anywhere in step 1
  cw_init(&w, 0, 1);
  T_EQ(cw_event(&w, CW_EV_SKIP), 0);
  T_EQ(w.state, CW_ZERO_PROMPT);
  start_zero_pass(&w);
  T_EQ(cw_event(&w, CW_EV_SKIP), 0);
  T_EQ(w.state, CW_ZERO_RESULT);
}

static void test_gain_nosignal_retry(void)
{
  cal_wiz w;
  char    t[64];

  log_reset();
  cw_init(&w, 0, 1);
  start_zero_pass(&w);
  ev(&w, CW_EV_NEXT);
  take(&w);

  measure(&w, SPAN_NONE, ACAL_OK);
  T_EQ(w.state, CW_GAIN_NOSIG);
  T_EQ(w.ch, 0);
  T_EQ(take(&w), CW_ACT_NONE);
  T_EQ(logged(CW_ACT_GAIN_MEASURE), 0);
  check_buttons(&w, B(CW_EV_RETRY) | B(CW_EV_SKIP) | B(CW_EV_CANCEL));

  //Retry: the level check again on the same channel, then the measurement
  ev(&w, CW_EV_RETRY);
  T_EQ(w.state, CW_GAIN_LEVEL_RUN);
  T_EQ(take(&w), CW_ACT_GAIN_LEVEL);
  T_EQ(log_ch[log_n - 1], 0);
  cw_level_result(&w, SPAN_X10);
  T_EQ(take(&w), CW_ACT_GAIN_MEASURE);
  cw_gain_result(&w, ACAL_OK);
  T_EQ(w.gain_st[0], CW_G_PASS);
  T_EQ(w.state, CW_GAIN_LEVEL_RUN);
  T_EQ(w.auto2, 1);

  //Retry never redoes step 1
  T_EQ(logged(CW_ACT_ZERO), 1);

  //A failed measurement: the reason, Retry / Skip / Cancel; Retry starts at the level check
  take(&w);
  cw_level_result(&w, SPAN_X1);
  T_EQ(take(&w), CW_ACT_GAIN_MEASURE);
  cw_gain_result(&w, ACAL_E_GAIN_DRIFT);
  T_EQ(w.state, CW_GAIN_FAIL);
  T_EQ(w.ch, 1);
  T_EQ(w.gain_st[1], CW_G_FAIL);
  check_buttons(&w, B(CW_EV_RETRY) | B(CW_EV_SKIP) | B(CW_EV_CANCEL));
  {
    acal_channel c;

    memset(&c, 0, sizeof(c));
    cw_gain_line(&w, 1, &c, t);
    T_STR(t, "CH2: gain: level drifts");
  }
  ev(&w, CW_EV_RETRY);
  T_EQ(w.gain_st[1], CW_G_NONE);
  T_EQ(take(&w), CW_ACT_GAIN_LEVEL);
  cw_level_result(&w, SPAN_X1);
  T_EQ(take(&w), CW_ACT_GAIN_MEASURE);
  cw_gain_result(&w, ACAL_OK);
  T_EQ(w.state, CW_GAIN_RESULT);
  ev(&w, CW_EV_NEXT);
  ev(&w, CW_EV_DONE);
  T_EQ(take(&w), CW_ACT_COMMIT);
  T_EQ(w.commit_zero, 1);
  T_EQ(logged(CW_ACT_ZERO), 1);
}

static void test_two_probes_auto(void)
{
  cal_wiz w;

  log_reset();
  cw_init(&w, 1, 1);
  ev(&w, CW_EV_SKIP);
  take(&w);

  measure(&w, SPAN_X1, ACAL_OK);
  T_EQ(w.auto2, 1);
  T_EQ(take(&w), CW_ACT_GAIN_LEVEL);
  T_EQ(log_ch[log_n - 1], 1);

  //CH2 already on the post: measured at once, no prompt
  cw_level_result(&w, SPAN_X1);
  T_EQ(w.state, CW_GAIN_MEAS_RUN);
  T_EQ(w.ch, 1);
  T_EQ(take(&w), CW_ACT_GAIN_MEASURE);
  T_EQ(log_ch[log_n - 1], 1);
  cw_gain_result(&w, ACAL_OK);
  T_EQ(w.state, CW_GAIN_RESULT);
  T_EQ(w.auto2, 0);
  T_EQ(w.gain_st[1], CW_G_PASS);

  //Zero kept, gains new: only the gains are committed
  ev(&w, CW_EV_NEXT);
  ev(&w, CW_EV_DONE);
  T_EQ(take(&w), CW_ACT_COMMIT);
  T_EQ(w.commit_zero, 0);
  T_EQ(w.commit_gain, 1);

  //The silent check failing on the measurement itself shows the reason (not a prompt)
  cw_init(&w, 1, 1);
  ev(&w, CW_EV_SKIP);
  take(&w);
  measure(&w, SPAN_X1, ACAL_OK);
  take(&w);
  cw_level_result(&w, SPAN_X1);
  take(&w);
  cw_gain_result(&w, ACAL_E_GAIN_SPLIT);
  T_EQ(w.state, CW_GAIN_FAIL);
  T_EQ(w.ch, 1);
}

static void test_gain_skip(void)
{
  cal_wiz      w;
  acal_channel c;
  char         t[80];

  memset(&c, 0, sizeof(c));

  log_reset();
  cw_init(&w, 1, 1);
  ev(&w, CW_EV_SKIP);
  take(&w);

  //Skip CH1: the CH2 prompt; skip CH2: the summary
  ev(&w, CW_EV_SKIP);
  T_EQ(w.gain_st[0], CW_G_SKIP);
  T_EQ(w.state, CW_GAIN_PROMPT);
  T_EQ(w.ch, 1);
  T_EQ(take(&w), CW_ACT_NONE);
  ev(&w, CW_EV_SKIP);
  T_EQ(w.gain_st[1], CW_G_SKIP);
  T_EQ(w.state, CW_SUMMARY);

  c.gain_range = 2;
  c.gain_flags = ACAL_GAIN_KEPT;
  cw_gain_line(&w, 0, &c, t);
  T_STR(t, "CH1: skipped, gain kept");
  cw_summary_line(&w, 0, &c, t);
  T_STR(t, "CH1: zero kept, gain kept");
  c.gain_range = ACAL_GAIN_NONE;
  cw_gain_line(&w, 1, &c, t);
  T_STR(t, "CH2: skipped, gain not measured");
  cw_summary_line(&w, 1, &c, t);
  T_STR(t, "CH2: zero kept, gain not measured");

  //Nothing changed: Done commits nothing
  ev(&w, CW_EV_DONE);
  T_EQ(take(&w), CW_ACT_COMMIT);
  T_EQ(w.commit_zero, 0);
  T_EQ(w.commit_gain, 0);

  //New zeros, CH1 gain measured, CH2 skipped after a failure: the zeros (and with them the gains) are committed
  log_reset();
  cw_init(&w, 0, 1);
  start_zero_pass(&w);
  ev(&w, CW_EV_NEXT);
  take(&w);
  measure(&w, SPAN_X1, ACAL_OK);
  take(&w);
  cw_level_result(&w, SPAN_NONE);
  measure(&w, SPAN_X1, ACAL_E_GAIN_CLIP);
  T_EQ(w.state, CW_GAIN_FAIL);
  ev(&w, CW_EV_SKIP);
  T_EQ(w.gain_st[1], CW_G_SKIP);
  T_EQ(w.state, CW_SUMMARY);
  c.gain_range = 2;
  c.gain[ACAL_FULL][0] = 33000;
  c.gain[ACAL_FULL][1] = 32570;
  cw_summary_line(&w, 0, &c, t);
  T_STR(t, "CH1: cores matched to 0.2 code, gain 1.3 %");
  cw_summary_line(&w, 1, &c, t);
  T_STR(t, "CH2: cores matched to 1.3 code, gain kept");
  ev(&w, CW_EV_DONE);
  T_EQ(take(&w), CW_ACT_COMMIT);
  T_EQ(w.commit_zero, 1);
  T_EQ(w.commit_gain, 0);

  //Zero kept, CH1 skipped, CH2 measured: gains only
  cw_init(&w, 1, 1);
  ev(&w, CW_EV_SKIP);
  take(&w);
  ev(&w, CW_EV_SKIP);
  measure(&w, SPAN_X1, ACAL_OK);
  T_EQ(w.state, CW_GAIN_RESULT);
  ev(&w, CW_EV_NEXT);
  ev(&w, CW_EV_DONE);
  T_EQ(take(&w), CW_ACT_COMMIT);
  T_EQ(w.commit_zero, 0);
  T_EQ(w.commit_gain, 1);
}

static void test_gain_unavailable(void)
{
  cal_wiz w;

  log_reset();
  cw_init(&w, 1, 0);
  start_zero_pass(&w);
  ev(&w, CW_EV_NEXT);
  T_EQ(w.state, CW_GAIN_NA);
  T_EQ(cw_step(&w), 2);
  T_EQ(take(&w), CW_ACT_PREPARE);
  check_buttons(&w, B(CW_EV_NEXT) | B(CW_EV_CANCEL));
  ev(&w, CW_EV_NEXT);
  T_EQ(w.state, CW_SUMMARY);
  ev(&w, CW_EV_DONE);
  T_EQ(take(&w), CW_ACT_COMMIT);
  T_EQ(w.commit_zero, 1);
  T_EQ(logged(CW_ACT_GAIN_LEVEL) + logged(CW_ACT_GAIN_MEASURE), 0);

  //Stock bitstream and zero skipped: nothing to commit
  cw_init(&w, 1, 0);
  ev(&w, CW_EV_SKIP);
  T_EQ(w.state, CW_GAIN_NA);
  take(&w);
  ev(&w, CW_EV_NEXT);
  ev(&w, CW_EV_DONE);
  T_EQ(take(&w), CW_ACT_COMMIT);
  T_EQ(w.commit_zero + w.commit_gain, 0);
}

//Drives a fresh wizard to state target (with a prior calibration and the AL3); 0 when the path is not known
static int drive_to(cal_wiz *w, int target)
{
  static const int rc_fail[2] = { ACAL_E_ZERO, ACAL_OK };

  cw_init(w, 1, 1);

  switch(target)
  {
    case CW_ZERO_PROMPT:
      break;
    case CW_ZERO_RESULT:
      ev(w, CW_EV_START);
      take(w);
      cw_zero_result(w, rc_fail, match_ok);
      break;
    case CW_GAIN_NA:
      cw_init(w, 1, 0);
      ev(w, CW_EV_SKIP);
      take(w);
      break;
    case CW_GAIN_PROMPT:
      ev(w, CW_EV_SKIP);
      take(w);
      break;
    case CW_GAIN_NOSIG:
      ev(w, CW_EV_SKIP);
      take(w);
      measure(w, 0, ACAL_OK);
      break;
    case CW_GAIN_FAIL:
      ev(w, CW_EV_SKIP);
      take(w);
      measure(w, SPAN_X1, ACAL_E_GAIN_LEVER);
      break;
    case CW_GAIN_RESULT:
      ev(w, CW_EV_SKIP);
      take(w);
      ev(w, CW_EV_SKIP);
      measure(w, SPAN_X1, ACAL_OK);
      break;
    case CW_SUMMARY:
      ev(w, CW_EV_SKIP);
      take(w);
      ev(w, CW_EV_SKIP);
      ev(w, CW_EV_SKIP);
      break;
    default:
      return 0;
  }

  T_EQ(w->state, target);
  return 1;
}

static void test_cancel_everywhere(void)
{
  static const int states[] = { CW_ZERO_PROMPT, CW_ZERO_RESULT, CW_GAIN_NA, CW_GAIN_PROMPT, CW_GAIN_NOSIG, CW_GAIN_FAIL,
                                CW_GAIN_RESULT, CW_SUMMARY };
  cal_wiz  w;
  uint32_t i;

  for(i=0;i<sizeof(states)/sizeof(states[0]);i++)
  {
    log_reset();
    T_TRUE(drive_to(&w, states[i]));
    while(take(&w) != CW_ACT_NONE)
      ;

    ev(&w, CW_EV_CANCEL);
    T_EQ(w.state, CW_END);
    T_EQ(take(&w), CW_ACT_DISCARD);
    T_EQ(w.commit_zero + w.commit_gain, 0);
    T_EQ(logged(CW_ACT_COMMIT), 0);
    T_EQ(logged(CW_ACT_DISCARD), 1);

    //Nothing more after the end
    check_buttons(&w, 0);
    T_EQ(take(&w), CW_ACT_NONE);
  }
}

static void test_stray(void)
{
  static const int states[] = { CW_ZERO_PROMPT, CW_ZERO_RESULT, CW_GAIN_NA, CW_GAIN_PROMPT, CW_GAIN_NOSIG, CW_GAIN_FAIL,
                                CW_GAIN_RESULT, CW_SUMMARY };
  static const uint32_t masks[] = {
    B(CW_EV_START) | B(CW_EV_SKIP) | B(CW_EV_CANCEL),
    B(CW_EV_RETRY) | B(CW_EV_SKIP) | B(CW_EV_CANCEL),         //CH1 failed: no Next
    B(CW_EV_NEXT) | B(CW_EV_CANCEL),
    B(CW_EV_MEASURE) | B(CW_EV_SKIP) | B(CW_EV_CANCEL),
    B(CW_EV_RETRY) | B(CW_EV_SKIP) | B(CW_EV_CANCEL),
    B(CW_EV_RETRY) | B(CW_EV_SKIP) | B(CW_EV_CANCEL),
    B(CW_EV_NEXT) | B(CW_EV_CANCEL),
    B(CW_EV_DONE) | B(CW_EV_CANCEL) };
  cal_wiz  w, before;
  uint32_t i;
  uint32_t m[2] = { 1, 1 };
  int      rc[2] = { ACAL_OK, ACAL_OK };

  for(i=0;i<sizeof(states)/sizeof(states[0]);i++)
  {
    T_TRUE(drive_to(&w, states[i]));
    while(take(&w) != CW_ACT_NONE)
      ;

    check_buttons(&w, masks[i]);

    //Results of actions that were not asked for change nothing
    memcpy(&before, &w, sizeof(w));
    cw_zero_result(&w, rc, m);
    cw_level_result(&w, SPAN_X1);
    cw_gain_result(&w, ACAL_OK);
    T_MEM(&w, &before, sizeof(w));
  }

  //Running states take no buttons and only their own result
  cw_init(&w, 1, 1);
  ev(&w, CW_EV_START);
  check_buttons(&w, 0);
  memcpy(&before, &w, sizeof(w));
  cw_level_result(&w, SPAN_X1);
  cw_gain_result(&w, ACAL_OK);
  T_MEM(&w, &before, sizeof(w));

  drive_to(&w, CW_GAIN_PROMPT);
  ev(&w, CW_EV_MEASURE);
  check_buttons(&w, 0);
  memcpy(&before, &w, sizeof(w));
  cw_zero_result(&w, rc, m);
  cw_gain_result(&w, ACAL_OK);
  T_MEM(&w, &before, sizeof(w));
  cw_level_result(&w, SPAN_X1);
  T_EQ(w.state, CW_GAIN_MEAS_RUN);
  check_buttons(&w, 0);
  memcpy(&before, &w, sizeof(w));
  cw_zero_result(&w, rc, m);
  cw_level_result(&w, SPAN_X1);
  T_MEM(&w, &before, sizeof(w));
}

static void test_numbers(void)
{
  acal_channel c;
  cal_wiz      w;
  char         t[80];

  //Cores matched: tenths of a code, rounded, at least 0.1
  T_EQ(cw_match_tenths(0), 1);
  T_EQ(cw_match_tenths(1), 1);
  T_EQ(cw_match_tenths(2), 1);
  T_EQ(cw_match_tenths(3), 2);
  T_EQ(cw_match_tenths(8), 5);
  T_EQ(cw_match_tenths(16), 10);
  T_EQ(cw_match_tenths(24), 15);
  T_EQ(cw_match_tenths(160), 100);

  cw_fmt_tenths(t, 0);
  T_STR(t, "0.0");
  cw_fmt_tenths(t, 13);
  T_STR(t, "1.3");
  cw_fmt_tenths(t, 1005);
  T_STR(t, "100.5");

  //Gain: full class |G_A / G_B - 1| in tenths of a percent; never measured: 0
  memset(&c, 0, sizeof(c));
  c.gain_range = ACAL_GAIN_NONE;
  c.gain[ACAL_FULL][0] = 33000;
  c.gain[ACAL_FULL][1] = 32570;
  T_EQ(cw_gain_tenths(&c), 0);
  c.gain_range = 2;
  T_EQ(cw_gain_tenths(&c), 13);                             //1.32 %
  c.gain[ACAL_FULL][0] = 32570;
  c.gain[ACAL_FULL][1] = 33000;
  T_EQ(cw_gain_tenths(&c), 13);                             //1.30 %
  c.gain[ACAL_FULL][0] = 32768;
  c.gain[ACAL_FULL][1] = 32768;
  T_EQ(cw_gain_tenths(&c), 0);
  c.gain[ACAL_FULL][0] = 32768 + 1081;                      //the bench's 1.033 ratio: 3.3 %
  T_EQ(cw_gain_tenths(&c), 33);
  c.gain[ACAL_FULL][0] = 32768 + 16;                        //0.0488 % rounds to 0.0
  T_EQ(cw_gain_tenths(&c), 0);
  c.gain[ACAL_FULL][0] = 32768 + 17;                        //0.0519 % rounds to 0.1
  T_EQ(cw_gain_tenths(&c), 1);
  c.gain[ACAL_GATED][0] = 40000;                            //the gated class does not count
  T_EQ(cw_gain_tenths(&c), 1);
  c.gain[ACAL_FULL][1] = 0;
  T_EQ(cw_gain_tenths(&c), 0);

  //Level check: x1 and x10 probes pass, an open input does not
  T_EQ(cw_level_ok(SPAN_X1), 1);
  T_EQ(cw_level_ok(SPAN_X10), 1);
  T_EQ(cw_level_ok(64), 1);                                 //4 codes at range 2: 40 codes at range 5
  T_EQ(cw_level_ok(63), 0);
  T_EQ(cw_level_ok(SPAN_NONE), 0);
  T_EQ(cw_level_ok(0), 0);
  T_EQ(cw_level_ok(-SPAN_X1), 0);                           //levels swapped
  T_EQ(cw_level_ok(ACAL_GAIN_MAX_SPAN16 * 3), 1);           //clips at range 2 but range 0 or 1 takes it

  cw_progress(t, 0, 2);
  T_STR(t, "CH1 range 3 of 6");
  cw_progress(t, 1, 5);
  T_STR(t, "CH2 range 6 of 6");

  //The texts of a full run
  cw_init(&w, 0, 1);
  w.zero_new   = 1;
  w.match16[0] = 0;
  w.match16[1] = 7;
  w.gain_st[0] = CW_G_PASS;
  w.gain_st[1] = CW_G_PASS;
  c.gain[ACAL_FULL][0] = 33000;
  c.gain[ACAL_FULL][1] = 32570;
  cw_summary_line(&w, 0, &c, t);
  T_STR(t, "CH1: cores matched to 0.1 code, gain 1.3 %");
  cw_summary_line(&w, 1, &c, t);
  T_STR(t, "CH2: cores matched to 0.4 code, gain 1.3 %");
  cw_gain_line(&w, 0, &c, t);
  T_STR(t, "CH1: core gains 1.3 % apart, now matched");
  w.gain_st[1] = CW_G_NONE;
  cw_gain_line(&w, 1, &c, t);
  T_STR(t, "");
  cw_summary_line(&w, 1, &c, t);
  T_STR(t, "CH2: cores matched to 0.4 code, gain kept");
  w.zero_new = 0;
  w.gain_st[0] = CW_G_PASS;
  cw_summary_line(&w, 0, &c, t);
  T_STR(t, "CH1: zero kept, gain 1.3 %");

  //Big match values clamp to 16 bits
  {
    uint32_t big[2] = { 70000, 5 };

    cw_init(&w, 0, 1);
    ev(&w, CW_EV_START);
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
  test_zero_skip();
  test_gain_nosignal_retry();
  test_two_probes_auto();
  test_gain_skip();
  test_gain_unavailable();
  test_cancel_everywhere();
  test_stray();
  test_numbers();
  return t_report("test_cal_wizard");
}
