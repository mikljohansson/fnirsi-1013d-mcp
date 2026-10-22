//----------------------------------------------------------------------------------------------------------------------------------
// test_cap_hw.c - the target glue of the capture stages (cap_hw.c) against a mock FPGA bus and SD sector 708: the
// self-check's repeat watch, REPEAT and the runtime guard only act on captures taken at the 0x63 value the firmware wrote
// (fw-guardfix, bench 2026-10-22: a DEBUG_FPGA write of 0x63 by the capture-window sweep made the guard move and store a
// core). cap_hw.c is included so every test starts from a fresh boot (its state is static)
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

//A fresh boot on 0x153D: sector 708 holds stored (trained on 0x153D) with the clean sets (stored 0 = no trailer)
static void boot(uint16_t stored, uint16_t sets)
{
  memset(&fm, 0, sizeof(fm));
  memset(&ev, 0, sizeof(ev));
  memset(sd708, 0, sizeof(sd708));
  sd_writes = 0;
  now_ms    = 1000;

  memset(&cap_st, 0, sizeof(cap_st));
  memset(&cap_watch, 0, sizeof(cap_watch));
  cap_fpga = 0; cap_training = 0; cap_stored_sets = 0; cap_sets = 0; cap_sets_v = 0; cap_moved = 0; cap_store = 0;
  memset(cap_train, 0, sizeof(cap_train));

  if(stored)
  {
    cs_trailer_pack(stored, 0x153D, sd708 + CS_TRAILER_OFFSET);
    cs_ext_pack(sd708, sets);
  }

  fpga_capture_init(0x153D);
  fpga_capture_loaded(sd708);
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

//Control: the trained value in effect garbles; the guard moves CH1 A once and stores the new value
static void test_guard_acts_on_own_value(void)
{
  uint16_t v, f;
  pc_hw_cap c;

  boot(0x08E4, 0xFFFF);
  T_EQ(fm.reg63, 0x08E4);
  T_EQ(fm.reg64, CS_THR_USE);
  T_TRUE(cap_st.flags & PC_CAPF_TRAINED);

  capture_ch1a_garbled();
  capture_ch1a_garbled();
  T_EQ(fm.reg63, 0x08E4);
  T_EQ(ev.n, 0);
  capture_ch1a_garbled();

  //CH1 A 4 removed from 2..5: {2, 3, 5} -> 3
  T_EQ(fm.reg63, 0x08E3);
  pc_hal_cap_state(&c);
  T_EQ(c.value, 0x08E3);
  T_EQ(c.stored, 0x08E3);
  T_TRUE(c.flags & PC_CAPF_MOVED);
  T_FALSE(c.flags & PC_CAPF_REPEAT);
  T_EQ(ev.n, 1); T_EQ(ev.a, 0x21); T_EQ(ev.b, 0x08E3);
  T_EQ(sd_writes, 1);
  T_EQ(cs_trailer_unpack(sd708 + CS_TRAILER_OFFSET, &v, &f), CS_T_VALID);
  T_EQ(v, 0x08E3); T_EQ(f, 0x153D);
}

//The bench bug: the sweep writes 0x63 = 0x08E4 through DEBUG_FPGA while the stored (and written) value is 0x08E3, and
//CH1 A at stage 4 garbles. No move, no store, no log, no REPEAT; the counts are still read. Back at the firmware's value
//the earlier captures do not count: three more flags are needed
static void test_guard_ignores_foreign_value(void)
{
  uint16_t v, f;
  uint32_t k;
  pc_hw_cap c;

  boot(0x08E3, 0xFFFF);
  T_EQ(fm.reg63, 0x08E3);
  T_TRUE(cap_st.flags & PC_CAPF_TRAINED);
  T_EQ(fm.writes63, 2);               //boot: 0x08E4, then the stored value at the calibration load

  raw_write_63(0x08E4);

  for(k=0;k<6;k++)
    capture_ch1a_garbled();

  pc_hal_cap_state(&c);
  T_EQ(fm.reg63, 0x08E4);             //the sweep's value stays
  T_EQ(fm.writes63, 2);               //the firmware wrote no 0x63 since
  T_EQ(c.value, 0x08E3);
  T_EQ(c.stored, 0x08E3);
  T_TRUE(c.flags & PC_CAPF_TRAINED);
  T_FALSE(c.flags & (PC_CAPF_MOVED | PC_CAPF_REPEAT));
  T_EQ(ev.n, 0);
  T_EQ(sd_writes, 0);
  T_EQ(cs_trailer_unpack(sd708 + CS_TRAILER_OFFSET, &v, &f), CS_T_VALID);
  T_EQ(v, 0x08E3);
  T_EQ(c.reads, 6);                   //GET_HEALTH still shows what the FPGA counted
  T_EQ(c.flagged_n, 6);
  T_EQ(c.flagged, 0x01);
  T_EQ(c.cnt[0], 150);

  //The sweep restores the firmware's value; now CH1 A3 garbles: two flags are not enough, the third moves it
  raw_write_63(0x08E3);
  capture_ch1a_garbled();
  capture_ch1a_garbled();
  T_EQ(fm.reg63, 0x08E3);
  T_EQ(ev.n, 0);
  capture_ch1a_garbled();
  T_EQ(ev.n, 1); T_EQ(ev.a, 0x21);
  T_TRUE(fm.reg63 != 0x08E3);
  T_EQ(sd_writes, 1);
}

//Without a trained value REPEAT follows the same rule. And a raw write of the stored value while the firmware wrote
//another one since (an unsaved wizard result): not the firmware's value either
static void test_repeat_ignores_foreign_value(void)
{
  uint32_t k;
  pc_hw_cap c;

  //No trailer: the power-on 0x08E4 written, not trained
  boot(0, 0);
  T_EQ(fm.reg63, CS_PO);
  T_FALSE(cap_st.flags & PC_CAPF_TRAINED);

  raw_write_63(0x08E2);

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

  //Trained 0x08E3, the firmware wrote 0x08E4 since, the FPGA holds 0x08E3 through a raw write: no move, no REPEAT
  boot(0x08E3, 0xFFFF);
  fpga_capture_write(0x08E4);
  T_FALSE(cap_st.flags & PC_CAPF_TRAINED);
  raw_write_63(0x08E3);

  for(k=0;k<4;k++)
    capture_ch1a_garbled();

  pc_hal_cap_state(&c);
  T_FALSE(c.flags & (PC_CAPF_MOVED | PC_CAPF_REPEAT));
  T_EQ(ev.n, 0);
  T_EQ(sd_writes, 0);
  T_EQ(fm.reg63, 0x08E3);
}

//1.28 (fw-capmetric): a training's quiet sets, residuals and sums per channel for GET_CAL (pc_hal_cap_train). Mock:
//CH2 A dirty at 2, its residual q per capture 40 at stage 5, 10 at the others (a class (a) mix at 5); CH2 B dirty with
//its own encode at every stage, clean with the A encode at 3..5, q 1 per capture
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
  uint8_t sa = cs_stage(v, 2), sb = cs_stage(v, 3);

  (void)ctx;
  memset(cnt, 0, CS_CORES * sizeof(cnt[0]));
  cnt[2] = (sa == 2) ? 500 : 0;
  cnt[3] = (!(v & CS_REF_CH2B) || (sb == 2)) ? 400 : 0;

  if(resid & 0x04)
    e[2] = CS_RESID_DIV * ((sa == 5) ? 40u : 10u);
  if(resid & 0x08)
    e[3] = CS_RESID_DIV;

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
  T_EQ(ct.flags, PC_CAPT_RAN | PC_CAPT_REF);
  T_EQ(ct.set[0], 0x18);              //q 20, 20, 80 at 3..5: 20 x 5 / 4 + 4 = 29, 5 out
  T_EQ(ct.set[1], 0x38);
  T_EQ(ct.q[0][0], 20);
  T_EQ(ct.q[0][3], 80);
  T_EQ(cs_stage(fm.reg63, 2), 3);
  T_EQ(ct.sum[0][0], 2 * 500);
  T_EQ(ct.sum[0][1], 0);
  T_EQ(ct.q[1][1], 2);                //core B: the reference pass's numbers
  T_EQ(ct.sum[1][0], 2 * 400);
  T_EQ(ct.sum[1][1], 0);
  T_EQ(fm.reg63 & CS_REF_CH2B, CS_REF_CH2B);
}

int main(void)
{
  test_guard_acts_on_own_value();
  test_guard_ignores_foreign_value();
  test_repeat_ignores_foreign_value();
  test_train_note();
  return t_report("test_cap_hw");
}
