//----------------------------------------------------------------------------------------------------------------------------------
// cal_wiz.c - the calibration wizard's state machine and texts (cal_wiz.h). No globals, no hardware, no libc.
//----------------------------------------------------------------------------------------------------------------------------------

#include "cal_wiz.h"

//----------------------------------------------------------------------------------------------------------------------------------

void cw_init(cal_wiz *w, int prior, int al3)
{
  uint8_t *p = (uint8_t *)w;
  uint32_t i;

  for(i=0;i<sizeof(*w);i++)
    p[i] = 0;

  w->state = CW_ZERO_PROMPT;
  w->prior = prior ? 1 : 0;
  w->al3   = al3 ? 1 : 0;
}

int cw_step(const cal_wiz *w)
{
  if(w->state <= CW_ZERO_RESULT)
    return 1;

  if(w->state <= CW_GAIN_RESULT)
    return 2;

  return 3;
}

int cw_button(const cal_wiz *w, int ev)
{
  switch(w->state)
  {
    case CW_ZERO_PROMPT:
      return (ev == CW_EV_START) || (ev == CW_EV_CANCEL) || ((ev == CW_EV_SKIP) && w->prior);

    case CW_ZERO_RESULT:
      return (ev == CW_EV_RETRY) || (ev == CW_EV_CANCEL) || ((ev == CW_EV_SKIP) && w->prior) ||
             ((ev == CW_EV_NEXT) && (w->zero_st[0] == CW_Z_PASS) && (w->zero_st[1] == CW_Z_PASS));

    case CW_GAIN_NA:
    case CW_GAIN_RESULT:
      return (ev == CW_EV_NEXT) || (ev == CW_EV_CANCEL);

    case CW_GAIN_PROMPT:
      return (ev == CW_EV_MEASURE) || (ev == CW_EV_SKIP) || (ev == CW_EV_CANCEL);

    case CW_GAIN_NOSIG:
    case CW_GAIN_FAIL:
      return (ev == CW_EV_RETRY) || (ev == CW_EV_SKIP) || (ev == CW_EV_CANCEL);

    case CW_SUMMARY:
      return (ev == CW_EV_DONE) || (ev == CW_EV_CANCEL);

    default:
      return 0;
  }
}

static void cw_act(cal_wiz *w, uint8_t state, uint8_t action, uint8_t ch)
{
  w->state     = state;
  w->action    = action;
  w->action_ch = ch;
}

//Into step 2 with the zeros of step 1 (zero_new) or the earlier ones
static void cw_enter_gain(cal_wiz *w, uint8_t zero_new)
{
  w->zero_new   = zero_new;
  w->gain_st[0] = CW_G_NONE;
  w->gain_st[1] = CW_G_NONE;
  w->ch         = 0;
  w->auto2      = 0;

  //Zeros exist here: new ones, or Skip, which step 1 offers only with a bound block (prior)
  cw_act(w, w->al3 ? CW_GAIN_PROMPT : CW_GAIN_NA, CW_ACT_PREPARE, 0);
}

static void cw_level(cal_wiz *w, uint8_t ch, uint8_t silent)
{
  w->ch    = ch;
  w->auto2 = silent;
  cw_act(w, CW_GAIN_LEVEL_RUN, CW_ACT_GAIN_LEVEL, ch);
}

int cw_event(cal_wiz *w, int ev)
{
  if(!cw_button(w, ev))
    return 0;

  if(ev == CW_EV_CANCEL)
  {
    w->commit_zero = 0;
    w->commit_gain = 0;
    cw_act(w, CW_END, CW_ACT_DISCARD, 0);
    return 1;
  }

  switch(w->state)
  {
    case CW_ZERO_PROMPT:
    case CW_ZERO_RESULT:
      if((ev == CW_EV_START) || (ev == CW_EV_RETRY))
      {
        w->zero_st[0] = CW_Z_NONE;
        w->zero_st[1] = CW_Z_NONE;
        cw_act(w, CW_ZERO_RUN, CW_ACT_ZERO, 0);
      }
      else
        cw_enter_gain(w, ev == CW_EV_NEXT);   //Skip keeps the earlier zeros (a new result is dropped)
      break;

    case CW_GAIN_NA:
    case CW_GAIN_RESULT:
      cw_act(w, CW_SUMMARY, CW_ACT_NONE, 0);
      break;

    case CW_GAIN_PROMPT:
    case CW_GAIN_NOSIG:
    case CW_GAIN_FAIL:
      if(ev == CW_EV_SKIP)
      {
        w->gain_st[w->ch] = CW_G_SKIP;

        //CH1 skipped: the CH2 prompt (its probe is not on the post yet); CH2 skipped: the summary
        if(w->ch == 0)
        {
          w->ch = 1;
          cw_act(w, CW_GAIN_PROMPT, CW_ACT_NONE, 0);
        }
        else
          cw_act(w, CW_SUMMARY, CW_ACT_NONE, 0);
      }
      else
      {
        //Measure or Retry: the level check first, the measurement follows it
        w->gain_st[w->ch] = CW_G_NONE;
        cw_level(w, w->ch, 0);
      }
      break;

    case CW_SUMMARY:
      w->commit_zero = w->zero_new;
      w->commit_gain = !w->zero_new && ((w->gain_st[0] == CW_G_PASS) || (w->gain_st[1] == CW_G_PASS));
      cw_act(w, CW_END, CW_ACT_COMMIT, 0);
      break;
  }

  return 1;
}

int cw_take(cal_wiz *w, uint8_t *ch)
{
  int a = w->action;

  *ch       = w->action_ch;
  w->action = CW_ACT_NONE;
  return a;
}

//----------------------------------------------------------------------------------------------------------------------------------

void cw_zero_result(cal_wiz *w, const int rc[2], const uint32_t match16[2])
{
  uint32_t ch;

  if(w->state != CW_ZERO_RUN)
    return;

  for(ch=0;ch<2;ch++)
  {
    w->zero_rc[ch] = (int16_t)rc[ch];
    w->zero_st[ch] = (rc[ch] == ACAL_OK) ? CW_Z_PASS : CW_Z_FAIL;
    w->match16[ch] = (uint16_t)((match16[ch] > 0xFFFF) ? 0xFFFF : match16[ch]);
  }

  cw_act(w, CW_ZERO_RESULT, CW_ACT_NONE, 0);
}

void cw_level_result(cal_wiz *w, int32_t span16)
{
  if(w->state != CW_GAIN_LEVEL_RUN)
    return;

  if(cw_level_ok(span16))
    cw_act(w, CW_GAIN_MEAS_RUN, CW_ACT_GAIN_MEASURE, w->ch);
  else if(w->auto2)
  {
    //CH2 does not see the post yet: ask for its probe
    w->auto2 = 0;
    cw_act(w, CW_GAIN_PROMPT, CW_ACT_NONE, 0);
  }
  else
    cw_act(w, CW_GAIN_NOSIG, CW_ACT_NONE, 0);
}

void cw_gain_result(cal_wiz *w, int rc)
{
  if(w->state != CW_GAIN_MEAS_RUN)
    return;

  w->auto2 = 0;
  w->gain_rc[w->ch] = (int16_t)rc;

  if(rc != ACAL_OK)
  {
    w->gain_st[w->ch] = CW_G_FAIL;
    cw_act(w, CW_GAIN_FAIL, CW_ACT_NONE, 0);
    return;
  }

  w->gain_st[w->ch] = CW_G_PASS;

  //CH1 passed: a second probe may already be on the post (one node), so look at CH2 silently
  if(w->ch == 0)
    cw_level(w, 1, 1);
  else
    cw_act(w, CW_GAIN_RESULT, CW_ACT_NONE, 0);
}

//----------------------------------------------------------------------------------------------------------------------------------

int cw_level_ok(int32_t span16)
{
  if(span16 <= 0)
    return 0;

  //acal_gain_mid_at scales a probe-range quantity by the nominal sensitivities: the span the measurement range will see
  return acal_gain_mid_at(span16, acal_gain_range(span16)) >= ACAL_GAIN_MIN_LEVER16;
}

uint32_t cw_match_tenths(uint32_t match16)
{
  uint32_t t = (match16 * 10 + 8) / 16;

  return t ? t : 1;
}

uint32_t cw_gain_tenths(const acal_channel *c)
{
  uint32_t a = c->gain[ACAL_FULL][ACAL_CORE_A], b = c->gain[ACAL_FULL][ACAL_CORE_B];

  if((c->gain_range == ACAL_GAIN_NONE) || (b == 0))
    return 0;

  return (((a > b) ? a - b : b - a) * 1000 + b / 2) / b;
}

//----------------------------------------------------------------------------------------------------------------------------------

static char *cw_str(char *p, const char *s)
{
  while(*s)
    *p++ = *s++;

  *p = 0;
  return p;
}

static char *cw_uint(char *p, uint32_t v)
{
  char     t[10];
  uint32_t n = 0;

  do
  {
    t[n++] = (char)('0' + v % 10);
    v /= 10;
  } while(v);

  while(n)
    *p++ = t[--n];

  *p = 0;
  return p;
}

char *cw_fmt_tenths(char *p, uint32_t v)
{
  p = cw_uint(p, v / 10);
  *p++ = '.';
  return cw_uint(p, v % 10);
}

static char *cw_ch(char *p, uint32_t ch)
{
  return cw_str(p, ch ? "CH2" : "CH1");
}

char *cw_progress(char *p, uint32_t ch, uint32_t range)
{
  p = cw_ch(p, ch);
  p = cw_str(p, " range ");
  p = cw_uint(p, range + 1);
  p = cw_str(p, " of ");
  return cw_uint(p, ACAL_RANGES);
}

static char *cw_matched(char *p, const cal_wiz *w, uint32_t ch)
{
  p = cw_str(p, "cores matched to ");
  p = cw_fmt_tenths(p, cw_match_tenths(w->match16[ch]));
  return cw_str(p, " code");
}

char *cw_zero_line(const cal_wiz *w, uint32_t ch, char *p)
{
  p = cw_ch(p, ch);
  p = cw_str(p, ": ");

  switch(w->zero_st[ch])
  {
    case CW_Z_PASS: return cw_matched(p, w, ch);
    case CW_Z_FAIL: return cw_str(p, acal_reason_text(w->zero_rc[ch]));
    default:        return cw_str(p, "not measured");
  }
}

static char *cw_gain_kept(char *p, const acal_channel *c)
{
  return cw_str(p, (c->gain_range == ACAL_GAIN_NONE) ? "gain not measured" : "gain kept");
}

char *cw_gain_line(const cal_wiz *w, uint32_t ch, const acal_channel *c, char *p)
{
  *p = 0;

  if(w->gain_st[ch] == CW_G_NONE)
    return p;

  p = cw_ch(p, ch);
  p = cw_str(p, ": ");

  switch(w->gain_st[ch])
  {
    case CW_G_PASS:
      p = cw_str(p, "core gains ");
      p = cw_fmt_tenths(p, cw_gain_tenths(c));
      return cw_str(p, " % apart, now matched");

    case CW_G_FAIL:
      return cw_str(p, acal_reason_text(w->gain_rc[ch]));

    default:
      p = cw_str(p, "skipped, ");
      return cw_gain_kept(p, c);
  }
}

char *cw_summary_line(const cal_wiz *w, uint32_t ch, const acal_channel *c, char *p)
{
  p = cw_ch(p, ch);
  p = cw_str(p, ": ");
  p = w->zero_new ? cw_matched(p, w, ch) : cw_str(p, "zero kept");
  p = cw_str(p, ", ");

  if(w->gain_st[ch] == CW_G_PASS)
  {
    p = cw_str(p, "gain ");
    p = cw_fmt_tenths(p, cw_gain_tenths(c));
    return cw_str(p, " %");
  }

  return cw_gain_kept(p, c);
}
