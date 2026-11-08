//----------------------------------------------------------------------------------------------------------------------------------
// test_cap_hw.c - the target glue of the capture stages (cap_hw.c) against a mock FPGA bus and SD sector 708: the
// self-check's repeat watch, REPEAT and the runtime guard only act on captures taken at the 0x63 value the firmware wrote
// (fw-guardfix, bench 2026-10-22: a DEBUG_FPGA write of 0x63 by the capture-window sweep made the guard move and store a
// core). fw-153f (0x153F): one stage per chip, the guard moves a flagged core's chip. cap_hw.c is included so every
// test starts from a fresh boot (its state is static)
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include "test_util.h"
#include "../cap_hw.c"

//Mock FPGA: 0x63 / 0x64 writes, the 0x36 read (the 0x63 value, then the four counts)
static struct
{
  uint8_t  cmd;
  uint16_t reg63;
  uint8_t  reg64;
  uint16_t cnt[CS_CORES];
  uint32_t rd;                        //read_short index within the 0x36 read
  uint32_t writes63;
} fm;

void fpga_write_cmd(uint8 command)  { fm.cmd = command; fm.rd = 0; }
void fpga_write_byte(uint8 data)    { if(fm.cmd == 0x64) fm.reg64 = data; }
void fpga_write_short(uint16 data)  { if(fm.cmd == 0x63) { fm.reg63 = data; fm.writes63++; } }

uint16 fpga_read_short(void)
{
  uint32_t i = fm.rd++;

  if(fm.cmd != 0x36)
    return 0;

  return (i == 0) ? fm.reg63 : fm.cnt[i - 1];
}

//A write that does not pass fpga_capture_write (DEBUG_FPGA, fpga/al3/tools/capwin_sweep.py)
static void raw_write_63(uint16_t v)
{
  fm.reg63 = v;
}

static uint32_t now_ms;
uint32_t pc_hal_ms(void) { return now_ms; }

//fw-capgate: what the FPGA holds (1: divider 0, a filter or peak detect; 0: Filter Off at a divided rate)
static int full_rate = 1;
int pc_glue_adc_full_rate(void) { return full_rate; }

static struct { uint32_t n; uint8_t a; uint16_t b; } ev;
void pc_usb_log_fpga_capture(uint8 a, uint16 b) { ev.n++; ev.a = a; ev.b = b; }

static uint8_t  sd708[512];
static uint32_t sd_writes;

int pc_hal_sd_read(uint32_t sector, uint32_t count, uint8_t *dst)
{
  if((sector != INPUT_CALIBRATION_SECTOR) || (count != 1))
    return -1;

  memcpy(dst, sd708, 512);
  return 0;
}

int pc_hal_sd_write(uint32_t sector, uint32_t count, const uint8_t *src)
{
  if((sector != INPUT_CALIBRATION_SECTOR) || (count != 1))
    return -1;

  memcpy(sd708, src, 512);
  sd_writes++;
  return 0;
}

//A fresh boot on the built-for bitstream: sector 708 holds stored, trained on the bitstream fpga, with the clean sets
//(stored 0 = no trailer)
static void boot_on(uint16_t stored, uint16_t sets, uint16_t fpga)
{
  memset(&fm, 0, sizeof(fm));
  memset(&ev, 0, sizeof(ev));
  memset(sd708, 0, sizeof(sd708));
  sd_writes = 0;
  now_ms    = 1000;
  full_rate = 1;

  memset(&cap_st, 0, sizeof(cap_st));
  memset(&cap_watch, 0, sizeof(cap_watch));
  cap_training = 0; cap_stored_sets = 0; cap_sets = 0; cap_sets_v = 0; cap_moved = 0; cap_store = 0;
  memset(cap_train, 0, sizeof(cap_train));

  if(stored)
  {
    cs_trailer_pack(stored, fpga, sd708 + CS_TRAILER_OFFSET);
    cs_ext_pack(sd708, sets);
  }

  fpga_capture_init();
  fpga_capture_loaded(sd708);
}

//Trained on the bitstream this firmware is built for (PC_FW_FPGA_VERSION)
static void boot(uint16_t stored, uint16_t sets)
{
  boot_on(stored, sets, PC_FW_FPGA_VERSION);
}

//One accepted full-rate capture with CH1 A garbled (the other cores quiet), 5 s after the previous one, then the MAIN
//loop's service
static void capture_ch1a_garbled(void)
{
  memset(fm.cnt, 0, sizeof(fm.cnt));
  fm.cnt[0] = 150;
  now_ms += 5000;
  fpga_capture_check();
  fpga_capture_service();
}

//0x153F values (both cores of a chip at one stage): CH1 at 4 / 3, CH2 at 3
#define V_4   0x06E4
#define V_3   0x06DB

//Control: the trained value in effect garbles; the guard moves the chip of CH1 A once and stores the new value
static void test_guard_acts_on_own_value(void)
{
  uint16_t v, f, sets;
  pc_hw_cap c;

  T_EQ(V_4, cs_with_chip(CS_PO, 0, 4));
  T_EQ(V_3, CS_PO);
  boot(V_4, 0xFFFF);
  T_EQ(fm.reg63, V_4);
  T_EQ(fm.reg64, CS_THR_USE);
  T_TRUE(cap_st.flags & PC_CAPF_TRAINED);

  capture_ch1a_garbled();
  capture_ch1a_garbled();
  T_EQ(fm.reg63, V_4);
  T_EQ(ev.n, 0);
  capture_ch1a_garbled();

  //CH1 4 removed from 2..5 for both cores: {2, 3, 5} -> 3, written into both fields; event a = 0x20 | both cores
  T_EQ(fm.reg63, V_3);
  pc_hal_cap_state(&c);
  T_EQ(c.value, V_3);
  T_EQ(c.stored, V_3);
  T_TRUE(c.flags & PC_CAPF_MOVED);
  T_FALSE(c.flags & PC_CAPF_REPEAT);
  T_EQ(ev.n, 1); T_EQ(ev.a, 0x23); T_EQ(ev.b, V_3);
  T_EQ(sd_writes, 1);
  T_EQ(cs_trailer_unpack(sd708 + CS_TRAILER_OFFSET, &v, &f), CS_T_VALID);
  T_EQ(v, V_3); T_EQ(f, PC_FW_FPGA_VERSION);
  T_EQ(cs_ext_unpack(sd708, &sets), 1);
  T_EQ(cs_sets_core(sets, 0), 0x2C);
  T_EQ(cs_sets_core(sets, 1), 0x2C);
  T_EQ(cs_sets_core(sets, 2), 0x3C);

  //Moved once per boot: CH1 B now flags at 3 three times, REPEAT, no second move
  {
    int i;

    for(i=0;i<3;i++)
    {
      memset(fm.cnt, 0, sizeof(fm.cnt));
      fm.cnt[1] = 150;
      now_ms += 5000;
      fpga_capture_check();
      fpga_capture_service();
    }
  }

  pc_hal_cap_state(&c);
  T_EQ(fm.reg63, V_3);
  T_TRUE(c.flags & PC_CAPF_REPEAT);
  T_EQ(ev.n, 2); T_EQ(ev.a, 0x02);
  T_EQ(sd_writes, 1);
}

//fw-capgate (bench 2026-11-06): Filter Off at a divided rate. The counters differ between the cores on a real edge
//(the encodes are gated, the cores sample half a record sample apart): the capture is not read at all, so it neither
//flags, counts toward REPEAT nor reaches the guard, and it leaves no trace in the state. Back at full rate three flags
//are needed again
static void test_gated_capture_is_not_checked(void)
{
  pc_hw_cap c;
  int i;

  boot(V_4, 0xFFFF);
  full_rate = 0;

  for(i=0;i<5;i++)
    capture_ch1a_garbled();

  T_EQ(fm.reg63, V_4);
  T_EQ(ev.n, 0);
  T_EQ(sd_writes, 0);
  pc_hal_cap_state(&c);
  T_EQ(c.value, V_4);
  T_EQ(c.reads, 0);
  T_EQ(c.flagged_n, 0);
  T_EQ(c.flagged, 0);
  T_FALSE(c.flags & (PC_CAPF_READ | PC_CAPF_FLAGGED | PC_CAPF_REPEAT | PC_CAPF_MOVED));

  full_rate = 1;
  capture_ch1a_garbled();
  capture_ch1a_garbled();
  T_EQ(fm.reg63, V_4);
  T_EQ(ev.n, 0);
  pc_hal_cap_state(&c);
  T_EQ(c.reads, 2);
  T_EQ(c.flagged_n, 2);

  //A gated capture in between changes nothing
  full_rate = 0;
  capture_ch1a_garbled();
  full_rate = 1;
  T_EQ(fm.reg63, V_4);

  capture_ch1a_garbled();
  T_EQ(fm.reg63, V_3);
  pc_hal_cap_state(&c);
  T_TRUE(c.flags & PC_CAPF_MOVED);
  T_EQ(c.reads, 3);
}

//fw-csbusy (bench 2026-11-08, fw 0.3.1 on 0x153F): a 3.3 V 24.576 MHz clock on CH1 at 200 MSa/s makes both CH1 cores
//count about 43000, about 2 % apart (the cores' gain mismatch). The old absolute limit flagged most captures and the
//guard moved CH1 3 -> 4 and stored it. Now the chip abstains: nothing flags, nothing moves, nothing is stored or
//logged, the counts stay readable; a quiet-input garble afterwards still moves the chip after three flags
static void test_busy_signal_is_not_flagged(void)
{
  pc_hw_cap c;
  uint32_t w0;
  int i;

  boot(V_3, 0xFFFF);
  w0 = fm.writes63;

  for(i=0;i<20;i++)
  {
    fm.cnt[0] = (uint16_t)(42290 + 37 * i); fm.cnt[1] = 43153; fm.cnt[2] = 0; fm.cnt[3] = 0;
    if(i & 1)
    {
      fm.cnt[0] = 43153; fm.cnt[1] = (uint16_t)(42290 + 37 * i);
    }
    now_ms += 1000;
    fpga_capture_check();
    fpga_capture_service();
  }

  T_EQ(fm.reg63, V_3);
  T_EQ(fm.writes63, w0);              //no 0x63 write
  T_EQ(ev.n, 0);
  T_EQ(sd_writes, 0);
  pc_hal_cap_state(&c);
  T_EQ(c.value, V_3);
  T_EQ(c.reads, 20);
  T_EQ(c.flagged_n, 0);
  T_EQ(c.flagged, 0);
  T_EQ(c.cnt[0], 43153);              //the last capture (odd: B the lower)
  T_TRUE(c.flags & PC_CAPF_READ);
  T_FALSE(c.flags & (PC_CAPF_FLAGGED | PC_CAPF_REPEAT | PC_CAPF_MOVED));

  //Then a quiet input with CH1 A garbled: the watch has nothing from the busy captures, the third flag moves
  capture_ch1a_garbled();
  capture_ch1a_garbled();
  T_EQ(fm.reg63, V_3);
  capture_ch1a_garbled();
  pc_hal_cap_state(&c);
  T_TRUE(c.flags & PC_CAPF_MOVED);
  T_EQ(c.flagged_n, 3);
  T_EQ(ev.n, 1); T_EQ(ev.a, 0x23);
  T_EQ(sd_writes, 1);
}

//The bench bug: the sweep writes 0x63 = V_4 through DEBUG_FPGA while the stored (and written) value is V_3, and CH1 at
//stage 4 garbles. No move, no store, no log, no REPEAT; the counts are still read. Back at the firmware's value
//the earlier captures do not count: three more flags are needed
static void test_guard_ignores_foreign_value(void)
{
  uint16_t v, f;
  uint32_t k;
  pc_hw_cap c;

  boot(V_3, 0xFFFF);
  T_EQ(fm.reg63, V_3);
  T_TRUE(cap_st.flags & PC_CAPF_TRAINED);
  T_EQ(fm.writes63, 2);               //boot: the power-on value, then the stored value at the calibration load

  raw_write_63(V_4);

  for(k=0;k<6;k++)
    capture_ch1a_garbled();

  pc_hal_cap_state(&c);
  T_EQ(fm.reg63, V_4);                //the sweep's value stays
  T_EQ(fm.writes63, 2);               //the firmware wrote no 0x63 since
  T_EQ(c.value, V_3);
  T_EQ(c.stored, V_3);
  T_TRUE(c.flags & PC_CAPF_TRAINED);
  T_FALSE(c.flags & (PC_CAPF_MOVED | PC_CAPF_REPEAT));
  T_EQ(ev.n, 0);
  T_EQ(sd_writes, 0);
  T_EQ(cs_trailer_unpack(sd708 + CS_TRAILER_OFFSET, &v, &f), CS_T_VALID);
  T_EQ(v, V_3);
  T_EQ(c.reads, 6);                   //GET_HEALTH still shows what the FPGA counted
  T_EQ(c.flagged_n, 6);
  T_EQ(c.flagged, 0x01);
  T_EQ(c.cnt[0], 150);

  //The sweep restores the firmware's value; now CH1 3 garbles: two flags are not enough, the third moves the chip
  raw_write_63(V_3);
  capture_ch1a_garbled();
  capture_ch1a_garbled();
  T_EQ(fm.reg63, V_3);
  T_EQ(ev.n, 0);
  capture_ch1a_garbled();
  T_EQ(ev.n, 1); T_EQ(ev.a, 0x23);
  T_EQ(fm.reg63, V_4);                //{2, 4, 5}: 4
  T_EQ(sd_writes, 1);
}

//Without a trained value REPEAT follows the same rule. And a raw write of the stored value while the firmware wrote
//another one since (an unsaved wizard result): not the firmware's value either
static void test_repeat_ignores_foreign_value(void)
{
  uint32_t k;
  pc_hw_cap c;

  //No trailer: the power-on value written, not trained
  boot(0, 0);
  T_EQ(fm.reg63, CS_PO);
  T_FALSE(cap_st.flags & PC_CAPF_TRAINED);

  raw_write_63(cs_with_chip(CS_PO, 0, 2));

  for(k=0;k<4;k++)
    capture_ch1a_garbled();

  pc_hal_cap_state(&c);
  T_FALSE(c.flags & PC_CAPF_REPEAT);
  T_EQ(ev.n, 0);

  //At the firmware's value: REPEAT and event 23 once (a = the core; no trained set, nothing to move)
  raw_write_63(CS_PO);

  for(k=0;k<3;k++)
    capture_ch1a_garbled();

  pc_hal_cap_state(&c);
  T_TRUE(c.flags & PC_CAPF_REPEAT);
  T_FALSE(c.flags & PC_CAPF_MOVED);
  T_EQ(ev.n, 1); T_EQ(ev.a, 0x01);
  T_EQ(fm.reg63, CS_PO);
  T_EQ(sd_writes, 0);

  //Trained V_3, the firmware wrote V_4 since, the FPGA holds V_3 through a raw write: no move, no REPEAT
  boot(V_3, 0xFFFF);
  fpga_capture_write(V_4);
  T_FALSE(cap_st.flags & PC_CAPF_TRAINED);
  raw_write_63(V_3);

  for(k=0;k<4;k++)
    capture_ch1a_garbled();

  pc_hal_cap_state(&c);
  T_FALSE(c.flags & (PC_CAPF_MOVED | PC_CAPF_REPEAT));
  T_EQ(ev.n, 0);
  T_EQ(sd_writes, 0);
  T_EQ(fm.reg63, V_3);

  //0x153F: the dead bits 15:12 in the FPGA's copy (a DEBUG_FPGA write) are not the firmware's concern: the value it
  //compares is 0x63 & CS_MASK
  boot(V_3, 0xFFFF);
  raw_write_63(V_3 | 0x3000);

  for(k=0;k<3;k++)
    capture_ch1a_garbled();

  T_EQ(ev.n, 1); T_EQ(ev.a, 0x23);
  T_EQ(fm.reg63, V_4);
}

//fw-153e / fw-153f: a card trained on the previous bitstream (0x153E, the release before 0x153F): the stored value is
//not applied (the power-on value, OTHER: Diagnostics asks for Calibrate), the guard and the repeat watch's store do
//nothing, the card is not written; the wizard's commit binds the new value to the built-for word and the next save
//stores that
static void test_stored_for_previous_bitstream(void)
{
  uint16_t v, f;
  uint32_t k;
  uint8_t  sec[512];
  pc_hw_cap c;

  T_TRUE(PC_FW_FPGA_VERSION != 0x153E);
  T_EQ(CS_PO, 0x06DB);
  boot_on(0x08E3, 0xFFFF, 0x153E);
  pc_hal_cap_state(&c);
  T_EQ(fm.reg63, CS_PO);
  T_EQ(fm.reg64, CS_THR_USE);
  T_EQ(c.value, CS_PO);
  T_EQ(c.trailer, CS_T_VALID);
  T_EQ(c.stored, 0x08E3);
  T_EQ(c.stored_fpga, 0x153E);
  T_TRUE(c.flags & PC_CAPF_HAS);
  T_TRUE(c.flags & PC_CAPF_OTHER);
  T_FALSE(c.flags & PC_CAPF_TRAINED);

  //Garbled captures at the power-on value: REPEAT once, nothing moved, nothing stored
  for(k=0;k<4;k++)
    capture_ch1a_garbled();

  pc_hal_cap_state(&c);
  T_TRUE(c.flags & PC_CAPF_REPEAT);
  T_FALSE(c.flags & PC_CAPF_MOVED);
  T_EQ(fm.reg63, CS_PO);
  T_EQ(sd_writes, 0);

  //A save without a new training keeps the card's trailer as it was (0x153E's value, still not applied)
  memset(sec, 0, sizeof(sec));
  fpga_capture_save(sec);
  T_EQ(cs_trailer_unpack(sec + CS_TRAILER_OFFSET, &v, &f), CS_T_VALID);
  T_EQ(v, 0x08E3); T_EQ(f, 0x153E);

  //The wizard's value, committed at its Save: trained on the built-for bitstream, OTHER and REPEAT gone
  fpga_capture_write(V_4);
  fpga_capture_commit();
  pc_hal_cap_state(&c);
  T_EQ(c.stored, V_4);
  T_EQ(c.stored_fpga, PC_FW_FPGA_VERSION);
  T_TRUE(c.flags & PC_CAPF_TRAINED);
  T_FALSE(c.flags & (PC_CAPF_OTHER | PC_CAPF_REPEAT));
  memset(sec, 0, sizeof(sec));
  fpga_capture_save(sec);
  T_EQ(cs_trailer_unpack(sec + CS_TRAILER_OFFSET, &v, &f), CS_T_VALID);
  T_EQ(v, V_4); T_EQ(f, PC_FW_FPGA_VERSION);

  //The next boot applies it
  boot_on(V_4, 0, PC_FW_FPGA_VERSION);
  T_EQ(fm.reg63, V_4);
  T_TRUE(cap_st.flags & PC_CAPF_TRAINED);

  //A 0x153E trailer with a reference bit (0x153A..0x153E's B timing) is valid and OTHER, not applied
  boot_on(0x2924, 0xFFFF, 0x153E);
  pc_hal_cap_state(&c);
  T_EQ(c.trailer, CS_T_VALID);
  T_EQ(c.stored, 0x2924);
  T_TRUE(c.flags & PC_CAPF_OTHER);
  T_EQ(fm.reg63, CS_PO);
}

//1.28 (fw-capmetric): a training's quiet sets, residuals and sums per channel for GET_CAL (pc_hal_cap_train). Mock
//0x153F (both cores at the chip's stage, bits 8:6): CH2 A dirty at 2, its residual q per capture 40 at stage 5, 10 at
//the others (a class (a) mix at 5); CH2 B dirty at 2 and 4, q 1 per capture. fw-153f: the chip's set {3} in both
static uint32_t tr_parks;

static int tr_park(void *ctx, uint32_t code16)
{
  (void)ctx;
  (void)code16;
  tr_parks++;
  return 0;
}

static int tr_measure(void *ctx, uint16_t v, uint8_t resid, uint16_t cnt[CS_CORES], uint32_t e[CS_CORES])
{
  uint8_t sa = cs_stage(v, 2);

  (void)ctx;
  memset(cnt, 0, CS_CORES * sizeof(cnt[0]));
  cnt[2] = (sa == 2) ? 500 : 0;
  cnt[3] = ((sa == 2) || (sa == 4)) ? 400 : 0;

  if(resid & 0x04)
    e[2] = CS_RESID_DIV * ((sa == 5) ? 40u : 10u);
  if(resid & 0x08)
    e[3] = CS_RESID_DIV;

  return 0;
}

//The same with CH2 B dirty at 3 as well: A {3, 4}, B {5}, nothing in common
static int tr_measure_b3(void *ctx, uint16_t v, uint8_t resid, uint16_t cnt[CS_CORES], uint32_t e[CS_CORES])
{
  uint8_t sa = cs_stage(v, 2);

  tr_measure(ctx, v, resid, cnt, e);
  cnt[3] = (sa == 5) ? 0 : 400;
  return 0;
}

static void test_train_note(void)
{
  static const cs_io io = { 0, 2, 1, 0, tr_park, tr_measure };
  pc_hw_cap_train ct;
  cs_train        t;

  boot(0, 0);
  pc_hal_cap_train(1, &ct);
  T_EQ(ct.flags, 0);

  T_EQ(fpga_capture_train(0x0C, &io, &t), CS_OK);
  pc_hal_cap_train(0, &ct);
  T_EQ(ct.flags, 0);                  //CH1 not trained
  pc_hal_cap_train(1, &ct);
  T_EQ(ct.flags, PC_CAPT_RAN);
  T_EQ(ct.set[0], 0x08);              //A: q 20, 20, 80 at 3..5 (20 x 5 / 4 + 4 = 29, 5 out): {3, 4}; B {3, 5}
  T_EQ(ct.set[1], 0x08);
  T_EQ(ct.q[0][0], 20);
  T_EQ(ct.q[0][3], 80);
  T_EQ(fm.reg63, cs_with_chip(CS_PO, 1, 3));
  T_EQ(cs_stage(fm.reg63, 3), 3);
  T_EQ(ct.sum[0][0], 2 * 500);
  T_EQ(ct.sum[0][1], 0);
  T_EQ(ct.q[1][1], 2);                //core B's own numbers
  T_EQ(ct.sum[1][0], 2 * 400);
  T_EQ(ct.sum[1][1], 0);
  T_EQ(ct.sum[1][2], 2 * 400);

  //No common stage (B dirty at 3 too): failed, nothing changes
  boot(0, 0);
  {
    static const cs_io io2 = { 0, 2, 1, 0, tr_park, tr_measure_b3 };

    T_EQ(fpga_capture_train(0x0C, &io2, &t), CS_E_DIRTY);
  }
  pc_hal_cap_train(1, &ct);
  T_EQ(ct.flags, PC_CAPT_RAN | PC_CAPT_FAILED);
  T_EQ(ct.set[0], 0);
  T_EQ(t.bad, 0x0C);
  T_EQ(fm.reg63, CS_PO);
  T_EQ(ev.a, 0x10 | 0x0C);
}

int main(void)
{
  test_guard_acts_on_own_value();
  test_gated_capture_is_not_checked();
  test_busy_signal_is_not_flagged();
  test_guard_ignores_foreign_value();
  test_repeat_ignores_foreign_value();
  test_stored_for_previous_bitstream();
  test_train_note();
  return t_report("test_cap_hw");
}
