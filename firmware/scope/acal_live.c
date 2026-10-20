//----------------------------------------------------------------------------------------------------------------------------------
// acal_live.c - calibration v4 on the scope (acal_live.h, docs/plans/interleave-tracking.md, protocol row 53)
//
// A - B tracker: fpga_channel_correction folds the tracked d / e into every CH1 / CH2 correction (acal_v4_apply: a no-op
// unless v3 is bound and the class is full); fpga_prepare_correction remembers what a readout applied; at the end of
// fpga_read_sample_data an 8-bit full-class record is offered to acal_track_record. 16-bit readouts, the gated class and
// the calibration's scratch channel (calibrationsettings) never feed it nor get it.
//
// Class probe (design section 8): from the main loop, per channel at most every ACAL_PROBE_EVERY_MS, gated / full / gated
// conversions on the live input through a private raw scratch channel (its own trace buffer, so the screen's traces stay),
// then everything the UI had back in the FPGA. The UI's next arm re-applies its filter setting (pc_dec_apply).
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>

#include "types.h"
#include "variables.h"
#include "fpga_control.h"
#include "timer.h"
#include "scope_functions.h"
#include "pc_hooks.h"
#include "acal_live.h"

#define ACAL_PROBE_RATE_GATED   6       //2 MSa/s, as the calibration's gated points
#define ACAL_PROBE_RATE_FULL    0       //200 MSa/s
#define ACAL_PROBE_SAMPLES      500     //per core and conversion (the calibration's 1000-sample readout)
#define ACAL_PROBE_CONVS_GATED  8
#define ACAL_PROBE_CONVS_FULL   64      //5 us each: 64 spread the 40 us offset-PWM ripple period

acal_track acal_trk[2];
acal_cm    acal_cms[2];

//What the last readout of a channel applied (acal_live_prepared)
static uint8  live_armed[2];            //v3 bound and full class: the record may feed the tracker
static int32  live_d[2];
static int32  live_e[2];

#if PC_CAL_CLASS_PROBE
static uint32 probe_due[2] = { ACAL_PROBE_FIRST_MS, ACAL_PROBE_FIRST_MS };
static uint8  probe_next;
static uint8  probe_buf[2 * ACAL_PROBE_SAMPLES];
static CHANNELSETTINGS probe_ch;        //zero: no compensation, no invert, no dcoffset, DC: raw codes (not CH1 / CH2)
#endif

static int live_ch(PCHANNELSETTINGS s)
{
  if(s == &scopesettings.channel1)
    return 0;

  if(s == &scopesettings.channel2)
    return 1;

  return -1;
}

//----------------------------------------------------------------------------------------------------------------------------------

void acal_live_reset(void)
{
  uint32 ch;

  for(ch=0;ch<2;ch++)
  {
    acal_track_reset(&acal_trk[ch]);
    acal_cm_reset(&acal_cms[ch]);
    live_armed[ch] = 0;
    live_d[ch]     = 0;
    live_e[ch]     = 0;
#if PC_CAL_CLASS_PROBE
    probe_due[ch]  = timer0ticks + ACAL_PROBE_FIRST_MS;
#endif
  }
}

//The calibration wizard's Done wrote a calibration (cal_wiz_ui.c): the tracked values belong to the old one
void cal_wizard_committed(void)
{
  acal_live_reset();
}

void acal_live_apply(PCHANNELSETTINGS s, acal_corr *c)
{
  int ch = live_ch(s);

  if(ch >= 0)
    acal_v4_apply(&acal_trk[ch], &acal_cms[ch], c);
}

void acal_live_prepared(PCHANNELSETTINGS s, const acal_corr *c)
{
  int ch = live_ch(s);

  if(ch < 0)
    return;

  live_armed[ch] = (c->version == 3) && (c->cls == ACAL_FULL);
  live_d[ch]     = acal_trk[ch].d_q8;
  live_e[ch]     = acal_trk[ch].e_q24;
}

void acal_live_record(PCHANNELSETTINGS s, uint32 wide)
{
  int ch = live_ch(s);

  if((ch < 0) || wide || !live_armed[ch])
    return;

  //tracebuffer[0] core B, [1] core A, nofsamples per core (fpga_read_sample_data's two passes)
  acal_track_record(&acal_trk[ch], s->tracebuffer, 2 * scopesettings.nofsamples, s->invert, live_d[ch], live_e[ch],
                    timer0ticks);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Class probe

#if PC_CAL_CLASS_PROBE
//convs conversions at one rate through the raw scratch channel: per-core mean and RMS (1/16 code), clipped samples
static void probe_part(uint32 rate, uint32 convs, int32_t *mean16, uint32_t *rms16, uint32_t *clipped)
{
  acal_acc acc[2];
  uint32   i, k;

  fpga_set_sample_rate(rate);
  fpga_set_time_base(sample_rate_time_per_div[rate]);

  memset(acc, 0, sizeof(acc));

  for(i=0;i<convs;i++)
  {
    fpga_do_conversion();
    while(fpga_done_conversion() == 0) PC_BUSY_POLL();

    fpga_read_sample_data(&probe_ch, 100);
    acal_acc_add(&acc[ACAL_CORE_A], probe_buf + 1, ACAL_PROBE_SAMPLES, 2);
    acal_acc_add(&acc[ACAL_CORE_B], probe_buf, ACAL_PROBE_SAMPLES, 2);
  }

  for(k=0;k<2;k++)
  {
    mean16[k] = acal_mean16(&acc[k]);
    rms16[k]  = acal_rms16(&acc[k]);
    *clipped += acc[k].clipped;
  }
}

//One probe of channel ch on its current range, offset PWM and coupling (nothing of the front end is written), then the
//UI's sample rate, divider, time base, sample counts and trigger mode back
static void probe_run(uint32 ch, PCHANNELSETTINGS s)
{
  acal_cm_meas m;
  acal_corr    kg, kf;
  uint32       tm  = scopesettings.triggermode;
  uint32       sm  = scopesettings.samplemode;
  uint32       sc  = scopesettings.samplecount;
  uint32       ns  = scopesettings.nofsamples;
  uint32       div = fpga_divider;
  uint8        who = fpga_trace_who;

  fpga_trace_who = FPGA_WHO_PROBE;

  probe_ch.adc1command = s->adc1command;
  probe_ch.adc2command = s->adc2command;
  probe_ch.tracebuffer = probe_buf;

  //Trigger circuit off, FPGA auto mode (no lockup in NORMAL or SINGLE), the calibration's 500 samples per core
  scopesettings.samplemode  = 0;
  scopesettings.triggermode = 0;
  fpga_set_trigger_mode();
  scopesettings.samplecount = 2 * ACAL_PROBE_SAMPLES;
  scopesettings.nofsamples  = ACAL_PROBE_SAMPLES;

  memset(&m, 0, sizeof(m));
  probe_part(ACAL_PROBE_RATE_GATED, ACAL_PROBE_CONVS_GATED, m.mean16[0], m.rms16[0], &m.clipped);
  probe_part(ACAL_PROBE_RATE_FULL,  ACAL_PROBE_CONVS_FULL,  m.mean16[1], m.rms16[1], &m.clipped);
  probe_part(ACAL_PROBE_RATE_GATED, ACAL_PROBE_CONVS_GATED, m.mean16[2], m.rms16[2], &m.clipped);

  scopesettings.samplecount = sc;
  scopesettings.nofsamples  = ns;
  scopesettings.samplemode  = (uint8)sm;
  scopesettings.triggermode = (uint8)tm;

  //The UI's rate; a raw divider a host wrote (SET_TIMEBASE raw) is not in the rate table: written back as it was
  fpga_set_sample_rate(scopesettings.samplerate);

  if(fpga_divider != div)
  {
    fpga_write_cmd(0x0D);
    fpga_write_int(div);
    fpga_divider = div;
  }

  fpga_set_time_base(scopesettings.timeperdiv);
  fpga_set_trigger_mode();
  fpga_trace_who = who;

  //The channel's v3 correction in each class (open loop: without v4), then the check and the update
  acal_correction(&adc_cal[ch], s->dc_calibration_offset, s->samplevoltperdiv, sample_rate_settings[ACAL_PROBE_RATE_GATED],
                  s->dcoffset, s->dc_shift_value, s->adc1compensation, s->adc2compensation, &kg);
  acal_correction(&adc_cal[ch], s->dc_calibration_offset, s->samplevoltperdiv, 0, s->dcoffset, s->dc_shift_value,
                  s->adc1compensation, s->adc2compensation, &kf);
  acal_cm_probe(&acal_cms[ch], &m, &kg, &kf, timer0ticks, 0);
}
#endif

void acal_live_service(int allowed)
{
#if PC_CAL_CLASS_PROBE
  PCHANNELSETTINGS s;
  acal_acc acc;
  uint32   now = timer0ticks, i, ch;

  //Nothing in flight: the UI read and drew its last conversion (display_data_done) and arms the next one in this pass
  if(!allowed || !adc_cal_loaded || !scopesettings.runstate || !scopesettings.display_data_done || touchstate ||
     scopesettings.long_mode || scopesettings.waveviewmode)
    return;

  for(i=0;i<2;i++)
  {
    ch = probe_next ^ i;

    if((int32)(now - probe_due[ch]) < 0)
      continue;

    s = ch ? &scopesettings.channel2 : &scopesettings.channel1;

    if(!s->enable || !acal_bound(&adc_cal[ch], s->dc_calibration_offset))
    {
      probe_due[ch] = now + ACAL_PROBE_RETRY_MS;
      continue;
    }

    //The channel's last record quiet: RMS about its mean <= 2 codes (a DC level is fine, a signal is not)
    memset(&acc, 0, sizeof(acc));
    acal_acc_add(&acc, s->tracebuffer, scopesettings.samplecount, 1);

    if(acal_rms16(&acc) > ACAL_CM_MAX_RMS16)
    {
      probe_due[ch] = now + ACAL_PROBE_RETRY_MS;
      continue;
    }

    probe_run(ch, s);
    probe_due[ch] = timer0ticks + ACAL_PROBE_EVERY_MS;
    probe_next    = (uint8)(ch ^ 1);
    break;
  }
#else
  (void)allowed;
#endif
}
