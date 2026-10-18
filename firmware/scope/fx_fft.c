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

//----------------------------------------------------------------------------------------------------------------------------------
// Memory access. The scope runs with the MMU and so the D-cache off: every data access goes to DDR (about 90 CPU cycles
// on the bench, fw 0.2.27 FFT_BENCH), the stack included. So the hot loops are ARM assembly with everything in
// registers: complex values move as one LDRD / STRD (or a 4-word STM), twiddles come from a per-plan table, the first
// stage is fused into the windowed load. Each has a C twin that computes the same integers (the host build uses it);
// host_tests `make test-arm` runs the assembly under qemu-arm against numpy and byte-compares it with the C build.

typedef struct { int32 re, im; } __attribute__((aligned(8), may_alias)) fx_cpx;
typedef uint16 __attribute__((may_alias)) fx_u16;

static inline void fx_ld(const int32 *a, int32 *re, int32 *im)
{
  fx_cpx v = *(const fx_cpx *)a;

  *re = v.re;
  *im = v.im;
}

static inline void fx_st(int32 *a, int32 re, int32 im)
{
  fx_cpx v;

  v.re = re;
  v.im = im;
  *(fx_cpx *)a = v;
}

//(a * c + b * s) >> 30, rounded
static inline int32 fx_mac30(int32 a, int32 c, int32 b, int32 s)
{
  return (int32)(((int64)a * c + (int64)b * s + (1 << 29)) >> 30);
}

//----------------------------------------------------------------------------------------------------------------------------------

int fx_fft_init(FX_FFT *p, int log2n, int window, int32 *win, int32 *buf)
{
  uint32 n, N, half, k;
  int32 *tw;
  int64  sum;
  int    i;

  if((log2n < FX_FFT_LOG2_MIN) || (log2n > FX_FFT_LOG2_MAX) || (window < 0) || (window >= FX_WIN_COUNT) ||
     (((unsigned long)win | (unsigned long)buf) & 7))
    return -1;

  p->log2n  = log2n;
  p->window = window;
  p->win    = win;
  p->buf    = buf;
  p->shift  = 0;

  N    = 1u << log2n;
  half = N >> 1;

  //Periodic windows: w[n] for n = 0..N/2, mirrored into w[N - n]
  for(n = 0; n <= half; n++)
  {
    int32 w;

    switch(window)
    {
      case FX_WIN_HANN:
        w = (int32)(((int64)Q30 - fx_cos_q30(n, log2n)) >> 1);
        break;

      case FX_WIN_FLATTOP:
      {
        int64 a = fx_flattop[0];

        for(i = 1; i < 5; i++)
        {
          int64 t = ((int64)fx_flattop[i] * fx_cos_q30(n * i, log2n) + (1 << 29)) >> 30;
          a += (i & 1) ? -t : t;
        }
        w = (int32)a;
        break;
      }

      default:
        w = Q30;
        break;
    }

    win[n] = w;
    if(n && (n < half))
      win[N - n] = w;
  }

  //Twiddles W_N^k = c - i s for k < N/2, (c, s) pairs after the window: the split step uses k < N/4, the N/2-point
  //complex stages the even entries (W_(N/2)^k = W_N^2k)
  tw = win + N;
  for(k = 0; k < N / 2; k++)
  {
    tw[2 * k]     = fx_cos_q30(k, log2n);
    tw[2 * k + 1] = fx_sin_q30(k, log2n);
  }

  //Window sum over all N points
  sum = 0;
  for(n = 0; n < N; n++)
    sum += win[n];

  //Level term in log2 units (fx_fft.h): L = log2|X|^2 - (2 log2(sum) - 59) - 2 shift
  p->norm_q16 = 2 * fx_log2_q16((uint64)sum) - (59 << 16);

  return 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
//One radix-2 DIT stage on the complex buffer [buf, end): blocks of 2 half points, butterfly j of a block pairs a = blk + j
//with b = a + half under W = tw[j * twstep] = c - i s: t = b W, a' = a + t, b' = a - t (fx_mac30 rounding).
//hoff = 8 half bytes (a to b), twstep in bytes.

#if defined(__arm__)
void fx_stage_arm(int32 *buf, int32 *end, uint32 hoff, uint32 twstep, const int32 *tw);

__asm__(
"  .text\n"
"  .arm\n"
"  .align 2\n"
"  .global fx_stage_arm\n"
"  .type fx_stage_arm, %function\n"
"fx_stage_arm:\n"
"  push   {r4-r11, lr}\n"
"  ldr    r4, [sp, #36]\n"            /* tw (fifth argument, above the 9 saved registers) */
"  sub    sp, sp, #12\n"
"  str    r1, [sp, #0]\n"             /* end */
"  str    r4, [sp, #4]\n"             /* twiddle table */
"  cmp    r0, r1\n"
"  bhs    3f\n"
"1:\n"                                /* block: a = r0, b = a + r2, a runs to lr = blk + hoff */
"  str    r0, [sp, #8]\n"
"  add    lr, r0, r2\n"
"  ldr    r1, [sp, #4]\n"
"2:\n"
"  ldrd   r4, r5, [r1]\n"             /* c, s */
"  add    r1, r1, r3\n"
"  ldrd   r8, r9, [r0, r2]\n"         /* b = (br, bi) */
"  mov    r10, #0x20000000\n"
"  mov    r11, #0\n"
"  smlal  r10, r11, r8, r4\n"         /* br c */
"  smlal  r10, r11, r9, r5\n"         /* + bi s */
"  mov    r12, r10, lsr #30\n"
"  orr    r12, r12, r11, lsl #2\n"    /* tr */
"  mov    r10, #0x20000000\n"
"  mov    r11, #0\n"
"  smlal  r10, r11, r9, r4\n"         /* bi c */
"  rsb    r5, r5, #0\n"
"  smlal  r10, r11, r8, r5\n"         /* - br s */
"  mov    r4, r10, lsr #30\n"
"  orr    r4, r4, r11, lsl #2\n"      /* ti */
"  ldrd   r6, r7, [r0]\n"             /* a = (ar, ai) */
"  add    r10, r6, r12\n"
"  add    r11, r7, r4\n"
"  sub    r8, r6, r12\n"
"  sub    r9, r7, r4\n"
"  strd   r10, r11, [r0]\n"
"  strd   r8, r9, [r0, r2]\n"
"  add    r0, r0, #8\n"
"  cmp    r0, lr\n"
"  blo    2b\n"
"  ldr    r0, [sp, #8]\n"
"  add    r0, r0, r2, lsl #1\n"
"  ldr    r1, [sp, #0]\n"
"  cmp    r0, r1\n"
"  blo    1b\n"
"3:\n"
"  add    sp, sp, #12\n"
"  pop    {r4-r11, pc}\n"
"  .size fx_stage_arm, . - fx_stage_arm\n"
);
#endif

//The butterflies of one twiddle across a stage (early stages, where the blocks outnumber the twiddles): for a = start;
//a < end; a += 2 hoff, b = a + hoff, with W = c - i s held in registers. Two LDRD, two STRD per butterfly.
#if defined(__arm__)
void fx_bfly_arm(int32 *a, int32 *end, uint32 hoff, int32 c, int32 s);

__asm__(
"  .text\n"
"  .arm\n"
"  .align 2\n"
"  .global fx_bfly_arm\n"
"  .type fx_bfly_arm, %function\n"
"fx_bfly_arm:\n"
"  push   {r4-r11, lr}\n"
"  ldr    r5, [sp, #36]\n"            /* s (fifth argument) */
"  mov    r4, r3\n"                   /* c */
"  rsb    r3, r5, #0\n"               /* -s */
"  cmp    r0, r1\n"
"  bhs    2f\n"
"1:\n"
"  ldrd   r8, r9, [r0, r2]\n"         /* b = (br, bi) */
"  mov    r10, #0x20000000\n"
"  mov    r11, #0\n"
"  smlal  r10, r11, r8, r4\n"         /* br c */
"  smlal  r10, r11, r9, r5\n"         /* + bi s */
"  mov    r12, r10, lsr #30\n"
"  orr    r12, r12, r11, lsl #2\n"    /* tr */
"  mov    r10, #0x20000000\n"
"  mov    r11, #0\n"
"  smlal  r10, r11, r9, r4\n"         /* bi c */
"  smlal  r10, r11, r8, r3\n"         /* - br s */
"  mov    lr, r10, lsr #30\n"
"  orr    lr, lr, r11, lsl #2\n"      /* ti */
"  ldrd   r6, r7, [r0]\n"             /* a = (ar, ai) */
"  add    r10, r6, r12\n"
"  add    r11, r7, lr\n"
"  sub    r8, r6, r12\n"
"  sub    r9, r7, lr\n"
"  strd   r10, r11, [r0]\n"
"  strd   r8, r9, [r0, r2]\n"
"  add    r0, r0, r2, lsl #1\n"
"  cmp    r0, r1\n"
"  blo    1b\n"
"2:\n"
"  pop    {r4-r11, pc}\n"
"  .size fx_bfly_arm, . - fx_bfly_arm\n"
);
#endif

static void fx_bfly(int32 *a, int32 *end, uint32 half, int32 c, int32 s)
{
#if defined(__arm__)
  fx_bfly_arm(a, end, 8 * half, c, s);
#else
  for(; a < end; a += 4 * half)
  {
    int32 ar, ai, br, bi, tr, ti;

    fx_ld(a + 2 * half, &br, &bi);
    tr = fx_mac30(br, c, bi, s);
    ti = fx_mac30(bi, c, br, -s);
    fx_ld(a, &ar, &ai);
    fx_st(a, ar + tr, ai + ti);
    fx_st(a + 2 * half, ar - tr, ai - ti);
  }
#endif
}

static void fx_stage(int32 *buf, int32 *end, uint32 half, uint32 twidx, const int32 *tw)
{
#if defined(__arm__)
  fx_stage_arm(buf, end, 8 * half, 8 * twidx, tw);
#else
  int32 *blk, *a;

  for(blk = buf; blk < end; blk += 4 * half)
  {
    const int32 *w = tw;

    for(a = blk; a < blk + 2 * half; a += 2, w += 2 * twidx)
    {
      int32 ar, ai, br, bi, tr, ti, c = w[0], s = w[1];

      fx_ld(a + 2 * half, &br, &bi);
      tr = fx_mac30(br, c, bi, s);
      ti = fx_mac30(bi, c, br, -s);
      fx_ld(a, &ar, &ai);
      fx_st(a, ar + tr, ai + ti);
      fx_st(a + 2 * half, ar - tr, ai - ti);
    }
  }
#endif
}

//Stages 2..log2m of the M = N/2 point complex FFT (stage 1 is done by the load). Stage s has M / 2^s blocks of
//half = 2^(s-1) butterflies; twiddle j of a block is W_M^(j M / 2^s) = W_N^(j M / 2^(s-1)) (table index). The kernel
//per twiddle costs 4 accesses per butterfly plus a call per twiddle, the kernel per block 5 plus a little per
//block, so early stages (more blocks than twiddles) take the first.
static void fx_fft_stages(FX_FFT *p)
{
  int     log2m = p->log2n - 1;
  uint32  M = 1u << log2m;
  int32  *buf = p->buf, *end = buf + 2 * M;
  const int32 *tw = p->win + (2u << log2m);
  int     stage;

  for(stage = 2; stage <= log2m; stage++)
  {
    uint32 half = 1u << (stage - 1), blocks = M >> stage, twidx = M >> (stage - 1), j;

    if(blocks > half)
    {
      for(j = 0; j < half; j++)
        fx_bfly(buf + 2 * j, end, half, tw[2 * j * twidx], tw[2 * j * twidx + 1]);
    }
    else
      fx_stage(buf, end, half, twidx, tw);
  }
}

//Real split step: Z_k (k < M) of z[n] = x[2n] + i x[2n+1] into 2 X_k for k = 0..M (bin M at buf[N], buf[N + 1]).
//Pairs k, M - k for k = 1..M/2 - 1 with W_N^k = c - i s from the table:
//E = A + conj(B), O' = -i (A - conj(B)), T = W O', X_k = E + T, X_(M-k) = conj(E - T).
#if defined(__arm__)
void fx_split_arm(int32 *A, int32 *B, const int32 *tw, uint32 count);

__asm__(
"  .text\n"
"  .arm\n"
"  .align 2\n"
"  .global fx_split_arm\n"
"  .type fx_split_arm, %function\n"
"fx_split_arm:\n"
"  push   {r4-r11, lr}\n"
"1:\n"
"  ldrd   r4, r5, [r0]\n"             /* A = (ar, ai) */
"  ldrd   r6, r7, [r1]\n"             /* B = (br, bi) */
"  add    r8, r4, r6\n"               /* er = ar + br */
"  sub    r9, r5, r7\n"               /* ei = ai - bi */
"  add    r5, r5, r7\n"               /* or = ai + bi */
"  sub    r4, r6, r4\n"               /* oi = br - ar */
"  ldrd   r6, r7, [r2], #8\n"         /* c, s */
"  mov    r10, #0x20000000\n"
"  mov    r11, #0\n"
"  smlal  r10, r11, r5, r6\n"         /* or c */
"  smlal  r10, r11, r4, r7\n"         /* + oi s */
"  mov    r12, r10, lsr #30\n"
"  orr    r12, r12, r11, lsl #2\n"    /* tr */
"  mov    r10, #0x20000000\n"
"  mov    r11, #0\n"
"  smlal  r10, r11, r4, r6\n"         /* oi c */
"  rsb    r7, r7, #0\n"
"  smlal  r10, r11, r5, r7\n"         /* - or s */
"  mov    r5, r10, lsr #30\n"
"  orr    r5, r5, r11, lsl #2\n"      /* ti */
"  add    r6, r8, r12\n"
"  add    r7, r9, r5\n"
"  strd   r6, r7, [r0], #8\n"         /* X_k = E + T */
"  sub    r10, r8, r12\n"
"  sub    r11, r5, r9\n"
"  strd   r10, r11, [r1], #-8\n"      /* X_(M-k) = conj(E - T) */
"  subs   r3, r3, #1\n"
"  bne    1b\n"
"  pop    {r4-r11, pc}\n"
"  .size fx_split_arm, . - fx_split_arm\n"
);
#endif

static void fx_fft_split(FX_FFT *p)
{
  uint32 M = 1u << (p->log2n - 1);
  int32 *buf = p->buf;
  const int32 *tw = p->win + 2 * M;
  int32  zr, zi;

  fx_ld(buf, &zr, &zi);
  fx_st(buf, 2 * (zr + zi), 0);
  fx_st(buf + 2 * M, 2 * (zr - zi), 0);

#if defined(__arm__)
  fx_split_arm(buf + 2, buf + 2 * (M - 1), tw + 2, M / 2 - 1);
#else
  {
    uint32 k;

    for(k = 1; k < M / 2; k++)
    {
      int32 *A = buf + 2 * k;
      int32 *B = buf + 2 * (M - k);
      int32 ar, ai, br, bi;
      int32 er, ei, orr, oi, c = tw[2 * k], s = tw[2 * k + 1], tr, ti;

      fx_ld(A, &ar, &ai);
      fx_ld(B, &br, &bi);
      er  = ar + br;  ei = ai - bi;
      orr = ai + bi;  oi = br - ar;
      tr  = fx_mac30(orr, c, oi, s);
      ti  = fx_mac30(oi, c, orr, -s);

      fx_st(A, er + tr, ei + ti);
      fx_st(B, er - tr, ti - ei);
    }
  }
#endif

  //k = M/2: X = conj(Z)
  fx_ld(buf + M, &zr, &zi);
  fx_st(buf + M, 2 * zr, -2 * zi);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Windowed, scaled load in bit-reversed order fused with stage 1: output positions 2q, 2q+1 = z[Q] +- z[Q + M/2],
//Q = q reversed over log2m - 1 bits, z[n] = w[2n] x[2n] + i w[2n+1] x[2n+1].
//Scale: N * max|x| <= 2^29 with max|x| < 2^b, shift s = 29 - log2n - b. Bounds: every partial DFT component
//<= N * max <= 2^29, twiddle products <= sqrt(2) * 2^29, split step outputs (2 X_k) <= 2^30, |X|^2 < 2^61.
//Rounding: (v w + 2^(k-1)) >> k is computed as (((v w) >> (k-1)) + 1) >> 1, which is the same integer.

static int fx_bits(uint32 v)
{
  return 32 - fx_clz32(v);
}

//u8 samples: |x - offset| <= 255 < 2^8, so s = 21 - log2n, and the window drops to Q22 (w >> 8, error below 2^-22)
//so that v w fits 32 bits; the shift is then k = 22 - s = log2n + 1.
#if defined(__arm__)
void fx_load_u8_arm(const uint8 *x, const int32 *win, int32 *out, uint32 M, int32 offset, int32 k1);

__asm__(
"  .text\n"
"  .arm\n"
"  .align 2\n"
"  .global fx_load_u8_arm\n"
"  .type fx_load_u8_arm, %function\n"
"fx_load_u8_arm:\n"
"  push   {r4-r11, lr}\n"
"  ldr    r7, [sp, #36]\n"            /* offset */
"  ldr    r6, [sp, #40]\n"            /* k1 = k - 1 */
"  mov    r5, r3\n"                   /* M: byte offset of x[2 (Q + M/2)] from x[2Q] */
"  mov    r4, r3, lsr #1\n"           /* count M/2 */
"  mov    r3, #0\n"                   /* Q */
"1:\n"
"  add    r8, r0, r3, lsl #1\n"
"  ldrh   r12, [r8]\n"                /* x[2Q], x[2Q+1] */
"  ldrh   lr, [r8, r5]\n"             /* x[2Q+M], x[2Q+M+1] */
"  add    r8, r1, r3, lsl #3\n"
"  ldrd   r10, r11, [r8]\n"           /* w[2Q], w[2Q+1] */
"  and    r9, r12, #255\n"
"  sub    r9, r9, r7\n"
"  mov    r10, r10, asr #8\n"
"  mul    r10, r9, r10\n"
"  mov    r10, r10, asr r6\n"
"  add    r10, r10, #1\n"
"  mov    r10, r10, asr #1\n"         /* z0 re */
"  mov    r9, r12, lsr #8\n"
"  sub    r9, r9, r7\n"
"  mov    r11, r11, asr #8\n"
"  mul    r11, r9, r11\n"
"  mov    r11, r11, asr r6\n"
"  add    r11, r11, #1\n"
"  mov    r11, r11, asr #1\n"         /* z0 im */
"  add    r12, r8, r5, lsl #2\n"
"  ldrd   r8, r9, [r12]\n"            /* w[2Q+M], w[2Q+M+1] */
"  and    r12, lr, #255\n"
"  sub    r12, r12, r7\n"
"  mov    r8, r8, asr #8\n"
"  mul    r8, r12, r8\n"
"  mov    r8, r8, asr r6\n"
"  add    r8, r8, #1\n"
"  mov    r8, r8, asr #1\n"           /* z1 re */
"  mov    lr, lr, lsr #8\n"
"  sub    lr, lr, r7\n"
"  mov    r9, r9, asr #8\n"
"  mul    r9, lr, r9\n"
"  mov    r9, r9, asr r6\n"
"  add    r9, r9, #1\n"
"  mov    r9, r9, asr #1\n"           /* z1 im */
"  add    r12, r10, r8\n"
"  add    lr, r11, r9\n"
"  sub    r10, r10, r8\n"
"  sub    r11, r11, r9\n"
"  mov    r8, r12\n"
"  mov    r9, lr\n"
"  stmia  r2!, {r8, r9, r10, r11}\n"  /* z0 + z1, z0 - z1 */
"  mov    r12, r5, lsr #2\n"          /* top bit of the log2m - 1 bit index */
"2:\n"
"  tst    r3, r12\n"
"  beq    3f\n"
"  eor    r3, r3, r12\n"
"  movs   r12, r12, lsr #1\n"
"  bne    2b\n"
"3:\n"
"  orr    r3, r3, r12\n"
"  subs   r4, r4, #1\n"
"  bne    1b\n"
"  pop    {r4-r11, pc}\n"
"  .size fx_load_u8_arm, . - fx_load_u8_arm\n"
);
#endif

static void fx_load_u8(FX_FFT *p, const uint8 *x, int32 offset)
{
  uint32 M = 1u << (p->log2n - 1);
  int32  k1 = p->log2n;                 //k - 1, k = 22 - shift = log2n + 1

#if defined(__arm__)
  fx_load_u8_arm(x, p->win, p->buf, M, offset, k1);
#else
  const int32 *win = p->win;
  int32  *o = p->buf;
  uint32  q, Q = 0, bit;

  for(q = 0; q < M / 2; q++, o += 4)
  {
    uint32 x0 = *(const fx_u16 *)(x + 2 * Q), x1 = *(const fx_u16 *)(x + 2 * Q + M);
    const int32 *w0 = win + 2 * Q, *w1 = w0 + M;
    int32 z0r = ((((int32)(x0 & 0xFF) - offset) * (w0[0] >> 8)) >> k1) + 1;
    int32 z0i = ((((int32)(x0 >> 8) - offset) * (w0[1] >> 8)) >> k1) + 1;
    int32 z1r = ((((int32)(x1 & 0xFF) - offset) * (w1[0] >> 8)) >> k1) + 1;
    int32 z1i = ((((int32)(x1 >> 8) - offset) * (w1[1] >> 8)) >> k1) + 1;

    z0r >>= 1; z0i >>= 1; z1r >>= 1; z1i >>= 1;
    fx_st(o,     z0r + z1r, z0i + z1i);
    fx_st(o + 2, z0r - z1r, z0i - z1i);

    bit = M >> 2;
    while(Q & bit) { Q ^= bit; bit >>= 1; }
    Q |= bit;
  }
#endif
}

//i32 samples (C only): 64-bit products with the Q30 window
static void fx_load_i32(FX_FFT *p, const int32 *x, int32 offset)
{
  uint32 M = 1u << (p->log2n - 1), q, Q = 0, bit;
  int    k1 = 30 - p->shift - 1;
  const int32 *win = p->win;
  int32  *o = p->buf;

#define FX_I32Z(n, w) ((int32)((((int64)(x[n] - offset) * (w)) >> k1) + 1) >> 1)
  for(q = 0; q < M / 2; q++, o += 4)
  {
    uint32 n0 = 2 * Q, n1 = n0 + M;
    int32  z0r = FX_I32Z(n0, win[n0]), z0i = FX_I32Z(n0 + 1, win[n0 + 1]);
    int32  z1r = FX_I32Z(n1, win[n1]), z1i = FX_I32Z(n1 + 1, win[n1 + 1]);

    fx_st(o,     z0r + z1r, z0i + z1i);
    fx_st(o + 2, z0r - z1r, z0i - z1i);

    bit = M >> 2;
    while(Q & bit) { Q ^= bit; bit >>= 1; }
    Q |= bit;
  }
#undef FX_I32Z
}

void fx_fft_run_u8(FX_FFT *p, const uint8 *x, int32 offset)
{
  //|x - offset| <= 255 keeps the scaling bound (fx_fft.h: offset 0..255)
  if(offset < 0) offset = 0;
  if(offset > 255) offset = 255;

  p->shift = 21 - p->log2n;
  fx_load_u8(p, x, offset);
  fx_fft_stages(p);
  fx_fft_split(p);
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
  fx_load_i32(p, x, offset);
  fx_fft_stages(p);
  fx_fft_split(p);
}

//----------------------------------------------------------------------------------------------------------------------------------

void fx_fft_cdb(const FX_FFT *p, int first, int count, int32 offset, int16 *out)
{
  uint32 M = 1u << (p->log2n - 1);
  int32  base = p->norm_q16 + p->shift * 131072;   //2 * shift octaves
  const int32 *b = p->buf + 2 * first;
  int    k;

  for(k = first; k < first + count; k++, b += 2)
  {
    int32  re, im, cdb;
    uint64 pw;

    fx_ld(b, &re, &im);
    pw = (uint64)((int64)re * re) + (uint64)((int64)im * im);

    if(pw == 0)
    {
      *out++ = FX_CDB_FLOOR;
      continue;
    }

    //DC and Nyquist carry their full value in one bin (no conjugate partner): one octave of power less
    cdb = fx_cdb_from_q16(fx_log2_q16(pw) - base - (((k == 0) || ((uint32)k == M)) ? 65536 : 0)) + offset;

    //FX_CDB_FLOOR stays reserved for an empty bin
    if(cdb <= FX_CDB_FLOOR)
      cdb = FX_CDB_FLOOR + 1;
    else if(cdb > 32767)
      cdb = 32767;

    *out++ = (int16)cdb;
  }
}
