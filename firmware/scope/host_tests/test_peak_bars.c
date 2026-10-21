// test_peak_bars.c - the Scope view's bars for a peak-detect record (ui-sample, peak_bars.c), run natively: the column
// walk at whole, fractional and zoomed-in steps, the correction and inversion, the record's end, the drawing into a
// framebuffer column by column, the overload rule, the phosphor map's accumulation, and the decimation setting the UI
// arms for Sample Peak and back (ui_ctrl.c ui_acq_decim_set into pc_decim_config, as pc_hal_scope.c pc_dec_config does),
// and (fw-small) the record's extremes for the Vmax / Vmin / Vpp cards (pb_extremes) against the drawn bars.

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "peak_bars.h"
#include "phos_map.h"
#include "pc_decim.h"
#include "types.h"
#include "ui_ctrl.h"
#include "test_util.h"

#define N_REC   3000
#define Q16(v)  ((uint32_t)((v) * 65536.0 + 0.5))

static uint16_t rec[N_REC];

//A flat record (min 100, max 110) with a spike to 200 at sample 1000 and a dip to 20 at sample 1777 (fw-c6's model)
static void make_record(void)
{
  uint32_t i;

  for(i = 0; i < N_REC; i++)
    rec[i] = (uint16_t)((110 << 8) | 100);

  rec[1000] = (uint16_t)((200 << 8) | 105);
  rec[1777] = (uint16_t)((108 << 8) | 20);
}

//What a column must show: the lowest min and the highest max over samples a .. b - 1 (raw codes, identity correction)
static void expect(uint32_t a, uint32_t b, int32_t *lo, int32_t *hi)
{
  uint32_t i;

  *lo = 255;
  *hi = 0;

  for(i = a; i < b; i++)
  {
    if((int32_t)(rec[i] & 0xFF) < *lo)
      *lo = rec[i] & 0xFF;

    if((int32_t)(rec[i] >> 8) > *hi)
      *hi = rec[i] >> 8;
  }
}

static void test_walk_whole(void)
{
  pb_walk  w;
  int32_t  lo, hi, elo, ehi;
  uint32_t c = 0, bad = 0;

  make_record();
  pb_start(&w, rec, N_REC, 0, Q16(4.0), 65536, 0, 0);

  while(pb_next(&w, &lo, &hi))
  {
    expect(c * 4, c * 4 + 4, &elo, &ehi);
    bad += (lo != elo) || (hi != ehi);
    c++;
  }

  T_EQ(c, N_REC / 4);
  T_EQ(bad, 0);

  //The spike's column and the dip's: the bar reaches them; a neighbour does not
  pb_start(&w, rec, N_REC, 1000, Q16(4.0), 65536, 0, 0);
  T_TRUE(pb_next(&w, &lo, &hi));
  T_EQ(lo, 100);
  T_EQ(hi, 200);
  T_TRUE(pb_next(&w, &lo, &hi));
  T_EQ(hi, 110);

  pb_start(&w, rec, N_REC, 1776, Q16(4.0), 65536, 0, 0);
  T_TRUE(pb_next(&w, &lo, &hi));
  T_EQ(lo, 20);
  T_EQ(hi, 110);
}

//A fractional step (2.5 samples per column): column c covers floor(2.5 c) .. floor(2.5 (c + 1)) - 1, every sample once
static void test_walk_fraction(void)
{
  pb_walk  w;
  int32_t  lo, hi, elo, ehi;
  uint32_t c = 0, bad = 0, covered = 0;

  make_record();
  pb_start(&w, rec, N_REC, 0, Q16(2.5), 65536, 0, 0);

  while(pb_next(&w, &lo, &hi))
  {
    uint32_t a = (c * 5) / 2, b = ((c + 1) * 5) / 2;

    if(b > N_REC)
      b = N_REC;

    expect(a, b, &elo, &ehi);
    bad     += (lo != elo) || (hi != ehi);
    covered += b - a;
    c++;
  }

  T_EQ(c, N_REC * 2 / 5);
  T_EQ(covered, N_REC);
  T_EQ(bad, 0);
}

//Zoomed in (a quarter sample per column): each sample's bar on four columns, as the linear trace holds a sample
static void test_walk_zoom(void)
{
  pb_walk  w;
  int32_t  lo, hi;
  uint32_t c = 0, bad = 0;

  make_record();
  pb_start(&w, rec, N_REC, 999, Q16(0.25), 65536, 0, 0);

  for(c = 0; c < 12; c++)
  {
    T_TRUE(pb_next(&w, &lo, &hi));
    bad += (hi != (((999 + c / 4) == 1000) ? 200 : 110));
  }

  T_EQ(bad, 0);
}

//The record's end: a column reaching past it takes what it holds, then the walk stops; a start past it draws nothing
static void test_walk_end(void)
{
  pb_walk w;
  int32_t lo, hi;

  make_record();
  rec[N_REC - 1] = (uint16_t)((150 << 8) | 90);

  pb_start(&w, rec, N_REC, N_REC - 3, Q16(4.0), 65536, 0, 0);
  T_TRUE(pb_next(&w, &lo, &hi));
  T_EQ(lo, 90);
  T_EQ(hi, 150);
  T_FALSE(pb_next(&w, &lo, &hi));

  pb_start(&w, rec, N_REC, N_REC + 5, Q16(1.0), 65536, 0, 0);
  T_FALSE(pb_next(&w, &lo, &hi));

  //A zero step is one Q16 unit: never stuck
  pb_start(&w, rec, 2, 0, 0, 65536, 0, 0);
  T_EQ(w.step_q16, 1);
}

//The bytes go through the correction the midpoint took: the bar brackets the trace's code; inverted, min and max swap
static void test_correction(void)
{
  pb_walk  w;
  int32_t  lo, hi;
  uint32_t gain = pc_peak_gain(70000, 71000), i, bad = 0;
  int32_t  off  = pc_peak_off(-300000, -200000);

  make_record();

  for(i = 0; i < N_REC; i += 37)
    rec[i] = (uint16_t)(((i * 7 + 128) & 0xFF) << 8 | ((i * 3) & 0x7F));

  pb_start(&w, rec, N_REC, 0, Q16(1.0), gain, off, 0);

  for(i = 0; pb_next(&w, &lo, &hi); i++)
  {
    uint8_t mn = (uint8_t)rec[i], mx = (uint8_t)(rec[i] >> 8);
    int32_t mid = pc_peak_mid(mn, mx, gain, off);

    bad += (lo != pc_peak_code(mn, gain, off)) || (hi != pc_peak_code(mx, gain, off));
    bad += (mx >= mn) && ((lo > mid) || (mid > hi));
  }

  T_EQ(i, N_REC);
  T_EQ(bad, 0);

  pb_start(&w, rec, N_REC, 1000, Q16(1.0), gain, off, 1);
  T_TRUE(pb_next(&w, &lo, &hi));
  T_EQ(lo, 255 - pc_peak_code(200, gain, off));
  T_EQ(hi, 255 - pc_peak_code(105, gain, off));
}

//fw-small: the Vmax / Vmin / Vpp cards' extremes (pb_extremes) are the tallest bar's: the lowest min and the highest max
//of the whole record through the same correction, inverted alike; the midpoint trace reaches the spike at about half
static void test_extremes(void)
{
  static const uint32_t steps[] = { Q16(1.0), Q16(2.5), Q16(4.0), Q16(0.25) };
  pb_walk  w;
  int32_t  lo, hi, blo, bhi, l, h, mid_max = 0;
  uint32_t gain = pc_peak_gain(70000, 71000), i, k;
  int32_t  off  = pc_peak_off(-300000, -200000);
  int      inv;

  make_record();

  //Identity correction: the spike 200 and the dip 20 in raw codes
  pb_extremes(rec, N_REC, 65536, 0, 0, &lo, &hi);
  T_EQ(lo, 20); T_EQ(hi, 200);
  pb_extremes(rec, N_REC, 65536, 0, 1, &lo, &hi);
  T_EQ(lo, 255 - 200); T_EQ(hi, 255 - 20);

  //The midpoint trace (what the cards read before): the spike's sample at (105 + 200) / 2
  for(i = 0; i < N_REC; i++)
  {
    int32_t m = pc_peak_mid((uint8_t)rec[i], (uint8_t)(rec[i] >> 8), 65536, 0);

    if(m > mid_max)
      mid_max = m;
  }
  T_EQ(mid_max, 153);
  pb_extremes(rec, N_REC, 65536, 0, 0, &lo, &hi);
  T_EQ(hi - mid_max, 47);                                     //the card now reads the spike, not its midpoint

  //With a correction, both polarities and every column step: equal to the extremes over the drawn bars
  for(inv = 0; inv < 2; inv++)
  {
    pb_extremes(rec, N_REC, gain, off, inv, &lo, &hi);
    T_EQ(lo, inv ? 255 - pc_peak_code(200, gain, off) : pc_peak_code(20, gain, off));
    T_EQ(hi, inv ? 255 - pc_peak_code(20, gain, off) : pc_peak_code(200, gain, off));

    for(k = 0; k < sizeof(steps) / sizeof(steps[0]); k++)
    {
      blo = 255; bhi = 0;
      pb_start(&w, rec, N_REC, 0, steps[k], gain, off, inv);

      while(pb_next(&w, &l, &h))
      {
        if(l < blo) blo = l;
        if(h > bhi) bhi = h;
      }

      T_EQ(blo, lo); T_EQ(bhi, hi);
    }
  }

  //An empty record: nothing (the caller keeps the midpoint's values)
  pb_extremes(rec, 0, gain, off, 0, &lo, &hi);
  T_EQ(lo, 255); T_EQ(hi, 0);
}

//The drawing: one vertical run per column from the upper to the lower row, nothing else in the column
static uint8_t fb[480][800];

static int32_t to_y(void *ctx, int32_t code)
{
  (void)ctx;
  return 448 - (code * 399) / 255;           //the plot's rows 49 .. 448, code 0 at the bottom
}

static void vline(void *ctx, int32_t x, int32_t y0, int32_t y1)
{
  int32_t y;

  (*(uint32_t *)ctx)++;

  for(y = y0; y <= y1; y++)
    fb[y][x] = 1;
}

static void test_draw_pixels(void)
{
  pb_walk    w;
  uint32_t   calls = 0, cols, x, y, bad = 0;
  pb_draw_cb cb = { to_y, vline, &calls };

  make_record();
  memset(fb, 0, sizeof(fb));

  //3000 samples over the 725 columns 3 .. 727 at 4 samples per column: 725 bars (the record holds 750)
  pb_start(&w, rec, N_REC, 0, Q16(4.0), 65536, 0, 0);
  cols = pb_draw(&w, 3, 728, &cb);

  T_EQ(cols, 725);
  T_EQ(calls, 725);

  for(x = 0; x < 800; x++)
  {
    int32_t lo = 255, hi = 0, top, bottom;

    if((x >= 3) && (x < 728))
      expect((x - 3) * 4, (x - 3) * 4 + 4, &lo, &hi);

    top    = (x >= 3) && (x < 728) ? to_y(0, hi) : 480;
    bottom = (x >= 3) && (x < 728) ? to_y(0, lo) : -1;

    for(y = 0; y < 480; y++)
      bad += fb[y][x] != (((int32_t)y >= top) && ((int32_t)y <= bottom));
  }

  T_EQ(bad, 0);

  //The spike's column (samples 1000..1003 at x 253) runs from code 200's row to code 100's; the dip's (1776..1779 at
  //x 447) from 110's down to 20's
  T_EQ(fb[to_y(0, 200)][253], 1);
  T_EQ(fb[to_y(0, 200) - 1][253], 0);
  T_EQ(fb[to_y(0, 110)][252], 1);
  T_EQ(fb[to_y(0, 111)][252], 0);
  T_EQ(fb[to_y(0, 20)][447], 1);
  T_EQ(fb[to_y(0, 20) + 1][447], 0);
  T_EQ(fb[to_y(0, 100)][448], 1);
  T_EQ(fb[to_y(0, 99)][448], 0);
}

//Overload (the caller's red): either byte of any pair at 0 or 255; 1 and 254 are not
static void test_overload(void)
{
  make_record();
  T_FALSE(pb_overload(rec, N_REC));

  rec[2999] = (uint16_t)((254 << 8) | 1);
  T_FALSE(pb_overload(rec, N_REC));

  rec[500] = (uint16_t)((255 << 8) | 100);
  T_TRUE(pb_overload(rec, N_REC));
  T_EQ(pc_peak_overload(100, 255), 0xFE);

  make_record();
  rec[0] = (uint16_t)((110 << 8) | 0);
  T_TRUE(pb_overload(rec, N_REC));
  T_EQ(pc_peak_overload(0, 110), 0x01);

  //A pair at a rail drawn: the bar still spans its bytes (the colour is the caller's)
  {
    pb_walk w;
    int32_t lo, hi;

    pb_start(&w, rec, N_REC, 0, Q16(1.0), 65536, 0, 0);
    T_TRUE(pb_next(&w, &lo, &hi));
    T_EQ(lo, 0);
    T_EQ(hi, 110);
  }
}

//Phosphor: each column's rows min .. max hit once per record; two records twice
static uint8_t  cells[PHOS_W * PHOS_H] __attribute__((aligned(8)));
static uint32_t ext[PHOS_W];

static void test_phosphor(void)
{
  phos_map m = { cells, ext };
  uint16_t lut[256];
  pb_walk  w;
  uint32_t c, x, r, bad = 0, expect_touch = 0, t;

  for(c = 0; c < 256; c++)
    lut[c] = (uint16_t)(399 - (c * 399) / 255);

  memset(cells, 0, sizeof(cells));
  memset(ext, 0, sizeof(ext));
  make_record();

  for(x = 0; x < 725; x++)
  {
    int32_t lo, hi;

    expect(x * 4, x * 4 + 4, &lo, &hi);
    expect_touch += lut[lo] - lut[hi] + 1;
  }

  pb_start(&w, rec, N_REC, 0, Q16(4.0), 65536, 0, 0);
  t = pb_phos_record(&m, &w, lut, 1, 726, 10);
  T_EQ(t, expect_touch);

  pb_start(&w, rec, N_REC, 0, Q16(4.0), 65536, 0, 0);
  T_EQ(pb_phos_record(&m, &w, lut, 1, 726, 10), expect_touch);

  for(x = 0; x < PHOS_W; x++)
  {
    int32_t lo = 255, hi = 0;

    if((x >= 1) && (x < 726))
      expect((x - 1) * 4, (x - 1) * 4 + 4, &lo, &hi);

    for(r = 0; r < PHOS_H; r++)
    {
      int in = (x >= 1) && (x < 726) && ((int32_t)r >= lut[hi]) && ((int32_t)r <= lut[lo]);

      bad += cells[x * PHOS_H + r] != (in ? 20 : 0);
    }
  }

  T_EQ(bad, 0);

  //phos_add_span: either order, rows clipped, a column outside the map not written
  memset(cells, 0, sizeof(cells));
  memset(ext, 0, sizeof(ext));
  T_EQ(phos_add_span(&m, 5, 30, 20, 7), 11);
  T_EQ(cells[5 * PHOS_H + 20], 7);
  T_EQ(cells[5 * PHOS_H + 30], 7);
  T_EQ(cells[5 * PHOS_H + 31], 0);
  T_EQ(ext[5], 20u | (31u << 16));
  T_EQ(phos_add_span(&m, 6, -5, 2, 7), 3);
  T_EQ(phos_add_span(&m, PHOS_W, 0, 10, 7), 0);
  T_EQ(phos_add_span(&m, -1, 0, 10, 7), 0);
}

//The UI's decimation for Sample Peak and back (pc_hal_scope.c: pc_dec_ui_set -> pc_dec_config -> pc_decim_config)
static void ui_config(uint8 set, uint32_t divider, uint32_t total, pc_decim_cfg *c)
{
  pc_decim_config(set & 3, (uint8_t)(set & (PC_DECIM_SET_FIR | PC_DECIM_SET_PEAK)), divider, total,
                  PC_DECIM_HAS_FIR | PC_DECIM_HAS_CAPSTAGES, c);
}

static void test_decim_calls(void)
{
  pc_decim_cfg c;
  uint8        cic2 = PC_DECIM_DEFAULT_SCOPE, cic2fir = PC_DECIM_CIC2 | PC_DECIM_SET_FIR;

  //Scope, CIC2 at 1 MSa/s (divider 99 -> N 100): Peak arms peak detect, K 1, no FIR, the window N
  T_EQ(ui_acq_decim_set(cic2, 1, 0), cic2 | PC_DECIM_SET_PEAK);
  ui_config(ui_acq_decim_set(cic2, 1, 0), 99, 3000, &c);
  T_EQ(c.peak, 1);
  T_EQ(c.k, 1);
  T_EQ(c.fir_en, 0);
  T_EQ(c.n, 100);
  T_EQ(c.why, PC_DECIM_WHY_NONE);
  T_EQ(pc_decim_shift_byte(&c) & 0xC0, 0xC0);

  //Back to Normal: the Filter as it was
  T_EQ(ui_acq_decim_set(cic2, 0, 0), cic2);
  ui_config(ui_acq_decim_set(cic2, 0, 0), 99, 3000, &c);
  T_EQ(c.peak, 0);
  T_EQ(c.k, 2);

  //The FIR is kept behind Peak and back with Normal
  ui_config(ui_acq_decim_set(cic2fir, 1, 0), 99, 3000, &c);
  T_EQ(c.peak, 1);
  T_EQ(c.fir_en, 0);
  ui_config(ui_acq_decim_set(cic2fir, 0, 0), 99, 3000, &c);
  T_EQ(c.peak, 0);
  T_EQ(c.k, 2);
  T_EQ(c.fir_en, 1);

  //Filter off with Peak: still peak detect; off again with Normal: the legacy path
  ui_config(ui_acq_decim_set(PC_DECIM_OFF, 1, 0), 99, 3000, &c);
  T_EQ(c.peak, 1);
  ui_config(ui_acq_decim_set(PC_DECIM_OFF, 0, 0), 99, 3000, &c);
  T_EQ(c.k, 0);
  T_EQ(c.peak, 0);

  //The Spectrum acquires Normal: no peak bit, its own Filter
  T_EQ(ui_acq_decim_set(cic2fir, 1, 1), cic2fir);

  //The record the menu leaves: at most 6000 samples with Peak (the 16-bit layout); 9000 would fall back (why 3), which is
  //why the depth chips above 6000 go (pc_dec_fit_memory)
  ui_config(ui_acq_decim_set(cic2, 1, 0), 99, 6000, &c);
  T_EQ(c.peak, 1);
  ui_config(ui_acq_decim_set(cic2, 1, 0), 99, 9000, &c);
  T_EQ(c.peak, 0);
  T_EQ(c.why, PC_DECIM_WHY_TOTAL);
}

int main(void)
{
  test_walk_whole();
  test_walk_fraction();
  test_walk_zoom();
  test_walk_end();
  test_correction();
  test_extremes();
  test_draw_pixels();
  test_overload();
  test_phosphor();
  test_decim_calls();
  return t_report("test_peak_bars");
}
