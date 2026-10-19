//----------------------------------------------------------------------------------------------------------------------------------
// acq_view.h - the scope's sample rate, time/div and record kept consistent (fw 0.2.64; the maths in tb_view.c).
//
// One rule for every writer (ACQ menu, plot tap zoom, the host's SET_TIMEBASE / SET_TRIGGER total, memory depth and Filter
// changes): in short mode the time/div must be viable for the sample rate and the record length (tb_view.h), and the
// trigger point keeps the trace window inside the record. A time/div that is not viable is snapped to the nearest
// viable one and the scope says so for a few seconds at the top middle of the plot ("Time/div set to 1us for this
// rate"). Roll (long) mode has its own time bases and is left alone.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef ACQ_VIEW_H
#define ACQ_VIEW_H

#include "types.h"

//Short-mode time/div indices (time_div_texts: 11 = 200 ms/div .. 34 = 5 ns/div)
#define ACQ_TB_FIRST   11
#define ACQ_TB_LAST    34

//How long the snap notice shows once drawn
#define ACQ_NOTICE_MS  4000

//1 when time/div index `timeperdiv` can be chosen now (short mode, viable for the current rate and record)
int acq_view_tb_ok(uint32 timeperdiv);

//Snap the time/div to the nearest viable one when it is not viable (and say so when `notice`), then keep the trigger
//point inside the record. Returns 1 when the time/div changed. Does nothing in long mode.
int acq_view_fit(int notice);

//Set time/div index `timeperdiv` by hand (the ACQ menu's box, fw 0.2.68): the same path the snap takes. Returns 0 and
//changes nothing when it is not viable now (acq_view_tb_ok) or in long mode.
int acq_view_set_tb(uint32 timeperdiv);

//Keep the trigger point inside the record (short mode)
void acq_view_clamp_pos(void);

//The trigger point after a finger move of dx px along the record-position slider, from `start`
int32 acq_view_slider_pos(int32 start, int32 dx);

//fw 0.2.73, tap to jump: the trigger point position that centres the slider window on a touch-down at x, y outside it
//(slider_ov.h sov_jump; the position unchanged inside the window, above the row, or with no to-scale window)
int32 acq_view_slider_jump(int32 pos, int32 x, int32 y);

//The snap notice to draw now, or 0; the first call after a snap starts its ACQ_NOTICE_MS
const char *acq_view_notice(void);

#endif // ACQ_VIEW_H
