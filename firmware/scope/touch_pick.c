//----------------------------------------------------------------------------------------------------------------------------------
// touch_pick.c - plot touch geometry (see touch_pick.h). Host-tested: host_tests/test_touch_pick.c.
//----------------------------------------------------------------------------------------------------------------------------------

#include "touch_pick.h"

int32 touch_clamp(int32 v, int32 lo, int32 hi)
{
  if(v < lo)
    return lo;

  if(v > hi)
    return hi;

  return v;
}

int32 scope_trace_clamp(int32 position)
{
  return touch_clamp(position, TRACE_POS_MIN, TRACE_POS_MAX);
}

int32 scope_pointer_top(int32 position)
{
  return touch_clamp(442 - position, POINTER_TOP_MIN, POINTER_TOP_MAX);
}

int32 scope_pointer_tip(int32 position)
{
  return scope_pointer_top(position) + POINTER_TIP_DY;
}

int32 pick_left_handle(const TOUCH_POINTER *p, uint32 count, int32 x, int32 y)
{
  int32  pick = -1;
  int32  best = HANDLE_HALF + 1;
  int32  d;
  uint32 i;

  if((x < 0) || (x >= HANDLE_W))
    return -1;

  for(i = 0; i < count; i++)
  {
    if(!p[i].on)
      continue;

    //Rows tip - 22 .. tip + 21 (44 rows)
    d = y - scope_pointer_tip(p[i].position);

    if((d < -HANDLE_HALF) || (d >= HANDLE_HALF))
      continue;

    if(d < 0)
      d = -d;

    //<= so a tie goes to the later-drawn pointer, the one on top
    if(d <= best)
    {
      best = d;
      pick = (int32)i;
    }
  }

  return pick;
}

uint32 pick_channel_line(uint8 on1, uint16 position1, uint8 on2, uint16 position2, int32 y)
{
  int32 d1 = y - (448 - (int32)position1);
  int32 d2 = y - (448 - (int32)position2);

  if(d1 < 0)
    d1 = -d1;

  if(d2 < 0)
    d2 = -d2;

  if(!on1 || (d1 >= CHANNEL_GRAB_RADIUS))
    d1 = -1;

  if(!on2 || (d2 >= CHANNEL_GRAB_RADIUS))
    d2 = -1;

  if((d2 >= 0) && ((d1 < 0) || (d2 <= d1)))
    return 2;

  if(d1 >= 0)
    return 1;

  return 0;
}

int32 touch_shared_delta(int32 delta, int32 a, int32 b, int32 lo, int32 hi)
{
  a = touch_clamp(a, lo, hi);
  b = touch_clamp(b, lo, hi);

  //The larger start limits the move up, the smaller the move down
  if(a < b)
  {
    int32 t = a;
    a = b;
    b = t;
  }

  return touch_clamp(delta, lo - b, hi - a);
}

//----------------------------------------------------------------------------------------------------------------------------------
// fw 0.2.64: touch batch 2

//y on the segment a..b at column x (a.x <= x <= b.x, a.x != b.x), rounded towards a
static int32 seg_y(const TOUCH_XY *a, const TOUCH_XY *b, int32 x)
{
  return (int32)a->y + (((int32)b->y - (int32)a->y) * (x - (int32)a->x)) / ((int32)b->x - (int32)a->x);
}

int32 trace_distance(const TOUCH_XY *p, uint32 n, int32 x, int32 y)
{
  int32  lo = 0x7FFFFFFF, hi = -0x7FFFFFFF, x0 = x - 1, x1 = x + 1, ya, yb;
  uint32 i;

  for(i = 0; i < n; i++)
  {
    //The point itself
    if(((int32)p[i].x >= x0) && ((int32)p[i].x <= x1))
    {
      if((int32)p[i].y < lo) lo = p[i].y;
      if((int32)p[i].y > hi) hi = p[i].y;
    }

    //The line to the next point, clipped to the columns x - 1 .. x + 1
    if(i + 1 < n)
    {
      const TOUCH_XY *a = &p[i], *b = &p[i + 1];
      int32 ax, bx, c0, c1;

      if(a->x > b->x)
      {
        const TOUCH_XY *t = a;
        a = b;
        b = t;
      }

      ax = a->x;
      bx = b->x;

      if((bx < x0) || (ax > x1) || (ax == bx))
        continue;

      c0 = (ax > x0) ? ax : x0;
      c1 = (bx < x1) ? bx : x1;
      ya = seg_y(a, b, c0);
      yb = seg_y(a, b, c1);

      if(ya > yb)
      {
        int32 t = ya;
        ya = yb;
        yb = t;
      }

      if(ya < lo) lo = ya;
      if(yb > hi) hi = yb;
    }
  }

  if(lo > hi)
    return -1;

  if(y < lo)
    return lo - y;

  if(y > hi)
    return y - hi;

  return 0;
}

uint32 pick_channel_near(uint8 on1, uint16 position1, int32 trace1, uint8 on2, uint16 position2, int32 trace2, int32 y)
{
  int32 d1 = y - (448 - (int32)position1);
  int32 d2 = y - (448 - (int32)position2);

  if(d1 < 0)
    d1 = -d1;

  if(d2 < 0)
    d2 = -d2;

  if((trace1 >= 0) && (trace1 < d1))
    d1 = trace1;

  if((trace2 >= 0) && (trace2 < d2))
    d2 = trace2;

  if(!on1 || (d1 > TRACE_GRAB_RADIUS))
    d1 = -1;

  if(!on2 || (d2 > TRACE_GRAB_RADIUS))
    d2 = -1;

  if((d2 >= 0) && ((d1 < 0) || (d2 <= d1)))
    return 2;

  if(d1 >= 0)
    return 1;

  return 0;
}

int32 touch_tap_zone(int32 x, int32 y, int32 bottom_left, int32 bottom_right)
{
  int32 left, bottom;

  if((x < 3) || (x > 719) || (y < 50))
    return TAP_NONE;

  if(x < 358)
    left = 1;
  else if(x > 362)
    left = 0;
  else
    return TAP_NONE;

  bottom = left ? bottom_left : bottom_right;

  if(bottom > TAP_GRID_BOTTOM)
    bottom = TAP_GRID_BOTTOM;

  if(y >= bottom)
    return TAP_NONE;

  if(y < 100)
    return left ? TAP_CH1_MORE : TAP_CH2_MORE;

  if(y >= bottom - TAP_BAND_H)
    return left ? TAP_CH1_LESS : TAP_CH2_LESS;

  return left ? TAP_TB_SLOWER : TAP_TB_FASTER;
}

int32 touch_cards_top(uint32 count)
{
  if(count == 0)
    return TAP_GRID_BOTTOM;

  return CARDS_FIRST_Y - CARDS_PITCH * (int32)((count - 1) / CARDS_PER_ROW);
}
