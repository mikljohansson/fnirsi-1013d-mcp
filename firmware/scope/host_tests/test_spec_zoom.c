//----------------------------------------------------------------------------------------------------------------------------------
// test_spec_zoom.c - the Spectrum's zoom ladder (spec_ladder.c, fw 0.2.77; docs/ui-guidelines.md section 2, "Span
// drives the sample rate"), mirroring test_acq_view.c for the Scope: a model of the view (cell, rate, run state) driven
// by taps and pans as spectrum.c drives it. At every FFT length: walks to both ends and back from every span and many
// centres, random tap / pan sequences and hand-picked rates, asserting after every step that the rate is the one the
// rule gives for the cell (an independent reference written here), that the ends are plain stops the other direction
// leaves at once, and that a stopped record keeps its rate. Plus the bins that draw a cell, Auto's cell and the numbers
// of the ends that firmware/AGENTS.md section 52 quotes.
//----------------------------------------------------------------------------------------------------------------------------------

#include "test_util.h"
#include "spec_ladder.h"

#define RATES 18

static const uint32 rates[RATES] =
{
  200000000, 100000000, 50000000, 20000000, 10000000, 5000000, 2000000, 1000000, 500000, 200000, 100000, 50000, 20000,
  10000, 5000, 2000, 1000, 500
};

#define HZ(x)  ((uint64)(x) * 1000)

//----------------------------------------------------------------------------------------------------------------------------------
// The rule, written again in the plainest way (doubles: host only)

static double ref_span_hz(int i)
{
  static const double m[3] = { 1, 2, 5 };
  double v = m[i % 3];
  int    p;

  if(i == SPL_FULL)
    return 25e6;
  for(p = 0; p < i / 3; p++)
    v *= 10;
  return v;
}

//Slowest UI rate with fs / 2 >= stop and N / fs <= 2.048 s
static int ref_rate(double stop_hz, int log2n)
{
  int i;

  for(i = RATES - 1; i >= 0; i--)
    if((rates[i] / 2.0 >= stop_hz) && ((double)(1 << log2n) / rates[i] <= 2.048))
      return i;
  return -1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// The view model: what spectrum.c keeps and does

typedef struct
{
  spl_cell c;
  int      rate;          //index into rates[]
  int      running;       //rate free (running or Single)
  int      log2n;
} model;

//After a zoom or pan step with the rate free: the ladder's rate for the cell (spectrum.c spec_cell_apply)
static void apply(model *m)
{
  if(m->running)
    m->rate = spl_rate(spl_stop(&m->c), m->log2n, rates, RATES);
}

static const uint32 *m_rates(const model *m, int32 *n)
{
  if(m->running)
  {
    *n = RATES;
    return rates;
  }
  *n = 1;
  return &rates[m->rate];
}

static int tap(model *m, int dir)
{
  int32 n;
  const uint32 *r = m_rates(m, &n);
  int   ch = spl_zoom(&m->c, dir, m->log2n, r, n);

  if(ch)
    apply(m);
  return ch;
}

static int pan(model *m, int64 d)
{
  int32 n;
  const uint32 *r = m_rates(m, &n);
  int   ch = spl_pan(&m->c, d, spl_axis_mhz(r, n));

  if(ch)
    apply(m);
  return ch;
}

//The pair is the rule's, and the cell is a sane one
static void check_pair(const model *m, int line)
{
  double stop = spl_stop(&m->c) / 1000.0;
  int    fails = t_failures;

  T_TRUE(m->c.span <= SPL_FULL);
  T_TRUE(spl_stop(&m->c) - spl_start(&m->c) == spl_span_mhz(m->c.span));
  T_TRUE(stop <= 25e6);
  T_TRUE(m->rate >= 0);
  if(m->rate < 0)
    return;
  T_TRUE(rates[m->rate] / 2.0 >= stop);
  if(m->running)
  {
    T_EQ(m->rate, ref_rate(stop, m->log2n));
    T_TRUE((double)(1 << m->log2n) / rates[m->rate] <= 2.048);
  }
  if(t_failures != fails)
    printf("  (line %d: N %d span %d centre %llu mHz rate %d running %d)\n", line, 1 << m->log2n, m->c.span,
           (unsigned long long)m->c.centre, m->rate, m->running);
}

static void start_at(model *m, int log2n, int span, uint64 centre)
{
  m->log2n = log2n;
  m->running = 1;
  m->c.span = (uint8)span;
  m->c.centre = centre;
  spl_fit(&m->c, SPL_FMAX_MHZ);
  apply(m);
}

//Taps in one direction until the end; the end is a plain stop and the other direction leaves it at once
static int walk(model *m, int dir)
{
  int steps = 0;
  model e;

  while(tap(m, dir))
  {
    check_pair(m, __LINE__);
    if(++steps > 40)
      break;
  }
  T_TRUE(steps <= 40);

  e = *m;
  T_EQ(tap(&e, dir), 0);
  T_EQ(e.c.span, m->c.span);
  T_EQ(e.c.centre, m->c.centre);
  T_EQ(e.rate, m->rate);

  e = *m;
  T_EQ(tap(&e, -dir), 1);
  check_pair(&e, __LINE__);

  return steps;
}

//----------------------------------------------------------------------------------------------------------------------------------

static void test_spans(void)
{
  int i;

  T_EQ(spl_span_mhz(0), HZ(1));
  T_EQ(spl_span_mhz(1), HZ(2));
  T_EQ(spl_span_mhz(2), HZ(5));
  T_EQ(spl_span_mhz(9), HZ(1000));
  T_EQ(spl_span_mhz(21), HZ(10000000));
  T_EQ(spl_span_mhz(22), HZ(20000000));
  T_EQ(spl_span_mhz(SPL_FULL), HZ(25000000));
  for(i = 0; i <= SPL_FULL; i++)
    T_TRUE(spl_span_mhz(i) == (uint64)(ref_span_hz(i) * 1000));
  T_EQ(spl_axis_mhz(rates, RATES), HZ(25000000));
  T_EQ(spl_axis_mhz(&rates[4], 1), HZ(5000000));
}

static void test_rate(void)
{
  int    l;
  uint64 f;

  //The full axis runs at 50 MSa/s (fs / 2 = 25 MHz), as the coordinator decided (option a)
  T_EQ(spl_rate(HZ(25000000), 11, rates, RATES), 2);
  T_EQ(spl_rate(HZ(20000000), 11, rates, RATES), 2);
  T_EQ(spl_rate(HZ(10000000), 11, rates, RATES), 3);
  T_EQ(spl_rate(HZ(10000001), 11, rates, RATES), 2);
  T_EQ(spl_rate(HZ(1000), 11, rates, RATES), 15);        //2 kSa/s
  T_EQ(spl_rate(HZ(500), 11, rates, RATES), 16);         //1 kSa/s, 2.048 s
  T_EQ(spl_rate(HZ(200), 11, rates, RATES), 16);         //500 Sa/s would take 4.1 s
  T_EQ(spl_rate(HZ(200), 10, rates, RATES), 17);         //500 Sa/s, 2.048 s
  T_EQ(spl_rate(HZ(200), 12, rates, RATES), 15);         //2 kSa/s, 2.048 s
  T_EQ(spl_rate(HZ(30000000), 11, &rates[2], 1), -1);

  //Against the reference at every stop on a fine grid
  for(l = 10; l <= 12; l++)
    for(f = HZ(1); f <= HZ(25000000); f += f / 7 + 1)
      T_EQ(spl_rate(f, l, rates, RATES), ref_rate(f / 1000.0, l));
}

//From every span and many centres at every FFT length: in to the end, out to the end, in, out
static void test_walks(void)
{
  static const uint64 centres[] = { 0, HZ(3), HZ(77), HZ(250), HZ(1000), HZ(4321), HZ(50000), HZ(123456), HZ(1000000),
                                    HZ(3333333), HZ(9999999), HZ(12500000), HZ(20000000), HZ(24999999), HZ(25000000) };
  int l, s, k, leg;

  for(l = 10; l <= 12; l++)
    for(s = 0; s <= SPL_FULL; s++)
      for(k = 0; k < (int)(sizeof(centres) / sizeof(centres[0])); k++)
      {
        model m;

        start_at(&m, l, s, centres[k]);
        check_pair(&m, __LINE__);

        for(leg = 0; leg < 4; leg++)
        {
          walk(&m, (leg & 1) ? -1 : 1);

          //Out ends at the whole axis at 50 MSa/s
          if(leg & 1)
          {
            T_EQ(m.c.span, SPL_FULL);
            T_EQ(m.c.centre, HZ(12500000));
            T_EQ(m.rate, 2);
          }
          //In ends where one more span would need a capture over 2.048 s or hold under 16 bins (on the first leg the
          //start cell may itself be past that end, e.g. 1 Hz: then nothing moves)
          else
          {
            spl_cell t = m.c;

            if(leg)
              T_TRUE(spl_ok(&m.c, l, rates, RATES));
            if(t.span)
            {
              t.span--;
              T_EQ(spl_ok(&t, l, rates, RATES), 0);
            }
          }
        }
      }
}

//The ends in numbers (section 52): zooming in from the whole axis, and from a cell at 0 Hz
static void test_end_numbers(void)
{
  int   l;
  model m;

  for(l = 10; l <= 12; l++)
  {
    //From the whole axis the centre stays at 12.5 MHz and the rate at 50 MSa/s: the end is 16 bins of 50 MHz / N
    start_at(&m, l, SPL_FULL, 0);
    walk(&m, 1);
    T_EQ(m.rate, 2);
    T_EQ(m.c.centre, HZ(12500000));
    T_EQ(spl_span_mhz(m.c.span), (l == 10) ? HZ(1000000) : (l == 11) ? HZ(500000) : HZ(200000));

    //At 0 Hz (taps keep the centre, so this is the narrowest cell from 0 that can be zoomed into): the capture time
    //ends it at 2048 and 4096 points; at 1024 the slowest rate (500 Sa/s, 2.048 s) holds and 16 bins end it
    {
      int s;

      for(s = SPL_FULL; s > 0; s--)
      {
        spl_cell c = { (uint8)(s - 1), spl_span_mhz(s - 1) / 2 };

        if(!spl_ok(&c, l, rates, RATES))
          break;
      }
      start_at(&m, l, s, 0);
      T_EQ(spl_start(&m.c), 0);
      T_EQ(spl_span_mhz(m.c.span), (l == 10) ? HZ(10) : (l == 11) ? HZ(500) : HZ(1000));
      T_EQ(rates[m.rate], (l == 10) ? 500u : (l == 11) ? 1000u : 2000u);
    }
  }

  //From 0 Hz, a cell's span holds between a quarter and a half of the N bins while the capture limit does not bite
  for(l = 10; l <= 12; l++)
  {
    int s;

    for(s = 0; s <= SPL_FULL; s++)
    {
      spl_cell c = { (uint8)s, spl_span_mhz(s) / 2 };
      int32    r = spl_rate(spl_stop(&c), l, rates, RATES);
      uint64   bins4 = (spl_span_mhz(s) << (l + 2)) / ((uint64)rates[r] * 1000);    //4 x bins

      if(r == ref_rate(spl_stop(&c) / 1000.0, 30))          //not held faster by the capture time
      {
        T_TRUE(bins4 >= (1u << l));
        T_TRUE(bins4 <= (2u << l));
      }
    }
  }
}

static uint32 lcg_s = 12345;
static uint32 lcg(void)
{
  lcg_s = lcg_s * 1103515245u + 12345u;
  return lcg_s >> 8;
}

//Random taps, pans (plot and slider sized) and hand-picked rates; the pair is the rule's after every zoom or pan step
static void test_random(void)
{
  int l, i, run;

  for(l = 10; l <= 12; l++)
    for(run = 0; run < 300; run++)
    {
      model m;
      int   hand = 0;

      start_at(&m, l, (int)(lcg() % (SPL_FULL + 1)), (uint64)(lcg() % 25000000) * 1000);

      for(i = 0; i < 60; i++)
      {
        uint32 a = lcg() % 10;
        int    ch;

        if(a < 3)
          ch = tap(&m, (a == 0) ? -1 : 1);
        else if(a < 8)
        {
          //A drag: a fraction of the span, or a slider move over the whole axis
          int64 d = (a < 6) ? ((int64)(lcg() % 1441) - 720) * (int64)spl_span_mhz(m.c.span) / 720 :
                              ((int64)(lcg() % 1453) - 726) * (int64)(rates[m.rate] / 2) * 1000 / 726;
          ch = pan(&m, d);
        }
        else
        {
          //A rate picked by hand in the ACQ menu: it holds until the next step
          m.rate = (int)(lcg() % RATES);
          hand = 1;
          continue;
        }

        if(ch)
          hand = 0;
        if(!hand)
          check_pair(&m, __LINE__);
      }
    }
}

//A pan that moves the stop across a rate boundary moves the rate
static void test_pan_boundary(void)
{
  model m;

  start_at(&m, 11, 9, HZ(2000));              //1 kHz span, 1.5 .. 2.5 kHz
  T_EQ(rates[m.rate], 5000u);
  T_EQ(pan(&m, HZ(100)), 1);                  //stop 2.6 kHz
  T_EQ(rates[m.rate], 10000u);
  T_EQ(pan(&m, -HZ(100)), 1);
  T_EQ(rates[m.rate], 5000u);
  T_EQ(pan(&m, -HZ(10000)), 1);               //to 0 .. 1 kHz
  T_EQ(spl_start(&m.c), 0);
  T_EQ(rates[m.rate], 2000u);
  T_EQ(pan(&m, -HZ(10)), 0);                  //at 0 Hz: nothing moves
  T_EQ(pan(&m, HZ(30000000)), 1);             //to the top of the axis
  T_EQ(spl_stop(&m.c), HZ(25000000));
  T_EQ(rates[m.rate], 50000000u);
  T_EQ(pan(&m, HZ(1)), 0);

  //The whole axis cannot pan
  start_at(&m, 11, SPL_FULL, 0);
  T_EQ(pan(&m, HZ(1000)), 0);
  T_EQ(pan(&m, -HZ(1000)), 0);
}

//A hand-picked rate holds until the next zoom or pan step, which takes the rule's rate
static void test_hand_rate(void)
{
  model m;

  start_at(&m, 11, 12, HZ(5000));             //10 kHz span, 0 .. 10 kHz: 20 kSa/s
  T_EQ(rates[m.rate], 20000u);
  m.rate = 0;                                 //200 MSa/s by hand
  T_EQ(tap(&m, 1), 1);
  check_pair(&m, __LINE__);
  T_EQ(rates[m.rate], 20000u);                //5 kHz span about 5 kHz: stop 7.5 kHz

  m.rate = 17;                                //500 Sa/s by hand: does not cover the cell
  T_EQ(pan(&m, HZ(1)), 1);
  check_pair(&m, __LINE__);
}

//Stopped: the record's rate is kept, zoom out ends at the widest span its axis holds, zoom in at 16 bins
static void test_stopped(void)
{
  int   l, r;
  model m;

  for(l = 10; l <= 12; l++)
    for(r = 0; r < RATES; r++)
    {
      uint64 axis = spl_axis_mhz(&rates[r], 1);

      m.log2n = l;
      m.running = 0;
      m.rate = r;
      spl_from_axis(&m.c, axis);
      T_TRUE(spl_stop(&m.c) <= axis);

      walk(&m, -1);
      T_EQ(m.rate, r);
      T_TRUE(spl_stop(&m.c) <= axis);
      T_TRUE((m.c.span == SPL_FULL) || (spl_span_mhz(m.c.span + 1) > axis) || ((m.c.span + 1 == SPL_FULL) && (axis < SPL_FMAX_MHZ)));

      walk(&m, 1);
      T_EQ(m.rate, r);
      T_TRUE(spl_span_mhz(m.c.span) * (1u << l) >= (uint64)SPL_MIN_BINS * rates[r] * 1000);
      if(m.c.span)
        T_TRUE(spl_span_mhz(m.c.span - 1) * (1u << l) < (uint64)SPL_MIN_BINS * rates[r] * 1000);

      {
        int moves = m.c.centre != spl_span_mhz(m.c.span) / 2;

        T_EQ(pan(&m, -(int64)SPL_FMAX_MHZ), moves);
      }
      T_EQ(m.rate, r);
      pan(&m, (int64)SPL_FMAX_MHZ);
      T_TRUE(spl_stop(&m.c) <= axis);
      T_EQ(m.rate, r);
    }

  //The entry cell for a rate is that rate's own cell (no rate change on entering the view) where the axis allows
  for(r = 2; r < RATES; r++)
  {
    spl_cell c;

    spl_from_axis(&c, spl_axis_mhz(&rates[r], 1));
    T_EQ(spl_start(&c), 0);
    T_EQ(spl_rate(spl_stop(&c), 10, rates, RATES), r);
  }
}

//The bins that draw a cell: they hold the whole cell when the rate covers it, at least 8 and at most one bin over on
//each side; clamped inside the axis when the rate does not
static void test_bins(void)
{
  int l, s, r, k;

  for(l = 10; l <= 12; l++)
    for(r = 0; r < RATES; r++)
    {
      uint64 bin = (uint64)rates[r] * 1000 >> l;
      uint64 capb = SPL_FMAX_MHZ / bin;
      uint32 half = (capb < (1u << (l - 1))) ? (uint32)((capb < 16) ? 16 : capb) : (1u << (l - 1));

      for(s = 0; s <= SPL_FULL; s++)
        for(k = 0; k < 7; k++)
        {
          spl_cell c = { (uint8)s, (uint64)(lcg() % 25000000) * 1000 };
          uint32 f, n;
          int    whole;

          spl_fit(&c, SPL_FMAX_MHZ);
          whole = spl_bins(&c, bin, half, &f, &n);
          T_TRUE(n >= 8);
          T_TRUE(f + n <= half);
          if(spl_stop(&c) <= (uint64)half * bin && (spl_span_mhz(s) / bin + 2 <= half))
          {
            T_EQ(whole, 1);
            T_TRUE((uint64)f * bin <= spl_start(&c));
            T_TRUE((uint64)(f + n) * bin >= spl_stop(&c));
            if(n > 8)
              T_TRUE((uint64)(n - 2) * bin < spl_span_mhz(s));
          }
        }
    }
}

//Auto lands on a ladder cell from 0 Hz the zoom can reach, and on the rule's rate for it
static void test_auto(void)
{
  spl_cell c;
  int      l;
  uint64   f0, fhi;

  spl_auto(&c, HZ(1000), HZ(10000), 11, rates, RATES);
  T_EQ(spl_span_mhz(c.span), HZ(20000));
  T_EQ(spl_start(&c), 0);
  T_EQ(rates[spl_rate(spl_stop(&c), 11, rates, RATES)], 50000u);

  //Wide band, low fundamental: narrowed until RBW <= f0 / 4
  spl_auto(&c, HZ(1000), HZ(5000000), 11, rates, RATES);
  T_EQ(spl_span_mhz(c.span), HZ(100000));
  T_EQ(rates[spl_rate(spl_stop(&c), 11, rates, RATES)], 200000u);

  //Nothing found: the whole axis
  spl_auto(&c, 0, 0, 11, rates, RATES);
  T_EQ(c.span, SPL_FULL);

  for(l = 10; l <= 12; l++)
    for(f0 = 0; f0 < HZ(30000000); f0 = f0 * 3 + HZ(1))
      for(fhi = 0; fhi < HZ(30000000); fhi = fhi * 5 + HZ(7))
      {
        model m;
        int   i, ok = 0;

        spl_auto(&c, f0, fhi, l, rates, RATES);
        T_EQ(spl_start(&c), 0);
        T_TRUE(spl_ok(&c, l, rates, RATES));

        //A cell of the ladder at the rule's rate, and the zoom leaves it both ways to the ends
        start_at(&m, l, c.span, c.centre);
        T_EQ(m.c.centre, c.centre);
        check_pair(&m, __LINE__);

        for(i = 0; i < 2; i++)
          ok += walk(&m, i ? -1 : 1) >= 0;
        T_EQ(ok, 2);
      }
}

int main(void)
{
  test_spans();
  test_rate();
  test_walks();
  test_end_numbers();
  test_random();
  test_pan_boundary();
  test_hand_rate();
  test_stopped();
  test_bins();
  test_auto();
  return t_report("test_spec_zoom");
}
