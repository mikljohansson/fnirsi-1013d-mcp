//----------------------------------------------------------------------------------------------------------------------------------
// trig_ui.c - the trigger menu's Type row as data (trig_ui.h)
//----------------------------------------------------------------------------------------------------------------------------------

#include "trig_ui.h"

tui_state trig_ui;

//Ladder step i: m x 10^e x 10 ns, m = 1, 2, 5, in 5 ns units (10 ns = 2)
static const uint8 tui_mant[3] = { 2, 4, 10 };

static const char *const tui_texts[TUI_STEPS] =
{
  "10ns", "20ns", "50ns", "100ns", "200ns", "500ns", "1us", "2us", "5us", "10us", "20us", "50us", "100us", "200us",
  "500us", "1ms", "2ms", "5ms", "10ms", "20ms", "50ms", "100ms", "200ms", "500ms", "1s", "2s", "5s", "10s"
};

void tui_defaults(tui_state *s)
{
  s->type    = TUI_EDGE;
  s->longer  = 0;
  s->width_i = 7;                     //2us
  s->idle_i  = 1 + 11;                //50us
  s->n       = 2;
}

uint32 tui_step_t5(uint32 i)
{
  uint32 t = tui_mant[i % 3], e = i / 3;

  while(e--)
    t *= 10;

  return t;
}

uint32 tui_tw5(uint32 divider, int wide, int ext)
{
  if(ext)
    return 1;

  return wide ? 2 * (divider / 2 + 1) : 2 * (divider + 1);
}

uint32 tui_words(uint32 i, uint32 tw5)
{
  //t5 <= 2^31 and tw5 >= 1: no overflow in the rounding term
  return (tui_step_t5(i) + tw5 / 2) / tw5;
}

void tui_range(uint32 tw5, uint32 min_words, uint32 *lo, uint32 *hi)
{
  uint32 i, l = 0, h = TUI_STEPS - 1;

  for(i = 0; i < TUI_STEPS; i++)
    if(tui_words(i, tw5) >= min_words)
    {
      l = i;
      break;
    }

  for(i = TUI_STEPS; i-- > 0;)
    if(tui_words(i, tw5) <= TUI_WORDS_MAX)
    {
      h = i;
      break;
    }

  if(h < l)
    h = l;

  *lo = l;
  *hi = h;
}

uint32 tui_nearest(uint32 words, uint32 tw5, uint32 lo, uint32 hi)
{
  uint64 t = (uint64)words * tw5;
  uint32 i, best = lo;
  uint64 bestd = ~(uint64)0;

  for(i = lo; i <= hi; i++)
  {
    uint64 s = tui_step_t5(i), d = (s > t) ? s - t : t - s;

    if(d < bestd)
    {
      bestd = d;
      best  = i;
    }
  }

  return best;
}

static uint32 tui_clamp(uint32 v, uint32 lo, uint32 hi)
{
  return (v < lo) ? lo : ((v > hi) ? hi : v);
}

int tui_rate(tui_state *s, uint32 tw5)
{
  uint32 lo, hi, v;

  if(s->type == TUI_PULSE)
  {
    tui_range(tw5, TUI_WIDTH_MIN, &lo, &hi);
    v = tui_clamp(s->width_i, lo, hi);

    if(v == s->width_i)
      return TUI_SNAP_NONE;

    s->width_i = (uint8)v;
    return TUI_SNAP_WIDTH;
  }

  if((s->type == TUI_NTH) && (s->idle_i != TUI_IDLE_OFF))
  {
    tui_range(tw5, TUI_IDLE_MIN, &lo, &hi);
    v = tui_clamp(s->idle_i - 1u, lo, hi) + 1;

    if(v == s->idle_i)
      return TUI_SNAP_NONE;

    s->idle_i = (uint8)v;
    return TUI_SNAP_IDLE;
  }

  return TUI_SNAP_NONE;
}

void tui_hw(const tui_state *s, uint32 tw5, uint8 *type, uint8 *longer, uint32 *n, uint32 *h)
{
  *longer = 0;
  *n      = 0;
  *h      = 0;

  switch(s->type)
  {
    case TUI_PULSE:
      *type   = TUI_PULSE;
      *longer = s->longer ? 1 : 0;
      *n      = tui_words(s->width_i, tw5);
      break;

    case TUI_NTH:
      *type = TUI_NTH;
      *n    = s->n;
      if(s->idle_i != TUI_IDLE_OFF)
        *h = tui_words(s->idle_i - 1u, tw5);
      break;

    default:
      *type = TUI_EDGE;
      break;
  }
}

void tui_from_hw(tui_state *s, uint8 type, uint8 longer, uint32 n, uint32 h, uint32 tw5)
{
  uint32 lo, hi;

  s->type = (type > TUI_TIMEOUT) ? TUI_TIMEOUT : type;

  if(type == TUI_PULSE)
  {
    s->longer = longer ? 1 : 0;
    tui_range(tw5, TUI_WIDTH_MIN, &lo, &hi);
    s->width_i = (uint8)tui_nearest(n, tw5, lo, hi);
  }
  else if(type == TUI_NTH)
  {
    s->n = (uint16)((n < TUI_N_MIN) ? TUI_N_MIN : ((n > TUI_N_MAX) ? TUI_N_MAX : n));

    if(h)
    {
      tui_range(tw5, TUI_IDLE_MIN, &lo, &hi);
      s->idle_i = (uint8)(1 + tui_nearest(h, tw5, lo, hi));
    }
    else
      s->idle_i = TUI_IDLE_OFF;
  }
}

void tui_set_type(tui_state *s, uint8 type, uint32 tw5)
{
  s->type = (type > TUI_NTH) ? TUI_EDGE : type;
  (void)tui_rate(s, tw5);
}

int tui_step_width(tui_state *s, int dir, uint32 tw5)
{
  uint32 lo, hi, v;

  tui_range(tw5, TUI_WIDTH_MIN, &lo, &hi);
  v = tui_clamp(s->width_i, lo, hi);

  if(dir > 0)
  {
    if(v >= hi)
      return 0;
    v++;
  }
  else
  {
    if(v <= lo)
      return 0;
    v--;
  }

  s->width_i = (uint8)v;
  return 1;
}

int tui_step_idle(tui_state *s, int dir, uint32 tw5)
{
  uint32 lo, hi, v = s->idle_i;

  tui_range(tw5, TUI_IDLE_MIN, &lo, &hi);

  if(v != TUI_IDLE_OFF)
    v = tui_clamp(v - 1, lo, hi) + 1;

  if(dir > 0)
  {
    if(v == TUI_IDLE_OFF)
      v = lo + 1;
    else if(v >= hi + 1)
      return 0;
    else
      v++;
  }
  else
  {
    if(v == TUI_IDLE_OFF)
      return 0;
    v = (v <= lo + 1) ? TUI_IDLE_OFF : v - 1;
  }

  s->idle_i = (uint8)v;
  return 1;
}

int tui_step_n(tui_state *s, int dir)
{
  if(dir > 0)
  {
    if(s->n >= TUI_N_MAX)
      return 0;
    s->n++;
  }
  else
  {
    if(s->n <= TUI_N_MIN)
      return 0;
    s->n--;
  }

  return 1;
}

void tui_set_n(tui_state *s, int32 n)
{
  s->n = (uint16)((n < (int32)TUI_N_MIN) ? TUI_N_MIN : ((n > (int32)TUI_N_MAX) ? TUI_N_MAX : (uint32)n));
}

uint16 tui_word0(const tui_state *s)
{
  return (uint16)(0xA000 | ((s->type <= TUI_NTH) ? s->type : TUI_EDGE) | ((s->longer ? 1 : 0) << 2) |
                  ((s->width_i & 31) << 3) | ((s->idle_i & 31) << 8));
}

uint16 tui_word1(const tui_state *s)
{
  return s->n;
}

int tui_restore(tui_state *s, uint16 w0, uint16 w1)
{
  uint32 type = w0 & 3, width = (w0 >> 3) & 31, idle = (w0 >> 8) & 31;

  tui_defaults(s);

  if(((w0 & 0xE000) != 0xA000) || (type > TUI_NTH) || (width >= TUI_STEPS) || (idle > TUI_STEPS) || (w1 < TUI_N_MIN))
    return 0;

  s->type    = (uint8)type;
  s->longer  = (uint8)((w0 >> 2) & 1);
  s->width_i = (uint8)width;
  s->idle_i  = (uint8)idle;
  s->n       = w1;
  return 1;
}

const char *tui_step_text(uint32 i)
{
  return (i < TUI_STEPS) ? tui_texts[i] : "";
}

const char *tui_idle_text(uint32 idle_i)
{
  return (idle_i == TUI_IDLE_OFF) ? "Off" : tui_step_text(idle_i - 1);
}

char *tui_n_text(char *p, uint32 n)
{
  char  d[6];
  int   k = 0;

  do
  {
    d[k++] = (char)('0' + n % 10);
    n /= 10;
  }
  while(n && (k < 5));

  while(k)
    *p++ = d[--k];
  *p = 0;

  return p;
}

//----------------------------------------------------------------------------------------------------------------------------------
// The glyphs: the mockups' polylines (rows.py glyph()). A 2 px stroke along a segment from a to b on whole-pixel
// coordinates covers the pixel band beside the line (y - 1 and y for a horizontal line at y, the columns a.x .. b.x - 1)
// and the right-angle joins fill the 2 x 2 pixel square at each inner vertex. Nth is not the mockup's one polyline
// (0,12 2,12 2,8 4,8 4,12 6,12 6,8 8,8 8,12 10,12 10,2 13,2): at a 2 px pitch its 2 px strokes merge into one block
// (TrigMenuNth.png shows the block), so the two short edges stand as strokes of their own, three pixels apart, each a
// rising step with a stub, on the baseline of the tall edge. TUI_GLYPH_BREAK separates the strokes.

#define TUI_GLYPH_BREAK 0xFF

static const uint8 tui_gl_rise[]  = { 0, 12,  6, 12,  6, 2,  13, 2 };
static const uint8 tui_gl_fall[]  = { 0, 2,  6, 2,  6, 12,  13, 12 };
static const uint8 tui_gl_pulsp[] = { 0, 12,  3, 12,  3, 2,  10, 2,  10, 12,  13, 12 };
static const uint8 tui_gl_pulsn[] = { 0, 2,  3, 2,  3, 12,  10, 12,  10, 2,  13, 2 };
static const uint8 tui_gl_nth[]   = { 0, 12,  10, 12,  10, 2,  13, 2,  TUI_GLYPH_BREAK, TUI_GLYPH_BREAK,
                                      2, 12,  2, 9,  4, 9,  TUI_GLYPH_BREAK, TUI_GLYPH_BREAK,
                                      6, 12,  6, 9,  8, 9 };

uint32 tui_glyph_kind(uint8 type, uint8 edge)
{
  switch(type)
  {
    case TUI_PULSE:
      return edge ? TUI_GLYPH_PULSE_N : TUI_GLYPH_PULSE_P;

    case TUI_NTH:
      return TUI_GLYPH_NTH;

    default:
      return edge ? TUI_GLYPH_FALL : TUI_GLYPH_RISE;
  }
}

uint32 tui_glyph_rects(uint32 kind, tui_rect *out)
{
  const uint8 *p;
  uint32       np, i, n = 0;

  switch(kind)
  {
    case TUI_GLYPH_FALL:    p = tui_gl_fall;  np = sizeof(tui_gl_fall) / 2;  break;
    case TUI_GLYPH_PULSE_P: p = tui_gl_pulsp; np = sizeof(tui_gl_pulsp) / 2; break;
    case TUI_GLYPH_PULSE_N: p = tui_gl_pulsn; np = sizeof(tui_gl_pulsn) / 2; break;
    case TUI_GLYPH_NTH:     p = tui_gl_nth;   np = sizeof(tui_gl_nth) / 2;   break;
    default:                p = tui_gl_rise;  np = sizeof(tui_gl_rise) / 2;  break;
  }

  for(i = 0; i + 1 < np; i++)
  {
    uint32    x0 = p[2 * i], y0 = p[2 * i + 1], x1 = p[2 * i + 2], y1 = p[2 * i + 3];
    tui_rect *r;

    //A break ends one stroke; the point after it starts the next
    if((x0 == TUI_GLYPH_BREAK) || (x1 == TUI_GLYPH_BREAK))
      continue;

    r = &out[n++];

    if(y0 == y1)
    {
      r->x = (uint8)((x0 < x1) ? x0 : x1);
      r->w = (uint8)((x0 < x1) ? x1 - x0 : x0 - x1);
      r->y = (uint8)(y0 - 1);
      r->h = 2;
    }
    else
    {
      r->x = (uint8)(x0 - 1);
      r->w = 2;
      r->y = (uint8)((y0 < y1) ? y0 : y1);
      r->h = (uint8)((y0 < y1) ? y1 - y0 : y0 - y1);
    }

    //The join at the segment's end: the vertex is followed by a perpendicular segment of the same stroke
    if((i + 2 < np) && (p[2 * i + 4] != TUI_GLYPH_BREAK))
    {
      r    = &out[n++];
      r->x = (uint8)(x1 - 1);
      r->y = (uint8)(y1 - 1);
      r->w = 2;
      r->h = 2;
    }
  }

  return n;
}
