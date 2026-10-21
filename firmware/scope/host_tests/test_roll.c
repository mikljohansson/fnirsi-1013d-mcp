//----------------------------------------------------------------------------------------------------------------------------------
// test_roll.c - the roll mode's trace from the logger stream (fw-roll): roll.c's plan, column conversion, record and
// trigger on synthetic words, then end to end on fpga_model.c's 0x153D stream through fpga_log.c and pc_logring.c (the
// sequence pc_hal_scope.c's pc_glue_roll_start / _read run on the scope: 0x0D = the plan's divider, fpga_log_start with
// both channels in the legacy layout, drain, read, feed).
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdlib.h>

#include "test_util.h"
#include "fpga_model.h"
#include "fpga_bus.h"
#include "fpga_log.h"
#include "pc_logring.h"
#include "roll.h"

static uint8_t buf1[ROLL_LEN], buf2[ROLL_LEN];
static uint8_t words[4 * 20000];
static uint8_t ring_mem[32768];

static const roll_corr unity = { 65536, 65536, 0, 0, 0 };

//One column of W words: CH1 core B b1, core A a1, CH2 core B b2, core A a2 in every word
static uint32_t put_col(uint8_t *p, uint32_t wpc, uint8_t b1, uint8_t a1, uint8_t b2, uint8_t a2)
{
  uint32_t i;

  for(i=0;i<wpc;i++, p += 4)
  {
    p[0] = b1; p[1] = a1; p[2] = b2; p[3] = a2;
  }

  return wpc;
}

//----------------------------------------------------------------------------------------------------------------------------------

static void test_plan(void)
{
  //time/div in ms of timeperdiv 0..8 (variables.c time_div_texts)
  static const uint32_t tdiv_ms[ROLL_TB_COUNT] = { 50000, 20000, 10000, 5000, 2000, 1000, 500, 200, 100 };
  uint32_t div, wpc, t;

  for(t=0;t<ROLL_TB_COUNT;t++)
  {
    T_EQ(roll_plan((uint8_t)t, &div, &wpc), 0);

    //A column (one pixel, 50 per division) is exactly W words of 2 (D + 1) x 5 ns
    T_EQ((uint64_t)wpc * (div + 1) * 10, (uint64_t)tdiv_ms[t] * 1000000 / ROLL_PX_PER_DIV);

    //The 0x0D compare is 20 bits (PC_DIVIDER_MAX); two 8-bit channels within the logger's 250 kS/s tier
    T_TRUE(div <= 0xFFFFF);
    T_TRUE(400 * 1 * 2 <= div + 1);

    //At least 8 samples per channel per column (the boxcar), at most 2000 words per second (bus cost about 2.4 ms/s)
    T_TRUE(2 * wpc >= 8);
    T_TRUE((uint64_t)100000000 / (div + 1) <= 2000);
  }

  T_EQ(roll_plan(9, &div, &wpc), -1);
  T_EQ(roll_plan(10, &div, &wpc), -1);
  T_EQ(roll_plan(11, &div, &wpc), -1);
}

static void test_code(void)
{
  roll_corr c = unity;

  //The mean of both cores over the column, rounded half up
  T_EQ(roll_code(100 * 4, 100 * 4, 4, &c), 100);
  T_EQ(roll_code(100 * 4, 101 * 4, 4, &c), 101);              //100.5 -> 101
  T_EQ(roll_code(400 + 1, 400, 4, &c), 100);                  //100.125
  T_EQ(roll_code(0, 0, 4, &c), 0);
  T_EQ(roll_code(255 * 100, 255 * 100, 100, &c), 255);

  //Per-core correction: core A gain 1.5, core B offset -10 codes; the limits; inversion after the limits
  c.gain_a = 98304;
  c.off_b  = -10 * 65536;
  T_EQ(roll_code(100 * 8, 100 * 8, 8, &c), 120);              //(150 + 90) / 2
  c.gain_a = 65536;
  c.off_a  = c.off_b = -200 * 65536;
  T_EQ(roll_code(100 * 8, 100 * 8, 8, &c), 0);
  c.off_a  = c.off_b = 200 * 65536;
  T_EQ(roll_code(100 * 8, 100 * 8, 8, &c), 255);
  c.off_a  = c.off_b = 0;
  c.invert = 1;
  T_EQ(roll_code(40 * 8, 40 * 8, 8, &c), 215);
  T_EQ(roll_code(255 * 8, 255 * 8, 8, &c), 0);

  //100 words of 255 at gain 1.5 (the 50 s/div column): no overflow
  c.invert = 0;
  c.gain_a = c.gain_b = 98304;
  T_EQ(roll_code(255 * 100, 255 * 100, 100, &c), 255);
  c.gain_a = c.gain_b = 32768;
  T_EQ(roll_code(255 * 100, 255 * 100, 100, &c), 128);       //127.5 -> 128
}

//Auto: columns from words split anywhere, the record slides once full
static void test_auto(void)
{
  roll_model m;
  roll_corr  c[2] = { unity, unity };
  uint32_t   n = 0, i, ev, added, total = 0;

  m.level = 0;
  roll_reset(&m, buf1, buf2, 4, ROLL_AUTO, 0, 0);
  T_EQ(m.state, ROLL_RUN);

  //Column i: CH1 cores i and i + 2 (mean i + 1), CH2 200 / 100 (mean 150)
  for(i=0;i<10;i++)
    n += put_col(words + 4 * n, 4, (uint8_t)i, (uint8_t)(i + 2), 200, 100);

  //Fed in odd pieces: 3 + 7 + 30 words
  ev  = roll_feed(&m, words, 3, 0, c, &added);       total += added;
  T_EQ(added, 0); T_EQ(m.nw, 3);
  ev |= roll_feed(&m, words + 12, 7, 0, c, &added);  total += added;
  T_EQ(added, 2); T_EQ(m.nw, 2);
  ev |= roll_feed(&m, words + 40, 30, 0, c, &added); total += added;
  T_EQ(total, 10); T_EQ(m.count, 10); T_EQ(ev, 0);

  for(i=0;i<10;i++)
  {
    T_EQ(buf1[i], i + 1);
    T_EQ(buf2[i], 150);
  }

  //A gap drops the column being built: 2 words, a gap, then 4 words make one column of the 4 after the gap
  n = put_col(words, 2, 50, 50, 50, 50);
  roll_feed(&m, words, n, 0, c, &added);
  T_EQ(added, 0);
  n = put_col(words, 4, 20, 20, 30, 30);
  roll_feed(&m, words, n, 1, c, &added);
  T_EQ(added, 1); T_EQ(m.gaps, 1); T_EQ(m.count, 11);
  T_EQ(buf1[10], 20); T_EQ(buf2[10], 30);

  //Full: the oldest go; the newest is last
  for(i=0;i<ROLL_LEN;i++)
  {
    n = put_col(words, 4, (uint8_t)(i & 0xFF), (uint8_t)(i & 0xFF), 7, 7);
    roll_feed(&m, words, n, 0, c, &added);
  }

  T_EQ(m.count, ROLL_LEN);
  T_EQ(buf1[ROLL_LEN - 1], (ROLL_LEN - 1) & 0xFF);
  T_EQ(buf1[ROLL_LEN - 2], (ROLL_LEN - 2) & 0xFF);
  T_EQ(buf1[0], 0);                                           //the 11 columns before went: i = 0 is the oldest
  T_EQ(m.state, ROLL_RUN);

  //Viewer time bases (no plan) and EXT: Auto whatever the mode
  roll_reset(&m, buf1, buf2, 4, ROLL_NORMAL, 2, 0);
  T_EQ(m.mode, ROLL_AUTO); T_EQ(m.trig_ch, ROLL_NO_TRIGGER); T_EQ(m.state, ROLL_RUN);
}

//Single / Normal: the crossing of the trigger level in the edge's direction starts the record
static void test_trigger(void)
{
  roll_model m;
  roll_corr  c[2] = { unity, unity };
  uint32_t   n = 0, i, ev, added;

  //Rising on CH1 at 150: 100 100 140 160 ...: the record starts at the 160 column
  m.level = 150;
  roll_reset(&m, buf1, buf2, 2, ROLL_NORMAL, 0, 0);
  T_EQ(m.state, ROLL_WAIT);
  n += put_col(words + 4 * n, 2, 100, 100, 9, 9);
  n += put_col(words + 4 * n, 2, 100, 100, 9, 9);
  n += put_col(words + 4 * n, 2, 140, 140, 9, 9);
  n += put_col(words + 4 * n, 2, 160, 160, 8, 8);
  n += put_col(words + 4 * n, 2, 170, 170, 7, 7);
  ev = roll_feed(&m, words, n, 0, c, &added);
  T_EQ(ev, ROLL_EV_TRIG); T_EQ(added, 2); T_EQ(m.state, ROLL_RUN);
  T_EQ(buf1[0], 160); T_EQ(buf2[0], 8); T_EQ(buf1[1], 170);

  //Equal to the level counts as reached (prev < level, now >= level); falling needs prev >= level, now below
  m.level = 150;
  roll_reset(&m, buf1, buf2, 1, ROLL_SINGLE, 0, 0);
  n = 0;
  n += put_col(words + 4 * n, 1, 149, 149, 0, 0);
  n += put_col(words + 4 * n, 1, 150, 150, 0, 0);
  roll_feed(&m, words, n, 0, c, &added);
  T_EQ(added, 1); T_EQ(buf1[0], 150);

  roll_reset(&m, buf1, buf2, 1, ROLL_NORMAL, 0, 1);
  n = 0;
  n += put_col(words + 4 * n, 1, 140, 140, 0, 0);            //below: no falling crossing into it from nothing
  n += put_col(words + 4 * n, 1, 160, 160, 0, 0);            //rising: not this edge
  n += put_col(words + 4 * n, 1, 150, 150, 0, 0);            //still >= level
  n += put_col(words + 4 * n, 1, 149, 149, 0, 0);            //falling crossing
  roll_feed(&m, words, n, 0, c, &added);
  T_EQ(added, 1); T_EQ(buf1[0], 149);

  //On CH2, inverted: the crossing is on the drawn (inverted) codes
  c[1].invert = 1;
  roll_reset(&m, buf1, buf2, 1, ROLL_NORMAL, 1, 0);
  n = 0;
  n += put_col(words + 4 * n, 1, 0, 0, 200, 200);            //drawn 55
  n += put_col(words + 4 * n, 1, 0, 0, 50, 50);              //drawn 205: rising through 150
  roll_feed(&m, words, n, 0, c, &added);
  T_EQ(added, 1); T_EQ(buf2[0], 205);
  c[1].invert = 0;

  //A gap between the two columns: no crossing across it
  roll_reset(&m, buf1, buf2, 1, ROLL_NORMAL, 0, 0);
  n = put_col(words, 1, 100, 100, 0, 0);
  roll_feed(&m, words, n, 0, c, &added);
  n = put_col(words, 1, 200, 200, 0, 0);
  ev = roll_feed(&m, words, n, 1, c, &added);
  T_EQ(ev, 0); T_EQ(added, 0); T_EQ(m.state, ROLL_WAIT);

  //Single: full after ROLL_LEN columns from the crossing, then nothing more
  roll_reset(&m, buf1, buf2, 1, ROLL_SINGLE, 0, 0);
  n = 0;
  n += put_col(words + 4 * n, 1, 100, 100, 0, 0);
  for(i=0;i<ROLL_LEN + 10;i++)
    n += put_col(words + 4 * n, 1, 200, 200, (uint8_t)i, (uint8_t)i);
  ev = roll_feed(&m, words, n, 0, c, &added);
  T_EQ(ev, ROLL_EV_TRIG | ROLL_EV_DONE); T_EQ(added, ROLL_LEN); T_EQ(m.count, ROLL_LEN); T_EQ(m.state, ROLL_DONE);
  T_EQ(buf2[ROLL_LEN - 1], (ROLL_LEN - 1) & 0xFF);
  T_EQ(roll_feed(&m, words, 4, 0, c, &added), 0); T_EQ(added, 0);

  //Normal: full, then empty and waiting; the next crossing (a fall and a rise later) starts the next record
  roll_reset(&m, buf1, buf2, 1, ROLL_NORMAL, 0, 0);
  n = 0;
  n += put_col(words + 4 * n, 1, 100, 100, 0, 0);
  for(i=0;i<ROLL_LEN;i++)
    n += put_col(words + 4 * n, 1, 200, 200, 0, 0);
  n += put_col(words + 4 * n, 1, 200, 200, 0, 0);            //waiting: still high, no crossing
  n += put_col(words + 4 * n, 1, 100, 100, 0, 0);
  n += put_col(words + 4 * n, 1, 180, 180, 0, 0);            //the crossing
  ev = roll_feed(&m, words, n, 0, c, &added);
  T_EQ(ev, ROLL_EV_TRIG | ROLL_EV_RESTART); T_EQ(m.count, 1); T_EQ(buf1[0], 180); T_EQ(m.state, ROLL_RUN);
  T_EQ(added, ROLL_LEN + 1);
}

//----------------------------------------------------------------------------------------------------------------------------------
// End to end on the model's stream: the words carry their write index (lane 0x20 + l = byte l), so each column's
// expected code follows from the FPGA word counter of its first word

static uint8_t idx_code(uint64_t w0, uint32_t wpc, int ch)
{
  uint32_t sa = 0, sb = 0, i;

  for(i=0;i<wpc;i++)
  {
    uint32_t n = (uint32_t)(w0 + i);

    sa += (n >> (16 * ch)) & 0xFF;               //core A: lane 0x20 / 0x22
    sb += (n >> (16 * ch + 8)) & 0xFF;           //core B: lane 0x21 / 0x23
  }

  return (uint8_t)roll_code(sa, sb, wpc, &unity);
}

typedef struct
{
  uint32_t bad, columns, gaps;
} e2e_check;

//What roll_view_pass does each pass: read everything (draining as it goes), feed; then check the new columns against the
//word counter of their first word (the column in progress began m->nw words before this chunk, or at it after a gap)
static void e2e_pass(fpga_log *l, roll_model *m, e2e_check *k)
{
  roll_corr c[2] = { unity, unity };
  uint8_t   chunk[8192];
  uint32_t  n, added, i, j;
  uint64_t  w, start;
  uint8_t   f;
  int       gap;

  for(j=0;j<16;j++)
  {
    n = pc_logring_read(&l->ring, chunk, sizeof(chunk), &w, &f);

    if(!n)
    {
      if(!fpga_log_drain(l))
        break;
      continue;
    }

    gap   = (f & PC_LOG_CF_GAP) != 0;
    start = gap ? w : w - m->nw;
    k->gaps += (uint32_t)gap;
    roll_feed(m, chunk, n / 4, gap, c, &added);

    for(i=0;i<added;i++)
    {
      uint64_t w0 = start + (uint64_t)i * m->wpc;
      uint32_t at = m->count - added + i;

      if((buf1[at] != idx_code(w0, m->wpc, 0)) || (buf2[at] != idx_code(w0, m->wpc, 1)))
        k->bad++;
    }

    k->columns += added;
  }
}

static void test_stream(void)
{
  fpga_log   l;
  roll_model m;
  e2e_check  k;
  uint32_t   div, wpc, pass;

  //100 ms/div: D 49999, 4 words per column (2 ms)
  T_EQ(roll_plan(8, &div, &wpc), 0);
  fm_reset(0x153D, div, 77);
  fm.capture = 1;

  m.level = 0;
  roll_reset(&m, buf1, buf2, wpc, ROLL_AUTO, 0, 0);
  memset(&k, 0, sizeof(k));
  T_EQ(fpga_log_start(&l, 3, FPGA_LOG_F_LEGACY, 0, ring_mem, sizeof(ring_mem), 100000), 0);
  T_EQ(l.nlanes, 4); T_EQ(l.lanes[0], 0x21); T_EQ(l.lanes[1], 0x20); T_EQ(l.lanes[2], 0x23); T_EQ(l.lanes[3], 0x22);

  //Passes of 30 ms for 1 s: 500 columns, each the boxcar of its 4 words, consecutive
  for(pass=0;pass<34;pass++)
  {
    fm_advance_ns(30000000);
    e2e_pass(&l, &m, &k);
  }

  T_EQ(k.bad, 0);
  T_EQ(k.gaps, 0);
  T_EQ(l.consumed, fm.n);                                      //everything the FPGA wrote was taken
  T_EQ(k.columns, (uint32_t)(l.consumed / wpc));
  T_TRUE(k.columns >= 505 && k.columns <= 512);               //1.02 s at 2 ms per column
  T_EQ(m.count, k.columns);

  //A 5 s stall (a modal menu): the FPGA ring (8192 words, 4.1 s) laps: one gap, the trace goes on from the newest words
  fm_advance_ns(2000000000u);
  fm_advance_ns(2000000000u);
  fm_advance_ns(1000000000u);
  e2e_pass(&l, &m, &k);
  fm_advance_ns(30000000);
  e2e_pass(&l, &m, &k);
  T_EQ(k.gaps, 1); T_EQ(m.gaps, 1); T_EQ(l.gaps, 1);
  T_EQ(k.bad, 0);
  T_EQ(l.consumed, fm.n);

  //The release (what every arm does first): 0x69 = 0, no tail read
  fm.ntrace = 0;
  fpga_log_stop(&l, FPGA_LOG_ENDED);
  T_EQ(fm.reg_69, 0); T_EQ(fm.trace[0].cmd, 0x69); T_EQ(fm.ntrace, 1);
}

int main(void)
{
  test_plan();
  test_code();
  test_auto();
  test_trigger();
  test_stream();
  return t_report("test_roll");
}
