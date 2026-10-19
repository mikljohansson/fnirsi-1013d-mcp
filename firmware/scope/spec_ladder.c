//----------------------------------------------------------------------------------------------------------------------------------
// spec_ladder.c - the Spectrum's zoom ladder (see spec_ladder.h). Pure C, no globals; host_tests/test_spec_zoom.c.
//----------------------------------------------------------------------------------------------------------------------------------

#include "spec_ladder.h"

static const uint8 spl_mant[3] = { 1, 2, 5 };

uint64 spl_span_mhz(int i)
{
  uint64 v;
  int    p;

  if(i >= SPL_FULL)
    return SPL_FMAX_MHZ;

  if(i < 0)
    i = 0;

  v = (uint64)spl_mant[i % 3] * 1000;
  for(p = i / 3; p > 0; p--)
    v *= 10;

  return v;
}

uint64 spl_start(const spl_cell *c)
{
  uint64 h = spl_span_mhz(c->span) / 2;

  return (c->centre > h) ? c->centre - h : 0;
}

uint64 spl_stop(const spl_cell *c)
{
  return spl_start(c) + spl_span_mhz(c->span);
}

uint64 spl_axis_mhz(const uint32 *rates, int32 n)
{
  uint64 a = (n > 0) ? (uint64)rates[0] * 500 : 0;

  return (a < SPL_FMAX_MHZ) ? a : SPL_FMAX_MHZ;
}

//fs / 2 in mHz covers stop
static int spl_covers(uint32 fs, uint64 stop)
{
  return (uint64)fs * 500 >= stop;
}

//N / fs <= SPL_TMAX_MS
static int spl_time_ok(uint32 fs, int log2n)
{
  return ((uint64)1000 << log2n) <= (uint64)fs * SPL_TMAX_MS;
}

int32 spl_rate(uint64 stop, int log2n, const uint32 *rates, int32 n)
{
  int32 i;

  if(n == 1)
    return spl_covers(rates[0], stop) ? 0 : -1;

  for(i = n - 1; i >= 0; i--)
    if(spl_covers(rates[i], stop) && spl_time_ok(rates[i], log2n))
      return i;

  return -1;
}

int spl_ok(const spl_cell *c, int log2n, const uint32 *rates, int32 n)
{
  uint64 stop = spl_stop(c);
  int32  i;

  if((n < 1) || (stop > spl_axis_mhz(rates, n)))
    return 0;

  //The slowest rate that covers the stop: the rule's rate unless the capture time limit holds it faster
  for(i = n - 1; (i > 0) && !spl_covers(rates[i], stop); i--) {}

  if(!spl_covers(rates[i], stop) || ((n > 1) && !spl_time_ok(rates[i], log2n)))
    return 0;

  //span / (fs / N) >= SPL_MIN_BINS
  return spl_span_mhz(c->span) * ((uint64)1 << log2n) >= (uint64)SPL_MIN_BINS * rates[i] * 1000;
}

void spl_fit(spl_cell *c, uint64 axis)
{
  uint64 s = spl_span_mhz(c->span);

  if(s >= axis)
    c->centre = axis / 2;
  else if(c->centre < s / 2)
    c->centre = s / 2;
  else if(c->centre > axis - s / 2)
    c->centre = axis - s / 2;
}

int spl_zoom(spl_cell *c, int dir, int log2n, const uint32 *rates, int32 n)
{
  uint64   axis = spl_axis_mhz(rates, n);
  spl_cell t = *c;

  if(dir > 0)
  {
    if(!t.span)
      return 0;

    t.span--;
    spl_fit(&t, axis);
    if(!spl_ok(&t, log2n, rates, n))
      return 0;
  }
  else
  {
    if(t.span >= SPL_FULL)
      return 0;

    t.span++;

    //The whole axis is a cell only where the axis is the full one; otherwise the widest 1-2-5 span inside it
    if(((t.span == SPL_FULL) && (axis < SPL_FMAX_MHZ)) || ((t.span < SPL_FULL) && (spl_span_mhz(t.span) > axis)))
      return 0;

    spl_fit(&t, axis);
  }

  *c = t;
  return 1;
}

int spl_pan(spl_cell *c, int64 d, uint64 axis)
{
  spl_cell t = *c;

  if((d < 0) && ((uint64)(-d) > t.centre))
    t.centre = 0;
  else
    t.centre = (uint64)((int64)t.centre + d);

  spl_fit(&t, axis);

  if(t.centre == c->centre)
    return 0;

  *c = t;
  return 1;
}

void spl_from_axis(spl_cell *c, uint64 axis)
{
  int i;

  if(axis >= SPL_FMAX_MHZ)
    i = SPL_FULL;
  else
    for(i = SPL_FULL - 1; (i > 0) && (spl_span_mhz(i) > axis); i--) {}

  c->span = (uint8)i;
  c->centre = spl_span_mhz(i) / 2;
  spl_fit(c, axis);
}

void spl_auto(spl_cell *c, uint64 f0, uint64 fhi, int log2n, const uint32 *rates, int32 n)
{
  uint64   axis = spl_axis_mhz(rates, n), s = fhi + fhi / 4;
  spl_cell t;
  int      i;

  if(20 * f0 > s)
    s = 20 * f0;
  if(!s || (s > SPL_FMAX_MHZ))
    s = SPL_FMAX_MHZ;

  for(i = 0; (i < SPL_FULL) && (spl_span_mhz(i) < s); i++) {}

  c->span = (uint8)i;
  c->centre = spl_span_mhz(i) / 2;
  spl_fit(c, axis);

  //Resolve the fundamental: RBW = 1.5 fs / N <= f0 / 4, i.e. 6000 fs <= f0 N (fs in Sa/s, f0 in mHz)
  while(f0 && c->span)
  {
    int32 r = spl_rate(spl_stop(c), log2n, rates, n);

    if((r < 0) || ((uint64)6000 * rates[r] <= f0 * ((uint64)1 << log2n)))
      break;

    t.span = c->span - 1;
    t.centre = spl_span_mhz(t.span) / 2;
    spl_fit(&t, axis);
    if(!spl_ok(&t, log2n, rates, n))
      break;
    *c = t;
  }

  //Never past the zoom-in end
  while(!spl_ok(c, log2n, rates, n) && (c->span < SPL_FULL))
  {
    c->span++;
    c->centre = spl_span_mhz(c->span) / 2;
    spl_fit(c, axis);
  }
}

int spl_bins(const spl_cell *c, uint64 bin, uint32 half, uint32 *first, uint32 *span)
{
  uint64 start = spl_start(c), stop = spl_stop(c), f, l;

  if(!bin || (half < 8))
  {
    *first = 0;
    *span = half;
    return 0;
  }

  f = start / bin;
  l = (stop + bin - 1) / bin;
  if(l - f < 8)
    l = f + 8;
  if(l - f > half)
  {
    f = 0;
    l = half;
  }
  if(l > half)
  {
    f = half - (l - f);
    l = half;
  }

  *first = (uint32)f;
  *span = (uint32)(l - f);

  return (f * bin <= start) && (l * bin >= stop);
}
