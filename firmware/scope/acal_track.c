//----------------------------------------------------------------------------------------------------------------------------------
// acal_track.c - background tracking of the core A / core B mismatch (acal_track.h, docs/plans/interleave-tracking.md)
//----------------------------------------------------------------------------------------------------------------------------------

#include "acal_track.h"

#define TRK_LO          4                 //usable pair: all three samples within 4..251
#define TRK_HI          251
#define TRK_MAX_SLOPE   6                 //|s[n-1] - s[n+1]| <= 6 codes
#define TRK_MIN_PAIRS   256               //usable record: at least 256 usable pairs ...
#define TRK_DROP_SHIFT  3                 //... and at most 1/8 of the in-range pairs dropped for slope
#define TRK_Y_CLAMP     32                //|y| <= 16 codes (y in half codes)

//The solve works on normalised central moments in Q30 of u = (x4 - x0) 2^p / 1024 (acal_track_solve), the y moments in
//Q24 codes. The thresholds at zoom 0 (u = x / 256), shifted by 2p / 4p at zoom p:
#define TRK_V_MIN       ((int64_t)1 << 20)  //e identifiable: v >= 64 codes^2 = 2^-10 u^2
//q identifiable: w >= 65536 codes^4 = 2^-16 u^4 (a sine of +-27 codes) and w >= v^2 / 4 (x visits three separated levels:
//a sine has w = v^2 / 2, three equal levels v^2 / 2, a quiet input mixed with the post square about v^2 / 2). The design's
//256 codes^4 alone let a two-level signal through: the noise spread of x within each level (sigma_x^2 about 0.4 code^2)
//gives w = sigma_x^2 D^2, about 3000 codes^4 for the post's 88-code step, and because the true A - B does not vary with
//that noise the fit then pulls q towards 0 and biases e (test_acal_track (c))
#define TRK_W_MIN       ((int64_t)16384)
#define TRK_W_REL_SHIFT 2
#define TRK_D_MAX       (32 * 256)        //|d| <= 32 codes (1/256 code)
#define TRK_E_MAX       (1 << 21)         //|e| <= 1/8 (Q24)
#define TRK_Q_MAX       (1 << 16)         //|q| <= 3.9e-3 per code^2 (6.9 codes at 42 codes; Q24)
#define TRK_NUM_MAX     ((int64_t)1 << 32)  //solve numerators (Q24 codes): |t_k| <= 64 codes = 2^30 normally

//Rounded division, half away from zero (symmetric for negative n), d > 0
static int64_t sdiv(int64_t n, int64_t d)
{
  return (n >= 0) ? (n + d / 2) / d : -((-n + d / 2) / d);
}

//Rounded shift right, symmetric
static int64_t sshr(int64_t v, int s)
{
  int64_t h = (int64_t)1 << (s - 1);

  return (v >= 0) ? ((v + h) >> s) : -((-v + h) >> s);
}

//Q30 product
static int64_t qmul(int64_t a, int64_t b)
{
  return sshr(a * b, 30);
}

static int64_t clamp64(int64_t v, int64_t lim)
{
  return (v > lim) ? lim : ((v < -lim) ? -lim : v);
}

static uint32_t isqrt32(uint32_t v)
{
  uint32_t r = 0, b = 1u << 30;

  while(b > v)
    b >>= 2;

  while(b)
  {
    if(v >= r + b)
    {
      v -= r + b;
      r = (r >> 1) + b;
    }
    else
    {
      r >>= 1;
    }

    b >>= 2;
  }

  return r;
}

//----------------------------------------------------------------------------------------------------------------------------------

void acal_track_reset(acal_track *t)
{
  uint8_t *p = (uint8_t *)t;
  uint32_t i;

  for(i=0;i<sizeof(*t);i++)
    p[i] = 0;
}

//----------------------------------------------------------------------------------------------------------------------------------

//Normalised moment: sum / n x 2^s (s may be negative), rounded
static int64_t norm(int64_t sum, int s, int64_t n)
{
  return (s >= 0) ? sdiv(sum * ((int64_t)1 << s), n) : sdiv(sum, n * ((int64_t)1 << -s));
}

void acal_track_solve(acal_track *t)
{
  int64_t n = t->m[0], M1 = t->m[1], M2 = t->m[2], M3 = t->m[3], M4 = t->m[4];
  int64_t x0, x2, C1, C2, C3, C4, U0, U1, U2;
  int64_t mu, c2, c3, c4, t0, t1, t2, v, k3, beta, w, num, q_u, e_u, q40, b32, a;
  int     p = 0, e_ok;

  //Centre on the quarter code x0 nearest the mean: exact integer shifts of the sums (|x0| <= 496; each term of C4 below
  //1.5e18, of U2 below 3.4e16). The fit runs about x0, free of the cancellation between large raw moments when the
  //signal sits off mid-scale, and is moved back to x = 0 at the end
  x0 = clamp64(sdiv(M1, n), 496);
  x2 = x0 * x0;
  C1 = M1 - x0 * n;
  C2 = M2 - 2 * x0 * M1 + x2 * n;
  C3 = M3 - 3 * x0 * M2 + 3 * x2 * M1 - x2 * x0 * n;
  C4 = M4 - 4 * x0 * M3 + 6 * x2 * M2 - 4 * x2 * x0 * M1 + x2 * x2 * n;
  C4 = (C4 < 0) ? 0 : C4;
  U0 = t->t[0];
  U1 = t->t[1] - x0 * t->t[0];
  U2 = t->t[2] - 2 * x0 * t->t[1] + x2 * t->t[0];

  //Zoom: u = (x4 - x0) 2^p / 1024 (|u| < 1 at p = 0) with the largest p <= 8 that keeps E[u^4] <= 1, so the Q30
  //moments keep their precision at any spread. The products below stay bounded (Cauchy-Schwarz): |c3| <= 1,
  //|beta k3| <= c4, |beta t1| <= sqrt(c4) |y|, |y| < 64 codes (Q24: 2^30)
  while((p < 8) && (C4 <= n * ((int64_t)1 << (36 - 4 * p))))
    p++;

  //Normalised central moments, Q30 of u^k, and the y moments, Q24 codes (U_k: 1/256 code x (x4 - x0)^k)
  mu = norm(C1, 20 + p, n);
  c2 = norm(C2, 10 + 2 * p, n);
  c3 = norm(C3, 3 * p, n);
  c4 = norm(C4, 4 * p - 10, n);
  t0 = norm(U0, 16, n);
  t1 = norm(U1, 6 + p, n);
  t2 = norm(U2, 2 * p - 4, n);

  //Spread of x, and the cross term of the quadratic part
  v  = c2 - qmul(mu, mu);
  v  = (v < 0) ? 0 : v;
  k3 = c3 - qmul(c2, mu);

  //sqrt(v) in 1/16 code: v (codes^2) = v_Q30 / 2^(14 + 2p), x 256
  t->spread16 = (uint16_t)isqrt32((uint32_t)(v >> (6 + 2 * p)));

  //Held values: q Q40 per code^2, the linear coefficient about x0 b = e + 2 q X0 (X0 = x0 / 4 codes) Q32 per code. In
  //the solve's units (Q24 codes per u^k, 256 / 2^p codes per u): q_u = q 2^-2p, e_u = b 2^-p
  q40  = (int64_t)t->q_q24 * 65536;
  q_u  = q40 >> (2 * p);
  b32  = (int64_t)t->e_q24 * 256 + sdiv(q40 * x0, 512);
  e_u  = sdiv(b32, (int64_t)1 << p);
  e_ok = (v >= (TRK_V_MIN << (2 * p)));

  t->flags &= (uint8_t)~(ACAL_TRK_GAIN | ACAL_TRK_BOW);

  if(e_ok)
  {
    //Orthogonal polynomials: p1 = u - mu, p2 = u^2 - beta p1 - c2
    beta = sdiv(k3 * ((int64_t)1 << 30), v);
    w    = (c4 - qmul(c2, c2)) - qmul(beta, k3);

    if((w >= (TRK_W_MIN << (4 * p))) && (w >= (qmul(v, v) >> TRK_W_REL_SHIFT)))
    {
      num = clamp64(t2 - qmul(beta, t1) - qmul(c2 - qmul(beta, mu), t0), TRK_NUM_MAX);
      q_u = clamp64(sdiv(num * ((int64_t)1 << 30), w), ((int64_t)TRK_Q_MAX << 16) >> (2 * p));
      q40 = q_u * ((int64_t)1 << (2 * p));
      t->q_q24 = (int32_t)sshr(q40, 16);
      t->flags |= ACAL_TRK_BOW;
    }

    num = clamp64(t1 - qmul(mu, t0) - qmul(q_u, k3), TRK_NUM_MAX);
    e_u = clamp64(sdiv(num * ((int64_t)1 << 30), v), ((int64_t)1 << 33) >> p);
    b32 = e_u * ((int64_t)1 << p);
    t->e_q24 = (int32_t)clamp64(sshr(b32 - sdiv(q40 * x0, 512), 8), TRK_E_MAX);
    t->flags |= ACAL_TRK_GAIN;
  }

  //Value at x0 (Q24 codes), moved to x = 0: d = a - b X0 + q X0^2
  a = t0 - qmul(q_u, c2) - qmul(e_u, mu);
  a = a - sdiv(b32 * x0, 1024) + sdiv(q40 * x2, (int64_t)1 << 20);
  t->d_q8 = (int32_t)clamp64(sshr(a, 16), TRK_D_MAX);
}

//----------------------------------------------------------------------------------------------------------------------------------

int acal_track_record(acal_track *t, const uint8_t *buf, uint32_t count, int invert, int32_t applied_d_q8,
                      int32_t applied_e_q24, uint32_t now_ms)
{
  uint32_t flip = invert ? 255 : 0;
  uint32_t n, pairs = 0, inrange = 0;
  int32_t  l, c, r, x, y, x2;
  int64_t  s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, y0 = 0, y1 = 0, y2 = 0;
  int64_t  S[5], T[3], ad, ae;
  int      k;

  //Rate gate: at most one accepted record per ACAL_TRK_MIN_MS
  if(t->records && (uint32_t)(now_ms - t->last_ms) < ACAL_TRK_MIN_MS)
    return 0;

  if(count > ACAL_TRK_MAX_COUNT)
    count = ACAL_TRK_MAX_COUNT;

  if(count >= 3)
  {
    l = buf[0] ^ flip;
    c = buf[1] ^ flip;

    for(n=1;n<count-1;n++)
    {
      r = buf[n + 1] ^ flip;

      if((l >= TRK_LO) && (l <= TRK_HI) && (c >= TRK_LO) && (c <= TRK_HI) && (r >= TRK_LO) && (r <= TRK_HI))
      {
        inrange++;

        if((l - r <= TRK_MAX_SLOPE) && (r - l <= TRK_MAX_SLOPE))
        {
          //x in quarter codes, y = A - B in half codes (+ when s is core A: odd n)
          x = 2 * c + l + r - 512;
          y = 2 * c - l - r;
          y = (n & 1) ? y : -y;
          y = (y > TRK_Y_CLAMP) ? TRK_Y_CLAMP : ((y < -TRK_Y_CLAMP) ? -TRK_Y_CLAMP : y);
          x2 = x * x;

          pairs++;
          s1 += x;
          s2 += x2;
          s3 += (int64_t)x2 * x;
          s4 += (int64_t)x2 * x2;
          y0 += y;
          y1 += x * y;
          y2 += (int64_t)x2 * y;
        }
      }

      l = c;
      c = r;
    }
  }

  t->last_pairs = (uint16_t)((pairs > 65535) ? 65535 : pairs);

  if((pairs < TRK_MIN_PAIRS) || (((inrange - pairs) << TRK_DROP_SHIFT) > inrange))
  {
    t->flags &= (uint8_t)~ACAL_TRK_LAST;
    return 0;
  }

  //Open-loop domain, y in 1/256 code: y256 = 128 y + d (1/256 code) + e x (e Q24, x4 quarter codes: e x4 / 2^18).
  //|ae x S3| <= 2^21 x 496^3 x 16384 = 4.2e18
  ad = (applied_d_q8 > TRK_D_MAX) ? TRK_D_MAX : ((applied_d_q8 < -TRK_D_MAX) ? -TRK_D_MAX : applied_d_q8);
  ae = (applied_e_q24 > TRK_E_MAX) ? TRK_E_MAX : ((applied_e_q24 < -TRK_E_MAX) ? -TRK_E_MAX : applied_e_q24);
  s0 = pairs;

  S[0] = s0; S[1] = s1; S[2] = s2; S[3] = s3; S[4] = s4;
  T[0] = 128 * y0 + ad * s0 + sdiv(ae * s1, 1 << 18);
  T[1] = 128 * y1 + ad * s1 + sdiv(ae * s2, 1 << 18);
  T[2] = 128 * y2 + ad * s2 + sdiv(ae * s3, 1 << 18);

  //Forgetting: M <- M - M / 256 + S (symmetric rounding, no drift for negative sums)
  for(k=0;k<5;k++)
    t->m[k] += S[k] - sshr(t->m[k], ACAL_TRK_TAU_SHIFT);

  for(k=0;k<3;k++)
    t->t[k] += T[k] - sshr(t->t[k], ACAL_TRK_TAU_SHIFT);

  t->last_ms = now_ms;
  t->records++;
  t->flags |= ACAL_TRK_DATA | ACAL_TRK_LAST;

  acal_track_solve(t);

  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------

void acal_track_apply(const acal_track *t, acal_corr *c)
{
  int64_t e = t->e_q24;
  int64_t k;

  if((c->version != 3) || ((t->d_q8 == 0) && (e == 0)))
    return;

  //A' = A - (d + e x) / 2 with x = code - 128: gain_a (1 - e/2), off_a (1 - e/2) - 65536 (d - 128 e) / 2; B the
  //opposite. In 2^-25: off e_q24 / 2^25 = off e / 2, 65536 d / 2 = d_q8 2^32 / 2^25, 65536 x 128 e / 2 = e_q24 2^23 / 2^25
  k = (int64_t)t->d_q8 * ((int64_t)1 << 32) - e * ((int64_t)1 << 23);

  c->gain_a = (uint32_t)((int64_t)c->gain_a - sshr((int64_t)c->gain_a * e, 25));
  c->gain_b = (uint32_t)((int64_t)c->gain_b + sshr((int64_t)c->gain_b * e, 25));
  c->off_a  = (int32_t)(c->off_a - sshr((int64_t)c->off_a * e + k, 25));
  c->off_b  = (int32_t)(c->off_b + sshr((int64_t)c->off_b * e + k, 25));

  acal_clips(c);
}
