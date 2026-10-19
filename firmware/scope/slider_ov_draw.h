//----------------------------------------------------------------------------------------------------------------------------------
// slider_ov_draw.h - drawing the slider row and its overview (fw 0.2.73; geometry in slider_ov.h). The caller has set the
// screen buffer; colours are 0x00RRGGBB.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef SLIDER_OV_DRAW_H
#define SLIDER_OV_DRAW_H

#include "types.h"
#include "slider_ov.h"

//The row black (y 452..471 over x 2..727) and the line at y 452
void sov_draw_row(void);

//A run of rows lo..hi (0..SOV_H - 1) at track column x: `full` when the column is inside the window wx, ww, else `dim`
void sov_draw_run(int32 x, int32 lo, int32 hi, uint32 full, uint32 dim, int32 wx, int32 ww);

//The window: its left and right edge lines over the track's height, drawn last
void sov_draw_window(int32 wx, int32 ww);

#endif
