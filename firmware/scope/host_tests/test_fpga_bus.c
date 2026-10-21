//----------------------------------------------------------------------------------------------------------------------------------
// test_fpga_bus.c - the bus sequences of fpga_bus.c (fw 0.2.72) against fpga_model.c:
//   - every readout returns ring words start .. start + count - 1 exactly (lanes, ring wrap, 16-bit pairs, the 1.00o5
//     two-pass trace read), while the model without the dummy strobe returns mem[S], mem[S], mem[S + 1] (finding 5);
//   - the arm order: fpga_arm_reset never loses a changed pretrigger on the model, at any rate or strobe timing (the
//     1.00o5 order did on 0x1532..0x1536 at slow legacy rates: the model of 0x1535 still reproduces that race, as the
//     reason the order is what it is); the write trace shows 0x0B / 0x0C before 0x01 = 1 and again inside the window;
//   - the bounded ready wait;
//   - fw 0.2.87 (firmware/AGENTS.md section 57): the values the FPGA holds (0x0D, 0x53, 0x54, 0x56) follow every write, and
//     fpga_decim_write writes 0x54 at every arm with the filter on, so the shift always matches the divider the FPGA holds
//     (fpga/al3/AGENTS.md "First-record saturation": S 9 / 7 armed at D 4095 saturated every word);
//   - fw 0.2.97 (firmware/AGENTS.md section 64): records that end on the auto time-out or a forced stop on the
//     built-for bitstream 0x153B (the model's capture logic, fpga/al3/README.md section 11): the forced stop reads the
//     newest total words, a triggered FPGA-auto record completes; the model of 0x153A shows the fault that was fixed;
//   - fw-c5 (0x153C, protocol 1.26, firmware/AGENTS.md section 69): the trigger extension registers (fpga_trig_write,
//     fpga_trig_arm): every type's arm sequence (0x5E, and 0x5F / 0x60 for the qualified types, inside the 0x01 = 1
//     window after the geometry and before 0x0E), the 0x66 one-shot consumed by the next 0x01 = 1, the hold-off masking
//     a trigger across a re-arm, released by 0x66 + arm or by 0x1B = 0, and the 0x34 / 0x37 status reads.
//----------------------------------------------------------------------------------------------------------------------------------

#include "test_util.h"
#include "fpga_bus.h"
#include "fpga_model.h"
#include "pc_decim.h"
#include <string.h>

//The model's bitstreams the firmware's sequences are run against: the built-for 0x153C and its predecessors with the
//same bus (the firmware runs on 0x153C only; the older models show the faults the order and the dummy strobe answer)
static const uint16_t versions[] = { 0x1535, 0x1537, 0x1539, 0x153A, 0x153B, 0x153C };
#define NVER (sizeof(versions) / sizeof(versions[0]))

//One lane read against the memory, from a start near the ring end so it wraps
static void test_readout(void)
{
  uint32_t i, v, lane, k;
  uint8_t  buf[3000];
  uint16_t w16[600];

  for(v=0;v<NVER;v++)
  {
    fm_reset(versions[v], 0, 11 + v);
    fm_fill(v);

    for(k=0;k<3;k++)
    {
      uint32_t start = (k == 0) ? 0 : ((k == 1) ? 100 : fm.ring - 700);
      int      ok = 1;

      for(lane=0;lane<4;lane++)
      {
        fpga_read_lane((uint8_t)(0x20 + lane), start, 1500, buf, 1, fm.ring);

        for(i=0;i<1500;i++)
          if(buf[i] != fm.mem[(start + i) % fm.ring][lane])
            ok = 0;
      }

      T_TRUE(ok);
    }

    //Stride 2 (READ_RAW layout 1): both lanes of CH1 interleaved
    fpga_read_lane(0x21, 7, 500, buf, 2, fm.ring);
    fpga_read_lane(0x20, 7, 500, buf + 1, 2, fm.ring);
    {
      int ok = 1;

      for(i=0;i<500;i++)
        if((buf[2 * i] != fm.mem[7 + i][1]) || (buf[2 * i + 1] != fm.mem[7 + i][0]))
          ok = 0;

      T_TRUE(ok);
    }

    //16-bit layout: low byte from 0x22, high byte from 0x23 of the same word
    fpga_read_decim_data(0x22, 0x23, 300, 600, w16);
    {
      int ok = 1;

      for(i=0;i<600;i++)
        if(w16[i] != (uint16_t)(fm.mem[300 + i][2] | (fm.mem[300 + i][3] << 8)))
          ok = 0;

      T_TRUE(ok);
    }

    //The 1.00o5 trace read (fpga_read_sample_data): two passes from the same start, core A into the odd slots first,
    //core B into the even ones; word i of the record must give B at 2i, A at 2i + 1
    {
      uint32_t start = 1234 % fm.ring;
      int ok = 1;

      fpga_read_start(0x22, (uint16_t)start);
      for(i=0;i<1000;i++)
        buf[2 * i + 1] = fpga_io_strobe_read();
      fpga_read_start(0x23, (uint16_t)start);
      for(i=0;i<1000;i++)
        buf[2 * i] = fpga_io_strobe_read();

      for(i=0;i<1000;i++)
        if((buf[2 * i] != fm.mem[start + i][3]) || (buf[2 * i + 1] != fm.mem[start + i][2]))
          ok = 0;

      T_TRUE(ok);
    }
  }

  //The model has the lag: without the dummy strobe (the 1.00o5 readouts up to fw 0.2.69) the first byte repeats and
  //everything after it is one word late
  fm_reset(0x153B, 0, 5);
  fm_fill(3);
  fpga_write_cmd(0x1F);
  fpga_write_short(40);
  fpga_write_cmd(0x20);
  fpga_io_read_mode();
  for(i=0;i<100;i++)
    buf[i] = fpga_io_strobe_read();
  T_EQ(buf[0], fm.mem[40][0]);
  T_EQ(buf[1], fm.mem[40][0]);
  T_EQ(buf[2], fm.mem[41][0]);
  T_EQ(buf[99], fm.mem[138][0]);
}

//The 1.00o5 arm order up to fw 0.2.69 on 0x1532..0x1535 (fpga_do_conversion, pc_hal_arm): geometry only after the 0x05 poll
static void legacy_arm(uint16_t pre, uint16_t total)
{
  fpga_write_cmd(0x01);
  fpga_write_byte(0x01);
  fpga_write_cmd(0x05);
  while((fpga_read_byte() & 1) == 0)
    ;
  fpga_write_cmd(0x0B);
  fpga_write_short(pre);
  fpga_write_cmd(0x0C);
  fpga_write_short(total);
  fpga_write_cmd(0x0E);
  fpga_write_int(3000);
  fpga_write_cmd(0x01);
  fpga_write_byte(0x00);
}

static void arm_once(uint16_t pre, uint16_t total)
{
  fpga_arm_cfg c;

  c.write_0e     = 1;
  c.pre_words    = pre;
  c.total_words  = total;
  c.auto_timeout = 3000;
  c.max_polls    = 0;
  c.idle         = 0;
  T_EQ(fpga_arm_reset(&c), 0);
}

static void test_arm_order(void)
{
  static const uint32_t dividers[] = { 0, 9, 199, 399, 1999 };
  uint32_t v, d, run;
  uint32_t legacy_lost_1535 = 0, legacy_lost_1537 = 0, lost = 0, runs = 0;

  for(v=0;v<NVER;v++)
  {
    for(d=0;d<5;d++)
    {
      fm_reset(versions[v], dividers[d], 1000 * v + d + 1);

      for(run=0;run<200;run++)
      {
        //Alternate pretriggers as the bench check does (30 / 50 words, plus a random one)
        uint16_t pre = (uint16_t)((run % 3 == 0) ? 30 : ((run % 3 == 1) ? 50 : 20 + (fm.seed >> 9) % 400));

        fm_advance_ns(fm.seed % 50000);
        fm.ntrace = 0;
        arm_once(pre, 1500);
        fm_advance_ns(30000);           //the capture: the first edges after the release

        runs++;
        if(fm.pre_applied != pre)
          lost++;
      }
    }
  }

  T_EQ(lost, 0);
  T_TRUE(runs >= 5 * 200 * NVER);

  //The model reproduces the race of finding 2 with the old order, and 0x1537's fix of it
  for(d=2;d<5;d++)
  {
    fm_reset(0x1535, dividers[d], 77 + d);
    for(run=0;run<200;run++)
    {
      uint16_t pre = (uint16_t)((run & 1) ? 30 : 50);

      fm_advance_ns(fm.seed % 50000);
      legacy_arm(pre, 1500);
      fm_advance_ns(50000);
      legacy_lost_1535 += (fm.pre_applied != pre);
    }

    fm_reset(0x1537, dividers[d], 77 + d);
    for(run=0;run<200;run++)
    {
      uint16_t pre = (uint16_t)((run & 1) ? 30 : 50);

      fm_advance_ns(fm.seed % 50000);
      legacy_arm(pre, 1500);
      fm_advance_ns(50000);
      legacy_lost_1537 += (fm.pre_applied != pre);
    }
  }

  printf("  arm order: %u runs, %u lost; the 1.00o5 order loses %u of 600 on 0x1535 at D 199..1999, %u on 0x1537\n",
         (unsigned)runs, (unsigned)lost, (unsigned)legacy_lost_1535, (unsigned)legacy_lost_1537);
  T_TRUE(legacy_lost_1535 > 100);
  T_EQ(legacy_lost_1537, 0);
}

static void test_arm_trace(void)
{
  int i0b, i01, i0b2, i01b;
  uint32_t v;

  for(v=0;v<NVER;v++)
  {
    fm_reset(versions[v], 199, 5 + v);
    fm.ntrace = 0;
    arm_once(123, 456);

    i01  = fm_trace_find(0x01, 1, 0);
    i01b = fm_trace_find(0x01, 0, 0);
    i0b  = fm_trace_find(0x0B, 123, 0);
    T_TRUE(i01 >= 0);
    T_TRUE(i01b > i01);

    //Geometry before the reset, then again inside the window, 0x0E inside, release last
    T_TRUE(i0b >= 0 && i0b < i01);
    T_TRUE(fm_trace_find(0x0C, 456, 0) < i01);
    i0b2 = fm_trace_find(0x0B, 123, (uint32_t)i01);
    T_TRUE(i0b2 > i01 && i0b2 < i01b);
    T_TRUE(fm_trace_find(0x0C, 456, (uint32_t)i01) > i01);
    T_TRUE(fm_trace_find(0x0E, 3000, (uint32_t)i01) < i01b);
    T_EQ(i01b, (int)fm.ntrace - 1);
  }
}

static void test_ready_bound(void)
{
  fpga_arm_cfg c;
  uint32_t idles = 0;

  fm_reset(0x153B, 0, 9);
  fm.ready_never = 1;
  fm.ntrace = 0;

  memset(&c, 0, sizeof(c));
  c.pre_words = 10;
  c.max_polls = 1000;
  T_EQ(fpga_arm_reset(&c), -1);
  T_EQ(fm.ready_polls, 1000);
  T_EQ(fm.in_reset, 0);                                  //released again
  T_EQ(fm.trace[fm.ntrace - 1].cmd, 0x01);
  T_EQ(fm.trace[fm.ntrace - 1].value, 0);
  (void)idles;
}

//The UI record start (pc_glue_ui_read_start): exact at every trigger address, where 1.00o5's constants were not
static void test_ring_start(void)
{
  static const uint32_t rings[] = { 8192 };
  uint32_t r, a, pre, bad = 0, old_bad = 0;

  for(r=0;r<1;r++)
    for(pre=0;pre<2000;pre+=37)
      for(a=0;a<rings[r];a+=13)
      {
        uint32_t s = fpga_ring_start(a, pre, rings[r]);
        //1.00o5: the stock / AL3 / EF2 branches wrapped with ring - 751
        uint32_t old = (a < pre) ? a + rings[r] - 751 : a - pre;

        bad     += ((s + pre) % rings[r] != a) || (s >= rings[r]);
        old_bad += (old != s);
      }

  T_EQ(bad, 0);
  T_TRUE(old_bad > 0);
  T_EQ(fpga_ring_start(10, 750, 4096), 3356);
  T_EQ(fpga_ring_start(8191, 8192, 8192), 8191);
}

//The write record behind GET_LINK_STATS 64-66 and the encode check's choice (eh_capture_mode)
static void test_write_record(void)
{
  memset(&fpga_bus_wr, 0, sizeof(fpga_bus_wr));
  T_EQ(fpga_bus_note_write(0x0D, 0), 1);  T_EQ(fpga_bus_wr.since_reset, 1);
  T_EQ(fpga_bus_note_write(0x01, 1), 1);  T_EQ(fpga_bus_wr.since_reset, 0);   //the sampling reset clears 0x33 bit 0
  T_EQ(fpga_bus_note_write(0x01, 0), 0);  T_EQ(fpga_bus_wr.since_reset, 0);   //the release does not
  T_EQ(fpga_bus_note_write(0x53, 2), 1);  T_EQ(fpga_bus_wr.since_reset, 1);
  T_EQ(fpga_bus_note_write(0x0B, 50), 0); T_EQ(fpga_bus_wr.since_reset, 1);
  T_EQ(fpga_bus_note_write(0x56, 1), 1);
  T_EQ(fpga_bus_wr.rate, 1); T_EQ(fpga_bus_wr.filter, 2); T_EQ(fpga_bus_wr.resets, 1);

  //fw 0.2.87: 0x54 / 0x55 are traced and counted as filter writes but leave since_reset alone; the held values
  T_EQ(fpga_bus_note_write(0x01, 1), 1);  T_EQ(fpga_bus_wr.since_reset, 0);
  T_EQ(fpga_bus_note_write(0x54, 15), 1); T_EQ(fpga_bus_wr.since_reset, 0);
  T_EQ(fpga_bus_note_write(0x55, 0x1234), 1); T_EQ(fpga_bus_wr.since_reset, 0);
  T_EQ(fpga_bus_wr.filter, 4);
  T_EQ(fpga_bus_wr.held, FPGA_HELD_DIVIDER | FPGA_HELD_MODE | FPGA_HELD_SHIFT | FPGA_HELD_FIR);
  T_EQ(fpga_bus_wr.divider, 0); T_EQ(fpga_bus_wr.mode, 2); T_EQ(fpga_bus_wr.shift, 15); T_EQ(fpga_bus_wr.fir, 1);
  T_EQ(fpga_bus_note_write(0x0D, 4095), 1); T_EQ(fpga_bus_wr.divider, 4095);
}

//An arm's decimation writes as pc_dec_apply makes them, at the divider the FPGA holds (pc_hal_divider_now)
static int decim_arm(uint8_t k_req, int fir, pc_decim_cfg *c)
{
  T_TRUE(fpga_bus_wr.held & FPGA_HELD_DIVIDER);
  pc_decim_config(k_req, fir ? PC_DECIM_SET_FIR : 0, fpga_bus_wr.divider, 4096, PC_DECIM_HAS_FIR, c);
  return fpga_decim_write(pc_decim_mode(c), 1, pc_decim_cmd56(c), c->shift);
}

static void write_0d(uint32_t d)
{
  fpga_write_cmd(0x0D);
  fpga_write_int(d);
}

//fw 0.2.87: fpga_decim_write against the model's registers
static void test_decim_write(void)
{
  pc_decim_cfg c, ref;
  int i;

  fm_reset(0x153B, 199, 21);
  T_EQ(fpga_bus_wr.held, 0);

  //The bench case: the host's raw 4095, then a UI time/div snap (no 0x0D: nothing reaches the bus), then CIC2 + FIR.
  //The divider the FPGA holds gives N 4096 and S 15; the UI table's 199 / 399 gave S 7 / 9 (every word 0xFFFF)
  write_0d(4095);
  T_EQ(fpga_bus_wr.divider, 4095); T_EQ(fm.divider, 4095);
  fm.ntrace = 0;
  T_EQ(decim_arm(PC_DECIM_CIC2, 1, &c), 1);
  T_EQ(c.n, 4096); T_EQ(c.k, 2); T_EQ(c.shift, 15);
  T_EQ(fm.reg_53, 0x02); T_EQ(fm.reg_56, 1); T_EQ(fm.reg_54, 15);
  T_TRUE(fm_trace_find(0x53, 0x02, 0) < fm_trace_find(0x54, 15, 0));
  pc_decim_config(PC_DECIM_CIC2, PC_DECIM_SET_FIR, 199, 4096, PC_DECIM_HAS_FIR, &ref);
  T_EQ(ref.shift, 7);
  pc_decim_config(PC_DECIM_CIC2, PC_DECIM_SET_FIR, 399, 4096, PC_DECIM_HAS_FIR, &ref);
  T_EQ(ref.shift, 9);

  //Unchanged: no settle, no 0x53 / 0x56, but 0x54 again
  fm.ntrace = 0;
  T_EQ(decim_arm(PC_DECIM_CIC2, 1, &c), 0);
  T_EQ(fm.ntrace, 1); T_EQ(fm.trace[0].cmd, 0x54); T_EQ(fm.trace[0].value, 15);

  //The firmware's record of the shift matches, the FPGA's register does not (a lost or foreign write): the arm fixes it
  fm.reg_54 = 9;
  T_EQ(decim_arm(PC_DECIM_CIC2, 1, &c), 0);
  T_EQ(fm.reg_54, 15);

  //A foreign 0x54 through the bus: tracked, so the arm rewrites it and settles
  fpga_write_cmd(0x54);
  fpga_write_byte(9);
  T_EQ(fpga_bus_wr.shift, 9);
  T_EQ(decim_arm(PC_DECIM_CIC2, 1, &c), 1);
  T_EQ(fm.reg_54, 15);

  //A divider change settles once, even with the same mode and shift
  write_0d(4094);
  T_EQ(decim_arm(PC_DECIM_CIC2, 1, &c), 1);
  T_EQ(decim_arm(PC_DECIM_CIC2, 1, &c), 0);

  //Filter off: 0x53 = 0, no 0x54
  fm.ntrace = 0;
  T_EQ(decim_arm(PC_DECIM_OFF, 0, &c), 1);
  T_EQ(fm.reg_53, 0); T_EQ(fm_trace_find(0x54, 15, 0), -1);

  //Bode-engine-like sequence (pc_bode.c bh_arm): a UI legacy conversion at D 19, then per step the engine's rate write,
  //its mode (CIC3 + FIR at N = 16 x 2^k, legacy at D 0) and the arm; between the steps the UI writes its own rate.
  //After each arm the model's 0x54 is the shift for the model's divider, written after the 0x0D and before 0x01 = 1.
  {
    static const uint32_t divs[] = { 4095, 4095, 1023, 255, 15, 0, 65535, 4095 };

    write_0d(19);
    (void)decim_arm(PC_DECIM_OFF, 0, &c);

    for(i=0;i<(int)(sizeof(divs) / sizeof(divs[0]));i++)
    {
      int i0d, i54, i01;

      fm.ntrace = 0;
      write_0d(divs[i]);
      (void)decim_arm(divs[i] ? PC_DECIM_CIC3 : PC_DECIM_OFF, divs[i] != 0, &c);
      arm_once(0, 4096);

      pc_decim_config(divs[i] ? PC_DECIM_CIC3 : PC_DECIM_OFF, divs[i] ? PC_DECIM_SET_FIR : 0, fm.divider, 4096,
                      PC_DECIM_HAS_FIR, &ref);
      T_EQ(fm.reg_53, pc_decim_mode(&ref));
      i0d = fm_trace_find(0x0D, divs[i], 0);
      i01 = fm_trace_find(0x01, 1, 0);

      if(ref.k)
      {
        i54 = fm_trace_find(0x54, ref.shift, 0);
        T_EQ(fm.reg_54, ref.shift); T_EQ(fm.reg_56, ref.fir_en);
        T_TRUE((i0d >= 0) && (i54 > i0d) && (i01 > i54));
      }

      //The UI writes its rate between two engine steps (a UI conversion's fpga_set_sample_rate)
      write_0d(199);
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// fw 0.2.97: records that end on the auto time-out or a forced stop (fpga/al3/README.md section 11, the model's capture
// logic). The words carry their write index, so a record is contiguous when its indices count up by one.

#define TO_TOTAL  2048

static uint32_t to_idx[TO_TOTAL];

static void to_arm(uint16_t pre, uint32_t reg0e, int normal)
{
  fpga_arm_cfg c;

  //As pc_hal_arm: the trigger mode, then the sampling reset with the geometry and 0x0E
  fpga_write_cmd(0x1A);
  fpga_write_byte(normal ? 0x01 : 0x00);

  memset(&c, 0, sizeof(c));
  c.pre_words    = pre;
  c.total_words  = TO_TOTAL;
  c.write_0e     = 1;
  c.auto_timeout = reg0e;
  T_EQ(fpga_arm_reset(&c), 0);
}

static void to_force(void)
{
  fpga_write_cmd(0x1A);
  fpga_write_byte(0x00);
}

static int to_done(void)
{
  fpga_write_cmd(0x0A);
  return fpga_read_byte() & 1;
}

static int to_wait(uint32_t max_us)
{
  uint32_t us;

  for(us = 0; us < max_us; us += 5)
  {
    if(to_done())
      return 1;

    fm_advance_ns(5000);
  }

  return to_done();
}

static int to_trig(void)
{
  fpga_write_cmd(0x18);
  return fpga_read_byte() & 1;
}

static uint16_t to_addr(void)
{
  uint16_t hi, lo;

  fpga_write_cmd(0x14);
  hi = fpga_read_byte();
  lo = fpga_read_byte();
  return (uint16_t)(((hi << 8) | lo) & 0x1FFF);
}

//Read the record as the firmware does (start = 0x14 - pre); 1 = contiguous. *newest: it ends at the stop
static int to_read(uint16_t pre, uint16_t addr, int *newest)
{
  static uint8_t b[4][TO_TOTAL];
  uint32_t start = fpga_ring_start(addr, pre, fm.ring), i, l;
  int ok = 1;

  for(l = 0; l < 4; l++)
    fpga_read_lane((uint8_t)(0x20 + l), start, TO_TOTAL, b[l], 1, fm.ring);

  for(i = 0; i < TO_TOTAL; i++)
  {
    to_idx[i] = (uint32_t)b[0][i] | ((uint32_t)b[1][i] << 8) | ((uint32_t)b[2][i] << 16) | ((uint32_t)b[3][i] << 24);

    if(i && (to_idx[i] != to_idx[i - 1] + 1))
      ok = 0;
  }

  *newest = (to_idx[TO_TOTAL - 1] + 1 == fm.stop_n);
  return ok;
}

static void to_reset(uint16_t version, uint32_t seed)
{
  fm_reset(version, 0, seed);
  fm.capture = 1;
  fm.trig_at = -1;
  fm_fill(seed);                                  //stale words that are not write indices
}

static void test_timeout_records(void)
{
  static const uint16_t pres[] = { 0, 200 };
  uint32_t p, run, spliced_old = 0, runs_old = 0;
  int newest;

  for(p = 0; p < 2; p++)
  {
    uint16_t pre = pres[p], addr;

    //1. A forced stop after 1.12 rings (normal arm, 0x1A = 0 at the firmware's deadline): 0x153B reads the newest
    //   total words. 0x153A read ring words 0 .. total - 1 across the stop (W mod 8192 about 1000: spliced, the bench
    //   fault fw 0.2.97 worked around and 0x153B fixed)
    to_reset(0x153B, 31 + p);
    to_arm(pre, 2 * TO_TOTAL, 1);
    fm_advance_ns((8192 + 1000) * 10);
    to_force();
    T_TRUE(to_wait(100)); T_EQ(to_trig(), 0);
    addr = to_addr();
    T_EQ(addr, (uint16_t)((fm.stop_n + 2 * 8192 - TO_TOTAL + pre) % 8192));
    T_TRUE(to_read(pre, addr, &newest)); T_TRUE(newest);

    to_reset(0x153A, 31 + p);
    to_arm(pre, 2 * TO_TOTAL, 1);
    fm_advance_ns((8192 + 1000) * 10);
    to_force();
    T_TRUE(to_wait(100)); T_EQ(to_trig(), 0);
    T_EQ(to_read(pre, to_addr(), &newest), 0);                //the fault 0x153B fixed

    //2. FPGA auto with 0x0E = total and a trigger at word total / 2: the post-trigger region completes (0x153A cut
    //   it at W = total + 2: the engine's incoherent records on 0.2.89)
    to_reset(0x153B, 41 + p);
    fm.trig_at = TO_TOTAL / 2;
    to_arm(pre, TO_TOTAL, 0);
    T_TRUE(to_wait(200)); T_EQ(to_trig(), 1);
    addr = to_addr();
    T_EQ(addr, TO_TOTAL / 2);
    T_TRUE(to_read(pre, addr, &newest)); T_EQ(to_idx[pre], TO_TOTAL / 2);

    //3. The forced stop at random times: always contiguous and the newest words
    for(run = 0; run < 24; run++)
    {
      uint32_t words = 8192 + (run * 2731u + p * 977u) % (3 * 8192);
      int ok_old;

      to_reset(0x153B, 100 + run + 50 * p);
      to_arm(pre, 2 * TO_TOTAL, 1);
      fm_advance_ns(words * 10);
      to_force();
      T_TRUE(to_wait(100));
      T_TRUE(to_read(pre, to_addr(), &newest)); T_TRUE(newest);

      to_reset(0x153A, 100 + run + 50 * p);
      to_arm(pre, 2 * TO_TOTAL, 1);
      fm_advance_ns(words * 10);
      to_force();
      T_TRUE(to_wait(100));
      ok_old = to_read(pre, to_addr(), &newest);
      T_EQ(ok_old, fm.stop_n % 8192 >= TO_TOTAL);
      spliced_old += !ok_old;
      runs_old++;
    }
  }

  //The Frequency response engine's arm (pc_bode.c bh_arm: pretrigger 0, FPGA auto, 0x0E = 2 x total samples = 4 x
  //total words). The trigger comes in the first period: at word 0 (the reset's trigger_previous = 0 against a first
  //sample above the level) or a few words in, or never: every record contiguous, 0x18 says which triggered
  {
    static const int64_t at[] = { 0, 37, -1 };

    for(run = 0; run < 3; run++)
    {
      uint16_t addr;
      int seen = 0;

      to_reset(0x153B, 200 + run);
      fm.trig_at = at[run];
      to_arm(0, 4 * TO_TOTAL, 0);

      //The engine's poll: 0x18 seen at any poll (bh_poll), until 0x0A
      while(!to_done())
      {
        seen |= to_trig();
        fm_advance_ns(1000);
      }
      seen |= to_trig();
      addr = to_addr();

      T_TRUE(to_read(0, addr, &newest));                       //every one contiguous
      T_EQ(seen, at[run] >= 0);                                 //triggered when it triggered
      T_TRUE(newest);

      if(run < 2)
        T_EQ(fm.stop_n, (uint32_t)at[run] + TO_TOTAL);          //complete long before the time-out
      else
        T_EQ(fm.stop_n, 4 * TO_TOTAL + 2);
    }
  }

  printf("  time-out records: the forced stop spliced %u of %u records on the 0x153A model (expected about %u), none on 0x153B\n",
         (unsigned)spliced_old, (unsigned)runs_old, (unsigned)(runs_old * TO_TOTAL / 8192));
  T_TRUE(spliced_old > 0);
}

//----------------------------------------------------------------------------------------------------------------------------------
// fw-c5 (0x153C): the trigger extension registers (fpga_bus.h "Trigger types and hold-off")

//The arm's write trace: 0x5E (and 0x5F / 0x60 for a qualified type) once, inside the window, after the second 0x0C and
//before 0x0E; never 0x1B or 0x66 in an arm
static void trig_arm_trace_check(uint8_t type)
{
  int i01 = fm_trace_find(0x01, 1, 0), i01b = fm_trace_find(0x01, 0, 0);
  int i0c2, i5e, i0e, i5f, i60;

  T_TRUE(i01 >= 0); T_TRUE(i01b > i01);
  i0c2 = fm_trace_find(0x0C, fm.reg_0c, (uint32_t)i01);
  i0e  = fm_trace_find(0x0E, fm.reg_0e, (uint32_t)i01);
  i5e  = fm_trace_find(0x5E, fm.reg_5e, 0);
  T_TRUE(i0c2 > i01); T_TRUE(i0e > i0c2); T_TRUE(i0e < i01b);
  T_TRUE(i5e > i0c2); T_TRUE(i5e < i0e);
  T_EQ(fm_trace_find(0x5E, fm.reg_5e, (uint32_t)i5e + 1), -1);               //once per arm
  T_EQ(fm_trace_find(0x1B, fm.reg_1b, 0), -1);
  T_EQ(fm_trace_find(0x66, 1, 0), -1);

  i5f = fm_trace_find(0x5F, fm.reg_5f, 0);
  i60 = fm_trace_find(0x60, fm.reg_60, 0);

  if(type == FPGA_TRIG_EDGE)
  {
    T_EQ(i5f, -1); T_EQ(i60, -1);                                              //the edge type writes only 0x5E
  }
  else
  {
    T_TRUE(i5f > i5e); T_TRUE(i60 > i5f); T_TRUE(i60 < i0e);                  //type, N, H, then 0x0E
  }
}

static void test_trig_ext(void)
{
  static const struct { uint8_t type, longer; uint32_t n, h, holdoff; } cases[] =
  {
    { FPGA_TRIG_EDGE,    0, 0,        0,       0 },
    { FPGA_TRIG_PULSE,   0, 1500,     0,       0 },             //shorter than 15 us at 100 MSa/s words
    { FPGA_TRIG_PULSE,   1, 0xFFFFFF, 0,       200000 },        //the largest N, a 1 ms hold-off
    { FPGA_TRIG_NTH,     0, 3,        100000,  0 },             //the third edge after a 1 ms idle time
    { FPGA_TRIG_NTH,     0, 65535,    0,       12 },
    { FPGA_TRIG_TIMEOUT, 0, 1,        0,       0xFFFFFFFFu },   //N = 1: the edge with one sample of context
  };
  fpga_trig_ext t;
  uint32_t c, v;
  uint8_t st[4];
  int i5e, i5f, i60, i1b, i66;

  for(v=0;v<NVER;v++)
  {
    for(c=0;c<sizeof(cases) / sizeof(cases[0]);c++)
    {
      fm_reset(versions[v], 199, 300 + c);
      memset(&t, 0, sizeof(t));
      t.type = cases[c].type; t.longer = cases[c].longer; t.n = cases[c].n; t.h = cases[c].h; t.holdoff = cases[c].holdoff;

      //SET_TRIGGER2: the registers at once, 0x1B, and the 0x66 request
      fm.ntrace = 0;
      fpga_trig_write(&t, 1);
      T_EQ(fm.reg_5e, (t.type & 7) | (t.longer ? 8 : 0));
      T_EQ(fm.reg_1b, t.holdoff);
      T_EQ(fm.ho_clr_req, 1);
      T_EQ(fpga_trig.type, t.type); T_EQ(fpga_trig.n, t.n); T_EQ(fpga_trig.h, t.h); T_EQ(fpga_trig.holdoff, t.holdoff);
      i5e = fm_trace_find(0x5E, fm.reg_5e, 0);
      i5f = fm_trace_find(0x5F, t.n, 0);
      i60 = fm_trace_find(0x60, t.h, 0);
      i1b = fm_trace_find(0x1B, t.holdoff, 0);
      i66 = fm_trace_find(0x66, 1, 0);
      T_EQ(i5e, 0); T_TRUE(i1b > i5e); T_TRUE(i66 > i1b); T_EQ(i66, (int)fm.ntrace - 1);

      if(t.type == FPGA_TRIG_EDGE)
      {
        T_EQ(i5f, -1); T_EQ(i60, -1); T_EQ(fm.ntrace, 3);
      }
      else
      {
        T_EQ(fm.reg_5f, t.n); T_EQ(fm.reg_60, t.h);
        T_TRUE(i5f > i5e); T_TRUE(i60 > i5f); T_TRUE(i1b > i60); T_EQ(fm.ntrace, 5);
      }

      //A write without the clear request leaves 0x66 alone
      fm.ho_clr_req = 0;
      fm.ntrace = 0;
      fpga_trig_write(&t, 0);
      T_EQ(fm_trace_find(0x66, 1, 0), -1);
      T_EQ(fm.ho_clr_req, 0);
      fm.ho_clr_req = 1;

      //The arm: the type registers inside the window, the one-shot consumed by the 0x01 = 1
      fm.ntrace = 0;
      arm_once(123, 456);
      trig_arm_trace_check(t.type);
      T_EQ(fm.ho_clr_req, 0);
      T_EQ(fm.reg_5e, (t.type & 7) | (t.longer ? 8 : 0));                    //re-written with the same value
      T_EQ(fm.reg_1b, t.holdoff);                                             //untouched by the arm

      //A second arm without a new 0x66: the request stays consumed
      fm.ntrace = 0;
      arm_once(123, 456);
      trig_arm_trace_check(t.type);
      T_EQ(fm.ho_clr_req, 0);

      //0x37 / 0x34 on the built-for model: type bit 2 never set here, the hold-off idle, the qualifier at its reset
      //state (Nth edge: N - 1 edges still to count, counting when H = 0); older models answer 0
      fpga_trig_status(st);
      if(versions[v] >= 0x153C)
      {
        uint32_t n16 = t.n & 0xFFFF, rem = (t.type == FPGA_TRIG_NTH) ? ((n16 ? n16 : 1) - 1) : 0;

        T_EQ(st[0], 0);
        T_EQ(st[1], (uint8_t)(((t.type == FPGA_TRIG_NTH) && !t.h) << 5) | (t.longer ? 0x04 : 0) | (t.type & 3));
        T_EQ(((uint32_t)st[2] << 8) | st[3], rem);
      }
      else
      {
        T_EQ(st[0], 0); T_EQ(st[1], 0); T_EQ(st[2], 0); T_EQ(st[3], 0);
      }
    }
  }

  //The hold-off on the capture logic (the 0x153C model, divider 0: 10 ns per word): a trigger starts it, a re-arm's
  //trigger inside it is not taken (the FPGA auto time-out ends that record untriggered), 0x66 + arm clears it, 0x1B =
  //0 releases it at once, and it runs out by itself; 0x37 bit 4 follows
  {
    static const uint32_t holdoffs[] = { 200000, 40000, 10000 };    //1 ms, 200 us, 50 us
    uint32_t h;

    for(h=0;h<3;h++)
    {
      uint32_t ho_ns = holdoffs[h] * 5;

      to_reset(0x153C, 500 + h);
      memset(&t, 0, sizeof(t));
      t.holdoff = holdoffs[h];
      fpga_trig_write(&t, 1);                                                 //clear with the first arm (a new session)

      //1. the first record triggers and starts the hold-off
      fm.trig_at = 40;
      to_arm(10, 4 * TO_TOTAL, 0);
      T_TRUE(to_wait(200)); T_EQ(to_trig(), 1);
      fpga_trig_status(st); T_EQ(st[0] & 0x10, 0x10);
      T_EQ(fm.ho_blocked, 0);

      //2. a re-arm at once (no 0x66): its trigger falls inside the hold-off and is not taken; the FPGA auto time-out
      //   (0x0E = 4 x total words = 2 x 2 x 400 words, 16 us at 10 ns) ends the record untriggered
      fm.trig_at = 40;
      to_arm(10, 4 * TO_TOTAL, 0);
      T_TRUE(to_wait(200)); T_EQ(to_trig(), 0);
      T_EQ(fm.ho_blocked, 1);
      T_EQ(to_addr(), (uint16_t)((fm.stop_n + 2 * 8192 - TO_TOTAL + 10) % 8192));   //0x153B's untriggered address

      //3. 0x66 = 1 before the next arm: the hold-off is cleared by that 0x01 = 1 and the trigger is taken again
      fpga_write_cmd(0x66); fpga_write_byte(0x01);
      fm.trig_at = 40;
      to_arm(10, 4 * TO_TOTAL, 0);
      T_TRUE(to_wait(200)); T_EQ(to_trig(), 1);
      T_EQ(fm.ho_blocked, 1);
      fpga_trig_status(st); T_EQ(st[0] & 0x10, 0x10);

      //4. 0x1B = 0 releases the running hold-off at once (fpga/al3/AGENTS.md deviation 1)
      t.holdoff = 0;
      fpga_trig_write(&t, 0);
      fpga_trig_status(st); T_EQ(st[0] & 0x10, 0);
      fm.trig_at = 40;
      to_arm(10, 4 * TO_TOTAL, 0);
      T_TRUE(to_wait(200)); T_EQ(to_trig(), 1);
      T_EQ(fm.ho_blocked, 1);

      //5. back on once the timer of step 3 has run out (the FPGA keeps counting while 0x1B = 0 masks nothing, so a
      //   value written back inside that time would mask again): a trigger starts it, and it runs out by itself after
      //   the hold-off time (no clear needed)
      fm_advance_ns(ho_ns + 1000);
      t.holdoff = holdoffs[h];
      fpga_trig_write(&t, 0);
      fpga_trig_status(st); T_EQ(st[0] & 0x10, 0);
      fm.trig_at = 40;
      to_arm(10, 4 * TO_TOTAL, 0);
      T_TRUE(to_wait(200)); T_EQ(to_trig(), 1);
      fpga_trig_status(st); T_EQ(st[0] & 0x10, 0x10);
      fm_advance_ns(ho_ns + 1000);
      fpga_trig_status(st); T_EQ(st[0] & 0x10, 0);
      fm.trig_at = 40;
      to_arm(10, 4 * TO_TOTAL, 0);
      T_TRUE(to_wait(200)); T_EQ(to_trig(), 1);
      T_EQ(fm.ho_blocked, 1);
    }

    //The hold-off is not a 0x153B feature: the same sequence on the 0x153B model takes every trigger
    to_reset(0x153B, 600);
    memset(&t, 0, sizeof(t));
    t.holdoff = 200000;
    fpga_trig_write(&t, 1);
    fm.trig_at = 40;
    to_arm(10, 4 * TO_TOTAL, 0);
    T_TRUE(to_wait(200)); T_EQ(to_trig(), 1);
    fm.trig_at = 40;
    to_arm(10, 4 * TO_TOTAL, 0);
    T_TRUE(to_wait(200)); T_EQ(to_trig(), 1);
    T_EQ(fm.ho_blocked, 0);
  }
}

//C6 peak detect (0x153C): fpga_decim_write with the peak bit against the model (0x53 = 1, 0x54 = 0, 0x56 = 2, the 0x29
//bytes), a capture whose words are { max, min } of the model's synthetic windows (read as the firmware reads a 16-bit
//record and as READ_RAW layout 3 reads the lanes), the overload rule on them, and the way back (0x56 = 0: the filter's
//registers again; the mode byte follows 0x53)
static void test_peak(void)
{
  pc_decim_cfg c;
  uint16_t w16[600];
  uint8_t  mn[600], mx[600];
  uint32_t i, n, spikes = 0, dips = 0;
  int ok;

  fm_reset(0x153C, 19, 61);
  fm.capture = 1;
  fm.trig_at = -1;
  fm_fill(5);
  write_0d(19);
  n = fm_window();
  T_EQ(n, 20);

  //The peak arm's writes: K = 1 to 0x53 (the contract's register value), shift 0, 0x56 bit 1; the FIR request is dropped
  pc_decim_config(PC_DECIM_CIC2, PC_DECIM_SET_FIR | PC_DECIM_SET_PEAK, fm.divider, 3000, PC_DECIM_HAS_FIR, &c);
  T_EQ(c.peak, 1); T_EQ(c.k, 1); T_EQ(c.n, 20); T_EQ(c.fir_en, 0); T_EQ(c.shift, 0); T_EQ(c.gain, 1);
  T_EQ(pc_decim_cmd56(&c), 2); T_EQ(pc_decim_status(&c), 0x81); T_EQ(pc_decim_shift_byte(&c), 0xC0);
  fm.ntrace = 0;
  T_EQ(fpga_decim_write(pc_decim_mode(&c), 1, pc_decim_cmd56(&c), c.shift), 1);
  T_EQ(fm.reg_53, 0x01); T_EQ(fm.reg_56, 0x02); T_EQ(fm.reg_54, 0);
  T_TRUE(fm_peak_on()); T_TRUE(fm_dec_on());

  //0x29 as fpga_get_decim_state reads it: byte 0 = 16-bit path, K 1, no FIR; byte 1 = peak active and written, S 0
  fpga_write_cmd(0x29);
  T_EQ(fpga_read_byte(), 0x81);
  T_EQ(fpga_read_byte(), 0xC0);

  //A capture: 600 words from the release (the auto time-out ends it), then the words as the firmware's 16-bit readout
  //and READ_RAW layout 3 read them: lane 0x20 = min, 0x21 = max of the window, the reference being fm_peak_word
  fpga_write_cmd(0x1A);
  fpga_write_byte(0x00);
  {
    fpga_arm_cfg a;

    memset(&a, 0, sizeof(a));
    a.pre_words    = 0;
    a.total_words  = 600;
    a.write_0e     = 1;
    a.auto_timeout = 598;
    T_EQ(fpga_arm_reset(&a), 0);
  }
  fm_advance_ns(700 * 100);
  fpga_write_cmd(0x0A);
  T_EQ(fpga_read_byte() & 1, 1);
  T_TRUE(fm.stop_n >= 600);

  fpga_read_decim_data(0x20, 0x21, 0, 600, w16);
  fpga_read_lane(0x20, 0, 600, mn, 1, fm.ring);
  fpga_read_lane(0x21, 0, 600, mx, 1, fm.ring);
  ok = 1;

  for(i=0;i<600;i++)
  {
    uint8_t rmn, rmx;

    fm_peak_word(0, i, &rmn, &rmx);

    if((mn[i] != rmn) || (mx[i] != rmx) || (w16[i] != (uint16_t)(rmn | (rmx << 8))) || (mn[i] > mx[i]))
      ok = 0;
    if(mx[i] == 255) spikes++;
    if(mn[i] == 0)   dips++;
  }

  T_TRUE(ok);
  //The 255 spikes every 1000 samples land in words 0, 50, 100, ..., 550 (12), the 0 dips every 1777 in 6 words (sample
  //0 is a spike); a window never misses one, which is what sample skipping would do
  T_EQ(spikes, 12); T_EQ(dips, 6);
  T_EQ(mx[50], 255); T_EQ(mn[88], 0); T_EQ(mn[50] < 255, 1);
  T_EQ(pc_peak_overload(mn[50], mx[50]), 0xFE); T_EQ(pc_peak_overload(mn[88], mx[88]), 0x01);
  T_EQ(pc_peak_overload(mn[1], mx[1]), 0);

  //CH2's lanes hold its own windows
  fpga_read_lane(0x22, 7, 100, mn, 1, fm.ring);
  fpga_read_lane(0x23, 7, 100, mx, 1, fm.ring);
  ok = 1;
  for(i=0;i<100;i++)
  {
    uint8_t rmn, rmx;

    fm_peak_word(1, 7 + i, &rmn, &rmx);
    if((mn[i] != rmn) || (mx[i] != rmx))
      ok = 0;
  }
  T_TRUE(ok);

  //The way back: the filter's registers again (0x53 = 2 with its shift, 0x56 = FIR as asked); the model's mode byte
  //follows 0x53 and the peak bits clear
  pc_decim_config(PC_DECIM_CIC2, PC_DECIM_SET_FIR, fm.divider, 3000, PC_DECIM_HAS_FIR, &c);
  T_EQ(c.peak, 0); T_EQ(c.k, 2);
  T_EQ(fpga_decim_write(pc_decim_mode(&c), 1, pc_decim_cmd56(&c), c.shift), 1);
  T_EQ(fm.reg_53, 0x02); T_EQ(fm.reg_56, 0x01); T_EQ(fm.reg_54, c.shift);
  T_FALSE(fm_peak_on()); T_TRUE(fm_dec_on());
  fpga_write_cmd(0x29);
  T_EQ(fpga_read_byte(), 0xE2);                   //16-bit, FIR enabled and active (N 20), K 2
  T_EQ(fpga_read_byte(), c.shift);

  //Off: legacy
  pc_decim_config(PC_DECIM_OFF, 0, fm.divider, 3000, PC_DECIM_HAS_FIR, &c);
  (void)fpga_decim_write(pc_decim_mode(&c), 1, pc_decim_cmd56(&c), c.shift);
  T_EQ(fm.reg_53, 0); T_EQ(fm.reg_56, 0); T_FALSE(fm_dec_on());
  fpga_write_cmd(0x29);
  T_EQ(fpga_read_byte(), 0x00);

  //An older model ignores 0x56 bit 1
  fm_reset(0x153B, 19, 62);
  write_0d(19);
  pc_decim_config(PC_DECIM_OFF, PC_DECIM_SET_PEAK, fm.divider, 3000, PC_DECIM_HAS_FIR, &c);
  (void)fpga_decim_write(pc_decim_mode(&c), 1, pc_decim_cmd56(&c), c.shift);
  T_EQ(fm.reg_56, 0x02); T_FALSE(fm_peak_on());
  fpga_write_cmd(0x29);
  T_EQ(fpga_read_byte(), 0x81);                   //K 1 was written: the filter path
  T_EQ(fpga_read_byte(), 0x00);
}

int main(void)
{
  test_write_record();
  test_ring_start();
  test_readout();
  test_arm_order();
  test_arm_trace();
  test_ready_bound();
  test_decim_write();
  test_timeout_records();
  test_trig_ext();
  test_peak();
  return t_report("test_fpga_bus");
}
