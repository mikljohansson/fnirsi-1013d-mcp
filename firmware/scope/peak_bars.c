//----------------------------------------------------------------------------------------------------------------------------------
// peak_bars.c - the column walk over a peak-detect record (see peak_bars.h). Host-tested: host_tests/test_peak_bars.c.
//----------------------------------------------------------------------------------------------------------------------------------

#include "peak_bars.h"
#include "pc_decim.h"

uint32_t pb_step_q16(uint32_t samples_per_col_q16)
{
  return samples_per_col_q16 ? samples_per_col_q16 : 1;
}

void pb_start(pb_walk *w, const uint16_t *pairs, uint32_t n, uint32_t first, uint32_t step_q16, uint32_t gain, int32_t off,
              int invert)
{
  w->pairs    = pairs;
  w->n        = n;
  w->pos_q16  = (first < n) ? (first << 16) : (n << 16);
  w->step_q16 = pb_step_q16(step_q16);
  w->gain     = gain;
  w->off      = off;
  w->invert   = invert;
}

int pb_next(pb_walk *w, int32_t *lo, int32_t *hi)
{
  uint32_t i = w->pos_q16 >> 16, end, mn = 255, mx = 0;
  uint64_t next = (uint64_t)w->pos_q16 + w->step_q16;
  int32_t  a, b;

  if(i >= w->n)
    return 0;

  //The samples floor(p) .. floor(p + step) - 1, at least one; a column reaching past the record takes what it holds
  end = (next >= ((uint64_t)w->n << 16)) ? w->n : (uint32_t)(next >> 16);
  if(end <= i)
    end = i + 1;

  w->pos_q16 = (next >= ((uint64_t)w->n << 16)) ? (w->n << 16) : (uint32_t)next;

  for(; i < end; i++)
  {
    uint32_t p = w->pairs[i], l = p & 0xFF, h = p >> 8;

    if(l < mn)
      mn = l;

    if(h > mx)
      mx = h;
  }

  a =pc_peak_code((uint8_t)mn, w->gain, w->off);
  b = pc_peak_code((uint8_t)mx, w->gain, w->off);

  if(w->invert)
  {
    *lo = 255 - b;
    *hi = 255 - a;
  }
  else
  {
    *lo = a;
    *hi = b;
  }

  return 1;
}

uint32_t pb_draw(pb_walk *w, int32_t x0, int32_t x1, const pb_draw_cb *cb)
{
  uint32_t cols = 0;
  int32_t  x, lo, hi, y0, y1;

  for(x = x0; (x < x1) && pb_next(w, &lo, &hi); x++, cols++)
  {
    y0 = cb->to_y(cb->ctx, lo);
    y1 = cb->to_y(cb->ctx, hi);

    if(y0 <= y1)
      cb->vline(cb->ctx, x, y0, y1);
    else
      cb->vline(cb->ctx, x, y1, y0);
  }

  return cols;
}

uint32_t pb_phos_record(phos_map *m, pb_walk *w, const uint16_t *lut, int32_t x0, int32_t x1, uint32_t inc)
{
  uint32_t touches = 0;
  int32_t  x, lo, hi;

  for(x = x0; (x < x1) && pb_next(w, &lo, &hi); x++)
    touches += phos_add_span(m, x, lut[lo], lut[hi], inc);

  return touches;
}

int pb_overload(const uint16_t *pairs, uint32_t n)
{
  uint32_t i;

  for(i = 0; i < n; i++)
    if(pc_peak_overload((uint8_t)pairs[i], (uint8_t)(pairs[i] >> 8)))
      return 1;

  return 0;
}
