//----------------------------------------------------------------------------------------------------------------------------------
// cal_wiz_ui.c - the calibration wizard on the scope (firmware/README.md section 6.4): the full-screen view, its touch
// loop, and the measurements the state machine (cal_wiz.c) asks for. System settings > Baseline calibration opens it
// (statemachine.c); it owns the screen until Cancel or Done and restores it afterwards.
//
// The measurement helpers (zero step and gain step of calibration data v3, adc_cal.h, README 6.3) moved here from
// scope_functions.c: only the wizard uses them, and keeping them static next to their one caller avoids exporting a
// dozen cal_* functions. They work on copies: nothing reaches adc_cal / sectors 708 and 709 before Done.
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "menu.h"
#include "scope_functions.h"
#include "touchpanel.h"
#include "timer.h"
#include "fpga_control.h"
#include "display_lib.h"
#include "ff.h"
#include "DS3231.h"
#include "usb_interface.h"
#include "pc_hooks.h"
#include "variables.h"
#include "adc_cal.h"
#include "cal_wiz.h"

#include <string.h>

//----------------------------------------------------------------------------------------------------------------------------------
//Measurements

#define CAL_PWM_HIGHCODE    500     //coarse points: low PWM gives a high code
#define CAL_PWM_LOWCODE    1200
#define CAL_FINE_STEP       150     //slope points: centre +- 150 counts (+- 40 codes)
#define CAL_RATE_GATED        6     //2 MSa/s
#define CAL_RATE_FULL         0     //200 MSa/s

//FPGA commands per channel: relay, offset PWM, core A, core B, coupling
static const uint8 cal_cmds[2][5] = { { 0x33, 0x32, 0x20, 0x21, 0x34 }, { 0x36, 0x35, 0x22, 0x23, 0x37 } };

static uint8 cal_backup_invert[2];
static uint8 cal_backup_trigger;

static void wiz_progress(uint32 ch, uint32 r);

static void cal_set_rate(uint32 rate)
{
  fpga_set_sample_rate(rate);
  fpga_set_time_base(sample_rate_time_per_div[rate]);
}

//convs conversions at PWM pwm on range r: per core accumulators, first and second half of the conversions apart
static void cal_measure(uint32 r, int32 pwm, uint32 settle_ms, uint32 convs, acal_acc acc[2][2])
{
  uint32 i, h;

  calibrationsettings.dc_calibration_offset[r] = (uint16)pwm;
  fpga_set_channel_offset(&calibrationsettings);
  timer0_delay(settle_ms);

  memset(acc, 0, 4 * sizeof(acal_acc));

  for(i=0;i<convs;i++)
  {
    fpga_do_conversion();
    while(fpga_done_conversion() == 0) PC_BUSY_POLL();

    //No compensation (calibrationsettings has none), no invert, no dcoffset: raw codes, core A odd, core B even
    fpga_read_sample_data(&calibrationsettings, 100);

    h = (i >= convs / 2);
    acal_acc_add(&acc[h][ACAL_CORE_A], calibrationsettings.tracebuffer + 1, scopesettings.nofsamples, 2);
    acal_acc_add(&acc[h][ACAL_CORE_B], calibrationsettings.tracebuffer, scopesettings.nofsamples, 2);
  }
}

//Both halves of a measurement merged: mean, noise and clipped samples per core, drift between the halves
static void cal_result(acal_acc acc[2][2], int32_t *mean16, int32_t *drift16, uint32_t *noise16, uint32_t *clipped)
{
  uint32 k;
  acal_acc all;

  for(k=0;k<2;k++)
  {
    all = acc[0][k];
    acal_acc_merge(&all, &acc[1][k]);
    mean16[k] = acal_mean16(&all);

    if(drift16)
      drift16[k] = acal_mean16(&acc[1][k]) - acal_mean16(&acc[0][k]);

    if(noise16)
      noise16[k] = acal_rms16(&all);

    *clipped += all.clipped;
  }
}

//Zero step of one channel (inputs open or shorted): per hardware range the PWM centre where the mean of both cores reads
//128, the offset slope of each core from two PWM points 150 counts either side and its zero at that centre, each with
//the ADC clock gated (2 MSa/s, every legacy time base but 200 MSa/s) and at full rate (200 MSa/s, every filtered
//capture). *match16: the worst |drift A - drift B| of the zero points over ranges and classes (summary)
static int cal_channel(acal_channel *c, uint32 ch, uint32_t *match16)
{
  acal_acc        acc[2][2];
  acal_range_meas m;
  int32_t         mean[2], hi, lo, p, d;
  uint32_t        r, cl, unused = 0;
  int             rc;

  *match16 = 0;

  for(r=0;r<ACAL_RANGES;r++)
  {
    wiz_progress(ch, r);

    calibrationsettings.samplevoltperdiv = r;
    fpga_set_channel_voltperdiv(&calibrationsettings);
    timer0_delay(60);

    memset(&m, 0, sizeof(m));
    cal_set_rate(CAL_RATE_GATED);

    //Coarse centre from two far points (clipping there only biases the estimate)
    cal_measure(r, CAL_PWM_HIGHCODE, 30, 2, acc);
    cal_result(acc, mean, 0, 0, &unused);
    hi = (mean[0] + mean[1]) / 2;
    cal_measure(r, CAL_PWM_LOWCODE, 30, 2, acc);
    cal_result(acc, mean, 0, 0, &unused);
    lo = (mean[0] + mean[1]) / 2;

    p = acal_centre_pwm(CAL_PWM_HIGHCODE, hi, CAL_PWM_LOWCODE, lo);

    if(p < 0)
      return ACAL_E_RESPONSE;

    //Slope of each core, gated
    m.pwm_lo = p - CAL_FINE_STEP;
    m.pwm_hi = p + CAL_FINE_STEP;
    cal_measure(r, m.pwm_lo, 60, 4, acc);
    cal_result(acc, m.mean16_lo[ACAL_GATED], 0, 0, &m.clipped);
    cal_measure(r, m.pwm_hi, 60, 4, acc);
    cal_result(acc, m.mean16_hi[ACAL_GATED], 0, 0, &m.clipped);

    //Centre refined on the slope points, then the zeros there in both clock classes
    m.pwm = acal_interp_pwm(m.pwm_lo, (m.mean16_lo[ACAL_GATED][0] + m.mean16_lo[ACAL_GATED][1]) / 2, m.pwm_hi,
                            (m.mean16_hi[ACAL_GATED][0] + m.mean16_hi[ACAL_GATED][1]) / 2);

    cal_measure(r, m.pwm, 60, 8, acc);
    cal_result(acc, m.zero16[ACAL_GATED], m.drift16[ACAL_GATED], m.noise16[ACAL_GATED], &m.clipped);

    //Full rate: a 1000-sample readout spans 5 us of the 40 us offset-PWM ripple period, so each conversion sees one
    //ripple phase; 64 conversions (about 70 ms) average it out (bench 0.2.43: 8 left 0.2-0.4 code). The full-rate
    //slope points too: the cores' gains differ per class, and so may their offset slopes
    cal_set_rate(CAL_RATE_FULL);
    cal_measure(r, m.pwm, 30, 64, acc);
    cal_result(acc, m.zero16[ACAL_FULL], m.drift16[ACAL_FULL], m.noise16[ACAL_FULL], &m.clipped);
    cal_measure(r, m.pwm_hi, 60, 64, acc);
    cal_result(acc, m.mean16_hi[ACAL_FULL], 0, 0, &m.clipped);
    cal_measure(r, m.pwm_lo, 60, 64, acc);
    cal_result(acc, m.mean16_lo[ACAL_FULL], 0, 0, &m.clipped);

    for(cl=0;cl<ACAL_CLASSES;cl++)
    {
      d = m.drift16[cl][ACAL_CORE_A] - m.drift16[cl][ACAL_CORE_B];
      d = (d < 0) ? -d : d;

      if((uint32_t)d > *match16)
        *match16 = (uint32_t)d;
    }

    rc = acal_range_store(&m, c, (uint8)r);

    if(rc != ACAL_OK)
      return rc;
  }

  return ACAL_OK;
}

//The scratch channel for channel ch: its FPGA commands and trace buffer, DC coupling
static void cal_scratch_channel(uint32 ch)
{
  calibrationsettings.voltperdivcommand = cal_cmds[ch][0];
  calibrationsettings.offsetcommand     = cal_cmds[ch][1];
  calibrationsettings.adc1command       = cal_cmds[ch][2];
  calibrationsettings.adc2command       = cal_cmds[ch][3];
  calibrationsettings.couplingcommand   = cal_cmds[ch][4];
  calibrationsettings.tracebuffer       = ch ? (uint8 *)channel2tracebuffer : (uint8 *)channel1tracebuffer;
  fpga_set_channel_coupling(&calibrationsettings);
}

//Gain step (calibration data v3, adc_cal.h): the probe tip on the calibration post, the generator holds its output low
//(about -1.24 V against probe ground) or high (about +1.87 V). Both cores and both clock classes see the same node, so
//the spans between the two levels give every relative gain; no level needs to be known. The gains are measured on one
//range and used on all (assumption: the mismatch is inside the ADC; firmware/README.md section 6.3 has the bench check).
//
//Generator as a static level: frequency 0 freezes the phase accumulator, threshold 0 keeps the output low, all ones
//keeps it high (the accumulator is never all ones in practice; the lever check would catch it)
static void cal_gen_level(uint32 high)
{
  fpga_write_cmd(0x52);
  fpga_write_int(high ? 0xFFFFFFFF : 0);
  fpga_write_cmd(0x51);
  fpga_write_int(0);
  fpga_write_cmd(0x50);
  fpga_write_int(1);
}

//Per core means (1/16 code) of convs conversions at one post level, rate and PWM
static void cal_gain_level(uint32 r, int32 pwm, uint32 rate, uint32 settle_ms, uint32 convs, uint32 high, int32_t *mean16,
                           uint32_t *clipped)
{
  acal_acc acc[2][2];

  cal_set_rate(rate);
  cal_gen_level(high);
  cal_measure(r, pwm, settle_ms, convs, acc);
  cal_result(acc, mean16, 0, 0, clipped);
}

//Level check: both levels at range 2 (gated, 2 conversions each). Returns the span (1/16 code, mean of both cores; it
//picks the measurement range, cw_level_ok decides whether a probe is on the post); *mid16 = the levels' midpoint from
//the 0 V code
static int32_t cal_gain_check(const acal_channel *c, int32_t *mid16)
{
  int32_t  lo[2], hi[2];
  uint32_t unused = 0;
  uint8    r = ACAL_GAIN_PROBE_RANGE;

  calibrationsettings.samplevoltperdiv = r;
  fpga_set_channel_voltperdiv(&calibrationsettings);
  timer0_delay(60);
  cal_gain_level(r, c->pwm[r], CAL_RATE_GATED, 60, 2, 0, lo, &unused);
  cal_gain_level(r, c->pwm[r], CAL_RATE_GATED, 30, 2, 1, hi, &unused);

  *mid16 = (hi[0] + hi[1] + lo[0] + lo[1]) / 4 - (c->zero[ACAL_GATED][0][r] + c->zero[ACAL_GATED][1][r]) / 2;
  return (hi[0] + hi[1] - lo[0] - lo[1]) / 2;
}

//The measurement after a passed level check: the range from the span, the PWM that puts the levels symmetric about the
//0 V code (even-order INL cancels in the span); low, high, low again (linear drift cancels in the spans), each gated (8)
//and at full rate (64: PWM ripple). c changes only when the checks pass
static int cal_gain_measure(acal_channel *c, int32_t span16, int32_t mid16)
{
  acal_gain_meas m;
  int32_t        pwm;
  uint32_t       l;
  uint8          r = acal_gain_range(span16);

  pwm = acal_gain_pwm(c, r, acal_gain_mid_at(mid16, r));

  calibrationsettings.samplevoltperdiv = r;
  fpga_set_channel_voltperdiv(&calibrationsettings);
  timer0_delay(60);

  memset(&m, 0, sizeof(m));

  for(l=0;l<3;l++)
  {
    cal_gain_level(r, pwm, CAL_RATE_GATED, l ? 30 : 100, 8, l == 1, m.mean16[ACAL_GATED][l], &m.clipped);
    cal_gain_level(r, pwm, CAL_RATE_FULL, 30, 64, l == 1, m.mean16[ACAL_FULL][l], &m.clipped);
  }

  return acal_gain_store(&m, r, c);
}

//Measurement set-up: trigger off, AUTO mode (no lockup in NORMAL or SINGLE), level 0, 1000 samples per readout (500 per
//core), the raw scratch channel (no compensation, no invert, no dcoffset, DC coupling)
static void cal_begin(void)
{
  PCHANNELSETTINGS chs[2] = { &scopesettings.channel1, &scopesettings.channel2 };
  uint32           ch;

  cal_backup_trigger = scopesettings.triggermode;
  scopesettings.samplemode  = 0;
  scopesettings.triggermode = 0;
  fpga_set_trigger_mode();
  fpga_write_cmd(0x17);
  fpga_write_byte(0);

  scopesettings.samplecount = 1000;
  scopesettings.nofsamples  = 500;

  for(ch=0;ch<2;ch++)
  {
    cal_backup_invert[ch] = chs[ch]->invert;
    chs[ch]->invert       = 0;
  }

  calibrationsettings.adc1compensation = 0;
  calibrationsettings.adc2compensation = 0;
  calibrationsettings.invert           = 0;
  calibrationsettings.dcoffset         = 0;
  calibrationsettings.dc_shift_value   = 0;
  calibrationsettings.coupling         = 0;
}

//The UI's settings back into the FPGA (channels, sample rate, trigger mode; the generator after a gain measurement)
static void cal_end(uint32 gen)
{
  PCHANNELSETTINGS chs[2] = { &scopesettings.channel1, &scopesettings.channel2 };
  uint32           ch;

  if(gen)
    fpga_on_off_generator();

  scopesettings.samplecount = fpgasettings.totalsamples;
  scopesettings.nofsamples  = fpgasettings.totalsamples / 2;

  for(ch=0;ch<2;ch++)
  {
    chs[ch]->invert = cal_backup_invert[ch];
    fpga_set_channel_voltperdiv(chs[ch]);
    fpga_set_channel_offset(chs[ch]);
    fpga_set_channel_coupling(chs[ch]);
  }

  fpga_set_sample_rate(scopesettings.samplerate);

  scopesettings.triggermode = cal_backup_trigger;
  fpga_set_trigger_mode();
}

//----------------------------------------------------------------------------------------------------------------------------------
//The wizard

static cal_wiz      wiz;
static acal_channel wiz_new[2];         //zero step result (both channels)
static acal_channel wiz_work[2];        //what Done commits (CW_ACT_PREPARE, then the gain step)
static int32_t      wiz_span[2];        //level check: span and midpoint for the measurement after it
static int32_t      wiz_mid[2];
static uint8        wiz_usb;            //a USB host was seen at the last check
static uint8        wiz_session;        //FPGA in the measurement set-up (cal_begin)
static uint8        wiz_gen;            //the generator was used in this session

//Layout: header, body lines (font_3), progress line, two result lines, a note, four button slots at the bottom
#define WIZ_TEXT_X        40
#define WIZ_LINE1_Y       84
#define WIZ_LINE2_Y      108
#define WIZ_WARN_Y       146
#define WIZ_PROG_Y       214
#define WIZ_RES_Y        256
#define WIZ_RES_DY        36
#define WIZ_NOTE_Y       336
#define WIZ_BTN_Y        400
#define WIZ_BTN_W        150
#define WIZ_BTN_H         52

static const uint16 wiz_slot_x[4] = { 30, 230, 430, 620 };

//Slot of an event's button: Cancel left, Skip, Retry, the step's primary action right
static uint32 wiz_slot(int ev)
{
  switch(ev)
  {
    case CW_EV_CANCEL: return 0;
    case CW_EV_SKIP:   return 1;
    case CW_EV_RETRY:  return 2;
    default:           return 3;
  }
}

static const char *wiz_label(int ev)
{
  switch(ev)
  {
    case CW_EV_START:   return "Start";
    case CW_EV_MEASURE: return "Measure";
    case CW_EV_RETRY:   return "Retry";
    case CW_EV_SKIP:    return "Skip";
    case CW_EV_NEXT:    return "Next";
    case CW_EV_DONE:    return "Done";
    default:            return "Cancel";
  }
}

static uint32 wiz_text_width(PFONTDATA font, const char *s)
{
  PFONTINFORMATION info;
  uint32           w = 0, c;

  for(;*s;s++)
  {
    c = (uint8)*s;

    for(info=(PFONTINFORMATION)font->fontinformation;info;info=info->next_info)
    {
      if((c >= info->first_char) && (c <= info->last_char))
      {
        w += info->fontmetrics[c - info->first_char].width;
        break;
      }
    }
  }

  return w;
}

static void wiz_text(uint32 x, uint32 y, uint32 colour, const char *s)
{
  display_set_fg_color(colour);
  display_text(x, y, (int8 *)s);
}

//Pressed: light fill, black label; the primary action green, the others grey
static void wiz_button(int ev, uint32 pressed)
{
  uint32      x = wiz_slot_x[wiz_slot(ev)];
  const char *t = wiz_label(ev);

  display_set_fg_color(pressed ? 0x00CCCCCC : ((wiz_slot(ev) == 3) ? 0x00007A00 : GREY_COLOR));
  display_fill_rounded_rect(x, WIZ_BTN_Y, WIZ_BTN_W, WIZ_BTN_H, 3);
  display_set_fg_color(LIGHTGREY1_COLOR);
  display_draw_rounded_rect(x, WIZ_BTN_Y, WIZ_BTN_W, WIZ_BTN_H, 3);

  display_set_font(&font_3);
  wiz_text(x + (WIZ_BTN_W - wiz_text_width(&font_3, t)) / 2, WIZ_BTN_Y + 18, pressed ? BLACK_COLOR : WHITE_COLOR, t);
}

//The shown button under (x, y), -1 for none. Touch areas: the button rectangles, 8 pixels taller above and below
static int wiz_hit(uint32 x, uint32 y)
{
  uint32 x0;
  int    ev;

  if((y < WIZ_BTN_Y - 8) || (y >= WIZ_BTN_Y + WIZ_BTN_H + 8))
    return -1;

  for(ev=0;ev<CW_EVENTS;ev++)
  {
    x0 = wiz_slot_x[wiz_slot(ev)];

    if(cw_button(&wiz, ev) && (x >= x0) && (x < x0 + WIZ_BTN_W))
      return ev;
  }

  return -1;
}

//Check mark, 16 x 16, green
static void wiz_tick(uint32 x, uint32 y)
{
  uint32 i;

  display_set_fg_color(GREEN_COLOR);

  for(i=0;i<3;i++)
  {
    display_draw_line(x, y + 7 + i, x + 5, y + 12 + i);
    display_draw_line(x + 5, y + 12 + i, x + 15, y + 1 + i);
  }
}

//Result line: tick and text (passed), a red reason (failed) or grey text (skipped)
static void wiz_result(uint32 y, uint32 tick, uint32 colour, const char *t)
{
  if(tick)
    wiz_tick(WIZ_TEXT_X, y);

  wiz_text(WIZ_TEXT_X + 26, y, colour, t);
}

//USB host attached and not suspended: the MUSB frame number moves (1 ms frames)
static uint8 wiz_usb_host(void)
{
  uint32 f = *USBC_REG_FRNUM & 0x7FF;

  timer0_delay(3);
  return (*USBC_REG_FRNUM & 0x7FF) != f;
}

static void wiz_draw_usb(void)
{
  display_set_fg_color(BLACK_COLOR);
  display_fill_rect(20, WIZ_WARN_Y - 2, 760, 48);

  if(wiz_usb && (wiz.state <= CW_ZERO_RESULT))
  {
    display_set_font(&font_3);
    wiz_text(WIZ_TEXT_X, WIZ_WARN_Y, ORANGE_COLOR, "USB is connected: the PC ground can shift the zero by about a code.");

    if(wiz.state != CW_ZERO_RUN)
      wiz_text(WIZ_TEXT_X, WIZ_WARN_Y + 22, ORANGE_COLOR, (wiz.state == CW_ZERO_PROMPT) ? "Unplug it, or Start anyway." :
                                                          "Unplug it and Retry, or go on anyway.");
  }
}

//Progress line, redrawn on its own while a measurement runs
static void wiz_draw_progress(const char *t)
{
  display_set_fg_color(BLACK_COLOR);
  display_fill_rect(20, WIZ_PROG_Y - 2, 760, 22);
  display_set_font(&font_3);
  wiz_text(WIZ_TEXT_X, WIZ_PROG_Y, LIGHTGREY2_COLOR, t);
}

static void wiz_progress(uint32 ch, uint32 r)
{
  char t[40];

  cw_progress(t + 10, ch, r);
  memcpy(t, "Measuring ", 10);
  wiz_draw_progress(t);
}

static char *wiz_chtext(char *p, const char *a, uint32 ch, const char *b)
{
  while(*a) *p++ = *a++;
  *p++ = 'C';
  *p++ = 'H';
  *p++ = (char)('1' + ch);
  while(*b) *p++ = *b++;
  *p = 0;
  return p;
}

//"Date 2026-10-19" when a DS3231 answers and the RTC is on, else nothing
static uint32 wiz_date(char *p)
{
  if(!(pc_glue_ui_has(PC_FEAT_RTC) && onoffRTC))
    return 0;

  get_fattime();
  memcpy(p, "Date 20yy-mm-dd", 16);
  p[7]  = (char)('0' + (year / 10) % 10);
  p[8]  = (char)('0' + year % 10);
  p[10] = (char)('0' + (month / 10) % 10);
  p[11] = (char)('0' + month % 10);
  p[13] = (char)('0' + (day / 10) % 10);
  p[14] = (char)('0' + day % 10);
  return 1;
}

static void wiz_draw_step1(void)
{
  char   t[80];
  uint32 ch;

  wiz_text(WIZ_TEXT_X, WIZ_LINE1_Y, WHITE_COLOR, "Unplug both probes and the USB cable.");
  wiz_text(WIZ_TEXT_X, WIZ_LINE2_Y, LIGHTGREY2_COLOR,
           "Both channels, all six ranges, about 8 s. Nothing is saved before Done.");
  wiz_draw_usb();
  display_set_font(&font_3);

  if(wiz.state == CW_ZERO_RESULT)
  {
    for(ch=0;ch<2;ch++)
    {
      cw_zero_line(&wiz, ch, t);
      wiz_result(WIZ_RES_Y + ch * WIZ_RES_DY, wiz.zero_st[ch] == CW_Z_PASS,
                 (wiz.zero_st[ch] == CW_Z_PASS) ? WHITE_COLOR : RED_COLOR, t);
    }

    if((wiz.zero_st[0] != CW_Z_PASS) || (wiz.zero_st[1] != CW_Z_PASS))
      wiz_text(WIZ_TEXT_X, WIZ_NOTE_Y, LIGHTGREY2_COLOR, wiz.prior ?
               "Nothing is applied unless both channels pass: Retry, or Skip to keep the earlier zeros." :
               "Nothing is applied unless both channels pass: check the inputs and Retry.");
  }
}

static void wiz_draw_step2(void)
{
  char   t[80];
  uint32 ch, y = WIZ_RES_Y;

  switch(wiz.state)
  {
    case CW_GAIN_NA:
      wiz_text(WIZ_TEXT_X, WIZ_LINE1_Y, WHITE_COLOR, "The gain step needs the AL3 FPGA: gains kept.");
      return;

    case CW_GAIN_NOSIG:
      wiz_chtext(t, "No signal on ", wiz.ch, ": tip on the post?");
      wiz_text(WIZ_TEXT_X, WIZ_LINE1_Y, ORANGE_COLOR, t);
      wiz_text(WIZ_TEXT_X, WIZ_LINE2_Y, LIGHTGREY2_COLOR,
               "Check that the probe is at x1 and touches the post, then Retry.");
      break;

    case CW_GAIN_FAIL:
      cw_gain_line(&wiz, wiz.ch, &wiz_work[wiz.ch], t);
      wiz_text(WIZ_TEXT_X, WIZ_LINE1_Y, RED_COLOR, t);
      wiz_text(WIZ_TEXT_X, WIZ_LINE2_Y, LIGHTGREY2_COLOR, "Retry, or Skip to keep this channel's earlier gains.");
      break;

    case CW_GAIN_RESULT:
      wiz_text(WIZ_TEXT_X, WIZ_LINE1_Y, WHITE_COLOR, "Gain step done.");
      break;

    default:
      //Prompt and the measurements: the instruction for the channel in focus (the silent CH2 check has none)
      if(!wiz.auto2)
      {
        wiz_chtext(t, "Plug in the ", wiz.ch, " probe at x1 and hold its tip on the calibration post.");
        wiz_text(WIZ_TEXT_X, WIZ_LINE1_Y, WHITE_COLOR, t);
        wiz_text(WIZ_TEXT_X, WIZ_LINE2_Y, LIGHTGREY2_COLOR,
                 "The ground clip is not needed. Skip keeps this channel's earlier gains.");
      }
      break;
  }

  //Measured and skipped channels (a failure is the line above)
  for(ch=0;ch<2;ch++)
  {
    if((wiz.gain_st[ch] == CW_G_PASS) || (wiz.gain_st[ch] == CW_G_SKIP))
    {
      cw_gain_line(&wiz, ch, &wiz_work[ch], t);
      wiz_result(y, wiz.gain_st[ch] == CW_G_PASS, (wiz.gain_st[ch] == CW_G_PASS) ? WHITE_COLOR : LIGHTGREY2_COLOR, t);
      y += WIZ_RES_DY;
    }
  }

  if(wiz.state == CW_GAIN_LEVEL_RUN)
  {
    wiz_chtext(t, wiz.auto2 ? "Checking whether " : "Checking the signal on ", wiz.ch, wiz.auto2 ? " sees the post" : "");
    wiz_draw_progress(t);
  }
  else if(wiz.state == CW_GAIN_MEAS_RUN)
  {
    wiz_chtext(t, "Measuring the ", wiz.ch, " gain, about 1 s");
    wiz_draw_progress(t);
  }
}

static void wiz_draw_step3(void)
{
  char   t[80];
  uint32 ch;

  if(wiz.zero_new || (wiz.gain_st[0] == CW_G_PASS) || (wiz.gain_st[1] == CW_G_PASS))
    wiz_text(WIZ_TEXT_X, WIZ_LINE1_Y, WHITE_COLOR, "Done saves this calibration, Cancel keeps the earlier one.");
  else
    wiz_text(WIZ_TEXT_X, WIZ_LINE1_Y, WHITE_COLOR, "Nothing changed: the calibration stays as it was.");

  for(ch=0;ch<2;ch++)
  {
    cw_summary_line(&wiz, ch, &wiz_work[ch], t);
    wiz_text(WIZ_TEXT_X, WIZ_LINE2_Y + 20 + ch * WIZ_RES_DY, ch ? CHANNEL2_COLOR : CHANNEL1_COLOR, t);
  }

  if(wiz_date(t))
    wiz_text(WIZ_TEXT_X, WIZ_LINE2_Y + 20 + 2 * WIZ_RES_DY, LIGHTGREY2_COLOR, t);
}

//The whole view, drawn off screen and copied (no black frame in between)
static void wiz_draw(void)
{
  static const char *titles[3] = { "Zero offsets", "Core gains", "Summary" };
  char t[16];
  int  step = cw_step(&wiz), ev;

  display_set_screen_buffer(displaybuffertmp);
  display_set_fg_color(BLACK_COLOR);
  display_fill_rect(0, 0, 800, 480);
  display_set_fg_color(LIGHTGREY1_COLOR);
  display_draw_rounded_rect(10, 10, 780, 460, 3);
  display_draw_horz_line(62, 20, 780);

  memcpy(t, "Step n of 3", 12);
  t[5] = (char)('0' + step);
  display_set_font(&font_5);
  wiz_text(30, 26, LIGHTGREY2_COLOR, t);
  wiz_text(200, 26, WHITE_COLOR, titles[step - 1]);

  display_set_font(&font_3);

  if(step == 1)
    wiz_draw_step1();
  else if(step == 2)
    wiz_draw_step2();
  else
    wiz_draw_step3();

  for(ev=0;ev<CW_EVENTS;ev++)
    if(cw_button(&wiz, ev))
      wiz_button(ev, 0);

  if(wiz.state == CW_ZERO_RUN)
    wiz_draw_progress("Measuring");

  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_source_buffer(displaybuffertmp);
  display_copy_rect_to_screen(0, 0, 800, 480);
}

//CW_ACT_PREPARE: the working copy for steps 2-3. New zeros take the previous block's measured gains (acal_gain_carry,
//flag KEPT) until the gain step measures new ones; skipped zeros keep the whole block
static void wiz_prepare(void)
{
  uint32 ch;

  for(ch=0;ch<2;ch++)
  {
    if(wiz.zero_new)
    {
      wiz_work[ch] = wiz_new[ch];
      acal_gain_carry(adc_cal_loaded ? &adc_cal[ch] : 0, &wiz_work[ch]);
    }
    else
      wiz_work[ch] = adc_cal[ch];
  }
}

//CW_ACT_COMMIT. New zeros: the PWM centres and the 1.00o5 compensations (for older firmware too), the block, sectors 708
//(block) and 709 (the centres it is bound to); only gains: the block and 708; nothing changed: nothing written
static uint32 wiz_commit(void)
{
  PCHANNELSETTINGS chs[2] = { &scopesettings.channel1, &scopesettings.channel2 };
  uint32           ch, r;

  if(!wiz.commit_zero && !wiz.commit_gain)
    return 0;

  for(ch=0;ch<2;ch++)
  {
    if(wiz.commit_zero)
    {
      for(r=0;r<ACAL_RANGES;r++)
        chs[ch]->dc_calibration_offset[r] = wiz_work[ch].pwm[r];

      chs[ch]->dc_calibration_offset[6] = chs[ch]->dc_calibration_offset[5];
      acal_atlan_comps(&wiz_work[ch], &chs[ch]->adc1compensation, &chs[ch]->adc2compensation);

      //The new centre into the FPGA
      fpga_set_channel_offset(chs[ch]);
    }

    adc_cal[ch] = wiz_work[ch];
  }

  adc_cal_loaded = 1;

  scope_save_input_calibration_data();

  if(wiz.commit_zero)
    scope_save_configuration_data();

  cal_wizard_committed();
  return 1;
}

//Runs the pending actions (a measurement result can ask for the next one), then the FPGA back to the UI's settings.
//Returns 1 when a commit wrote something
static uint32 wiz_run_actions(void)
{
  uint32_t match[2];
  int      rc[2];
  uint32   ch, written = 0;
  uint8    ach;
  int      act;

  while((act = cw_take(&wiz, &ach)) != CW_ACT_NONE)
  {
    if((act == CW_ACT_ZERO) || (act == CW_ACT_GAIN_LEVEL) || (act == CW_ACT_GAIN_MEASURE))
    {
      wiz_draw();

      if(!wiz_session)
      {
        cal_begin();
        wiz_session = 1;
      }
    }

    switch(act)
    {
      case CW_ACT_ZERO:
        //Both channels, even when CH1 fails (both reasons shown); nothing is used unless both pass
        for(ch=0;ch<2;ch++)
        {
          cal_scratch_channel(ch);
          memset(&wiz_new[ch], 0, sizeof(acal_channel));
          rc[ch] = cal_channel(&wiz_new[ch], ch, &match[ch]);
        }

        cw_zero_result(&wiz, rc, match);
        break;

      case CW_ACT_PREPARE:
        wiz_prepare();
        break;

      case CW_ACT_GAIN_LEVEL:
        wiz_gen = 1;
        cal_scratch_channel(ach);
        wiz_span[ach] = cal_gain_check(&wiz_work[ach], &wiz_mid[ach]);
        cw_level_result(&wiz, wiz_span[ach]);
        break;

      case CW_ACT_GAIN_MEASURE:
      {
        acal_channel t = wiz_work[ach];

        wiz_gen = 1;
        cal_scratch_channel(ach);
        rc[0] = cal_gain_measure(&t, wiz_span[ach], wiz_mid[ach]);

        if(rc[0] == ACAL_OK)
          wiz_work[ach] = t;

        cw_gain_result(&wiz, rc[0]);
        break;
      }

      case CW_ACT_COMMIT:
        written = wiz_commit();
        break;

      default:
        //CW_ACT_DISCARD: the copies are dropped, nothing was written
        break;
    }
  }

  if(wiz_session)
  {
    cal_end(wiz_gen);
    wiz_session = 0;
    wiz_gen     = 0;
  }

  return written;
}

uint32 cal_wizard_run(void)
{
  PFONTDATA   font = &font_5;
  const char *msg;
  uint32      written = 0, tick;
  int         ev, last = -1;

  //The screen as it is (menus open), restored at the end
  display_set_destination_buffer(displaybuffer4);
  display_copy_rect_from_screen(0, 0, 800, 480);

  cw_init(&wiz, adc_cal_loaded && acal_bound(&adc_cal[0], scopesettings.channel1.dc_calibration_offset) &&
                acal_bound(&adc_cal[1], scopesettings.channel2.dc_calibration_offset), fpgasettings.fw_FPGA == 2);
  wiz_usb = wiz_usb_host();
  tick    = timer0_get_ticks();
  wiz_draw();

  while(wiz.state != CW_END)
  {
    //Step 1: the USB warning follows the cable (checked twice a second, redrawn only when it changes)
    if((wiz.state <= CW_ZERO_RESULT) && (timer0_get_ticks() - tick >= 500))
    {
      uint8 usb = wiz_usb_host();

      tick = timer0_get_ticks();

      if(usb != wiz_usb)
      {
        wiz_usb = usb;
        wiz_draw_usb();
      }
    }

    tp_i2c_read_status();

    if(!havetouch)
      continue;

    //Only the shown buttons react; anything else waits for the release and does nothing
    ev = wiz_hit(xtouch, ytouch);

    if(ev < 0)
    {
      tp_i2c_wait_for_touch_release();
      continue;
    }

    wiz_button(ev, 1);
    tp_i2c_wait_for_touch_release();

    last = ev;
    cw_event(&wiz, ev);
    written = wiz_run_actions();

    if(wiz.state != CW_END)
      wiz_draw();
  }

  //One message at the end, after Done
  if(last == CW_EV_DONE)
  {
    msg = written ? "Calibration saved" : "Calibration unchanged";
    display_set_fg_color(BLACK_COLOR);
    display_fill_rect(20, 70, 760, 390);
    display_set_font(font);
    wiz_text((800 - wiz_text_width(font, msg)) / 2, 220, written ? GREEN_COLOR : WHITE_COLOR, msg);
    timer0_delay(1200);
  }

  display_set_source_buffer(displaybuffer4);
  display_copy_rect_to_screen(0, 0, 800, 480);

  //Redraws of the settings only now, over the restored screen
  scope_acqusition_settings(0);
  scope_channel_settings(&scopesettings.channel1, 0);
  scope_channel_settings(&scopesettings.channel2, 0);

  return(last == CW_EV_DONE);
}
