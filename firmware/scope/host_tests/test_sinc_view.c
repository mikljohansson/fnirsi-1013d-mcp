//----------------------------------------------------------------------------------------------------------------------------------
// test_sinc_view.c - host tests for sinc_view.c: the sin(x)/x interpolation of the trace between samples
//----------------------------------------------------------------------------------------------------------------------------------

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_util.h"
#include "sinc_view.h"

#define PI 3.14159265358979323846

//Every row sums to one in Q12 (a constant stays that constant) and row 0 is the identity
static void test_table(void)
{
  int p, k, sum;

  T_EQ(SINCV_PHASES, 32);                       //sincv_at takes the top five fraction bits

  for(p = 0; p < SINCV_PHASES; p++)
  {
    for(sum = 0, k = 0; k < SINCV_TAPS; k++)
      sum += sincv_lut[p][k];

    T_EQ(sum, 1 << SINCV_Q);
  }

  for(k = 0; k < SINCV_TAPS; k++)
    T_EQ(sincv_lut[0][k], (k == SINCV_TAPS / 2 - 1) ? (1 << SINCV_Q) : 0);

  //The half-way row is symmetric
  for(k = 0; k < SINCV_TAPS; k++)
    T_EQ(sincv_lut[SINCV_PHASES / 2][k], sincv_lut[SINCV_PHASES / 2][SINCV_TAPS - 1 - k]);
}

//The curve passes through every sample; a constant and the record's ends stay in range
static void test_through_samples(void)
{
  int16 v[64];
  int32 i;

  for(i = 0; i < 64; i++)
    v[i] = (int16)(49 + (rand() % 400));

  for(i = 0; i < 64; i++)
    T_EQ(sincv_at(v, 64, (uint32)i << 16), v[i]);

  //Past the last sample: the last sample
  T_EQ(sincv_at(v, 64, (63u << 16) + 30000), v[63]);
  T_EQ(sincv_at(v, 64, 200u << 16), v[63]);

  for(i = 0; i < 64; i++)
    v[i] = 300;

  for(i = 0; i < 63 * 32; i++)
    T_EQ(sincv_at(v, 64, (uint32)i << 11), 300);

  //One sample
  T_EQ(sincv_at(v, 1, 0), 300);
  T_EQ(sincv_at(v, 1, 40000), 300);
}

//A sine sampled at 200 MSa/s, on the screen's scale (400 px peak to peak at most): the worst error of the curve
//against the true sine, away from the record's ends. Inside the front end's 30 MHz it stays within 2 px (one of them
//is the samples' own rounding to pixels); straight lines between the samples are off by up to 20 px
static double worst(double f_hz, double amp, int linear)
{
  int16  v[96];
  double w = 2 * PI * f_hz / 200e6, e, worst_e = 0;
  int32  i, y;

  for(i = 0; i < 96; i++)
    v[i] = (int16)floor(250 + amp * sin(w * i + 0.4) + 0.5);

  for(i = 8 * 32; i < 88 * 32; i++)
  {
    double x = i / 32.0;

    if(linear)
    {
      int32  k = i / 32;
      double f = x - k;

      e = fabs(v[k] + f * (v[k + 1] - v[k]) - (250 + amp * sin(w * x + 0.4)));
    }
    else
    {
      y = sincv_at(v, 96, (uint32)i << 11);
      e = fabs(y - (250 + amp * sin(w * x + 0.4)));
    }

    if(e > worst_e)
      worst_e = e;
  }

  return worst_e;
}

static void test_reconstruction(void)
{
  static const double f[] = { 1e6, 5e6, 10e6, 20e6, 30e6 };
  uint32 i;

  for(i = 0; i < sizeof(f) / sizeof(f[0]); i++)
  {
    double s = worst(f[i], 190, 0), l = worst(f[i], 190, 1);

    printf("sinc view: %4.0f MHz sine, 380 px peak to peak: worst error %.2f px (straight lines %.1f px)\n", f[i] / 1e6, s, l);
    T_TRUE(s <= 2.0);
  }

  //Straight lines miss the crests: by 2.5 px at 10 MHz (the calibration post), by 20 px at 30 MHz; the curve does not
  T_TRUE(worst(10e6, 190, 1) > 2.0);
  T_TRUE(worst(30e6, 190, 1) > 15.0);
}

//A step between two flat levels (the samples of a fast edge): the overshoot stays small and the value stays an int32
static void test_step(void)
{
  int16 v[40];
  int32 i, y, lo = 1000, hi = -1000;

  for(i = 0; i < 40; i++)
    v[i] = (i < 20) ? 100 : 400;

  for(i = 0; i < 39 * 32; i++)
  {
    y = sincv_at(v, 40, (uint32)i << 11);

    if(y < lo) lo = y;
    if(y > hi) hi = y;
  }

  printf("sinc view: one-sample step 100 -> 400: curve %d .. %d\n", (int)lo, (int)hi);
  T_TRUE((lo >= 100 - 45) && (hi <= 400 + 45));
}

int main(void)
{
  srand(1);
  test_table();
  test_through_samples();
  test_reconstruction();
  test_step();

  return t_report("test_sinc_view");
}
