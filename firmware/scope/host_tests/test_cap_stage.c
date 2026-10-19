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
//A dirty core counts about every word of the capture, a clean one 0; a quiet input moves no core by itself
typedef struct
{
  uint8_t  dirty[CS_CORES];
  uint8_t  dirty_ref[CS_CORES];
  uint32_t measures;
  uint16_t last;
  int      fail_at;                   //measure number that does not complete (0 = none)
} mock_fpga;

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
  T_TRUE(cs_has(0x153A));
  T_TRUE(cs_has(0x153F));
  T_FALSE(cs_has(0x1539));
  T_FALSE(cs_has(0x1632));

  //cs_pick: the middle of the clean run, the power-on stage when symmetric, then the clean one, then a reserve
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

static void test_train(void)
{
  mock_fpga m;
  cs_io     io = { &m, mock_measure };
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
  test_selfcheck();
  test_texts();
  test_trailer();
  return t_report("test_cap_stage");
}
