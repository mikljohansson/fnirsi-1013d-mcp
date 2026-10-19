// test_touch_pick.c - plot touch geometry (fw 0.2.54, touch_pick.c): pointer position as drawn, the left handle column
// (CH1, CH2, ref1..ref8), the channel zero-line pick and the shared clamped move of a channel and its trigger level.
// docs/plans/touch-review.md sections (b) defects 1, 3, 4, 9 and (c) rules 1-2. Run natively.

#include <stdio.h>
#include <string.h>

#include "touch_pick.h"
#include "test_util.h"

//Draw order of scope_draw_pointers: CH1, CH2, ref1..ref8
enum { P_CH1, P_CH2, P_REF1, P_REF2, P_REF3, P_REF4, P_REF5, P_REF6, P_REF7, P_REF8, P_COUNT };

static TOUCH_POINTER p[P_COUNT];

static void all_off(void)
{
  memset(p, 0, sizeof(p));
}

static void put(int i, uint16 position)
{
  p[i].on = 1;
  p[i].position = position;
}

static int32 pick(int32 x, int32 y)
{
  return pick_left_handle(p, P_COUNT, x, y);
}

static void test_pointer_geometry(void)
{
  int32 tp;

  //Unclamped: top 442 - tp, tip 448 - tp (the trace's zero row)
  T_EQ(scope_pointer_top(200), 242);
  T_EQ(scope_pointer_tip(200), 248);
  for(tp = 0; tp <= 393; tp++)
    T_EQ(scope_pointer_tip(tp), 448 - tp);

  //Clamped to the grid, as drawn: a pointer past an edge sits at the edge
  T_EQ(scope_pointer_top(393), 49);
  T_EQ(scope_pointer_top(396), 49);
  T_EQ(scope_pointer_top(449), 49);
  T_EQ(scope_pointer_top(65535), 49);
  T_EQ(scope_pointer_top(0), 442);
  T_EQ(scope_pointer_top(-10), 442);
  T_EQ(scope_pointer_tip(449), 55);

  //Trace position clamp (auto setup, settings load)
  T_EQ(scope_trace_clamp(0), 6);
  T_EQ(scope_trace_clamp(-25), 6);
  T_EQ(scope_trace_clamp(6), 6);
  T_EQ(scope_trace_clamp(200), 200);
  T_EQ(scope_trace_clamp(396), 396);
  T_EQ(scope_trace_clamp(399), 396);
  T_EQ(scope_trace_clamp(65535 - 10), 396);
}

static void test_left_column(void)
{
  int32 y;

  //Nothing switched on: nothing picked
  all_off();
  T_EQ(pick(10, 248), -1);

  //CH1 alone at 200 (tip row 248): box rows 226..269, columns 0..43
  put(P_CH1, 200);
  T_EQ(pick(10, 248), P_CH1);
  T_EQ(pick(0, 248), P_CH1);
  T_EQ(pick(43, 248), P_CH1);
  T_EQ(pick(44, 248), -1);
  T_EQ(pick(-1, 248), -1);
  T_EQ(pick(10, 226), P_CH1);
  T_EQ(pick(10, 225), -1);
  T_EQ(pick(10, 269), P_CH1);
  T_EQ(pick(10, 270), -1);
  for(y = 226; y <= 269; y++)
    T_EQ(pick(20, y), P_CH1);

  //Defect 1: the pointer's own pixels (x 2..22, rows 242..255) grab the channel
  for(y = 242; y <= 255; y++)
    T_EQ(pick(12, y), P_CH1);

  //A disabled channel is no candidate, even when nearer (defect 3)
  put(P_CH2, 210);
  p[P_CH2].on = 0;
  T_EQ(pick(10, 238), P_CH1);
  T_EQ(pick(10, 240), P_CH1);

  //Both on, 10 px apart: nearest wins; the midpoint (5 / 5) ties to CH2, drawn on top
  p[P_CH2].on = 1;
  T_EQ(pick(10, 248), P_CH1);
  T_EQ(pick(10, 238), P_CH2);
  T_EQ(pick(10, 244), P_CH1);
  T_EQ(pick(10, 243), P_CH2);
  T_EQ(pick(10, 260), P_CH1);
  T_EQ(pick(10, 217), P_CH2);           //only CH2's box (tip 238: rows 216..259)
  T_EQ(pick(10, 215), -1);

  //CH2 parked exactly on CH1: CH2 (on top) first; with CH2 moved away CH1 is reachable again
  put(P_CH2, 200);
  T_EQ(pick(10, 248), P_CH2);
  put(P_CH2, 100);
  T_EQ(pick(10, 248), P_CH1);
  T_EQ(pick(10, 348), P_CH2);

  //References share the column; a ref parked on a channel hides its pointer and wins the tie
  all_off();
  put(P_CH1, 200);
  put(P_REF3, 200);
  T_EQ(pick(10, 248), P_REF3);
  put(P_REF5, 200);
  T_EQ(pick(10, 248), P_REF5);          //the last drawn of three
  put(P_REF5, 150);
  T_EQ(pick(10, 298), P_REF5);
  T_EQ(pick(10, 248), P_REF3);

  //Refs: nearest, not first in order (defect 12's ref part)
  all_off();
  put(P_REF1, 200);                     //tip 248
  put(P_REF2, 210);                     //tip 238
  T_EQ(pick(10, 241), P_REF2);
  T_EQ(pick(10, 246), P_REF1);
  put(P_REF8, 205);                     //tip 243
  T_EQ(pick(10, 243), P_REF8);
  T_EQ(pick(10, 248), P_REF1);

  //Clamped pointers are grabbed where they are drawn (defect 4): position 449 and 65000 draw at the top (tip 55)
  all_off();
  put(P_CH1, 449);
  T_EQ(pick(10, 55), P_CH1);
  T_EQ(pick(10, 33), P_CH1);            //box rows 33..76 (the caller only sees y >= 45)
  T_EQ(pick(10, 76), P_CH1);
  T_EQ(pick(10, 77), -1);
  put(P_CH1, 65000);
  T_EQ(pick(10, 55), P_CH1);
  put(P_CH1, 0);                        //bottom: top 442, tip 448
  T_EQ(pick(10, 448), P_CH1);
  T_EQ(pick(10, 469), P_CH1);
  T_EQ(pick(10, 470), -1);

  //Drag range edges
  put(P_CH1, 6);
  T_EQ(pick(10, 442), P_CH1);
  put(P_CH1, 396);
  T_EQ(pick(10, 55), P_CH1);
  T_EQ(pick(10, 52), P_CH1);
}

static void test_channel_line(void)
{
  //CH1 zero row 248, CH2 zero row 238
  T_EQ(pick_channel_line(1, 200, 0, 0, 248), 1);
  T_EQ(pick_channel_line(1, 200, 0, 0, 277), 1);
  T_EQ(pick_channel_line(1, 200, 0, 0, 278), 0);
  T_EQ(pick_channel_line(1, 200, 0, 0, 219), 1);
  T_EQ(pick_channel_line(1, 200, 0, 0, 218), 0);
  T_EQ(pick_channel_line(0, 200, 0, 0, 248), 0);

  //Defect 3: CH2 disabled and nearer: CH1 still picked
  T_EQ(pick_channel_line(1, 200, 0, 210, 240), 1);
  T_EQ(pick_channel_line(1, 200, 0, 210, 238), 1);

  //Both on: nearest, a tie to CH2
  T_EQ(pick_channel_line(1, 200, 1, 210, 246), 1);
  T_EQ(pick_channel_line(1, 200, 1, 210, 240), 2);
  T_EQ(pick_channel_line(1, 200, 1, 210, 243), 2);
  T_EQ(pick_channel_line(1, 200, 1, 200, 248), 2);
  T_EQ(pick_channel_line(1, 200, 0, 200, 248), 1);

  //Out-of-range positions do not wrap (defect 4: was a uint32 449 - traceposition)
  T_EQ(pick_channel_line(1, 65000, 0, 0, 100), 0);
  T_EQ(pick_channel_line(1, 449, 0, 0, 1), 1);
}

static void test_shared_delta(void)
{
  //Free range: unchanged
  T_EQ(touch_shared_delta(10, 200, 250, 6, 396), 10);
  T_EQ(touch_shared_delta(-10, 200, 250, 6, 396), -10);
  T_EQ(touch_shared_delta(0, 200, 250, 6, 396), 0);

  //The higher one limits a move up, the lower one a move down (either argument order)
  T_EQ(touch_shared_delta(500, 200, 250, 6, 396), 146);
  T_EQ(touch_shared_delta(500, 250, 200, 6, 396), 146);
  T_EQ(touch_shared_delta(-500, 200, 250, 6, 396), -194);
  T_EQ(touch_shared_delta(-500, 250, 200, 6, 396), -194);

  //Defect 9: the level keeps its distance to the channel through a drag to the top and back
  {
    int32 ch = 300, lvl = 340, d;

    d = touch_shared_delta(200, ch, lvl, 6, 396);
    ch += d;
    lvl += d;
    T_EQ(lvl, 396);
    T_EQ(lvl - ch, 40);
    d = touch_shared_delta(-100, ch, lvl, 6, 396);
    ch += d;
    lvl += d;
    T_EQ(lvl - ch, 40);
  }

  //Starts outside the range are taken as clamped, so the limits never cross
  T_EQ(touch_shared_delta(5, 399, 200, 6, 396), 0);
  T_EQ(touch_shared_delta(-5, 399, 200, 6, 396), -5);
  T_EQ(touch_shared_delta(-5, 0, 0, 6, 396), 0);
  T_EQ(touch_shared_delta(5, 0, 0, 6, 396), 5);
}

int main(void)
{
  test_pointer_geometry();
  test_left_column();
  test_channel_line();
  test_shared_delta();
  return t_report("test_touch_pick");
}
