//----------------------------------------------------------------------------------------------------------------------------------
// cal_wiz.c - the calibration wizard's state machine and texts (cal_wiz.h). No globals, no hardware, no libc.
//----------------------------------------------------------------------------------------------------------------------------------

#include "cal_wiz.h"
#include "cap_stage.h"

//----------------------------------------------------------------------------------------------------------------------------------

void cw_init(cal_wiz *w, int al3)
{
  uint8_t *p = (uint8_t *)w;
  uint32_t i;

  for(i=0;i<sizeof(*w);i++)
    p[i] = 0;

  w->state = CW_ZERO;
  w->al3   = al3 ? 1 : 0;
}

int cw_screen(const cal_wiz *w)
{
  switch(w->state)
  {
    case CW_ZERO:
    case CW_ZERO_RUN:       return 1;
    case CW_GAIN:
    case CW_GAIN_LEVEL_RUN:
    case CW_GAIN_MEAS_RUN:  return 2 + w->ch;
    case CW_GAIN_NA:        return 2;
    case CW_DONE:           return 4;
    default:                return 0;
  }
}

int cw_running(const cal_wiz *w)
{
  return (w->state == CW_ZERO_RUN) || (w->state == CW_GAIN_LEVEL_RUN) || (w->state == CW_GAIN_MEAS_RUN);
}

int cw_action_button(const cal_wiz *w)
{
  switch(w->state)
  {
    case CW_ZERO:
      return ((w->zero_st[0] == CW_Z_PASS) && (w->zero_st[1] == CW_Z_PASS)) ? CW_EV_CONTINUE : CW_EV_CALIBRATE;

    case CW_GAIN:
      return (w->gain_st[w->ch] == CW_G_PASS) ? CW_EV_CONTINUE : CW_EV_CALIBRATE;

    case CW_GAIN_NA:
      return CW_EV_CONTINUE;

    case CW_DONE:
      return CW_EV_SAVE;

    case CW_END:
      return -1;

    default:
      return CW_EV_MEASURING;
  }
}

int cw_button(const cal_wiz *w, int ev)
{
  if(w->state == CW_END)
    return 0;

  //Cancel stays on every screen, also while a measurement runs
  if(ev == CW_EV_CANCEL)
    return 1;

  return (ev >= 0) && (ev == cw_action_button(w));
}

static void cw_act(cal_wiz *w, uint8_t state, uint8_t action, uint8_t ch)
{
  w->state     = state;
  w->action    = action;
  w->action_ch = ch;
}

static void cw_level(cal_wiz *w)
{
  w->gain_st[w->ch] = CW_G_NONE;
  cw_act(w, CW_GAIN_LEVEL_RUN, CW_ACT_GAIN_LEVEL, w->ch);
}

int cw_event(cal_wiz *w, int ev)
{
  if(!cw_button(w, ev))
    return 0;

  if(ev == CW_EV_CANCEL)
  {
    cw_act(w, CW_END, CW_ACT_DISCARD, 0);
    return 1;
  }

  switch(w->state)
  {
    case CW_ZERO:
      if(ev == CW_EV_CALIBRATE)
      {
        //Both channels again, also after one of them passed (the two belong to one moment)
        w->zero_st[0]  = CW_Z_NONE;
        w->zero_st[1]  = CW_Z_NONE;
        w->zero_det[0] = 0;
        w->zero_det[1] = 0;
        cw_act(w, CW_ZERO_RUN, CW_ACT_ZERO, 0);
      }
      else
      {
        //Continue: the working copy from the new zeros, then the gain screens
        w->ch = 0;
        cw_act(w, w->al3 ? CW_GAIN : CW_GAIN_NA, CW_ACT_PREPARE, 0);
      }
      break;

    case CW_GAIN:
      if(ev == CW_EV_CALIBRATE)
        cw_level(w);
      else if(w->ch == 0)
      {
        w->ch = 1;
        cw_act(w, CW_GAIN, CW_ACT_NONE, 0);
      }
      else
        cw_act(w, CW_DONE, CW_ACT_NONE, 0);
      break;

    case CW_GAIN_NA:
      cw_act(w, CW_DONE, CW_ACT_NONE, 0);
      break;

    case CW_DONE:
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

  cw_act(w, CW_ZERO, CW_ACT_NONE, 0);
}

void cw_zero_detail(cal_wiz *w, uint32_t ch, uint32_t range, const acal_fail *f)
{
  if((w->state != CW_ZERO_RUN) || (ch > 1))
    return;

  w->zero_det[ch]      = 1;
  w->zero_r[ch]        = (uint8_t)range;
  w->zero_cls[ch]      = f->cls;
  w->zero_core[ch]     = f->core;
  w->zero_val[ch]      = f->value;
  w->zero_within16[ch] = f->within16;
  w->zero_f[ch]        = *f;
}

void cw_level_result(cal_wiz *w, int32_t span16)
{
  if(w->state != CW_GAIN_LEVEL_RUN)
    return;

  if(cw_level_ok(span16))
    cw_act(w, CW_GAIN_MEAS_RUN, CW_ACT_GAIN_MEASURE, w->ch);
  else
  {
    w->gain_st[w->ch] = CW_G_NOSIG;
    cw_act(w, CW_GAIN, CW_ACT_NONE, 0);
  }
}

void cw_gain_result(cal_wiz *w, int rc)
{
  if(w->state != CW_GAIN_MEAS_RUN)
    return;

  w->gain_rc[w->ch] = (int16_t)rc;
  w->gain_st[w->ch] = (rc == ACAL_OK) ? CW_G_PASS : CW_G_FAIL;
  cw_act(w, CW_GAIN, CW_ACT_NONE, 0);
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

//1/16 code -> tenths of a code, rounded
static uint32_t cw_tenths16(uint32_t v16)
{
  return (v16 * 10 + 8) / 16;
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

const char *cw_title(const cal_wiz *w)
{
  switch(cw_screen(w))
  {
    case 1:  return "Zero level";
    case 2:  return "CH1 gain";
    case 3:  return "CH2 gain";
    case 4:  return "Done";
    default: return "";
  }
}

//The owner's screens (plan 6a), broken by hand for the 16 px font
const char *cw_instruction(const cal_wiz *w, int line)
{
  static const char *zero[2] = { "Disconnect everything from the device: both probes, anything on the",
                                 "calibration post, and the USB cable. Tap Calibrate." };
  static const char *ch1[2]  = { "Plug the CH1 probe back in and connect it to the calibration post.",
                                 "Set it to 1X on the handle. Tap Calibrate." };
  static const char *ch2[2]  = { "Plug the CH2 probe in and connect it to the calibration post.",
                                 "Set it to 1X on the handle. Tap Calibrate." };
  static const char *na[2]   = { "Gain calibration needs the open FPGA bitstream.",
                                 "Continue keeps the gains and saves the zero level." };

  if((line < 0) || (line > 1))
    return "";

  if(w->state == CW_GAIN_NA)
    return na[line];

  switch(cw_screen(w))
  {
    case 1:  return zero[line];
    case 2:  return ch1[line];
    case 3:  return ch2[line];
    default: return "";
  }
}

//The adc_cal reasons in plain words (acal_reason_text is the short form for the report and the protocol tools)
const char *cw_reason_text(int rc)
{
  switch(rc)
  {
    case ACAL_E_RESPONSE:   return "the offset control does not move the zero level";
    case ACAL_E_CLIP:       return "the input is not at 0 V: unplug the probe";
    case ACAL_E_NOISE:      return "too much noise: unplug the probes, move chargers and USB away";
    case ACAL_E_ZERO:       return "the zero level is out of range";
    case ACAL_E_SPREAD:     return "the two ADC cores differ too much";
    case ACAL_E_SLOPE:      return "the offset control is out of range";
    case ACAL_E_DRIFT:      return "the zero level is still moving: let the scope warm up";
    case ACAL_E_CLASS:      return "the zero level moves with the sample rate";
    case ACAL_E_GAIN_CLIP:  return "the post signal clips: set the probe to 1X";
    case ACAL_E_GAIN_DRIFT: return "the post signal moved: hold the tip still";
    case ACAL_E_GAIN_LEVER: return "no signal: tip on the post?";
    case ACAL_E_GAIN_SPLIT: return "the two ADC cores differ too much";
    case ACAL_E_GAIN_CLASS: return "the gain differs between sample rates";
    case ACAL_E_GAIN_NONE:  return "no generator in this FPGA bitstream";
    case ACAL_E_TIMING:     return "ADC capture timing: the 200 MSa/s data stays garbled";
    case ACAL_E_READOUT:    return "ADC readout fault near code 128: check the FPGA bitstream";
    case CW_RC_CANCELLED:   return "cancelled";
    default:                return "measurement failed";
  }
}

char *cw_progress(char *p, uint32_t ch, uint32_t range)
{
  p = cw_ch(p, ch);

  if(range == CW_PR_TRAIN)
    return cw_str(p, ": training capture stages...");

  p = cw_str(p, ": measuring range ");
  p = cw_uint(p, range + 1);
  p = cw_str(p, " of ");
  return cw_uint(p, ACAL_RANGES);
}

char *cw_zero_line(const cal_wiz *w, uint32_t ch, char *p)
{
  *p = 0;

  switch(w->zero_st[ch])
  {
    case CW_Z_PASS:
      p = cw_ch(p, ch);
      return cw_str(p, ": zero level done");

    case CW_Z_FAIL:
      p = cw_ch(p, ch);

      //Noise and drift (fw 0.2.60): where, and the numbers that tell pickup between conversions from noise inside them
      if(w->zero_det[ch] && ((w->zero_rc[ch] == ACAL_E_NOISE) || (w->zero_rc[ch] == ACAL_E_DRIFT)))
      {
        int32_t v = w->zero_val[ch];

        p = cw_str(p, (w->zero_rc[ch] == ACAL_E_NOISE) ? ": too much noise, range " : ": zero still moving, range ");
        p = cw_uint(p, w->zero_r[ch] + 1u);
        p = cw_str(p, (w->zero_cls[ch] == ACAL_FULL) ? " at 200 MSa/s: core " : " at 2 MSa/s: core ");
        p = cw_str(p, w->zero_core[ch] ? "B " : "A ");
        p = cw_fmt_tenths(p, cw_tenths16((uint32_t)((v < 0) ? -v : v)));

        if(w->zero_rc[ch] == ACAL_E_DRIFT)
          return cw_str(p, " code");

        p = cw_str(p, " codes, ");
        p = cw_fmt_tenths(p, cw_tenths16(w->zero_within16[ch]));
        return cw_str(p, " per capture");
      }

      p = cw_str(p, ": ");
      return cw_str(p, cw_reason_text(w->zero_rc[ch]));

    default:
      return p;
  }
}

static const char *cw_point_name(uint32_t pt)
{
  switch(pt)
  {
    case ACAL_PT_COARSE_HI:
    case ACAL_PT_COARSE_LO: return "coarse point";
    case ACAL_PT_G_ZERO:
    case ACAL_PT_F_ZERO:    return "zero point";
    default:                return "slope point";
  }
}

//"A 130.1 (126..135" plus ", 12 clip" (samples at 0 or 255) when there are any, then ")"
static char *cw_core_note(char *p, const acal_fail *f, uint32_t k)
{
  int32_t m = f->mean16[k];

  p = cw_str(p, k ? "B " : "A ");
  p = cw_fmt_tenths(p, cw_tenths16((uint32_t)((m < 0) ? 0 : m)));
  p = cw_str(p, " (");
  p = cw_uint(p, f->lo[k]);
  p = cw_str(p, "..");
  p = cw_uint(p, f->hi[k]);

  if(f->clipped[k])
  {
    p = cw_str(p, ", ");
    p = cw_uint(p, f->clipped[k]);
    p = cw_str(p, " clip");
  }

  return cw_str(p, ")");
}

void cw_cap_result(cal_wiz *w, uint32_t ch, int st, uint16_t v, uint8_t bad)
{
  if(ch > 1)
    return;

  w->cap_st[ch]  = (uint8_t)st;
  w->cap_v[ch]   = v;
  w->cap_bad[ch] = bad;
}

char *cw_zero_note(const cal_wiz *w, uint32_t ch, char *p)
{
  const acal_fail *f = &w->zero_f[ch];
  uint32_t k, n = 0;

  *p = 0;

  if((ch <= 1) && (w->zero_st[ch] == CW_Z_PASS) && (w->cap_st[ch] == CW_C_DONE))
  {
    p = cw_str(p, "Capture stages: ");
    return cs_text_ch(p, w->cap_v[ch], ch);
  }

  if((ch <= 1) && (w->zero_st[ch] == CW_Z_FAIL) && (w->cap_st[ch] == CW_C_FAIL))
  {
    p = cw_str(p, "Capture stages: no clean stage for ");

    for(k=0;k<CS_CORES;k++)
      if(w->cap_bad[ch] & (1u << k))
      {
        if(n++)
          p = cw_str(p, ", ");
        p = cs_core_name(p, k);
      }

    return p;
  }

  if((ch > 1) || (w->zero_st[ch] != CW_Z_FAIL) || !w->zero_det[ch])
    return p;

  //fw 0.2.100 (owner 2026-10-20 18:13): the stored noise over its limit names the limit of its rate (the one Diagnostics
  //shows, adc_cal.h ACAL_MAX_NOISE16_*) and what helps
  if((w->zero_rc[ch] == ACAL_E_NOISE) && (f->point >= ACAL_POINTS))
  {
    p = cw_str(p, "Noise RMS, inputs open, limit ");
    p = cw_fmt_tenths(p, cw_tenths16(ACAL_MAX_NOISE16(f->cls)));
    return cw_str(p, " code: move chargers and the USB cable away.");
  }

  if(f->point >= ACAL_POINTS)
    return p;

  p = cw_str(p, "Range ");
  p = cw_uint(p, w->zero_r[ch] + 1u);
  p = cw_str(p, (f->cls == ACAL_FULL) ? ", 200 MSa/s, " : ", 2 MSa/s, ");
  p = cw_str(p, cw_point_name(f->point));
  p = cw_str(p, ", PWM ");
  p = cw_uint(p, f->pwm);
  p = cw_str(p, ": ");
  p = cw_core_note(p, f, ACAL_CORE_A);
  p = cw_str(p, ", ");
  return cw_core_note(p, f, ACAL_CORE_B);
}

char *cw_gain_line(const cal_wiz *w, char *p)
{
  uint32_t ch = w->ch;

  *p = 0;

  if(w->state != CW_GAIN)
    return p;

  switch(w->gain_st[ch])
  {
    case CW_G_PASS:
      p = cw_ch(p, ch);
      return cw_str(p, " gain done");

    case CW_G_NOSIG:
      p = cw_str(p, "No signal on ");
      p = cw_ch(p, ch);
      return cw_str(p, ": tip on the post?");

    case CW_G_FAIL:
      p = cw_ch(p, ch);
      p = cw_str(p, ": ");
      return cw_str(p, cw_reason_text(w->gain_rc[ch]));

    default:
      return p;
  }
}

//"CH1 calibrated: cores matched to 0.1 code, gain mismatch 1.3 %". The gain mismatch is the full-rate difference of the
//two cores the correction now removes; without a gain step the earlier gains are kept (or there are none)
char *cw_done_line(const cal_wiz *w, uint32_t ch, const acal_channel *c, char *p)
{
  p = cw_ch(p, ch);
  p = cw_str(p, " calibrated: cores matched to ");
  p = cw_fmt_tenths(p, cw_match_tenths(w->match16[ch]));
  p = cw_str(p, " code, ");

  if(w->gain_st[ch] == CW_G_PASS)
  {
    p = cw_str(p, "gain mismatch ");
    p = cw_fmt_tenths(p, cw_gain_tenths(c));
    return cw_str(p, " %");
  }

  return cw_str(p, (c->gain_range == ACAL_GAIN_NONE) ? "gains not measured" : "earlier gains kept");
}
