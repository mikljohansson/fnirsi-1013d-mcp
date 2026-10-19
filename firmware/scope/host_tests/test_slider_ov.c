//----------------------------------------------------------------------------------------------------------------------------------
// test_slider_ov.c - the slider track overview's geometry (slider_ov.c, fw 0.2.72; docs/plans/slider-overview.md): the
// track the three modes share, the column ranges with their join, rows, line runs, the inside-window test.
//----------------------------------------------------------------------------------------------------------------------------------

#include "test_util.h"
#include "slider_ov.h"
#include "tb_view.h"
#include "bode_ui.h"

//The three modes map the same track (owner 10-19 21:36: the whole row, no frame sides)
static void test_geometry(void)
{
  T_EQ(SOV_X, 2);
  T_EQ(SOV_W, 726);
  T_EQ(SOV_Y, 453);
  T_EQ(SOV_H, 19);
  T_EQ(SOV_Y + SOV_H - 1, 471);                       //the old frame's bottom row is track now
  T_EQ(SOV_LINE_Y, SOV_Y - 1);
  T_EQ(TBV_SLIDER_X, SOV_X);
  T_EQ(TBV_SLIDER_W, SOV_W);
  T_EQ(BU_SL_X, SOV_X);
  T_EQ(BU_SL_W, SOV_W);
  T_EQ(TBV_SLIDER_FRAME_X, SOV_X);                    //the hit area keeps covering the whole track
  T_EQ(TBV_SLIDER_FRAME_X + TBV_SLIDER_FRAME_W, SOV_X + SOV_W);
  T_TRUE(TBV_SLIDER_HIT_Y < SOV_LINE_Y);
  T_TRUE(BU_SL_HIT_Y < SOV_LINE_Y);
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

  //The Scope: screen y 49..448 onto the 19 rows, top to bottom, every row used, clipped outside
  T_EQ(sov_row(49, 49, 449), 0);
  T_EQ(sov_row(448, 49, 449), SOV_H - 1);
  T_EQ(sov_row(10, 49, 449), 0);
  T_EQ(sov_row(479, 49, 449), SOV_H - 1);
  T_EQ(sov_row(249, 49, 449), 9);                     //the centre line: the middle row
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
  T_EQ(sov_row_fit(-31500, -3000, -60000), 9);
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

int main(void)
{
  test_geometry();
  test_items();
  test_rows();
  test_lines();
  test_inside();
  return t_report("test_slider_ov");
}
