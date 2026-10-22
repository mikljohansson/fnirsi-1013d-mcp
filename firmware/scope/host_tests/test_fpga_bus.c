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
//     fw-small: the forced stop (fpga_force_stop: 0x0E = 0, then 0x1A = 0) ends a capture armed with 0x0E = twice the
//     deadline within two write clocks;
//   - fw-c5 (0x153C, protocol 1.26, firmware/AGENTS.md section 69): the trigger extension registers (fpga_trig_write,
//     fpga_trig_arm): every type's arm sequence (0x5E, and 0x5F / 0x60 for the qualified types, inside the 0x01 = 1
//     window after the geometry and before 0x0E), the 0x66 one-shot consumed by the next 0x01 = 1, the hold-off masking
//     a trigger across a re-arm, released by 0x66 + arm or by 0x1B = 0, and the 0x34 / 0x37 status reads;
//   - fw-c7 (0x153C, C7 counter, fpga_counter.c): the boot writes (0x62 = 128 / 6, 0x61 = gate 100 ms, source 0), the
//     once-per-pass 0x35 read (17 strobes) and the values it derives from the model's synthetic square waves at every
//     gate, the rate-change rule (0x61 = 0 before the read, the result dropped, 0x61 = value after), a host
//     configuration applied by the next pass, the trigger source followed through 0x15; fw-ctrauto: the thresholds
//     following the signal (test_counter_auto, with a slow-settle noisy square counted sample by sample).
//   - fw-d5 (0x153D, protocol 1.27, firmware/AGENTS.md sections 75 and 79): the generator as a fractional two-step square
//     (gen_pat.c: the P16 / H16 formulas at many frequencies and duties, exact mean period, every edge within one 5 ns
//     clock of its ideal time on the model's square), the double-buffered frequency change (glitch-free, taken at once
//     with the step in progress completing, two commits in one step), the bus cost of one chirp step (18 strobes), the
//     0x39 status, entries other than 0 / 1 and the cut pattern registers ignored by the model (fpga-d3's RTL d60a0ad).
//----------------------------------------------------------------------------------------------------------------------------------

#include "test_util.h"
#include "fpga_bus.h"
#include "fpga_counter.h"
#include "fpga_model.h"
#include "pc_decim.h"
#include <string.h>

//The model's bitstreams the firmware's sequences are run against: the built-for 0x153D and its predecessors with the
//same bus (the firmware runs on 0x153D only; the older models show the faults the order and the dummy strobe answer)
static const uint16_t versions[] = { 0x1535, 0x1537, 0x1539, 0x153A, 0x153B, 0x153C, 0x153D };
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
  fm_reset(0x153D, 0, 5);
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

  fm_reset(0x153D, 0, 9);
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
  return fpga_decim_write(pc_decim_mode(c), pc_decim_cmd56(c), c->shift);
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

  fm_reset(0x153D, 199, 21);
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

  //fw-cruft: 0x53 bits 4..2 (the swap / falling-edge bits 0x153C ignores) never reach the bus, whatever the caller passes
  fm.ntrace = 0;
  (void)fpga_decim_write(0x1E, 0, 0);
  T_EQ(fm.reg_53, 0x02); T_EQ(fm_trace_find(0x53, 0x1E, 0), -1);
  (void)fpga_decim_write(0x00, 0, 0);

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

//The firmware's forced stop (pc_hal_force): 0x0E = 0, then 0x1A = 0
static void to_force(void)
{
  fpga_force_stop();
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

//fw-small: the deadline force with an explicit auto_timeout. The host's SET_TRIGGER auto_timeout goes to 0x0E as is
//(pc_auto_timeout -> fpga_arm_reset), and the firmware's deadline (pc_host_auto_ms) comes at about that many samples:
//in the legacy layout 0x0E then holds twice the deadline in words. The force must stop the capture at the deadline
//(within the two write clocks of the 0x1A synchroniser), not when the FPGA's own count expires up to twice later;
//0x1A = 0 alone (fw 0.2.41 .. fw-small) left time_base_cnt <= 0x0E and the capture running.
static void test_force_deadline(void)
{
  static const uint16_t vers[] = { 0x153B, 0x153C, 0x153D };
  static const uint32_t deadlines[] = { 3 * TO_TOTAL, 8192 + 777, 3 * 8192 + 5 };
  uint32_t v, d;
  int newest;

  for(v = 0; v < 3; v++)
    for(d = 0; d < 3; d++)
    {
      uint32_t dl = deadlines[d], n_force;

      to_reset(vers[v], 300 + 7 * v + d);
      to_arm(d ? 200 : 0, 2 * dl, 1);                          //firmware auto: armed normal, 0x0E = 2 x the deadline
      fm_advance_ns(dl * 10);                                  //the deadline (divider 0: one word per 10 ns)
      T_TRUE(!to_done());
      to_force();
      n_force = fm.n;
      T_TRUE(n_force < 2 * dl);                                //the FPGA's own count is far away

      fm_advance_ns(2 * 10);                                   //two write clocks
      T_TRUE(to_done());
      T_TRUE(fm.stop_n <= n_force + 2);
      T_EQ(fm.reg_0e, 0);
      T_EQ(to_trig(), 0);
      T_TRUE(to_read(d ? 200 : 0, to_addr(), &newest)); T_TRUE(newest);
    }
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

  fm_reset(0x153D, 19, 61);
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
  T_EQ(fpga_decim_write(pc_decim_mode(&c), pc_decim_cmd56(&c), c.shift), 1);
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
  T_EQ(fpga_decim_write(pc_decim_mode(&c), pc_decim_cmd56(&c), c.shift), 1);
  T_EQ(fm.reg_53, 0x02); T_EQ(fm.reg_56, 0x01); T_EQ(fm.reg_54, c.shift);
  T_FALSE(fm_peak_on()); T_TRUE(fm_dec_on());
  fpga_write_cmd(0x29);
  T_EQ(fpga_read_byte(), 0xE2);                   //16-bit, FIR enabled and active (N 20), K 2
  T_EQ(fpga_read_byte(), c.shift);

  //Off: legacy
  pc_decim_config(PC_DECIM_OFF, 0, fm.divider, 3000, PC_DECIM_HAS_FIR, &c);
  (void)fpga_decim_write(pc_decim_mode(&c), pc_decim_cmd56(&c), c.shift);
  T_EQ(fm.reg_53, 0); T_EQ(fm.reg_56, 0); T_FALSE(fm_dec_on());
  fpga_write_cmd(0x29);
  T_EQ(fpga_read_byte(), 0x00);

  //An older model ignores 0x56 bit 1
  fm_reset(0x153B, 19, 62);
  write_0d(19);
  pc_decim_config(PC_DECIM_OFF, PC_DECIM_SET_PEAK, fm.divider, 3000, PC_DECIM_HAS_FIR, &c);
  (void)fpga_decim_write(pc_decim_mode(&c), pc_decim_cmd56(&c), c.shift);
  T_EQ(fm.reg_56, 0x02); T_FALSE(fm_peak_on());
  fpga_write_cmd(0x29);
  T_EQ(fpga_read_byte(), 0x81);                   //K 1 was written: the filter path
  T_EQ(fpga_read_byte(), 0x00);
}

//----------------------------------------------------------------------------------------------------------------------------------
// C7 counter (fw-c7, FPGA 0x153C, fpga_counter.c): the boot writes, the once-per-pass 0x35 read and its cost, the derived
// values against the model's synthetic square waves at every gate, the rate-change rule (0x61 = 0 before the read, the
// result dropped, 0x61 = value after), a host configuration applied by the next pass, the trigger source followed
// through 0x15, the current / stale rule, the arithmetic's corner cases; the auto thresholds (fw-ctrauto)

#define CNT_PASS_STROBES  17

static uint32_t cnt_pass(uint32_t ms)
{
  uint32_t s0 = fm.strobes;

  fpga_counter_pass(ms);
  return fm.strobes - s0;
}

//fw-fullrate (FPGA 0x153D, owner 2026-10-21): the ADC encodes run at 200 MSa/s at every rate. With K = 0 at D > 0 the
//write clock stays at 100 MHz and a word is stored on every (D + 1)-th edge: the stored pairs are every (D + 1)-th pair
//of the full-rate stream, core B 5 ns before core A, the record geometry and rate as before (10 (D + 1) ns per word).
//0x153C divided the write clock itself (and gated the encodes); its words are one per edge
static void test_fullrate(void)
{
  static const uint32_t div[3] = { 0, 4, 399 };
  uint8_t  a[300], b[300], c2[300];
  uint32_t i, d, ok;

  for(d=0;d<3;d++)
  {
    fm_reset(0x153D, div[d], 77 + d);
    fm.capture   = 1;
    fm.raw_words = 1;
    fm.trig_at   = -1;
    fm_fill(9);
    write_0d(div[d]);

    fpga_write_cmd(0x1A);
    fpga_write_byte(0x00);
    {
      fpga_arm_cfg cfg;

      memset(&cfg, 0, sizeof(cfg));
      cfg.pre_words    = 0;
      cfg.total_words  = 300;
      cfg.write_0e     = 1;
      cfg.auto_timeout = 298;
      T_EQ(fpga_arm_reset(&cfg), 0);
    }

    //The ready wait in reset is one write clock (10 ns) at every rate; the capture takes 300 words x 10 (D + 1) ns
    T_TRUE(fm.ready_polls >= 1);
    fm_advance_ns(310 * 10 * (div[d] + 1));
    fpga_write_cmd(0x0A);
    T_EQ(fpga_read_byte() & 1, 1);
    T_TRUE(fm.stop_n >= 300);
    T_TRUE(fm.stop_n <= 302);

    fpga_read_lane(0x20, 0, 300, a, 1, fm.ring);
    fpga_read_lane(0x21, 0, 300, b, 1, fm.ring);
    fpga_read_lane(0x23, 0, 300, c2, 1, fm.ring);
    ok = 1;

    for(i=0;i<300;i++)
    {
      uint64_t j = 2ull * i * (div[d] + 1);

      if((a[i] != fm_raw_sample(0, j + 1)) || (b[i] != fm_raw_sample(0, j)) || (c2[i] != fm_raw_sample(1, j)))
        ok = 0;
    }

    T_TRUE(ok);
  }

  //The same stored geometry as the divided write clock of 0x153C: 300 words over 300 x 10 (D + 1) ns
  fm_reset(0x153C, 4, 5);
  fm.capture = 1;
  fm.trig_at = -1;
  write_0d(4);
  fpga_write_cmd(0x1A);
  fpga_write_byte(0x00);
  {
    fpga_arm_cfg cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.pre_words    = 0;
    cfg.total_words  = 300;
    cfg.write_0e     = 1;
    cfg.auto_timeout = 298;
    T_EQ(fpga_arm_reset(&cfg), 0);
  }
  fm_advance_ns(150 * 50);
  T_TRUE((fm.n >= 148) && (fm.n <= 152));
  fm_reset(0x153D, 4, 5);
  fm.capture = 1;
  fm.trig_at = -1;
  write_0d(4);
  fpga_write_cmd(0x1A);
  fpga_write_byte(0x00);
  {
    fpga_arm_cfg cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.pre_words    = 0;
    cfg.total_words  = 300;
    cfg.write_0e     = 1;
    cfg.auto_timeout = 298;
    T_EQ(fpga_arm_reset(&cfg), 0);
  }
  fm_advance_ns(150 * 50);
  T_TRUE((fm.n >= 148) && (fm.n <= 152));
}

static void test_counter_derive(void)
{
  uint64_t f, p, w, l;
  uint32_t d;

  T_EQ(fpga_counter_derive(0, 0, 0, &f, &p, &w, &l, &d), 0);
  T_EQ(f, 0); T_EQ(p, 0); T_EQ(d, 0);
  T_EQ(fpga_counter_derive(0, 1000, 500, &f, &p, &w, &l, &d), 0);

  //One period of two clocks: 100 MHz, the logic's ceiling
  T_EQ(fpga_counter_derive(1, 2, 1, &f, &p, &w, &l, &d), 1);
  T_EQ(f, 100000000000000ull); T_EQ(p, 10000); T_EQ(w, 5000); T_EQ(l, 5000); T_EQ(d, 500000);

  //30 MHz 50 % over a 1 s gate: E 3e7, T 2e8 (the largest T), W 1e8; thirds rounded to nearest
  T_EQ(fpga_counter_derive(30000000, 200000000, 100000000, &f, &p, &w, &l, &d), 1);
  T_EQ(f, 30000000000000ull); T_EQ(p, 33333); T_EQ(w, 16667); T_EQ(l, 16667); T_EQ(d, 500000);

  //3 periods in 10 clocks: 60 MHz exactly, period 16666.67 ps -> 16667; W 3 of 10 clocks: duty 300000, width 5000
  T_EQ(fpga_counter_derive(3, 10, 3, &f, &p, &w, &l, &d), 1);
  T_EQ(f, 60000000000000ull); T_EQ(p, 16667); T_EQ(w, 5000); T_EQ(l, 11667); T_EQ(d, 300000);

  //Two edges 1 s apart in a 1 s gate: 1.000000005 Hz, T = 2e8 - 1; the uHz field rounds it to 1 Hz (one clock of 2e8
  //is 5e-9 relative, the frequency field's resolution 1e-6 at 1 Hz; the period in ps keeps it)
  T_EQ(fpga_counter_derive(1, 199999999, 100000000, &f, &p, &w, &l, &d), 1);
  T_EQ(f, 1000000ull); T_EQ(p, 999999995000ull); T_EQ(w, 500000000000ull); T_EQ(d, 500000);

  //The 28-bit maxima do not overflow: E = T / 2 = 2^27 - 1
  T_EQ(fpga_counter_derive(0x7FFFFFF, 0xFFFFFFE, 0x7FFFFFF, &f, &p, &w, &l, &d), 1);
  T_EQ(f, 100000000000000ull); T_EQ(p, 10000); T_EQ(w, 5000); T_EQ(d, 500000);

  //W above T is clamped (cannot happen on the FPGA)
  T_EQ(fpga_counter_derive(10, 100, 200, &f, &p, &w, &l, &d), 1);
  T_EQ(d, 1000000); T_EQ(w, 50000); T_EQ(l, 0);

  T_EQ(fpga_counter_gate_ms(0), 0); T_EQ(fpga_counter_gate_ms(1), 10); T_EQ(fpga_counter_gate_ms(2), 100);
  T_EQ(fpga_counter_gate_ms(3), 1000);
}

static void test_counter(void)
{
  uint32_t r, n, i35, i61a, i61b;
  uint64_t t0;

  //Divider 199 (2 us write clock) keeps the model fast; the counter's stream is at 5 ns whatever the divider
  fm_reset(0x153D, 199, 900);
  fm.ntrace = 0;

  //Boot: 0x62 = 128 / 6, then 0x61 = gate 100 ms, source 0 (5 strobes); the gate runs on CH1 (0x15 = 0)
  r = fm.strobes;
  fpga_counter_boot();
  T_EQ(fm.strobes - r, 5);
  T_EQ(fm.ntrace, 2);
  T_EQ(fm.trace[0].cmd, 0x62); T_EQ(fm.trace[0].value, 0x8006);
  T_EQ(fm.trace[1].cmd, 0x61); T_EQ(fm.trace[1].value, 0x02);
  T_EQ(fm.reg_61, 0x02); T_EQ(fm.reg_62_lvl, 128); T_EQ(fm.reg_62_hys, 6); T_EQ(fm.cnt_cfg, 0x02); T_EQ(fm.cnt_restarts, 1);
  T_EQ(fpga_cnt.gate, FPGA_CNT_GATE_100MS); T_EQ(fpga_cnt.source, FPGA_CNT_SRC_TRIG); T_EQ(fpga_cnt.level, 128);
  T_EQ(fpga_cnt.hyst, 6); T_EQ(fpga_cnt.cfg_level, 0); T_EQ(fpga_cnt.cfg_hyst, 0); T_EQ(fpga_cnt.held, 0);
  T_EQ(fpga_cnt.pending, 0);

  //The rules below run on pinned thresholds (the auto ones: test_counter_auto): 128 / 6 pinned by the host, which the
  //first pass applies as a restart (0x62 unchanged, 0x61 = 0 / 2)
  fpga_counter_config(FPGA_CNT_GATE_100MS, FPGA_CNT_SRC_TRIG, 128, 6);

  //1 MHz 50 % on CH1 between codes 60 and 200. The first pass: no gate has ended, the read costs 17 strobes, and the
  //host configuration's restart 4
  fm_cnt_signal(0, 1000, 500, 35, 60, 200);
  fm.ntrace = 0;
  T_EQ(cnt_pass(1000), CNT_PASS_STROBES + 4);
  T_EQ(fpga_cnt.reads, 1); T_EQ(fpga_cnt.held, 0); T_EQ(fpga_cnt.results, 0); T_EQ(fm.ntrace, 2);
  T_EQ(fpga_cnt.restarts, 1);
  T_EQ(fpga_counter_current(1000), 0);

  //100 ms later one gate has been published: the result is taken, CH1, gate 2, exact 1 MHz 50 %
  fm_advance_ns(100000000);
  T_EQ(fm.cnt_publishes, 1);
  T_EQ(cnt_pass(1100), CNT_PASS_STROBES);
  T_EQ(fpga_cnt.held, 1); T_EQ(fpga_cnt.results, 1); T_EQ(fpga_cnt.dropped, 0);
  T_EQ(fpga_cnt.res_src, FPGA_CNT_RES_CH1); T_EQ(fpga_cnt.res_gate, 2); T_EQ(fpga_cnt.seq, 1);
  T_TRUE((fpga_cnt.e == 99999) || (fpga_cnt.e == 100000));
  T_EQ(fpga_cnt.t, fpga_cnt.e * 200); T_EQ(fpga_cnt.w, fpga_cnt.e * 100);
  T_EQ(fpga_cnt.min, 60); T_EQ(fpga_cnt.max, 200);
  T_EQ(fpga_cnt.has_period, 1);
  T_EQ(fpga_cnt.freq_uhz, 1000000000000ull); T_EQ(fpga_cnt.period_ps, 1000000); T_EQ(fpga_cnt.width_ps, 500000);
  T_EQ(fpga_cnt.low_ps, 500000); T_EQ(fpga_cnt.duty_ppm, 500000);
  T_EQ(fpga_cnt.res_ms, 1100); T_EQ(fpga_counter_current(1100), 1); T_EQ(fpga_counter_current(1501), 0);
  T_EQ(fpga_counter_current(1500), 1);                                         //three gates + 100 ms

  //The same gate read again: not new, nothing taken; the pass still costs 17 strobes and writes nothing
  fm.ntrace = 0;
  T_EQ(cnt_pass(1120), CNT_PASS_STROBES);
  T_EQ(fpga_cnt.results, 1); T_EQ(fm.ntrace, 0);

  //Two more gates in one interval: the latest one is taken (seq 3), once
  fm_advance_ns(200000000);
  cnt_pass(1320);
  T_EQ(fpga_cnt.results, 2); T_EQ(fpga_cnt.seq, 3);

  //Rate change: a 0x0D write while the gate runs; the gate that spans it ends before the next pass. That pass writes
  //0x61 = 0 before the 0x35 read, drops the result the read carries, and writes 0x61 = 2 after it: the model's gate
  //restarts at the value write and the next result is a clean full gate
  fm_advance_ns(30000000);
  fpga_write_cmd(0x0D); fpga_write_int(99);
  fm_advance_ns(80000000);
  n = fpga_cnt.results;
  fm.ntrace = 0;
  T_EQ(cnt_pass(1430), CNT_PASS_STROBES + 4);
  T_EQ(fpga_cnt.dropped, 1); T_EQ(fpga_cnt.results, n); T_EQ(fpga_cnt.restarts, 2);
  T_EQ(fm.ntrace, 2);                                                        //0x61 = 0, 0x61 = 2
  i61a = (uint32_t)fm_trace_find(0x61, 0, 0); i61b = (uint32_t)fm_trace_find(0x61, 2, 0);
  T_EQ(i61a, 0); T_EQ(i61b, 1);
  T_EQ(fm.cnt_cfg, 0x02); T_EQ(fm.cnt_restarts, 3);
  T_TRUE(fm.cnt_start_ps > fm.t_ps - 2000000);                                //restarted within this pass
  t0 = fm.cnt_start_ps;
  //A snapshot's "new" was consumed by the dropped read: the next pass before the gate ends sees nothing
  cnt_pass(1450);
  T_EQ(fpga_cnt.results, n); T_EQ(fpga_cnt.dropped, 1);
  fm_advance_ns(100000000);
  T_TRUE(fm.cnt_end_ps > t0);
  cnt_pass(1550);
  T_EQ(fpga_cnt.results, n + 1); T_EQ(fpga_cnt.freq_uhz, 1000000000000ull); T_EQ(fpga_cnt.e * 200, fpga_cnt.t);

  //A 0x53 write counts as a rate change too; a 0x54 / 0x56 write does not
  fpga_write_cmd(0x53); fpga_write_byte(0x01);
  fm.ntrace = 0;
  cnt_pass(1560);
  T_EQ(fpga_cnt.restarts, 3); T_EQ(fm.ntrace, 2);
  fpga_write_cmd(0x54); fpga_write_byte(0x03);
  fpga_write_cmd(0x56); fpga_write_byte(0x01);
  fm.ntrace = 0;
  cnt_pass(1580);
  T_EQ(fpga_cnt.restarts, 3); T_EQ(fm.ntrace, 0);

  //The host pins level 150: 0x62 = 150 / 6 then 0x61 = 0 / 2 (the gate restarted so no result mixes two levels)
  fpga_counter_config(FPGA_CNT_GATE_100MS, FPGA_CNT_SRC_TRIG, 150, 6);
  fm.ntrace = 0;
  T_EQ(cnt_pass(1600), CNT_PASS_STROBES + 3 + 4);
  T_EQ(fm.ntrace, 3);
  T_EQ(fm.trace[0].cmd, 0x62); T_EQ(fm.trace[0].value, 0x9606);
  T_EQ(fm.trace[1].cmd, 0x61); T_EQ(fm.trace[1].value, 0); T_EQ(fm.trace[2].cmd, 0x61); T_EQ(fm.trace[2].value, 2);
  T_EQ(fpga_cnt.level, 150); T_EQ(fpga_cnt.restarts, 4); T_EQ(fm.reg_62_lvl, 150);
  i35 = fpga_cnt.reads;
  T_EQ(i35, 10);                                                               //one 0x35 read per pass so far

  //A signal that stays under the level (high code 140 < 150): a gate with no edges publishes E = 0, which is a result
  //without a period; the cards then fall back to the record (fpga_counter_current stays 1: the counter is alive)
  fm_cnt_signal(0, 1000, 500, 35, 60, 140);
  fm_advance_ns(100000000);
  n = fpga_cnt.results;
  cnt_pass(1700);
  T_EQ(fpga_cnt.results, n + 1); T_EQ(fpga_cnt.e, 0); T_EQ(fpga_cnt.has_period, 0); T_EQ(fpga_cnt.freq_uhz, 0);
  T_EQ(fpga_cnt.min, 60); T_EQ(fpga_cnt.max, 140); T_EQ(fpga_counter_current(1700), 1);

  //Host configuration: 1 s gate, CH2, level 100, hysteresis 10: noted, written by the next pass (0x62, 0x61 = 0, 0x61 =
  //0x0B)
  fm_cnt_signal(1, 1000000, 200000, 0, 80, 110);                             //1 kHz 20 % on CH2 between 80 and 110
  fpga_counter_config(FPGA_CNT_GATE_1S, FPGA_CNT_SRC_CH2, 100, 10);
  T_EQ(fpga_cnt.pending, 1); T_EQ(fm.reg_61, 0x02);                          //nothing written yet
  fm.ntrace = 0;
  cnt_pass(1720);
  T_EQ(fpga_cnt.pending, 0); T_EQ(fm.ntrace, 3);
  T_EQ(fm.trace[0].cmd, 0x62); T_EQ(fm.trace[0].value, 0x640A);
  T_EQ(fm.trace[1].value, 0); T_EQ(fm.trace[2].cmd, 0x61); T_EQ(fm.trace[2].value, 0x0B);
  T_EQ(fm.cnt_cfg, 0x07); T_EQ(fpga_cnt.gate, 3); T_EQ(fpga_cnt.source, 2); T_EQ(fpga_cnt.level, 100); T_EQ(fpga_cnt.hyst, 10);
  fm.ntrace = 0;
  cnt_pass(1740);                                                              //nothing more to write
  T_EQ(fm.ntrace, 0);
  //The old result (read at 1700) ages out against the new 1 s gate's bound (3 s + 100 ms) only after 3.1 s; a 1 s gate
  //completes first
  T_EQ(fpga_counter_current(1700 + 3100), 1); T_EQ(fpga_counter_current(1700 + 3101), 0);
  T_EQ(fpga_cnt.min, 60); T_EQ(fpga_cnt.max, 140);                            //still the E = 0 result: lo 60, hi 140
  fm_advance_ns(1000000000);
  n = fpga_cnt.results;
  cnt_pass(2740);
  T_EQ(fpga_cnt.results, n + 1); T_EQ(fpga_cnt.res_src, FPGA_CNT_RES_CH2); T_EQ(fpga_cnt.res_gate, 3);
  T_TRUE((fpga_cnt.e == 999) || (fpga_cnt.e == 1000));
  T_EQ(fpga_cnt.freq_uhz, 1000000000ull); T_EQ(fpga_cnt.period_ps, 1000000000ull); T_EQ(fpga_cnt.width_ps, 200000000ull);
  T_EQ(fpga_cnt.low_ps, 800000000ull); T_EQ(fpga_cnt.duty_ppm, 200000); T_EQ(fpga_cnt.min, 80); T_EQ(fpga_cnt.max, 110);

  //128 / 6 again, 10 ms gate on EXT: 10 MHz 50 %; min / max read 255 / 0 on EXT
  fm_cnt_signal(2, 100, 50, 0, 0, 0);
  fpga_counter_config(FPGA_CNT_GATE_10MS, FPGA_CNT_SRC_EXT, 128, 6);
  fm.ntrace = 0;
  cnt_pass(2760);
  T_EQ(fm.trace[0].cmd, 0x62); T_EQ(fm.trace[0].value, 0x8006); T_EQ(fm.trace[2].value, 0x0D);
  fm_advance_ns(10000000);
  n = fpga_cnt.results;
  cnt_pass(2780);
  T_EQ(fpga_cnt.results, n + 1); T_EQ(fpga_cnt.res_src, FPGA_CNT_RES_EXT); T_EQ(fpga_cnt.res_gate, 1);
  T_TRUE((fpga_cnt.e == 99999) || (fpga_cnt.e == 100000));
  T_EQ(fpga_cnt.freq_uhz, 10000000000000ull); T_EQ(fpga_cnt.period_ps, 100000); T_EQ(fpga_cnt.min, 255); T_EQ(fpga_cnt.max, 0);
  T_EQ(fpga_counter_current(2780 + 130), 1); T_EQ(fpga_counter_current(2780 + 131), 0);

  //Source 0 follows the trigger source: 0x15 = 1 (CH2) restarts the FPGA's gate by itself (no firmware write), the
  //next result is CH2's
  fpga_counter_config(FPGA_CNT_GATE_100MS, FPGA_CNT_SRC_TRIG, 128, 6);
  cnt_pass(2800);
  T_EQ(fm.cnt_cfg, 0x02);
  fm_cnt_signal(0, 1000, 500, 35, 60, 200);
  fm_cnt_signal(1, 2000, 500, 0, 60, 200);
  fm_advance_ns(100000000);
  cnt_pass(2900);
  T_EQ(fpga_cnt.res_src, FPGA_CNT_RES_CH1); T_EQ(fpga_cnt.freq_uhz, 1000000000000ull);
  r = fm.cnt_restarts;
  fm.ntrace = 0;
  fpga_write_cmd(0x15); fpga_write_byte(0x01);
  T_EQ(fm.cnt_restarts, r + 1); T_EQ(fm.cnt_cfg, 0x06);
  fm_advance_ns(100000000);
  cnt_pass(3000);
  T_EQ(fm.ntrace, 1);                                                          //only the 0x15
  T_EQ(fpga_cnt.res_src, FPGA_CNT_RES_CH2); T_EQ(fpga_cnt.freq_uhz, 500000000000ull); T_EQ(fpga_cnt.duty_ppm, 250000);
  //A same-value 0x61 write does not restart the model's gate (the firmware never writes one on its own)
  r = fm.cnt_restarts;
  fpga_write_cmd(0x61); fpga_write_byte(0x02);
  T_EQ(fm.cnt_restarts, r);

  //Gate off: 0x61 = 0; the held result is not current
  fpga_counter_config(FPGA_CNT_GATE_OFF, FPGA_CNT_SRC_TRIG, 128, 6);
  fm.ntrace = 0;
  cnt_pass(3020);
  T_EQ(fm.ntrace, 2); T_EQ(fm.cnt_cfg, 0); T_EQ(fpga_cnt.gate, 0);
  T_EQ(fpga_counter_current(3020), 0);
  fm_advance_ns(300000000);
  n = fpga_cnt.results;
  cnt_pass(3320);
  T_EQ(fpga_cnt.results, n);
  //On again: one gate later a result
  fpga_counter_config(FPGA_CNT_GATE_100MS, FPGA_CNT_SRC_TRIG, 128, 6);
  cnt_pass(3340);
  fm_advance_ns(100000000);
  cnt_pass(3440);
  T_EQ(fpga_cnt.results, n + 1); T_EQ(fpga_counter_current(3440), 1);

  //On the 0x153B model the registers do not exist: the pass reads zeros and never holds a result
  fm_reset(0x153B, 199, 901);
  fpga_counter_boot();
  fm_cnt_signal(0, 1000, 500, 0, 60, 200);
  fm_advance_ns(100000000);
  cnt_pass(100);
  T_EQ(fpga_cnt.reads, 1); T_EQ(fpga_cnt.held, 0);
}

//fw-ctrauto: the bench's RC node (2026-10-22, 0x153D: 1 kHz, 89..184, a fast jump then a slow settle), sample by
//sample: one 1 ms period of 200000 clocks; the rise jumps 89 -> 140 and settles to 184, the fall jumps 184 -> 133 and
//settles to 89 (tau 100 us = 20000 clocks), plus -7..+7 codes of noise (bounded and uniform: the model shows the
//mechanism, the bench's Gaussian tails decide the fraction, fpga_counter.h). The noise repeats every period, so a correct count is exact (T = E x 200000)
#define CNT_RC_PERIOD  200000u

static uint8_t cnt_rc_tab[CNT_RC_PERIOD];

static void cnt_rc_build(void)
{
  uint32_t i, h = 0x12345678u;
  double   v = 140.0, a = 1.0 / 20000.0;
  int      x;

  for(i=0;i<CNT_RC_PERIOD;i++)
  {
    if(i == CNT_RC_PERIOD / 2)
      v = 133.0;

    v += (((i < CNT_RC_PERIOD / 2) ? 184.0 : 89.0) - v) * a;
    h = h * 1664525u + 1013904223u;
    x = (int)(v + 0.5) + (int)((h >> 16) % 15) - 7;
    cnt_rc_tab[i] = (uint8_t)((x < 0) ? 0 : ((x > 255) ? 255 : x));
  }
}

static uint8_t cnt_rc_wave(uint64_t clk)
{
  return cnt_rc_tab[clk % CNT_RC_PERIOD];
}

//The auto rule's thresholds for a min / max (fpga_counter.h): h = max(6, round(span / 6)) <= span / 2, the band centred
static void cnt_auto_expect(uint8_t mn, uint8_t mx, uint8_t *lvl, uint8_t *hys)
{
  uint32_t span = (uint32_t)(mx - mn), h = (span + 3) / 6;

  if(h < 6)        h = 6;
  if(h > span / 2) h = span / 2;
  *hys = (uint8_t)h;
  *lvl = (uint8_t)(((uint32_t)mn + mx + h + 1) / 2);
}

static void test_counter_auto(void)
{
  uint32_t n, r;
  uint8_t  lvl, hys;

  fm_reset(0x153D, 199, 902);
  fpga_counter_boot();

  //Boot: 128 / 6 written, both auto. A 1 MHz square 60..200 on CH1: the first gate at 128 / 6 counts it, but its min /
  //max ask for 142 / 23 (span 140: 23 = round(140 / 6); (60 + 200 + 23 + 1) / 2 = 142): misfit (not usable, the cards
  //show the record), and the pass writes 0x62 = 142 / 23 and restarts the gate
  fm_cnt_signal(0, 1000, 500, 35, 60, 200);
  cnt_pass(1000);
  fm_advance_ns(100000000);
  fm.ntrace = 0;
  T_EQ(cnt_pass(1100), CNT_PASS_STROBES + 3 + 4);
  T_EQ(fpga_cnt.results, 1); T_EQ(fpga_cnt.has_period, 1); T_EQ(fpga_cnt.misfit, 1);
  T_EQ(fpga_counter_current(1100), 1); T_EQ(fpga_counter_usable(1100), 0);
  T_EQ(fm.ntrace, 3); T_EQ(fm.trace[0].cmd, 0x62); T_EQ(fm.trace[0].value, 0x8E17);
  T_EQ(fm.trace[1].value, 0); T_EQ(fm.trace[2].value, 2);
  T_EQ(fpga_cnt.level, 142); T_EQ(fpga_cnt.hyst, 23); T_EQ(fpga_cnt.retunes, 1);
  //The next gate fits: usable, exact, nothing written
  fm_advance_ns(100000000);
  fm.ntrace = 0;
  T_EQ(cnt_pass(1200), CNT_PASS_STROBES);
  T_EQ(fpga_cnt.results, 2); T_EQ(fpga_cnt.misfit, 0); T_EQ(fpga_counter_usable(1200), 1);
  T_EQ(fpga_cnt.freq_uhz, 1000000000000ull); T_EQ(fpga_cnt.duty_ppm, 500000); T_EQ(fm.ntrace, 0);

  //Deadband: min / max moving by a few codes (58..203 asks for 143 / 24, within max(2, 24 / 4) = 6 of 142 / 23) keep the
  //thresholds: usable, no write, no restart
  fm_cnt_signal(0, 1000, 500, 35, 58, 203);
  r = fm.cnt_restarts;
  fm_advance_ns(100000000);
  fm.ntrace = 0;
  T_EQ(cnt_pass(1300), CNT_PASS_STROBES);
  T_EQ(fpga_cnt.misfit, 0); T_EQ(fpga_counter_usable(1300), 1); T_EQ(fm.ntrace, 0); T_EQ(fm.cnt_restarts, r);
  T_EQ(fpga_cnt.level, 142); T_EQ(fpga_cnt.hyst, 23);

  //No signal: 120..128 (span 8 < 12): the thresholds stay, the result is misfit
  fm_cnt_signal(0, 1000, 500, 35, 120, 128);
  fm_advance_ns(100000000);
  fm.ntrace = 0;
  cnt_pass(1400);
  T_EQ(fpga_cnt.min, 120); T_EQ(fpga_cnt.max, 128); T_EQ(fpga_cnt.misfit, 1); T_EQ(fpga_counter_usable(1400), 0);
  T_EQ(fm.ntrace, 0); T_EQ(fpga_cnt.level, 142); T_EQ(fpga_cnt.hyst, 23);
  //Even when the kept thresholds count it: an 8-code square at 124..132 under a pinned hysteresis 3 that the host then
  //returns to the auto level (the level stays 142 until a gate with a signal): first pinned 128 / 3 counts it
  fm_cnt_signal(0, 1000, 500, 35, 124, 132);
  fpga_counter_config(FPGA_CNT_GATE_100MS, FPGA_CNT_SRC_CH1, 128, 3);
  cnt_pass(1410);
  fm_advance_ns(100000000);
  cnt_pass(1510);
  T_EQ(fpga_cnt.has_period, 1); T_EQ(fpga_cnt.misfit, 0); T_EQ(fpga_counter_usable(1510), 1);   //pinned: the host's call
  fpga_counter_config(FPGA_CNT_GATE_100MS, FPGA_CNT_SRC_CH1, 0, 3);
  cnt_pass(1520);
  fm_advance_ns(100000000);
  fm.ntrace = 0;
  cnt_pass(1620);
  T_EQ(fpga_cnt.has_period, 1); T_EQ(fpga_cnt.misfit, 1); T_EQ(fpga_counter_usable(1620), 0);  //auto level: no signal
  T_EQ(fm.ntrace, 0); T_EQ(fpga_cnt.level, 128); T_EQ(fpga_cnt.hyst, 3);

  //The bench's slow-settle node, counted sample by sample at the 10 ms gate (ten periods per gate). With the thresholds
  //pinned at the old fixed 146 / 6 (the band 140..146 sits on the slow settles) the noise re-crosses the band: several
  //rising edges per period, a frequency several times 1 kHz
  cnt_rc_build();
  fm_cnt_wave(0, cnt_rc_wave);
  fpga_counter_config(FPGA_CNT_GATE_10MS, FPGA_CNT_SRC_CH1, 146, 6);
  cnt_pass(1700);
  T_EQ(fm.reg_62_lvl, 146); T_EQ(fm.reg_62_hys, 6);
  fm_advance_ns(10000000);
  n = fpga_cnt.results;
  cnt_pass(1710);
  T_EQ(fpga_cnt.results, n + 1); T_EQ(fpga_cnt.misfit, 0); T_EQ(fpga_cnt.has_period, 1);
  T_TRUE(fpga_cnt.e >= 20);                                                     //ten periods, at least twice the edges
  T_TRUE(fpga_cnt.freq_uhz >= 2000000000ull);
  //Back to auto (level and hysteresis 0): the next gate (still 146 / 6) is misfit against its min / max, the pass
  //retunes to the midpoint band, and the gate after that counts exactly 1 kHz
  fpga_counter_config(FPGA_CNT_GATE_10MS, FPGA_CNT_SRC_CH1, 0, 0);
  cnt_pass(1720);
  T_EQ(fpga_cnt.level, 146); T_EQ(fpga_cnt.hyst, 6);                           //no result yet: kept
  fm_advance_ns(10000000);
  cnt_pass(1730);
  T_EQ(fpga_cnt.misfit, 1); T_EQ(fpga_counter_usable(1730), 0);
  T_TRUE(fpga_cnt.min <= 84); T_TRUE(fpga_cnt.max >= 189);
  cnt_auto_expect(fpga_cnt.min, fpga_cnt.max, &lvl, &hys);
  T_EQ(fpga_cnt.level, lvl); T_EQ(fpga_cnt.hyst, hys); T_EQ(fm.reg_62_lvl, lvl); T_EQ(fm.reg_62_hys, hys);
  T_TRUE(hys >= 16);                                                            //round(span / 6) on about 82..191
  fm_advance_ns(10000000);
  cnt_pass(1740);
  T_EQ(fpga_cnt.misfit, 0); T_EQ(fpga_counter_usable(1740), 1);
  T_TRUE((fpga_cnt.e == 9) || (fpga_cnt.e == 10)); T_EQ(fpga_cnt.t, fpga_cnt.e * CNT_RC_PERIOD);
  T_EQ(fpga_cnt.freq_uhz, 1000000000ull);

  //A host-pinned hysteresis is kept with the auto level: level 0 / hysteresis 10 on the 60..200 square at 100 ms: the
  //level centres the 10-code band ((60 + 200 + 10 + 1) / 2 = 135), the hysteresis stays 10
  fm_cnt_signal(0, 1000, 500, 35, 60, 200);
  fpga_counter_config(FPGA_CNT_GATE_100MS, FPGA_CNT_SRC_CH1, 0, 10);
  cnt_pass(1800);
  T_EQ(fpga_cnt.hyst, 10);
  fm_advance_ns(100000000);
  cnt_pass(1900);
  T_EQ(fpga_cnt.misfit, 1); T_EQ(fpga_cnt.level, 135); T_EQ(fpga_cnt.hyst, 10);
  fm_advance_ns(100000000);
  cnt_pass(2000);
  T_EQ(fpga_cnt.misfit, 0); T_EQ(fpga_counter_usable(2000), 1); T_EQ(fpga_cnt.level, 135); T_EQ(fpga_cnt.hyst, 10);
  T_EQ(fpga_cnt.freq_uhz, 1000000000000ull);
  //A pinned level with the auto hysteresis: 150 / round(140 / 6) = 23 after a gate
  fpga_counter_config(FPGA_CNT_GATE_100MS, FPGA_CNT_SRC_CH1, 150, 0);
  cnt_pass(2010);
  T_EQ(fpga_cnt.level, 150); T_EQ(fpga_cnt.hyst, 10);
  fm_advance_ns(100000000);
  cnt_pass(2110);
  T_EQ(fpga_cnt.misfit, 1); T_EQ(fpga_cnt.level, 150); T_EQ(fpga_cnt.hyst, 23);
  fm_advance_ns(100000000);
  cnt_pass(2210);
  T_EQ(fpga_cnt.misfit, 0); T_EQ(fpga_counter_usable(2210), 1);

  //EXT never moves the thresholds and is never misfit
  fm_cnt_signal(2, 100, 50, 0, 0, 0);
  fpga_counter_config(FPGA_CNT_GATE_10MS, FPGA_CNT_SRC_EXT, 0, 0);
  cnt_pass(2220);
  fm_advance_ns(10000000);
  fm.ntrace = 0;
  cnt_pass(2230);
  T_EQ(fpga_cnt.res_src, FPGA_CNT_RES_EXT); T_EQ(fpga_cnt.misfit, 0); T_EQ(fpga_counter_usable(2230), 1);
  T_EQ(fm.ntrace, 0); T_EQ(fpga_cnt.level, 150); T_EQ(fpga_cnt.hyst, 23);
}

//----------------------------------------------------------------------------------------------------------------------------------
//The generator (fw-d5, 0x153D; the square only since fw-nopat)

#define GEN_VER  0x153D

//The contract's formulas: P16 within half a unit of 2^16 x 2e11 / f, the back conversion, the split, at many points
static void test_gen_formulas(void)
{
  static const uint64_t f_mhz[] = { 23300ull, 47123ull, 1000000ull, 999500ull, 1234567ull, 10000000ull, 123456000ull,
                                    333333333ull, 1000000000ull, 3300000000ull, 7777777777ull, 10000000000ull,
                                    12500000000ull, 25000000000ull };
  static const uint32_t duty[] = { 100, 1000, 2500, 3333, 5000, 6667, 7500, 9000, 9900 };
  uint32_t i, j, bad = 0, split_ok = 0;

  for(i = 0; i < sizeof(f_mhz) / sizeof(f_mhz[0]); i++)
  {
    uint64_t f = f_mhz[i], p16 = gp_p16_from_mhz(f);
    __uint128_t lhs = (__uint128_t)p16 * f, num = (__uint128_t)GP_NUM;
    __uint128_t err = (lhs > num) ? lhs - num : num - lhs;

    //|P16 f - 2^16 2e11| <= f / 2: P16 is the nearest unit
    if(err * 2 > f)
      bad++;

    //Back: within the rounding of one P16 unit (relative 1 / P16) plus the half mHz
    {
      uint64_t back = gp_mhz_from_p16(p16), d = (back > f) ? back - f : f - back;

      if((__uint128_t)d * p16 > (__uint128_t)f + p16)
        bad++;
    }

    for(j = 0; j < sizeof(duty) / sizeof(duty[0]); j++)
    {
      uint64_t  h16 = gp_h16_from_bp(p16, duty[j]);
      gp_square sq;
      int       st = gp_split(p16, h16, &sq);
      uint64_t  nh = h16 >> 16, nl = (p16 - h16) >> 16;

      if(((uint64_t)sq.n_h << 16 | sq.f_h) != h16 && (nh <= GP_STEP_MAX))
        bad++;

      if((st == 0) != ((nh >= 2) && (nl >= 2) && (nh <= GP_STEP_MAX) && (nl <= GP_STEP_MAX)))
        bad++;

      if(!st)
      {
        split_ok++;

        //The two steps add up to the period exactly; the duty comes back within one basis point
        if(((((uint64_t)sq.n_h + sq.n_l) << 16) + sq.f_h + sq.f_l) != p16)
          bad++;

        if(((uint32_t)gp_bp_from_h16(p16, h16) + 1 < duty[j]) || (gp_bp_from_h16(p16, h16) > duty[j] + 1))
          bad++;
      }
    }
  }

  T_EQ(bad, 0);
  T_TRUE(split_ok > 100);

  //The worked examples (firmware/AGENTS.md section 75)
  {
    gp_square sq;

    T_EQ(gp_split(gp_p16_from_mhz(1000000), gp_h16_from_bp(gp_p16_from_mhz(1000000), 5000), &sq), 0);
    T_EQ(sq.n_h, 100000); T_EQ(sq.f_h, 0); T_EQ(sq.n_l, 100000); T_EQ(sq.f_l, 0);
    T_EQ(gp_split(gp_p16_from_mhz(123456000), gp_h16_from_bp(gp_p16_from_mhz(123456000), 2500), &sq), 0);
    T_EQ(sq.n_h, 405); T_EQ(sq.f_h, 170); T_EQ(sq.n_l, 1215); T_EQ(sq.f_l, 509);
    T_EQ(gp_split(gp_p16_from_mhz(10000000000ull), gp_h16_from_bp(gp_p16_from_mhz(10000000000ull), 5000), &sq), 0);
    T_EQ(sq.n_h, 10); T_EQ(sq.f_h, 0); T_EQ(sq.n_l, 10); T_EQ(sq.f_l, 0);
  }

  //Limits: 25 MHz is 8 clocks, faster is refused; 2^49 and above (under 23.3 mHz) refused; a 1 % duty at 25 MHz has a
  //0.08 clock high step
  T_EQ(gp_split(8ull << 16, 4ull << 16, &(gp_square){0}), 0);
  T_EQ(gp_split((8ull << 16) - 1, 4ull << 16, &(gp_square){0}), 1);
  T_EQ(gp_split(1ull << 49, 1ull << 48, &(gp_square){0}), 1);
  T_EQ(gp_split(8ull << 16, gp_h16_from_bp(8ull << 16, 100), &(gp_square){0}), 2);

  //The UI's whole Hz / %: static levels at 0 Hz, 0 % and 100 %; a 1 % duty at 10 MHz clamped to 2 clocks high
  {
    uint64_t p16, h16;
    uint8_t  lvl;

    T_EQ(gp_ui_square(0, 50, &p16, &h16, &lvl), 1);    T_EQ(lvl, 0);
    T_EQ(gp_ui_square(1000, 0, &p16, &h16, &lvl), 1);  T_EQ(lvl, 0);
    T_EQ(gp_ui_square(1000, 100, &p16, &h16, &lvl), 1); T_EQ(lvl, 1);
    T_EQ(gp_ui_square(1000, 50, &p16, &h16, &lvl), 0); T_EQ(p16, 13107200000ull); T_EQ(h16, 6553600000ull);
    T_EQ(gp_ui_square(10000000, 1, &p16, &h16, &lvl), 0); T_EQ(p16, 20ull << 16); T_EQ(h16, 2ull << 16);
    T_EQ(gp_ui_square(10000000, 99, &p16, &h16, &lvl), 0); T_EQ(h16, 18ull << 16);
  }

  //The Bode stimulus: integer P, n_h = ceil(P / 2)
  {
    uint64_t p16, h16;

    gp_bode_square(133, &p16, &h16); T_EQ(p16, 133ull << 16); T_EQ(h16, 67ull << 16);
    gp_bode_square(20, &p16, &h16);  T_EQ(p16, 20ull << 16);  T_EQ(h16, 10ull << 16);
  }
}

//Run the model for ns (in slices the u32 advance takes)
static void gen_advance(uint64_t ns)
{
  while(ns)
  {
    uint32_t d = (ns > 1000000000ull) ? 1000000000u : (uint32_t)ns;

    fm_advance_ns(d);
    ns -= d;
  }
}

//The edges logged since index from: every rising edge k at start + k P, every falling one H later, in 1/65536 clock;
//the actual edge is the ideal one rounded up to the clock (Bresenham): -65536 < actual - ideal <= 0 after the shift.
//Returns the worst |actual - ideal| in 1/65536 clock, -1 on a wrong level sequence; *periods the complete periods seen
static int64_t gen_check_square(uint32_t from, uint64_t p16, uint64_t h16, uint32_t *periods, int64_t *mean_err16)
{
  uint64_t start = fm.gen_edge[from % FM_GEN_EDGES] >> 1;
  uint32_t i, n = fm.gen_nedge - from, rises = 0;
  int64_t  worst = 0;
  uint64_t last_rise = start;

  for(i = 0; i < n; i++)
  {
    uint64_t e = fm.gen_edge[(from + i) % FM_GEN_EDGES];
    uint8_t  level = (uint8_t)(e & 1);
    uint64_t k = i / 2;
    __int128 ideal = (__int128)((start << 16) + k * p16 + ((i & 1) ? h16 : 0));
    __int128 act = (__int128)((e >> 1) << 16);
    int64_t  d;

    if(level != (uint8_t)!(i & 1))
      return -1;

    //Accumulator rounding: the boundary is ideal rounded down (the carry adds the clock when the fraction wraps)
    d = (int64_t)(act - ideal);
    if(d < 0) d = -d;
    if(d > worst) worst = d;

    if(level)
    {
      rises++;
      last_rise = e >> 1;
    }
  }

  *periods = rises ? rises - 1 : 0;
  *mean_err16 = (int64_t)(((last_rise - start) << 16) - (uint64_t)*periods * p16);
  return worst;
}

//The square on the model: the firmware's sequence (fpga_gen_square + 0x50), then about `periods` periods
static void gen_square_case(uint64_t f_mhz, uint32_t bp, uint32_t periods, uint32_t seed)
{
  uint64_t  p16 = gp_p16_from_mhz(f_mhz), h16 = gp_h16_from_bp(p16, bp);
  gp_square sq;
  uint32_t  from, seen = 0;
  int64_t   worst, mean = 0;

  fm_reset(GEN_VER, 199999, seed);
  T_EQ(gp_split(p16, h16, &sq), 0);
  from = fm.gen_nedge;                                      //the start's rising edge is the next one logged
  fpga_gen_square(&sq);
  fpga_gen_drive(1);
  T_EQ(fm.gen_run, 1); T_EQ(fm.gen_drive, 1);
  gen_advance((uint64_t)periods * p16 * 5 / 65536 + 1000);

  worst = gen_check_square(from, p16, h16, &seen, &mean);
  T_TRUE(worst >= 0);
  T_TRUE(worst < 65536);                                    //every edge within one clock of its ideal time
  T_TRUE(seen + 2 >= periods);
  T_TRUE((mean > -65536) && (mean < 65536));                //the mean period exactly P16 / 65536 over the run

  if(!(worst >= 0 && worst < 65536 && mean > -65536 && mean < 65536))
    printf("  square %llu mHz %u bp: worst %lld / 65536 clock, mean error %lld over %u periods\n",
           (unsigned long long)f_mhz, bp, (long long)worst, (long long)mean, seen);
}

static void test_gen_square(void)
{
  uint32_t s0;

  gen_square_case(1000000, 5000, 1000, 1);                  //1 kHz, the wizard's: no fraction
  gen_square_case(999500, 5000, 1000, 2);                   //999.5 Hz: a fractional period
  gen_square_case(10000000, 2500, 1000, 3);                 //10 kHz 25 %
  gen_square_case(123456000, 2500, 1500, 4);                //123.456 kHz 25 %
  gen_square_case(1000000000, 5000, 1500, 5);               //1 MHz
  gen_square_case(3333333333ull, 3333, 1500, 6);            //3.33 MHz 33 %: fractions in both steps
  gen_square_case(10000000000ull, 5000, 1500, 7);           //10 MHz
  gen_square_case(47123, 7000, 200, 8);                     //47.123 Hz 70 %

  //The firmware's first enable: 0x6D = 0, 0x6C x 2, 0x6D = 0x33 (continuous, start, commit); a running square changes
  //with 0x6C x 2 and 0x6D = 0x23: 18 strobes, 5.4 us at 0.3 us a strobe
  fm_reset(GEN_VER, 199999, 9);
  {
    gp_square a = { 100000, 100000, 0, 0 }, b = { 50000, 50000, 0, 0 };

    fpga_gen_square(&a);
    T_EQ(fm.ntrace, 4);
    T_EQ(fm.trace[0].cmd, 0x6D); T_EQ(fm.trace[0].value, 0);
    T_EQ(fm.trace[1].cmd, 0x6C); T_EQ(fm.trace[2].cmd, 0x6C);
    T_EQ(fm.trace[3].cmd, 0x6D); T_EQ(fm.trace[3].value, 0x33);
    T_EQ(fm.gen_act[0].dur, 100000); T_EQ(fm.gen_act[0].flags, 0x20); T_EQ(fm.gen_act[1].flags, 0);
    s0 = fm.strobes;
    fpga_gen_square(&b);
    T_EQ(fm.strobes - s0, 18);
    T_EQ(fm.trace[6].cmd, 0x6D); T_EQ(fm.trace[6].value, 0x23);
    T_EQ(fm.gen_starts, 1);                                 //no restart
  }

  //The static levels (the wizard's gain step, UI 0 / 100 %): mode 0 at the idle level, the pin driven
  fpga_gen_level(1);
  fpga_gen_drive(1);
  T_EQ(fm.gen_run, 0); T_EQ(fm_pin(), 1);
  fpga_gen_level(0);
  T_EQ(fm_pin(), 0);
  fpga_gen_drive(0);
  T_EQ(fm_pin(), 2);

  //From a level back to the square: a fresh start (0x6D = 0 first)
  {
    gp_square a = { 10, 10, 0, 0 };

    fpga_gen_square(&a);
    T_EQ(fm.gen_run, 1); T_EQ(fm.gen_starts, 2);
  }
}

//The double buffer (fpga-d3's RTL): a frequency change while the square runs is glitch-free (every step a complete old
//or new one), taken at once (pending for 2 clocks: clear by the next bus read), the step in progress completing with its
//old length and every step from its boundary new; the accumulator is not cleared (phase-continuous); two commits within
//one step: the second wins at the boundary
static void test_gen_change(void)
{
  gp_square a, b, c;
  uint64_t  pa = gp_p16_from_mhz(999500), pb = gp_p16_from_mhz(1234567), pc = gp_p16_from_mhz(2000000);
  uint32_t  from, i, bad = 0, news = 0, first_new = 0;
  uint64_t  t_commit;
  uint8_t   st[2];

  (void)gp_split(pa, gp_h16_from_bp(pa, 5000), &a);
  (void)gp_split(pb, gp_h16_from_bp(pb, 2500), &b);
  (void)gp_split(pc, gp_h16_from_bp(pc, 5000), &c);

  fm_reset(GEN_VER, 199999, 21);
  fpga_gen_square(&a);
  fpga_gen_drive(1);
  gen_advance(10000000);                                    //10 ms: about 10 periods
  from = fm.gen_nedge;
  t_commit = fm_clk();
  fpga_gen_square(&b);

  fpga_gen_status(st);
  T_EQ(st[0] & 0xA0, 0x80);                                 //running, the commit already taken
  T_EQ(st[1], 1);                                           //mode 3
  gen_advance(20000000);

  for(i = from; i + 1 < fm.gen_nedge; i++)
  {
    uint64_t d = (fm.gen_edge[(i + 1) % FM_GEN_EDGES] >> 1) - (fm.gen_edge[i % FM_GEN_EDGES] >> 1);
    uint8_t  lvl = (uint8_t)(fm.gen_edge[i % FM_GEN_EDGES] & 1);
    uint32_t na = lvl ? a.n_h : a.n_l, nb = lvl ? b.n_h : b.n_l;
    int      is_a = (d == na) || (d == na + 1), is_b = (d == nb) || (d == nb + 1);

    if(!is_a && !is_b)
      bad++;

    if(is_b && !is_a)
    {
      news++;

      if(!first_new)
        first_new = i - from + 1;
    }
    else if(news)
      bad++;                                                //an old step after a new one
  }

  T_EQ(bad, 0);
  T_TRUE(news > 10);
  //The step running at the commit completes old; the first complete edge interval after it (step k + 1) is new
  T_EQ(first_new, 1);
  T_TRUE((fm.gen_edge[from % FM_GEN_EDGES] >> 1) >= t_commit);

  //Two commits within one step: B is taken, C replaces it before the boundary, so B never runs
  fm_reset(GEN_VER, 199999, 22);
  {
    gp_square slow = { 200000000, 200000000, 0, 0 };       //1 s steps

    fpga_gen_square(&slow);
    fpga_gen_drive(1);
    gen_advance(1000000);
    fpga_gen_square(&b);                                    //taken at once
    fpga_gen_square(&c);                                    //taken at once over it
    T_EQ(fm.gen_sh[0].dur, c.n_h);
    from = fm.gen_nedge;
    gen_advance(4500000000ull);                             //the slow step completes, then C
    for(i = from; i + 1 < fm.gen_nedge; i++)
    {
      uint64_t d = (fm.gen_edge[(i + 1) % FM_GEN_EDGES] >> 1) - (fm.gen_edge[i % FM_GEN_EDGES] >> 1);
      uint8_t  lvl = (uint8_t)(fm.gen_edge[i % FM_GEN_EDGES] & 1);

      if((d == (lvl ? b.n_h : b.n_l)) || (d == (lvl ? b.n_h : b.n_l) + 1))
        bad++;
    }
    T_EQ(bad, 0);
    T_TRUE(fm.gen_nedge - from > 100);
  }
}

//The 0x39 status (bytes 0 and 3: running, pending, the step bit, mode active; 5 strobes) through high and low; the stop.
//Entries other than 0 / 1 and the cut pattern registers (0x6E, 0x6F, 0x76..0x7D) change nothing on the model, as on
//fpga-d3's RTL
static void test_gen_status(void)
{
  gp_square sq = { 1000, 3000, 0, 0 };                      //5 us high, 15 us low
  uint32_t  s0;
  uint8_t   st[2];

  fm_reset(GEN_VER, 199999, 23);
  fpga_gen_status(st);
  T_EQ(st[0], 0); T_EQ(st[1], 0);
  fpga_gen_square(&sq);
  fpga_gen_drive(1);
  fm_advance_ns(1000);
  s0 = fm.strobes;
  fpga_gen_status(st);
  T_EQ(fm.strobes - s0, 5);
  T_EQ(st[0], 0x80); T_EQ(st[1], 1);                        //running, entry 0 (high), the commit taken by the start
  T_EQ(fm_pin(), 1);
  fm_advance_ns(10000);
  fpga_gen_status(st);
  T_EQ(st[0], 0x81);                                        //entry 1 (low)
  T_EQ(fm_pin(), 0);

  //Index 2 and above, 0x6E, 0x6F and the engine's 0x76..0x7D: ignored
  {
    fpga_gen_step s = { FPGA_GEN_S_LEVEL, 0, 77 };
    uint8_t c;

    fpga_gen_step_write(2, &s);
    fpga_gen_step_write(8, &s);
    for(c = 0x76; c <= 0x7D; c++)
    {
      fpga_write_cmd(c);
      fpga_write_byte(0xFF);
    }
    fpga_write_cmd(0x6F);
    fpga_write_byte(0xFF);
    fpga_write_cmd(0x6E);
    fpga_write_byte(0xFF);
    T_EQ(fm.gen_act[0].dur, 1000); T_EQ(fm.gen_act[1].dur, 3000); T_EQ(fm.gen_sh[0].dur, 1000);
    T_EQ(fm.gen_run, 1); T_EQ(fm.gen_starts, 1);
  }

  //Off: mode 0 at the low idle level, the status reads 0
  fpga_gen_level(0);
  fpga_gen_status(st);
  T_EQ(st[0], 0); T_EQ(st[1], 0);
  T_EQ(fm_pin(), 0);
}

//fw 0.2.105 (fw-capstage): the bus side of one capture stage training capture on 0x153D at the measured 0.30 us per
//strobe: 0x63, the arm of 8000 words at divider 0 with the input quiet (auto time-out), the 0x0A polls, the 0x36 read.
//The wizard's poll also scans the touch panel (fpga_done_conversion, not modelled); the training's time budget is
//firmware/AGENTS.md section 86
static void test_train_capture_cost(void)
{
  fpga_arm_cfg cfg;
  uint64_t     t0, us;
  uint32_t     polls = 0, k;

  fm_reset(0x153D, 0, 31);
  fm.strobe_ns = 300;
  fm.capture   = 1;
  fm.trig_at   = -1;
  write_0d(0);
  fpga_write_cmd(0x1A);
  fpga_write_byte(0x00);
  t0 = fm.t_ps;

  fpga_write_cmd(0x63);
  fpga_write_short(0x08E3);

  memset(&cfg, 0, sizeof(cfg));
  cfg.pre_words    = 4000;
  cfg.total_words  = 8000;
  cfg.write_0e     = 1;
  cfg.auto_timeout = 8000;
  T_EQ(fpga_arm_reset(&cfg), 0);

  do
  {
    fpga_write_cmd(0x0A);
    polls++;
  } while(!(fpga_read_byte() & 1) && (polls < 100000));

  fpga_write_cmd(0x36);

  for(k=0;k<10;k++)
    (void)fpga_read_byte();

  us = (fm.t_ps - t0) / 1000000;
  printf("  training capture on 0x153D: %u us of bus and capture, %u polls\n", (unsigned)us, (unsigned)polls);

  //8000 words at 10 ns are 80 us; the rest is the strobes. Under 0.2 ms each (section 86)
  T_TRUE(us >= 80);
  T_TRUE(us < 200);
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
  test_force_deadline();
  test_trig_ext();
  test_peak();
  test_fullrate();
  test_counter_derive();
  test_counter();
  test_counter_auto();
  test_gen_formulas();
  test_gen_square();
  test_gen_change();
  test_gen_status();
  test_train_capture_cost();
  return t_report("test_fpga_bus");
}
