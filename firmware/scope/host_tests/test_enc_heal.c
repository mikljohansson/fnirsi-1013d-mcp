//----------------------------------------------------------------------------------------------------------------------------------
// test_enc_heal.c - the full-rate capture check (enc_heal.c, fw 0.2.61; per capture with bounded re-arm since 0.2.62):
// which bitstreams get which check, the residual on clean, signal and garbled captures, both checks against a mocked
// FPGA (0x33 good / bad, ring data clean / garbled), the re-arm bound and the event fields (bench 2026-10-19: CH1 core B
// 15-29 codes in 27 of 60 rate changes on 0x1535; CH2 core A 14-30 codes in about 40 % of arms on 0x1537).
//----------------------------------------------------------------------------------------------------------------------------------

#include "test_util.h"
#include "enc_heal.h"

#include <math.h>

#define N 500                       //pairs per residual test capture (0.2.61's check size)

static uint32_t rng = 777;

static uint32_t rnd(void)
{
  rng = rng * 1664525u + 1013904223u;
  return rng >> 8;
}

static double gauss(void)
{
  double s = 0;
  int    i;

  for(i=0;i<12;i++)
    s += (rnd() & 0xFFFF) / 65536.0;

  return s - 6.0;
}

static uint8_t code(double v)
{
  long c = lround(v);

  return (uint8_t)((c < 0) ? 0 : ((c > 255) ? 255 : c));
}

//One channel's capture in the trace layout (buf[2i] core B, buf[2i + 1] core A): a sine of amp codes at f_hz sampled
//at 200 MSa/s (B half a word before A), core A at 131.5 and B at 128.0, noise sd codes; garbage: core B uniform
static void fill(uint8_t *buf, uint32_t n, double amp, double f_hz, double sd, int garbage)
{
  uint32_t i;
  double   w = 2 * 3.14159265358979 * f_hz * 5e-9;

  for(i=0;i<n;i++)
  {
    buf[2 * i]     = garbage ? (uint8_t)(rnd() & 0xFF) : code(128.0 + amp * sin(w * (2 * i)) + sd * gauss());
    buf[2 * i + 1] = code(131.5 + amp * sin(w * (2 * i + 1)) + sd * gauss());
  }
}

static void test_modes(void)
{
  T_EQ(eh_affected(0x1432), 0);
  T_EQ(eh_affected(0x1531), 0);
  T_EQ(eh_affected(0x1532), 1);
  T_EQ(eh_affected(0x1535), 1);
  T_EQ(eh_affected(0x1536), 1);
  T_EQ(eh_affected(0x1537), 0);
  T_EQ(eh_affected(0x1538), 0);

  T_EQ(eh_mode(0x1432), EH_MODE_NONE);
  T_EQ(eh_mode(0x1531), EH_MODE_NONE);
  T_EQ(eh_mode(0x1532), EH_MODE_DATA);
  T_EQ(eh_mode(0x1535), EH_MODE_DATA);
  T_EQ(eh_mode(0x1537), EH_MODE_DATA);
  T_EQ(eh_mode(0x1538), EH_MODE_FLAG);
  T_EQ(eh_mode(0x153F), EH_MODE_FLAG);
  T_EQ(eh_mode(0x1632), EH_MODE_NONE);
  T_EQ(eh_mode(0x1732), EH_MODE_NONE);

  T_EQ(eh_order_differs(0x1536), 0);
  T_EQ(eh_order_differs(0x1537), 1);
  T_EQ(eh_order_differs(0x1538), 0);

  //0x33 bits 3:2 = 01: convention 1 (B first)
  T_EQ(eh_conv_ok(EH_33_GOOD), 1);
  T_EQ(eh_conv_ok(0x77), 1);
  T_EQ(eh_conv_ok(0xF2), 0);
  T_EQ(eh_conv_ok(0xFA), 0);
  T_EQ(eh_conv_ok(0xFE), 0);

  //fw 0.2.72: a rate / filter write after the sampling reset sends a FLAG capture to the data check; the others keep theirs
  T_EQ(eh_capture_mode(EH_MODE_FLAG, 0), EH_MODE_FLAG);
  T_EQ(eh_capture_mode(EH_MODE_FLAG, 1), EH_MODE_DATA);
  T_EQ(eh_capture_mode(EH_MODE_DATA, 1), EH_MODE_DATA);
  T_EQ(eh_capture_mode(EH_MODE_NONE, 1), EH_MODE_NONE);
}

static void test_residual(void)
{
  uint8_t  buf[2 * N];
  uint32_t r;

  //Open input: 1 code per core, the cores 3.5 codes apart: about sqrt(1 + 1/2) = 1.2 codes
  fill(buf, N, 0, 0, 1.0, 0);
  r = eh_residual16(buf, N);
  T_TRUE(r >= 16 && r <= 24);

  //Signals cancel: 100-code sines at 1 MHz and 5 MHz stay near the noise, 10 MHz under the limit
  fill(buf, N, 100, 1e6, 1.0, 0);
  T_TRUE(eh_residual16(buf, N) <= 24);
  fill(buf, N, 100, 5e6, 1.0, 0);
  T_TRUE(eh_residual16(buf, N) <= 40);
  fill(buf, N, 100, 10e6, 1.0, 0);
  T_TRUE(eh_residual16(buf, N) < EH_LIMIT16);

  //A garbled core: far above the limit, with or without a signal
  fill(buf, N, 0, 0, 1.0, 1);
  T_TRUE(eh_residual16(buf, N) > 4 * EH_LIMIT16);
  fill(buf, N, 100, 1e6, 1.0, 1);
  T_TRUE(eh_residual16(buf, N) > 2 * EH_LIMIT16);

  //The per-capture check's size (EH_CHECK_PAIRS) separates them as well
  fill(buf, EH_CHECK_PAIRS, 0, 0, 1.0, 0);
  T_TRUE(eh_residual16(buf, EH_CHECK_PAIRS) <= 24);
  fill(buf, EH_CHECK_PAIRS, 0, 0, 1.0, 1);
  T_TRUE(eh_residual16(buf, EH_CHECK_PAIRS) > 4 * EH_LIMIT16);

  //A flat capture: 0
  memset(buf, 128, sizeof(buf));
  T_EQ(eh_residual16(buf, N), 0);
}

//Mock FPGA for eh_check: four cores of a ring, the 0x33 byte, and what was read
typedef struct
{
  uint8_t  ring[4][1024];
  uint8_t  s33;
  uint32_t status_reads, reads, last_start, last_count;
} fpga_t;

static uint8_t m_status(void *ctx)
{
  fpga_t *f = (fpga_t *)ctx;

  f->status_reads++;
  return f->s33;
}

static void m_read(void *ctx, uint8_t core, uint32_t start, uint32_t count, uint8_t *dst, uint32_t stride)
{
  fpga_t  *f = (fpga_t *)ctx;
  uint32_t i;

  f->reads++;
  f->last_start = start;
  f->last_count = count;

  for(i=0;i<count;i++)
    dst[i * stride] = f->ring[core][(start + i) % 1024];
}

//Clean ring (noise 1 code, an 80-code sine on CH1), garbage on one core (-1: none)
static void fpga_fill(fpga_t *f, int bad_core)
{
  uint8_t  buf[2 * 1024 / 2];
  uint32_t ch, i, k;

  for(ch=0;ch<2;ch++)
    for(k=0;k<2;k++)
    {
      fill(buf, 256, ch ? 0 : 80, 390625.0, 1.0, 0);   //one period per 256 words: the ring repeats seamlessly

      for(i=0;i<1024;i++)
        f->ring[2 * ch + (k ? 0 : 1)][i] = buf[2 * (i % 256) + k];       //k 0: core B (odd FPGA core), 1: core A
    }

  if(bad_core >= 0)
    for(i=0;i<1024;i++)
      f->ring[bad_core][i] = (uint8_t)(rnd() & 0xFF);
}

static void test_check(void)
{
  static fpga_t f;
  eh_io    io = { &f, m_status, m_read };
  uint32_t v;
  int      c;

  //FLAG (0x1538+): only 0x33 is read, bit 0 decides
  memset(&f, 0, sizeof(f));
  f.s33 = EH_33_GOOD;
  T_EQ(eh_check(&io, EH_MODE_FLAG, 100, 1500, &v), 0);
  T_EQ(v, EH_33_GOOD); T_EQ(f.status_reads, 1); T_EQ(f.reads, 0);
  f.s33 = 0x77;                                                 //CH2 A wrong, check failed
  T_EQ(eh_check(&io, EH_MODE_FLAG, 100, 1500, &v), 1);
  T_EQ(v, 0x77);
  f.s33 = 0xF5;                                                 //failed, the last pin check right (an async write)
  T_EQ(eh_check(&io, EH_MODE_FLAG, 100, 1500, &v), 1);

  //DATA (0x1532..0x1537): both channels, EH_CHECK_PAIRS words of each core from the record start, no 0x33
  memset(&f, 0, sizeof(f));
  fpga_fill(&f, -1);
  T_EQ(eh_check(&io, EH_MODE_DATA, 700, 1500, &v), 0);
  T_TRUE(v <= 24);
  T_EQ(f.status_reads, 0); T_EQ(f.reads, 4); T_EQ(f.last_start, 700); T_EQ(f.last_count, EH_CHECK_PAIRS);

  //Any one garbled core fails it: each of the four
  for(c=0;c<4;c++)
  {
    fpga_fill(&f, c);
    T_EQ(eh_check(&io, EH_MODE_DATA, 0, 1500, &v), 1);
    T_TRUE(v > 4 * EH_LIMIT16);
  }

  //A short record reads only its words
  fpga_fill(&f, -1);
  T_EQ(eh_check(&io, EH_MODE_DATA, 3, 40, &v), 0);
  T_EQ(f.last_count, 40);
}

static void test_rearm(void)
{
  eh_cap  c;
  eh_note n;
  int     i;

  //Good: accepted, nothing to log or count but the check
  eh_cap_reset(&c);
  T_EQ(eh_cap_step(&c, 0, 20, &n), EH_ACCEPT);
  T_EQ(n.bad, 0); T_EQ(n.first, 0); T_EQ(n.done, 0);

  //Bad twice, then good: two re-arms, one first failure, recovered with the first value
  T_EQ(eh_cap_step(&c, 1, 500, &n), EH_REARM);
  T_EQ(n.bad, 1); T_EQ(n.first, 1); T_EQ(n.done, 0);
  T_EQ(eh_cap_step(&c, 1, 600, &n), EH_REARM);
  T_EQ(n.first, 0); T_EQ(n.done, 0);
  T_EQ(eh_cap_step(&c, 0, 20, &n), EH_ACCEPT);
  T_EQ(n.bad, 0); T_EQ(n.done, 1); T_EQ(n.ok, 1); T_EQ(n.tries, 2); T_EQ(n.value, 500);
  T_EQ(c.tries, 0);

  //Never good: EH_MAX_TRIES re-arms, then accepted bad
  for(i=0;i<EH_MAX_TRIES;i++)
    T_EQ(eh_cap_step(&c, 1, 0x77, &n), EH_REARM);
  T_EQ(eh_cap_step(&c, 1, 0x77, &n), EH_ACCEPT);
  T_EQ(n.done, 1); T_EQ(n.ok, 0); T_EQ(n.tries, EH_MAX_TRIES); T_EQ(n.value, 0x77);

  //The next capture starts afresh
  T_EQ(eh_cap_step(&c, 1, 0x77, &n), EH_REARM);
  T_EQ(n.first, 1);

  //0x1537 statistics: 40 % bad per arm, independent: almost every capture recovers within the bound, about 0.67
  //re-arms per capture
  {
    uint32_t caps = 0, rearms = 0, lost = 0;

    eh_cap_reset(&c);

    while(caps < 10000)
    {
      if(eh_cap_step(&c, (rnd() % 100) < 40, 300, &n) == EH_REARM)
        rearms++;
      else
      {
        caps++;
        lost += n.done && !n.ok;
      }
    }

    T_EQ(lost, 0);
    T_TRUE(rearms > 6000 && rearms < 7400);
  }

  //Event fields: reason in bits 6:5, re-arms in 4:0 (saturated), bit 7 not recovered; caller in b's top nibble
  T_EQ(eh_event_a(EH_WHY_FLAG, 3, 1), 0x43);
  T_EQ(eh_event_a(EH_WHY_DATA, 16, 0), 0xB0);
  T_EQ(eh_event_a(EH_WHY_CONV, 0, 0), 0xE0);
  T_EQ(eh_event_a(EH_WHY_DATA, 40, 1), 0x3F);
  T_EQ(eh_event_b(1, 0x77), 0x1077);
  T_EQ(eh_event_b(2, 70000), 0x2FFF);
}

int main(void)
{
  test_modes();
  test_residual();
  test_check();
  test_rearm();
  return t_report("test_enc_heal");
}
