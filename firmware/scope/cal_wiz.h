//----------------------------------------------------------------------------------------------------------------------------------
// cal_wiz.h - the calibration wizard's steps (fw 0.2.59, docs/plans/ux-makeover.md section 6a, firmware/README.md section
// 6.4): a state machine with no globals and no hardware, host-buildable (host_tests/test_cal_wizard.c). The target glue
// (cal_wiz_ui.c) draws the state, turns button touches into events, runs the actions the machine asks for and feeds their
// results back.
//
// Four screens, one modal flow, the same two buttons on every screen (Cancel left, the action right):
//
//   1 Zero level  nothing connected (probes, post, USB): zeros, offset slopes and PWM centres of both channels (adc_cal.h)
//   2 CH1 gain    CH1 probe at 1X on the calibration post (bitstreams with the generator; else one "not possible" screen)
//   3 CH2 gain    the same for CH2
//   4 Done        the summary per channel; Save calibration writes, Cancel (on any screen) writes nothing
//
// Calibrate runs the screen's measurement and turns into Continue once it passed; a failure keeps Calibrate (retry).
// There is no Skip: a channel that cannot be measured is reported and the user decides between retrying and Cancel.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef CAL_WIZ_H
#define CAL_WIZ_H

#include <stdint.h>
#include "adc_cal.h"

//States
#define CW_ZERO            0      //screen 1: Calibrate, or Continue after both channels passed
#define CW_ZERO_RUN        1      //action ZERO issued: waiting for cw_zero_result
#define CW_GAIN            2      //screens 2-3 (channel ch): Calibrate, or Continue after it passed
#define CW_GAIN_LEVEL_RUN  3      //action GAIN_LEVEL issued: waiting for cw_level_result
#define CW_GAIN_MEAS_RUN   4      //action GAIN_MEASURE issued: waiting for cw_gain_result
#define CW_DONE            6      //screen 4: Save calibration
#define CW_END             7      //left (COMMIT or DISCARD issued)

//Events (buttons). The left button is always Cancel; the right one is the screen's action (cw_action_button)
#define CW_EV_CANCEL       0
#define CW_EV_CALIBRATE    1
#define CW_EV_CONTINUE     2
#define CW_EV_SAVE         3
#define CW_EVENTS          4
#define CW_EV_MEASURING    -2     //cw_action_button while a measurement runs: "Measuring...", greyed, not a button

//Actions the caller runs (cw_take)
#define CW_ACT_NONE          0
#define CW_ACT_ZERO          1    //zero step on both channels, then cw_zero_result
#define CW_ACT_PREPARE       2    //working copy for screens 2-4: the new zeros with the earlier gains carried over
#define CW_ACT_GAIN_LEVEL    3    //level check of channel action_ch at the post, then cw_level_result
#define CW_ACT_GAIN_MEASURE  4    //gain measurement of channel action_ch, then cw_gain_result
#define CW_ACT_COMMIT        5    //Save calibration: zeros and gains (sectors 708 and 709)
#define CW_ACT_DISCARD       6    //Cancel: nothing is written

//Per-channel outcomes
#define CW_Z_NONE          0      //zero step not run (or running again)
#define CW_Z_PASS          1
#define CW_Z_FAIL          2      //zero_rc says why

#define CW_G_NONE          0      //gain not measured in this run
#define CW_G_PASS          1
#define CW_G_NOSIG         2      //level check: no probe on the post
#define CW_G_FAIL          3      //measurement failed (gain_rc)

//Capture stage training (fw 0.2.79, FPGA 0x153A, cap_stage.h): the first thing of each channel's zero step
#define CW_C_NONE          0      //not run (no capture stages in this bitstream, or not reached)
#define CW_C_DONE          1      //trained: cap_v holds the stages
#define CW_C_FAIL          2      //a core has no clean stage (cap_bad): the channel fails with ACAL_E_READOUT

//A measurement stopped by Cancel (the glue's result code; never an ACAL_* code)
#define CW_RC_CANCELLED    (-1)

typedef struct
{
  uint8_t  state;
  uint8_t  ch;                    //screens 2-3: channel in focus
  uint8_t  action;                //CW_ACT_*, taken by cw_take
  uint8_t  action_ch;
  uint8_t  zero_st[2];            //CW_Z_*
  uint8_t  gain_st[2];            //CW_G_*
  int16_t  zero_rc[2];            //ACAL_* of the last zero step
  int16_t  gain_rc[2];            //ACAL_* of the last gain measurement
  uint16_t match16[2];            //zero step: worst |drift A - drift B| over ranges and classes, 1/16 code
  uint8_t  zero_det[2];           //cw_zero_detail given for this run
  uint8_t  zero_r[2];             //zero step failure: the range (0..5), class and core of the failing value,
  uint8_t  zero_cls[2];           //the value (acal_fail) and the RMS inside the conversions (cw_zero_detail)
  uint8_t  zero_core[2];
  int32_t  zero_val[2];
  uint32_t zero_within16[2];
  acal_fail zero_f[2];            //fw 0.2.67: the whole detail (point, PWM, raw per-core numbers) for the note line
  uint8_t  cap_st[2];             //fw 0.2.79: FPGA 0x153A capture stage training of the channel's cores (CW_C_*)
  uint8_t  cap_bad[2];            //cores without a clean stage (cap_stage.h order: bit 0 CH1 A .. bit 3 CH2 B)
  uint16_t cap_v[2];              //the 0x63 value after the channel's training
} cal_wiz;

void cw_init(cal_wiz *w);

//Button events: 1 when taken, 0 when the button is not offered in this state (stray touches)
int  cw_event(cal_wiz *w, int ev);
int  cw_button(const cal_wiz *w, int ev);            //1 when ev is offered now
int  cw_action_button(const cal_wiz *w);             //the right button: CW_EV_CALIBRATE / CONTINUE / SAVE, or
                                                     //CW_EV_MEASURING while a measurement runs, -1 after the end
int  cw_screen(const cal_wiz *w);                    //1..4 (0 after the end)
int  cw_running(const cal_wiz *w);                   //1 while a measurement action is outstanding

//The pending action (CW_ACT_NONE when there is none), cleared; its channel in *ch
int  cw_take(cal_wiz *w, uint8_t *ch);

//Results of the actions (ignored in any other state, e.g. after a Cancel during the measurement)
void cw_zero_result(cal_wiz *w, const int rc[2], const uint32_t match16[2]);
//Where a channel's zero step failed (range 0..5, acal_range_check's acal_fail), given before cw_zero_result; the noise
//and drift result lines name the range, rate and core with the values (fw 0.2.60)
void cw_zero_detail(cal_wiz *w, uint32_t ch, uint32_t range, const acal_fail *f);
//fw 0.2.79: the capture stage training of channel ch in this zero step (CW_C_*, the 0x63 value, the cores without a clean
//stage), given before cw_zero_detail; a passed channel's note then names its stages ("Capture stages: CH1 A4 B4")
void cw_cap_result(cal_wiz *w, uint32_t ch, int st, uint16_t v, uint8_t bad);
void cw_level_result(cal_wiz *w, int32_t span16);    //gated span at ACAL_GAIN_PROBE_RANGE, 1/16 code
void cw_gain_result(cal_wiz *w, int rc);

//Level check: 1 when the span (probe range) predicts at least ACAL_GAIN_MIN_LEVER16 on the range the measurement
//will use (acal_gain_range), i.e. a x1 or x10 probe is on the post
int  cw_level_ok(int32_t span16);

//Numbers of the texts, tenths
uint32_t cw_match_tenths(uint32_t match16);          //code, at least 1 (0.1)
uint32_t cw_gain_tenths(const acal_channel *c);      //percent: full-class |G_A / G_B - 1| (before the correction)

//Texts (no libc): each writes a NUL-terminated string at p and returns a pointer to the NUL. Lines are laid out for the
//16 px font in the wizard's 600 px text column (the longest is under 480 px).
const char *cw_title(const cal_wiz *w);                                            //"Zero level", "CH1 gain", ...
const char *cw_instruction(const cal_wiz *w, int line);                            //line 0 / 1, "" when none
const char *cw_reason_text(int rc);                                                //plain words for an ACAL_E_*
char *cw_fmt_tenths(char *p, uint32_t v);                                          //13 -> "1.3"
char *cw_progress(char *p, uint32_t ch, uint32_t range);                           //"CH1: measuring range 3 of 6"
#define CW_PR_TRAIN        0xFFu  //cw_progress range: the 0x153A capture stage training (fw 0.2.81), "CH1: training
                                  //capture stages..."
char *cw_zero_line(const cal_wiz *w, uint32_t ch, char *p);                        //screen 1 result ("" when none)
//fw 0.2.67: the small line under a failed channel's result: where (range, rate, point), the PWM and the raw per-core
//means with their extremes and clipped samples, e.g. "Range 1, 200 MSa/s, zero point, PWM 812: A 130.1 (126..135),
//B 125.9 (0..255, 12 clip)"; fw 0.2.79: "Capture stages: CH1 A4 B4" under a passed channel that trained them,
//"Capture stages: no clean stage for CH2 A" under one whose training failed. "" otherwise
char *cw_zero_note(const cal_wiz *w, uint32_t ch, char *p);
char *cw_gain_line(const cal_wiz *w, char *p);                                     //screens 2-3 result ("" when none)
char *cw_done_line(const cal_wiz *w, uint32_t ch, const acal_channel *c, char *p); //screen 4, one per channel

#endif /* CAL_WIZ_H */
