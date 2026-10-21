//----------------------------------------------------------------------------------------------------------------------------------
// acal_live.c - calibration v4 on the scope (acal_live.h, docs/plans/interleave-tracking.md, protocol row 53)
//
// A - B tracker: fpga_channel_correction folds the tracked d / e into every CH1 / CH2 correction (acal_v4_apply: a no-op
// unless v3 is bound and the class is full); fpga_prepare_correction remembers what a readout applied; at the end of
// fpga_read_sample_data an 8-bit full-class record is offered to acal_track_record. 16-bit readouts, the gated class and
// the calibration's scratch channel (calibrationsettings) never feed it nor get it. The class probe (fw 0.2.50-0.2.104:
// the full-rate class's common mode against the gated class) went with the gated class in fw 0.2.105 (FPGA 0x153D runs
// the ADCs at full rate at every rate, firmware/AGENTS.md section 83).
//----------------------------------------------------------------------------------------------------------------------------------


#include "types.h"
#include "variables.h"
#include "timer.h"
#include "acal_live.h"

acal_track acal_trk[2];

//What the last readout of a channel applied (acal_live_prepared)
static uint8  live_armed[2];            //v3 bound and full class: the record may feed the tracker
static int32  live_d[2];
static int32  live_e[2];

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
    live_armed[ch] = 0;
    live_d[ch]     = 0;
    live_e[ch]     = 0;
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
    acal_track_apply(&acal_trk[ch], c);
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
