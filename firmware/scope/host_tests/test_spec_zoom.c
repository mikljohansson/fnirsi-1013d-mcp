//----------------------------------------------------------------------------------------------------------------------------------
// test_spec_zoom.c - the Spectrum's zoom ladder (spec_ladder.c, fw 0.2.77; docs/ui-guidelines.md section 2, "Span
// drives the sample rate"), mirroring test_acq_view.c for the Scope: a model of the view (cell, rate, run state) driven
// by taps and pans as spectrum.c drives it. At every FFT length: walks to both ends and back from every span and many
// centres, random tap / pan sequences and hand-picked rates, asserting after every zoom step that the rate is the one
// the rule gives for the cell (an independent reference written here), after every pan that the rate is unchanged and
// the cell inside the current axis (fw 0.2.81, owner 2026-10-20), that the ends are plain stops the other direction
// leaves at once, and that a stopped record keeps its rate. Plus the bins that draw a cell, Auto's cell and the numbers
// of the ends that firmware/AGENTS.md section 52 quotes. fw 0.2.84: the capture time is the record's, memory depth /
// fs (the model's cap; the FFT length N only sets the 16-bin end), a depth change re-derives a ladder rate at once and
// leaves a hand-picked one, an N change never changes the rate.
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

//Slowest UI rate with fs / 2 >= stop and a capture of cap samples cap / fs <= 2.048 s
static int ref_rate(double stop_hz, uint32 cap)
{
  int i;

  for(i = RATES - 1; i >= 0; i--)
    if((rates[i] / 2.0 >= stop_hz) && ((double)cap / rates[i] <= 2.048))
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
  uint32   cap;           //the record, memory depth (>= N)
  int      ladder;        //the rate is the one the ladder set (spectrum.c lad_rate): a depth change re-derives it
} model;

//After a zoom step with the rate free: the ladder's rate for the cell (spectrum.c spec_cell_apply)
static void apply(model *m)
{
  if(m->running)
  {
    m->rate = spl_rate(spl_stop(&m->c), m->cap, rates, RATES);
    m->ladder = 1;
  }
}

//A rate picked by hand in the ACQ menu: holds until the next zoom step
static void hand(model *m, int r)
{
  m->rate = r;
  m->ladder = 0;
}

//fw 0.2.84: the memory depth changed (spectrum.c spec_follow_cap): a ladder rate follows at once, a hand rate holds
static void set_cap(model *m, uint32 cap)
{
  m->cap = cap;
  if(m->running && m->ladder)
    apply(m);
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
  int   ch = spl_zoom(&m->c, dir, m->log2n, m->cap, r, n);

  if(ch)
    apply(m);
  return ch;
}

//A pan keeps the rate and stays on its axis (spectrum.c spec_pan_by)
static int pan(model *m, int64 d)
{
  return spl_pan(&m->c, d, spl_axis_mhz(&rates[m->rate], 1));
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
    T_EQ(m->rate, ref_rate(stop, m->cap));
    T_TRUE((double)m->cap / rates[m->rate] <= 2.048);
  }
  if(t_failures != fails)
    printf("  (line %d: N %d cap %u span %d centre %llu mHz rate %d running %d)\n", line, 1 << m->log2n, m->cap,
           m->c.span, (unsigned long long)m->c.centre, m->rate, m->running);
}

//After a pan: the rate as before it, the cell inside the current axis (centred on it when wider)
static void check_pan(const model *m, int rate0, int line)
{
  uint64 axis = spl_axis_mhz(&rates[m->rate], 1), s = spl_span_mhz(m->c.span);
  int    fails = t_failures;

  T_EQ(m->rate, rate0);
  T_TRUE(spl_stop(&m->c) - spl_start(&m->c) == s);
  if(s < axis)
    T_TRUE(spl_stop(&m->c) <= axis);
  else
    T_EQ(m->c.centre, axis / 2);
  if(t_failures != fails)
    printf("  (line %d: N %d span %d centre %llu mHz rate %d)\n", line, 1 << m->log2n, m->c.span,
           (unsigned long long)m->c.centre, m->rate);
}

//A record of cap samples (0: exactly N, the 0.2.77 model)
static void start_cap(model *m, int log2n, uint32 cap, int span, uint64 centre)
{
  m->log2n = log2n;
  m->cap = cap ? cap : (1u << log2n);
  m->running = 1;
  m->c.span = (uint8)span;
  m->c.centre = centre;
  spl_fit(&m->c, SPL_FMAX_MHZ);
  apply(m);
}

static void start_at(model *m, int log2n, int span, uint64 centre)
{
  start_cap(m, log2n, 0, span, centre);
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
  T_EQ(spl_rate(HZ(25000000), 2048, rates, RATES), 2);
  T_EQ(spl_rate(HZ(20000000), 2048, rates, RATES), 2);
  T_EQ(spl_rate(HZ(10000000), 2048, rates, RATES), 3);
  T_EQ(spl_rate(HZ(10000001), 2048, rates, RATES), 2);
  T_EQ(spl_rate(HZ(1000), 2048, rates, RATES), 15);      //2 kSa/s
  T_EQ(spl_rate(HZ(500), 2048, rates, RATES), 16);       //1 kSa/s, 2.048 s
  T_EQ(spl_rate(HZ(200), 2048, rates, RATES), 16);       //500 Sa/s would take 4.1 s
  T_EQ(spl_rate(HZ(200), 1024, rates, RATES), 17);       //500 Sa/s, 2.048 s
  T_EQ(spl_rate(HZ(200), 4096, rates, RATES), 15);       //2 kSa/s, 2.048 s
  T_EQ(spl_rate(HZ(30000000), 2048, &rates[2], 1), -1);

  //fw 0.2.84: the record's capture time. The owner's case, 16k memory: 2 kSa/s would take 8.2 s, 10 kSa/s 1.64 s
  T_EQ(spl_rate(HZ(200), 16382, rates, RATES), 13);      //10 kSa/s
  T_EQ(spl_rate(HZ(200), 3000, rates, RATES), 15);       //2 kSa/s, 1.5 s (1 kSa/s would take 3 s)
  T_EQ(spl_rate(HZ(200), 6000, rates, RATES), 14);       //5 kSa/s, 1.2 s
  T_EQ(spl_rate(HZ(200), 4096, rates, RATES), 15);
  T_EQ(spl_rate(HZ(200), 4097, rates, RATES), 14);
  T_EQ(spl_rate(HZ(1000000), 16382, rates, RATES), 6);   //2 MSa/s: the limit does not bite
  T_EQ(spl_rate(HZ(200), 16382, &rates[17], 1), 0);      //stopped: the record's rate, no time limit

  //Against the reference at every stop on a fine grid, at N-sized records and the offered depths
  {
    static const uint32 caps[] = { 1024, 2048, 3000, 4096, 6000, 8190, 9000, 12000, 15000, 16382 };
    unsigned k;

    for(k = 0; k < sizeof(caps) / sizeof(caps[0]); k++)
      for(f = HZ(1); f <= HZ(25000000); f += f / 7 + 1)
        T_EQ(spl_rate(f, caps[k], rates, RATES), ref_rate(f / 1000.0, caps[k]));
  }
  for(l = 10; l <= 12; l++)
    T_EQ(spl_rate(HZ(300), 1u << l, rates, RATES), ref_rate(300, 1u << l));
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
              T_TRUE(spl_ok(&m.c, l, m.cap, rates, RATES));
            if(t.span)
            {
              t.span--;
              T_EQ(spl_ok(&t, l, m.cap, rates, RATES), 0);
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

        if(!spl_ok(&c, l, 1u << l, rates, RATES))
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
      int32    r = spl_rate(spl_stop(&c), 1u << l, rates, RATES);
      uint64   bins4 = (spl_span_mhz(s) << (l + 2)) / ((uint64)rates[r] * 1000);    //4 x bins

      //Not held faster by the capture time (fw 0.2.84: before, the reference was asked for 2^30 samples, so this never
      //ran), and not under the slowest rate's axis (1 .. 100 Hz at 500 Sa/s hold fewer bins: nothing slower exists)
      if((r == ref_rate(spl_stop(&c) / 1000.0, 1)) && (r < RATES - 1))
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

//Random taps, pans (plot and slider sized) and hand-picked rates; the pair is the rule's after every zoom step, a pan
//keeps the rate
static void test_random(void)
{
  int l, i, run;

  for(l = 10; l <= 12; l++)
    for(run = 0; run < 300; run++)
    {
      model m;

      start_at(&m, l, (int)(lcg() % (SPL_FULL + 1)), (uint64)(lcg() % 25000000) * 1000);

      for(i = 0; i < 60; i++)
      {
        uint32 a = lcg() % 10;

        if(a < 3)
        {
          if(tap(&m, (a == 0) ? -1 : 1))
            check_pair(&m, __LINE__);
        }
        else if(a < 8)
        {
          //A drag: a fraction of the span, or a slider move over the whole axis
          int64 d = (a < 6) ? ((int64)(lcg() % 1441) - 720) * (int64)spl_span_mhz(m.c.span) / 720 :
                              ((int64)(lcg() % 1453) - 726) * (int64)(rates[m.rate] / 2) * 1000 / 726;
          int   r0 = m.rate;

          pan(&m, d);
          check_pan(&m, r0, __LINE__);
        }
        else
        {
          //A rate picked by hand in the ACQ menu: it holds until the next zoom step
          hand(&m, (int)(lcg() % RATES));
        }
      }
    }
}

//fw 0.2.81 (owner 2026-10-20 10:24): a pan never changes the rate and is clamped to the current axis 0 .. fs / 2; a
//zoom step after it re-derives the rate from the new centre
static void test_pan_keeps_rate(void)
{
  model m;
  int   l, sp, k, r0;

  start_at(&m, 11, 9, HZ(2000));              //1 kHz span, 1.5 .. 2.5 kHz: 5 kSa/s, axis 2.5 kHz
  T_EQ(rates[m.rate], 5000u);
  T_EQ(pan(&m, HZ(100)), 0);                  //against the axis end: clamped, nothing moves
  T_EQ(spl_stop(&m.c), HZ(2500));
  T_EQ(rates[m.rate], 5000u);
  T_EQ(pan(&m, -HZ(100)), 1);
  T_EQ(rates[m.rate], 5000u);
  T_EQ(pan(&m, -HZ(10000)), 1);               //to 0 .. 1 kHz: still 5 kSa/s (the coarse resolution is kept)
  T_EQ(spl_start(&m.c), 0);
  T_EQ(rates[m.rate], 5000u);
  T_EQ(pan(&m, -HZ(10)), 0);                  //at 0 Hz: nothing moves
  T_EQ(tap(&m, 1), 1);                        //zoom in there: 500 Hz about 500 Hz, stop 750 Hz: 2 kSa/s
  T_EQ(spl_span_mhz(m.c.span), HZ(500));
  T_EQ(m.c.centre, HZ(500));
  T_EQ(rates[m.rate], 2000u);
  check_pair(&m, __LINE__);
  T_EQ(pan(&m, HZ(30000000)), 1);             //up to the end of the 2 kSa/s axis, not past it
  T_EQ(spl_stop(&m.c), HZ(1000));
  T_EQ(rates[m.rate], 2000u);
  T_EQ(tap(&m, -1), 1);                       //zoom out: 1 kHz about 750 Hz, 250 Hz .. 1.25 kHz: 5 kSa/s
  check_pair(&m, __LINE__);
  T_EQ(rates[m.rate], 5000u);

  //A wide span panned low keeps the rate; the next zoom in takes the rate of the new centre
  start_at(&m, 11, 21, HZ(12500000));         //10 MHz about 12.5 MHz: stop 17.5 MHz, 50 MSa/s
  T_EQ(rates[m.rate], 50000000u);
  T_EQ(pan(&m, -(int64)SPL_FMAX_MHZ), 1);     //0 .. 10 MHz, still 50 MSa/s
  T_EQ(spl_start(&m.c), 0);
  T_EQ(rates[m.rate], 50000000u);
  T_EQ(tap(&m, 1), 1);                        //5 MHz about 5 MHz, stop 7.5 MHz: 20 MSa/s
  T_EQ(rates[m.rate], 20000000u);
  check_pair(&m, __LINE__);

  //The whole axis cannot pan
  start_at(&m, 11, SPL_FULL, 0);
  T_EQ(pan(&m, HZ(1000)), 0);
  T_EQ(pan(&m, -HZ(1000)), 0);

  //From every starting cell at every FFT length: pans of any size keep the rate and clamp at both ends of the axis, and
  //a zoom step after the pan is the rule's pair for the new centre
  for(l = 10; l <= 12; l++)
    for(sp = 0; sp <= SPL_FULL; sp++)
      for(k = 0; k < 64; k++)
      {
        uint64 axis, s;
        model  e;
        int    j;

        start_at(&m, l, sp, (uint64)k * (SPL_FMAX_MHZ / 63));
        r0 = m.rate;
        T_TRUE(r0 >= 0);
        if(r0 < 0)
          continue;
        axis = spl_axis_mhz(&rates[r0], 1);
        s = spl_span_mhz(m.c.span);

        for(j = 0; j < 8; j++)
        {
          pan(&m, ((int64)(lcg() % 2001) - 1000) * (int64)(axis / 500));
          check_pan(&m, r0, __LINE__);
        }

        pan(&m, (int64)SPL_FMAX_MHZ);
        check_pan(&m, r0, __LINE__);
        if(s < axis)
          T_EQ(spl_stop(&m.c), axis);
        T_EQ(pan(&m, HZ(1)), 0);

        pan(&m, -(int64)SPL_FMAX_MHZ);
        check_pan(&m, r0, __LINE__);
        if(s < axis)
          T_EQ(spl_start(&m.c), 0);
        T_EQ(pan(&m, -HZ(1)), 0);

        for(j = -1; j <= 1; j += 2)
        {
          e = m;
          if(tap(&e, j))
            check_pair(&e, __LINE__);
        }
      }
}

//A hand-picked rate holds until the next zoom step, which takes the rule's rate; a pan keeps it
static void test_hand_rate(void)
{
  model m;

  start_at(&m, 11, 12, HZ(5000));             //10 kHz span, 0 .. 10 kHz: 20 kSa/s
  T_EQ(rates[m.rate], 20000u);
  hand(&m, 0);                                //200 MSa/s by hand
  T_EQ(tap(&m, 1), 1);
  check_pair(&m, __LINE__);
  T_EQ(rates[m.rate], 20000u);                //5 kHz span about 5 kHz: stop 7.5 kHz

  hand(&m, 17);                               //500 Sa/s by hand: does not cover the cell
  T_EQ(pan(&m, HZ(1)), 1);                    //the cell is centred on the 250 Hz axis, the rate kept
  T_EQ(m.rate, 17);
  T_EQ(m.c.centre, HZ(125));
  T_EQ(tap(&m, 1), 1);                        //2 kHz from 0: 5 kSa/s
  check_pair(&m, __LINE__);
  T_EQ(rates[m.rate], 5000u);
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
      m.cap = 1u << l;
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
    T_EQ(spl_rate(spl_stop(&c), 1024, rates, RATES), r);
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

  spl_auto(&c, HZ(1000), HZ(10000), 11, 3000, rates, RATES);
  T_EQ(spl_span_mhz(c.span), HZ(20000));
  T_EQ(spl_start(&c), 0);
  T_EQ(rates[spl_rate(spl_stop(&c), 3000, rates, RATES)], 50000u);

  //Wide band, low fundamental: narrowed until RBW <= f0 / 4
  spl_auto(&c, HZ(1000), HZ(5000000), 11, 3000, rates, RATES);
  T_EQ(spl_span_mhz(c.span), HZ(100000));
  T_EQ(rates[spl_rate(spl_stop(&c), 3000, rates, RATES)], 200000u);

  //Nothing found: the whole axis
  spl_auto(&c, 0, 0, 11, 3000, rates, RATES);
  T_EQ(c.span, SPL_FULL);

  for(l = 10; l <= 12; l++)
    for(f0 = 0; f0 < HZ(30000000); f0 = f0 * 3 + HZ(1))
      for(fhi = 0; fhi < HZ(30000000); fhi = fhi * 5 + HZ(7))
      {
        model m;
        int   i, ok = 0;

        spl_auto(&c, f0, fhi, l, 1u << l, rates, RATES);
        T_EQ(spl_start(&c), 0);
        T_TRUE(spl_ok(&c, l, 1u << l, rates, RATES));

        //A cell of the ladder at the rule's rate, and the zoom leaves it both ways to the ends
        start_at(&m, l, c.span, c.centre);
        T_EQ(m.c.centre, c.centre);
        check_pair(&m, __LINE__);

        for(i = 0; i < 2; i++)
          ok += walk(&m, i ? -1 : 1) >= 0;
        T_EQ(ok, 2);
      }
}

//fw 0.2.84 (b): the capture time is the record's, memory depth / fs. At every offered depth and FFT length the walks keep
//every running pair within 2.048 s of capture; the zoom-in ends from 0 Hz in numbers (the owner's case: 16k memory, N
//4096 took 8 s at 2 kSa/s; now the ladder stops at 10 kSa/s)
static const uint32 depths[] = { 3000, 6000, 8190, 9000, 12000, 15000, 16382 };
#define DEPTHS ((int)(sizeof(depths) / sizeof(depths[0])))

static void test_capture_time(void)
{
  static const uint64 centres[] = { 0, HZ(250), HZ(4321), HZ(123456), HZ(3333333), HZ(12500000), HZ(25000000) };
  int l, d, s, k, leg;

  for(d = 0; d < DEPTHS; d++)
    for(l = 10; l <= 12; l++)
    {
      if((1u << l) > depths[d])
        continue;

      for(s = 0; s <= SPL_FULL; s += 2)
        for(k = 0; k < (int)(sizeof(centres) / sizeof(centres[0])); k++)
        {
          model m;

          start_cap(&m, l, depths[d], s, centres[k]);
          check_pair(&m, __LINE__);
          for(leg = 0; leg < 4; leg++)
            walk(&m, (leg & 1) ? -1 : 1);
          T_EQ(m.c.span, SPL_FULL);
          T_EQ(m.rate, 2);
        }

      //The narrowest cell from 0 Hz the zoom reaches: the record's capture time ends it
      for(s = SPL_FULL; s > 0; s--)
      {
        spl_cell c = { (uint8)(s - 1), spl_span_mhz(s - 1) / 2 };

        if(!spl_ok(&c, l, depths[d], rates, RATES))
          break;
      }
      //The slowest rate whose capture of the record is within 2.048 s, and the widest 1-2-5 span its axis holds:
      //3000 -> 1 kHz at 2 kSa/s; 6000 .. 9000 -> 2 kHz at 5 kSa/s; 12000 .. 16382 -> 5 kHz at 10 kSa/s
      {
        model m;
        uint32 fs = (depths[d] <= 4096) ? 2000 : (depths[d] <= 10240) ? 5000 : 10000;

        start_cap(&m, l, depths[d], s, 0);
        T_EQ(spl_span_mhz(m.c.span), HZ((fs == 5000) ? 2000 : fs / 2));
        T_EQ(rates[m.rate], fs);
        T_TRUE((uint64)depths[d] * 1000 <= (uint64)rates[m.rate] * SPL_TMAX_MS);
      }
    }
}

//fw 0.2.84 (c): the pair invariant after a depth or FFT length change. A depth change re-derives a ladder rate at once
//(and back again: the pair is a function of cell and depth), leaves a hand-picked rate and a stopped record alone; an
//N change never changes the rate (N is not in it), only where zooming in ends
static void test_follow(void)
{
  model m, e;
  int   d, d2, s, k, l;

  //The owner's case: 1 kHz from 0 at 3000 (2 kSa/s), then 16k memory: 10 kSa/s at once, and zoom in ends there
  start_cap(&m, 11, 3000, 9, HZ(500));
  T_EQ(spl_span_mhz(m.c.span), HZ(1000));
  T_EQ(rates[m.rate], 2000u);
  set_cap(&m, 16382);
  check_pair(&m, __LINE__);
  T_EQ(rates[m.rate], 10000u);
  T_EQ(spl_span_mhz(m.c.span), HZ(1000));
  e = m;
  T_EQ(tap(&e, 1), 0);
  T_EQ(tap(&e, -1), 1);
  check_pair(&e, __LINE__);
  set_cap(&m, 3000);
  T_EQ(rates[m.rate], 2000u);

  //A hand-picked rate holds through a depth change; the next zoom step takes the rule's pair at the new depth
  hand(&m, 0);
  set_cap(&m, 6000);
  T_EQ(m.rate, 0);
  T_EQ(tap(&m, -1), 1);
  check_pair(&m, __LINE__);

  //A pan keeps the ladder's rate, so a depth change after it re-derives from the panned cell
  start_cap(&m, 11, 3000, 12, HZ(50000));                //10 kHz about 50 kHz: stop 55 kHz, 200 kSa/s
  T_EQ(rates[m.rate], 200000u);
  pan(&m, -(int64)HZ(50000));                            //0 .. 10 kHz, still 200 kSa/s
  T_EQ(rates[m.rate], 200000u);
  set_cap(&m, 6000);
  check_pair(&m, __LINE__);
  T_EQ(rates[m.rate], 20000u);

  //Stopped: the record's rate stays
  start_cap(&m, 11, 3000, 9, HZ(500));
  m.running = 0;
  set_cap(&m, 16382);
  T_EQ(rates[m.rate], 2000u);

  //An N change: the rate is already the rule's (N is not in it); the zoom-in end moves with the bins
  start_cap(&m, 10, 3000, SPL_FULL, 0);
  walk(&m, 1);
  T_EQ(spl_span_mhz(m.c.span), HZ(1000000));             //16 bins at N 1024 of 50 MSa/s
  m.log2n = 11;
  check_pair(&m, __LINE__);
  T_EQ(tap(&m, 1), 1);                                   //N 2048 holds 16 bins of a narrower span
  check_pair(&m, __LINE__);
  T_EQ(spl_span_mhz(m.c.span), HZ(500000));
  m.log2n = 10;
  check_pair(&m, __LINE__);                              //past the N 1024 end: in is refused, out leaves
  T_EQ(tap(&m, 1), 0);
  T_EQ(tap(&m, -1), 1);
  check_pair(&m, __LINE__);

  //Every span, centres and depth pairs: the ladder's pair at the new depth, and back
  for(l = 10; l <= 12; l++)
    for(s = 0; s <= SPL_FULL; s++)
      for(k = 0; k < 16; k++)
        for(d = 0; d < DEPTHS; d++)
          for(d2 = 0; d2 < DEPTHS; d2++)
          {
            int r0;

            if(((1u << l) > depths[d]) || ((1u << l) > depths[d2]))
              continue;
            start_cap(&m, l, depths[d], s, (uint64)k * (SPL_FMAX_MHZ / 15));
            r0 = m.rate;
            set_cap(&m, depths[d2]);
            check_pair(&m, __LINE__);
            T_TRUE((depths[d2] > depths[d]) ? (m.rate <= r0) : (m.rate >= r0));    //longer record, never slower
            set_cap(&m, depths[d]);
            T_EQ(m.rate, r0);
          }
}

int main(void)
{
  test_spans();
  test_rate();
  test_walks();
  test_end_numbers();
  test_random();
  test_pan_keeps_rate();
  test_hand_rate();
  test_stopped();
  test_bins();
  test_auto();
  test_capture_time();
  test_follow();
  return t_report("test_spec_zoom");
}
