//----------------------------------------------------------------------------------------------------------------------------------
// acal_live.h - calibration v4 on the scope (docs/plans/interleave-tracking.md, protocol row 53): the per-channel A - B
// tracker and full-class common mode (acal_track.h), their hooks in the readout (fpga_control.c) and the class probe
// scheduler (main loop). The maths is in acal_track.c (host-tested); this file only holds the state and drives the FPGA.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef ACAL_LIVE_H
#define ACAL_LIVE_H

#include "types.h"
#include "variables.h"
#include "acal_track.h"

//Class probe scheduler: 1 runs it from the main loop (pc_glue_ui_service), 0 builds without it (cm stays 0; the A - B
//tracker is not affected)
#ifndef PC_CAL_CLASS_PROBE
#define PC_CAL_CLASS_PROBE    1
#endif

#define ACAL_PROBE_FIRST_MS   5000    //first probe per channel this long after boot / a reset
#define ACAL_PROBE_EVERY_MS   30000   //then at most this often per channel
#define ACAL_PROBE_RETRY_MS   1000    //recheck of a due probe whose gate failed (input not quiet, view, host)

extern acal_track acal_trk[2];        //per channel (CH1, CH2), zero = nothing tracked
extern acal_cm    acal_cms[2];

//Boot (calibration block loaded) and every calibration change: tracker and cm back to zero, probes rescheduled
void acal_live_reset(void);

//fpga_channel_correction: v4 onto the channel's correction (CH1 / CH2 only; others untouched)
void acal_live_apply(PCHANNELSETTINGS s, acal_corr *c);

//fpga_prepare_correction: remember what this readout applies (class, tracker d / e), for acal_live_record
void acal_live_prepared(PCHANNELSETTINGS s, const acal_corr *c);

//End of fpga_read_sample_data: the trace buffer just read is offered to the tracker (CH1 / CH2, 8-bit full class only;
//wide = the readout came from the 16-bit filter path)
void acal_live_record(PCHANNELSETTINGS s, uint32 wide);

//Main loop (outside menus): runs a due class probe when the caller's part of the gate allows it (allowed: scope or
//spectrum view, no host capture armed / triggered, no READ_RAW hold)
void acal_live_service(int allowed);

#endif /* ACAL_LIVE_H */
