//----------------------------------------------------------------------------------------------------------------------------------
// bode_dsp.c - planner, DFT, measurement and markers of the on-device Bode analyser (see bode_dsp.h). Host-tested:
// host_tests/test_bode.c (plan vectors, synthetic records against the host tool's numbers).
//----------------------------------------------------------------------------------------------------------------------------------

#include "bode_dsp.h"
#include "fx_fft.h"

#define Q32               (1ll << 32)
#define MDB_PER_Q16_X32   394566036ll     //20 log10(2) x 1000 / 65536 x 2^32: mdB per Q16 log2 unit of an amplitude
#define LOG2_10_PER_MDB   713379ll        //log2(10) / 20000 x 2^32: Q32 log2 per mdB of an amplitude ratio

//Quality thresholds: the host tool's constants (mcp/src/fnirsi1013d/bode.py, the validated reference: hardware-status
//2026-10-19 Bode bench), in mdB of a code level or of a ratio, 1/100 code for an excursion. fw 0.2.87 checks every one of
//them against bode.py on identical records (host_tests/test_bode.py test_flags).
#define CH1_LOW_MDB       26021           //CH1_LOW_CODES 20: CH1 fundamental below 20 codes peak -> ch1_low
#define CH1_MIN_MDB       (-6021)         //CH1_MIN_CODES 0.5: below it (or flat1) CH1 is dead -> incoherent, no H; also
                                          //the floor of each CH1 half in the settle check (measure_pair's `floor`)
#define FLAT_STD_MDB      0               //FLAT_STD_CODES 1.0: a record with a standard deviation under 1 code is flat
#define FLAT_C100         5               //FLAT_CODES 0.05: CH2's largest distance from its mean under it: no tone
#define CH2_SMALL_MDB     9542            //CH2_SMALL_CODES 3: CH2 fundamental below 3 codes -> ch2_small
#define LOW_SNR_MDB       20000           //LOW_SNR_DB 20: CH2 fundamental less than 20 dB above its noise -> low_snr
#define LEAK_LIMIT_MDB    (-40000)        //LEAK_LIMIT_DB -40: CH1 one bin beside the fundamental above it -> incoherent
#define SETTLE_MDB        100             //SETTLE_DB 0.1 and SETTLE_DEG 1: the halves' limits (bode_unsettled)
#define SETTLE_MDEG       1000

//----------------------------------------------------------------------------------------------------------------------------------
// 64-bit helpers

//Upper 64 bits of a x b
static uint64 mulhi64(uint64 a, uint64 b)
{
  uint64 a0 = (uint32)a, a1 = a >> 32, b0 = (uint32)b, b1 = b >> 32;
  uint64 p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
  uint64 mid = (p00 >> 32) + (uint32)p01 + (uint32)p10;

  return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

static int msb64(uint64 v)
{
  int n = 63;

  if(!v)
    return -1;

  while(!(v >> n))
    n--;

  return n;
}

//a / b rounded half to even (Python round() of an exact quotient), b > 0
static uint64 div_round_even(uint64 a, uint64 b)
{
  uint64 q = a / b, r = a % b;

  if((2 * r > b) || ((2 * r == b) && (q & 1)))
    q++;

  return q;
}

//Signed division rounded half away from zero
static int64 sdiv_round(int64 a, int64 b)
{
  if(b < 0)
  {
    a = -a;
    b = -b;
  }

  return (a >= 0) ? (a + b / 2) / b : -((-a + b / 2) / b);
}

static uint64 gcd64(uint64 a, uint64 b)
{
  while(b)
  {
    uint64 t = a % b;
    a = b;
    b = t;
  }

  return a;
}

//log2(v) in Q32: integer part from the leading one, 32 fraction bits by repeated squaring of a Q62 mantissa
int64 bode_log2_q32(uint64 v)
{
  int    e = msb64(v), b;
  uint64 m;
  int64  r;

  if(e < 0)
    return -(1ll << 62);

  m = (e <= 62) ? (v << (62 - e)) : (v >> 1);     //[1, 2) in Q62
  r = (int64)e << 32;

  for(b = 31; b >= 0; b--)
  {
    m = mulhi64(m, m) << 2;                       //m^2 in [1, 4), Q62

    if(m >= (2ull << 62))
    {
      m >>= 1;
      r |= 1ll << b;
    }
  }

  return r;
}

//2^(2^-b) in Q62, b = 1..32
static const uint64 exp2_tab[32] =
{
  0x5A827999FCEF3242ull, 0x4C1BF828C6DC54B8ull, 0x45CAE0F1F545EB73ull, 0x42D561B3E6243D8Aull,
  0x4166C34C5615D0ECull, 0x40B268F9DE0183BAull, 0x4058F6A7ECCCD5B6ull, 0x402C6BE96AF2FB58ull,
  0x4016321B687027A8ull, 0x400B18178BA33B14ull, 0x40058BCE410147E8ull, 0x4002C5D7BFF71DAFull,
  0x400162E807EE7E5Bull, 0x4000B1730DF6A524ull, 0x400058B9497B8152ull, 0x40002C5C955DD701ull,
  0x4000162E46D6F26Cull, 0x40000B1722757B1Bull, 0x4000058B90FD3E0Cull, 0x400002C5C86F3F26ull,
  0x40000162E433C79Bull, 0x400000B17218EDD0ull, 0x40000058B90C3968ull, 0x4000002C5C860D54ull,
  0x400000162E4302D2ull, 0x4000000B17218073ull, 0x400000058B90BFFCull, 0x40000002C5C85FEFull,
  0x4000000162E42FF3ull, 0x40000000B17217F9ull, 0x4000000058B90BFCull, 0x400000002C5C85FEull,
};

//round(2^y), y in Q32, 0 <= y < 62 (y < 0 gives 0 or 1 as rounding says, larger y saturates)
uint64 bode_exp2_round(int64 y)
{
  uint64 m = 1ull << 62;
  uint32 f;
  int    i, b;

  //2^y in (0, 1): 1 when it is at least one half
  if(y < 0)
    return (y >= -(1ll << 32)) ? 1 : 0;

  if(y >= (62ll << 32))
    return ~0ull;

  i = (int)(y >> 32);
  f = (uint32)y;

  for(b = 0; b < 32; b++)
    if(f & (0x80000000u >> b))
      m = mulhi64(m, exp2_tab[b]) << 2;

  //value = m x 2^(i - 62)
  return (m + (1ull << (61 - i))) >> (62 - i);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Planner

void bode_plan_p0(uint32 p0, bode_plan *pp)
{
  uint32 n, per, j, jp, jb;
  uint64 g, num, err, tgt;

  if(p0 < BODE_P_MIN)
    p0 = BODE_P_MIN;

  if(p0 <= BODE_LEGACY_P_MAX)
  {
    n   = 1;
    per = p0;
  }
  else
  {
    n = 16;
    while(p0 > (uint64)BODE_MAX_SPP * n)
      n *= 2;

    //Below 24 kHz the samples per period must be an integer S (else p keeps P's odd part and no record fits)
    per = (n == 16) ? p0 : (uint32)(div_round_even(p0, n) * n);

    //At N = 16 a block is p = P / gcd(P, 16) samples: an odd P above 4095 would leave one block per record (no noise
    //estimate, no halves; P = 8191 no record at all), so there P is even (two blocks of at most 4095 samples fit)
    if((n == 16) && (per > BODE_MAX_RECORD / 2))
      per = (uint32)(div_round_even(p0, 2) * 2);
  }

  g = gcd64(per, n);
  pp->period = per;
  pp->n      = n;
  pp->p      = (uint32)(per / g);
  pp->q      = (uint32)(n / g);

  jp = (BODE_MIN_RECORD + pp->p - 1) / pp->p;
  jb = (BODE_MIN_PERIODS + pp->q - 1) / pp->q;
  j  = (jp > jb) ? jp : jb;

  //An even number of blocks: the halves hold whole periods (settle check)
  if((j & 1) && ((j + 1) * pp->p <= BODE_MAX_RECORD))
    j++;

  //Cannot exceed in range (p <= 8190, q <= 16 at N = 16; the host raises): clamp
  while((j > 1) && (j * pp->p > BODE_MAX_RECORD))
    j--;

  pp->blocks = (uint16)j;
  pp->record = (uint16)(j * pp->p);
  pp->f_mhz  = div_round_even(200000000000ull, per);
  pp->step   = (((uint64)n << 34) + per / 2) / per;

  //Coherence error in periods: |record x step / 2^34 - blocks x q| > 1e-3 -> Hann (the host's window rule; the integer
  //period makes the record exactly whole, the step's rounding leaves it under 2.5e-7 turn off)
  num = (uint64)pp->record * pp->step;
  tgt = ((uint64)j * pp->q) << 34;
  err = (num > tgt) ? num - tgt : tgt - num;
  pp->hann = (uint8)((err * 1000) > (1ull << 34));
}

int bode_plan_point(uint64 f_mhz, bode_plan *pp)
{
  if((f_mhz < BODE_FMIN_MHZ) || (f_mhz > BODE_FMAX_MHZ))
    return -1;

  bode_plan_p0((uint32)div_round_even(200000000000ull, f_mhz), pp);
  return 0;
}

int bode_plan_sweep(uint64 start_mhz, uint64 stop_mhz, int points, bode_plan *out, int max)
{
  uint64 lo = (start_mhz < stop_mhz) ? start_mhz : stop_mhz;
  uint64 hi = (start_mhz < stop_mhz) ? stop_mhz : start_mhz;
  int64  l_lo, l_hi, l_gen;
  int    i, n = 0;

  if((points < 1) || (points > BODE_MAX_POINTS) || (lo < BODE_FMIN_MHZ) || (hi > BODE_FMAX_MHZ) || (max < 1))
    return -1;

  l_lo  = bode_log2_q32(lo);
  l_hi  = bode_log2_q32(hi);
  l_gen = bode_log2_q32(200000000000ull);

  for(i = 0; i < points; i++)
  {
    bode_plan pp;
    uint32    p0;

    if(i == 0)
      p0 = (uint32)div_round_even(200000000000ull, lo);
    else if(i == points - 1)
      p0 = (uint32)div_round_even(200000000000ull, hi);
    else
    {
      int64 lf = l_lo + ((l_hi - l_lo) * i) / (points - 1);

      p0 = (uint32)bode_exp2_round(l_gen - lf);
    }

    bode_plan_p0(p0, &pp);

    if(n && (out[n - 1].period == pp.period))
      continue;

    if(n >= max)
      break;

    out[n++] = pp;
  }

  return n;
}

uint32 bode_rate_hz(const bode_plan *pp)
{
  return 200000000u / pp->n;
}

uint32 bode_divider(const bode_plan *pp)
{
  return (pp->n > 1) ? pp->n - 1 : 0;
}

uint32 bode_total(const bode_plan *pp)
{
  return (uint32)pp->record + (pp->record & 1);
}

uint32 bode_record_us(const bode_plan *pp, uint32 total)
{
  return (uint32)(((uint64)total * pp->n + 199) / 200);
}

uint32 bode_settle_ms(const bode_plan *pp, uint32 periods_x10)
{
  //periods / f = periods x P / 200e6 s: ms = periods_x10 x P / 2e6, rounded up
  uint64 ms = ((uint64)periods_x10 * pp->period + 1999999) / 2000000;

  return (ms < 1) ? 1 : (uint32)ms;
}

//----------------------------------------------------------------------------------------------------------------------------------
// DFT

#define TAB_LOG2          14                      //fx_cos_q30 grid: 16384 points per turn
#define TAB_QUARTER       (1u << (TAB_LOG2 - 2))
#define PH_SHIFT          (34 - TAB_LOG2)         //2^-34 turn units to table steps

static inline uint32 ph_index(uint64 ph)
{
  return (uint32)((ph + (1ull << (PH_SHIFT - 1))) >> PH_SHIFT);
}

//k-th smallest of a[0..n-1] (reorders a), Hoare's selection
static int32 select_k(int32 *a, uint32 n, uint32 k)
{
  uint32 lo = 0, hi = n - 1;

  while(lo < hi)
  {
    int32  piv = a[lo + (hi - lo) / 2];
    uint32 i = lo, j = hi;

    while(i <= j)
    {
      while(a[i] < piv) i++;
      while(a[j] > piv) j--;

      if(i <= j)
      {
        int32 t = a[i];
        a[i] = a[j];
        a[j] = t;
        i++;
        if(j == 0)
          break;
        j--;
      }
    }

    if(k <= j)
      hi = j;
    else if(k >= i)
      lo = i;
    else
      break;
  }

  return a[k];
}

//Median of |r| over the period differences r = x[n] - 2 x[n + p] + x[n + 2 p] (rk 2; with two blocks only
//x[n] - x[n + p], rk 1), in Q8 with the grouped-median interpolation inside the median's value (grid: the step of the
//samples, 256 for 8-bit codes x 256, else 1). The periodic signal cancels exactly, a linear drift (the 32-bit
//increment's rounding at low f) too, and a transient or spur that touches part of the record does not move the median.
//(The host takes the median over FFT bins instead: a transient that spans most of the record raises this estimate and
//leaves the host's, see bode_sweep.c for how the engine treats it.)
static uint32 noise_mad(const uint16 *x, uint32 L, uint32 p, uint32 blocks, uint32 grid, int32 *work, uint8 *rk)
{
  uint32 n, cnt, below = 0, equal = 0, half;
  int32  v;

  *rk = (uint8)((blocks >= 3) ? 2 : 1);
  cnt = L - *rk * p;

  if((blocks < 2) || !cnt)
    return 0;

  for(n = 0; n < cnt; n++)
  {
    int32 r = (*rk == 2) ? (int32)x[n] - 2 * (int32)x[n + p] + (int32)x[n + 2 * p] : (int32)x[n] - (int32)x[n + p];

    work[n] = (r < 0) ? -r : r;
  }

  half = cnt / 2;
  v = select_k(work, cnt, half);

  for(n = 0; n < cnt; n++)
  {
    if(work[n] < v)
      below++;
    else if(work[n] == v)
      equal++;
  }

  //Values sit on the grid: spread the ones equal to the median over [v - grid / 2, v + grid / 2)
  if(!equal)
    return (uint32)v << 8;

  {
    int64 m = ((int64)v << 8) - ((int64)grid << 7) + ((((int64)cnt * 128) - ((int64)below << 8)) * grid / equal);

    return (m < 0) ? 0 : (uint32)m;
  }
}

static void dft_core(const bode_plan *pp, const uint16 *x1, const uint16 *x2, int32 *work, bode_dft *d)
{
  uint32 L = pp->record, n, half = L / 2, p = pp->p;
  uint64 step = pp->step, ph = 0;
  uint64 lstep = ((1ull << 34) * (pp->hann ? 2 : 1) + L / 2) / L, phl = 0;
  uint64 wstep = ((1ull << 34) + L / 2) / L, phw = wstep / 2;
  int    c, nch = x2 ? 2 : 1;
  int64  s0 = 0, s1 = 0;
  int32  x0[2];

  //First pass: the rounded means (the offset taken off every sample)
  for(n = 0; n < L; n++)
  {
    s0 += x1[n];
    if(x2)
      s1 += x2[n];
  }

  x0[0] = (int32)((s0 + L / 2) / L);
  x0[1] = (int32)((s1 + L / 2) / L);

  for(c = 0; c < 2; c++)
  {
    bode_dft_ch *ch = &d->ch[c];

    ch->si = ch->sq = ch->sx = 0;
    ch->mad_q8 = 0;
    ch->rk = 0;
    ch->hi[0] = ch->hi[1] = ch->hq[0] = ch->hq[1] = ch->hx[0] = ch->hx[1] = 0;
    ch->li[0] = ch->li[1] = ch->lq[0] = ch->lq[1] = 0;
    ch->su = 0;
    ch->sxx = 0;
    ch->x0 = x0[c];
    ch->xmin = 0xFFFF;
    ch->xmax = 0;
  }

  d->wsum = d->wc = d->ws = 0;
  d->hwsum[0] = d->hwsum[1] = d->hwc[0] = d->hwc[1] = d->hws[0] = d->hws[1] = 0;
  d->lwc[0] = d->lwc[1] = d->lws[0] = d->lws[1] = 0;
  d->len = L;
  d->halves = (uint8)(((pp->blocks & 1) == 0) && (L >= 2 * p));

  for(c = 0; c < nch; c++)
  {
    const uint16 *x = c ? x2 : x1;
    bode_dft_ch  *ch = &d->ch[c];
    int64 si = 0, sq = 0, sx = 0, li0 = 0, lq0 = 0, li1 = 0, lq1 = 0, su = 0;
    uint64 sxx = 0;
    int64 hi0 = 0, hq0 = 0, hx0 = 0;
    int64 wsum = 0, wc = 0, ws = 0, hwsum0 = 0, hwc0 = 0, hws0 = 0, lwc0 = 0, lws0 = 0, lwc1 = 0, lws1 = 0;
    uint16 mn = 0xFFFF, mx = 0;
    uint32 orx = 0;

    ph = 0;
    phl = 0;
    phw = wstep / 2;

    for(n = 0; n < L; n++)
    {
      uint32 idx = ph_index(ph);
      int32  co = fx_cos_q30(idx, TAB_LOG2), si_ = fx_cos_q30(idx - TAB_QUARTER, TAB_LOG2);
      int32  xr = (int32)x[n] - ch->x0, xw = xr, w = 32768;

      if(x[n] < mn) mn = x[n];
      if(x[n] > mx) mx = x[n];
      orx |= x[n];

      if(pp->hann)
      {
        w  = (int32)(((1 << 30) - fx_cos_q30(ph_index(phw), TAB_LOG2)) >> 16);
        xw = (int32)(((int64)xr * w) >> 15);
        phw += wstep;
      }

      si += (int64)xw * co;
      sq -= (int64)xw * si_;
      sx += xw;
      su += xr;
      sxx += (uint64)((int64)xr * xr);

      if(c == 0)
      {
        wsum += w;
        wc   += (int64)w * co;
        ws   += (int64)w * si_;
      }

      if(n == half - 1)
      {
        hi0 = si;  hq0 = sq;  hx0 = sx;
        hwsum0 = wsum;  hwc0 = wc;  hws0 = ws;
      }

      //CH1: the tones one bin (Hann: two bins, the kernel's first zero) below and above the fundamental
      if(c == 0)
      {
        uint32 i0 = ph_index(ph - phl), i1 = ph_index(ph + phl);
        int32  c0 = fx_cos_q30(i0, TAB_LOG2), s0_ = fx_cos_q30(i0 - TAB_QUARTER, TAB_LOG2);
        int32  c1 = fx_cos_q30(i1, TAB_LOG2), s1_ = fx_cos_q30(i1 - TAB_QUARTER, TAB_LOG2);

        li0 += (int64)xw * c0;  lq0 -= (int64)xw * s0_;
        li1 += (int64)xw * c1;  lq1 -= (int64)xw * s1_;
        lwc0 += (int64)w * c0;  lws0 += (int64)w * s0_;
        lwc1 += (int64)w * c1;  lws1 += (int64)w * s1_;
        phl += lstep;
      }

      ph += step;
    }

    //Noise: the median absolute period difference (bode_dsp.h)
    ch->mad_q8 = noise_mad(x, L, p, pp->blocks, (orx & 0xFF) ? 1 : 256, work, &ch->rk);

    ch->si = si;
    ch->sq = sq;
    ch->sx = sx;
    ch->su = su;
    ch->sxx = sxx;
    ch->hi[0] = hi0;       ch->hq[0] = hq0;       ch->hx[0] = hx0;
    ch->hi[1] = si - hi0;  ch->hq[1] = sq - hq0;  ch->hx[1] = sx - hx0;
    ch->xmin = mn;
    ch->xmax = mx;

    if(c == 0)
    {
      ch->li[0] = li0;  ch->lq[0] = lq0;  ch->li[1] = li1;  ch->lq[1] = lq1;
      d->wsum = wsum;  d->wc = wc;  d->ws = ws;
      d->hwsum[0] = hwsum0;         d->hwc[0] = hwc0;       d->hws[0] = hws0;
      d->hwsum[1] = wsum - hwsum0;  d->hwc[1] = wc - hwc0;  d->hws[1] = ws - hws0;
      d->lwc[0] = lwc0;  d->lws[0] = lws0;  d->lwc[1] = lwc1;  d->lws[1] = lws1;
    }
  }
}

void bode_dft_run(const bode_plan *pp, const uint16 *x1, const uint16 *x2, int32 *work, bode_dft *d)
{
  dft_core(pp, x1, x2, work, d);
}

//The mean-corrected sums: X x wsum x 2^14 = (si - sx wc / wsum) + j (sq + sx ws / wsum)
static void dft_iq(int64 si, int64 sq, int64 sx, int64 wsum, int64 wc, int64 ws, int64 *i, int64 *q)
{
  if(wsum <= 0)
  {
    *i = *q = 0;
    return;
  }

  *i = si - sx * sdiv_round(wc, wsum);
  *q = sq + sx * sdiv_round(ws, wsum);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Levels and angles

int32 bode_mdb_u64(uint64 v)
{
  if(!v)
    return BODE_MDB_FLOOR;

  return (int32)(((int64)fx_log2_q16(v) * MDB_PER_Q16_X32 + (1ll << 31)) >> 32);
}

int32 bode_mdb_iq(int64 i, int64 q)
{
  uint64 ai = (i < 0) ? (uint64)-i : (uint64)i, aq = (q < 0) ? (uint64)-q : (uint64)q, pw;
  int    sh = 0;
  int64  l;

  while((ai | aq) >= (1ull << 31))
  {
    ai >>= 1;
    aq >>= 1;
    sh++;
  }

  pw = ai * ai + aq * aq;
  if(!pw)
    return BODE_MDB_FLOOR;

  //10 log10(pw x 2^(2 sh))
  l = (int64)fx_log2_q16(pw) + ((int64)(2 * sh) << 16);
  return (int32)((l * (MDB_PER_Q16_X32 / 2) + (1ll << 31)) >> 32);
}

int32 bode_wrap_mdeg(int32 a)
{
  while(a > 180000)
    a -= 360000;

  while(a <= -180000)
    a += 360000;

  return a;
}

//atan(2^-i) in 2^32 per turn
static const uint32 atan_tab[31] =
{
  536870912u, 316933406u, 167458907u, 85004756u, 42667331u, 21354465u, 10679838u, 5340245u, 2670163u, 1335087u,
  667544u, 333772u, 166886u, 83443u, 41722u, 20861u, 10430u, 5215u, 2608u, 1304u, 652u, 326u, 163u, 81u, 41u, 20u, 10u,
  5u, 3u, 1u, 1u
};

int32 bode_atan2_mdeg(int64 y, int64 x)
{
  uint32 ang = 0;
  int    i;

  if(!x && !y)
    return 0;

  while((x >= (1ll << 29)) || (x <= -(1ll << 29)) || (y >= (1ll << 29)) || (y <= -(1ll << 29)))
  {
    x /= 2;
    y /= 2;
  }

  //Scale small vectors up for resolution
  while((x < (1ll << 27)) && (x > -(1ll << 27)) && (y < (1ll << 27)) && (y > -(1ll << 27)))
  {
    x *= 2;
    y *= 2;
  }

  if(x < 0)
  {
    x = -x;
    y = -y;
    ang = 0x80000000u;
  }

  for(i = 0; i < 31; i++)
  {
    int64 xs = x >> i, ys = y >> i;

    if(y > 0)
    {
      x += ys;
      y -= xs;
      ang += atan_tab[i];
    }
    else
    {
      x -= ys;
      y += xs;
      ang -= atan_tab[i];
    }
  }

  return bode_wrap_mdeg((int32)(((int64)(int32)ang * 360000 + (1ll << 31)) >> 32));
}

//round(10^(mdb / 20000) x 2^(scale_q32 / 2^32))
static uint64 pow10_scaled(int32 mdb, int64 scale_q32)
{
  return bode_exp2_round(scale_q32 + (int64)mdb * LOG2_10_PER_MDB);
}

static const int32 probe_mdb[7] = { -6021, 0, 20000, 26021, 33979, 40000, 60000 };

int32 bode_volt_mdb(uint32 uv_q8, uint8 probe_index)
{
  //uV x 256 -> V: - 20 log10(256 x 1e6) = -48165.4 - 120000
  return bode_mdb_u64(uv_q8) - 168165 + probe_mdb[(probe_index < 7) ? probe_index : 1];
}

static int32 norm_mdb(int64 wsum);

int32 bode_dft_level(const bode_plan *pp, const uint16 *x, int32 *mdb_raw)
{
  static int32 dummy_work[1];          //one block: no noise estimate, no scratch use
  bode_dft d;
  int64 i, q;

  //The per-position sums are not needed: one position
  bode_plan one = *pp;

  one.p = 1;
  one.blocks = 1;
  dft_core(&one, x, 0, dummy_work, &d);

  dft_iq(d.ch[0].si, d.ch[0].sq, d.ch[0].sx, d.wsum, d.wc, d.ws, &i, &q);
  *mdb_raw = bode_mdb_iq(i, q) + norm_mdb(d.wsum);
  return *mdb_raw;
}

//The level term of X = (I + jQ) / (wsum x 2^14): -20 log10(wsum) - 14 x 20 log10(2)
static int32 norm_mdb(int64 wsum)
{
  return -bode_mdb_u64((uint64)wsum) - 84288;   //14 x 6020.6
}

static int32 code_c100(int64 raw_x_l, uint32 len, uint32 cpr_q24, int32 zero_q8)
{
  //(raw_x_l / len) x cpr / 2^24 x 100 + zero_q8 x 100 / 256
  int64 v = (raw_x_l * (int64)cpr_q24 * 100) / (int64)len;

  return (int32)((v >> 24) + ((int64)zero_q8 * 100) / 256);
}

void bode_measure(const bode_plan *pp, const bode_dft *d, const bode_scale s[2], const int32 zero_code_c100[2],
                  const uint32 uv_q8[2], bode_meas *m)
{
  int    c;
  int64  ih[2][2], qh[2][2];
  int32  cpr_mdb[2];

  for(c = 0; c < 2; c++)
  {
    const bode_dft_ch *ch = &d->ch[c];
    bode_chm *o = &m->c[c];
    int64  i, q, mean_l;
    int32  raw_mdb, lo, hi, cen;
    uint32 L = d->len;

    cpr_mdb[c] = bode_mdb_u64(s[c].cpr_q24) - 24 * 6020600 / 1000;   //20 log10(cpr / 2^24)

    dft_iq(ch->si, ch->sq, ch->sx, d->wsum, d->wc, d->ws, &i, &q);
    raw_mdb = bode_mdb_iq(i, q) + norm_mdb(d->wsum);

    o->code_mdb = (raw_mdb <= BODE_MDB_FLOOR / 2) ? BODE_MDB_FLOOR : raw_mdb + cpr_mdb[c];
    o->lvl_mdb  = (o->code_mdb == BODE_MDB_FLOOR) ? BODE_MDB_FLOOR : o->code_mdb + s[c].volt_mdb;
    o->ph_mdeg  = bode_atan2_mdeg(q, i);

    //Noise: sigma = MAD / 0.67449 / sqrt(6) (rk 2) or / sqrt(2) (rk 1); one bin's amplitude 2 sigma / sqrt(L)
    //(x sqrt(1.5) with the Hann window), as the host's median over the FFT bins gives for white noise
    if(!ch->mad_q8)
      o->noise_mdb = BODE_MDB_FLOOR;
    else
    {
      int32 sm = bode_mdb_u64(ch->mad_q8) - 48165 + 3421 - ((ch->rk == 2) ? 7782 : 3010);   //20 log10 sigma

      o->noise_mdb = sm + 6021 - bode_mdb_u64(L) / 2 + (pp->hann ? 1761 : 0) + cpr_mdb[c] + s[c].volt_mdb;
    }

    //Mean (the rounded mean x0: within half a raw unit), extremes, centring
    mean_l = (int64)ch->x0 * L;

    cen = code_c100(mean_l, L, s[c].cpr_q24, s[c].zero_q8);
    lo  = code_c100((int64)ch->xmin * L, L, s[c].cpr_q24, s[c].zero_q8);
    hi  = code_c100((int64)ch->xmax * L, L, s[c].cpr_q24, s[c].zero_q8);

    o->centre_c100 = cen;
    o->ac_c100     = ((hi - cen) > (cen - lo)) ? hi - cen : cen - lo;
    //fw 0.2.83: from this record only (bench 2026-10-20 10:52: the readout's sticky ADoverload leaked earlier captures in)
    o->clipped     = (uint8)((lo < 50) || (hi > 25450));

    //Standard deviation (host Measurement.std1 / std2): L^2 var = L sum x^2 - (sum x)^2 in raw units squared
    {
      uint64 a = ch->sxx * L, b = (uint64)(ch->su < 0 ? -ch->su : ch->su), b2 = b * b;

      o->std_mdb = (a <= b2) ? BODE_MDB_FLOOR : bode_mdb_u64(a - b2) / 2 - bode_mdb_u64(L) + cpr_mdb[c];
    }
    o->mean_uv     = (int32)(((int64)(cen - zero_code_c100[c]) * (int64)uv_q8[c]) / (256 * 100));

    //Halves
    dft_iq(ch->hi[0], ch->hq[0], ch->hx[0], d->hwsum[0], d->hwc[0], d->hws[0], &ih[c][0], &qh[c][0]);
    dft_iq(ch->hi[1], ch->hq[1], ch->hx[1], d->hwsum[1], d->hwc[1], d->hws[1], &ih[c][1], &qh[c][1]);
  }

  m->h_mdb  = m->c[1].lvl_mdb - m->c[0].lvl_mdb;
  m->h_mdeg = bode_wrap_mdeg(m->c[1].ph_mdeg - m->c[0].ph_mdeg);
  m->snr2_mdb = (m->c[1].noise_mdb == BODE_MDB_FLOOR) ? 200000 : m->c[1].lvl_mdb - m->c[1].noise_mdb;

  //CH1 leakage: the larger of the two side tones re the fundamental
  {
    int64 i0, q0, i1, q1, i, q;
    int32 a, b;

    dft_iq(d->ch[0].li[0], d->ch[0].lq[0], d->ch[0].sx, d->wsum, d->lwc[0], d->lws[0], &i0, &q0);
    dft_iq(d->ch[0].li[1], d->ch[0].lq[1], d->ch[0].sx, d->wsum, d->lwc[1], d->lws[1], &i1, &q1);
    dft_iq(d->ch[0].si, d->ch[0].sq, d->ch[0].sx, d->wsum, d->wc, d->ws, &i, &q);
    a = bode_mdb_iq(i0, q0);
    b = bode_mdb_iq(i1, q1);
    m->leak_mdb = ((a > b) ? a : b) - bode_mdb_iq(i, q);
  }

  //Halves: Hb / Ha = (Xb2 Xa1) / (Xb1 Xa2); the relative difference |Hb / Ha - 1| = |Xb2 Xa1 - Xb1 Xa2| / |Xb1 Xa2|
  m->half_ok = d->halves;

  //measure_pair's floor: each CH1 half at least CH1_MIN_CODES (0.5 code), else the halves are not compared
  if(d->halves)
  {
    int k;

    for(k = 0; k < 2; k++)
      if(bode_mdb_iq(ih[0][k], qh[0][k]) + norm_mdb(d->hwsum[k]) + cpr_mdb[0] < CH1_MIN_MDB)
        m->half_ok = 0;
  }
  m->half_mdb = m->half_mdeg = 0;
  m->half_rel_ppm = 0;

  if(m->half_ok)
  {
    int32 la[2], lb[2], pa[2], pb[2], num_mdb, den_mdb;
    int64 ar, ai, br, bi, cr, ci, dr, di, nr, ni;
    int   sh;

    for(c = 0; c < 2; c++)
    {
      la[c] = bode_mdb_iq(ih[c][0], qh[c][0]);
      lb[c] = bode_mdb_iq(ih[c][1], qh[c][1]);
      pa[c] = bode_atan2_mdeg(qh[c][0], ih[c][0]);
      pb[c] = bode_atan2_mdeg(qh[c][1], ih[c][1]);
    }

    m->half_mdb  = (lb[1] - lb[0]) - (la[1] - la[0]);
    m->half_mdeg = bode_wrap_mdeg((pb[1] - pb[0]) - (pa[1] - pa[0]));

    //The complex numbers scaled to below 2^30 each (common factors cancel in the ratio)
    ar = ih[1][1]; ai = qh[1][1];      //Xb2
    br = ih[0][0]; bi = qh[0][0];      //Xa1
    cr = ih[0][1]; ci = qh[0][1];      //Xb1
    dr = ih[1][0]; di = qh[1][0];      //Xa2

    //Bring each pair to the same scale: normalise Xb2 and Xb1 together, Xa1 and Xa2 together
    for(sh = 0; (ar >= (1ll << 30)) || (ar <= -(1ll << 30)) || (ai >= (1ll << 30)) || (ai <= -(1ll << 30)) ||
                (cr >= (1ll << 30)) || (cr <= -(1ll << 30)) || (ci >= (1ll << 30)) || (ci <= -(1ll << 30)); sh++)
    {
      ar /= 2; ai /= 2; cr /= 2; ci /= 2;
    }

    for(sh = 0; (br >= (1ll << 30)) || (br <= -(1ll << 30)) || (bi >= (1ll << 30)) || (bi <= -(1ll << 30)) ||
                (dr >= (1ll << 30)) || (dr <= -(1ll << 30)) || (di >= (1ll << 30)) || (di <= -(1ll << 30)); sh++)
    {
      br /= 2; bi /= 2; dr /= 2; di /= 2;
    }

    //num = Xb2 Xa1 - Xb1 Xa2 (each product below 2^61), den = Xb1 Xa2
    nr = (ar * br - ai * bi) - (cr * dr - ci * di);
    ni = (ar * bi + ai * br) - (cr * di + ci * dr);
    num_mdb = bode_mdb_iq(nr, ni);
    den_mdb = bode_mdb_iq(cr * dr - ci * di, cr * di + ci * dr);

    if(den_mdb > BODE_MDB_FLOOR)
    {
      uint64 r = (num_mdb <= BODE_MDB_FLOOR) ? 0 : pow10_scaled(num_mdb - den_mdb, bode_log2_q32(1000000));

      m->half_rel_ppm = (r > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32)r;
    }
  }
}

int bode_unsettled(const bode_meas *m)
{
  uint64 r1, r2, rel2, lim;
  int64  s12;

  if(!m->half_ok)
    return 0;

  if((m->half_mdb <= SETTLE_MDB) && (m->half_mdb >= -SETTLE_MDB) && (m->half_mdeg <= SETTLE_MDEG) &&
     (m->half_mdeg >= -SETTLE_MDEG))
    return 0;

  //noise_rel = sqrt(2) hypot(n1 / |X1|, n2 / |X2|); unsettled when half_rel > 4 sqrt(2) noise_rel, i.e.
  //half_rel^2 > 64 (r1^2 + r2^2). The ratios squared in units of 1e-12 (ppm^2)
  s12 = bode_log2_q32(1000000000000ull);
  r1  = (m->c[0].noise_mdb <= BODE_MDB_FLOOR) ? 0 : bode_exp2_round(s12 + 2 * (int64)(m->c[0].noise_mdb - m->c[0].lvl_mdb) * LOG2_10_PER_MDB);
  r2  = (m->c[1].noise_mdb <= BODE_MDB_FLOOR) ? 0 : bode_exp2_round(s12 + 2 * (int64)(m->c[1].noise_mdb - m->c[1].lvl_mdb) * LOG2_10_PER_MDB);
  lim = (r1 + r2 > (~0ull / 64)) ? ~0ull : 64 * (r1 + r2);
  rel2 = (uint64)m->half_rel_ppm * m->half_rel_ppm;

  return rel2 > lim;
}

//The host's Measurement properties (bode.py), fw 0.2.87
int bode_flat1(const bode_meas *m)
{
  return m->c[0].std_mdb < FLAT_STD_MDB;
}

int bode_ch1_dead(const bode_meas *m)
{
  return bode_flat1(m) || (m->c[0].code_mdb < CH1_MIN_MDB);
}

int bode_suspect(const bode_meas *m)
{
  return bode_ch1_dead(m) || ((m->c[1].std_mdb < FLAT_STD_MDB) && (m->c[1].ac_c100 < FLAT_C100));
}

int bode_flat2(const bode_meas *m, int coll)
{
  if(bode_flat1(m))
    return 0;

  return coll || ((m->c[1].ac_c100 < FLAT_C100) && (m->c[1].code_mdb < CH2_SMALL_MDB));
}

int bode_incoherent(const bode_meas *m)
{
  return m->leak_mdb > LEAK_LIMIT_MDB;
}

//bode.py BodeRunner._result, the flags of the measurement itself
uint16 bode_meas_flags(const bode_meas *m, int coll)
{
  uint16 f = 0;

  if(m->c[0].clipped)                                   f |= BODE_F_CH1_CLIPPED;
  if(m->c[1].clipped)                                   f |= BODE_F_CLIPPED;
  if(bode_flat2(m, coll))                               f |= BODE_F_CH2_OVERLOAD;
  if(m->c[0].code_mdb < CH1_LOW_MDB)                    f |= BODE_F_CH1_LOW;
  if(m->c[1].code_mdb < CH2_SMALL_MDB)                  f |= BODE_F_CH2_SMALL;
  if(m->snr2_mdb < LOW_SNR_MDB)                         f |= BODE_F_LOW_SNR;
  if((m->leak_mdb > LEAK_LIMIT_MDB) || bode_ch1_dead(m)) f |= BODE_F_INCOHERENT;
  if(bode_unsettled(m))                                 f |= BODE_F_UNSETTLED;

  return f;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Markers

const int32 bode_ph_targets_mdeg[BODE_PH_TARGETS] = { 45000, -45000, 90000, -90000, -135000, -180000 };

//Crossings of y through level between points a..b (inclusive, valid points only, in order); writes up to max frequencies
static int crossings(const int64 *x, const int32 *y, int n, int32 level, uint64 *out, int max, int last_only)
{
  int i, k = 0;

  for(i = 0; i + 1 < n; i++)
  {
    int64 a = (int64)y[i] - level, b = (int64)y[i + 1] - level, xc;

    if(a == 0)
      xc = x[i];
    else if((a < 0) != (b < 0) && (b != 0))
      xc = x[i] + (a * (x[i + 1] - x[i])) / (a - b);
    else
      continue;

    if(last_only)
    {
      out[0] = bode_exp2_round(xc);
      k = 1;
    }
    else if(k < max)
      out[k++] = bode_exp2_round(xc);
  }

  if((n > 0) && (y[n - 1] == level))
  {
    if(last_only)
    {
      out[0] = bode_exp2_round(x[n - 1]);
      k = 1;
    }
    else if(k < max)
      out[k++] = bode_exp2_round(x[n - 1]);
  }

  return k;
}

void bode_markers_find(const uint64 *f_mhz, const int32 *mag_mdb, const int32 *ph_mdeg, const uint8 *ok, int n,
                       bode_markers *mk)
{
  static int64 x[BODE_MAX_POINTS];
  static int32 y[BODE_MAX_POINTS], ph[BODE_MAX_POINTS];
  int   i, v = 0, imax = 0;
  int32 m0, m1, ref;

  for(i = 0; i < (int)sizeof(*mk); i++)
    ((uint8 *)mk)[i] = 0;

  for(i = 0; i < n && v < BODE_MAX_POINTS; i++)
  {
    if(!ok[i])
      continue;

    x[v]  = bode_log2_q32(f_mhz[i]);
    y[v]  = mag_mdb[i];
    ph[v] = ph_mdeg[i];

    //Unwrap
    if(v)
    {
      while(ph[v] - ph[v - 1] > 180000)
        ph[v] -= 360000;
      while(ph[v] - ph[v - 1] < -180000)
        ph[v] += 360000;
    }

    if(y[v] > y[imax])
      imax = v;

    v++;
  }

  mk->valid = (uint8)((v > 255) ? 255 : v);
  if(v < 2)
    return;

  m0 = y[0];
  m1 = y[v - 1];

  if((imax > 0) && (imax < v - 1) && (y[imax] >= ((m0 > m1) ? m0 : m1) + 1000))
  {
    //Resonant: the vertex of the parabola through the maximum and its neighbours (dB over log f), Lagrange form in
    //x relative to the maximum (Q32 log2 -> Q12 to keep the products in 64 bits up to 11 octaves between points)
    int64 xa = (x[imax - 1] - x[imax]) >> 20, xb = (x[imax + 1] - x[imax]) >> 20;
    int64 d0 = (int64)y[imax - 1] - y[imax], d2 = (int64)y[imax + 1] - y[imax];
    int64 num = d0 * xb * xb - d2 * xa * xa, den = 2 * (d0 * xb - d2 * xa), xv = 0;
    int64 l0, l1, l2;
    uint64 lo[1] = { 0 }, hi[1] = { 0 };
    int   nlo, nhi;

    if(den < 0)
      xv = num / den;

    if(xv < xa) xv = xa;
    if(xv > xb) xv = xb;

    //Lagrange basis at xv (Q30)
    l0 = (((xv) * (xv - xb)) << 30) / (xa * (xa - xb));
    l1 = (((xv - xa) * (xv - xb)) << 30) / ((-xa) * (-xb));
    l2 = (((xv - xa) * (xv)) << 30) / (xb * (xb - xa));
    ref = (int32)(((int64)y[imax - 1] * l0 + (int64)y[imax] * l1 + (int64)y[imax + 1] * l2 + (1ll << 29)) >> 30);

    mk->shape     = BODE_SHAPE_RESONANT;
    mk->have_peak = 1;
    mk->peak_mhz  = bode_exp2_round(x[imax] + (xv << 20));
    mk->peak_mdb  = ref;
    mk->ref_mdb   = ref;

    nlo = crossings(x, y, imax + 1, ref - 3000, lo, 1, 1);
    nhi = crossings(x + imax, y + imax, v - imax, ref - 3000, hi, 1, 0);

    if(nlo)
      mk->f3db_mhz[mk->n3db++] = lo[0];
    if(nhi)
      mk->f3db_mhz[mk->n3db++] = hi[0];

    if(nlo && nhi && (hi[0] > lo[0]))
    {
      mk->have_q = 1;
      mk->q_x100 = (uint32)((mk->peak_mhz * 100 + (hi[0] - lo[0]) / 2) / (hi[0] - lo[0]));
    }
  }
  else
  {
    ref = (m0 >= m1) ? m0 : m1;
    mk->shape   = (m0 > m1 + 3000) ? BODE_SHAPE_LOWPASS : (m1 > m0 + 3000) ? BODE_SHAPE_HIGHPASS : BODE_SHAPE_FLAT;
    mk->ref_mdb = ref;
    mk->n3db    = (uint8)crossings(x, y, v, ref - 3000, mk->f3db_mhz, 2, 0);
  }

  for(i = 0; i < BODE_PH_TARGETS; i++)
  {
    uint64 f[1] = { 0 };

    if(crossings(x, ph, v, bode_ph_targets_mdeg[i], f, 1, 0))
      mk->ph_mhz[i] = f[0];
  }
}

int bode_interp(const uint64 *f_mhz, const int32 *y, const uint8 *ok, int n, uint64 f, int32 *out)
{
  int    i, prev = -1;
  int64  xf = bode_log2_q32(f);

  for(i = 0; i < n; i++)
  {
    if(!ok[i])
      continue;

    if(f_mhz[i] == f)
    {
      *out = y[i];
      return 1;
    }

    if((prev >= 0) && (f_mhz[prev] < f) && (f_mhz[i] > f))
    {
      int64 x0 = bode_log2_q32(f_mhz[prev]), x1 = bode_log2_q32(f_mhz[i]);

      *out = (int32)(y[prev] + (((int64)y[i] - y[prev]) * (xf - x0)) / (x1 - x0));
      return 1;
    }

    prev = i;
  }

  return 0;
}
