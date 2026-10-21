// test_tb_view.c - the record behind the scope trace (fw 0.2.64, tb_view.c): time/div viability derived from the record
// length, the snap after a sample-rate change, the trigger point limits that keep the window inside the record, the
// drawn window, the record-position slider and its drag. docs/plans/task-queue.md "ACQ: sample rate vs time/div
// coupling". Run natively.

#include <stdio.h>
#include <string.h>

#include "tb_view.h"
#include "test_util.h"

//variables.c tables (1.00o5): divisions per second per time/div index, samples per second per rate index (short mode)
static const uint32 fpd[35] =
{
  1, 2, 4, 8, 20, 40, 80, 200, 400, 800, 2000,
  5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000,
  500000, 1000000, 2000000, 5000000, 10000000, 20000000, 50000000, 100000000, 200000000
};

static const uint32 rate[18] =
{
  200000000, 100000000, 50000000, 20000000, 10000000, 5000000, 2000000, 1000000, 500000,
  200000, 100000, 50000, 20000, 10000, 5000, 2000, 1000, 500
};

//time_per_div_sample_rate (the coupled rate of a time/div: plot tap zoom, SET_TIMEBASE mode 0)
static const uint8 coupled[35] =
{
  28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18,
  17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0
};

//1.00o5's viable_time_per_div rows 0..17 (short mode), removed from variables.c in 0.2.64
static const uint8 old_viable[18][35] =
{
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

static const uint32 depths[] = { 3000, 6000, 8190, 9000, 12000, 15000, 16382 };
#define NDEPTH ((int)(sizeof(depths) / sizeof(depths[0])))

//Sample index read at trace column x for first sample f (the drawing steps 1 / xpps per column from column 3)
static int64_t index_at(int32 f, int32 x, uint32 fs, uint32 d)
{
  return (int64_t)f + ((int64_t)(x - TBV_X_FIRST) * fs) / ((int64_t)TBV_PX_PER_DIV * d);
}

static void test_viability(void)
{
  int r, t, k, lo, hi, n;

  for(k = 0; k < NDEPTH; k++)
  {
    for(r = 0; r < 18; r++)
    {
      //A contiguous, non-empty set of time/div per rate
      lo = -1;
      hi = -1;
      n  = 0;

      for(t = 11; t <= 34; t++)
      {
        if(tbv_viable(rate[r], fpd[t], depths[k]))
        {
          if(lo < 0)
            lo = t;
          hi = t;
          n++;

          //Every viable pair: at least ten samples per division (fw 0.2.69), the record spans the trace area
          T_TRUE(rate[r] >= (uint64)fpd[t] * TBV_MIN_SAMPLES_PER_DIV);
          T_TRUE((int64_t)TBV_SPAN_PX * rate[r] < (int64_t)depths[k] * TBV_PX_PER_DIV * fpd[t]);
        }
      }

      T_TRUE(n > 0);
      T_EQ(n, hi - lo + 1);
    }

    //The coupled rate of every short time/div (plot tap zoom, SET_TIMEBASE mode 0) is viable at every depth down to
    //50 ns/div (index 31); 20, 10 and 5 ns/div hold fewer than ten samples at any rate (fw 0.2.69) and are never viable
    for(t = 11; t <= 31; t++)
      T_TRUE(tbv_viable(rate[coupled[t]], fpd[t], depths[k]));
    for(t = 32; t <= 34; t++)
      for(r = 0; r < 18; r++)
        T_TRUE(!tbv_viable(rate[r], fpd[t], depths[k]));
  }

  //Edges at the default depth: 200 MSa/s reaches 1 us/div (200 samples/div) and 50 ns/div (ten); 2 us/div would need
  //400, 20 ns/div holds four (fw 0.2.69; one sample per division was enough before)
  T_TRUE(tbv_viable(200000000, 1000000, 3000));
  T_TRUE(!tbv_viable(200000000, 500000, 3000));
  T_TRUE(tbv_viable(200000000, 20000000, 3000));
  T_TRUE(!tbv_viable(200000000, 50000000, 3000));
  T_TRUE(!tbv_viable(200000000, 200000000, 3000));
  //The owner's bench case (2026-10-19): 100 kSa/s at 10 us/div is one sample per division; 100 us/div (ten) is viable
  T_TRUE(!tbv_viable(100000, 100000, 3000));
  T_TRUE(tbv_viable(100000, 10000, 3000));
  //Deeper memory: 200 MSa/s at 15000 reaches 5 us/div (1000 samples/div, record 75 div)
  T_TRUE(tbv_viable(200000000, 200000, 15000));
  T_TRUE(!tbv_viable(200000000, 100000, 15000));
  //Degenerate inputs
  T_TRUE(!tbv_viable(0, 1, 3000));
  T_TRUE(!tbv_viable(1, 0, 3000));
  T_TRUE(!tbv_viable(1000, 5, 1));
}

//The hand-written table against the derivation at 3000 samples: it is not reproduced, so the table goes. Every cell
//where they disagree is explained: the table offered 250 samples/div (the record, 3000 / 250 = 12 div, ends before
//the 14.5 div trace area: zeros past the record end), or withheld 200 samples/div (fits: 15 div), or offered fewer
//than ten samples per division (fw 0.2.69; 74 cells: 3 at 250 samples/div, 71 below ten samples/div; 4 cells at 200
//withheld; with the one-sample rule of 0.2.64 it was 20 cells, 17 below one sample/div)
static void test_against_old_table(void)
{
  int r, t, diff = 0, only_old = 0, only_new = 0;

  for(r = 0; r < 18; r++)
  {
    for(t = 11; t <= 34; t++)
    {
      int now = tbv_viable(rate[r], fpd[t], 3000);
      uint64 spd100 = ((uint64)rate[r] * 100) / fpd[t];     //samples per division x 100

      if(now == old_viable[r][t])
        continue;

      diff++;

      if(old_viable[r][t])
      {
        only_old++;
        T_TRUE((spd100 == 25000) || (spd100 < 100 * TBV_MIN_SAMPLES_PER_DIV));
      }
      else
      {
        only_new++;
        T_TRUE(spd100 == 20000);
      }
    }
  }

  printf("test_tb_view: derived vs 1.00o5 table at 3000 samples: %d cells differ (%d only in the table, %d only derived)\n",
         diff, only_old, only_new);
  T_EQ(only_old, 74);
  T_EQ(only_new, 4);
}

static void test_snap(void)
{
  //The owner's case: 1 kHz at 500 us/div (index 19), then 200 MSa/s: 1 us/div (index 27), the slowest that fits
  T_EQ(tbv_snap(200000000, fpd, 11, 34, 19, 3000), 27);
  //Already viable: unchanged
  T_EQ(tbv_snap(1000000, fpd, 11, 34, 21, 3000), 21);
  //500 Sa/s at 5 ns/div: 20 ms/div (index 14, ten samples per division; fw 0.2.69)
  T_EQ(tbv_snap(500, fpd, 11, 34, 34, 3000), 14);
  //The owner's bench case: 100 kSa/s at 10 us/div (index 24) snaps to 100 us/div (index 21)
  T_EQ(tbv_snap(100000, fpd, 11, 34, 24, 3000), 21);
  //Deeper memory moves the slow end: 200 MSa/s at 15000 samples from 500 us/div to 5 us/div (index 25)
  T_EQ(tbv_snap(200000000, fpd, 11, 34, 19, 15000), 25);
  //Nothing viable (a two-sample record)
  T_EQ(tbv_snap(200000000, fpd, 11, 34, 19, 2), -1);
}

//fw 0.2.68 (owner: after a rate change the snapped time/div cycled until it was set by hand once): the snap is a
//manual set of the snapped value. Every rate, depth, trigger sample, start time/div and trigger point: the state after
//tbv_fit equals the state after tbv_set with the same index from the same start; the result is viable with the trigger
//point inside the record; a second fit, and a manual set of the value already set, change nothing.
static void test_snap_is_manual_set(void)
{
  static const int32 starts[] = { -100000, -2000, 0, 3, 362, 700, 726, 5000, 100000 };
  int r, d, k, t, p;

  for(r = 0; r < 18; r++)
    for(d = 0; d < NDEPTH; d++)
      for(k = 0; k < 3; k++)
      {
        uint32 total = depths[d];
        uint32 trig  = (k == 0) ? total / 4 : (k == 1) ? total / 2 : total - 1000;

        for(t = 11; t <= 34; t++)
          for(p = 0; p < (int)(sizeof(starts) / sizeof(starts[0])); p++)
          {
            tbv_tb a, b, c;
            int32  lo, hi;
            int    ch;

            a.tpd = b.tpd = t;
            a.pos = b.pos = starts[p];

            ch = tbv_fit(&a, rate[r], fpd, 11, 34, total, trig);

            if(!ch)
            {
              //Viable (or nothing is): only the trigger point is clamped, as a manual set of the same index does
              T_EQ(a.tpd, t);
              tbv_set(&b, t, rate[r], fpd, total, trig);
              T_EQ(a.pos, b.pos);
              continue;
            }

            tbv_set(&b, a.tpd, rate[r], fpd, total, trig);
            T_EQ(a.tpd, b.tpd);
            T_EQ(a.pos, b.pos);
            T_TRUE(tbv_viable(rate[r], fpd[a.tpd], total));
            tbv_pos_limits(rate[r], fpd[a.tpd], total, trig, &lo, &hi);
            T_TRUE((a.pos >= lo) && (a.pos <= hi));

            c = a;
            T_EQ(tbv_fit(&c, rate[r], fpd, 11, 34, total, trig), 0);
            T_EQ(c.tpd, a.tpd);
            T_EQ(c.pos, a.pos);
            tbv_set(&c, a.tpd, rate[r], fpd, total, trig);
            T_EQ(c.pos, a.pos);
          }
      }
}

static void test_limits_and_window(void)
{
  int r, t, k, x;
  int32 lo, hi, f, xend;

  for(k = 0; k < NDEPTH; k++)
  {
    uint32 total = depths[k];
    uint32 trigs[3] = { total / 4, total / 2, total - 1000 };
    int    j;

    for(j = 0; j < 3; j++)
    {
      for(r = 0; r < 18; r++)
      {
        for(t = 11; t <= 34; t++)
        {
          uint32 fs = rate[r], d = fpd[t], trig = trigs[j];

          tbv_pos_limits(fs, d, total, trig, &lo, &hi);
          T_TRUE(lo <= hi);

          if(tbv_viable(fs, d, total))
          {
            //Both ends keep the window inside the record, with the nominal trigger sample
            tbv_window((int32)trig, hi, fs, d, total, &f, &xend);
            T_EQ(xend, TBV_X_END);
            T_TRUE(f >= 0);
            T_TRUE(index_at(f, TBV_X_END, fs, d) <= (int64_t)total - 1);

            //At hi the record starts at the left edge (within one column's worth of samples)
            T_TRUE(f <= (int64_t)fs / ((int64_t)TBV_PX_PER_DIV * d) + 1);

            tbv_window((int32)trig, lo, fs, d, total, &f, &xend);
            T_TRUE(f >= 0);
            T_TRUE(index_at(f, TBV_X_END, fs, d) <= (int64_t)total - 1);

            //Beyond the limits the window is clamped, not shifted out of the record
            tbv_window((int32)trig, hi + 5000, fs, d, total, &f, &xend);
            T_EQ(f, 0);
            tbv_window((int32)trig, lo - 5000, fs, d, total, &f, &xend);
            T_TRUE(index_at(f, TBV_X_END, fs, d) <= (int64_t)total - 1);

            //A trigger found a little off the nominal point
            tbv_window((int32)trig + 40, lo, fs, d, total, &f, &xend);
            T_TRUE(index_at(f, TBV_X_END, fs, d) <= (int64_t)total - 1);
            tbv_window((int32)trig - 40, hi, fs, d, total, &f, &xend);
            T_TRUE(f >= 0);
          }
          else
          {
            //Record shorter than the trace area: from the left edge, and nothing drawn past the record's end
            if((int64_t)TBV_SPAN_PX * fs >= (int64_t)total * TBV_PX_PER_DIV * d)
            {
              T_EQ(lo, hi);
              tbv_window((int32)trig, hi, fs, d, total, &f, &xend);
              T_EQ(f, 0);
              T_TRUE(xend <= TBV_X_END);
              T_TRUE(xend > TBV_X_FIRST);
              x = xend - 1;
              T_TRUE(index_at(f, x, fs, d) <= (int64_t)total - 1);
            }
          }
        }
      }
    }
  }

  //Clamp
  tbv_pos_limits(200000000, 1000000, 3000, 1500, &lo, &hi);
  T_EQ(tbv_clamp_pos(hi + 10, 200000000, 1000000, 3000, 1500), hi);
  T_EQ(tbv_clamp_pos(lo - 10, 200000000, 1000000, 3000, 1500), lo);
  T_EQ(tbv_clamp_pos(lo, 200000000, 1000000, 3000, 1500), lo);

  //1.00o5's first sample formula for an ordinary case: trig 1500, 2 px per sample, pos 357: 1500 - 178.5 - 1 -> 1320
  tbv_window(1500, 357, 25000000, 1000000, 3000, &f, &xend);
  T_EQ(f, 1320);
  T_EQ(xend, TBV_X_END);
}

static void test_slider(void)
{
  int32 x, w, d, lo, hi;

  //3000 samples at 200 MSa/s, 1 us/div: about 2893 visible of 3000, the window nearly the whole track
  tbv_slider(0, 200000000, 1000000, 3000, &x, &w);
  T_EQ(x, TBV_SLIDER_X);
  T_TRUE((w > 690) && (w <= TBV_SLIDER_W));

  //At the record's end the window ends at the track's end
  tbv_slider(3000 - 2893, 200000000, 1000000, 3000, &x, &w);
  T_TRUE(x + w <= TBV_SLIDER_X + TBV_SLIDER_W);
  T_TRUE(x + w >= TBV_SLIDER_X + TBV_SLIDER_W - 1);

  //Zoomed in a long way: the minimum width, still inside the track at the end
  tbv_slider(14999, 200000000, 200000000, 15000, &x, &w);
  T_EQ(w, TBV_SLIDER_MIN_W);
  T_EQ(x + w, TBV_SLIDER_X + TBV_SLIDER_W);

  //A short record: the whole track
  tbv_slider(0, 200000000, 2000, 3000, &x, &w);
  T_EQ(x, TBV_SLIDER_X);
  T_EQ(w, TBV_SLIDER_W);

  //Negative first sample is taken as 0
  tbv_slider(-5, 200000000, 1000000, 3000, &x, &w);
  T_EQ(x, TBV_SLIDER_X);

  //Drag right: later samples, the trigger point moves left; the whole track moves by the whole record
  d = tbv_slider_move(10, 25000000, 1000000, 3000);                //2 px per sample: 10 px = 41.3 samples = 82 px
  T_EQ(d, -82);                                                    //(fw 0.2.73: the track is 726 px)
  T_EQ(tbv_slider_move(-10, 25000000, 1000000, 3000), 82);
  T_EQ(tbv_slider_move(0, 25000000, 1000000, 3000), 0);
  T_EQ(tbv_slider_move(TBV_SLIDER_W, 25000000, 1000000, 3000), -6000);

  //The slider follows the finger: moving the window by its own track position moves the first sample by as much
  tbv_pos_limits(25000000, 1000000, 3000, 1500, &lo, &hi);
  {
    int32 f0, f1, xe, x0, x1;

    tbv_window(1500, hi, 25000000, 1000000, 3000, &f0, &xe);
    tbv_slider(f0, 25000000, 1000000, 3000, &x0, &w);
    d = tbv_slider_move(100, 25000000, 1000000, 3000);
    tbv_window(1500, tbv_clamp_pos(hi + d, 25000000, 1000000, 3000, 1500), 25000000, 1000000, 3000, &f1, &xe);
    tbv_slider(f1, 25000000, 1000000, 3000, &x1, &w);
    T_TRUE((x1 - x0 >= 99) && (x1 - x0 <= 101));
  }
}

int main(void)
{
  test_viability();
  test_against_old_table();
  test_snap();
  test_snap_is_manual_set();
  test_limits_and_window();
  test_slider();
  return t_report("test_tb_view");
}
