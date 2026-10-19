//----------------------------------------------------------------------------------------------------------------------------------
// test_enc_heal.c - the full-rate capture check and retry (enc_heal.c, fw 0.2.61): which bitstreams re-write 0x0D, the
// residual on clean, signal and garbled captures, and the retry loop against a mocked capture / retry that returns bad
// data and then good (bench 2026-10-19: CH1 core B 15-29 codes in 27 of 60 rate changes, 1.1 otherwise).
//----------------------------------------------------------------------------------------------------------------------------------

#include "test_util.h"
#include "enc_heal.h"

#include <math.h>

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
static void fill(uint8_t *buf, double amp, double f_hz, double sd, int garbage)
{
  uint32_t i;
  double   w = 2 * 3.14159265358979 * f_hz * 5e-9;

  for(i=0;i<EH_SAMPLES;i++)
  {
    buf[2 * i]     = garbage ? (uint8_t)(rnd() & 0xFF) : code(128.0 + amp * sin(w * (2 * i)) + sd * gauss());
    buf[2 * i + 1] = code(131.5 + amp * sin(w * (2 * i + 1)) + sd * gauss());
  }
}

//Mock target: capture returns garbage on CH1 until `heal_after` re-writes were made (-1: never)
typedef struct { int rewrites, captures, heal_after, bad_ch; } mock_t;

static void m_retry(void *ctx)
{
  ((mock_t *)ctx)->rewrites++;
}

static void m_capture(void *ctx, uint32_t res16[2])
{
  mock_t  *m = (mock_t *)ctx;
  uint8_t  buf[2 * EH_SAMPLES];
  uint32_t ch;
  int      bad = (m->heal_after < 0) || (m->rewrites < m->heal_after);

  m->captures++;

  for(ch=0;ch<2;ch++)
  {
    fill(buf, 0, 0, 1.0, bad && ((int)ch == m->bad_ch));
    res16[ch] = eh_residual16(buf, EH_SAMPLES);
  }
}

static void test_affected(void)
{
  T_EQ(eh_affected(0x1432), 0);
  T_EQ(eh_affected(0x1531), 0);
  T_EQ(eh_affected(0x1532), 1);
  T_EQ(eh_affected(0x1535), 1);
  T_EQ(eh_affected(0x1536), 1);
  T_EQ(eh_affected(0x1537), 0);
}

static void test_residual(void)
{
  uint8_t  buf[2 * EH_SAMPLES];
  uint32_t r;

  //Open input: 1 code per core, the cores 3.5 codes apart: about sqrt(1 + 1/2) = 1.2 codes
  fill(buf, 0, 0, 1.0, 0);
  r = eh_residual16(buf, EH_SAMPLES);
  T_TRUE(r >= 16 && r <= 24);

  //Signals cancel: 100-code sines at 1 MHz and 5 MHz stay near the noise, 10 MHz under the limit
  fill(buf, 100, 1e6, 1.0, 0);
  T_TRUE(eh_residual16(buf, EH_SAMPLES) <= 24);
  fill(buf, 100, 5e6, 1.0, 0);
  T_TRUE(eh_residual16(buf, EH_SAMPLES) <= 40);
  fill(buf, 100, 10e6, 1.0, 0);
  T_TRUE(eh_residual16(buf, EH_SAMPLES) < EH_LIMIT16);

  //A garbled core: far above the limit, with or without a signal
  fill(buf, 0, 0, 1.0, 1);
  T_TRUE(eh_residual16(buf, EH_SAMPLES) > 4 * EH_LIMIT16);
  fill(buf, 100, 1e6, 1.0, 1);
  T_TRUE(eh_residual16(buf, EH_SAMPLES) > 2 * EH_LIMIT16);

  //A flat capture: 0
  memset(buf, 128, sizeof(buf));
  T_EQ(eh_residual16(buf, EH_SAMPLES), 0);
}

static void test_heal(void)
{
  eh_ops    o = { 0, m_retry, m_capture };
  eh_result r;
  mock_t    m;

  //Good from the start: one capture, nothing written, not counted as bad
  memset(&m, 0, sizeof(m)); m.heal_after = 0; m.bad_ch = 0; o.ctx = &m;
  T_EQ(eh_run(&o, &r), 1);
  T_EQ(r.bad, 0); T_EQ(r.tries, 0); T_EQ(m.rewrites, 0); T_EQ(m.captures, 1);
  T_TRUE(r.first16 <= 24);

  //Bad, healed by the third re-write (CH1), checked after each
  memset(&m, 0, sizeof(m)); m.heal_after = 3; m.bad_ch = 0; o.ctx = &m;
  T_EQ(eh_run(&o, &r), 1);
  T_EQ(r.bad, 1); T_EQ(r.tries, 3); T_EQ(m.rewrites, 3); T_EQ(m.captures, 4);
  T_TRUE(r.first16 > 4 * EH_LIMIT16);
  T_TRUE(r.res16[0] <= 24 && r.res16[1] <= 24);

  //CH2 bad (as in the wizard earlier that day), healed by the first re-write
  memset(&m, 0, sizeof(m)); m.heal_after = 1; m.bad_ch = 1; o.ctx = &m;
  T_EQ(eh_run(&o, &r), 1);
  T_EQ(r.tries, 1);

  //Never heals: EH_MAX_TRIES re-writes, then failure
  memset(&m, 0, sizeof(m)); m.heal_after = -1; m.bad_ch = 0; o.ctx = &m;
  T_EQ(eh_run(&o, &r), 0);
  T_EQ(r.bad, 1); T_EQ(r.ok, 0); T_EQ(r.tries, EH_MAX_TRIES); T_EQ(m.captures, EH_MAX_TRIES + 1);
  T_TRUE(r.res16[0] > EH_LIMIT16);
}

int main(void)
{
  test_affected();
  test_residual();
  test_heal();
  return t_report("test_enc_heal");
}
