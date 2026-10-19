//----------------------------------------------------------------------------------------------------------------------------------
// tb_view.h - the record behind the scope trace (fw 0.2.64, docs/plans/task-queue.md "ACQ: sample rate vs time/div coupling").
//
// The trace shows a window of the record: 50 px per division, trace columns 3..726 (scope_display_trace_data), the
// trigger point at column `pos` (scopesettings.triggerhorizontalposition) and the first sample shown
// F = trig - 1 - ceil(pos / xpps), xpps = 50 x (1 / time per div) / sample rate pixels per sample (1.00o5's formula).
// A time/div is viable for a sample rate when the record (memory depth x sample period) spans the whole trace area and
// a division holds at least one sample. The functions below derive that from the real record length; they replace
// 1.00o5's hand-written viable_time_per_div table (host_tests/test_tb_view.c compares the two).
//
// Units: fs = samples per second (sample_rate[]), fpd = divisions per second (frequency_per_div[], 1 / time per div),
// total = samples in the record, trig = the record's trigger sample. Pure C, no globals.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef TB_VIEW_H
#define TB_VIEW_H

#include "types.h"

#define TBV_X_FIRST              3       //first trace column (disp_xstart)
#define TBV_X_END                727     //one past the last trace column (disp_xend)
#define TBV_SPAN_PX              (TBV_X_END - TBV_X_FIRST)       //724: first trace column to the last pixel's end
                                         //(the drawing interpolates the last pixel towards column 727)
#define TBV_PX_PER_DIV           50
#define TBV_MIN_SAMPLES_PER_DIV  1       //200 MSa/s at 5 ns/div is exactly one

//Record-position slider under the grid: frame x 2..727, y 452..471 (scope_draw_grid); the window moves on a track
//x 4..723 and is drawn y 455..468. Its hit area is 44 px tall (TBV_SLIDER_HIT_Y..479) over the frame's width.
#define TBV_SLIDER_FRAME_X       2
#define TBV_SLIDER_FRAME_W       726
#define TBV_SLIDER_FRAME_Y       452
#define TBV_SLIDER_FRAME_H       20
#define TBV_SLIDER_X             4
#define TBV_SLIDER_W             720
#define TBV_SLIDER_MIN_W         8
#define TBV_SLIDER_HIT_Y         436

//1 when time/div `fpd` is viable at sample rate `fs` for a record of `total` samples
int tbv_viable(uint32 fs, uint32 fpd, uint32 total);

//The viable index of fpd[first..last] nearest to `current` (a tie to the lower index, the slower time/div); `current`
//itself when viable; -1 when none is
int32 tbv_snap(uint32 fs, const uint32 *fpd, int32 first, int32 last, int32 current, uint32 total);

//Trigger point columns that keep the trace window inside the record: lo..hi (lo == hi when the record is shorter than
//the trace area: the record starts at the left edge)
void tbv_pos_limits(uint32 fs, uint32 fpd, uint32 total, uint32 trig, int32 *lo, int32 *hi);

//pos limited to tbv_pos_limits
int32 tbv_clamp_pos(int32 pos, uint32 fs, uint32 fpd, uint32 total, uint32 trig);

//The time/div index and trigger point column that a time/div change writes (fw 0.2.68). A manual set (the ACQ menu's
//box) and the snap after a rate, memory or Filter change go through tbv_set, so both leave the same state.
typedef struct
{
  int32 tpd;                             //time/div index into fpd[]
  int32 pos;                             //trigger point column (scopesettings.triggerhorizontalposition)
} tbv_tb;

//Time/div index t chosen: the trigger point kept inside the record
void tbv_set(tbv_tb *s, int32 t, uint32 fs, const uint32 *fpd, uint32 total, uint32 trig);

//The snap: when s->tpd is not viable, tbv_set with the nearest viable index of fpd[first..last] and return 1;
//otherwise (or with none viable) only the trigger point is kept inside the record, return 0
int tbv_fit(tbv_tb *s, uint32 fs, const uint32 *fpd, int32 first, int32 last, uint32 total, uint32 trig);

//The drawn window for trigger sample `trig` and trigger point `pos`: the first sample shown (kept inside the record)
//and the end column (exclusive; TBV_X_END unless the record ends inside the trace area)
void tbv_window(int32 trig, int32 pos, uint32 fs, uint32 fpd, uint32 total, int32 *first, int32 *xend);

//The slider's window for first sample `first`: x on the track and width
void tbv_slider(int32 first, uint32 fs, uint32 fpd, uint32 total, int32 *x, int32 *w);

//Trigger point change for a finger move of dx px along the slider: the window follows the finger, so dragging right
//shows later samples (the trace moves left)
int32 tbv_slider_move(int32 dx, uint32 fs, uint32 fpd, uint32 total);

#endif // TB_VIEW_H
