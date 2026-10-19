//----------------------------------------------------------------------------------------------------------------------------------
// touch_pick.h - plot touch geometry shared by the pointer drawing and the touch pick (fw 0.2.54, docs/plans/touch-review.md
// section c1): where a left-edge pointer is drawn, which pointer a touch in the left handle column grabs, which channel's
// zero line a touch on the plot grabs, and the clamped move shared by a channel and the trigger level that follows it.
// Pure C, no globals, host-buildable (host_tests/test_touch_pick.c).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef TOUCH_PICK_H
#define TOUCH_PICK_H

#include "types.h"

//Left / right pointer icons (14 rows): top row clamped to the grid, the tip (first of the two widest rows, icons.c) 6 below
#define POINTER_TOP_MIN       49
#define POINTER_TOP_MAX       442
#define POINTER_TIP_DY        6

//Handle boxes: 44 px, x 0..43 for the left column, rows tip - 22 .. tip + 21
#define HANDLE_W              44
#define HANDLE_HALF           22

//Drag range of a trace position, a reference position and the trigger level (screen units above the grid bottom)
#define TRACE_POS_MIN         6
#define TRACE_POS_MAX         396

//Grab radius around a channel's zero line on the plot (1.00o5's 30 px, unchanged in this batch)
#define CHANNEL_GRAB_RADIUS   30

//fw 0.2.64 (touch batch 2): grab radius around a channel's drawn trace or zero line on the plot, the dead band before a
//touch becomes a drag, and the movement that makes a touch on a trace no tap (the finger moved: 1.00o5's 2 px)
#define TRACE_GRAB_RADIUS     40
#define TOUCH_DEAD_BAND       6
#define TOUCH_MOVED           2

//Tap time limit: a lift inside the dead band within this many ms is a tap
#define TOUCH_TAP_MS          250

//Tap bands on the empty plot (owner: keep 1.00o5's V/div and time base bands): x 3..719, left half x < 358, right half
//x > 362; V/div more sensitive y 50..99; time base y 100 .. bottom - 81; V/div less sensitive the 80 rows above
//`bottom`, the first row below the plot that the band may not use (the grid's bottom 449, or the top of the measurement
//cards on that half); nothing on the record slider row
#define TAP_BAND_H            80
#define TAP_GRID_BOTTOM       449

enum
{
  TAP_NONE = 0,
  TAP_TB_SLOWER,                        //left half, middle: time/div one step slower
  TAP_TB_FASTER,                        //right half, middle: one step faster
  TAP_CH1_MORE,                         //left half, top: CH1 V/div one step more sensitive
  TAP_CH1_LESS,                         //left half, bottom band: CH1 one step less sensitive
  TAP_CH2_MORE,
  TAP_CH2_LESS
};

//Measurement cards (scope_display_channel_measurements): rows of three from y 426 upwards, 24 px pitch, 22 px tall
#define CARDS_FIRST_Y         426
#define CARDS_PITCH           24
#define CARDS_PER_ROW         3

//One point of a drawn trace (layout of DISPLAYPOINTS, variables.h)
typedef struct
{
  uint16 x;
  uint16 y;
} TOUCH_XY;

//One pointer at the left edge: drawn when on, at screen unit `position` (traceposition)
typedef struct
{
  uint8  on;
  uint16 position;
} TOUCH_POINTER;

//v limited to lo..hi
int32 touch_clamp(int32 v, int32 lo, int32 hi);

//A trace / reference / trigger level position limited to the drag range TRACE_POS_MIN..TRACE_POS_MAX
int32 scope_trace_clamp(int32 position);

//Top row of a left or right pointer icon for screen unit `position`: 442 - position, clamped to the grid (49..442)
int32 scope_pointer_top(int32 position);

//Screen row of a pointer's tip: scope_pointer_top + POINTER_TIP_DY (448 - position while not clamped)
int32 scope_pointer_tip(int32 position);

//The left handle column: index into p[0..count-1] (draw order, the last one drawn on top) of the switched-on pointer whose
//44 px box holds (x, y), nearest tip first, a tie to the later-drawn one; -1 when none (or x outside the column)
int32 pick_left_handle(const TOUCH_POINTER *p, uint32 count, int32 x, int32 y);

//Channel zero lines on the plot: 1 or 2 for the nearest switched-on channel whose zero row (448 - position) is less than
//CHANNEL_GRAB_RADIUS from y, a tie to channel 2 (drawn on top); 0 when none
uint32 pick_channel_line(uint8 on1, uint16 position1, uint8 on2, uint16 position2, int32 y);

//Distance from row y to a drawn trace (its points p[0..n-1] in drawing order, joined as drawn) over the columns x - 1 .. x + 1;
//0 when y is inside the span the trace covers there, -1 when the trace has nothing in those columns
int32 trace_distance(const TOUCH_XY *p, uint32 n, int32 x, int32 y);

//Channel on the plot (fw 0.2.64): 1 or 2 for the nearest switched-on channel whose distance (the nearer of its zero line
//and its drawn trace, -1 = no trace there) is within TRACE_GRAB_RADIUS of y, a tie to channel 2 (drawn on top); 0 when none
uint32 pick_channel_near(uint8 on1, uint16 position1, int32 trace1, uint8 on2, uint16 position2, int32 trace2, int32 y);

//The tap band at (x, y) (TAP_*); bottom_left / bottom_right: first row the lower band may not use on each half
int32 touch_tap_zone(int32 x, int32 y, int32 bottom_left, int32 bottom_right);

//First row of a half's measurement cards (TAP_GRID_BOTTOM when none): `count` cards drawn
int32 touch_cards_top(uint32 count);

//A drag of `delta` screen units applied to two positions that move together (a channel and the trigger level on it):
//delta limited so both starting positions, each first clamped to lo..hi, stay inside lo..hi
int32 touch_shared_delta(int32 delta, int32 a, int32 b, int32 lo, int32 hi);

#endif // TOUCH_PICK_H
