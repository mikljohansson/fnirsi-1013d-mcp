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
//a stage at the edge of the transition (edge_stage) garbles only while the input is parked at some band points
//(edge_points, bit p) and then only in edge_pct % of the captures (deterministic: every capture adds edge_pct to an
//accumulator, a capture garbles when it reaches 100), as CH1 A4 on the owner's unit (bench 2026-10-20: codes 127..135)
typedef struct
{
  uint8_t  dirty[CS_CORES];
  uint8_t  dirty_ref[CS_CORES];
  uint8_t  edge_stage[CS_CORES];      //0 = none
  uint8_t  edge_points[CS_CORES];
  uint32_t edge_pct;
  uint32_t edge_acc[CS_CORES];
  uint32_t edge_tries[CS_CORES];      //captures at the edge stage and a bad point
  uint32_t edge_hits[CS_CORES];       //of them garbled
  uint32_t point, parks, measures;
  uint16_t last;
  int      fail_at;                   //measure number that does not complete (0 = none)
  int      park_fail_at;              //park number that does not complete (0 = none)
} mock_fpga;

static int mock_park(void *ctx, uint32_t p)
{
  mock_fpga *m = (mock_fpga *)ctx;

  m->parks++;
  m->point = p;
  return (m->park_fail_at && (m->parks == (uint32_t)m->park_fail_at)) ? -1 : 0;
}

static int mock_measure(void *ctx, uint16_t v, uint16_t cnt[CS_CORES])
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

    if(m->edge_stage[k] && (s == m->edge_stage[k]) && !(v & cs_ref_bit(k)) && ((m->edge_points[k] >> m->point) & 1))
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

  //cs_pick: the middle of the widest clean run, 3 or 4 before a reserve, the power-on stage when symmetric
  T_EQ(cs_pick(0x2C, 4), 3);          //fw 0.2.81, CH1 A on the owner's unit: 2, 3 clean, 4 bad, 5 clean
  T_EQ(cs_pick(0x2C, 3), 3);
  T_EQ(cs_pick(0x34, 4), 4);          //2 clean, 3 bad, 4, 5 clean: the run of two holding 4
  T_EQ(cs_pick(0x34, 3), 4);          //the power-on stage outside the widest run is not kept
  T_EQ(cs_pick(0x14, 3), 4);          //2 and 4 alone: 4
  T_EQ(cs_pick(0x28, 4), 3);          //3 and 5 alone: 3
  T_EQ(cs_pick(0x3C, 4), 4);          //2..5 clean
  T_EQ(cs_pick(0x3C, 3), 3);
  T_EQ(cs_pick(0x1C, 4), 3);          //2..4 clean: 3 has 2 beside it
  T_EQ(cs_pick(0x38, 3), 4);          //3..5 clean
  T_EQ(cs_pick(0x18, 3), 3);          //only 3, 4: power-on
  T_EQ(cs_pick(0x08, 4), 3);
  T_EQ(cs_pick(0x30, 3), 4);          //4, 5
  T_EQ(cs_pick(0x24, 3), 2);          //2 and 5, not 3 or 4: next to the power-on stage
  T_EQ(cs_pick(0x24, 4), 5);
  T_EQ(cs_pick(0x04, 4), 2);
  T_EQ(cs_pick(0x00, 4), CS_NONE);
  T_EQ(cs_pick(0x03, 4), CS_NONE);    //0 and 1 are not tried
}

static void test_band_pwm(void)
{
  //600 reads 168 codes, 900 reads 88 codes: 80 codes per 300 counts
  T_EQ(cs_band_code(0), 120);
  T_EQ(cs_band_code(2), 128);
  T_EQ(cs_band_code(CS_BAND_POINTS - 1), 140);
  T_EQ(cs_band_pwm(600, 168 * 16, 900, 88 * 16, 128), 750);
  T_EQ(cs_band_pwm(600, 168 * 16, 900, 88 * 16, 120), 780);
  T_EQ(cs_band_pwm(600, 168 * 16, 900, 88 * 16, 140), 705);
  T_EQ(cs_band_pwm(600, 168 * 16, 900, 88 * 16, 200), 480);   //beyond the low point: extrapolated (negative numerator)
  T_EQ(cs_band_pwm(600, 168 * 16, 900, 88 * 16, 169), 596);   //rounded either side
  T_EQ(cs_band_pwm(600, 100 * 16, 900, 100 * 16, 128), -1);   //no response
  T_EQ(cs_band_pwm(600, 90 * 16, 900, 100 * 16, 128), -1);    //wrong sign
  T_EQ(cs_band_pwm(900, 168 * 16, 600, 88 * 16, 128), -1);
}

//fw 0.2.81: the owner's unit (bench 2026-10-20 on 0x153A): CH1 A bad at 4 only at codes 128 and 132 (band points 2, 3)
//in 30 % of the captures, CH1 B bad at 2, CH2 A clean, CH2 B bad at 2. The band training picks CH1 A3 B4, CH2 A3 B4
static void test_band(void)
{
  mock_fpga m;
  cs_io     io = { &m, CS_BAND_POINTS, CS_BAND_REPS, mock_park, mock_measure };
  cs_io     one = { &m, 1, 1, mock_park, mock_measure };
  cs_train  t;
  uint16_t  v;

  mock_init(&m);
  m.dirty[1]       = 0x07;
  m.dirty[3]       = 0x07;
  m.edge_stage[0]  = 4;
  m.edge_points[0] = 0x0C;
  m.edge_pct       = 30;

  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_EQ(t.captures, CS_BAND_POINTS * 4 * CS_BAND_REPS);
  T_EQ(m.parks, CS_BAND_POINTS);
  T_EQ(m.edge_tries[0], 4);           //two points, two captures each
  T_TRUE(m.edge_hits[0] >= 1);
  T_EQ(t.clean[0], 0x2C);
  T_EQ(t.clean[1], 0x38);
  T_TRUE(t.sum[0][4 - CS_STAGE_MIN] > 0);
  T_EQ(t.sum[0][3 - CS_STAGE_MIN], 0);
  T_EQ(t.sum[1][2 - CS_STAGE_MIN], (uint32_t)7900 * CS_BAND_POINTS * CS_BAND_REPS);
  T_EQ(cs_stage(t.value, 0), 3);
  T_EQ(cs_stage(t.value, 1), 4);
  v = t.value;

  T_EQ(cs_train_run(&io, v, 0x0C, &t), CS_OK);
  T_EQ(t.clean[2], 0x3C);
  T_EQ(t.clean[3], 0x38);
  T_EQ(t.value, 0x08E3);

  {
    char s[64];

    cs_text(s, t.value);
    T_STR(s, "CH1 A3 B4, CH2 A3 B4");
  }

  //0.2.79's training (one capture per stage at the parked code) on the same unit: the intermittent stage reads clean and
  //the power-on A4 is kept, the fault the bench saw
  mock_init(&m);
  m.dirty[1]       = 0x07;
  m.edge_stage[0]  = 4;
  m.edge_points[0] = 0x01;            //the one point is the parked code
  m.edge_pct       = 30;
  T_EQ(cs_train_run(&one, CS_PO, 0x03, &t), CS_OK);
  T_EQ(cs_stage(t.value, 0), 4);

  //The limit: a stage that garbles at one code only and in a third of the captures can pass the band's two captures
  //there (the in-operation watch then asks for a new calibration)
  mock_init(&m);
  m.edge_stage[0]  = 3;
  m.edge_points[0] = 0x20;            //code 140
  m.edge_pct       = 34;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_EQ(m.edge_tries[0], 2);
  T_EQ(m.edge_hits[0], 0);            //two tries at 34 %: not seen (the limit of two captures per point)
  T_EQ(cs_stage(t.value, 0), 4);

  //A park that does not complete, a band without points: fail
  mock_init(&m);
  m.park_fail_at = 3;
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_E_IO);
  T_EQ(t.value, CS_PO);
  T_EQ(t.captures, 2 * 4 * CS_BAND_REPS);

  {
    cs_io none = { &m, 0, CS_BAND_REPS, mock_park, mock_measure };

    T_EQ(cs_train_run(&none, CS_PO, 0x03, &t), CS_E_IO);
    none.points = 1;
    none.reps   = 0;
    T_EQ(cs_train_run(&none, CS_PO, 0x03, &t), CS_E_IO);
  }
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
  cs_io     io = { &m, 1, 1, mock_park, mock_measure };
  cs_train  t;

  //All of 2..5 clean: the power-on stages, four captures, the other channel untouched
  mock_init(&m);
  T_EQ(cs_train_run(&io, CS_PO, 0x03, &t), CS_OK);
  T_EQ(t.value, CS_PO);
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
  T_EQ(cs_stage(t.value, 1), 4);
  T_EQ(t.clean_ref[1], 0x18);
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
}

int main(void)
{
  test_fields();
  test_train();
  test_band_pwm();
  test_band();
  test_watch();
  test_selfcheck();
  test_texts();
  test_trailer();
  return t_report("test_cap_stage");
}
