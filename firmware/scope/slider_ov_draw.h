//----------------------------------------------------------------------------------------------------------------------------------
// slider_ov_draw.h - drawing the slider row and its overview (fw 0.2.73; geometry in slider_ov.h). The caller has set the
// screen buffer; colours are 0x00RRGGBB.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef SLIDER_OV_DRAW_H
#define SLIDER_OV_DRAW_H

#include "types.h"
#include "slider_ov.h"

//The row black (y 452..471 over x 2..727)
void sov_draw_row(void);

//A run of rows lo..hi (0..SOV_H - 1) at track column x: `full` when the column is inside the window wx, ww, else `dim`
void sov_draw_run(int32 x, int32 lo, int32 hi, uint32 full, uint32 dim, int32 wx, int32 ww);

//fw 0.2.92: a curve of the Frequency response overview (slider_ov.h sov_curve): valid points joined as on the chart, a
//flagged point one pixel in `flag` with no line to it
void sov_draw_curve(const sov_curve *cv, uint32 flag, int32 wx, int32 ww);

//The overview's curves (decision 8): the phase first, the magnitude over it, so where they overlap the magnitude is seen.
//Either may be 0 (Magnitude only, Phase only)
void sov_draw_curves(const sov_curve *mag, const sov_curve *phase, uint32 flag, int32 wx, int32 ww);

//The window: its 1 px black margin and its box of 1 px white lines, drawn last (over the overview)
void sov_draw_window(int32 wx, int32 ww);

#endif
