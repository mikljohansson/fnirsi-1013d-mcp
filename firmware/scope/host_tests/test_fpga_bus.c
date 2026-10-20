//----------------------------------------------------------------------------------------------------------------------------------
// test_fpga_bus.c - the bus sequences of fpga_bus.c (fw 0.2.72) against fpga_model.c:
//   - the contract per bitstream (version word);
//   - every readout returns ring words start .. start + count - 1 exactly (lanes, ring wrap, 16-bit pairs, the 1.00o5
//     two-pass trace read), while the model without the dummy strobe returns mem[S], mem[S], mem[S + 1] (finding 5);
//   - the arm order: on 0x1532..0x1536 at slow legacy rates the 1.00o5 order (0x0B only inside the reset window) loses a
//     changed pretrigger in the model, fpga_arm_reset never does, on any bitstream, rate or strobe timing; the write trace
//     shows 0x0B / 0x0C before 0x01 = 1 and again inside the window;
//   - the bounded ready wait;
//   - fw 0.2.87 (firmware/AGENTS.md section 57): the values the FPGA holds (0x0D, 0x53, 0x54, 0x56) follow every write, and
//     fpga_decim_write writes 0x54 at every arm with the filter on, so the shift always matches the divider the FPGA holds
//     (fpga/al3/AGENTS.md "First-record saturation": S 9 / 7 armed at D 4095 saturated every word).
//----------------------------------------------------------------------------------------------------------------------------------

#include "test_util.h"
#include "fpga_bus.h"
#include "fpga_model.h"
#include "pc_decim.h"

static const uint16_t versions[] = { 0x1432, 0x1532, 0x1533, 0x1534, 0x1535, 0x1536, 0x1537, 0x1538, 0x1539, 0x153F,
                                     0x1632, 0x1732, 0x0000 };
#define NVER (sizeof(versions) / sizeof(versions[0]))

static void test_contract(void)
{
  fpga_bus_contract c;
  uint32_t i;

  for(i=0;i<NVER;i++)
  {
    int prog = (versions[i] >= 0x1532 && versions[i] <= 0x153F) || versions[i] == 0x1632 || versions[i] == 0x1732;

    fpga_bus_contract_for(versions[i], &c);
    T_EQ(c.read_dummy, 1);
    T_EQ(c.prog, prog);
    T_EQ(c.geometry_early, prog);
  }

  //Before fpga_bus_init (boot, before the version read): stock rules
  T_EQ(fpga_bus.read_dummy, 1);
  T_EQ(fpga_bus.prog, 0);
}

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
    fpga_bus_init(versions[v]);

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
  fm_reset(0x1539, 0, 5);
  fm_fill(3);
  fpga_bus_init(0x1539);
  fpga_bus.read_dummy = 0;
  fpga_read_lane(0x20, 40, 100, buf, 1, fm.ring);
  T_EQ(buf[0], fm.mem[40][0]);
  T_EQ(buf[1], fm.mem[40][0]);
  T_EQ(buf[2], fm.mem[41][0]);
  T_EQ(buf[99], fm.mem[138][0]);
  fpga_bus_init(0x1539);
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

  c.geometry     = 1;
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
    fpga_bus_init(versions[v]);

    if(!fpga_bus.prog)
      continue;

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
  T_TRUE(runs >= 5 * 200 * 10);

  //The model reproduces the race of finding 2 with the old order, and 0x1537's fix of it
  fpga_bus_init(0x1535);
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
    fpga_bus_init(versions[v]);
    fm.ntrace = 0;
    arm_once(123, 456);

    i01  = fm_trace_find(0x01, 1, 0);
    i01b = fm_trace_find(0x01, 0, 0);
    i0b  = fm_trace_find(0x0B, 123, 0);
    T_TRUE(i01 >= 0);
    T_TRUE(i01b > i01);

    if(fpga_bus.prog)
    {
      //Geometry before the reset, then again inside the window, 0x0E inside, release last
      T_TRUE(i0b >= 0 && i0b < i01);
      T_TRUE(fm_trace_find(0x0C, 456, 0) < i01);
      i0b2 = fm_trace_find(0x0B, 123, (uint32_t)i01);
      T_TRUE(i0b2 > i01 && i0b2 < i01b);
      T_TRUE(fm_trace_find(0x0C, 456, (uint32_t)i01) > i01);
      T_TRUE(fm_trace_find(0x0E, 3000, (uint32_t)i01) < i01b);
      T_EQ(i01b, (int)fm.ntrace - 1);
    }
    else
    {
      //Stock and unknown: fixed geometry, nothing written to 0x0B / 0x0C
      T_EQ(i0b, -1);
      T_EQ(fm_trace_find(0x0C, 456, 0), -1);
    }
  }
}

static void test_ready_bound(void)
{
  fpga_arm_cfg c;
  uint32_t idles = 0;

  fm_reset(0x1539, 0, 9);
  fpga_bus_init(0x1539);
  fm.ready_never = 1;
  fm.ntrace = 0;

  memset(&c, 0, sizeof(c));
  c.geometry  = 1;
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
  static const uint32_t rings[] = { 4096, 6144, 8192 };
  uint32_t r, a, pre, bad = 0, old_bad = 0;

  for(r=0;r<3;r++)
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
  return fpga_decim_write(pc_decim_mode(c), 1, c->fir_en, c->shift);
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

  fm_reset(0x153A, 199, 21);
  fpga_bus_init(0x153A);
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

int main(void)
{
  test_contract();
  test_write_record();
  test_ring_start();
  test_readout();
  test_arm_order();
  test_arm_trace();
  test_ready_bound();
  test_decim_write();
  return t_report("test_fpga_bus");
}
