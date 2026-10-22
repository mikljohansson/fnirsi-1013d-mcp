// test_acal_track.c - background interleave-mismatch tracking (acal_track.c, docs/plans/interleave-tracking.md section 7),
// run natively: a drifting two-core model of one channel (per-core offset, gain, bow, Gaussian noise), the v3 correction
// with stale stored values (acal_correction), the tracker's correction folded in (acal_track_apply), the readout loop of
// fpga_read_adc_data (gain, offset, dither, clamp, rail rule), and the tracker fed with the resulting trace buffers.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "adc_cal.h"
#include "acal_track.h"
#include "test_util.h"

#define NS        3000            //samples per record (trace buffer of one channel)
#define REC_MS    50              //records come at 20 per second (simulated)
#define TAU       256
#define PI        3.14159265358979323846

//----------------------------------------------------------------------------------------------------------------------------------
//Deterministic noise

static uint64_t rs = 0x9E3779B97F4A7C15ull;

static uint64_t rnd64(void)
{
  rs ^= rs >> 12; rs ^= rs << 25; rs ^= rs >> 27;
  return rs * 0x2545F4914F6CDD1Dull;
}

static double urand(void)
{
  return ((double)(rnd64() >> 11) + 0.5) / 9007199254740992.0;
}

static double gauss(void)
{
  static int have = 0;
  static double next;
  double r, a;

  if(have)
  {
    have = 0;
    return next;
  }

  r = sqrt(-2.0 * log(urand()));
  a = 2.0 * PI * urand();
  next = r * sin(a);
  have = 1;
  return r * cos(a);
}

//----------------------------------------------------------------------------------------------------------------------------------
//The channel model

typedef struct
{
  double off[2];                          //[A, B] zero of the core relative to 128, codes
  double gain[2];                         //raw codes per input code
  double bow;                             //core A: + bow ((level - 128) / 42)^2 codes
  double sigma;                           //noise RMS, codes
} model;

enum { SIG_QUIET, SIG_SINE, SIG_SQUARE, SIG_SINE40, SIG_MIX };

static const uint16_t pwm[ACAL_RANGES] = { 904, 900, 908, 903, 908, 903 };
static acal_channel chan;                 //stored v3 values (taken from the model at t = 0, stale afterwards)
static uint32_t dither = 0x12345678u;     //fpga_dither
static uint32_t now_ms = 1000;
static int      sq_level;                 //the post square: level of the next record

static void store_channel(const model *md)
{
  uint32_t r, k, cls;

  memset(&chan, 0, sizeof(chan));

  for(r=0;r<ACAL_RANGES;r++)
  {
    chan.pwm[r] = pwm[r];

    for(cls=0;cls<ACAL_CLASSES;cls++)
      for(k=0;k<2;k++)
      {
        chan.zero[cls][k][r]  = (int16_t)lround((128.0 + md->off[k]) * 16.0);
        chan.slope[cls][k][r] = -17000;
        chan.noise[cls][k][r] = 16;
      }
  }

  for(cls=0;cls<ACAL_CLASSES;cls++)
    for(k=0;k<2;k++)
      chan.gain[cls][k] = (uint16_t)lround(32768.0 / md->gain[k]);

  chan.gain_range = 2;
}

//The v3 correction of a full-rate readout (range 2, dcoffset 0, divider 0) with the tracker folded in
static void correction(const acal_track *t, acal_corr *c)
{
  acal_correction(&chan, pwm, 2, 0, 0, 385, 0, 0, c);

  if(t)
    acal_track_apply(t, c);
}

//Noise-free level of core k (0 = A) for an input of v codes about mid-scale
static double level(const model *md, int k, double v)
{
  double l = 128.0 + md->off[k] + md->gain[k] * v;

  if(k == 0)
    l += md->bow * ((l - 128.0) / 42.0) * ((l - 128.0) / 42.0);

  return l;
}

//Mean corrected code of core k at input v (the dither makes the loop's mean exactly (raw x gain + off) / 65536)
static double code_mean(const model *md, const acal_corr *c, int k, double v)
{
  return (level(md, k, v) * (k ? c->gain_b : c->gain_a) + (k ? c->off_b : c->off_a)) / 65536.0;
}

static double split_at(const model *md, const acal_corr *c, double v)
{
  return code_mean(md, c, 0, v) - code_mean(md, c, 1, v);
}

//The input of one record, codes about mid-scale
static void signal(int sig, double *in)
{
  double ph = 2.0 * PI * urand(), f = 0.0, a = 40.0;
  uint32_t n;

  if(sig == SIG_MIX)
    sig = (rnd64() & 1) ? SIG_SINE : SIG_QUIET;

  if(sig == SIG_SINE)
    f = 1e6;
  else if(sig == SIG_SINE40)
    f = 40e6;

  for(n=0;n<NS;n++)
  {
    if(sig == SIG_QUIET)
      in[n] = 0.0;
    else if(sig == SIG_SQUARE)
      in[n] = sq_level ? 53.0 : -35.0;
    else
      in[n] = a * sin(2.0 * PI * f * n * 5e-9 + ph);
  }

  if(sig == SIG_SQUARE)
    sq_level ^= 1;
}

//fpga_read_adc_data for one channel: core A pass (odd positions), then core B (even positions) with the rail rule
static void readout(const model *md, const acal_corr *c, const double *in, uint8_t *buf)
{
  int32_t raw, s, first;
  uint32_t n;
  int k;

  for(k=0;k<2;k++)
  {
    int32_t gain = (int32_t)(k ? c->gain_b : c->gain_a);
    int32_t off  = k ? c->off_b : c->off_a;

    for(n=(k ? 0 : 1);n<NS;n+=2)
    {
      raw = (int32_t)lround(level(md, k, in[n]) + md->sigma * gauss());
      raw = (raw < 0) ? 0 : ((raw > 255) ? 255 : raw);

      ACAL_DITHER_STEP(dither);
      s = (raw * gain + off + (int32_t)(dither >> 16)) >> 16;
      s = (s < 0) ? 0 : ((s > 255) ? 255 : s);

      if(k)
      {
        first = buf[n + 1];

        if((s < c->clip_lo) || (first < c->clip_lo))
        {
          if(first < s)
            s = first;
          else
            buf[n + 1] = (uint8_t)s;
        }
        else if((s > c->clip_hi) || (first > c->clip_hi))
        {
          if(first > s)
            s = first;
          else
            buf[n + 1] = (uint8_t)s;
        }
      }

      buf[n] = (uint8_t)s;
    }
  }
}

static uint8_t buf[NS];
static double  inp[NS];

//One record through the readout and the tracker; returns acal_track_record's result
static int step(acal_track *t, const model *md, int sig)
{
  acal_corr c;
  int32_t ad = t->d_q8, ae = t->e_q24;

  correction(t, &c);
  signal(sig, inp);
  readout(md, &c, inp, buf);
  now_ms += REC_MS;
  return acal_track_record(t, buf, NS, 0, ad, ae, now_ms);
}

//Measured split and fs/2 amplitude over nrec quiet records with the current correction (not fed to the tracker)
static void probe_quiet(const acal_track *t, const model *md, int nrec, double *split, double *fs2)
{
  acal_corr c;
  double sa = 0.0, sb = 0.0, alt = 0.0;
  uint32_t n;
  int i;

  correction(t, &c);

  for(i=0;i<nrec;i++)
  {
    signal(SIG_QUIET, inp);
    readout(md, &c, inp, buf);

    for(n=0;n<NS;n++)
    {
      if(n & 1)
        sa += buf[n];
      else
        sb += buf[n];

      alt += (n & 1) ? buf[n] : -(double)buf[n];
    }
  }

  *split = (sa - sb) / (nrec * NS / 2);
  *fs2   = fabs(alt) / (nrec * NS);
}

//Open-loop truth: least-squares d, e, q of the v3-corrected A - B against x over the input range lo..hi
static void truth_fit(const model *md, double lo, double hi, double *d, double *e, double *q)
{
  acal_corr c;
  double a[3][4] = { { 0 } }, x, y, p[3], f;
  int i, j, r;

  correction(0, &c);

  for(i=0;i<=200;i++)
  {
    double v = lo + (hi - lo) * i / 200.0;
    double ca = code_mean(md, &c, 0, v), cb = code_mean(md, &c, 1, v);

    x = (ca + cb) / 2.0 - 128.0;
    y = ca - cb;
    p[0] = 1.0; p[1] = x; p[2] = x * x;

    for(r=0;r<3;r++)
    {
      for(j=0;j<3;j++)
        a[r][j] += p[r] * p[j];

      a[r][3] += p[r] * y;
    }
  }

  for(r=0;r<3;r++)
    for(i=r+1;i<3;i++)
    {
      f = a[i][r] / a[r][r];

      for(j=r;j<4;j++)
        a[i][j] -= f * a[r][j];
    }

  for(r=2;r>=0;r--)
  {
    for(j=r+1;j<3;j++)
      a[r][3] -= a[r][j] * a[j][3];

    a[r][3] /= a[r][r];
  }

  *d = a[0][3]; *e = a[1][3]; *q = a[2][3];
}

static double tq8(const acal_track *t)  { return t->d_q8 / 256.0; }
static double te24(const acal_track *t) { return t->e_q24 / 16777216.0; }
static double tqq(const acal_track *t)  { return t->q_q24 / 16777216.0; }

static void reset_model(model *md)
{
  md->off[0] = 3.9; md->off[1] = 0.0;
  md->gain[0] = 1.0; md->gain[1] = 1.0;
  md->bow = 0.0;
  md->sigma = 1.0;
}

//----------------------------------------------------------------------------------------------------------------------------------
//(a) 1.7-code step of core A's offset

static void test_offset_step(void)
{
  acal_track t;
  model md;
  double split, fs2, an, worst = 0.0;
  int i, conv = 0;

  reset_model(&md);
  store_channel(&md);
  acal_track_reset(&t);

  for(i=0;i<1536;i++)
    step(&t, &md, SIG_MIX);

  probe_quiet(&t, &md, 16, &split, &fs2);
  printf("(a) before the step: d %.4f e %.6f, quiet split %.4f fs/2 %.4f\n", tq8(&t), te24(&t), split, fs2);

  md.off[0] += 1.7;

  for(i=1;i<=1536;i++)
  {
    step(&t, &md, SIG_MIX);

    if(i % 16 == 0)
    {
      acal_corr c;

      correction(&t, &c);
      an = split_at(&md, &c, 0.0);
      probe_quiet(&t, &md, 16, &split, &fs2);

      //conv: the first probe after the last one at or above 0.1 code
      if((fabs(split) >= 0.1) || (fs2 >= 0.1) || (fabs(an) >= 0.1))
        conv = i + 16;

      if(i == 64 || i == 256 || i == 512 || i == 768 || i == 1536)
        printf("(a) %4d records after the step: d %.4f, split model %.4f measured %.4f, fs/2 %.4f\n", i, tq8(&t), an,
               split, fs2);

      if(i > 768 && fabs(split) > worst)
        worst = fabs(split);
    }
  }

  printf("(a) below 0.1 code (model split, measured split and fs/2; probes every 16 records) from %d records after the "
         "step on (%.1f s, %.2f tau), worst measured split after 3 tau %.4f\n", conv, conv * REC_MS / 1000.0,
         conv / (double)TAU, worst);
  T_TRUE(conv <= 3 * TAU);
}

//----------------------------------------------------------------------------------------------------------------------------------
//(b) 3 % gain drift of core A over 60 s on the sine

static void test_gain_drift(void)
{
  acal_track t;
  acal_corr c;
  model md;
  double sp, sn, d, e, q, worst = 0.0;
  int i, conv = 0;

  reset_model(&md);
  store_channel(&md);
  acal_track_reset(&t);

  for(i=0;i<1024;i++)
    step(&t, &md, SIG_SINE);

  for(i=1;i<=1200;i++)
  {
    md.gain[0] = 1.0 + 0.03 * i / 1200.0;
    step(&t, &md, SIG_SINE);
  }

  correction(&t, &c);
  truth_fit(&md, -40, 40, &d, &e, &q);
  printf("(b) ramp end: e %.6f (truth %.6f), split at +40 %.4f, at -40 %.4f\n", te24(&t), e, split_at(&md, &c, 40.0),
         split_at(&md, &c, -40.0));

  for(i=1;i<=1536;i++)
  {
    step(&t, &md, SIG_SINE);
    correction(&t, &c);
    sp = split_at(&md, &c, 40.0);
    sn = split_at(&md, &c, -40.0);

    //conv: the first record after the last one at or above 0.1 code
    if((fabs(sp) >= 0.1) || (fabs(sn) >= 0.1))
      conv = i + 1;

    if(i > 768)
    {
      worst = (fabs(sp) > worst) ? fabs(sp) : worst;
      worst = (fabs(sn) > worst) ? fabs(sn) : worst;
    }

    if(i == 256 || i == 768 || i == 1536)
      printf("(b) %4d records after the ramp: e %.6f d %.4f, split at +40 %.4f at -40 %.4f\n", i, te24(&t), tq8(&t), sp, sn);
  }

  printf("(b) split at +-40 below 0.1 code from %d records after the ramp end on (%.1f s, %.2f tau), worst after 3 tau "
         "%.4f\n", conv, conv * REC_MS / 1000.0, conv / (double)TAU, worst);
  T_TRUE(conv <= 3 * TAU);
}

//----------------------------------------------------------------------------------------------------------------------------------
//(c) bow of 1 code at 42 codes: q learned on the sine, then the two-level square only

//Spread v (codes^2) and the norm w of the quadratic part (codes^4) of the accumulated x, in double
static void moments_vw(const acal_track *t, double *v, double *w)
{
  double m[5], c3;
  int k;

  for(k=0;k<5;k++)
    m[k] = (double)t->m[k] / (double)t->m[0] / pow(4.0, k);

  *v = m[2] - m[1] * m[1];
  c3 = m[3] - m[2] * m[1];
  *w = (m[4] - m[2] * m[2]) - c3 * c3 / *v;
}

static void test_bow(void)
{
  acal_track t;
  acal_corr c;
  model md;
  double d, e, q, emax = 0.0, ebias, v, w;
  int i;

  reset_model(&md);
  md.bow = 1.0;
  store_channel(&md);
  acal_track_reset(&t);
  truth_fit(&md, -40, 40, &d, &e, &q);

  for(i=0;i<1536;i++)
    step(&t, &md, SIG_SINE);

  printf("(c) after the sine: q %.3e per code^2 (truth %.3e = 1/42^2 %.3e), e %.6f (truth %.6f), d %.4f\n", tqq(&t), q,
         1.0 / (42.0 * 42.0), te24(&t), e, tq8(&t));
  T_TRUE(fabs(tqq(&t) - q) < 0.1 * q);

  for(i=1;i<=3072;i++)
  {
    step(&t, &md, SIG_SQUARE);
    emax = (fabs(te24(&t) - e) > emax) ? fabs(te24(&t) - e) : emax;

    if(i == 256 || i == 768 || i == 1536 || i == 3072)
    {
      correction(&t, &c);
      printf("(c) square %4d records: q %.3e e %.6f d %.4f flags %02x, residual split at -35 %.3f (q x^2 %.3f), at +53 "
             "%.3f (q x^2 %.3f)\n", i, tqq(&t), te24(&t), tq8(&t), t.flags, split_at(&md, &c, -35.0), q * 35 * 35,
             split_at(&md, &c, 53.0), q * 53 * 53);
    }
  }

  printf("(c) square only: max |e - truth| %.6f over 3072 records (bound 0.0005)\n", emax);
  T_TRUE(emax < 0.0005);

  //Without q learned: the square alone (q held at 0) biases e by about q (x1 + x2)
  acal_track_reset(&t);
  sq_level = 0;

  for(i=0;i<1536;i++)
    step(&t, &md, SIG_SQUARE);

  ebias = te24(&t) - e;
  moments_vw(&t, &v, &w);
  printf("(c) square from reset (q never learned): q %.3e flags %02x, e %.6f, bias %.6f (expected q (x1 + x2) = %.6f); "
         "v %.0f codes^2, w %.0f codes^4 (noise within the levels)\n", tqq(&t), t.flags, te24(&t), ebias, q * 18.0, v, w);
  T_EQ(t.q_q24, 0);
  T_EQ(t.flags & ACAL_TRK_BOW, 0);

  //A quiet input mixed with the square (three levels): q identifiable
  acal_track_reset(&t);

  for(i=0;i<1536;i++)
    step(&t, &md, (i % 3) ? SIG_SQUARE : SIG_QUIET);

  moments_vw(&t, &v, &w);
  printf("(c) quiet + square from reset: q %.3e (truth %.3e) e %.6f (truth %.6f) flags %02x, w %.0f codes^4 = v^2 / %.2f\n",
         tqq(&t), q, te24(&t), e, t.flags, w, v * v / w);
  T_EQ(t.flags & ACAL_TRK_BOW, ACAL_TRK_BOW);
  T_TRUE(fabs(tqq(&t) - q) < 0.1 * q);
  T_TRUE(fabs(te24(&t) - e) < 0.0005);
}

//----------------------------------------------------------------------------------------------------------------------------------
//(d) quiet input: e and q held, d converges

static void test_quiet(void)
{
  acal_track t;
  model md;
  double d, e, q;
  int i;

  reset_model(&md);
  store_channel(&md);
  md.off[0] += 1.2;
  md.gain[0] = 1.02;
  acal_track_reset(&t);
  truth_fit(&md, -1, 1, &d, &e, &q);

  for(i=0;i<1536;i++)
    step(&t, &md, SIG_QUIET);

  printf("(d) quiet input: d %.4f (truth %.4f), e %d q %d (held at 0), flags %02x, spread %.3f codes\n", tq8(&t), d,
         t.e_q24, t.q_q24, t.flags, t.spread16 / 16.0);
  T_EQ(t.e_q24, 0);
  T_EQ(t.q_q24, 0);
  T_EQ(t.flags & (ACAL_TRK_GAIN | ACAL_TRK_BOW), 0);
  T_TRUE(fabs(tq8(&t) - d) < 0.02);

  //Held e survives a later quiet period (learned on the sine first)
  md.off[0] -= 1.2;
  acal_track_reset(&t);

  for(i=0;i<1024;i++)
    step(&t, &md, SIG_SINE);

  e = te24(&t);

  for(i=0;i<2048;i++)
    step(&t, &md, SIG_QUIET);

  printf("(d) e learned on the sine %.6f, after 2048 quiet records %.6f, flags %02x\n", e, te24(&t), t.flags);
  T_TRUE(fabs(te24(&t) - e) < 5e-4);      //e keeps updating while the sine's share keeps v >= 64 codes^2
  T_EQ(t.flags & ACAL_TRK_GAIN, 0);
}

//----------------------------------------------------------------------------------------------------------------------------------
//(e) 40 MHz sine: rejected

static void test_reject(void)
{
  acal_track t, ref;
  model md;
  int i, acc = 0;

  reset_model(&md);
  store_channel(&md);
  acal_track_reset(&t);

  for(i=0;i<300;i++)
    step(&t, &md, SIG_QUIET);

  ref = t;

  for(i=0;i<100;i++)
    acc += step(&t, &md, SIG_SINE40);

  printf("(e) 40 MHz sine: %d of 100 records accepted, last usable pairs %u\n", acc, t.last_pairs);
  T_EQ(acc, 0);
  T_EQ(t.flags & ACAL_TRK_LAST, 0);
  T_EQ(t.records, ref.records);
  T_MEM(t.m, ref.m, sizeof(t.m));
  T_EQ(t.d_q8, ref.d_q8);

  T_EQ(step(&t, &md, SIG_QUIET), 1);
  T_EQ(t.flags & ACAL_TRK_LAST, ACAL_TRK_LAST);
}

//----------------------------------------------------------------------------------------------------------------------------------
//(f) noise floor with constant truth

static void test_noise_floor(void)
{
  acal_track t;
  model md;
  double d, e, q, sd = 0, sd2 = 0, se = 0, se2 = 0, dm, em, dsd, esd;
  int i, n = 2000;

  reset_model(&md);
  store_channel(&md);
  md.off[0] += 1.0;
  md.gain[0] = 1.01;
  acal_track_reset(&t);
  truth_fit(&md, -40, 40, &d, &e, &q);

  for(i=0;i<1536;i++)
    step(&t, &md, SIG_SINE);

  for(i=0;i<n;i++)
  {
    step(&t, &md, SIG_SINE);
    sd += tq8(&t); sd2 += tq8(&t) * tq8(&t);
    se += te24(&t); se2 += te24(&t) * te24(&t);
  }

  dm = sd / n; em = se / n;
  dsd = sqrt(sd2 / n - dm * dm);
  esd = sqrt(se2 / n - em * em);
  printf("(f) noise floor over %d records: d mean %.4f (truth %.4f) std %.5f code, e mean %.6f (truth %.6f) std %.2e\n",
         n, dm, d, dsd, em, e, esd);
  T_TRUE(dsd < 0.005);
  T_TRUE(esd < 1.5e-4);
  T_TRUE(fabs(dm - d) < 0.02);
  T_TRUE(fabs(em - e) < 2e-4);

  //The same channel on a quiet input (e held): d alone
  for(i=0;i<1536;i++)
    step(&t, &md, SIG_QUIET);

  sd = sd2 = 0;

  for(i=0;i<n;i++)
  {
    step(&t, &md, SIG_QUIET);
    sd += tq8(&t); sd2 += tq8(&t) * tq8(&t);
  }

  dm = sd / n;
  dsd = sqrt(sd2 / n - dm * dm);
  printf("(f) quiet input, %d records: d mean %.4f (truth %.4f) std %.5f code, e held %.6f\n", n, dm, d, dsd, te24(&t));
  T_TRUE(dsd < 0.005);
  T_TRUE(fabs(dm - d) < 0.02);
}

//----------------------------------------------------------------------------------------------------------------------------------
//(g) the integer solve against a double-precision least-squares reference

static void solve_ref(const double *m, const double *ty, int nterm, double qheld, double *out)
{
  //normal equations for d + e x (+ q x^2), x and y in codes; with nterm 2: y - qheld x^2 against d + e x
  double a[3][4];
  int r, j, i;
  double f;

  for(r=0;r<nterm;r++)
  {
    for(j=0;j<nterm;j++)
      a[r][j] = m[r + j];

    a[r][3] = ty[r] - ((nterm == 2) ? qheld * m[r + 2] : 0.0);
  }

  for(r=0;r<nterm;r++)
    for(i=r+1;i<nterm;i++)
    {
      f = a[i][r] / a[r][r];

      for(j=r;j<nterm;j++)
        a[i][j] -= f * a[r][j];

      a[i][3] -= f * a[r][3];
    }

  for(r=nterm-1;r>=0;r--)
  {
    for(j=r+1;j<nterm;j++)
      a[r][3] -= a[r][j] * out[j];

    out[r] = a[r][3] / a[r][r];
  }

  if(nterm == 2)
    out[2] = qheld;
}

static void test_solve(void)
{
  acal_track t;
  double dmax = 0, emax = 0, qrel = 0, dq = 0;
  int set, n2 = 0, n3 = 0;

  for(set=0;set<400;set++)
  {
    int     levels = 1 + (int)(rnd64() % 6), np = 500 + (int)(rnd64() % 3000), i, k;
    double  lv[6], cen = (urand() - 0.5) * 100.0, amp = 4.0 + urand() * 60.0;
    double  dt = (urand() - 0.5) * 8.0, et = (urand() - 0.5) * 0.08, qt = (urand() - 0.5) * 2e-3;
    double  mr[5] = { 0 }, tr[3] = { 0 }, ref[3];
    int     sine = (set & 1);

    for(i=0;i<levels;i++)
      lv[i] = cen + (urand() - 0.5) * amp * 2.0;

    acal_track_reset(&t);
    t.e_q24 = (int32_t)((urand() - 0.5) * 1e6);
    t.q_q24 = (int32_t)((qt + (urand() - 0.5) * 4e-4) * 16777216.0);   //held q near the truth (e stays inside +-1/8)

    for(i=0;i<np;i++)
    {
      double xc = sine ? cen + amp * sin(urand() * 2 * PI) : lv[rnd64() % levels] + gauss() * 0.7;
      int32_t x4 = (int32_t)lround(xc * 4.0), y256;
      double xq;

      x4 = (x4 > 492) ? 492 : ((x4 < -496) ? -496 : x4);
      xq = x4 / 4.0;
      y256 = (int32_t)lround((dt + et * xq + qt * xq * xq + gauss() * 1.3) * 256.0);

      for(k=0;k<5;k++)
      {
        int64_t p = 1;
        int j;

        for(j=0;j<k;j++)
          p *= x4;

        t.m[k] += 256 * p;
        mr[k] += 256.0 * pow(xq, k);

        if(k < 3)
        {
          t.t[k] += 256 * p * y256;
          tr[k] += 256.0 * pow(xq, k) * (y256 / 256.0);
        }
      }
    }

    {
      int32_t eh = t.e_q24, qh = t.q_q24;
      double v;

      acal_track_solve(&t);

      v = mr[2] / mr[0] - (mr[1] / mr[0]) * (mr[1] / mr[0]);

      if(t.flags & ACAL_TRK_BOW)
      {
        solve_ref(mr, tr, 3, 0, ref);
        n3++;
        qrel = (fabs(tqq(&t) - ref[2]) > qrel) ? fabs(tqq(&t) - ref[2]) : qrel;
      }
      else if(t.flags & ACAL_TRK_GAIN)
      {
        T_EQ(t.q_q24, qh);
        solve_ref(mr, tr, 2, qh / 16777216.0, ref);
        n2++;
      }
      else
      {
        //e and q held: d = mean of y - q x^2 - e x
        T_EQ(t.q_q24, qh);
        T_EQ(t.e_q24, eh);
        ref[2] = qh / 16777216.0;
        ref[1] = eh / 16777216.0;
        ref[0] = (tr[0] - ref[2] * mr[2] - ref[1] * mr[1]) / mr[0];
        T_TRUE(v < 64.0 + 1e-6);
      }

      dq   = fabs(tq8(&t) - ref[0]);
      dmax = (dq > dmax) ? dq : dmax;
      emax = (fabs(te24(&t) - ref[1]) > emax) ? fabs(te24(&t) - ref[1]) : emax;
    }
  }

  printf("(g) solve vs double over 400 sets (%d with q, %d with q held): max |d err| %.5f code (d_q8 step 1/256: "
         "rounding <= 0.00195), max |e err| %.2e, max |q err| %.2e per code^2\n", n3, n2, dmax, emax, qrel);
  T_TRUE(dmax < 1.0 / 512.0 + 1e-3);
  T_TRUE(emax < 2e-5);
  T_TRUE(n3 > 50 && n2 > 20);
}

//----------------------------------------------------------------------------------------------------------------------------------
//(h) acal_track_apply

static void test_apply(void)
{
  acal_track t;
  acal_corr c0, c;
  model md;
  double worst = 0.0, worst_x = 0.0;
  int v;

  reset_model(&md);
  store_channel(&md);
  acal_track_reset(&t);

  //d = e = 0: byte-identical
  correction(0, &c0);
  c = c0;
  acal_track_apply(&t, &c);
  T_MEM(&c, &c0, sizeof(c));

  //d and e set: A' = A - (d + e x) / 2, B' = B + (d + e x) / 2; the mean of the cores is unchanged
  t.d_q8 = 437;                           //1.707 codes
  t.e_q24 = 503316;                       //0.03
  c = c0;
  acal_track_apply(&t, &c);

  for(v=-120;v<=120;v+=4)
  {
    double a0 = code_mean(&md, &c0, 0, v), b0 = code_mean(&md, &c0, 1, v);
    double a1 = code_mean(&md, &c, 0, v),  b1 = code_mean(&md, &c, 1, v);
    double dl = tq8(&t) + te24(&t) * (a0 - 128.0), db = tq8(&t) + te24(&t) * (b0 - 128.0);
    double err = fabs((a1 - (a0 - dl / 2.0))) + fabs(b1 - (b0 + db / 2.0));
    double mean = fabs((a1 + b1) - (a0 + b0) - te24(&t) * (b0 - a0) / 2.0);

    worst   = (err > worst) ? err : worst;
    worst_x = (mean > worst_x) ? mean : worst_x;
  }

  printf("(h) apply: max per-core error %.2e code, mean of the cores moved by %.2e beyond e x split / 4\n", worst, worst_x);
  T_TRUE(worst < 1e-3);
  T_TRUE(worst_x < 1e-3);
  T_EQ(c.corr16, c0.corr16);
  T_EQ(c.gain16, c0.gain16);
  T_EQ(c.zero_q8, c0.zero_q8);
  T_EQ(c.comp_a, c0.comp_a); T_EQ(c.comp_b, c0.comp_b);
  T_EQ(c.frac_a, c0.frac_a); T_EQ(c.frac_b, c0.frac_b);
  T_EQ(c.shift, c0.shift);
  T_TRUE(c.gain_a < c0.gain_a && c.gain_b > c0.gain_b);

  //Clip limits as acal_correction computes them
  {
    acal_corr k = c;
    int32_t lo_a = (int32_t)((int64_t)c.off_a >> 16), lo_b = (int32_t)((int64_t)c.off_b >> 16);
    int32_t hi_a = (int32_t)(((int64_t)255 * c.gain_a + c.off_a) >> 16);
    int32_t hi_b = (int32_t)(((int64_t)255 * c.gain_b + c.off_b) >> 16);

    acal_clips(&k);
    T_EQ(c.clip_lo, (lo_a > lo_b) ? lo_a : lo_b);
    T_EQ(c.clip_hi, (hi_a < hi_b) ? hi_a : hi_b);
    T_MEM(&k, &c, sizeof(c));
  }

  //Gated class and version 1: untouched
  acal_correction(&chan, pwm, 2, 99, 0, 385, 0, 0, &c0);
  c = c0;
  acal_track_apply(&t, &c);
  T_MEM(&c, &c0, sizeof(c));

  acal_correction(0, pwm, 2, 0, 0, 385, -2, 3, &c0);
  c = c0;
  acal_track_apply(&t, &c);
  T_MEM(&c, &c0, sizeof(c));
}

//----------------------------------------------------------------------------------------------------------------------------------
//(i) rate gate, inversion

static void test_gate(void)
{
  acal_track t, ref, u;
  acal_corr c;
  model md;
  uint8_t inv[NS];
  uint32_t n;

  reset_model(&md);
  store_channel(&md);
  acal_track_reset(&t);

  correction(&t, &c);
  signal(SIG_SINE, inp);
  readout(&md, &c, inp, buf);

  T_EQ(acal_track_record(&t, buf, NS, 0, 0, 0, 0xFFFFFFF0u), 1);  //first record: no gate
  ref = t;
  T_EQ(acal_track_record(&t, buf, NS, 0, 0, 0, 0xFFFFFFF0u + 49), 0);
  T_MEM(&t, &ref, sizeof(t));
  T_EQ(acal_track_record(&t, buf, NS, 0, 0, 0, 0xFFFFFFF0u + 50), 1);  //wraps through 0
  T_EQ(t.records, 2);
  T_EQ(t.last_ms, 0xFFFFFFF0u + 50);

  //An inverted buffer gives the same state
  for(n=0;n<NS;n++)
    inv[n] = (uint8_t)(255 - buf[n]);

  u = ref;
  T_EQ(acal_track_record(&u, inv, NS, 1, 0, 0, 0xFFFFFFF0u + 50), 1);
  T_MEM(&u, &t, sizeof(t));

  //Too short a record: not usable, only last_pairs and LAST change
  ref = t;
  T_EQ(acal_track_record(&t, buf, 200, 0, 0, 0, 0xFFFFFFF0u + 200), 0);
  T_EQ(t.flags & ACAL_TRK_LAST, 0);
  T_EQ(t.records, ref.records);
  T_MEM(t.m, ref.m, sizeof(t.m));
}

//----------------------------------------------------------------------------------------------------------------------------------
//(j) class probe: full-class common mode against the gated class (acal_cm_*, design section 8)

//Raw means (1/16 code) the probe would measure for an input of v codes about mid-scale: per class the stored zero plus a
//drift, divided by the class gain G (raw - z = v / G, so the corrected codes are T + v in both classes)
static void cm_means(const acal_channel *ch, uint32_t cls, double v, double drift, int32_t mean16[2])
{
  int k;

  for(k=0;k<2;k++)
    mean16[k] = (int32_t)lround(ch->zero[cls][k][2] + 16.0 * (drift + v * 32768.0 / ch->gain[cls][k]));
}

static void cm_meas(const acal_channel *ch, double v, double drift_full, double drift_g2, acal_cm_meas *m)
{
  memset(m, 0, sizeof(*m));
  cm_means(ch, ACAL_GATED, v, 0.0, m->mean16[0]);
  cm_means(ch, ACAL_FULL, v, drift_full, m->mean16[1]);
  cm_means(ch, ACAL_GATED, v, drift_g2, m->mean16[2]);
  m->rms16[0][0] = m->rms16[0][1] = m->rms16[2][0] = m->rms16[2][1] = 16;
  m->rms16[1][0] = m->rms16[1][1] = 20;
}

static void test_class_probe(void)
{
  acal_cm cm, ref;
  acal_cm_meas m;
  acal_corr kg, kf, c0, c, t1;
  acal_track trk;
  model md;
  int32_t c_q8, mf;
  double v;

  //Gains as on the bench: full A/B 1.033 (core A 1.6 % high), gated 1.004, full class 1.1 % low
  reset_model(&md);
  md.gain[0] = 1.016;
  md.gain[1] = 0.984;
  store_channel(&md);
  chan.gain[ACAL_GATED][0] = 32703; chan.gain[ACAL_GATED][1] = 32834;
  chan.gain[ACAL_FULL][0]  = 32604; chan.gain[ACAL_FULL][1]  = 33681;

  acal_correction(&chan, pwm, 2, 199, 0, 385, 0, 0, &kg);
  acal_correction(&chan, pwm, 2, 0, 0, 385, 0, 0, &kf);
  T_EQ(kg.cls, ACAL_GATED); T_EQ(kf.cls, ACAL_FULL);

  //No drift: c = 0 at any DC level (the corrected gains of both classes agree); +0.8 code full-class drift: c = 0.8 code
  //at 0 V, +40 and -30 codes (the probe is input-independent for a DC input)
  acal_cm_reset(&cm);

  for(v=-30.0;v<=40.0;v+=35.0)
  {
    cm_meas(&chan, v, 0.0, 0.0, &m);
    ref = cm;
    T_EQ(acal_cm_probe(&ref, &m, &kg, &kf, 1000, &c_q8), 1);
    T_TRUE(c_q8 >= -8 && c_q8 <= 8);      //the means are whole sixteenths: up to 1/32 code per core

    cm_meas(&chan, v, 0.8, 0.0, &m);
    ref = cm;
    T_EQ(acal_cm_probe(&ref, &m, &kg, &kf, 1000, &c_q8), 1);
    printf("(j) DC input %+.0f codes, full class +0.8 code: c %.3f code\n", v, c_q8 / 256.0);
    T_TRUE(c_q8 >= 205 - 8 && c_q8 <= 205 + 8);
  }

  //First valid probe takes c, then EMA 1/4
  acal_cm_reset(&cm);
  cm_meas(&chan, 0.0, 0.8, 0.0, &m);
  T_EQ(acal_cm_probe(&cm, &m, &kg, &kf, 5000, &c_q8), 1);
  T_EQ(cm.cm_q8, c_q8); T_EQ(cm.probes, 1); T_EQ(cm.last_ms, 5000); T_EQ(cm.flags, ACAL_CM_DATA | ACAL_CM_LAST);
  mf = cm.cm_q8;
  cm_meas(&chan, 0.0, 0.0, 0.0, &m);
  T_EQ(acal_cm_probe(&cm, &m, &kg, &kf, 35000, &c_q8), 1);
  T_TRUE(c_q8 >= -8 && c_q8 <= 8);
  T_TRUE(cm.cm_q8 - (mf + (c_q8 - mf) / 4) >= -1 && cm.cm_q8 - (mf + (c_q8 - mf) / 4) <= 1);
  T_EQ(cm.probes, 2); T_EQ(cm.last_ms, 35000);

  //Validity: noisy part, clipping, gated parts 0.31 / 0.26 code apart, |c| > 8 codes, gated class not gated: rejected,
  //nothing but LAST and last_c changes
  cm_meas(&chan, 0.0, 0.8, 0.0, &m);
  m.rms16[1][1] = ACAL_CM_MAX_RMS16 + 1;
  ref = cm;
  T_EQ(acal_cm_probe(&cm, &m, &kg, &kf, 40000, &c_q8), 0);
  T_EQ(c_q8, 0); T_EQ(cm.cm_q8, ref.cm_q8); T_EQ(cm.probes, ref.probes); T_EQ(cm.flags, ACAL_CM_DATA);
  m.rms16[1][1] = ACAL_CM_MAX_RMS16;
  T_EQ(acal_cm_probe(&cm, &m, &kg, &kf, 40000, &c_q8), 1);
  cm = ref;

  cm_meas(&chan, 0.0, 0.8, 0.0, &m);
  m.clipped = 1;
  T_EQ(acal_cm_probe(&cm, &m, &kg, &kf, 40000, 0), 0);
  T_EQ(cm.cm_q8, ref.cm_q8);

  cm_meas(&chan, 0.0, 0.8, 0.31, &m);
  T_EQ(acal_cm_probe(&cm, &m, &kg, &kf, 40000, &c_q8), 0);
  T_TRUE(c_q8 != 0);                      //compared, then refused for the gated disagreement
  T_EQ(cm.cm_q8, ref.cm_q8); T_EQ(cm.flags & ACAL_CM_LAST, 0);
  cm_meas(&chan, 0.0, 0.8, 0.26, &m);    //0.26 quantised to sixteenths: 0.25 (0.31: 0.3125)
  T_EQ(acal_cm_probe(&cm, &m, &kg, &kf, 40000, 0), 1);
  cm = ref;

  cm_meas(&chan, 0.0, 8.2, 0.0, &m);
  T_EQ(acal_cm_probe(&cm, &m, &kg, &kf, 40000, 0), 0);
  cm_meas(&chan, 0.0, 7.8, 0.0, &m);
  T_EQ(acal_cm_probe(&cm, &m, &kg, &kf, 40000, 0), 1);
  cm = ref;

  cm_meas(&chan, 0.0, 0.8, 0.0, &m);
  T_EQ(acal_cm_probe(&cm, &m, &kf, &kf, 40000, 0), 0);
  acal_correction(0, pwm, 2, 199, 0, 385, 0, 0, &c);
  T_EQ(acal_cm_probe(&cm, &m, &c, &kf, 40000, 0), 0);
  T_EQ(cm.cm_q8, ref.cm_q8);

  //Apply: full class 8-bit, both cores off by cm, corrected full means equal the gated ones again; corr16 by cm / 16;
  //gains, gain16, zero_q8, comp / frac unchanged
  acal_cm_reset(&cm);
  cm_meas(&chan, 0.0, 0.8, 0.0, &m);
  T_EQ(acal_cm_probe(&cm, &m, &kg, &kf, 5000, &c_q8), 1);
  c0 = kf;
  c  = kf;
  acal_cm_apply(&cm, &c);
  T_EQ(c.off_a, c0.off_a - cm.cm_q8 * 256); T_EQ(c.off_b, c0.off_b - cm.cm_q8 * 256);
  T_EQ(c.corr16, c0.corr16 - (cm.cm_q8 + 8) / 16);
  T_EQ(c.gain_a, c0.gain_a); T_EQ(c.gain_b, c0.gain_b); T_EQ(c.gain16, c0.gain16);
  T_EQ(c.zero_q8, c0.zero_q8); T_EQ(c.comp_a, c0.comp_a); T_EQ(c.frac_a, c0.frac_a);
  t1 = c;
  acal_clips(&t1);
  T_MEM(&t1, &c, sizeof(c));
  {
    int32_t g = (acal_cm_mean_q8(&kg, m.mean16[0]) + acal_cm_mean_q8(&kg, m.mean16[2])) / 2;
    int32_t f = acal_cm_mean_q8(&c, m.mean16[1]);

    printf("(j) after apply: full %.3f gated %.3f code\n", f / 256.0, g / 256.0);
    T_TRUE(f - g >= -1 && f - g <= 1);
  }

  //Gated correction: corr16 moves (the 16-bit path is the full class), the 8-bit loop does not
  c = kg;
  acal_cm_apply(&cm, &c);
  T_EQ(c.corr16, kg.corr16 - (cm.cm_q8 + 8) / 16);
  T_EQ(c.off_a, kg.off_a); T_EQ(c.off_b, kg.off_b); T_EQ(c.clip_lo, kg.clip_lo);

  //Version 1: untouched; cm 0: untouched
  acal_correction(0, pwm, 2, 0, 0, 385, -2, 3, &c0);
  c = c0;
  acal_cm_apply(&cm, &c);
  T_MEM(&c, &c0, sizeof(c));
  ref = cm;
  ref.cm_q8 = 0;
  c = kf;
  acal_cm_apply(&ref, &c);
  T_MEM(&c, &kf, sizeof(c));

  //acal_v4_apply = cm first, then the split; the split keeps the mean of the cores (and corr16)
  acal_track_reset(&trk);
  trk.d_q8 = 300;
  trk.e_q24 = 200000;
  c = kf;
  acal_v4_apply(&trk, &cm, &c);
  t1 = kf;
  acal_cm_apply(&cm, &t1);
  acal_track_apply(&trk, &t1);
  T_MEM(&c, &t1, sizeof(c));
  T_EQ(c.corr16, kf.corr16 - (cm.cm_q8 + 8) / 16);
}

int main(void)
{
  test_class_probe();
  test_gate();
  test_apply();
  test_solve();
  test_reject();
  test_quiet();
  test_noise_floor();
  test_offset_step();
  test_gain_drift();
  test_bow();

  return t_report("test_acal_track");
}
