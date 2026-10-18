//----------------------------------------------------------------------------------------------------------------------------------
// fx_fft.c - fixed-point real-input FFT, windows and dB conversion (see fx_fft.h). No libc, no float, host-buildable.
//----------------------------------------------------------------------------------------------------------------------------------

#include "fx_fft.h"

extern const int32  fx_cos_tab[(FX_FFT_N_MAX / 4) + 1];
extern const uint32 fx_log2_tab[257];
extern const int32  fx_flattop[5];

#define Q30           (1 << 30)
#define QUARTER       (FX_FFT_N_MAX / 4)

//----------------------------------------------------------------------------------------------------------------------------------

int32 fx_cos_q30(uint32 k, int log2n)
{
  uint32 K = (k << (FX_FFT_LOG2_MAX - log2n)) & (FX_FFT_N_MAX - 1);
  uint32 r = K & (QUARTER - 1);

  switch(K >> (FX_FFT_LOG2_MAX - 2))
  {
    case 0:  return fx_cos_tab[r];
    case 1:  return -fx_cos_tab[QUARTER - r];
    case 2:  return -fx_cos_tab[r];
    default: return fx_cos_tab[QUARTER - r];
  }
}

//sin(2 pi k / N) = cos(2 pi (k - N/4) / N)
static int32 fx_sin_q30(uint32 k, int log2n)
{
  return fx_cos_q30(k - (1u << (log2n - 2)), log2n);
}

//----------------------------------------------------------------------------------------------------------------------------------

static int fx_clz32(uint32 v)
{
#if defined(__GNUC__)
  return v ? __builtin_clz(v) : 32;
#else
  int n = 0;
  if(!v) return 32;
  while(!(v & 0x80000000u)) { v <<= 1; n++; }
  return n;
#endif
}

int32 fx_log2_q16(uint64 v)
{
  uint32 hi = (uint32)(v >> 32);
  int    e;
  uint64 m;
  uint32 idx, f, t0, t1;

  if(v == 0)
    return -0x7FFFFFFF;

  e = hi ? 63 - fx_clz32(hi) : 31 - fx_clz32((uint32)v);

  //Leading one to bit 63; the next 8 bits index the table, the 16 after them interpolate
  m   = v << (63 - e);
  idx = (uint32)(m >> 55) & 0xFF;
  f   = (uint32)(m >> 39) & 0xFFFF;
  t0  = fx_log2_tab[idx];
  t1  = fx_log2_tab[idx + 1];

  return (e << 16) + (int32)t0 + (int32)(((t1 - t0) * f + 0x8000) >> 16);
}

//10 * log10(2) * 100 = 301.0299957 centi-dB per octave; K = 301.0299957 * 2^16
int32 fx_cdb_from_q16(int32 log2q16)
{
  return (int32)(((int64)log2q16 * 19728302 + 0x80000000LL) >> 32);
}

uint32 fx_enbw_milli(int window)
{
  switch(window)
  {
    case FX_WIN_HANN:    return 1500;
    case FX_WIN_FLATTOP: return 3770;
    default:             return 1000;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

int fx_fft_init(FX_FFT *p, int log2n, int window, int32 *win, int32 *buf)
{
  uint32 n, half;
  int64  sum;
  int    i;

  if((log2n < FX_FFT_LOG2_MIN) || (log2n > FX_FFT_LOG2_MAX) || (window < 0) || (window >= FX_WIN_COUNT))
    return -1;

  p->log2n  = log2n;
  p->window = window;
  p->win    = win;
  p->buf    = buf;
  p->shift  = 0;

  half = 1u << (log2n - 1);

  //Periodic windows, w[n] for n = 0..N/2 (the other half mirrors)
  for(n = 0; n <= half; n++)
  {
    switch(window)
    {
      case FX_WIN_HANN:
        win[n] = (int32)(((int64)Q30 - fx_cos_q30(n, log2n)) >> 1);
        break;

      case FX_WIN_FLATTOP:
      {
        int64 w = fx_flattop[0];

        for(i = 1; i < 5; i++)
        {
          int64 t = ((int64)fx_flattop[i] * fx_cos_q30(n * i, log2n) + (1 << 29)) >> 30;
          w += (i & 1) ? -t : t;
        }
        win[n] = (int32)w;
        break;
      }

      default:
        win[n] = Q30;
        break;
    }
  }

  //Window sum over all N points: w[0] + w[N/2] + 2 * (w[1] + ... + w[N/2 - 1])
  sum = 0;
  for(n = 1; n < half; n++)
    sum += win[n];
  sum = 2 * sum + win[0] + win[half];

  //Level term in log2 units (fx_fft.h): L = log2|X|^2 - (2 log2(sum) - 59) - 2 shift
  p->norm_q16 = 2 * fx_log2_q16((uint64)sum) - (59 << 16);

  return 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
//Radix-2 decimation in time on M = N/2 complex points, input in bit-reversed order. W = c - i s = exp(-2 pi i j / len).

static void fx_fft_complex(int32 *buf, int log2n)
{
  int    log2m = log2n - 1;
  uint32 M = 1u << log2m;
  uint32 half, len, i, j, tstride;
  int    stage;

  for(stage = 1; stage <= log2m; stage++)
  {
    half    = 1u << (stage - 1);
    len     = half << 1;
    tstride = M >> (stage - 1);         //W_len^j = W_N^(j * N / len)

    //j = 0: W = 1
    for(i = 0; i < M; i += len)
    {
      int32 *a = buf + 2 * i;
      int32 *b = a + 2 * half;
      int32 ar = a[0], ai = a[1], br = b[0], bi = b[1];

      a[0] = ar + br;  a[1] = ai + bi;
      b[0] = ar - br;  b[1] = ai - bi;
    }

    for(j = 1; j < half; j++)
    {
      int32 c = fx_cos_q30(j * tstride, log2n);
      int32 s = fx_sin_q30(j * tstride, log2n);

      for(i = j; i < M; i += len)
      {
        int32 *a = buf + 2 * i;
        int32 *b = a + 2 * half;
        int32 br = b[0], bi = b[1];
        int32 tr = (int32)(((int64)br * c + (int64)bi * s + (1 << 29)) >> 30);
        int32 ti = (int32)(((int64)bi * c - (int64)br * s + (1 << 29)) >> 30);
        int32 ar = a[0], ai = a[1];

        a[0] = ar + tr;  a[1] = ai + ti;
        b[0] = ar - tr;  b[1] = ai - ti;
      }
    }
  }
}

//Real split step: Z_k (k < M) of z[n] = x[2n] + i x[2n+1] into 2 X_k for k = 0..M (bin M at buf[N], buf[N + 1]).
static void fx_fft_split(int32 *buf, int log2n)
{
  uint32 M = 1u << (log2n - 1);
  uint32 k;
  int32  zr = buf[0], zi = buf[1];

  buf[0]     = 2 * (zr + zi);  buf[1]     = 0;
  buf[2 * M] = 2 * (zr - zi);  buf[2 * M + 1] = 0;

  for(k = 1; k < M / 2; k++)
  {
    int32 *A = buf + 2 * k;
    int32 *B = buf + 2 * (M - k);
    int32 ar = A[0], ai = A[1], br = B[0], bi = B[1];
    int32 er = ar + br, ei = ai - bi;          //E  = A + conj(B)
    int32 orr = ai + bi, oi = br - ar;         //O' = -i (A - conj(B))
    int32 c = fx_cos_q30(k, log2n);
    int32 s = fx_sin_q30(k, log2n);
    int32 tr = (int32)(((int64)orr * c + (int64)oi * s + (1 << 29)) >> 30);   //T = (c - i s) O'
    int32 ti = (int32)(((int64)oi * c - (int64)orr * s + (1 << 29)) >> 30);

    A[0] = er + tr;  A[1] = ei + ti;           //X_k     = E + T
    B[0] = er - tr;  B[1] = ti - ei;           //X_(M-k) = conj(E - T)
  }

  //k = M/2: X = conj(Z)
  buf[M]     = 2 * buf[M];
  buf[M + 1] = -2 * buf[M + 1];
}

//----------------------------------------------------------------------------------------------------------------------------------

static int fx_bits(uint32 v)
{
  return 32 - fx_clz32(v);
}

//Scale so that N * max|x| <= 2^29: max|x| < 2^b, shift s = 29 - log2n - b. Bounds: every partial DFT component
//<= N * max <= 2^29, twiddle products <= sqrt(2) * 2^29, split step outputs (2 X_k) <= 2^30, |X|^2 < 2^61.
#define FX_LOAD(SAMPLE)                                                                         \
  {                                                                                             \
    int    log2n = p->log2n;                                                                    \
    uint32 N = 1u << log2n, M = N >> 1, n, r = 0, bit;                                          \
    int    sh = 30 - p->shift;                                                                  \
    int64  rnd = (int64)1 << (sh - 1);                                                          \
    const int32 *win = p->win;                                                                  \
    int32 *buf = p->buf;                                                                        \
                                                                                                \
    for(n = 0; n < M; n++)                                                                      \
    {                                                                                           \
      uint32 m0 = 2 * n, m1 = m0 + 1;                                                           \
      int32  w0 = win[m0 <= M ? m0 : N - m0];                                                   \
      int32  w1 = win[m1 <= M ? m1 : N - m1];                                                   \
                                                                                                \
      buf[2 * r]     = (int32)(((int64)(SAMPLE(m0)) * w0 + rnd) >> sh);                         \
      buf[2 * r + 1] = (int32)(((int64)(SAMPLE(m1)) * w1 + rnd) >> sh);                         \
                                                                                                \
      /* reversed increment of r over log2n - 1 bits */                                         \
      bit = M >> 1;                                                                             \
      while(r & bit) { r ^= bit; bit >>= 1; }                                                   \
      r |= bit;                                                                                 \
    }                                                                                           \
                                                                                                \
    fx_fft_complex(buf, log2n);                                                                 \
    fx_fft_split(buf, log2n);                                                                   \
  }

void fx_fft_run_u8(FX_FFT *p, const uint8 *x, int32 offset)
{
  int32 lo = 0 - offset, hi = 255 - offset;
  uint32 maxabs = (uint32)((lo < 0 ? -lo : lo) > (hi < 0 ? -hi : hi) ? (lo < 0 ? -lo : lo) : (hi < 0 ? -hi : hi));

  p->shift = 29 - p->log2n - fx_bits(maxabs);

#define FX_U8(m) ((int32)x[m] - offset)
  FX_LOAD(FX_U8)
#undef FX_U8
}

void fx_fft_run_i32(FX_FFT *p, const int32 *x, int32 offset)
{
  uint32 N = 1u << p->log2n, n, maxabs = 0;

  for(n = 0; n < N; n++)
  {
    int32  v = x[n] - offset;
    uint32 a = (uint32)(v < 0 ? -v : v);

    if(a > maxabs)
      maxabs = a;
  }

  p->shift = 29 - p->log2n - fx_bits(maxabs);

#define FX_I32(m) (x[m] - offset)
  FX_LOAD(FX_I32)
#undef FX_I32
}

//----------------------------------------------------------------------------------------------------------------------------------

void fx_fft_cdb(const FX_FFT *p, int first, int count, int16 *out)
{
  uint32 M = 1u << (p->log2n - 1);
  int32  base = p->norm_q16 + p->shift * 131072;   //2 * shift octaves
  int    k;

  for(k = first; k < first + count; k++)
  {
    int32  re = p->buf[2 * k], im = p->buf[2 * k + 1];
    uint64 pw = (uint64)((int64)re * re) + (uint64)((int64)im * im);
    int32  cdb;

    if(pw == 0)
    {
      *out++ = FX_CDB_FLOOR;
      continue;
    }

    //DC and Nyquist carry their full value in one bin (no conjugate partner): one octave of power less
    cdb = fx_cdb_from_q16(fx_log2_q16(pw) - base - (((k == 0) || ((uint32)k == M)) ? 65536 : 0));

    if(cdb < FX_CDB_FLOOR)
      cdb = FX_CDB_FLOOR;
    else if(cdb > 32767)
      cdb = 32767;

    *out++ = (int16)cdb;
  }
}
