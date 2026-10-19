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

//fw 0.2.64 (touch batch 2): distance to the drawn trace
static void test_trace_distance(void)
{
  TOUCH_XY lin[4] = { { 100, 200 }, { 101, 200 }, { 102, 300 }, { 103, 300 } };   //a square edge between 101 and 102
  TOUCH_XY peak[4] = { { 200, 150 }, { 200, 250 }, { 201, 160 }, { 201, 240 } };  //Peak style: max, min per column
  TOUCH_XY sparse[2] = { { 300, 100 }, { 320, 300 } };                           //2 px per... a slow edge over 20 columns

  //On the flat part, above and below
  T_EQ(trace_distance(lin, 4, 100, 200), 0);
  T_EQ(trace_distance(lin, 4, 100, 190), 10);
  T_EQ(trace_distance(lin, 4, 100, 215), 15);

  //On the edge: the whole vertical span counts
  T_EQ(trace_distance(lin, 4, 101, 250), 0);
  T_EQ(trace_distance(lin, 4, 102, 299), 0);
  T_EQ(trace_distance(lin, 4, 102, 310), 10);

  //No trace in those columns
  T_EQ(trace_distance(lin, 4, 110, 200), -1);
  T_EQ(trace_distance(lin, 0, 100, 200), -1);

  //Peak style: the band between max and min
  T_EQ(trace_distance(peak, 4, 200, 200), 0);
  T_EQ(trace_distance(peak, 4, 201, 140), 10);
  T_EQ(trace_distance(peak, 4, 200, 260), 10);

  //Interpolated between sparse points: at column 310 the line is at y 200, columns 309..311 span 190..210
  T_EQ(trace_distance(sparse, 2, 310, 200), 0);
  T_EQ(trace_distance(sparse, 2, 310, 230), 20);
  T_EQ(trace_distance(sparse, 2, 310, 170), 20);
}

static void test_channel_near(void)
{
  //CH1 zero line at row 248 (position 200), its square wave drawn 100 px higher: grabbable on the wave
  T_EQ(pick_channel_near(1, 200, 5, 0, 0, -1, 150), 1);
  //Only the zero line (trace far away): 1.00o5 behaviour within the larger radius
  T_EQ(pick_channel_near(1, 200, 200, 0, 0, -1, 248 + 40), 1);
  T_EQ(pick_channel_near(1, 200, 200, 0, 0, -1, 248 + 41), 0);
  //A trace within 40 px of the touch
  T_EQ(pick_channel_near(1, 200, 40, 0, 0, -1, 100), 1);
  T_EQ(pick_channel_near(1, 200, 41, 0, 0, -1, 100), 0);
  //Nearest of two, a tie to CH2, a switched-off channel never
  T_EQ(pick_channel_near(1, 200, 10, 1, 100, 20, 120), 1);
  T_EQ(pick_channel_near(1, 200, 10, 1, 100, 10, 120), 2);
  T_EQ(pick_channel_near(1, 200, 30, 0, 100, 0, 120), 1);
  T_EQ(pick_channel_near(0, 200, 0, 0, 100, 0, 120), 0);
}

static void test_tap_zones(void)
{
  //No cards: 1.00o5's bands, contiguous, up to the grid's bottom
  T_EQ(touch_tap_zone(100, 60, 449, 449), TAP_CH1_MORE);
  T_EQ(touch_tap_zone(500, 99, 449, 449), TAP_CH2_MORE);
  T_EQ(touch_tap_zone(100, 100, 449, 449), TAP_TB_SLOWER);
  T_EQ(touch_tap_zone(500, 368, 449, 449), TAP_TB_FASTER);
  T_EQ(touch_tap_zone(100, 369, 449, 449), TAP_CH1_LESS);
  T_EQ(touch_tap_zone(500, 448, 449, 449), TAP_CH2_LESS);

  //Nothing on the record slider row, below the grid, at the centre line, outside the plot
  T_EQ(touch_tap_zone(100, 449, 449, 449), TAP_NONE);
  T_EQ(touch_tap_zone(100, 460, 449, 449), TAP_NONE);
  T_EQ(touch_tap_zone(360, 200, 449, 449), TAP_NONE);
  T_EQ(touch_tap_zone(2, 200, 449, 449), TAP_NONE);
  T_EQ(touch_tap_zone(720, 200, 449, 449), TAP_NONE);
  T_EQ(touch_tap_zone(100, 49, 449, 449), TAP_NONE);

  //Cards on the left half (two rows: top 402): the band moves up above them; the right half is unchanged
  T_EQ(touch_cards_top(0), 449);
  T_EQ(touch_cards_top(3), 426);
  T_EQ(touch_cards_top(4), 402);
  T_EQ(touch_cards_top(12), 354);
  T_EQ(touch_tap_zone(100, 410, 402, 449), TAP_NONE);
  T_EQ(touch_tap_zone(100, 401, 402, 449), TAP_CH1_LESS);
  T_EQ(touch_tap_zone(100, 322, 402, 449), TAP_CH1_LESS);
  T_EQ(touch_tap_zone(100, 321, 402, 449), TAP_TB_SLOWER);
  T_EQ(touch_tap_zone(500, 410, 402, 449), TAP_CH2_LESS);

  //Dead band and the moved threshold
  T_TRUE(TOUCH_DEAD_BAND >= 6);
  T_TRUE(TOUCH_MOVED < TOUCH_DEAD_BAND);
}

int main(void)
{
  test_pointer_geometry();
  test_left_column();
  test_channel_line();
  test_shared_delta();
  test_trace_distance();
  test_channel_near();
  test_tap_zones();
  return t_report("test_touch_pick");
}
