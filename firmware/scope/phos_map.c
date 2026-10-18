//----------------------------------------------------------------------------------------------------------------------------------
// phos_map.c - digital phosphor hit-count map (see phos_map.h). Pure C, host-tested (host_tests/test_phosphor.c).
//----------------------------------------------------------------------------------------------------------------------------------

#include "phos_map.h"

#include <string.h>

//exp(-ln 16 / (1000 T)) in Q24 for T = 0.2 s, 1 s, 5 s; infinite = 1
const uint32_t phos_persist_k1[PHOS_PERSIST_COUNT] = { 16546239u, 16730764u, 16767915u, PHOS_K_ONE };

uint32_t phos_k_dt(uint32_t k1, uint32_t dt)
{
  uint64_t r = PHOS_K_ONE, b = k1;

  if(k1 >= PHOS_K_ONE)
    return PHOS_K_ONE;

  //Square and multiply, Q24 (products below 2^48)
  while(dt)
  {
    if(dt & 1)
      r = (r * b + (1u << 23)) >> 24;

    b  = (b * b + (1u << 23)) >> 24;
    dt >>= 1;
  }

  return (uint32_t)r;
}

uint32_t phos_inc(uint32_t k1, uint32_t interval_ms)
{
  uint32_t k, inc;

  if(k1 >= PHOS_K_ONE)
    return 1;

  if(interval_ms < 1)
    interval_ms = 1;

  k   = phos_k_dt(k1, interval_ms);
  inc = (uint32_t)((319ull * (PHOS_K_ONE - k) + (1u << 23)) >> 24);

  return (inc < 1) ? 1 : (inc > 255) ? 255 : inc;
}

uint32_t phos_interval_update(uint32_t avg_ms, uint32_t interval_ms)
{
  int32_t diff;

  if(interval_ms > PHOS_INTERVAL_MAX)
    interval_ms = PHOS_INTERVAL_MAX;

  diff = (int32_t)interval_ms - (int32_t)avg_ms;

  //1/8 of the difference, rounded away from zero so the estimate reaches the value
  avg_ms = (uint32_t)((int32_t)avg_ms + ((diff >= 0) ? ((diff + 7) >> 3) : -((-diff + 7) >> 3)));

  return avg_ms ? avg_ms : 1;
}

//----------------------------------------------------------------------------------------------------------------------------------

void phos_clear(phos_map *m)
{
  uint32_t x;

  for(x = 0; x < PHOS_W; x++)
  {
    uint32_t e = m->ext[x];

    if(e)
    {
      memset(m->cells + x * PHOS_H + (e & 0xFFFF), 0, (e >> 16) - (e & 0xFFFF));
      m->ext[x] = 0;
    }
  }
}

//Saturating add of four bytes (ARMv5TE has no UQADD8): low 7 bits per byte without carries, bit 7 and its carry out
//rebuilt, saturated bytes forced to 0xFF
static inline uint32_t sat_add4(uint32_t a, uint32_t b)
{
  uint32_t s = (a & 0x7F7F7F7Fu) + (b & 0x7F7F7F7Fu);
  uint32_t c = ((a & b) | ((a | b) & s)) & 0x80808080u;

  return (s ^ ((a ^ b) & 0x80808080u)) | ((c << 1) - (c >> 7));
}

//Add inc to rows lo..hi of column x (clipped to the map) and widen the column's extent
static uint32_t emit(phos_map *m, int32_t x, int32_t lo, int32_t hi, uint32_t inc4)
{
  uint32_t *w, i, last, e, ne, mlo, mhi;

  if(lo < 0)
    lo = 0;

  if(hi > PHOS_H - 1)
    hi = PHOS_H - 1;

  if(lo > hi)
    return 0;

  w    = (uint32_t *)(void *)(m->cells + (uint32_t)x * PHOS_H);
  i    = (uint32_t)lo >> 2;
  last = (uint32_t)hi >> 2;
  mlo  = 0xFFFFFFFFu << (8 * (lo & 3));
  mhi  = 0xFFFFFFFFu >> (8 * (3 - (hi & 3)));

  if(i == last)
    w[i] = sat_add4(w[i], inc4 & mlo & mhi);
  else
  {
    w[i] = sat_add4(w[i], inc4 & mlo);

    for(i++; i < last; i++)
      w[i] = sat_add4(w[i], inc4);

    w[last] = sat_add4(w[last], inc4 & mhi);
  }

  e = m->ext[x];

  if(!e)
    ne = (uint32_t)lo | ((uint32_t)(hi + 1) << 16);
  else
  {
    uint32_t elo = e & 0xFFFF, eend = e >> 16;

    if((uint32_t)lo < elo)
      elo = (uint32_t)lo;

    if((uint32_t)hi + 1 > eend)
      eend = (uint32_t)hi + 1;

    ne = elo | (eend << 16);
  }

  if(ne != e)
    m->ext[x] = ne;

  return (uint32_t)(hi - lo + 1);
}

//y on the line (x0, y0) .. (x1, y1) at xc (Q16 x, x0 < x1)
static int32_t interp(int32_t y0, int32_t y1, int32_t x0, int32_t x1, int32_t xc)
{
  if(xc <= x0)
    return y0;

  if(xc >= x1)
    return y1;

  return y0 + (int32_t)(((int64_t)(y1 - y0) * (xc - x0)) / (x1 - x0));
}

//Widen the run [lo, hi] towards the next column's point e, stopping one row short of it
#define EXTEND(lo, hi, e)  do { if((e) > (hi) + 1) (hi) = (e) - 1; else if((e) < (lo) - 1) (lo) = (e) + 1; } while(0)

uint32_t phos_add_record(phos_map *m, const uint8_t *codes, uint32_t n, const uint16_t *lut,
                         int32_t x0_q16, int32_t dx_q16, int32_t xmin, int32_t xmax, uint32_t inc)
{
  const uint32_t *wp;
  uint32_t word, sh, i, code, touches = 0, inc4;
  int32_t  x, cur, lo, hi, last, xprev;

  if(!n || (dx_q16 <= 0) || !inc)
    return 0;

  if(inc > 255)
    inc = 255;

  inc4 = inc * 0x01010101u;

  if(xmin < 0)
    xmin = 0;

  if(xmax > PHOS_W - 1)
    xmax = PHOS_W - 1;

  if(xmin > xmax)
    return 0;

  //Samples come from uncached DDR: one word read per four samples
  wp   = (const uint32_t *)(const void *)((uintptr_t)codes & ~(uintptr_t)3);
  sh   = (uint32_t)((uintptr_t)codes & 3) * 8;
  word = *wp++;

#define NEXT_CODE(v)  do { if(sh == 32) { word = *wp++; sh = 0; } (v) = (word >> sh) & 0xFF; sh += 8; } while(0)

  NEXT_CODE(code);
  x     = x0_q16;
  xprev = x;
  cur   = x >> 16;
  lo    = hi = last = lut[code];

  for(i = 1; i < n; i++)
  {
    int32_t ny, c, e, cc;

    NEXT_CODE(code);
    ny = lut[code];
    x += dx_q16;
    c  = x >> 16;

    if(c == cur)
    {
      if(ny < lo)
        lo = ny;

      if(ny > hi)
        hi = ny;

      last  = ny;
      xprev = x;
      continue;
    }

    //Leaving column cur: join it to the next column's point
    e = (c == cur + 1) ? ny : interp(last, ny, xprev, x, ((cur + 1) << 16) + 0x8000);
    EXTEND(lo, hi, e);

    if((cur >= xmin) && (cur <= xmax))
      touches += emit(m, cur, lo, hi, inc4);

    //Columns between two samples (zoomed in): the line's points at the column centres
    for(cc = cur + 1; (cc < c) && (cc <= xmax); cc++)
    {
      int32_t en = (cc + 1 == c) ? ny : interp(last, ny, xprev, x, ((cc + 1) << 16) + 0x8000);

      lo = hi = e;
      EXTEND(lo, hi, en);

      if(cc >= xmin)
        touches += emit(m, cc, lo, hi, inc4);

      e = en;
    }

    cur   = c;
    lo    = hi = last = ny;
    xprev = x;

    if(cur > xmax)
      break;
  }

#undef NEXT_CODE

  if((cur >= xmin) && (cur <= xmax))
    touches += emit(m, cur, lo, hi, inc4);

  return touches;
}

uint32_t phos_add_line(phos_map *m, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int skip_first, uint32_t inc)
{
  int32_t  dx = (x2 > x1) ? x2 - x1 : x1 - x2, sx = (x1 < x2) ? 1 : -1;
  int32_t  dy = (y2 > y1) ? y1 - y2 : y2 - y1, sy = (y1 < y2) ? 1 : -1;
  int32_t  err = dx + dy, x = x1, y = y1, rc = -1, rlo = 0, rhi = 0;
  uint32_t touches = 0, inc4;
  int      first = 1;

  if(!inc)
    return 0;

  inc4 = ((inc > 255) ? 255 : inc) * 0x01010101u;

  for(;;)
  {
    if(!(first && skip_first))
    {
      //Pixels in the same column form one run
      if((x == rc) && (rc >= 0))
      {
        if(y < rlo)
          rlo = y;

        if(y > rhi)
          rhi = y;
      }
      else
      {
        if((rc >= 0) && (rc < PHOS_W))
          touches += emit(m, rc, rlo, rhi, inc4);

        rc  = x;
        rlo = rhi = y;
      }
    }

    first = 0;

    if((x == x2) && (y == y2))
      break;

    {
      int32_t e2 = 2 * err;

      if(e2 >= dy)
      {
        err += dy;
        x   += sx;
      }

      if(e2 <= dx)
      {
        err += dx;
        y   += sy;
      }
    }
  }

  if((rc >= 0) && (rc < PHOS_W))
    touches += emit(m, rc, rlo, rhi, inc4);

  return touches;
}

//----------------------------------------------------------------------------------------------------------------------------------

uint32_t phos_render(phos_map *m, const phos_map *under, uint16_t *dst, uint32_t stride, const uint16_t *pal,
                     uint32_t k16, uint32_t seed)
{
  uint32_t x, pixels = 0;
  int      decay = (k16 < 65536);

  for(x = 0; x < PHOS_W; x++)
  {
    uint32_t        e = m->ext[x], i, last, d, nlo = 0, nend = 0, ne, ui = 1, ulast = 0;
    uint32_t       *w;
    const uint32_t *uw = 0;
    uint16_t       *col = dst + x;

    if(!e)
      continue;

    w    = (uint32_t *)(void *)(m->cells + x * PHOS_H);
    i    = (e & 0xFFFF) >> 2;
    last = ((e >> 16) - 1) >> 2;
    d    = (seed + 25033u * x) & 0xFFFF;

    if(under && under->ext[x])
    {
      uint32_t ue = under->ext[x];

      uw    = (const uint32_t *)(const void *)(under->cells + x * PHOS_H);
      ui    = (ue & 0xFFFF) >> 2;
      ulast = ((ue >> 16) - 1) >> 2;
    }

    for(; i <= last; i++)
    {
      uint32_t v = w[i], nv = v, u, j;
      uint16_t *p;

      if(!v)
        continue;

      u = (uw && (i >= ui) && (i <= ulast)) ? uw[i] : 0;
      p = col + 4 * i * stride;

      if(decay)
        nv = 0;

      for(j = 0; j < 32; j += 8, p += stride)
      {
        uint32_t c = (v >> j) & 0xFF, n = c, ub;

        if(!c)
          continue;

        //count * k16 + d < 2^24: no overflow
        if(decay)
        {
          n   = (c * k16 + d) >> 16;
          nv |= n << j;
        }

        ub = (u >> j) & 0xFF;

        if(!ub || (n > ub))
        {
          *p = pal[c];
          pixels++;
        }
      }

      if(nv != v)
        w[i] = nv;

      if(!nv)
        continue;

      if(!nend)
        nlo = 4 * i;

      nend = 4 * i + 4;
    }

    //The extent shrinks to the lit words (word granularity: the rows around the old ends in a lit word are zero)
    ne = nend ? (nlo | (nend << 16)) : 0;

    if(ne != e)
      m->ext[x] = ne;
  }

  return pixels;
}

uint32_t phos_lit(const phos_map *m)
{
  uint32_t x, y, n = 0;

  for(x = 0; x < PHOS_W; x++)
  {
    uint32_t e = m->ext[x];

    for(y = e & 0xFFFF; y < (e >> 16); y++)
      if(m->cells[x * PHOS_H + y])
        n++;
  }

  return n;
}

//----------------------------------------------------------------------------------------------------------------------------------

uint32_t phos_isqrt(uint32_t v)
{
  uint32_t r = 0, b = 1u << 30;

  while(b > v)
    b >>= 2;

  while(b)
  {
    if(v >= r + b)
    {
      v -= r + b;
      r  = (r >> 1) + b;
    }
    else
      r >>= 1;

    b >>= 2;
  }

  return r;
}

static uint16_t rgb565(uint32_t r, uint32_t g, uint32_t b)
{
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

//Index on the square-root curve: 15 for one hit, 255 at saturation
static uint32_t level(uint32_t c)
{
  return phos_isqrt(c * 255);
}

void phos_palette_mono(uint16_t *pal, uint32_t rgb)
{
  uint32_t c, r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;

  pal[0] = 0;

  for(c = 1; c < 256; c++)
  {
    uint32_t br = 48 + (207 * level(c) + 127) / 255;

    pal[c] = rgb565((r * br) / 255, (g * br) / 255, (b * br) / 255);
  }
}

void phos_palette_heat(uint16_t *pal)
{
  static const uint8_t stop[][4] = { { 0, 0, 0, 96 }, { 50, 0, 0, 255 }, { 100, 0, 200, 255 }, { 150, 0, 255, 0 },
                                     { 195, 255, 255, 0 }, { 235, 255, 0, 0 }, { 255, 255, 255, 255 } };
  uint32_t c, s = 0;

  pal[0] = 0;

  for(c = 1; c < 256; c++)
  {
    uint32_t l = level(c), span, t, ch[3], j;

    while((s < sizeof(stop) / sizeof(stop[0]) - 2) && (l > stop[s + 1][0]))
      s++;

    span = stop[s + 1][0] - stop[s][0];
    t    = l - stop[s][0];

    for(j = 0; j < 3; j++)
      ch[j] = (uint32_t)((int32_t)stop[s][j + 1] + (((int32_t)stop[s + 1][j + 1] - (int32_t)stop[s][j + 1]) * (int32_t)t) / (int32_t)span);

    pal[c] = rgb565(ch[0], ch[1], ch[2]);
  }
}
