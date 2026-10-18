// test_phosphor.c - the phosphor view's hit-count map (phos_map.c), run natively: rasteriser, cost model (cells
// touched per record), decay to the persistence time, rate-normalised increments, render rules, palettes, determinism.

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "phos_map.h"
#include "test_util.h"

static uint8_t  cells_a[PHOS_W * PHOS_H] __attribute__((aligned(8)));
static uint8_t  cells_b[PHOS_W * PHOS_H] __attribute__((aligned(8)));
static uint32_t ext_a[PHOS_W], ext_b[PHOS_W];
static phos_map ma = { cells_a, ext_a }, mb = { cells_b, ext_b };

static uint8_t  codes[16384 + 8] __attribute__((aligned(8)));
static uint16_t lut[256], pal[256], pal2[256];
static uint16_t fb[PHOS_H * 800], fb2[PHOS_H * 800];
static uint8_t  zero[PHOS_W * PHOS_H];

static void reset(void)
{
  memset(cells_a, 0, sizeof(cells_a));
  memset(cells_b, 0, sizeof(cells_b));
  memset(ext_a, 0, sizeof(ext_a));
  memset(ext_b, 0, sizeof(ext_b));
}

static uint8_t cell(const phos_map *m, int x, int y) { return m->cells[x * PHOS_H + y]; }

//Identity-like LUT: code c -> row c + 72 (0..255 -> 72..327)
static void lut_offset(void)
{
  int c;

  for(c = 0; c < 256; c++)
    lut[c] = (uint16_t)(c + 72);
}

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }

//dx for n samples across the 728 columns
static int32_t dx_span(uint32_t n) { return (int32_t)(((int64_t)PHOS_W << 16) / n); }

static void test_flat(void)
{
  uint32_t t, x;
  int ok = 1;

  reset();
  lut_offset();
  memset(codes, 128, 3000);

  t = phos_add_record(&ma, codes, 3000, lut, 0, dx_span(3000), 0, PHOS_W - 1, 10);
  T_EQ(t, PHOS_W);                                      //cost model: one cell per column
  T_EQ(phos_lit(&ma), PHOS_W);

  for(x = 0; x < PHOS_W; x++)
    if((cell(&ma, x, 200) != 10) || (ext_a[x] != (200u | (201u << 16))))
      ok = 0;
  T_TRUE(ok);
  T_EQ(cell(&ma, 5, 199), 0);
  T_EQ(cell(&ma, 5, 201), 0);

  //Saturation: 26 x 10 = 260 -> 255
  for(x = 0; x < 25; x++)
    phos_add_record(&ma, codes, 3000, lut, 0, dx_span(3000), 0, PHOS_W - 1, 10);
  T_EQ(cell(&ma, 100, 200), 255);
  T_EQ(cell(&ma, 100, 201), 0);

  //inc 0 adds nothing
  T_EQ(phos_add_record(&ma, codes, 3000, lut, 0, dx_span(3000), 0, PHOS_W - 1, 0), 0);
}

static void test_square(void)
{
  uint32_t i, t, y;
  int ok = 1;

  reset();
  lut_offset();

  //4 samples per column, 8 edges: high code 228 (row 300), low code 28 (row 100)
  for(i = 0; i < 2912; i++)
    codes[i] = ((i / 364) & 1) ? 28 : 228;

  t = phos_add_record(&ma, codes, 2912, lut, 0, 1 << 14, 0, PHOS_W - 1, 1);
  //Each edge adds a run of |dy| - 1 = 199 rows in the column before it
  T_EQ(t, PHOS_W + 7 * 199);
  T_EQ(phos_lit(&ma), PHOS_W + 7 * 199);

  //The first edge: sample 364 starts column 91; column 90 runs from row 300 down to row 101
  for(y = 101; y <= 300; y++)
    if(cell(&ma, 90, y) != 1)
      ok = 0;
  T_TRUE(ok);
  T_EQ(cell(&ma, 90, 100), 0);
  T_EQ(cell(&ma, 91, 100), 1);
  T_EQ(cell(&ma, 91, 101), 0);
}

static void test_zoomed(void)
{
  uint32_t t, y;
  int ok = 1;

  reset();

  //Two samples 10 columns apart: row 0 -> row 100, every row once (a joined line), 11 columns
  lut[0] = 0;
  lut[1] = 100;
  codes[0] = 0;
  codes[1] = 1;
  t = phos_add_record(&ma, codes, 2, lut, 0, 10 << 16, 0, PHOS_W - 1, 1);
  T_EQ(t, 101);
  T_EQ(phos_lit(&ma), 101);

  for(y = 0; y <= 100; y++)
  {
    uint32_t x, hits = 0;

    for(x = 0; x <= 10; x++)
      hits += cell(&ma, x, y);

    if(hits != 1)
      ok = 0;
  }
  T_TRUE(ok);
  T_EQ(cell(&ma, 0, 0), 1);
  T_EQ(cell(&ma, 0, 14), 1);         //column 0 runs to just below the line's point at column 1's centre (15)
  T_EQ(cell(&ma, 1, 15), 1);
  T_EQ(cell(&ma, 10, 100), 1);
  T_EQ(cell(&ma, 11, 100), 0);

  //Clipping to xmin..xmax, a start left of the map
  reset();
  t = phos_add_record(&ma, codes, 2, lut, -(5 << 16), 10 << 16, 2, 4, 1);
  T_EQ(t, 25);                        //columns 2..4 of a line 10 rows per column, the last one up to the sample
  T_EQ(ext_a[1], 0);
  T_EQ(ext_a[5], 0);
  T_TRUE(ext_a[2] && ext_a[4]);
}

static void test_cost_worst(void)
{
  uint32_t i, t;

  reset();

  //Full-scale alternation, several samples per column: every column spans all rows
  lut[0] = 0;
  lut[1] = PHOS_H - 1;
  for(i = 0; i < 4096; i++)
    codes[i] = (uint8_t)(i & 1);

  t = phos_add_record(&ma, codes, 4096, lut, 0, dx_span(4096), 0, PHOS_W - 1, 1);
  T_EQ(t, PHOS_W * PHOS_H);
  T_EQ(phos_lit(&ma), PHOS_W * PHOS_H);

  //Render without decay: one pixel per lit cell
  phos_palette_mono(pal, 0xFFFF00, 255);
  T_EQ(phos_render(&ma, 0, fb, 800, pal, 256, 0), PHOS_W * PHOS_H);

  //Unaligned start: the word reader takes the right bytes
  reset();
  lut_offset();
  memset(codes, 128, 64);
  codes[3] = 0;
  t = phos_add_record(&ma, codes + 3, 4, lut, 0, 1 << 16, 0, PHOS_W - 1, 1);
  T_EQ(cell(&ma, 0, 72), 1);
  T_EQ(cell(&ma, 1, 200), 1);
  T_EQ(t, 128 + 1 + 1 + 1);           //rows 72..199 in column 0, then one per column
}

static void feed_random(phos_map *m, uint32_t seed, int records)
{
  int r;
  uint32_t i;

  rng = seed;
  lut_offset();

  for(r = 0; r < records; r++)
  {
    for(i = 0; i < 3000; i++)
      codes[i] = (uint8_t)(128 + (int)(rnd() % 61) - 30 + (((i / 150) & 1) ? 40 : -40));

    phos_add_record(m, codes, 3000, lut, (int32_t)(rnd() % 65536) - 32768, dx_span(3000), 0, PHOS_W - 1, 1 + rnd() % 60);
  }
}

static void test_determinism(void)
{
  uint32_t s = 0, t;

  reset();
  feed_random(&ma, 7, 20);
  feed_random(&mb, 7, 20);
  T_MEM(cells_a, cells_b, sizeof(cells_a));
  T_MEM(ext_a, ext_b, sizeof(ext_a));

  phos_palette_heat(pal, 255);
  memset(fb, 0, sizeof(fb));
  memset(fb2, 0, sizeof(fb2));

  for(t = 0; t < 10; t++, s = phos_seed_next(s))
  {
    phos_render(&ma, 0, fb, 800, pal, phos_persist_k[PHOS_PERSIST_1S], s);
    phos_render(&mb, 0, fb2, 800, pal, phos_persist_k[PHOS_PERSIST_1S], s);
  }
  T_MEM(cells_a, cells_b, sizeof(cells_a));
  T_MEM(ext_a, ext_b, sizeof(ext_a));
  T_MEM(fb, fb2, sizeof(fb));

  //A different input gives a different map
  reset();
  feed_random(&ma, 7, 20);
  feed_random(&mb, 8, 20);
  T_TRUE(memcmp(cells_a, cells_b, sizeof(cells_a)) != 0);

  //Clear: nothing lit, no extents
  phos_clear(&ma);
  T_EQ(phos_lit(&ma), 0);
  T_MEM(cells_a, zero, sizeof(cells_a));
  T_EQ(ext_a[100] | ext_a[0] | ext_a[PHOS_W - 1], 0);
}

//Mean count over the 728 cells of row 200 after `ticks` ticks from `start`
static uint32_t decay_mean_x100(uint32_t k, uint32_t start, uint32_t ticks)
{
  uint32_t x, t, s = 0, sum = 0;

  reset();
  lut_offset();
  memset(codes, 128, 3000);
  phos_add_record(&ma, codes, 3000, lut, 0, dx_span(3000), 0, PHOS_W - 1, start);

  for(t = 0; t < ticks; t++, s = phos_seed_next(s))
    phos_render(&ma, 0, fb, 800, pal, k, s);

  for(x = 0; x < PHOS_W; x++)
    sum += cell(&ma, x, 200);

  return (sum * 100) / PHOS_W;
}

static void test_decay(void)
{
  uint32_t m, t, s, x;
  int empty = 1;

  T_EQ(phos_persist_k[PHOS_PERSIST_INF], 256);
  T_EQ(phos_k_ticks(223, 1), 223);
  T_EQ(phos_k_ticks(128, 2), 64);
  T_EQ(phos_k_ticks(256, 4), 256);

  //Persistence T: full (255) falls to 1/16 (15.9) in T, +-10 %
  m = decay_mean_x100(phos_persist_k[PHOS_PERSIST_02S], 255, 200 / PHOS_TICK_MS);
  T_TRUE((m >= 1435) && (m <= 1755));
  m = decay_mean_x100(phos_persist_k[PHOS_PERSIST_1S], 255, 1000 / PHOS_TICK_MS);
  T_TRUE((m >= 1435) && (m <= 1755));
  m = decay_mean_x100(phos_persist_k[PHOS_PERSIST_5S], 255, 5000 / PHOS_TICK_MS);
  T_TRUE((m >= 1435) && (m <= 1755));
  printf("decay: 1/16 point at T: %u.%02u (0.2 s), ", decay_mean_x100(128, 255, 4) / 100, decay_mean_x100(128, 255, 4) % 100);
  printf("%u.%02u (1 s), ", decay_mean_x100(223, 255, 20) / 100, decay_mean_x100(223, 255, 20) % 100);
  printf("%u.%02u (5 s), expected 15.94\n", decay_mean_x100(249, 255, 100) / 100, decay_mean_x100(249, 255, 100) % 100);

  //Dithered: small counts decay exponentially on average (4 / e after one time constant, 256 / 7 ticks at k = 249)
  m = decay_mean_x100(249, 4, 37);
  T_TRUE((m >= 125) && (m <= 170));

  //k^n for 4 ticks at once ~ 4 single ticks
  T_TRUE(decay_mean_x100(phos_k_ticks(223, 4), 255, 5) * 100 / decay_mean_x100(223, 255, 20) >= 85);

  //Infinite: no change
  T_EQ(decay_mean_x100(256, 77, 50), 7700);

  //Everything fades to zero and the extents empty
  decay_mean_x100(249, 255, 1);
  for(t = 0, s = 0; t < 2000; t++, s = phos_seed_next(s))
    phos_render(&ma, 0, fb, 800, pal, 249, s);
  for(x = 0; x < PHOS_W; x++)
    if(ext_a[x])
      empty = 0;
  T_TRUE(empty);
  T_EQ(phos_lit(&ma), 0);
  T_EQ(phos_render(&ma, 0, fb, 800, pal, 249, s), 0);
}

static void test_increment(void)
{
  uint32_t rate = PHOS_RATE_INIT, i, t, s = 0, inc, sum10 = 0;
  int      full = 1;

  T_EQ(phos_inc(256, 384), 1);
  T_EQ(phos_inc(223, 384), 27);
  T_EQ(phos_inc(128, 384), 106);
  T_EQ(phos_inc(249, 384), 6);
  T_EQ(phos_inc(128, 1), 255);       //slow records: each one at full brightness

  //Rate estimate converges: 3 records per 2 ticks = 384
  for(i = 0; i < 60; i++)
    rate = phos_rate_update(rate, 3, 2);
  T_EQ(rate, 384);
  T_EQ(phos_rate_update(rate, 0, 0), 384);
  for(i = 0; i < 200; i++)
    rate = phos_rate_update(rate, 0, 4);
  T_EQ(rate, PHOS_RATE_MIN);

  //Steady state at 1.5 records per tick, 1 s: a row hit by every record stays saturated (shown after the decay, so at
  //phos_full), one hit by 10 % averages about 28 over the last 200 ticks (between hits its count falls and rises)
  reset();
  lut_offset();
  inc = phos_inc(223, 384);
  for(t = 0; t < 400; t++, s = phos_seed_next(s))
  {
    uint32_t r, nrec = (t & 1) ? 2 : 1;

    for(r = 0; r < nrec; r++)
    {
      uint32_t rec = t + t / 2 + r;

      memset(codes, 100, 3000);
      if(rec % 10 == 0)
        memset(codes, 150, 3000);
      phos_add_record(&ma, codes, 3000, lut, 0, dx_span(3000), 0, PHOS_W - 1, inc);
      memset(codes, 200, 3000);
      phos_add_record(&mb, codes, 3000, lut, 0, dx_span(3000), 0, PHOS_W - 1, inc);
    }

    phos_render(&ma, 0, fb, 800, pal, 223, s);
    phos_render(&mb, 0, fb, 800, pal, 223, s);

    if(t >= 200)
    {
      uint32_t x;

      for(x = 0; x < PHOS_W; x++)
        sum10 += cell(&ma, x, 222);

      if(cell(&mb, 300, 272) < phos_full(223) - 12)      //ticks with one record dip a little
        full = 0;
    }
  }
  T_TRUE(full);
  sum10 /= 200 * PHOS_W;
  printf("increment: a 10 %% hit rate averages %u (expected ~28)\n", sum10);
  T_TRUE((sum10 >= 21) && (sum10 <= 35));
}

static void test_render_rules(void)
{
  uint32_t n;

  reset();
  lut_offset();
  phos_palette_mono(pal, 0xFFFF00, 255);
  phos_palette_mono(pal2, 0x00FFFF, 255);

  memset(codes, 128, 3000);
  phos_add_record(&ma, codes, 3000, lut, 0, dx_span(3000), 0, PHOS_W - 1, 100);
  phos_add_record(&mb, codes, 3000, lut, 0, dx_span(3000), 0, 99, 50);
  phos_add_record(&mb, codes, 3000, lut, 0, dx_span(3000), 100, PHOS_W - 1, 150);
  memset(codes, 10, 3000);
  phos_add_record(&mb, codes, 3000, lut, 0, dx_span(3000), 0, PHOS_W - 1, 1);

  memset(fb, 0, sizeof(fb));
  T_EQ(phos_render(&ma, 0, fb, 800, pal, 256, 0), PHOS_W);
  n = phos_render(&mb, &ma, fb, 800, pal2, 256, 0);
  //CH2 only where brighter: columns 100.. on row 200, and all of its own row 82
  T_EQ(n, (PHOS_W - 100) + PHOS_W);
  T_EQ(fb[200 * 800 + 50], pal[100]);
  T_EQ(fb[200 * 800 + 150], pal2[150]);
  T_EQ(fb[82 * 800 + 7], pal2[1]);
  T_EQ(fb[81 * 800 + 7], 0);         //zero cells keep what was there

  //Palettes
  T_EQ(pal[0], 0);
  phos_palette_mono(pal, 0xFFFFFF, 255);
  T_EQ(pal[255], 0xFFFF);
  T_EQ(pal[1], (uint16_t)(((60 & 0xF8) << 8) | ((60 & 0xFC) << 3) | (60 >> 3)));   //24 % for one hit
  {
    uint32_t c, mono = 1;

    for(c = 2; c < 256; c++)
      if((pal[c] >> 11) < (pal[c - 1] >> 11))
        mono = 0;
    T_TRUE(mono);
  }
  phos_palette_mono(pal, 0xFFFFFF, phos_full(128));   //0.2 s: full brightness from 127 on
  T_EQ(phos_full(128), 127);
  T_EQ(pal[127], 0xFFFF);
  T_TRUE(pal[100] != 0xFFFF);
  phos_palette_heat(pal, 255);
  T_EQ(pal[255], 0xFFFF);
  T_EQ(pal[1] & 0xFFE0, 0);           //rare: blue only
  T_TRUE((pal[1] & 0x1F) > 0);
  T_EQ(phos_isqrt(65025), 255);
  T_EQ(phos_isqrt(255), 15);
  T_EQ(phos_isqrt(0), 0);
}

static void test_lines(void)
{
  uint32_t t;

  reset();
  T_EQ(phos_add_line(&ma, 10, 10, 20, 10, 0, 1), 11);
  T_EQ(phos_add_line(&ma, 20, 10, 20, 30, 1, 1), 20);       //chained: the shared pixel once
  T_EQ(phos_add_line(&ma, 20, 30, 10, 30, 1, 1), 10);
  T_EQ(phos_add_line(&ma, 10, 30, 10, 10, 1, 1), 20);       //closing the square: its end is the first segment's start
  T_EQ(cell(&ma, 10, 10), 2);                               //closing point hit twice (start of the first, end of the last)
  T_EQ(cell(&ma, 20, 10), 1);
  T_EQ(phos_lit(&ma), 60);

  //Steep diagonal: one run per column; clipped outside the map
  reset();
  t = phos_add_line(&ma, 0, 0, 3, 9, 0, 1);
  T_EQ(t, 10);
  T_EQ(phos_add_line(&ma, -5, 5, 2, 5, 0, 1), 3);
  T_EQ(phos_add_line(&ma, 5, -3, 5, PHOS_H + 3, 0, 1), PHOS_H);
}

int main(void)
{
  test_flat();
  test_square();
  test_zoomed();
  test_cost_worst();
  test_determinism();
  test_decay();
  test_increment();
  test_render_rules();
  test_lines();

  return t_report("test_phosphor");
}
