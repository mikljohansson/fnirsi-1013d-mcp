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
