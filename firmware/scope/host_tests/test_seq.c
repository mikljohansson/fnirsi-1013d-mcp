//----------------------------------------------------------------------------------------------------------------------------------
// test_seq.c - pc_seq.c, the 0x1536 sequencer maths: fit rules (fpga/al3/README.md section 6 limits), register bytes,
// status and table entry layouts, segment slot addresses, the UI averaging plan and the exact rounded mean.
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdlib.h>

#include "pc_seq.h"
#include "test_util.h"

static pc_seq_cfg seg(uint8_t m, uint16_t count, uint8_t flags)
{
  pc_seq_cfg c = { PC_SEQ_SEGMENTS, flags, m, count, 0, 0 };
  return c;
}

static pc_seq_cfg avg(uint16_t positions, uint32_t n)
{
  pc_seq_cfg c = { PC_SEQ_AVERAGE, 0, 0, positions, n, 0 };
  return c;
}

static void test_fit(void)
{
  pc_seq_cfg c;
  uint8_t m;

  c = seg(7, 32, 0);
  T_EQ(pc_seq_fit(&c, 16, 120, 0), PC_SEQ_FIT_OK);               //README bench check 2
  T_EQ(pc_seq_fit(&c, 16, 126, 0), PC_SEQ_FIT_OK);               //2^7 - 2
  T_EQ(pc_seq_fit(&c, 16, 127, 0), PC_SEQ_FIT_TOTAL);            //2 words after the window
  T_EQ(pc_seq_fit(&c, 120, 120, 0), PC_SEQ_FIT_PRE);
  T_EQ(pc_seq_fit(&c, 0, 0, 0), PC_SEQ_FIT_PRE);

  for(m=0;m<16;m++)
  {
    c = seg(m, 1, 0);
    T_EQ(pc_seq_fit(&c, 1, 20, 0), ((m >= 5) && (m <= 12)) ? PC_SEQ_FIT_OK : PC_SEQ_FIT_M);

    if((m >= 5) && (m <= 12))
    {
      T_EQ(pc_seq_max_count(&c, 20), 8192u >> m);
      c.count = (uint16_t)(8192u >> m);
      T_EQ(pc_seq_fit(&c, 1, 20, 1), PC_SEQ_FIT_OK);
      c.count++;
      T_EQ(pc_seq_fit(&c, 1, 20, 1), PC_SEQ_FIT_COUNT);
    }
  }

  c = seg(7, 0, 0);
  T_EQ(pc_seq_fit(&c, 1, 20, 0), PC_SEQ_FIT_COUNT);
  c = seg(7, 4, PC_SEQ_F_CIRCULAR);
  T_EQ(pc_seq_fit(&c, 1, 20, 0), PC_SEQ_FIT_OK);
  T_EQ(pc_seq_slots(&c), 64);
  c.flags = 0;
  T_EQ(pc_seq_slots(&c), 4);
  c.flags = 2;
  T_EQ(pc_seq_fit(&c, 1, 20, 0), PC_SEQ_FIT_FLAGS);

  //Averaging: legacy lanes exact up to N = 257, 16-bit up to 65536
  c = avg(1, 257);
  T_EQ(pc_seq_fit(&c, 750, 1500, 0), PC_SEQ_FIT_OK);
  c.n = 258;
  T_EQ(pc_seq_fit(&c, 750, 1500, 0), PC_SEQ_FIT_N);
  T_EQ(pc_seq_fit(&c, 750, 1500, 1), PC_SEQ_FIT_OK);
  c.n = 65536;
  T_EQ(pc_seq_fit(&c, 750, 1500, 1), PC_SEQ_FIT_OK);
  c.n = 65537;
  T_EQ(pc_seq_fit(&c, 750, 1500, 1), PC_SEQ_FIT_N);
  c.n = 0;
  T_EQ(pc_seq_fit(&c, 750, 1500, 1), PC_SEQ_FIT_N);
  c.n = 16;
  T_EQ(pc_seq_fit(&c, 50, 2046, 1), PC_SEQ_FIT_OK);
  T_EQ(pc_seq_fit(&c, 50, 2047, 1), PC_SEQ_FIT_TOTAL);
  c.count = 2;
  T_EQ(pc_seq_fit(&c, 50, 1024, 1), PC_SEQ_FIT_OK);
  T_EQ(pc_seq_fit(&c, 50, 1025, 1), PC_SEQ_FIT_COUNT);
  T_EQ(pc_seq_max_count(&c, 1000), 2);
  T_EQ(pc_seq_max_count(&c, 2047), 0);
  c.count = 0;
  T_EQ(pc_seq_fit(&c, 50, 1000, 1), PC_SEQ_FIT_COUNT);
  c.count = 1; c.flags = PC_SEQ_F_CIRCULAR;
  T_EQ(pc_seq_fit(&c, 50, 1000, 1), PC_SEQ_FIT_FLAGS);

  c.mode = 3;
  T_EQ(pc_seq_fit(&c, 50, 1000, 1), PC_SEQ_FIT_MODE);
  memset(&c, 0, sizeof(c));
  T_EQ(pc_seq_fit(&c, 9999, 0, 0), PC_SEQ_FIT_OK);               //off: no geometry rule
}

static void test_registers(void)
{
  pc_seq_cfg c = seg(9, 16, PC_SEQ_F_CIRCULAR);
  uint8_t raw[8] = { 0xE6, 0x01, 0x02, 0x03, 0x80, 0x05, 0xA7, 0xFE };
  pc_seq_status st;

  T_EQ(pc_seq_cmd57(&c, 0), 0x05);
  T_EQ(pc_seq_cmd57(&c, 1), 0x0D);
  T_EQ(pc_seq_cmd58(&c), 16);
  c = avg(3, 65536);
  T_EQ(pc_seq_cmd57(&c, 0), 0x02);
  T_EQ(pc_seq_cmd57(&c, 1), 0x0A);
  T_EQ(pc_seq_cmd5a(&c), 0);
  c.n = 256;
  T_EQ(pc_seq_cmd5a(&c), 256);
  c.flags = PC_SEQ_F_CIRCULAR;                                    //circular only means something for segments
  T_EQ(pc_seq_cmd57(&c, 0), 0x02);

  T_EQ(pc_seq_holdoff_units(0), 0);
  T_EQ(pc_seq_holdoff_units(1), 1);
  T_EQ(pc_seq_holdoff_units(5), 1);
  T_EQ(pc_seq_holdoff_units(6), 2);
  T_EQ(pc_seq_holdoff_units(1000), 200);
  T_EQ(pc_seq_holdoff_units(0xFFFFFFFFu), 858993459u);

  pc_seq_status_decode(raw, &st);
  T_EQ(st.flags, 0xE6); T_EQ(st.count, 0x010203); T_EQ(st.untriggered, 0x8005); T_EQ(st.state, 5); T_EQ(st.index, 0x7FE);
}

static void test_entries(void)
{
  pc_seq_entry e, d;
  uint64_t w;

  e.timestamp = 0xFFFFFFFFFFull; e.offset = 0x1FFF; e.triggered = 1; e.valid = 1;
  w = pc_seq_entry_encode(&e);
  T_EQ(w, 0x803FFFFFFFFFFFFFull);
  pc_seq_entry_decode(w, &d);
  T_EQ(d.timestamp, e.timestamp); T_EQ(d.offset, 0x1FFF); T_EQ(d.triggered, 1); T_EQ(d.valid, 1);

  //Bits 62..54 are 0 in the FPGA; decode ignores them
  pc_seq_entry_decode(0x0040000000000000ull | (5ull << 40) | 20000, &d);
  T_EQ(d.timestamp, 20000); T_EQ(d.offset, 5); T_EQ(d.triggered, 0); T_EQ(d.valid, 0);

  //Segment 3 of m = 7, trigger offset 10, pre 16: record wraps at the slot end
  T_EQ(pc_seq_seg_word(7, 3, 10, 16, 0), 3 * 128 + 122);
  T_EQ(pc_seq_seg_word(7, 3, 10, 16, 5), 3 * 128 + 127);
  T_EQ(pc_seq_seg_word(7, 3, 10, 16, 6), 3 * 128 + 0);
  T_EQ(pc_seq_seg_word(7, 3, 10, 16, 16), 3 * 128 + 10);
  T_EQ(pc_seq_seg_word(12, 1, 4095, 0, 1), 4096);
}

static void test_ui_plan(void)
{
  uint32_t us;

  //200 MSa/s, 3000 samples (1500 words), trigger at 50 %: 64 records of 30.16 us
  T_EQ(pc_seq_dead_ns(1500, 750, 10), 7500 + 40 + 752 * 10 + 100);
  T_EQ(pc_seq_ui_plan(1500, 750, 0, &us), 64); T_EQ(us, (64 * 30160 + 999) / 1000);
  //1 MSa/s (D 199): 4.51 ms per record -> 8
  T_EQ(pc_seq_ui_plan(1500, 750, 199, &us), 8);
  //100 kSa/s (D 1999): 45 ms per record -> no hardware run
  T_EQ(pc_seq_ui_plan(1500, 750, 1999, &us), 0); T_EQ(us, 0);
  //Too long for the accumulator, or no pretrigger room
  T_EQ(pc_seq_ui_plan(3000, 750, 0, &us), 0);
  T_EQ(pc_seq_ui_plan(2047, 750, 0, &us), 0);
  T_EQ(pc_seq_ui_plan(2046, 750, 0, &us), 64);
  T_EQ(pc_seq_ui_plan(1500, 1500, 0, &us), 0);
  T_EQ(pc_seq_ui_plan(0, 0, 0, &us), 0);
  //Powers of two only, never above the budget
  {
    uint32_t d;

    for(d=0;d<2000;d+=7)
    {
      uint32_t n = pc_seq_ui_plan(1500, 375, d, &us);

      if(n)
      {
        T_TRUE((n & (n - 1)) == 0); T_TRUE(n >= 2); T_TRUE(n <= 64);
        T_TRUE(us <= PC_SEQ_UI_BUDGET_US + 1);
      }
    }
  }
}

static void test_division(void)
{
  uint32_t c, k, bad = 0;

  T_EQ(pc_seq_recip(1), 0);
  T_EQ(pc_seq_recip(2), 0x80000000u);
  T_EQ(pc_seq_recip(3), 0x55555556u);

  //floor(x / c) for every x < 2^24 at multiples and one below, and a sweep (Granlund-Montgomery bound)
  for(c=1;c<=256;c++)
  {
    uint32_t r = pc_seq_recip(c), x;

    for(k=1;k*c < (1u << 24);k++)
    {
      x = k * c;
      if(pc_seq_udiv(x, c, r) != k) bad++;
      if(pc_seq_udiv(x - 1, c, r) != k - 1) bad++;
    }

    for(x=0;x<(1u << 24);x+=4093)
      if(pc_seq_udiv(x, c, r) != x / c) bad++;

    x = (1u << 24) - 1;
    if(pc_seq_udiv(x, c, r) != x / c) bad++;
  }

  T_EQ(bad, 0);
}

static void test_avg_code(void)
{
  uint32_t c, v, bad = 0;

  //Constant records: the mean of c copies of raw v through the core correction equals the correction of v, rounded
  for(c=1;c<=256;c+=(c < 8) ? 1 : 37)
  {
    uint32_t r = pc_seq_recip(c);

    for(v=0;v<256;v++)
    {
      int32_t g = 65536 + 2100, off = -150000;
      int64_t exact = ((int64_t)v * g + off + 32768) >> 16;

      exact = (exact < 0) ? 0 : ((exact > 255) ? 255 : exact);

      if(pc_seq_avg_code(v * c, c, r, 65536, 0) != (int32_t)v) bad++;
      if(pc_seq_avg_code(v * c, c, r, g, off) != exact) bad++;
    }
  }

  T_EQ(bad, 0);

  //Half codes round up: two records 100 and 101
  T_EQ(pc_seq_avg_code(201, 2, pc_seq_recip(2), 65536, 0), 101);
  //Mean 100.25 with a gain of 2: 200.5 -> 201
  T_EQ(pc_seq_avg_code(401, 4, pc_seq_recip(4), 131072, 0), 201);
  //Clamps
  T_EQ(pc_seq_avg_code(255 * 3, 3, pc_seq_recip(3), 70000, 1 << 20), 255);
  T_EQ(pc_seq_avg_code(0, 3, pc_seq_recip(3), 65536, -(1 << 20)), 0);
  //The largest legacy sum the UI path sees: 255 x 256 records
  T_EQ(pc_seq_avg_code(255 * 256, 256, pc_seq_recip(256), 65536, 0), 255);
}

int main(void)
{
  test_fit();
  test_registers();
  test_entries();
  test_ui_plan();
  test_division();
  test_avg_code();

  return t_report("test_seq");
}
