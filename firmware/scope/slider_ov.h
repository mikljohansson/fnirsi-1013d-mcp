//----------------------------------------------------------------------------------------------------------------------------------
// slider_ov.h - the overview in the slider track under the plot (fw 0.2.72, docs/plans/slider-overview.md).
//
// The slider row of the Scope, the Spectrum and the Frequency response (owner 2026-10-19 21:36, section 10 F item 6):
//   y 452          one line over x 2..727 that parts the track from the plot above it (the old frame's top; its left,
//                  right and bottom lines are gone)
//   y 453..471     the track: 19 rows over x 2..727, 726 columns; the overview uses all of it
//   the window     two vertical lines, 1 px each, at its left and right edge over the track's full height; the overview
//                  between them in the full colour, outside them in the dimmed colour
// Column x stands for the part of the whole (record, axis, 1 Hz .. 10 MHz) that maps to track pixel x, the same mapping
// as the window's (tbv_slider, bu_slider_x, the Spectrum's window). Row 0 is the top row (y 453).
//
// Pure C (host_tests/test_slider_ov.c); the drawing helpers are in slider_ov_draw.c.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef SLIDER_OV_H
#define SLIDER_OV_H

#include "types.h"

#define SOV_LINE_Y      452       //the line between the plot and the track
#define SOV_X           2         //first track column
#define SOV_W           726       //track columns, x 2..727
#define SOV_Y           453       //top track row
#define SOV_H           19        //track rows, y 453..471

//The dimmed colours (half intensity, exact in RGB565): CH1 yellow, CH2 cyan, the Frequency response phase pink
#define SOV_CH1_DIM     0x00808000
#define SOV_CH2_DIM     0x00008080
#define SOV_PHASE_DIM   0x00804080

//The items (samples, bins) of column c (0..SOV_W - 1) of a whole of n items: *a..*b, inclusive. The range starts at the
//previous column's last item, so a step between two columns joins as a vertical line (a column shares one item with the
//one before it; with fewer items than columns, only where the item changes)
//n up to 5,000,000 (32-bit maths: the target has no divide instruction and 64-bit division is slow)
void  sov_items(int32 c, uint32 n, uint32 *a, uint32 *b);

//The same ranges for c = 0, 1, 2, ... without a division per column (the Scope's envelope pass): start, then one
//sov_cols_next per column
typedef struct
{
  uint32 q, r, rem, s, b;
} sov_cols;

void  sov_cols_start(sov_cols *it, uint32 n);
void  sov_cols_next(sov_cols *it, uint32 *a, uint32 *b);

//The row (0..SOV_H - 1) of a value on a linear scale where `top` is row 0's upper edge and `bot` the bottom row's lower
//edge (bot > top for screen y, bot < top for levels); clipped to the track. 32-bit maths: |bot - top| < 100,000,000
int32 sov_row(int32 v, int32 top, int32 bot);

//Fitted: vmax on row 0, vmin on the bottom row (the middle row when they are equal); clipped
int32 sov_row_fit(int32 v, int32 vmax, int32 vmin);

//The row at column c on a straight line from (c0, r0) to (c1, r1), rounded (c0 < c1)
int32 sov_lerp(int32 c0, int32 r0, int32 c1, int32 r1, int32 c);

//The vertical run at a column of a line arriving at row `cur` from the previous column's row `prev` (prev < 0: no
//previous column): the rows after prev up to cur, so consecutive runs join without overlap
void  sov_join(int32 prev, int32 cur, int32 *lo, int32 *hi);

//1 when track column x lies inside a window drawn at wx, ww wide (its edge lines included)
int   sov_inside(int32 x, int32 wx, int32 ww);

//The track column of a position x (clipped to the track)
int32 sov_col(int32 x);

//Tap to jump (fw 0.2.72, owner 10 F item 2): a touch-down on the slider row (y SOV_TAP_Y and below; not the Scope's
//extended drag area above it, which borders the cards) outside the window centres the window on the touch; the drag
//goes on from there, so a tap and a drag end the same way (the Scope scrolls, the Spectrum pans, the Frequency response
//sweeps the new span)
#define SOV_TAP_Y       450

//The shift in track pixels that centres a window wx, ww on x, kept on the track; 0 when x is inside the window or the
//touch is above the row
int32 sov_jump(int32 x, int32 y, int32 wx, int32 ww);

#endif
