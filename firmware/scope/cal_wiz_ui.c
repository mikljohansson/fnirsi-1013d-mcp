//----------------------------------------------------------------------------------------------------------------------------------
// cal_wiz_ui.c - the calibration wizard on the scope (fw 0.2.59, firmware/README.md section 6.4, docs/plans/ux-makeover.md
// 6a): the step dialog, its touch loop, and the measurements the state machine (cal_wiz.c) asks for. Opened by Calibrate
// on the Diagnostics pages, Calibrate now on the first-run screen (ui_diag_view.c) and, until the Settings rebuild
// removes it, System settings > Baseline calibration (statemachine.c). It owns the screen until Cancel or Save
// calibration and restores it afterwards.
//
// The measurement helpers (zero step and gain step of calibration data v3, adc_cal.h, README 6.3) moved here from
// scope_functions.c: only the wizard uses them, and keeping them static next to their one caller avoids exporting a
// dozen cal_* functions. They work on copies: nothing reaches adc_cal / sectors 708 and 709 before Save calibration.
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
#include "ui_dlg.h"
#include "enc_heal.h"
#include "pc_hal.h"
#include "cap_hw.h"

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

static uint8  cal_backup_invert[2];
static uint8  cal_backup_trigger;
static uint32 cal_backup_divider;

static int wiz_progress(uint32 ch, uint32 r);       //1: Cancel was tapped
static int wiz_poll_cancel(void);

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

//Both halves of a measurement merged: mean, noise and clipped samples per core, drift between the halves. noise16 is
//the RMS about the mean of all the conversions (the checked value), within16 the RMS inside the conversions (reported
//with a noise or drift failure: the difference is what moved between conversions)
static void cal_result(acal_acc acc[2][2], int32_t *mean16, int32_t *drift16, uint32_t *noise16, uint32_t *within16,
                       uint32_t *clipped)
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

    if(within16)
      within16[k] = acal_rms16_within(&all);

    *clipped += all.clipped;
  }
}

//The gated part of one range up to the centre: two coarse points, the centre between them, the slope points 150 counts
//either side of it and the centre refined on them (m->pwm, m->pwm_lo / hi, m->mean16_lo / hi of the gated class).
//ACAL_E_RESPONSE when the coarse points do not move; *centre the coarse centre (-1 then)
static int cal_centre(uint32 r, acal_range_meas *m, acal_point pt[ACAL_POINTS], int32_t *centre, acal_fail *fail)
{
  acal_acc acc[2][2];
  int32_t  mean[2], hi, lo, p;
  uint32_t unused = 0;

  memset(m, 0, sizeof(*m));
  memset(pt, 0, ACAL_POINTS * sizeof(acal_point));
  memset(fail, 0, sizeof(*fail));
  fail->point = ACAL_PT_NONE;
  *centre = -1;

  cal_set_rate(CAL_RATE_GATED);

  //Coarse centre from two far points (clipping there only biases the estimate)
  cal_measure(r, CAL_PWM_HIGHCODE, 30, 2, acc);
  cal_result(acc, mean, 0, 0, 0, &unused);
  acal_point_set(&pt[ACAL_PT_COARSE_HI], CAL_PWM_HIGHCODE, ACAL_GATED, 2, acc);
  hi = (mean[0] + mean[1]) / 2;
  cal_measure(r, CAL_PWM_LOWCODE, 30, 2, acc);
  cal_result(acc, mean, 0, 0, 0, &unused);
  acal_point_set(&pt[ACAL_PT_COARSE_LO], CAL_PWM_LOWCODE, ACAL_GATED, 2, acc);
  lo = (mean[0] + mean[1]) / 2;

  p = acal_centre_pwm(CAL_PWM_HIGHCODE, hi, CAL_PWM_LOWCODE, lo);

  if(p < 0)
  {
    acal_fail_point(pt, ACAL_E_RESPONSE, fail);
    return ACAL_E_RESPONSE;
  }

  *centre = p;

  //Slope of each core, gated
  m->pwm_lo = p - CAL_FINE_STEP;
  m->pwm_hi = p + CAL_FINE_STEP;
  cal_measure(r, m->pwm_lo, 60, 4, acc);
  cal_result(acc, m->mean16_lo[ACAL_GATED], 0, 0, 0, &m->clipped);
  acal_point_set(&pt[ACAL_PT_G_LO], m->pwm_lo, ACAL_GATED, 4, acc);
  cal_measure(r, m->pwm_hi, 60, 4, acc);
  cal_result(acc, m->mean16_hi[ACAL_GATED], 0, 0, 0, &m->clipped);
  acal_point_set(&pt[ACAL_PT_G_HI], m->pwm_hi, ACAL_GATED, 4, acc);

  //Centre refined on the slope points
  m->pwm = acal_interp_pwm(m->pwm_lo, (m->mean16_lo[ACAL_GATED][0] + m->mean16_lo[ACAL_GATED][1]) / 2, m->pwm_hi,
                           (m->mean16_hi[ACAL_GATED][0] + m->mean16_hi[ACAL_GATED][1]) / 2);
  return ACAL_OK;
}

//One range of the zero step (inputs open or shorted), relay already set: the PWM centre where the mean of both cores
//reads 128 (cal_centre), the offset slope of each core from two PWM points 150 counts either side and its zero at that
//centre, each with the ADC clock gated (2 MSa/s, every legacy time base but 200 MSa/s) and at full rate (200 MSa/s, every
//filtered capture). Every point goes into pt (acal_point, ACAL_PT_*), *centre is the coarse centre (-1 when it failed).
//Returns ACAL_E_RESPONSE, ACAL_E_TIMING, ACAL_E_READOUT or acal_range_check's result with *fail (point fields included).
//Shared by the wizard and CAL_PROBE (fw 0.2.67), so the host sees exactly the wizard's measurement.
//
//fw 0.2.79 (owner 2026-10-19 20:30, fail fast): the zero is measured at the centre again, where the level straddles
//0x7F / 0x80 and the most bits flip. 0.2.67-0.2.78 measured it as the mean of two points 12 codes either side to pass on
//FPGA 0x1538 / 0x1539, whose 200 MSa/s capture garbled cores near 128; a bitstream with that fault now fails the step
//with "ADC readout fault near code 128" instead of being calibrated around it
static int cal_range(uint32 r, acal_range_meas *m, acal_point pt[ACAL_POINTS], int32_t *centre, acal_fail *fail)
{
  acal_acc acc[2][2];
  int      rc;

  rc = cal_centre(r, m, pt, centre, fail);

  if(rc != ACAL_OK)
    return rc;

  //The zeros at the refined centre, gated
  cal_measure(r, m->pwm, 60, 8, acc);
  cal_result(acc, m->zero16[ACAL_GATED], m->drift16[ACAL_GATED], m->noise16[ACAL_GATED], m->within16[ACAL_GATED],
             &m->clipped);
  acal_point_set(&pt[ACAL_PT_G_ZERO], m->pwm, ACAL_GATED, 8, acc);

  //Full rate: a 1000-sample readout spans 5 us of the 40 us offset-PWM ripple period, so each conversion sees one
  //ripple phase; 64 conversions (about 70 ms) average it out (bench 0.2.43: 8 left 0.2-0.4 code). The full-rate
  //slope points too: the cores' gains differ per class, and so may their offset slopes
  cal_set_rate(CAL_RATE_FULL);

  //Every full-rate conversion is checked and re-armed when garbled (fpga_done_conversion, enc_heal.h); one that stayed
  //garbled after the re-arms fails the range
  fpga_enc_garbled = 0;
  cal_measure(r, m->pwm, 30, 64, acc);
  cal_result(acc, m->zero16[ACAL_FULL], m->drift16[ACAL_FULL], m->noise16[ACAL_FULL], m->within16[ACAL_FULL],
             &m->clipped);
  acal_point_set(&pt[ACAL_PT_F_ZERO], m->pwm, ACAL_FULL, 64, acc);
  cal_measure(r, m->pwm_hi, 60, 64, acc);
  cal_result(acc, m->mean16_hi[ACAL_FULL], 0, 0, 0, &m->clipped);
  acal_point_set(&pt[ACAL_PT_F_HI], m->pwm_hi, ACAL_FULL, 64, acc);
  cal_measure(r, m->pwm_lo, 60, 64, acc);
  cal_result(acc, m->mean16_lo[ACAL_FULL], 0, 0, 0, &m->clipped);
  acal_point_set(&pt[ACAL_PT_F_LO], m->pwm_lo, ACAL_FULL, 64, acc);

  if(fpga_enc_garbled)
  {
    acal_fail_point(pt, ACAL_E_TIMING, fail);
    return ACAL_E_TIMING;
  }

  rc = acal_range_check(m, fail);

  //Samples at the rails only at full rate while the level is near 128: the readout fault, not the input
  if((rc == ACAL_E_CLIP) && acal_readout_fault(pt))
    rc = ACAL_E_READOUT;

  acal_fail_point(pt, rc, fail);
  return rc;
}

//fw 0.2.79: one training capture (cap_stage.h): 0x63 = v, CS_TRAIN_WORDS words at 200 MSa/s with the trigger off (the
//rate and set-up of the zero step), the four 0x36 counts. The UI's record geometry is put back afterwards
static int cal_cap_measure(void *ctx, uint16_t v, uint16_t cnt[4])
{
  uint32 total = fpgasettings.totalsamples;

  (void)ctx;
  fpga_capture_write(v);
  fpgasettings.totalsamples = 2 * CS_TRAIN_WORDS;
  fpga_do_conversion();
  while(fpga_done_conversion() == 0) PC_BUSY_POLL();
  fpga_capture_counts(cnt);
  fpgasettings.totalsamples = total;
  return 0;
}

//fw 0.2.81: the training band's offset PWM per point (cs_band_pwm from the gated slope points of cal_centre)
#define CAL_BAND_SETTLE_MS  60      //per point: 4 codes (about 15 PWM counts) from the last, as cal_measure's steps

static int32_t cal_band_pwm[CS_BAND_POINTS];

static int cal_cap_park(void *ctx, uint32_t p)
{
  (void)ctx;

  if(p >= CS_BAND_POINTS)
    return -1;

  calibrationsettings.dc_calibration_offset[0] = (uint16)cal_band_pwm[p];
  fpga_set_channel_offset(&calibrationsettings);
  timer0_delay(CAL_BAND_SETTLE_MS);
  return 0;
}

//fw 0.2.79: FPGA 0x153A capture stage training of channel ch's two cores, the first thing of its zero step: range 0, the
//gated centre search, a quiet input checked (a signal would count at every stage), then cs_train_run. fw 0.2.81: over
//the band of codes 120..140 around the centre (cs_band_pwm, CS_BAND_REPS captures per stage and point), since a stage at
//the edge of the AD9288 data transition garbles only at some codes and in some captures (bench 2026-10-20, CH1 A4).
//ACAL_OK, ACAL_E_RESPONSE / ACAL_E_NOISE (the input), ACAL_E_READOUT (a core without a clean stage: fail fast)
static int cal_train(uint32 ch, cs_train *t, acal_fail *fail)
{
  static const cs_io io = { 0, CS_BAND_POINTS, CS_BAND_REPS, cal_cap_park, cal_cap_measure };
  acal_range_meas m;
  acal_point      pt[ACAL_POINTS];
  int32_t         centre, mlo, mhi;
  uint32          k, i;
  int             rc;

  memset(t, 0, sizeof(*t));
  calibrationsettings.samplevoltperdiv = 0;
  fpga_set_channel_voltperdiv(&calibrationsettings);
  timer0_delay(60);

  rc = cal_centre(0, &m, pt, &centre, fail);

  if(rc != ACAL_OK)
    return rc;

  for(i=ACAL_PT_G_LO;i<=ACAL_PT_G_HI;i++)
    for(k=0;k<2;k++)
      if(pt[i].within16[k] > ACAL_MAX_NOISE16)
      {
        fail->cls      = ACAL_GATED;
        fail->core     = (uint8_t)k;
        fail->value    = (int32_t)pt[i].within16[k];
        fail->within16 = pt[i].within16[k];
        return ACAL_E_NOISE;
      }

  //The band's PWM points from the gated slope points (both cores' mean); every one inside the PWM range, else the
  //offset control cannot reach the band (fail fast as a centre that does not respond)
  mlo = (m.mean16_lo[ACAL_GATED][0] + m.mean16_lo[ACAL_GATED][1]) / 2;
  mhi = (m.mean16_hi[ACAL_GATED][0] + m.mean16_hi[ACAL_GATED][1]) / 2;

  for(i=0;i<CS_BAND_POINTS;i++)
  {
    cal_band_pwm[i] = cs_band_pwm(m.pwm_lo, mlo, m.pwm_hi, mhi, cs_band_code(i));

    if((cal_band_pwm[i] < ACAL_PWM_MIN) || (cal_band_pwm[i] > ACAL_PWM_MAX))
    {
      acal_fail_point(pt, ACAL_E_RESPONSE, fail);
      return ACAL_E_RESPONSE;
    }
  }

  cal_set_rate(CAL_RATE_FULL);

  rc = fpga_capture_train((uint8_t)(ch ? 0x0C : 0x03), &io, t);
  return (rc == CS_OK) ? ACAL_OK : ACAL_E_READOUT;
}

//The last range cal_channel measured per channel, for CAL_PROBE in the wizard's context
static acal_point cal_last_pt[2][ACAL_POINTS];
static acal_fail  cal_last_fail[2];
static int32_t    cal_last_centre[2], cal_last_pwm[2];
static uint8      cal_last_r[2];
static uint8      cal_last_garbled[2];
static uint8      cal_cap_st[2], cal_cap_bad[2];   //fw 0.2.79: the channel's capture stage training (CW_C_*)

//Zero step of one channel: every range (cal_range) and the store. *match16: the worst |drift A - drift B| of the zero
//points over ranges and classes (summary); *fail and *fail_r: where a failed range failed (the result line and its note)
static int cal_channel(acal_channel *c, uint32 ch, uint32_t *match16, acal_fail *fail, uint32_t *fail_r)
{
  acal_range_meas m;
  acal_point      pt[ACAL_POINTS];
  int32_t         d, centre;
  uint32_t        r, cl;
  int             rc;

  *match16 = 0;
  memset(fail, 0, sizeof(*fail));
  fail->point = ACAL_PT_NONE;
  *fail_r = 0;
  cal_cap_st[ch]  = CW_C_NONE;
  cal_cap_bad[ch] = 0;

  //fw 0.2.79: on FPGA 0x153A the capture stages of this channel's cores first (cal_train), the zeros after them
  if(fpga_capture_has())
  {
    cs_train t;

    //fw 0.2.81: the band training takes about half a second per channel (twice that with the reference pass): say so
    if(wiz_progress(ch, CW_PR_TRAIN))
      return CW_RC_CANCELLED;

    rc = cal_train(ch, &t, fail);

    if(rc != ACAL_OK)
    {
      cal_cap_st[ch]  = (rc == ACAL_E_READOUT) ? CW_C_FAIL : CW_C_NONE;
      cal_cap_bad[ch] = t.bad;
      return rc;
    }

    cal_cap_st[ch] = CW_C_DONE;
  }

  for(r=0;r<ACAL_RANGES;r++)
  {
    if(wiz_progress(ch, r))
      return CW_RC_CANCELLED;

    *fail_r = r;
    calibrationsettings.samplevoltperdiv = r;
    fpga_set_channel_voltperdiv(&calibrationsettings);
    timer0_delay(60);

    rc = cal_range(r, &m, pt, &centre, fail);

    memcpy(cal_last_pt[ch], pt, sizeof(pt));
    cal_last_fail[ch]    = *fail;
    cal_last_centre[ch]  = centre;
    cal_last_pwm[ch]     = (centre < 0) ? -1 : m.pwm;
    cal_last_r[ch]       = (uint8)r;
    cal_last_garbled[ch] = fpga_enc_garbled;

    if((rc == ACAL_E_RESPONSE) || (rc == ACAL_E_TIMING))
      return rc;

    for(cl=0;cl<ACAL_CLASSES;cl++)
    {
      d = m.drift16[cl][ACAL_CORE_A] - m.drift16[cl][ACAL_CORE_B];
      d = (d < 0) ? -d : d;

      if((uint32_t)d > *match16)
        *match16 = (uint32_t)d;
    }

    if(rc == ACAL_OK)
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

//Per core means (1/16 code) of convs conversions at one post level, rate and PWM. 0 when a full-rate capture stayed
//garbled after the re-arms (enc_heal.h)
static int cal_gain_level(uint32 r, int32 pwm, uint32 rate, uint32 settle_ms, uint32 convs, uint32 high, int32_t *mean16,
                          uint32_t *clipped)
{
  acal_acc acc[2][2];

  cal_set_rate(rate);
  cal_gen_level(high);
  fpga_enc_garbled = 0;
  cal_measure(r, pwm, settle_ms, convs, acc);
  cal_result(acc, mean16, 0, 0, 0, clipped);
  return !fpga_enc_garbled;
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
    if(wiz_poll_cancel())
      return CW_RC_CANCELLED;

    cal_gain_level(r, pwm, CAL_RATE_GATED, l ? 30 : 100, 8, l == 1, m.mean16[ACAL_GATED][l], &m.clipped);

    if(!cal_gain_level(r, pwm, CAL_RATE_FULL, 30, 64, l == 1, m.mean16[ACAL_FULL][l], &m.clipped))
      return ACAL_E_TIMING;
  }

  return acal_gain_store(&m, r, c);
}

//Measurement set-up: trigger off, AUTO mode (no lockup in NORMAL or SINGLE), level 0, 1000 samples per readout (500 per
//core), the raw scratch channel (no compensation, no invert, no dcoffset, DC coupling)
static void cal_begin(void)
{
  PCHANNELSETTINGS chs[2] = { &scopesettings.channel1, &scopesettings.channel2 };
  uint32           ch;

  fpga_trace_who = FPGA_WHO_CAL;

  cal_backup_divider = fpga_divider;
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

  //A raw divider a host wrote (SET_TIMEBASE raw) is not in the rate table: written back as it was (as the class probe)
  if(fpga_divider != cal_backup_divider)
  {
    fpga_write_cmd(0x0D);
    fpga_write_int(cal_backup_divider);
    fpga_divider = cal_backup_divider;
  }

  fpga_set_time_base(scopesettings.timeperdiv);

  scopesettings.triggermode = cal_backup_trigger;
  fpga_set_trigger_mode();

  fpga_trace_who = FPGA_WHO_UI;
}

//----------------------------------------------------------------------------------------------------------------------------------
//The wizard (docs/plans/ux-makeover.md 6a): one step dialog over the dimmed, frozen screen (ui_dlg.h)

static cal_wiz      wiz;
static acal_channel wiz_new[2];         //zero step result (both channels)
static acal_channel wiz_work[2];        //what Save commits (CW_ACT_PREPARE, then the gain screens)
static int32_t      wiz_span[2];        //level check: span and midpoint for the measurement after it
static int32_t      wiz_mid[2];
static uint8        wiz_usb;            //a USB host was seen at the last check
static uint8        wiz_session;        //FPGA in the measurement set-up (cal_begin)
static uint8        wiz_gen;            //the generator was used in this session
static uint8        wiz_abort;          //Cancel tapped while a measurement ran
static ui_step      wiz_dlg;            //what is on the screen
static char         wiz_res[2][96];     //the result lines
static char         wiz_note[2][128];   //their notes (a zero step failure's numbers, fw 0.2.67)
static int8         wiz_zrc_seen[2];    //zero step in progress: channels finished (1) and their result
static int          wiz_zrc[2];
static uint16       wiz_cap_start;      //fw 0.2.79: 0x63 when the wizard opened (Cancel puts it back)

static const char *wiz_label(int ev)
{
  switch(ev)
  {
    case CW_EV_CALIBRATE: return "Calibrate";
    case CW_EV_CONTINUE:  return "Continue";
    case CW_EV_SAVE:      return "Save calibration";
    case CW_EV_MEASURING: return "Measuring...";
    default:              return 0;
  }
}

//USB host attached and not suspended: the MUSB frame number moves (1 ms frames)
static uint8 wiz_usb_host(void)
{
  uint32 f = *USBC_REG_FRNUM & 0x7FF;

  timer0_delay(3);
  return (*USBC_REG_FRNUM & 0x7FF) != f;
}

static void wiz_result(uint32 i, int icon, int colour)
{
  wiz_dlg.result[i]        = wiz_res[i];
  wiz_dlg.result_icon[i]   = (uint8)icon;
  wiz_dlg.result_colour[i] = (uint8)colour;
}

//The dialog for the machine's state; progress lines (zero step, gain step) are filled by the measurement callbacks
static void wiz_build(void)
{
  int    a = cw_action_button(&wiz);
  uint32 ch;

  wiz_dlg.title    = cw_title(&wiz);

  //fw 0.2.68 (owner): "Step M of N" at the right end of the title line, as the volts calibration. Four screens with the
  //generator (zero, CH1 gain, CH2 gain, save); without it (CW_GAIN_NA) three, the save screen being step 3
  {
    static char st[] = "Step 1 of 4";
    int         m = cw_screen(&wiz), n = wiz.al3 ? 4 : 3;

    if(m > n)
      m = n;

    st[5]  = (char)('0' + m);
    st[10] = (char)('0' + n);
    wiz_dlg.step = m ? st : 0;
  }
  wiz_dlg.line[0]  = cw_instruction(&wiz, 0);
  wiz_dlg.line[1]  = cw_instruction(&wiz, 1);
  wiz_dlg.warn     = (wiz_usb && (cw_screen(&wiz) == 1)) ? "USB is connected: unplug it before Calibrate" : "";
  wiz_dlg.left     = "Cancel";
  wiz_dlg.right    = wiz_label(a);
  wiz_dlg.right_style = (a == CW_EV_MEASURING) ? UI_ACT_DISABLED : UI_ACT_PRIMARY;
  wiz_dlg.result[0] = wiz_dlg.result[1] = "";
  wiz_dlg.note[0] = wiz_dlg.note[1] = "";
  wiz_res[0][0] = wiz_res[1][0] = 0;

  switch(wiz.state)
  {
    case CW_ZERO:
      for(ch=0;ch<2;ch++)
      {
        cw_zero_line(&wiz, ch, wiz_res[ch]);
        wiz_result(ch, (wiz.zero_st[ch] == CW_Z_PASS) ? 1 : 2, (wiz.zero_st[ch] == CW_Z_PASS) ? UD_C_TITLE : UD_C_WARN);
        cw_zero_note(&wiz, ch, wiz_note[ch]);
        wiz_dlg.note[ch] = wiz_note[ch];
      }
      break;

    case CW_GAIN:
      cw_gain_line(&wiz, wiz_res[0]);
      wiz_result(0, (wiz.gain_st[wiz.ch] == CW_G_PASS) ? 1 : 2,
                 (wiz.gain_st[wiz.ch] == CW_G_PASS) ? UD_C_TITLE : UD_C_WARN);
      break;

    case CW_DONE:
      for(ch=0;ch<2;ch++)
      {
        cw_done_line(&wiz, ch, &wiz_work[ch], wiz_res[ch]);
        wiz_result(ch, 1, ch ? UD_C_CH2 : UD_C_CH1);
      }
      break;

    default:
      break;
  }
}

static void wiz_show(void)
{
  wiz_build();
  ui_dlg_step_draw(&wiz_dlg);
}

//A measurement runs: Cancel stays live. Checked between the measurement points (about once a second); the press shows
//at once, the measurement stops at the next point
static int wiz_poll_cancel(void)
{
  if(wiz_abort)
    return 1;

  tp_i2c_read_status();

  if(havetouch && (ui_dlg_step_hit(&wiz_dlg, xtouch, ytouch) == 1))
  {
    ui_dlg_step_press(&wiz_dlg, 1);
    ui_dlg_release();
    wiz_abort = 1;
  }

  return wiz_abort;
}

//Zero step progress: "CH1: zero level done" for a finished channel, "CH2: measuring range 3 of 6" for the running one
static int wiz_progress(uint32 ch, uint32 r)
{
  uint32 i;

  for(i=0;i<2;i++)
  {
    if(wiz_zrc_seen[i])
    {
      char *p = wiz_res[i];

      p[0] = 'C';
      p[1] = 'H';
      p[2] = (char)('1' + i);
      p[3] = 0;

      if(wiz_zrc[i] == ACAL_OK)
        memcpy(p + 3, ": zero level done", 18);
      else
      {
        memcpy(p + 3, ": ", 3);
        strcpy(p + 5, cw_reason_text(wiz_zrc[i]));
      }

      wiz_result(i, (wiz_zrc[i] == ACAL_OK) ? 1 : 2, (wiz_zrc[i] == ACAL_OK) ? UD_C_TITLE : UD_C_WARN);
    }
    else if(i == ch)
    {
      cw_progress(wiz_res[i], ch, r);
      wiz_result(i, 3, UD_C_TEXT);
    }
    else
      wiz_dlg.result[i] = "";
  }

  ui_dlg_step_draw(&wiz_dlg);
  return wiz_poll_cancel();
}

//Gain step progress, one line
static void wiz_gain_progress(uint32 ch, int measuring)
{
  char *p = wiz_res[0];

  p[0] = 'C';
  p[1] = 'H';
  p[2] = (char)('1' + ch);
  strcpy(p + 3, measuring ? ": measuring the gain, about 1 s" : ": checking the signal on the post");
  wiz_result(0, 3, UD_C_TEXT);
  wiz_dlg.result[1] = "";
  ui_dlg_step_draw(&wiz_dlg);
}

//CW_ACT_PREPARE: the working copy for screens 2-4. The new zeros take the previous block's measured gains
//(acal_gain_carry, flag KEPT) until the gain screens measure new ones
static void wiz_prepare(void)
{
  uint32 ch;

  for(ch=0;ch<2;ch++)
  {
    wiz_work[ch] = wiz_new[ch];
    acal_gain_carry(adc_cal_loaded ? &adc_cal[ch] : 0, &wiz_work[ch]);
  }
}

//CW_ACT_COMMIT: the PWM centres and the 1.00o5 compensations (for older firmware too), the blocks, sectors 708 (blocks)
//and 709 (the centres they are bound to)
static void wiz_commit(void)
{
  PCHANNELSETTINGS chs[2] = { &scopesettings.channel1, &scopesettings.channel2 };
  uint32           ch, r;

  for(ch=0;ch<2;ch++)
  {
    for(r=0;r<ACAL_RANGES;r++)
      chs[ch]->dc_calibration_offset[r] = wiz_work[ch].pwm[r];

    chs[ch]->dc_calibration_offset[6] = chs[ch]->dc_calibration_offset[5];
    acal_atlan_comps(&wiz_work[ch], &chs[ch]->adc1compensation, &chs[ch]->adc2compensation);

    //The new centre into the FPGA
    fpga_set_channel_offset(chs[ch]);

    adc_cal[ch] = wiz_work[ch];
  }

  adc_cal_loaded = 1;

  //fw 0.2.79: the trained 0x153A capture stages are stored with the calibration (sector 708 bytes 488..503)
  fpga_capture_commit();

  scope_save_input_calibration_data();
  scope_save_configuration_data();

  cal_wizard_committed();
}

//Runs the pending actions (a measurement result can ask for the next one), then the FPGA back to the UI's settings.
//Returns 1 when Save wrote the calibration
static uint32 wiz_run_actions(void)
{
  uint32_t match[2] = { 0, 0 }, fail_r;
  acal_fail fail;
  int      rc[2];
  uint32   ch, written = 0;
  uint8    ach;
  int      act, hold;

  //fw 0.2.78: a measurement or the Save write runs to its end; a host request meanwhile gets E_BUSY with the reason
  hold = pc_glue_ui_hold(PC_HOLD_CAL);

  while((act = cw_take(&wiz, &ach)) != CW_ACT_NONE)
  {
    if((act == CW_ACT_ZERO) || (act == CW_ACT_GAIN_LEVEL) || (act == CW_ACT_GAIN_MEASURE))
    {
      wiz_build();

      if(!wiz_session)
      {
        cal_begin();
        wiz_session = 1;
      }
    }

    switch(act)
    {
      case CW_ACT_ZERO:
        //Both channels (both results shown); nothing is used unless both pass
        wiz_zrc_seen[0] = wiz_zrc_seen[1] = 0;

        for(ch=0;ch<2;ch++)
        {
          rc[ch] = CW_RC_CANCELLED;

          if(!wiz_abort)
          {
            cal_scratch_channel(ch);
            memset(&wiz_new[ch], 0, sizeof(acal_channel));
            rc[ch] = cal_channel(&wiz_new[ch], ch, &match[ch], &fail, &fail_r);
            cw_cap_result(&wiz, ch, cal_cap_st[ch], fpga_capture_value(), cal_cap_bad[ch]);
            cw_zero_detail(&wiz, ch, fail_r, &fail);
          }

          wiz_zrc[ch]      = rc[ch];
          wiz_zrc_seen[ch] = 1;
        }

        if(wiz_abort)
          cw_event(&wiz, CW_EV_CANCEL);
        else
          cw_zero_result(&wiz, rc, match);
        break;

      case CW_ACT_PREPARE:
        wiz_prepare();
        break;

      case CW_ACT_GAIN_LEVEL:
        wiz_gen = 1;
        wiz_gain_progress(ach, 0);
        cal_scratch_channel(ach);
        wiz_span[ach] = cal_gain_check(&wiz_work[ach], &wiz_mid[ach]);

        if(wiz_poll_cancel())
          cw_event(&wiz, CW_EV_CANCEL);
        else
          cw_level_result(&wiz, wiz_span[ach]);
        break;

      case CW_ACT_GAIN_MEASURE:
      {
        acal_channel t = wiz_work[ach];

        wiz_gen = 1;
        wiz_gain_progress(ach, 1);
        cal_scratch_channel(ach);
        rc[0] = cal_gain_measure(&t, wiz_span[ach], wiz_mid[ach]);

        if(wiz_abort)
          cw_event(&wiz, CW_EV_CANCEL);
        else
        {
          if(rc[0] == ACAL_OK)
            wiz_work[ach] = t;

          cw_gain_result(&wiz, rc[0]);
        }
        break;
      }

      case CW_ACT_COMMIT:
        wiz_commit();
        written = 1;
        break;

      case CW_ACT_DISCARD:
        //The copies are dropped, nothing was written; a capture stage training goes back to the value before
        fpga_capture_write(wiz_cap_start);
        break;

      default:
        break;
    }
  }

  if(wiz_session)
  {
    cal_end(wiz_gen);
    wiz_session = 0;
    wiz_gen     = 0;
  }

  pc_glue_ui_hold(hold);
  return written;
}

//The modal wizard. Owns the screen until Cancel or Save calibration and restores it; returns 1 when it saved
uint32 cal_wizard_run(void)
{
  uint32 written = 0, tick;
  int    hit, ev;

  cw_init(&wiz, fpgasettings.fw_FPGA == 2);
  wiz_abort = 0;
  wiz_usb   = wiz_usb_host();
  wiz_cap_start = fpga_capture_value();
  tick      = timer0_get_ticks();

  ui_dlg_bg_begin();
  wiz_show();

  while(wiz.state != CW_END)
  {
    //Screen 1: the USB warning follows the cable (checked twice a second, redrawn only when it changes)
    if((cw_screen(&wiz) == 1) && (timer0_get_ticks() - tick >= 500))
    {
      uint8 usb = wiz_usb_host();

      tick = timer0_get_ticks();

      if(usb != wiz_usb)
      {
        wiz_usb = usb;
        wiz_show();
      }
    }

    tp_i2c_read_status();

    //fw 0.2.78: between steps a host request cancels the wizard as Cancel does, nothing written (protocol 1.23); while
    //a step measures (wiz_run_actions) the hold answers it E_BUSY "calibration measurement running" instead
    if(PC_UI_YIELD())
    {
      cw_event(&wiz, CW_EV_CANCEL);
      written |= wiz_run_actions();
      continue;
    }

    if(!havetouch)
      continue;

    //Only the two buttons react; anything else waits for the release and does nothing (the dialog owns the screen)
    hit = ui_dlg_step_hit(&wiz_dlg, xtouch, ytouch);

    if(!hit)
    {
      ui_dlg_release();
      continue;
    }

    ui_dlg_step_press(&wiz_dlg, hit);
    ui_dlg_release();

    ev = (hit == 1) ? CW_EV_CANCEL : cw_action_button(&wiz);
    wiz_abort = 0;
    cw_event(&wiz, ev);
    written |= wiz_run_actions();

    if(wiz.state != CW_END)
      wiz_show();
  }

  ui_dlg_bg_end();
  return written;
}

//----------------------------------------------------------------------------------------------------------------------------------
//CAL_PROBE (fw 0.2.67, protocol 1.21 provisional, docs/protocol.md section 14 row 61): the zero step from the host through
//the wizard's own code, nothing stored. Mode 0: one channel and range (cal_begin, the scratch channel, the relay, cal_range,
//cal_end). Mode 1: the whole zero step as the wizard runs it after a Calibrate tap, in its screen context: the dimmed
//screen and the step dialog, the progress lines, the Cancel polls, both channels, every range (wiz_run_actions); the
//response then has the given channel's last measured range (the failing one, or range 6 when it passed). The scratch
//channel reads into the UI's trace buffers: their first 1000 bytes are put back, so a stopped trace stays as it was.
//MAIN context, UI stopped (the session checks)

static uint8 cal_probe_keep[2][1000];

int32_t pc_hal_cal_probe(uint8_t ch, uint8_t range, uint8_t settle, uint8_t prestrobe, uint8_t mode, uint8_t *dst)
{
  acal_range_meas m;
  acal_point      pt[ACAL_POINTS];
  acal_fail       fail;
  pc_hw_enc       e0, e1;
  int32_t         centre, pwm;
  uint8           flags = 0;
  int             rc;

  uint16          cap0 = fpga_capture_value();

  if((ch > 1) || (range >= ACAL_RANGES) || (settle > 15) || (prestrobe > 1) || (mode > 1))
    return -1;

  memcpy(cal_probe_keep[0], channel1tracebuffer, sizeof(cal_probe_keep[0]));
  memcpy(cal_probe_keep[1], channel2tracebuffer, sizeof(cal_probe_keep[1]));
  pc_hal_enc_state(&e0);

  fpga_read_settle    = settle;
  fpga_read_prestrobe = prestrobe;

  if(mode == 0)
  {
    cal_begin();
    cal_scratch_channel(ch);
    calibrationsettings.samplevoltperdiv = range;
    fpga_set_channel_voltperdiv(&calibrationsettings);
    timer0_delay(60);

    rc  = cal_range(range, &m, pt, &centre, &fail);
    pwm = (centre < 0) ? -1 : m.pwm;

    if(fpga_enc_garbled)
      flags |= ACAL_PROBE_GARBLED;

    cal_end(0);
  }
  else
  {
    //What a Calibrate tap on screen 1 does (cal_wizard_run): the dialog over the dimmed screen, the ZERO action
    memset(cal_last_pt, 0, sizeof(cal_last_pt));
    memset(cal_last_fail, 0, sizeof(cal_last_fail));
    cal_last_fail[0].point = cal_last_fail[1].point = ACAL_PT_NONE;
    cal_last_centre[0] = cal_last_centre[1] = -1;
    cal_last_pwm[0] = cal_last_pwm[1] = -1;
    cal_last_r[0] = cal_last_r[1] = 0;
    cal_last_garbled[0] = cal_last_garbled[1] = 0;

    cw_init(&wiz, fpgasettings.fw_FPGA == 2);
    wiz_abort = 0;
    wiz_usb   = 0;
    ui_dlg_bg_begin();
    wiz_show();
    cw_event(&wiz, CW_EV_CALIBRATE);
    wiz_run_actions();

    rc = wiz.zero_rc[ch];

    //A passed channel: its last range was range 6 (index 5); a cancelled run reports CW_RC_CANCELLED (-1)
    if(wiz.zero_st[ch] == CW_Z_NONE)
      rc = CW_RC_CANCELLED;

    wiz_show();
    timer0_delay(500);
    ui_dlg_bg_end();
    cw_init(&wiz, fpgasettings.fw_FPGA == 2);

    memcpy(pt, cal_last_pt[ch], sizeof(pt));
    fail   = cal_last_fail[ch];
    centre = cal_last_centre[ch];
    pwm    = cal_last_pwm[ch];
    range  = cal_last_r[ch];

    if(cal_last_garbled[ch])
      flags |= ACAL_PROBE_GARBLED;

    flags |= ACAL_PROBE_WIZARD;

    //Nothing stored: a capture stage training of the zero step goes back to the value before (fw 0.2.79)
    fpga_capture_write(cap0);
  }

  if(centre < 0)
    flags |= ACAL_PROBE_NOCENTRE;

  flags |= (uint8)((settle << 4) | (prestrobe ? ACAL_PROBE_PRESTROBE : 0));
  fpga_read_settle    = 0;
  fpga_read_prestrobe = 0;

  memcpy(channel1tracebuffer, cal_probe_keep[0], sizeof(cal_probe_keep[0]));
  memcpy(channel2tracebuffer, cal_probe_keep[1], sizeof(cal_probe_keep[1]));
  pc_hal_enc_state(&e1);

  return (int32_t)acal_probe_pack(dst, ch, range, rc, flags, centre, pwm, e1.bad - e0.bad, e1.recovered - e0.recovered,
                                  &fail, pt);
}
