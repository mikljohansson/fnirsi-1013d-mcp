//----------------------------------------------------------------------------------------------------------------------------------
// slider_ov.c - the slider track overview's geometry (pure C, see slider_ov.h).
//----------------------------------------------------------------------------------------------------------------------------------

#include "slider_ov.h"

static uint32 first_of(int32 c, uint32 n)
{
  return ((uint32)c * n) / SOV_W;
}

//The last item of column c without the join
static uint32 last_of(int32 c, uint32 n)
{
  uint32 s = first_of(c, n), e = first_of(c + 1, n);

  return (e > s) ? e - 1 : s;
}

void sov_items(int32 c, uint32 n, uint32 *a, uint32 *b)
{
  if(c < 0) c = 0;
  if(c > SOV_W - 1) c = SOV_W - 1;

  if(n == 0)
  {
    *a = *b = 0;
    return;
  }

  *b = last_of(c, n);
  *a = c ? last_of(c - 1, n) : 0;
  if(*a > *b)
    *a = *b;
}

//Column c's first item is s = c x n / W: step it by q = n / W and carry the remainder
void sov_cols_start(sov_cols *it, uint32 n)
{
  it->q   = n / SOV_W;
  it->r   = n % SOV_W;
  it->rem = 0;
  it->s   = 0;
  it->b   = 0;
}

void sov_cols_next(sov_cols *it, uint32 *a, uint32 *b)
{
  uint32 s = it->s, e = s + it->q;

  it->rem += it->r;
  if(it->rem >= SOV_W)
  {
    it->rem -= SOV_W;
    e++;
  }

  it->s = e;
  *a = it->b;
  *b = (e > s) ? e - 1 : s;
  if(*a > *b)
    *a = *b;
  it->b = *b;
}

static int32 clip_row(int32 r)
{
  if(r < 0) return 0;
  if(r > SOV_H - 1) return SOV_H - 1;
  return r;
}

int32 sov_row(int32 v, int32 top, int32 bot)
{
  int32 d = bot - top, n;

  if(d == 0)
    return 0;

  //Outside the scale: clipped before the multiply (no overflow)
  if(d > 0)
  {
    if(v < top) return 0;
    if(v >= bot) return SOV_H - 1;
    n = v - top;
  }
  else
  {
    if(v > top) return 0;
    if(v <= bot) return SOV_H - 1;
    n = top - v;
    d = -d;
  }

  return clip_row((int32)(((uint32)n * SOV_H) / (uint32)d));
}

int32 sov_row_fit(int32 v, int32 vmax, int32 vmin)
{
  int64 d = (int64)vmax - vmin;

  if(d <= 0)
    return SOV_H / 2;

  return clip_row((int32)((((int64)vmax - v) * (SOV_H - 1) * 2 + d) / (2 * d)));
}

int32 sov_lerp(int32 c0, int32 r0, int32 c1, int32 r1, int32 c)
{
  int32 d = c1 - c0, n;

  if(d <= 0)
    return r1;

  n = (r1 - r0) * (c - c0);
  return r0 + ((n >= 0) ? (n + d / 2) / d : -((-n + d / 2) / d));
}

void sov_join(int32 prev, int32 cur, int32 *lo, int32 *hi)
{
  if((prev < 0) || (prev == cur))
  {
    *lo = *hi = cur;
  }
  else if(cur > prev)
  {
    *lo = prev + 1;
    *hi = cur;
  }
  else
  {
    *lo = cur;
    *hi = prev - 1;
  }
}

int sov_inside(int32 x, int32 wx, int32 ww)
{
  return (x >= wx) && (x < wx + ww);
}

int32 sov_col(int32 x)
{
  if(x < SOV_X) return SOV_X;
  if(x > SOV_X + SOV_W - 1) return SOV_X + SOV_W - 1;
  return x;
}

int32 sov_jump(int32 x, int32 y, int32 wx, int32 ww)
{
  int32 t;

  if((y < SOV_TAP_Y) || (ww >= SOV_W) || sov_inside(x, wx, ww))
    return 0;

  t = sov_col(x) - ww / 2;
  if(t < SOV_X)
    t = SOV_X;
  if(t + ww > SOV_X + SOV_W)
    t = SOV_X + SOV_W - ww;

  return t - wx;
}

int sov_curve_fit(const sov_curve *cv, int32 *vmax, int32 *vmin)
{
  int   pass, any = 0;
  int32 i;

  *vmax = *vmin = 0;

  //Pass 0 the valid points, pass 1 the flagged ones when none is valid
  for(pass = 0; (pass < 2) && !any; pass++)
    for(i = 0; i < cv->n; i++)
      if(cv->have[i] && (pass || cv->ok[i]))
      {
        if(!any || (cv->v[i] > *vmax)) *vmax = cv->v[i];
        if(!any || (cv->v[i] < *vmin)) *vmin = cv->v[i];
        any = 1;
      }

  return any;
}

int32 sov_curve_row(const sov_curve *cv, int32 i, int32 vmax, int32 vmin)
{
  return sov_row_fit(cv->v[i], vmax, vmin);
}

int sov_curve_joins(const sov_curve *cv, int32 a, int32 b)
{
  int64 d = (int64)cv->v[b] - cv->v[a];

  if(cv->col[b] <= cv->col[a])
    return 0;

  return (cv->wrap <= 0) || ((d < cv->wrap) && (-d < cv->wrap));
}
