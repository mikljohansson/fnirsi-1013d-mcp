//----------------------------------------------------------------------------------------------------------------------------------
// test_slider_ov.c - the slider track overview's geometry (slider_ov.c, fw 0.2.73, decision 7 in fw 0.2.74; docs/plans/slider-overview.md): the
// track the three modes share, the column ranges with their join, rows, line runs, the inside-window test.
//----------------------------------------------------------------------------------------------------------------------------------

#include "test_util.h"
#include "slider_ov.h"
#include "tb_view.h"
#include "bode_ui.h"
#include "slider_ov_draw.h"
#include "display_lib.h"
#include "fnirsi_1013d_scope.h"

//fw 0.2.92: the display calls slider_ov_draw.c makes, into a screen the tests read back
static uint32 scr[480][800], fg;

void display_set_fg_color(uint32 color) { fg = color; }
void display_draw_horz_line(uint32 y, uint32 xs, uint32 xe) { uint32 x; for(x = xs; x <= xe; x++) scr[y][x] = fg; }
void display_draw_vert_line(uint32 x, uint32 ys, uint32 ye) { uint32 y; for(y = ys; y <= ye; y++) scr[y][x] = fg; }
void display_fill_rect(uint32 x, uint32 y, uint32 w, uint32 h)
{
  uint32 i;

  for(i = 0; i < h; i++)
    display_draw_horz_line(y + i, x, x + w - 1);
}

//The three modes map the same track (owner 10-19 22:15, decision 7: the whole row, no frame or line; the window a box
//with a 1 px margin)
static void test_geometry(void)
{
  T_EQ(SOV_X, 2);
  T_EQ(SOV_W, 726);
  T_EQ(SOV_Y, 452);                                   //the old frame's top line is track now
  T_EQ(SOV_H, 20);
  T_EQ(SOV_Y + SOV_H - 1, 471);                       //and its bottom line
  T_EQ(SOV_WIN_Y, SOV_Y + 1);                         //the box under the 1 px margin
  T_EQ(SOV_WIN_Y + SOV_WIN_H, SOV_Y + SOV_H - 1);     //and above it
  T_EQ(SOV_WIN_Y + 1, 454);                           //the box's inside: y 454..469, 16 rows
  T_EQ(SOV_WIN_Y + SOV_WIN_H - 2, 469);
  T_EQ(TBV_SLIDER_X, SOV_X);
  T_EQ(TBV_SLIDER_W, SOV_W);
  T_EQ(BU_SL_X, SOV_X);
  T_EQ(BU_SL_W, SOV_W);
  T_EQ(TBV_SLIDER_FRAME_X, SOV_X);                    //the hit area keeps covering the whole track
  T_EQ(TBV_SLIDER_FRAME_X + TBV_SLIDER_FRAME_W, SOV_X + SOV_W);
  T_TRUE(TBV_SLIDER_HIT_Y < SOV_Y);
  T_TRUE(BU_SL_HIT_Y < SOV_Y);
  T_TRUE(SOV_Y + SOV_H <= 480);
}

//Every item is in some column, the columns go in order, each starts where the previous one ended (the join)
static void check_items(uint32 n)
{
  uint32 a, b, pb = 0, covered = 0;
  int32  c;

  for(c = 0; c < SOV_W; c++)
  {
    sov_items(c, n, &a, &b);
    T_TRUE(a <= b);
    T_TRUE(b < n);
    if(c == 0)
      T_EQ(a, 0);
    else
    {
      T_EQ(a, pb);                                    //starts at the previous column's last item
      T_TRUE(b >= pb);
    }
    if(b + 1 > covered)
      covered = b + 1;
    pb = b;
  }

  T_EQ(covered, n);                                   //the last column reaches the end

  //The stepping iterator gives the same ranges without a division per column
  {
    sov_cols it;
    uint32 ia, ib;

    sov_cols_start(&it, n);
    for(c = 0; c < SOV_W; c++)
    {
      sov_items(c, n, &a, &b);
      sov_cols_next(&it, &ia, &ib);
      T_EQ(ia, a);
      T_EQ(ib, b);
    }
  }
  sov_items(SOV_W - 1, n, &a, &b);
  T_EQ(b, n - 1);
}

static void test_items(void)
{
  uint32 a, b;
  static const uint32 ns[] = { 2, 20, 100, 725, 726, 727, 1000, 1452, 2048, 3000, 8192, 15000, 16382, 5000000 };
  unsigned i;

  for(i = 0; i < sizeof(ns) / sizeof(ns[0]); i++)
    check_items(ns[i]);

  //More items than columns: column c is c x n / 726 .. (c + 1) x n / 726 - 1, plus the previous column's last
  sov_items(0, 16382, &a, &b);
  T_EQ(a, 0);
  T_EQ(b, 16382 / 726 - 1);                           //0..21
  sov_items(1, 16382, &a, &b);
  T_EQ(a, 21);
  T_EQ(b, (2 * 16382) / 726 - 1);                     //21..44

  //Fewer items than columns: a column joins the previous item only where the item changes
  sov_items(10, 100, &a, &b);                         //item 1 (columns 8..14)
  T_EQ(a, 1);
  T_EQ(b, 1);
  sov_items(8, 100, &a, &b);                          //the first column of item 1 joins item 0
  T_EQ(a, 0);
  T_EQ(b, 1);

  //Out of range and empty
  sov_items(-5, 3000, &a, &b);
  T_EQ(a, 0);
  sov_items(5000, 3000, &a, &b);
  T_EQ(b, 2999);
  sov_items(3, 0, &a, &b);
  T_EQ(a, 0);
  T_EQ(b, 0);
}

static void test_rows(void)
{
  int32 y, prev = 0;

  //The Scope: screen y 49..448 onto the 20 rows, top to bottom, every row used, clipped outside
  T_EQ(sov_row(49, 49, 449), 0);
  T_EQ(sov_row(448, 49, 449), SOV_H - 1);
  T_EQ(sov_row(10, 49, 449), 0);
  T_EQ(sov_row(479, 49, 449), SOV_H - 1);
  T_EQ(sov_row(249, 49, 449), 10);                    //the centre line: the middle row
  for(y = 49; y <= 448; y++)
  {
    int32 r = sov_row(y, 49, 449);

    T_TRUE((r == prev) || (r == prev + 1));
    prev = r;
  }
  T_EQ(prev, SOV_H - 1);

  //Levels: top above, bottom below (centi-dB: +10 dB at the top, 8 x 10 dB/div)
  T_EQ(sov_row(1000, 1000, -7000), 0);
  T_EQ(sov_row(-7000, 1000, -7000), SOV_H - 1);
  T_EQ(sov_row(-12000, 1000, -7000), SOV_H - 1);     //the noise floor under the plot: the bottom row
  T_EQ(sov_row(5000, 1000, -7000), 0);                //over the top: clipped
  T_EQ(sov_row(5, 5, 5), 0);                          //degenerate scale

  //Fitted
  T_EQ(sov_row_fit(-3000, -3000, -60000), 0);
  T_EQ(sov_row_fit(-60000, -3000, -60000), SOV_H - 1);
  T_EQ(sov_row_fit(-31500, -3000, -60000), 10);
  T_EQ(sov_row_fit(7, 7, 7), SOV_H / 2);
  T_EQ(sov_row_fit(100, 50, 0), 0);
  T_EQ(sov_row_fit(-100, 50, 0), SOV_H - 1);
}

static void test_lines(void)
{
  int32 lo, hi;

  T_EQ(sov_lerp(10, 0, 20, 10, 10), 0);
  T_EQ(sov_lerp(10, 0, 20, 10, 15), 5);
  T_EQ(sov_lerp(10, 0, 20, 10, 20), 10);
  T_EQ(sov_lerp(10, 18, 37, 0, 37), 0);
  T_EQ(sov_lerp(0, 18, 4, 0, 1), 13);                 //18 - 4.5: the step rounds away from zero, 13
  T_EQ(sov_lerp(5, 3, 5, 9, 5), 9);

  sov_join(-1, 7, &lo, &hi);
  T_EQ(lo, 7);
  T_EQ(hi, 7);
  sov_join(7, 7, &lo, &hi);
  T_EQ(lo, 7);
  T_EQ(hi, 7);
  sov_join(3, 9, &lo, &hi);                           //going down: the rows after 3 up to 9
  T_EQ(lo, 4);
  T_EQ(hi, 9);
  sov_join(9, 3, &lo, &hi);                           //going up
  T_EQ(lo, 3);
  T_EQ(hi, 8);
  sov_join(4, 5, &lo, &hi);
  T_EQ(lo, 5);
  T_EQ(hi, 5);
}

static void test_inside(void)
{
  int32 x, w;

  //At the window's drawn x and w, edges included
  T_TRUE(!sov_inside(99, 100, 50));
  T_TRUE(sov_inside(100, 100, 50));
  T_TRUE(sov_inside(149, 100, 50));
  T_TRUE(!sov_inside(150, 100, 50));

  //The 8 px minimum of tbv_slider: the bright part is the drawn window, even where the true share is narrower
  tbv_slider(14999, 200000000, 200000000, 15000, &x, &w);
  T_EQ(w, TBV_SLIDER_MIN_W);
  T_EQ(x + w, SOV_X + SOV_W);
  T_TRUE(sov_inside(SOV_X + SOV_W - 1, x, w));
  T_TRUE(sov_inside(SOV_X + SOV_W - 8, x, w));
  T_TRUE(!sov_inside(SOV_X + SOV_W - 9, x, w));

  //Full span: every column bright
  tbv_slider(0, 200000000, 2000, 3000, &x, &w);
  T_TRUE(sov_inside(SOV_X, x, w));
  T_TRUE(sov_inside(SOV_X + SOV_W - 1, x, w));

  //The Frequency response window ends where the next step begins: its right edge is one column before it
  T_TRUE(sov_inside(bu_slider_x(18) - 1, bu_slider_x(6), bu_slider_x(18) - bu_slider_x(6)));
  T_TRUE(!sov_inside(bu_slider_x(18), bu_slider_x(6), bu_slider_x(18) - bu_slider_x(6)));

  T_EQ(sov_col(0), SOV_X);
  T_EQ(sov_col(SOV_X + SOV_W), SOV_X + SOV_W - 1);   //10 MHz (bu_slider_x_mhz's end) on the last column
  T_EQ(sov_col(300), 300);
}

//Tap to jump (fw 0.2.73, owner 10 F item 2)
static void test_jump(void)
{
  int32 x, y;

  //Inside the window (edges included), above the row, or a full-width window: no jump
  T_EQ(sov_jump(100, 460, 100, 50), 0);
  T_EQ(sov_jump(149, 460, 100, 50), 0);
  T_EQ(sov_jump(400, SOV_TAP_Y - 1, 100, 50), 0);     //the Scope's extended drag area by the cards: no jump
  T_EQ(sov_jump(400, 460, SOV_X, SOV_W), 0);

  //Outside: the window's centre lands on the tap
  T_EQ(sov_jump(400, 460, 100, 50), 400 - 25 - 100);
  T_EQ(sov_jump(40, SOV_TAP_Y, 300, 50), 40 - 25 - 300);
  T_EQ(sov_jump(400, 479, 100, 51), 400 - 25 - 100);

  //Near the ends: kept on the track
  T_EQ(sov_jump(5, 460, 300, 50), SOV_X - 300);
  T_EQ(sov_jump(727, 460, 100, 50), SOV_X + SOV_W - 50 - 100);
  T_EQ(sov_jump(900, 460, 100, 50), SOV_X + SOV_W - 50 - 100);

  //Every column and window: the result is on the track and covers the tap (or is at an end)
  for(x = 0; x < 800; x += 7)
    for(y = 100; y < 600; y += 37)
    {
      int32 wx = SOV_X + y % (SOV_W - (y % 300) - 8), ww = 8 + y % 300, d = sov_jump(x, 470, wx, ww), nx = wx + d;

      T_TRUE(nx >= SOV_X);
      T_TRUE(nx + ww <= SOV_X + SOV_W);
      if(d)
        T_TRUE(sov_inside(sov_col(x), nx, ww));
    }
}

//The Scope: the jump through the record maths (acq_view_slider_jump = sov_jump + tbv_slider_move + tbv_clamp_pos) puts
//the drawn window's centre on the tap within the rounding of the record's steps
static void test_jump_scope(void)
{
  static const uint32 fss[] = { 200000000, 25000000, 5000000, 500000 };
  static const uint32 fpds[] = { 20000000, 1000000, 100000, 10000 };
  static const uint32 totals[] = { 3000, 15000, 16382 };
  unsigned a, b, c;
  int32 x, jumps = 0;

  for(a = 0; a < 4; a++)
    for(b = 0; b < 4; b++)
      for(c = 0; c < 3; c++)
      {
        uint32 fs = fss[a], fpd = fpds[b], total = totals[c], trig = total / 2;
        int32  pos0 = 362, first, xend, sx, sw;

        if(!tbv_viable(fs, fpd, total))
          continue;

        pos0 = tbv_clamp_pos(pos0, fs, fpd, total, trig);
        tbv_window((int32)trig, pos0, fs, fpd, total, &first, &xend);
        tbv_slider(first, fs, fpd, total, &sx, &sw);

        for(x = SOV_X; x < SOV_X + SOV_W; x += 29)
        {
          int32 d = sov_jump(x, 460, sx, sw), pos1, nsx, nsw, want;

          if(!d)
            continue;

          pos1 = tbv_clamp_pos(pos0 + tbv_slider_move(d, fs, fpd, total), fs, fpd, total, trig);
          tbv_window((int32)trig, pos1, fs, fpd, total, &first, &xend);
          tbv_slider(first, fs, fpd, total, &nsx, &nsw);

          jumps++;
          want = sx + d;                              //where sov_jump put it (kept on the track)
          T_TRUE((nsx - want <= 2) && (want - nsx <= 2));
          T_TRUE(sov_inside(x, nsx - 2, nsw + 4));
        }
      }

  T_TRUE(jumps > 200);
}

//The Frequency response: the jump in px through bu_slider_shift moves Start / Stop by whole steps so the window covers
//the tap (span kept, on the track)
static void test_jump_fr(void)
{
  int s0, w, x, jumps = 0;

  for(w = 3; w <= 12; w += 3)
    for(s0 = 0; s0 + w <= BODE_STEPS - 1; s0 += 2)
      for(x = SOV_X; x < SOV_X + SOV_W; x += 11)
      {
        int32 wx = bu_slider_x(s0), ww = bu_slider_x(s0 + w) - wx, d = sov_jump(x, 460, wx, ww);
        int   n;

        if(!d)
          continue;

        jumps++;
        n = bu_slider_shift(s0, s0 + w, d);
        T_TRUE(n != 0);
        T_TRUE((s0 + n >= 0) && (s0 + w + n <= BODE_STEPS - 1));
        //Covered, but for the nearest-step rounding at the far side (at most one step's width)
        T_TRUE(sov_inside(x, bu_slider_x(s0 + n) - 60, bu_slider_x(s0 + w + n) - bu_slider_x(s0 + n) + 120));
      }

  T_TRUE(jumps > 200);
}

//fw 0.2.92 (decision 8, owner 2026-10-20 13:35 and 13:36): in Overlaid the Frequency response track draws the magnitude
//and the phase, each fitted to the track's height on its own, flagged points grey for both, the phase first and the
//magnitude over it
#define T_PHASE      0x00FF80FF
#define T_FLAG       0x00707070

static void test_two_curves(void)
{
  static const int32 col[5]  = { 100, 200, 300, 400, 500 };
  static const int32 mv[5]   = { -40000, -20000, 0, -20000, -40000 };   //mdB: a hump, 40 dB high
  static const int32 pv[5]   = { 90000, 45000, 0, -45000, -90000 };    //millidegrees: a falling line, 180 degrees
  static const int32 pw[5]   = { 0, 170000, -170000, 0, 0 };           //a wrap between points 1 and 2
  static const int32 same[2] = { 100, 100 };
  static const uint8 ok[5]   = { 1, 1, 1, 1, 1 }, have[5] = { 1, 1, 1, 1, 1 };
  static const uint8 okf[5]  = { 1, 1, 1, 0, 1 };                      //point 3 flagged
  static const uint8 none[5] = { 0, 0, 0, 0, 0 };
  static uint8 on_ph[SOV_H][SOV_W], on_mag[SOV_H][SOV_W];
  sov_curve mag = { 5, col, mv, ok, have, 0, CHANNEL2_COLOR, SOV_CH2_DIM };
  sov_curve ph  = { 5, col, pv, ok, have, 180000, T_PHASE, SOV_PHASE_DIM };
  sov_curve c;
  int32 vmax, vmin, x, y, wx = 50, ww = 600;
  int   both = 0;

  //Each fitted on its own: both use all 20 rows although their scales differ
  T_TRUE(sov_curve_fit(&mag, &vmax, &vmin));
  T_EQ(vmax, 0); T_EQ(vmin, -40000);
  T_EQ(sov_curve_row(&mag, 2, vmax, vmin), 0);
  T_EQ(sov_curve_row(&mag, 0, vmax, vmin), SOV_H - 1);
  T_TRUE(sov_curve_fit(&ph, &vmax, &vmin));
  T_EQ(vmax, 90000); T_EQ(vmin, -90000);
  T_EQ(sov_curve_row(&ph, 0, vmax, vmin), 0);
  T_EQ(sov_curve_row(&ph, 4, vmax, vmin), SOV_H - 1);

  //The fit skips a flagged point, takes the flagged ones when none is valid; nothing measured, no fit
  c = mag;
  c.v = mv;
  c.ok = okf;
  {
    static const int32 spike[5] = { -40000, -20000, 0, 50000, -40000 };   //the flagged point would set the top

    c.v = spike;
    T_TRUE(sov_curve_fit(&c, &vmax, &vmin));
    T_EQ(vmax, 0); T_EQ(vmin, -40000);
    c.ok = none;
    T_TRUE(sov_curve_fit(&c, &vmax, &vmin));
    T_EQ(vmax, 50000); T_EQ(vmin, -40000);
  }
  c.have = none;
  T_FALSE(sov_curve_fit(&c, &vmax, &vmin));

  //Joins: the magnitude always, the phase not across a wrap, never back to the same column
  T_TRUE(sov_curve_joins(&mag, 0, 1));
  c = ph;
  c.v = pw;
  T_TRUE(sov_curve_joins(&c, 0, 1));
  T_FALSE(sov_curve_joins(&c, 1, 2));
  c = mag;
  c.n = 2;
  c.col = same;
  T_FALSE(sov_curve_joins(&c, 0, 1));

  //Overlaid: both drawn, full inside the window, dimmed outside
  sov_draw_row();
  sov_draw_curves(&mag, &ph, T_FLAG, wx, ww);
  T_EQ(scr[SOV_Y + 0][300], CHANNEL2_COLOR);         //the magnitude's peak
  T_EQ(scr[SOV_Y + 0][100], T_PHASE);                //the phase's top
  T_EQ(scr[SOV_Y + SOV_H - 1][500], CHANNEL2_COLOR); //both end on the bottom row: the magnitude over the phase
  sov_draw_row();
  sov_draw_curves(&mag, &ph, T_FLAG, 350, 300);      //the window right of the peak
  T_EQ(scr[SOV_Y + 0][300], SOV_CH2_DIM);
  T_EQ(scr[SOV_Y + 0][100], SOV_PHASE_DIM);
  T_EQ(scr[SOV_Y + SOV_H - 1][500], CHANNEL2_COLOR);

  //The order (owner 13:36): each curve alone, then the pair; every pixel of the magnitude is cyan in the pair, also those
  //the phase lands on, the phase's others pink, the rest black
  sov_draw_row();
  sov_draw_curve(&ph, T_FLAG, wx, ww);
  for(y = 0; y < SOV_H; y++)
    for(x = 0; x < SOV_W; x++)
      on_ph[y][x] = (scr[SOV_Y + y][SOV_X + x] == T_PHASE);
  sov_draw_row();
  sov_draw_curve(&mag, T_FLAG, wx, ww);
  for(y = 0; y < SOV_H; y++)
    for(x = 0; x < SOV_W; x++)
      on_mag[y][x] = (scr[SOV_Y + y][SOV_X + x] == CHANNEL2_COLOR);
  sov_draw_row();
  sov_draw_curves(&mag, &ph, T_FLAG, wx, ww);
  for(y = 0; y < SOV_H; y++)
    for(x = 0; x < SOV_W; x++)
    {
      uint32 px = scr[SOV_Y + y][SOV_X + x];

      if(on_mag[y][x])
      {
        T_EQ(px, CHANNEL2_COLOR);
        both += on_ph[y][x];
      }
      else if(on_ph[y][x])
        T_EQ(px, T_PHASE);
      else
        T_EQ(px, BLACK_COLOR);
    }
  T_TRUE(both >= 2);

  //Magnitude only / Phase only: that one curve
  sov_draw_row();
  sov_draw_curves(&mag, 0, T_FLAG, wx, ww);
  T_EQ(scr[SOV_Y + 0][100], BLACK_COLOR);
  T_EQ(scr[SOV_Y + 0][300], CHANNEL2_COLOR);
  sov_draw_row();
  sov_draw_curves(0, &ph, T_FLAG, wx, ww);
  T_EQ(scr[SOV_Y + 0][300], BLACK_COLOR);
  T_EQ(scr[SOV_Y + SOV_H - 1][500], T_PHASE);        //no magnitude over it now

  //A flagged point: a grey pixel in each curve, no line to or from it
  mag.ok = okf;
  ph.ok = okf;
  sov_draw_row();
  sov_draw_curves(&mag, &ph, T_FLAG, wx, ww);
  T_EQ(scr[SOV_Y + 10][400], T_FLAG);                //the magnitude's -20000 on 0..-40000: row 10
  T_EQ(scr[SOV_Y + 14][400], T_FLAG);                //the phase's -45000 on 90000..-90000: row 14
  for(y = 0; y < SOV_H; y++)
  {
    T_EQ(scr[SOV_Y + y][350], BLACK_COLOR);          //nothing joins 2 to 3
    T_EQ(scr[SOV_Y + y][450], BLACK_COLOR);          //nor 3 to 4
  }
}

int main(void)
{
  test_geometry();
  test_items();
  test_rows();
  test_lines();
  test_inside();
  test_jump();
  test_jump_scope();
  test_jump_fr();
  test_two_curves();
  return t_report("test_slider_ov");
}
