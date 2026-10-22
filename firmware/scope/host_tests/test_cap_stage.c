//----------------------------------------------------------------------------------------------------------------------------------
// test_cap_stage.c - FPGA 0x153A capture stage training against a mock FPGA that garbles chosen stages, the self-check,
// the texts and the sector 708 trailer (cap_stage.c, fw 0.2.79)
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include "test_util.h"
#include "cap_stage.h"
#include "adc_cal.h"

//Mock FPGA: per core the stages (bit s) that sit in the data transition, with the own-encode and the A-encode timing.
//A dirty core counts about every word of the capture, a clean one 0; a quiet input moves no core by itself. fw 0.2.81:
//a stage at the edge of the transition (edge_stage) garbles only while the input is parked between edge_lo16 and
//edge_hi16 (1/16 code) and then only in edge_pct % of the captures (deterministic: every capture adds edge_pct to an
//accumulator, a capture garbles when it reaches 100), as CH1 A4 on the owner's unit (bench 2026-10-20: codes 127..135).
//
//fw-capmetric, the data the residual reads (fpga/al3/README.md section 18, sweep 2026-10-22 11:23): a parked input of
//0.5 code rms noise (code16 + a uniform 0..15, in whole codes: at 8k - 0.5 half the samples read 8k - 1, half 8k).
//Class (a), a capture error next to the clean stage (mix, bit s): a sample at a carry's upper code 8k reads the bitwise
//mix of 8k and 8k - 1 with the ones winning in a few of the bits, 8k | ((8k - 1) & 0x0E) (0x60 -> 0x6E, 0x40 -> 0x4E),
//in mix_pct % of those samples; it jumps under 32 codes, so the 0x36 count stays 0. Class (b), the ADC's own error
//(floor_pct): at every stage a sample at 8k reads 8k + 4 in floor_pct % of those samples (0x60 -> 0x64)
typedef struct
{
  uint8_t  dirty[CS_CORES];
  uint8_t  dirty_ref[CS_CORES];
  uint8_t  edge_stage[CS_CORES];      //0 = none
  uint32_t edge_lo16[CS_CORES], edge_hi16[CS_CORES];
  uint32_t edge_pct;
  uint32_t edge_acc[CS_CORES];
  uint32_t edge_tries[CS_CORES];      //captures at the edge stage and a bad point
  uint32_t edge_hits[CS_CORES];       //of them garbled
  uint8_t  mix[CS_CORES];             //class (a) stages (bit s)
  uint32_t mix_pct, floor_pct;
  uint32_t e_fix[CS_CORES][CS_STAGES];  //fw-capmetric2: E of every capture at stage s (index s - 2) instead of the data
  uint32_t seed;
  uint32_t code16, parks, measures, resid_reads;
  uint16_t last;
  int      fail_at;                   //measure number that does not complete (0 = none)
  int      park_fail_at;              //park number that does not complete (0 = none)
} mock_fpga;

static uint32_t mock_rand(mock_fpga *m)
{
  m->seed = m->seed * 1103515245u + 12345u;
  return (m->seed >> 16) & 0x7FFF;
}

static int mock_park(void *ctx, uint32_t code16)
{
  mock_fpga *m = (mock_fpga *)ctx;

  m->parks++;
  m->code16 = code16;
  return (m->park_fail_at && (m->parks == (uint32_t)m->park_fail_at)) ? -1 : 0;
}

//One core's CS_RESID_WORDS words at stage s (mix: class (a) there) and its residual
static uint32_t mock_resid(mock_fpga *m, int mix)
{
  uint8_t  x[CS_RESID_WORDS];
  uint32_t j, c;

  for(j=0;j<CS_RESID_WORDS;j++)
  {
    c = (m->code16 + (mock_rand(m) & 15)) >> 4;

    if(c > 255)
      c = 255;

    if(!(c & 7) && c)
    {
      if(mix && ((mock_rand(m) % 100) < m->mix_pct))
        c = c | ((c - 1) & 0x0E);
      else if((mock_rand(m) % 100) < m->floor_pct)
        c = c + 4;
    }

    x[j] = (uint8_t)c;
  }

  m->resid_reads++;
  return cs_resid(x, CS_RESID_WORDS);
}

static int mock_measure(void *ctx, uint16_t v, uint8_t resid, uint16_t cnt[CS_CORES], uint32_t e[CS_CORES])
{
  mock_fpga *m = (mock_fpga *)ctx;
  uint32_t   k;

  m->measures++;
  m->last = v;

  if(m->fail_at && (m->measures == (uint32_t)m->fail_at))
    return -1;

  for(k=0;k<CS_CORES;k++)
  {
    uint8_t s     = cs_stage(v, k);
    uint8_t dirty = (v & cs_ref_bit(k)) ? m->dirty_ref[k] : m->dirty[k];

    if(s > 5)
      s = 5;

    cnt[k] = ((dirty >> s) & 1) ? 7900 : 0;

    if(m->edge_stage[k] && (s == m->edge_stage[k]) && !(v & cs_ref_bit(k)) && (m->code16 >= m->edge_lo16[k]) &&
       (m->code16 <= m->edge_hi16[k]))
    {
      m->edge_tries[k]++;
      m->edge_acc[k] += m->edge_pct;

      if(m->edge_acc[k] >= 100)
      {
        m->edge_acc[k] -= 100;
        m->edge_hits[k]++;
        cnt[k] = 60;                  //some rail samples and jumps in 8000 words
      }
    }

    if((resid & (1u << k)) && (s >= CS_STAGE_MIN) && m->e_fix[k][s - CS_STAGE_MIN])
      e[k] = m->e_fix[k][s - CS_STAGE_MIN];
    else if(resid & (1u << k))
      e[k] = mock_resid(m, !(v & cs_ref_bit(k)) && ((m->mix[k] >> s) & 1));
  }

  return 0;
}

//Stages 0 and 1 are always in the transition (README section 10); the reference timing garbles every stage unless set
static void mock_init(mock_fpga *m)
{
  uint32_t k;

  memset(m, 0, sizeof(*m));

  for(k=0;k<CS_CORES;k++)
  {
    m->dirty[k]     = 0x03;
    m->dirty_ref[k] = 0xFF;
  }
}

static void test_fields(void)
{
  T_EQ(cs_stage(CS_PO, 0), 4);
  T_EQ(cs_stage(CS_PO, 1), 4);
  T_EQ(cs_stage(CS_PO, 2), 3);
  T_EQ(cs_stage(CS_PO, 3), 4);
  T_EQ(cs_with(CS_PO, 2, 4), 0x0924);
  T_EQ(cs_with(0, 3, 7), 0x0E00);
  T_EQ(cs_ref_bit(0), 0);
  T_EQ(cs_ref_bit(1), 0x1000);
  T_EQ(cs_ref_bit(3), 0x2000);

  //cs_pick (fw 0.2.105): the middle of the widest clean run, 3 or 4 before a reserve, the lower of two equal centres
  //(no power-on tie-break); cs_tie names the two the tie pass measures
  T_EQ(cs_pick(0x2C), 3);             //fw 0.2.81, CH1 A on the owner's unit: 2, 3 clean, 4 bad, 5 clean
  T_EQ(cs_tie(0x2C), 0);
  T_EQ(cs_pick(0x34), 4);             //2 clean, 3 bad, 4, 5 clean: the run of two holding 4
  T_EQ(cs_tie(0x34), 0);
  T_EQ(cs_pick(0x14), 4);             //2 and 4 alone: 4
  T_EQ(cs_pick(0x28), 3);             //3 and 5 alone: 3
  T_EQ(cs_pick(0x3C), 3);             //2..5 clean: 3 and 4 tie, the lower (0.2.81 took the power-on 4: bench 2026-10-22)
  T_EQ(cs_tie(0x3C), 0x18);
  T_EQ(cs_pick(0x1C), 3);             //2..4 clean: 3 has 2 beside it
  T_EQ(cs_tie(0x1C), 0);
  T_EQ(cs_pick(0x38), 4);             //3..5 clean
  T_EQ(cs_tie(0x38), 0);
  T_EQ(cs_pick(0x18), 3);             //only 3, 4: a tie
  T_EQ(cs_tie(0x18), 0x18);
  T_EQ(cs_pick(0x08), 3);
  T_EQ(cs_tie(0x08), 0);
  T_EQ(cs_pick(0x30), 4);             //4, 5: 4 before the reserve
  T_EQ(cs_tie(0x30), 0);
  T_EQ(cs_pick(0x0C), 3);             //2, 3: 3 before the reserve
  T_EQ(cs_pick(0x24), 2);             //2 and 5, not 3 or 4: a tie, the lower
  T_EQ(cs_tie(0x24), 0x24);
  T_EQ(cs_pick(0x04), 2);
  T_EQ(cs_pick(0x00), CS_NONE);
  T_EQ(cs_tie(0x00), 0);
  T_EQ(cs_pick(0x03), CS_NONE);       //0 and 1 are not tried
}

static void test_band_pwm(void)
{
  //fw-capmetric: the carries 8k - 0.5, k = 8 .. 24 (63.5 .. 191.5), in 1/16 code
  T_EQ(CS_BAND_POINTS, 17);
  T_EQ(cs_band_code16(0), 1016);                  //63.5
  T_EQ(cs_band_code16(8), 2040);                  //127.5: 0x7F / 0x80
  T_EQ(cs_band_code16(CS_BAND_POINTS - 1), 3064); //191.5

  //600 reads 168 codes, 900 reads 88 codes: 80 codes per 300 counts
  T_EQ(cs_band_pwm(600, 168 * 16, 900, 88 * 16, 128 * 16), 750);
  T_EQ(cs_band_pwm(600, 168 * 16, 900, 88 * 16, 120 * 16), 780);
  T_EQ(cs_band_pwm(600, 168 * 16, 900, 88 * 16, 140 * 16), 705);
  T_EQ(cs_band_pwm(600, 168 * 16, 900, 88 * 16, 2040), 752);         //127.5: 751.875 rounded
  T_EQ(cs_band_pwm(600, 168 * 16, 900, 88 * 16, 1016), 992);         //63.5: 991.875
  T_EQ(cs_band_pwm(600, 168 * 16, 900, 88 * 16, 3064), 512);         //191.5: 511.875
  T_EQ(cs_band_pwm(600, 168 * 16, 900, 88 * 16, 200 * 16), 480);   //beyond the low point: extrapolated (negative numerator)
  T_EQ(cs_band_pwm(600, 168 * 16, 900, 88 * 16, 169 * 16), 596);   //rounded either side
  T_EQ(cs_band_pwm(600, 100 * 16, 900, 100 * 16, 128 * 16), -1);   //no response
  T_EQ(cs_band_pwm(600, 90 * 16, 900, 100 * 16, 128 * 16), -1);    //wrong sign
  T_EQ(cs_band_pwm(900, 168 * 16, 600, 88 * 16, 128 * 16), -1);
}

//fw 0.2.81: the owner's unit (bench 2026-10-20 on 0x153A): CH1 A bad at 4 only at codes 127..135 (band points 127.5
//and 135.5) in 30 % of the captures, CH1 B bad at 2, CH2 A clean, CH2 B bad at 2. The band training picks CH1 A3 B4,
//CH2 A3 B4
static void test_band(void)
{
  mock_fpga m;
  cs_io     io = { &m, CS_BAND_POINTS, CS_BAND_REPS, CS_TIE_REPS, mock_park, mock_measure };
  cs_io     one = { &m, 1, 1, 0, mock_park, mock_measure };
  cs_train  t;
  uint16_t  v;

  mock_init(&m);
  m.dirty[1]      = 0x07;
  m.dirty[3]      = 0x07;
  m.edge_stage[0] = 4;
  m.edge_lo16[0]  = 127 * 16;
  m.edge_hi16[0]  = 136 * 16;
  m.edge_pct      = 30;

  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_EQ(t.captures, CS_BAND_POINTS * 4 * CS_BAND_REPS);   //no tie: no tie pass
  T_EQ(m.parks, CS_BAND_POINTS);
  T_EQ(m.edge_tries[0], 2 * CS_BAND_REPS);               //two points
  T_TRUE(m.edge_hits[0] >= 1);
  T_EQ(m.resid_reads, CS_BAND_POINTS * 4 * CS_BAND_REPS * 2);   //both cores of the parked channel, every capture
  T_EQ(t.clean[0], 0x2C);
  T_EQ(t.clean[1], 0x38);
  T_EQ(t.quiet[0], 0x2C);             //noise alone: every clean stage is quiet
  T_EQ(t.quiet[1], 0x38);
  T_EQ(t.set[0], 0x2C);
  T_EQ(t.tie[0], 0);
  T_TRUE(t.sum[0][4 - CS_STAGE_MIN] > 0);
  T_EQ(t.sum[0][3 - CS_STAGE_MIN], 0);
  T_EQ(t.sum[1][2 - CS_STAGE_MIN], (uint32_t)7900 * CS_BAND_POINTS * CS_BAND_REPS);
  T_EQ(cs_stage(t.value, 0), 3);
  T_EQ(cs_stage(t.value, 1), 4);
  v = t.value;

  //CH2 A clean at 2..5: a tie of 3 and 4, both clean in the tie pass: 3 (the power-on 3 by coincidence)
  T_EQ(cs_train_run(&io, v, 0x0C, &t), CS_OK);
  T_EQ(t.clean[2], 0x3C);
  T_EQ(t.clean[3], 0x38);
  T_EQ(t.tie[2], 0x18);
  T_EQ(t.tie[3], 0);
  T_EQ(t.tie_sum[2][0], 0);
  T_EQ(t.tie_sum[2][1], 0);
  T_EQ(t.captures, CS_BAND_POINTS * 4 * CS_BAND_REPS + CS_TIE_POINTS * 2 * CS_TIE_REPS);
  T_EQ(t.value, 0x08E3);

  {
    char s[64];

    cs_text(s, t.value);
    T_STR(s, "CH1 A3 B4, CH2 A3 B4");
  }

  //0.2.79's training (one capture per stage at the parked code) on the same unit: the intermittent stage reads clean
  //(2..5), but the pick no longer keeps the power-on 4
  mock_init(&m);
  m.dirty[1]      = 0x07;
  m.edge_stage[0] = 4;
  m.edge_lo16[0]  = cs_band_code16(0);   //the one point is the parked code
  m.edge_hi16[0]  = cs_band_code16(0);
  m.edge_pct      = 30;
  T_EQ(cs_train_run(&one, CS_PO, 0x03, &t), CS_OK);
  T_EQ(t.clean[0], 0x3C);             //A4 not seen
  T_EQ(cs_stage(t.value, 0), 3);      //but no longer the power-on 4

  //A park that does not complete, a band without points: fail
  mock_init(&m);
  m.park_fail_at = 3;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_E_IO);
  T_EQ(t.value, CS_PO);
  T_EQ(t.captures, 2 * 4 * CS_BAND_REPS);

  {
    cs_io none = { &m, 0, CS_BAND_REPS, CS_TIE_REPS, mock_park, mock_measure };

    T_EQ(cs_train_run(&none, CS_PO, 0x03, &t), CS_E_IO);
    none.points = 1;
    none.reps   = 0;
    T_EQ(cs_train_run(&none, CS_PO, 0x03, &t), CS_E_IO);
  }
}

//fw 0.2.105, bench 2026-10-22 on 0x153D: CH1 A4 garbles a real signal but only rarely the quiet input (here: 5 % of the
//captures at codes 126..130). The band's tries there (one point, 127.5) see nothing (2..5 clean), the tie pass's 3 x 16
//at 4 see it: 3. A mirror unit whose lower centre 3 is the weak one gets 4. The 0.2.81 band (6 points, 2 captures)
//missed both
static void test_tie(void)
{
  mock_fpga m;
  cs_io     io = { &m, CS_BAND_POINTS, CS_BAND_REPS, CS_TIE_REPS, mock_park, mock_measure };
  cs_io     old = { &m, 6, 2, 0, mock_park, mock_measure };
  cs_train  t;

  mock_init(&m);
  m.edge_stage[0] = 4;
  m.edge_lo16[0]  = 126 * 16;         //codes 126 .. 130
  m.edge_hi16[0]  = 130 * 16;
  m.edge_pct      = 5;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_EQ(t.clean[0], 0x3C);
  T_EQ(t.sum[0][4 - CS_STAGE_MIN], 0);   //the band pass: 2 tries at 5 % see nothing
  T_EQ(t.tie[0], 0x18);
  T_EQ(t.tie[1], 0x18);
  T_EQ(t.tie_sum[0][0], 0);
  T_TRUE(t.tie_sum[0][1] > 0);        //the tie pass: 4 garbled at least once in 3 x 16 tries
  T_EQ(t.set[0], 0x2C);
  T_EQ(cs_stage(t.value, 0), 3);
  T_EQ(cs_stage(t.value, 1), 3);      //B: a clean tie, the lower
  T_EQ(m.parks, CS_BAND_POINTS + CS_TIE_POINTS);
  T_EQ(m.code16, 126 * 16);           //the tie pass runs from 130 down to 126
  T_EQ(m.resid_reads, CS_BAND_POINTS * 4 * CS_BAND_REPS * 2);   //the tie pass reads no data

  //The weak stage is the lower centre: 3 leaves, {2, 4, 5} gives 4
  mock_init(&m);
  m.edge_stage[0] = 3;
  m.edge_lo16[0]  = 126 * 16;
  m.edge_hi16[0]  = 130 * 16;
  m.edge_pct      = 5;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_TRUE(t.tie_sum[0][0] > 0);
  T_EQ(t.tie_sum[0][1], 0);
  T_EQ(t.set[0], 0x34);
  T_EQ(cs_stage(t.value, 0), 4);

  //A weak stage the band pass already sees (50 %): no tie for that core, the other core's tie pass runs anyway
  mock_init(&m);
  m.edge_stage[0] = 3;
  m.edge_lo16[0]  = 126 * 16;
  m.edge_hi16[0]  = 130 * 16;
  m.edge_pct      = 50;
  m.dirty[0]      = 0x03;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_EQ(t.set[0], 0x34);               //3 already dirty in the band pass at 50 %: no tie
  T_EQ(t.tie[0], 0);
  T_EQ(cs_stage(t.value, 0), 4);

  //6 band points of 2 captures without a tie pass on the 5 % unit: 2..5 clean
  mock_init(&m);
  m.edge_stage[0] = 4;
  m.edge_lo16[0]  = 126 * 16;
  m.edge_hi16[0]  = 130 * 16;
  m.edge_pct      = 5;
  T_EQ(cs_train_run(&old, CS_PO, 0x03, &t), CS_OK);
  T_EQ(t.clean[0], 0x3C);
  T_EQ(t.captures, 6 * 4 * 2);

  //A tie-pass park that does not complete
  mock_init(&m);
  m.park_fail_at = CS_BAND_POINTS + 1;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_E_IO);
  T_EQ(t.value, CS_PO);
}

//fw-capmetric: cs_resid, the 3-sample-median residual
static void test_resid(void)
{
  static uint8_t big[4096];
  uint8_t x[8];
  uint32_t i;

  T_EQ(cs_resid(x, 0), 0);
  T_EQ(cs_resid(x, 2), 0);

  //Constant, a ramp, a step: no residual
  memset(x, 100, sizeof(x));
  T_EQ(cs_resid(x, 8), 0);

  for(i=0;i<8;i++)
    x[i] = (uint8_t)(10 * i);

  T_EQ(cs_resid(x, 8), 0);

  memset(x, 0, 4);
  memset(x + 4, 255, 4);
  T_EQ(cs_resid(x, 8), 0);

  //One spike of +14 (0x60 -> 0x6E): 196; a spike of -5 at the end is not scored (j stops at n - 2)
  memset(x, 0x60, sizeof(x));
  x[3] = 0x6E;
  T_EQ(cs_resid(x, 8), 196);
  x[7] = 0x5B;
  T_EQ(cs_resid(x, 8), 196);
  x[6] = 0x5B;                        //two in a row are the median's: no residual
  T_EQ(cs_resid(x, 8), 196);

  //Alternating codes: every inner sample 1 code off its median
  for(i=0;i<8;i++)
    x[i] = (uint8_t)(0x7F + (i & 1));

  T_EQ(cs_resid(x, 8), 6);

  //The largest: 0 / 255 alternating over 4096 samples, 4094 x 65025 (fits u32)
  for(i=0;i<sizeof(big);i++)
    big[i] = (uint8_t)((i & 1) ? 255 : 0);

  T_EQ(cs_resid(big, sizeof(big)), 4094u * 65025u);
  T_EQ(CS_RESID_DIV, 127);
}

//fw-capmetric: cs_quiet on the bench sweep (fpga/al3/README.md section 18, 2026-10-22 11:23): q16 per stage 2..5 as
//printed by capwin_sweep.py, times 34 (17 points x 2 captures: q is their sum), and the 0x36 sums of the sweep. The
//quiet sets {3}, {3, 4, 5}, {3}, {3, 4, 5} give 0x08E3, the trained value, and drop CH1 A5 and CH2 A4
static void test_quiet(void)
{
  static const uint32_t q16x10[CS_CORES][CS_STAGES] =
  {
    { 150, 28, 4140, 97 }, { 514, 38, 39, 39 }, { 4730, 323, 684, 17700 }, { 1860, 186, 180, 184 }
  };
  static const uint32_t sums[CS_CORES][CS_STAGES] = { { 0, 0, 93, 0 }, { 89, 0, 0, 0 }, { 0, 0, 2146, 6528 },
                                                      { 2462, 0, 0, 0 } };
  static const uint8_t quiet[CS_CORES] = { 0x08, 0x38, 0x08, 0x38 };
  uint32_t q[CS_STAGES], sum[CS_STAGES], k, i;
  uint16_t v = 0;

  for(k=0;k<CS_CORES;k++)
  {
    for(i=0;i<CS_STAGES;i++)
      q[i] = q16x10[k][i] * 34 / 10;

    T_EQ(cs_quiet(sums[k], q), quiet[k]);
    v = cs_with(v, k, cs_pick(cs_quiet(sums[k], q)));
  }

  T_EQ(v, 0x08E3);

  //The outlier rule alone kept CH1 A5 (the sweep's 0x36 sums: {2, 3, 5}) and CH2 A2 ({2, 3})
  for(i=0;i<CS_STAGES;i++)
    q[i] = 0;

  T_EQ(cs_quiet(sums[0], q), 0x2C);
  T_EQ(cs_quiet(sums[2], q), 0x0C);

  //Within 25 % plus 4 of the best clean stage: 100 -> 129 quiet, 130 not; a better stage with outliers does not count
  sum[0] = 5; sum[1] = 0; sum[2] = 0; sum[3] = 0;
  q[0] = 1;   q[1] = 100; q[2] = 129; q[3] = 130;
  T_EQ(cs_quiet(sum, q), 0x18);

  //q 0 (no noise at all): +4 still lets equal noise-free stages through
  sum[0] = 0;
  q[0] = 0; q[1] = 4; q[2] = 5; q[3] = 0;
  T_EQ(cs_quiet(sum, q), 0x2C);

  //No outlier-clean stage: none quiet
  sum[0] = 1; sum[1] = 1; sum[2] = 1; sum[3] = 1;
  T_EQ(cs_quiet(sum, q), 0);
}

//fw-capmetric host tests (section 18 "Host tests"): the bench's two error classes on the mock
static void test_metric(void)
{
  mock_fpga m;
  cs_io     io = { &m, CS_BAND_POINTS, CS_BAND_REPS, CS_TIE_REPS, mock_park, mock_measure };
  cs_train  t;
  uint16_t  clean_v;

  //A clean unit with the noise only: CH1 A3 B3 (2..5 quiet, the tie pass keeps both centres), as the outlier rule
  mock_init(&m);
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_EQ(t.quiet[0], 0x3C);
  T_EQ(t.quiet[1], 0x3C);
  T_TRUE(t.q[0][0] > 0);              //the 0.5-code noise is scored
  clean_v = t.value;
  T_EQ(clean_v, 0x08DB);

  //Class (a): CH1 A mixes codes at its carries at 3 and 5 (3 % of the samples at 8k), invisible to 0x36. The outlier
  //rule keeps 2..5 (its pick 3, a mixing stage); the quiet rule keeps {2, 4} and picks 4. CH1 B as before
  mock_init(&m);
  m.mix[0]  = 0x28;
  m.mix_pct = 3;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_EQ(t.clean[0], 0x3C);
  T_EQ(cs_pick(t.clean[0]), 3);
  T_TRUE(t.q[0][3 - CS_STAGE_MIN] > 2 * t.q[0][4 - CS_STAGE_MIN]);
  T_EQ(t.quiet[0], 0x14);
  T_EQ(t.set[0], 0x14);
  T_EQ(cs_stage(t.value, 0), 4);
  T_EQ(cs_stage(t.value, 1), 3);

  //The bench's CH1 A: mixes at 2, 4 and 5, clean at 3 only
  mock_init(&m);
  m.mix[0]  = 0x34;
  m.mix_pct = 3;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_EQ(t.clean[0], 0x3C);
  T_EQ(t.set[0], 0x08);
  T_EQ(cs_stage(t.value, 0), 3);

  //Class (b): the ADC's own error at every stage (8k -> 8k + 4 in 9 % of those samples) raises every q alike: the
  //clean unit's pick and the class (a) unit's pick do not change
  mock_init(&m);
  m.floor_pct = 9;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_EQ(t.quiet[0], 0x3C);
  T_EQ(t.quiet[1], 0x3C);
  T_EQ(t.value, clean_v);

  mock_init(&m);
  m.floor_pct = 9;
  m.mix[0]    = 0x28;
  m.mix_pct   = 3;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_EQ(t.set[0], 0x14);
  T_EQ(cs_stage(t.value, 0), 4);

  //The reference pass scores its B core too: CH2 B dirty with its own encode, clean at 2..5 with the A encode
  mock_init(&m);
  m.dirty[3]     = 0xFF;
  m.dirty_ref[3] = 0x03;
  T_EQ(cs_train_run(&io, CS_PO, 0x0C, &t), CS_OK);
  T_EQ(t.ref, 0x08);
  T_EQ(t.quiet[3], 0);
  T_EQ(t.quiet_ref[3], 0x3C);
  T_TRUE(t.q_ref[3][0] > 0);
  T_EQ(t.value & CS_REF_CH2B, CS_REF_CH2B);
}

//fw-capmetric2: the noise bound (cap_stage.h "Noise bound"). The bench of 2026-10-22 17:20 (0x153D fd0be7a2, fw 0.2.105
//8c70c2fc, inputs open) read stale ring words for the residual: q per stage 2..5 summed over 34 captures, the 0x36 sums
//as there. The relative rule took them (0x06E3); the bound fails both channels, every core, and nothing changes
static void bench_e(mock_fpga *m, uint32_t k, const uint32_t q[CS_STAGES])
{
  uint32_t i;

  for(i=0;i<CS_STAGES;i++)
    m->e_fix[k][i] = q[i] / 34 * CS_RESID_DIV;
}

static void test_noise_bound(void)
{
  static const uint32_t q1a[CS_STAGES] = { 1461590, 1461590, 1461706, 1462218 };
  static const uint32_t q1b[CS_STAGES] = { 8958, 8972, 8992, 9024 };
  static const uint32_t q2a[CS_STAGES] = { 2070230, 2070188, 2069209, 2069803 };
  static const uint32_t q2b[CS_STAGES] = { 8082, 8113, 8179, 8263 };
  mock_fpga m;
  cs_io     io = { &m, CS_BAND_POINTS, CS_BAND_REPS, CS_TIE_REPS, mock_park, mock_measure };
  cs_train  t;
  uint32_t  i;

  //The bound in the units of q: 128 per capture is sigma 3.2 codes; the bench's B cores were at 263 and 238..243
  T_EQ(CS_RESID_MAX_Q16, 128);
  T_TRUE(q1b[1] / 34 > CS_RESID_MAX_Q16);
  T_TRUE(q2b[0] / 34 > CS_RESID_MAX_Q16);

  //CH1: A with outliers at 4 (34 there: the mock's dirty stage), B at 2
  mock_init(&m);
  m.dirty[0] |= 0x10;
  m.dirty[1] |= 0x04;
  bench_e(&m, 0, q1a);
  bench_e(&m, 1, q1b);
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_E_RESID);
  T_EQ(t.bad, 0x03);
  T_EQ(t.value, CS_PO);
  T_EQ(t.quiet[0], 0x2C);             //what the relative rule alone kept (the bench's [2, 3, 5])
  T_EQ(t.quiet[1], 0x38);

  //CH2: A with outliers at 4, B none
  mock_init(&m);
  m.dirty[2] |= 0x10;
  bench_e(&m, 2, q2a);
  bench_e(&m, 3, q2b);
  T_EQ(cs_train_run(&io, CS_PO, 0x0C, &t), CS_E_RESID);
  T_EQ(t.bad, 0x0C);
  T_EQ(t.value, CS_PO);

  //One core over it is enough; the bench's A alone, B from the mock's 0.5-code data
  mock_init(&m);
  bench_e(&m, 0, q1a);
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_E_RESID);
  T_EQ(t.bad, 0x01);

  //The edge: the best stage at 128 per capture passes, 129 fails; a noisier stage beside it does not matter
  mock_init(&m);

  for(i=0;i<CS_STAGES;i++)
    m.e_fix[0][i] = 1000 * CS_RESID_DIV;

  m.e_fix[0][3 - CS_STAGE_MIN] = CS_RESID_MAX_Q16 * CS_RESID_DIV;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_EQ(t.q[0][3 - CS_STAGE_MIN], CS_RESID_MAX_Q16 * CS_BAND_POINTS * CS_BAND_REPS);
  T_EQ(cs_stage(t.value, 0), 3);
  m.e_fix[0][3 - CS_STAGE_MIN] = (CS_RESID_MAX_Q16 + 1) * CS_RESID_DIV;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_E_RESID);
  T_EQ(t.bad, 0x01);

  //The reference pass is the timing in use for a B core dirty with its own encode: its q is the one bounded
  mock_init(&m);
  m.dirty[3]     = 0xFF;
  m.dirty_ref[3] = 0x03;

  for(i=0;i<CS_STAGES;i++)
    m.e_fix[3][i] = 300 * CS_RESID_DIV;

  T_EQ(cs_train_run(&io, CS_PO, 0x0C, &t), CS_E_RESID);
  T_EQ(t.ref, 0x08);
  T_EQ(t.bad, 0x08);

  //A clean unit at 3 codes rms (the zero step's full-rate noise limit) passes: q16 about 115 per capture (cap_stage.h)
  mock_init(&m);
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_TRUE(t.q[0][0] / (CS_BAND_POINTS * CS_BAND_REPS) < 16);   //the mock's 0.5-code noise: about 5
}

static void test_watch(void)
{
  cs_watch w;

  cs_watch_clear(&w);
  T_EQ(cs_watch_add(&w, 0x01, 1000), 0);
  T_EQ(cs_watch_add(&w, 0x00, 2000), 0);
  T_EQ(cs_watch_add(&w, 0x01, 11000), 0);
  T_EQ(cs_watch_add(&w, 0x05, 61000), 0x01);   //third within 60 s (1000 .. 61000)
  T_EQ(w.repeat, 0x01);
  T_EQ(cs_watch_add(&w, 0x05, 62000), 0);      //once only
  T_EQ(cs_watch_add(&w, 0x04, 63000), 0x04);   //CH2 A: 61000, 62000, 63000
  T_EQ(w.repeat, 0x05);

  //Spread out: never three within a minute
  cs_watch_clear(&w);
  T_EQ(cs_watch_add(&w, 0x02, 0), 0);
  T_EQ(cs_watch_add(&w, 0x02, 50000), 0);
  T_EQ(cs_watch_add(&w, 0x02, 120000), 0);
  T_EQ(cs_watch_add(&w, 0x02, 130000), 0);
  T_EQ(cs_watch_add(&w, 0x02, 181000), 0);     //120000, 130000, 181000: 61 s
  T_EQ(cs_watch_add(&w, 0x02, 182000), 0x02);  //130000, 181000, 182000: 52 s

  //The tick wraps
  cs_watch_clear(&w);
  T_EQ(cs_watch_add(&w, 0x08, 0xFFFFF000u), 0);
  T_EQ(cs_watch_add(&w, 0x08, 0xFFFFFF00u), 0);
  T_EQ(cs_watch_add(&w, 0x08, 0x00001000u), 0x08);
}

static void test_train(void)
{
  mock_fpga m;
  cs_io     io = { &m, 1, 1, 0, mock_park, mock_measure };
  cs_train  t;

  //All of 2..5 clean, no tie pass: 3 for both (not the power-on 4), four captures, the other channel untouched
  mock_init(&m);
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_EQ(t.value, 0x08DB);
  T_EQ(t.captures, 4);
  T_EQ(m.measures, 4);
  T_EQ(t.clean[0], 0x3C);
  T_EQ(t.clean[2], 0);                //not trained
  T_EQ(cs_stage(m.last, 2), 3);       //CH2 kept its stages during CH1's training
  T_EQ(cs_stage(m.last, 3), 4);

  //CH2 A garbles at 2 and 3 (window late): 4; CH2 B at 5: 3..4 with 2 clean beside 3: 3
  mock_init(&m);
  m.dirty[2] = 0x0F;
  m.dirty[3] = 0x23;
  T_EQ(cs_train_run(&io, CS_PO, 0x0C, &t), CS_OK);
  T_EQ(cs_stage(t.value, 2), 4);
  T_EQ(cs_stage(t.value, 3), 3);
  T_EQ(cs_stage(t.value, 0), 4);      //CH1 as it was
  T_EQ(t.value & (CS_REF_CH1B | CS_REF_CH2B), 0);

  //A trained value that is the start of a new training keeps the other channel's result
  {
    uint16_t v = t.value;

    mock_init(&m);
    m.dirty[0] = 0x07;                //CH1 A clean 3..5: 4 (middle)
    T_EQ(cs_train_run(&io, v, 0x03, &t), CS_OK);
    T_EQ(cs_stage(t.value, 0), 4);
    T_EQ(t.set[0], 0x38);
    T_EQ(cs_stage(t.value, 2), 4);
    T_EQ(cs_stage(t.value, 3), 3);
  }

  //CH1 B dirty everywhere with its own encode, clean at 3..4 with the A encode (data-align mode): reference bit set
  mock_init(&m);
  m.dirty[1]     = 0xFF;
  m.dirty_ref[1] = 0xE7;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_EQ(t.captures, 8);
  T_EQ(t.value & CS_REF_CH1B, CS_REF_CH1B);
  T_EQ(t.value & CS_REF_CH2B, 0);
  T_EQ(cs_stage(t.value, 1), 3);      //3, 4 clean with the A encode: a tie, the lower
  T_EQ(t.clean_ref[1], 0x18);
  T_EQ(t.set[1], 0x18);               //the reference timing's set
  T_EQ(t.clean_ref[0], 0);            //A cores are never measured with a reference bit

  //A stale reference bit in the start value is cleared for the trained B core when its own encode is clean
  mock_init(&m);
  T_EQ(cs_train_run(&io, CS_PO | CS_REF_CH2B | CS_REF_CH1B, 0x0C, &t), CS_OK);
  T_EQ(t.value & CS_REF_CH2B, 0);
  T_EQ(t.value & CS_REF_CH1B, CS_REF_CH1B);  //CH1 not trained: as it was

  //Fail fast: a bitstream that garbles at every stage fails, nothing changes, the bad cores are named
  mock_init(&m);
  m.dirty[2] = 0xFF;
  m.dirty[3] = 0xFF;
  T_EQ(cs_train_run(&io, CS_PO, 0x0C, &t), CS_E_DIRTY);
  T_EQ(t.value, CS_PO);
  T_EQ(t.bad, 0x0C);
  T_EQ(t.captures, 8);                //the B core tried the reference timing too

  //An A core with no clean stage: no reference pass for it
  mock_init(&m);
  m.dirty[0] = 0xFF;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_E_DIRTY);
  T_EQ(t.bad, 0x01);
  T_EQ(t.captures, 4);

  //A capture that does not complete
  mock_init(&m);
  m.fail_at = 2;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_E_IO);
  T_EQ(t.value, CS_PO);
}

static void test_guard(void)
{
  uint8_t  set[CS_CORES] = { 0x3C, 0x38, 0x3C, 0x10 };
  uint16_t sets = cs_sets_pack(set), v = CS_PO;
  cs_watch w;

  T_EQ(sets, 0x4FEF);
  T_EQ(cs_sets_core(sets, 0), 0x3C);
  T_EQ(cs_sets_core(sets, 1), 0x38);
  T_EQ(cs_sets_core(sets, 3), 0x10);
  T_EQ(cs_sets_with(sets, 2, 0x2C), 0x4BEF);

  //The bench case: CH1 A4 flagged, trained 2..5: 4 leaves the set, {2, 3, 5} gives 3
  T_EQ(cs_guard(&v, &sets, 0x01), 0x01);
  T_EQ(cs_stage(v, 0), 3);
  T_EQ(cs_sets_core(sets, 0), 0x2C);
  T_EQ(v, 0x08E3);

  //Flagged again (a later boot): 3 leaves, {2, 5}: 2; then 2 leaves: 5; then nothing is left: kept
  T_EQ(cs_guard(&v, &sets, 0x01), 0x01);
  T_EQ(cs_stage(v, 0), 2);
  T_EQ(cs_guard(&v, &sets, 0x01), 0x01);
  T_EQ(cs_stage(v, 0), 5);
  T_EQ(cs_guard(&v, &sets, 0x01), 0);
  T_EQ(cs_stage(v, 0), 5);
  T_EQ(cs_sets_core(sets, 0), 0x20);

  //CH2 B trained at 4 only: nothing to move to; CH1 B with a stage outside its set (not the trained value): kept
  v = cs_with(CS_PO, 1, 2);
  sets = cs_sets_pack(set);
  T_EQ(cs_guard(&v, &sets, 0x0A), 0);
  T_EQ(v, cs_with(CS_PO, 1, 2));
  T_EQ(sets, cs_sets_pack(set));

  //No stored set: no move
  v = CS_PO;
  sets = 0;
  T_EQ(cs_guard(&v, &sets, 0x0F), 0);
  T_EQ(v, CS_PO);

  //Two cores at once, the reference bit kept
  v = (uint16_t)(CS_PO | CS_REF_CH1B);
  sets = cs_sets_pack(set);
  T_EQ(cs_guard(&v, &sets, 0x05), 0x05);
  T_EQ(cs_stage(v, 0), 3);
  T_EQ(cs_stage(v, 2), 4);            //CH2 A at 3 in 2..5: {2, 4, 5} gives 4
  T_EQ(v & CS_REF_CH1B, CS_REF_CH1B);

  //The watch forgets a moved core (its next repeat counts from the new stage)
  cs_watch_clear(&w);
  T_EQ(cs_watch_add(&w, 0x05, 0), 0);
  T_EQ(cs_watch_add(&w, 0x05, 10), 0);
  T_EQ(cs_watch_add(&w, 0x05, 20), 0x05);
  cs_watch_forget(&w, 0x01);
  T_EQ(w.repeat, 0x04);
  T_EQ(cs_watch_add(&w, 0x01, 30), 0);
  T_EQ(cs_watch_add(&w, 0x01, 40), 0);
  T_EQ(cs_watch_add(&w, 0x01, 50), 0x01);
}

static void test_selfcheck(void)
{
  uint16_t c[4] = { 0, 0, 0, 0 }, ex[4];

  T_EQ(cs_flagged(c), 0);

  //A signal: both cores of a chip count alike
  c[0] = 120; c[1] = 118; c[2] = 40; c[3] = 44;
  T_EQ(cs_flagged(c), 0);
  cs_excess(c, ex);
  T_EQ(ex[0], 2);
  T_EQ(ex[1], 0);
  T_EQ(ex[3], 4);

  //CH2 A garbled on a quiet input
  c[0] = 0; c[1] = 0; c[2] = 37; c[3] = 0;
  T_EQ(cs_flagged(c), 0x04);
  c[2] = 8;
  T_EQ(cs_flagged(c), 0);             //at the limit
  c[2] = 9;
  T_EQ(cs_flagged(c), 0x04);
}

static void test_texts(void)
{
  char t[64];

  cs_text(t, CS_PO);
  T_STR(t, "CH1 A4 B4, CH2 A3 B4");
  cs_text_ch(t, CS_PO | CS_REF_CH2B, 1);
  T_STR(t, "CH2 A3 B4 A-clk");
  cs_core_name(t, 2);
  T_STR(t, "CH2 A");
  cs_core_name(t, 1);
  T_STR(t, "CH1 B");
}

static void test_trailer(void)
{
  uint8_t  b[CS_TRAILER_BYTES];
  uint16_t v, f;

  T_TRUE(CS_TRAILER_OFFSET >= ACAL_PROV_OFFSET + ACAL_PROV_BYTES);
  T_TRUE(CS_TRAILER_OFFSET + CS_TRAILER_BYTES <= 512);

  cs_trailer_pack(0x0924, 0x153A, b);
  T_EQ(b[0], 'A');
  T_EQ(b[1], 'C');
  T_EQ(b[2], 'A');
  T_EQ(b[3], 'P');
  T_EQ(cs_trailer_unpack(b, &v, &f), CS_T_VALID);
  T_EQ(v, 0x0924);
  T_EQ(f, 0x153A);

  b[8] ^= 1;
  T_EQ(cs_trailer_unpack(b, &v, &f), CS_T_BAD);
  T_EQ(v, 0);

  memset(b, 0, sizeof(b));
  T_EQ(cs_trailer_unpack(b, &v, &f), CS_T_NONE);

  //The clean sets after it (bytes 504..511), bound to the trailer by the CRC
  {
    uint8_t  sec[512];
    uint16_t sets;

    T_EQ(CS_EXT_OFFSET, CS_TRAILER_OFFSET + CS_TRAILER_BYTES);
    T_EQ(CS_EXT_OFFSET + CS_EXT_BYTES, 512);
    memset(sec, 0, sizeof(sec));
    T_EQ(cs_ext_unpack(sec, &sets), 0);         //a 0.2.79..0.2.104 save: zeros
    cs_trailer_pack(0x08E3, 0x153D, sec + CS_TRAILER_OFFSET);
    cs_ext_pack(sec, 0x1E7F);
    T_EQ(sec[504], 0x7F);
    T_EQ(sec[505], 0x1E);
    T_EQ(cs_ext_unpack(sec, &sets), 1);
    T_EQ(sets, 0x1E7F);
    T_EQ(cs_trailer_unpack(sec + CS_TRAILER_OFFSET, &v, &f), CS_T_VALID);   //the trailer as 0.2.79+ read it
    T_EQ(v, 0x08E3);

    //Another trailer (an older firmware's save of a new value with the old sets left): not valid
    cs_trailer_pack(0x08E4, 0x153D, sec + CS_TRAILER_OFFSET);
    T_EQ(cs_ext_unpack(sec, &sets), 0);
    T_EQ(sets, 0);
  }
}

int main(void)
{
  test_fields();
  test_train();
  test_band_pwm();
  test_band();
  test_tie();
  test_resid();
  test_quiet();
  test_metric();
  test_noise_bound();
  test_watch();
  test_guard();
  test_selfcheck();
  test_texts();
  test_trailer();
  return t_report("test_cap_stage");
}
