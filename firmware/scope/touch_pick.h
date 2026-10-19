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

//A drag of `delta` screen units applied to two positions that move together (a channel and the trigger level on it):
//delta limited so both starting positions, each first clamped to lo..hi, stay inside lo..hi
int32 touch_shared_delta(int32 delta, int32 a, int32 b, int32 lo, int32 hi);

#endif // TOUCH_PICK_H
