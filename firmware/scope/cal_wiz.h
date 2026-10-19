//----------------------------------------------------------------------------------------------------------------------------------
// cal_wiz.h - the calibration wizard's steps (firmware/README.md section 6.4): a state machine with no globals and no
// hardware, host-buildable (host_tests/test_cal_wizard.c). The target glue (cal_wiz_ui.c) draws the state, turns
// button touches into events, runs the actions the machine asks for and feeds their results back.
//
//   Step 1 Zero     probes and USB unplugged: zeros, offset slopes and PWM centres of both channels (adc_cal.h)
//   Step 2 Gain     one channel at a time, probe tip on the calibration post (AL3 bitstreams only)
//   Step 3 Summary  Done commits what changed, Cancel (at any step) commits nothing
//
// Nothing is applied unless both channels pass the zero step; a skipped step keeps the earlier values.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef CAL_WIZ_H
#define CAL_WIZ_H

#include <stdint.h>
#include "adc_cal.h"

//States
#define CW_ZERO_PROMPT     0      //Start, Skip (prior), Cancel
#define CW_ZERO_RUN        1      //action ZERO issued: waiting for cw_zero_result
#define CW_ZERO_RESULT     2      //Next (both passed), Retry, Skip (prior), Cancel
#define CW_GAIN_NA         3      //gain step not possible (no generator on the post): Next, Cancel
#define CW_GAIN_PROMPT     4      //probe of channel ch on the post: Measure, Skip, Cancel
#define CW_GAIN_LEVEL_RUN  5      //action GAIN_LEVEL issued (auto2: the silent CH2 check): waiting for cw_level_result
#define CW_GAIN_MEAS_RUN   6      //action GAIN_MEASURE issued: waiting for cw_gain_result
#define CW_GAIN_NOSIG      7      //no signal on channel ch: Retry, Skip, Cancel
#define CW_GAIN_FAIL       8      //measurement on channel ch failed (gain_rc): Retry, Skip, Cancel
#define CW_GAIN_RESULT     9      //CH2 measured: Next, Cancel
#define CW_SUMMARY        10      //Done, Cancel
#define CW_END            11      //left (COMMIT or DISCARD issued)

//Events (buttons)
#define CW_EV_START        0
#define CW_EV_MEASURE      1
#define CW_EV_RETRY        2
#define CW_EV_SKIP         3
#define CW_EV_NEXT         4
#define CW_EV_CANCEL       5
#define CW_EV_DONE         6
#define CW_EVENTS          7

//Actions the caller runs (cw_take)
#define CW_ACT_NONE          0
#define CW_ACT_ZERO          1    //zero step on both channels, then cw_zero_result
#define CW_ACT_PREPARE       2    //working copy for steps 2-3: the new zeros (zero_new, gains carried over) or the old block
#define CW_ACT_GAIN_LEVEL    3    //level check of channel action_ch at the post, then cw_level_result
#define CW_ACT_GAIN_MEASURE  4    //gain measurement of channel action_ch, then cw_gain_result
#define CW_ACT_COMMIT        5    //Done: commit_zero / commit_gain say what to write (both 0: nothing changed)
#define CW_ACT_DISCARD       6    //Cancel: nothing is written

//Per-channel outcomes
#define CW_Z_NONE          0      //zero step not run (or retried)
#define CW_Z_PASS          1
#define CW_Z_FAIL          2

#define CW_G_NONE          0      //gain not measured in this run
#define CW_G_PASS          1
#define CW_G_FAIL          2      //last measurement failed (gain_rc), shown until Retry or Skip
#define CW_G_SKIP          3      //skipped: the working copy's gains stay (carried over, flag ACAL_GAIN_KEPT, or none)

typedef struct
{
  uint8_t  state;
  uint8_t  ch;                    //gain step: channel in focus
  uint8_t  auto2;                 //the CH2 level check after CH1 passed runs silently (no signal -> CH2 prompt)
  uint8_t  prior;                 //a bound v3 block exists for both channels: Skip offered in step 1 (so step 2 always
                                  //has zeros to measure against)
  uint8_t  al3;                   //bitstream with the generator on 0x50-0x52 (else step 2 is CW_GAIN_NA)
  uint8_t  zero_new;              //step 1 result taken (Next)
  uint8_t  action;                //CW_ACT_*, taken by cw_take
  uint8_t  action_ch;
  uint8_t  commit_zero;           //with CW_ACT_COMMIT: zeros changed (sectors 708 and 709)
  uint8_t  commit_gain;           //with CW_ACT_COMMIT: only gains changed (sector 708)
  uint8_t  zero_st[2];            //CW_Z_*
  uint8_t  gain_st[2];            //CW_G_*
  int16_t  zero_rc[2];            //ACAL_* of the last zero step
  int16_t  gain_rc[2];            //ACAL_* of the last gain measurement
  uint16_t match16[2];            //zero step: worst |drift A - drift B| over ranges and classes, 1/16 code
} cal_wiz;

void cw_init(cal_wiz *w, int prior, int al3);

//Button events: 1 when taken, 0 when the button is not offered in this state (stray touches, running states)
int  cw_event(cal_wiz *w, int ev);
int  cw_button(const cal_wiz *w, int ev);            //1 when the button for ev is shown
int  cw_step(const cal_wiz *w);                      //1..3 for the header "Step n of 3"

//The pending action (CW_ACT_NONE when there is none), cleared; its channel in *ch
int  cw_take(cal_wiz *w, uint8_t *ch);

//Results of the actions (ignored in any other state)
void cw_zero_result(cal_wiz *w, const int rc[2], const uint32_t match16[2]);
void cw_level_result(cal_wiz *w, int32_t span16);    //gated span at ACAL_GAIN_PROBE_RANGE, 1/16 code
void cw_gain_result(cal_wiz *w, int rc);

//Level check: 1 when the span (probe range) predicts at least ACAL_GAIN_MIN_LEVER16 on the range the measurement
//will use (acal_gain_range), i.e. a x1 or x10 probe is on the post
int  cw_level_ok(int32_t span16);

//Numbers of the texts, tenths
uint32_t cw_match_tenths(uint32_t match16);          //code, at least 1 (0.1)
uint32_t cw_gain_tenths(const acal_channel *c);      //percent: full-class |G_A / G_B - 1|

//Texts (no libc): each writes a NUL-terminated string at p and returns a pointer to the NUL
char *cw_fmt_tenths(char *p, uint32_t v);                                          //13 -> "1.3"
char *cw_progress(char *p, uint32_t ch, uint32_t range);                           //"CH1 range 3 of 6"
char *cw_zero_line(const cal_wiz *w, uint32_t ch, char *p);                        //step 1 result line
char *cw_gain_line(const cal_wiz *w, uint32_t ch, const acal_channel *c, char *p); //step 2 line ("" when none)
char *cw_summary_line(const cal_wiz *w, uint32_t ch, const acal_channel *c, char *p);

#endif /* CAL_WIZ_H */
