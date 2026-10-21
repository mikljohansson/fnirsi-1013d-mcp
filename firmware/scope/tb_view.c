//----------------------------------------------------------------------------------------------------------------------------------
// tb_view.c - the record behind the scope trace (see tb_view.h). Host-tested: host_tests/test_tb_view.c.
//----------------------------------------------------------------------------------------------------------------------------------

#include "tb_view.h"

#define TBV_LIMIT  0x3FFFFFFFLL                 //trigger point columns are kept well inside int32

static int64 tbv_floordiv(int64 a, int64 b)
{
  int64 q = a / b;

  if(((a % b) != 0) && ((a < 0) != (b < 0)))
    q--;

  return q;
}

static int32 tbv_sat(int64 v)
{
  if(v > TBV_LIMIT)
    return (int32)TBV_LIMIT;

  if(v < -TBV_LIMIT)
    return (int32)-TBV_LIMIT;

  return (int32)v;
}

//Samples between the first trace column and column 727: floor(724 / xpps)
static int64 tbv_span_samples(uint32 fs, uint32 fpd)
{
  return ((int64)TBV_SPAN_PX * fs) / ((int64)TBV_PX_PER_DIV * fpd);
}

int tbv_viable(uint32 fs, uint32 fpd, uint32 total)
{
  if((fs == 0) || (fpd == 0) || (total < 2))
    return 0;

  //At least TBV_MIN_SAMPLES_PER_DIV samples per division (fw 0.2.69: ten; one before)
  if((uint64)fs < (uint64)fpd * TBV_MIN_SAMPLES_PER_DIV)
    return 0;

  //The record spans the trace area: the last column's sample is inside the record
  return tbv_span_samples(fs, fpd) <= (int64)total - 1;
}

int32 tbv_snap(uint32 fs, const uint32 *fpd, int32 first, int32 last, int32 current, uint32 total)
{
  int32 i, best = -1, bestd = 0x7FFFFFFF, d;

  for(i = first; i <= last; i++)
  {
    if(!tbv_viable(fs, fpd[i], total))
      continue;

    d = (i < current) ? current - i : i - current;

    //Strictly nearer: a tie keeps the lower index
    if(d < bestd)
    {
      bestd = d;
      best  = i;
    }
  }

  return best;
}

void tbv_pos_limits(uint32 fs, uint32 fpd, uint32 total, uint32 trig, int32 *lo, int32 *hi)
{
  int64 num = (int64)TBV_PX_PER_DIV * fpd;
  int64 v, h, l;

  if((fs == 0) || (fpd == 0) || (total < 2))
  {
    *lo = *hi = 0;
    return;
  }

  //First sample shown >= 0: pos <= (trig - 1) x xpps
  h = tbv_floordiv(((int64)trig - 1) * num, fs);

  //Last column's sample <= total - 1: pos > (trig - total + V - 1) x xpps
  v = tbv_span_samples(fs, fpd);
  l = tbv_floordiv(((int64)trig - (int64)total + v - 1) * num, fs) + 1;

  //Record shorter than the trace area: it starts at the left edge
  if(l > h)
    l = h;

  *lo = tbv_sat(l);
  *hi = tbv_sat(h);
}

int32 tbv_clamp_pos(int32 pos, uint32 fs, uint32 fpd, uint32 total, uint32 trig)
{
  int32 lo, hi;

  tbv_pos_limits(fs, fpd, total, trig, &lo, &hi);

  if(pos < lo)
    return lo;

  if(pos > hi)
    return hi;

  return pos;
}

void tbv_set(tbv_tb *s, int32 t, uint32 fs, const uint32 *fpd, uint32 total, uint32 trig)
{
  s->tpd = t;
  s->pos = tbv_clamp_pos(s->pos, fs, fpd[t], total, trig);
}

int tbv_fit(tbv_tb *s, uint32 fs, const uint32 *fpd, int32 first, int32 last, uint32 total, uint32 trig)
{
  int32 t = tbv_snap(fs, fpd, first, last, s->tpd, total);

  if((t >= 0) && (t != s->tpd))
  {
    tbv_set(s, t, fs, fpd, total, trig);
    return 1;
  }

  if((s->tpd >= first) && (s->tpd <= last))
    s->pos = tbv_clamp_pos(s->pos, fs, fpd[s->tpd], total, trig);

  return 0;
}

int32 tbv_ladder_rate(uint32 fpd, const uint32 *rates, int32 nrates, uint32 total)
{
  int32 i;

  for(i = 0; i < nrates; i++)
    if(tbv_viable(rates[i], fpd, total))
      return i;

  return -1;
}

int32 tbv_zoom_step(int32 t, int dir, const uint32 *fpd, int32 first, int32 last, const uint32 *rates, int32 nrates,
                    uint32 total)
{
  int32 step = (dir < 0) ? -1 : 1, i;

  if(dir == 0)
    return -1;

  for(i = t + step; (i >= first) && (i <= last); i += step)
    if(tbv_ladder_rate(fpd[i], rates, nrates, total) >= 0)
      return i;

  //Nothing further that way. From a time/div off the ladder (a start no rate can show, e.g. 5 ns/div), the ladder's end
  //on the other side, so a step never leaves the pair where no rate can show it
  if((t >= first) && (t <= last) && (tbv_ladder_rate(fpd[t], rates, nrates, total) >= 0))
    return -1;

  for(i = t - step; (i >= first) && (i <= last); i -= step)
    if(tbv_ladder_rate(fpd[i], rates, nrates, total) >= 0)
      return i;

  return -1;
}

void tbv_window(int32 trig, int32 pos, uint32 fs, uint32 fpd, uint32 total, int32 *first, int32 *xend)
{
  int64 num = (int64)TBV_PX_PER_DIV * fpd;
  int64 f, fmax, end;

  *xend = TBV_X_END;

  if((fs == 0) || (fpd == 0) || (total < 2))
  {
    *first = 0;
    return;
  }

  //1.00o5: F = trig - pos / xpps - 1, truncated: trig - 1 - ceil(pos x fs / (50 fpd))
  f = (int64)trig - 1 + tbv_floordiv(-(int64)pos * fs, num);

  fmax = (int64)total - 1 - tbv_span_samples(fs, fpd);

  if(fmax >= 0)
  {
    //The window stays inside the record (a trigger found off the nominal point can push it)
    if(f > fmax)
      f = fmax;

    if(f < 0)
      f = 0;

    *first = (int32)f;
    return;
  }

  //Record shorter than the trace area: from the left edge to the record's end, nothing drawn after it
  *first = 0;
  end = TBV_X_FIRST + ((int64)total * num + fs - 1) / fs;

  if(end < TBV_X_FIRST + 1)
    end = TBV_X_FIRST + 1;

  if(end < TBV_X_END)
    *xend = (int32)end;
}

void tbv_slider(int32 first, uint32 fs, uint32 fpd, uint32 total, int32 *x, int32 *w)
{
  int64 vis, ww, xx;

  if((fs == 0) || (fpd == 0) || (total < 2))
  {
    *x = TBV_SLIDER_X;
    *w = TBV_SLIDER_W;
    return;
  }

  if(first < 0)
    first = 0;

  vis = tbv_span_samples(fs, fpd) + 1;
  ww  = (vis * TBV_SLIDER_W) / total;

  if(ww > TBV_SLIDER_W)
    ww = TBV_SLIDER_W;

  if(ww < TBV_SLIDER_MIN_W)
    ww = TBV_SLIDER_MIN_W;

  xx = TBV_SLIDER_X + ((int64)first * TBV_SLIDER_W) / total;

  if(xx + ww > TBV_SLIDER_X + TBV_SLIDER_W)
    xx = TBV_SLIDER_X + TBV_SLIDER_W - ww;

  *x = (int32)xx;
  *w = (int32)ww;
}

int32 tbv_slider_move(int32 dx, uint32 fs, uint32 fpd, uint32 total)
{
  int64 num;

  if((fs == 0) || (fpd == 0))
    return 0;

  //dx px on the track = dx x total / 720 samples = that x xpps columns, the trigger point moving the other way
  num = (int64)dx * total * TBV_PX_PER_DIV * fpd;

  return tbv_sat(-(num / ((int64)TBV_SLIDER_W * fs)));
}
