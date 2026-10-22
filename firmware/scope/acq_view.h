//----------------------------------------------------------------------------------------------------------------------------------
// acq_view.h - the scope's sample rate, time/div and record kept consistent (fw 0.2.64; the maths in tb_view.c).
//
// One rule for every writer (ACQ menu, plot tap zoom, the host's SET_TIMEBASE / SET_TRIGGER total, memory depth and Filter
// changes): in short mode the time/div must be viable for the sample rate and the record length (tb_view.h), and the
// trigger point keeps the trace window inside the record. A time/div that is not viable is snapped to the nearest
// viable one and the scope says so for a few seconds at the top middle of the plot ("Time/div set to 1us for this
// rate"). Roll (long) mode has its own time bases and is left alone.
//
// fw 0.2.76 (docs/ui-guidelines.md section 2, "Time/div drives the sample rate"): while a new record can be taken (the
// rate is free), the tap zoom and the ACQ menu's time/div boxes set the time/div and the ladder's rate for it
// (tb_view.h tbv_ladder_rate); a rate chosen by hand holds until the next zoom step or time/div box. While stopped the
// record's rate is kept and the time/div steps through what the record can show.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef ACQ_VIEW_H
#define ACQ_VIEW_H

#include "types.h"

//Short-mode time/div indices (time_div_texts: 11 = 200 ms/div .. 34 = 5 ns/div)
#define ACQ_TB_FIRST   11
#define ACQ_TB_LAST    34

//How long the snap notice shows once drawn
#define ACQ_NOTICE_MS  4000

//Short-mode sample rates: sample_rate[0..ACQ_RATES - 1], 200 MSa/s .. 500 Sa/s
#define ACQ_RATES      18

//1 when the sample rate may change with the time/div (fw 0.2.76): not in the waveform viewer, and running or in Single
//(the ACQ menu's rule for its rate boxes). Stopped, the record on screen keeps its rate.
int acq_view_rate_free(void);

//1 when time/div index `timeperdiv` can be chosen now (short mode): with the rate free, when the ladder has a rate
//for it; stopped, when it is viable for the record's rate (and, with a rate pending, for that rate too)
int acq_view_tb_ok(uint32 timeperdiv);

//fw-stoprate (owner 2026-10-22): the ACQ menu's Sample Rate grid takes taps in STOP, as bench scopes do. Stopped, the
//tapped rate becomes the pending setting: scopesettings.samplerate stays the rate the record on screen was captured
//at (trace, slider, cursors, measurements and the stopped zoom read it, unchanged), nothing is written to the FPGA,
//and the next UI arm (scope_acquire_trace_data after RUN) takes it as if it had been tapped while running
//(acq_view_rate_arm). The grid's green cell and the top bar's ACQ chip show the setting (acq_view_rate_setting).
//Any write of the FPGA's rate (fpga_set_sample_rate: the host's SET_TIMEBASE, AUTO SET, a settings load, a time/div
//tap that moves the rate) drops a pending rate: the last setting written wins.
#define ACQ_RATE_NONE  0xFF

//1 when the Sample Rate grid takes a tap: not in roll (the rate is off the grid there)
int acq_view_rate_live(void);

//The rate setting: the pending one in STOP, else the record's / FPGA's (scopesettings.samplerate)
uint32 acq_view_rate_setting(void);

//A tap on rate r (< ACQ_RATES) in the grid: with the rate free (running or Single) scopesettings.samplerate = r and
//ACQ_SET_RATE (the caller writes the FPGA's rate and fits the time/div); stopped, r is stored as pending (none when it
//is the record's rate) and 0 is returned (nothing to write). Nothing changes when the grid is not live.
int acq_view_rate_tap(uint32 r);

//At a UI arm: a pending rate becomes scopesettings.samplerate (returns 1: the caller writes the FPGA's rate and runs
//acq_view_fit(1), the rules of a rate tapped while running); 0 with none pending or in roll
int acq_view_rate_arm(void);

//Forget a pending rate (fpga_set_sample_rate: a new rate written wins)
void acq_view_rate_drop(void);

//Snap the time/div to the nearest viable one when it is not viable (and say so when `notice`), then keep the trigger
//point inside the record. Returns 1 when the time/div changed. Does nothing in long mode.
int acq_view_fit(int notice);

//Set time/div index `timeperdiv` by hand (the ACQ menu's box, fw 0.2.68): the same path the snap takes; with the rate
//free (fw 0.2.76) the ladder's rate is set with it. Returns 0 and changes nothing when it cannot be chosen now
//(acq_view_tb_ok) or in long mode, ACQ_SET_TB when only the time/div changed, ACQ_SET_RATE when the rate changed too
//(the caller writes the FPGA's rate).
#define ACQ_SET_TB     1
#define ACQ_SET_RATE   2
int acq_view_set_tb(uint32 timeperdiv);

//One tap zoom step (fw 0.2.76): dir < 0 one time/div slower, > 0 faster. With `rate_free` the time/div and the
//ladder's rate for it (tbv_zoom_step over every short-mode rate); otherwise the next time/div the record's rate can show,
//and at the record's limit (where the ladder would go on) the notice "Stopped: press RUN to zoom further". Returns 0
//at an end (nothing changed), else ACQ_SET_TB / ACQ_SET_RATE as acq_view_set_tb. Short mode only.
int acq_view_zoom(int dir, int rate_free);

//fw 0.2.77, the Spectrum's span sets the rate (spec_ladder.h): the short-mode time/div to pair with sample_rate[r]: the
//one nearest the current time/div whose ladder rate is r, else the nearest viable at r (tbv_snap); -1 when none is
int32 acq_view_tb_for_rate(uint32 r);

//Keep the trigger point inside the record (short mode)
void acq_view_clamp_pos(void);

//The trigger point after a finger move of dx px along the record-position slider, from `start`
int32 acq_view_slider_pos(int32 start, int32 dx);

//fw 0.2.73, tap to jump: the trigger point position that centres the slider window on a touch-down at x, y outside it
//(slider_ov.h sov_jump; the position unchanged inside the window, above the row, or with no to-scale window)
int32 acq_view_slider_jump(int32 pos, int32 x, int32 y);

//The snap notice to draw now, or 0; the first call after a snap starts its ACQ_NOTICE_MS. Two slots (ui-type): the
//time/div's notice (slot 0) shows before the trigger's (slot 1, "Width set to 5us for this rate"), each for its time
const char *acq_view_notice(void);

//Queue a notice (head, mid and tail joined) in a slot: a new notice replaces its slot's, a time/div notice also drops a
//pending trigger notice
void acq_view_notice_queue(int slot, const char *head, const char *mid, const char *tail);

//fw 0.2.84: the ACQ menu's memory depth chips that can be chosen, bit i for (i + 1) x 3000 samples (i 0 .. 4): those
//within the record the bitstream holds (`max_total`) and, with the decimation Filter on, within the 16-bit layout's
//`filter_max` (one sample per word halves the memory). The menu draws its Memory column from this on opening and again
//on every Filter change, so the depths a filter hid come back at once when it goes Off (owner, fw 0.2.76). ui-sample:
//`filter_on` is "the record is 16-bit", the Filter on or Sample Peak (the same layout), and the Sample box redraws it too.
#define ACQ_MEM_CHIPS  5
uint32 acq_view_mem_chips(uint32 max_total, int filter_on, uint32 filter_max);

#endif // ACQ_VIEW_H
