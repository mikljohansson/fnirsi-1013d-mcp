//----------------------------------------------------------------------------------------------------------------------------------
// Session / handler tests for pc_cmds.c against mock_hal.c. Host build: make -C firmware/scope/host_tests test
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pc_cmds.h"
#include "mock_hal.h"
#include "test_util.h"

static pc_session S;
static uint16_t   next_seq = 1;

//Decoded response
typedef struct
{
  int       ok;            //frame valid (magic, CRCs, RESPONSE flag)
  pc_header h;
  uint8_t  *p;
  uint32_t  len;
} resp_t;

static resp_t decode(const mock_resp *r)
{
  resp_t d;

  memset(&d, 0, sizeof(d));

  if(r->len < 16 || !pc_header_valid(r->data))
    return d;

  pc_header_decode(r->data, &d.h);
  d.p   = r->data + 16;
  d.len = d.h.len;

  if(r->len != PC_FRAME_SIZE(d.h.len))
    return d;

  if(d.h.len && pc_crc32(d.p, d.h.len) != pc_get_u32(d.p + d.h.len))
    return d;

  d.ok = (d.h.flags == PC_FLAG_RESPONSE) && (d.h.proto_major == 1);
  return d;
}

static void push_frame(uint16_t cmd, uint16_t seq, const void *payload, uint32_t len)
{
  static uint8_t f[600];

  memcpy(f + 16, payload, len);
  mock_rx_push(f, pc_frame_finish(f, cmd, seq, 0, 0, len));
}

//Send one request, poll MAIN until a response with that seq appears (or give up)
static resp_t req_ctx(uint16_t cmd, const void *payload, uint32_t len, int ctx)
{
  uint16_t seq = next_seq++;
  int i;
  uint32_t j;

  mock_clear_resps();
  push_frame(cmd, seq, payload, len);

  for(i=0;i<50;i++)
  {
    pc_poll(&S, ctx);

    for(j=0;j<mock_nresps;j++)
    {
      resp_t d = decode(&mock_resps[j]);
      if(d.ok && d.h.seq == seq)
        return d;
    }

    mock.now += 1;
  }

  {
    static uint8_t zero[70000];
    resp_t none;
    memset(&none, 0, sizeof(none));
    none.p = zero;
    none.h.status = 0xEEEE;
    t_checks++; t_failures++;
    printf("FAIL: no response to cmd 0x%04x seq %u\n", cmd, seq);
    return none;
  }
}

static resp_t req(uint16_t cmd, const void *payload, uint32_t len)
{
  return req_ctx(cmd, payload, len, PC_CTX_MAIN);
}

static void setup(uint16_t version)
{
  mock_reset(version);
  pc_session_init(&S);
  next_seq = 1;
}

static void go_remote(uint8_t flags, uint16_t wd)
{
  uint8_t p[4] = { 1, flags, (uint8_t)wd, (uint8_t)(wd >> 8) };
  resp_t r = req(PC_CMD_SET_REMOTE, p, 4);
  T_EQ(r.h.status, PC_OK);
  T_EQ(r.p[0], PC_REMOTE);
}

//----------------------------------------------------------------------------------------------------------------------------------

static void test_info(void)
{
  resp_t r;

  setup(0x1532);
  r = req(PC_CMD_GET_INFO, 0, 0);
  T_TRUE(r.ok); T_EQ(r.h.status, 0); T_EQ(r.len, 76);
  T_EQ(r.p[0], 1); T_EQ(pc_get_u16(r.p + 2), 512); T_EQ(pc_get_u32(r.p + 4), 66560);
  T_EQ(pc_get_u16(r.p + 8), 0x1532); T_EQ(r.p[10], 2); T_EQ(pc_get_u16(r.p + 12), 8192);
  T_EQ(pc_get_u32(r.p + 20) & (PC_CAP_PROG_TRIGGER_POS | PC_CAP_GENERATOR_DDS), PC_CAP_PROG_TRIGGER_POS | PC_CAP_GENERATOR_DDS);
  T_EQ(pc_get_u32(r.p + 20) & (PC_CAP_READ_LAG_UNVERIFIED | PC_CAP_INTERLEAVE_UNVERIFIED), PC_CAP_READ_LAG_UNVERIFIED | PC_CAP_INTERLEAVE_UNVERIFIED);
  T_EQ(pc_get_u32(r.p + 20) & PC_CAP_HOLDOFF, 0);
  T_EQ(pc_get_u32(r.p + 24), 50000000); T_EQ(pc_get_u16(r.p + 28), 16382); T_EQ(pc_get_u16(r.p + 30), 80);
  T_STR((char *)r.p + 36, PC_FW_VERSION);
  T_EQ(pc_get_u16(r.p + 72), 800);

  //Stock: no programmable geometry, no generator
  setup(0x1432);
  r = req(PC_CMD_GET_INFO, 0, 0);
  T_EQ(r.p[10], 1); T_EQ(pc_get_u16(r.p + 12), 4096);
  T_EQ(pc_get_u32(r.p + 20) & (PC_CAP_PROG_TRIGGER_POS | PC_CAP_GENERATOR_DDS), 0);
  T_EQ(pc_get_u32(r.p + 24), 0);
  T_EQ(pc_get_u16(r.p + 28), 3000); T_EQ(pc_get_u16(r.p + 30), 3000);

  //EP4 is recognised
  setup(0x1732);
  r = req(PC_CMD_GET_INFO, 0, 0);
  T_EQ(r.p[10], 4); T_EQ(pc_get_u16(r.p + 12), 6144); T_EQ(pc_get_u16(r.p + 28), 12286);

  //Unknown variant fails closed: caps 0, only the basic system commands
  setup(0x9999);
  r = req(PC_CMD_GET_INFO, 0, 0);
  T_EQ(r.p[10], 0); T_EQ(pc_get_u32(r.p + 20), 0);
  r = req(PC_CMD_PING, "x", 1);
  T_EQ(r.h.status, PC_OK);
  {
    uint8_t p[4] = { 1, 0, 0, 0 };
    r = req(PC_CMD_SET_REMOTE, p, 4);
    T_EQ(r.h.status, PC_E_UNSUPPORTED);
  }
  r = req(PC_CMD_GET_CHANNEL, "\0", 1);
  T_EQ(r.h.status, PC_E_UNSUPPORTED);
  T_EQ(mock.enters, 0);
}

static void test_gating(void)
{
  uint8_t p[12];
  resp_t r;

  setup(0x1532);
  memset(p, 0, sizeof(p));

  //REMOTE-only command in LOCAL (spec section 13 example status)
  r = req(PC_CMD_SET_CHANNEL, p, 12);
  T_EQ(r.h.status, PC_E_NOT_REMOTE); T_EQ(r.h.cmd, PC_CMD_SET_CHANNEL);
  T_EQ(mock.ch_writes, 0);

  //Read-only commands in LOCAL come from the UI settings, no FPGA access
  r = req(PC_CMD_GET_CHANNEL, "\1", 1);
  T_EQ(r.h.status, 0); T_EQ(r.len, 24); T_EQ(r.p[0], 1); T_EQ(r.p[2], 5); T_EQ(r.p[3], 5);
  T_EQ(pc_get_u16(r.p + 10), 1050); T_EQ(pc_get_u16(r.p + 12), 1050);
  //uv_per_lsb_q8 = 3800000 * 200 * 10 / 8192 = 927734.375 -> 927734
  T_EQ(pc_get_u32(r.p + 16), 927734); T_EQ((int32_t)pc_get_u32(r.p + 20), 128 * 256);

  r = req(PC_CMD_GET_TIMEBASE, 0, 0);
  T_EQ(r.p[0], 24); T_EQ(pc_get_u32(r.p + 4), 19); T_EQ(pc_get_u32(r.p + 8), 10000000); T_EQ(pc_get_u32(r.p + 12), 100000);

  r = req(PC_CMD_ARM, 0, 0);
  T_EQ(r.h.status, PC_E_NOT_REMOTE); T_EQ(mock.arms, 0);

  r = req(0x7777, 0, 0);
  T_EQ(r.h.status, PC_E_UNKNOWN_CMD);

  //Not built in this firmware: capability bits are 0 and the commands fail closed
  r = req(PC_CMD_READ_SCREEN, 0, 0);    T_EQ(r.h.status, PC_E_UNSUPPORTED);
  r = req(PC_CMD_ENTER_FEL, 0, 0);      T_EQ(r.h.status, PC_E_UNSUPPORTED);
  go_remote(0, 0);
  r = req(PC_CMD_ACQUIRE_AVG, 0, 0);    T_EQ(r.h.status, PC_E_UNSUPPORTED);
  r = req(PC_CMD_READ_LIVE, 0, 0);      T_EQ(r.h.status, PC_E_UNSUPPORTED);
#if !PC_DEBUG
  r = req(PC_CMD_FORCE, 0, 0);          T_EQ(r.h.status, PC_E_UNSUPPORTED);
  r = req(PC_CMD_DEBUG_FPGA, 0, 0);     T_EQ(r.h.status, PC_E_UNSUPPORTED);
#endif
}

static void test_remote_enter_release(void)
{
  uint8_t p[4];
  resp_t r;

  setup(0x1532);
  go_remote(0, 0);
  T_EQ(mock.enters, 1);
  T_TRUE(pc_remote_active(&S));

  //Acquire again only updates flags/watchdog
  go_remote(PC_RF_IGNORE_DTR, 5);
  T_EQ(mock.enters, 1);

  //Release with KEEP_SETTINGS
  p[0] = 0; p[1] = PC_RF_KEEP_SETTINGS; p[2] = p[3] = 0;
  r = req(PC_CMD_SET_REMOTE, p, 4);
  T_EQ(r.h.status, 0); T_EQ(r.p[0], PC_LOCAL); T_EQ(r.p[23], PC_EXIT_RELEASE);
  T_EQ(mock.exits, 1); T_EQ(mock.last_keep, 1);

  //Short payload: missing fields read as 0 (action 1, flags 0, watchdog 0)
  r = req(PC_CMD_SET_REMOTE, "\1", 1);
  T_EQ(r.h.status, 0); T_EQ(r.p[0], PC_REMOTE); T_EQ(pc_get_u16(r.p + 20), 0xFFFF);

  p[0] = 2;
  r = req(PC_CMD_SET_REMOTE, p, 4);
  T_EQ(r.h.status, PC_E_BAD_ARG);

  //Power-off ISR ordering: snapshot must be restored while REMOTE without KEEP
  T_EQ(pc_power_off_restore_needed(&S), 1);
  go_remote(PC_RF_KEEP_SETTINGS, 0);
  T_EQ(pc_power_off_restore_needed(&S), 0);
}

static void test_exits(void)
{
  resp_t r;

  //Watchdog
  setup(0x1532);
  go_remote(0, 2);
  mock.now += 1000;
  r = req(PC_CMD_GET_STATUS, 0, 0);
  T_EQ(pc_get_u16(r.p + 20), 2);              //status frame fed the watchdog: 2 s left
  mock.now += 2000;
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(pc_remote_active(&S), 0); T_EQ(S.exit_reason, PC_EXIT_WATCHDOG); T_EQ(mock.exits, 1); T_EQ(mock.last_keep, 0);

  //DTR drop
  setup(0x1532);
  go_remote(0, 0);
  mock.link_events = PC_LINK_DTR_DOWN; mock.dtr = 0;
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(pc_remote_active(&S), 0); T_EQ(S.exit_reason, PC_EXIT_DTR);

  //...ignored with IGNORE_DTR
  setup(0x1532);
  go_remote(PC_RF_IGNORE_DTR, 0);
  mock.link_events = PC_LINK_DTR_DOWN;
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(pc_remote_active(&S), 1);

  //USB reset always releases
  mock.link_events = PC_LINK_RESET;
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(pc_remote_active(&S), 0); T_EQ(S.exit_reason, PC_EXIT_USB);

  //Banner tap
  setup(0x1532);
  go_remote(0, 0);
  mock.banner_touch = 1;
  mock.now += 200;
  pc_remote_service(&S);
  T_EQ(pc_remote_active(&S), 0); T_EQ(S.exit_reason, PC_EXIT_TAP);

  //ASCII escape hatch
  setup(0x1532);
  go_remote(0, 0);
  mock_clear_resps();
  mock_rx_push((const uint8_t *)"#LOCAL\n", 7);
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(pc_remote_active(&S), 0); T_EQ(S.exit_reason, PC_EXIT_RELEASE);
  T_EQ(mock_nresps, 1); T_EQ(mock_resps[0].len, 4); T_MEM(mock_resps[0].data, "#OK\n", 4);
}

static void test_ascii_ver(void)
{
  setup(0x1532);
  mock_clear_resps();
  mock_rx_push((const uint8_t *)"#VER\r\n#FOO\n", 11);
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock_nresps, 2);
  mock_resps[0].data[mock_resps[0].len - 1] = 0;
  T_STR((char *)mock_resps[0].data, "#OK,FNIRSI-1013D-AI,fw=" PC_FW_VERSION ",proto=1.0,fpga=0x1532");
  T_MEM(mock_resps[1].data, "#ERR,UNKNOWN\n", 13);
}

static void test_channel(void)
{
  uint8_t p[12];
  resp_t r;

  setup(0x1532);
  go_remote(0, 0);

  memset(p, 0, sizeof(p));
  p[0] = 0; p[1] = PC_CH_RANGE; p[3] = 7;
  r = req(PC_CMD_SET_CHANNEL, p, 12);
  T_EQ(r.h.status, PC_E_BAD_ARG); T_EQ(pc_get_u16(r.p), 1);
  T_EQ(mock.ch_writes, 0);

  //Range 2, coupling AC, raw PWM 900, invert
  p[1] = PC_CH_RANGE | PC_CH_COUPLING | PC_CH_OFFSET | PC_CH_INVERT; p[3] = 2; p[4] = 1; p[5] = 1; p[7] = 1;
  pc_put_u32(p + 8, 900);
  r = req(PC_CMD_SET_CHANNEL, p, 12);
  T_EQ(r.h.status, 0);
  T_EQ(r.p[2], 2); T_EQ(r.p[4], 1); T_EQ(r.p[5], 1);
  T_EQ(pc_get_u16(r.p + 10), 900); T_EQ((int16_t)pc_get_u16(r.p + 8), 900 - 1020);
  T_EQ(S.settle_pending, 1);

  //PWM outside 0..2000
  p[1] = PC_CH_OFFSET; p[7] = 0; pc_put_u32(p + 8, 5000);
  r = req(PC_CMD_SET_CHANNEL, p, 12);
  T_EQ(r.h.status, PC_E_BAD_ARG); T_EQ(pc_get_u16(r.p), 3);

  p[0] = 2;
  r = req(PC_CMD_SET_CHANNEL, p, 12);
  T_EQ(r.h.status, PC_E_BAD_ARG);

  //ARM waits out the relay settling (50 ms)
  {
    uint32_t t0 = mock.now;
    r = req(PC_CMD_ARM, 0, 0);
    T_EQ(r.h.status, 0);
    T_TRUE(mock.now - t0 >= 40);
  }
}

static void test_timebase(void)
{
  uint8_t p[8];
  resp_t r;

  setup(0x1532);
  go_remote(0, 0);

  memset(p, 0, 8); pc_put_u32(p + 4, 21);
  r = req(PC_CMD_SET_TIMEBASE, p, 8);
  T_EQ(r.h.status, 0); T_EQ(r.p[0], 21); T_EQ(pc_get_u32(r.p + 4), 199); T_EQ(pc_get_u32(r.p + 8), 1000000);

  pc_put_u32(p + 4, 5);
  r = req(PC_CMD_SET_TIMEBASE, p, 8);
  T_EQ(r.h.status, PC_E_UNSUPPORTED);

  pc_put_u32(p + 4, 35);
  r = req(PC_CMD_SET_TIMEBASE, p, 8);
  T_EQ(r.h.status, PC_E_BAD_ARG);

  p[0] = 1; pc_put_u32(p + 4, 0xFFFFFFFFu);
  r = req(PC_CMD_SET_TIMEBASE, p, 8);
  T_EQ(r.h.status, 0); T_EQ(r.p[0], 0xFF); T_EQ(r.p[1], 0xFF); T_EQ(pc_get_u32(r.p + 4), 0xFFFFFFFFu);
  T_EQ(pc_get_u32(r.p + 8), 0); T_EQ(pc_get_u32(r.p + 12), 0xFFFFFFFFu);

  p[0] = 1; pc_put_u32(p + 4, 4);
  r = req(PC_CMD_SET_TIMEBASE, p, 8);
  T_EQ(pc_get_u32(r.p + 8), 40000000); T_EQ(pc_get_u32(r.p + 12), 25000);
}

static void set_trigger(uint16_t mask, uint8_t src, uint8_t edge, uint8_t level, uint8_t mode,
                        uint32_t pre, uint32_t total, uint32_t hold, uint32_t ato, resp_t *out)
{
  uint8_t p[24];

  memset(p, 0, sizeof(p));
  pc_put_u16(p, mask); p[2] = src; p[3] = edge; p[4] = level; p[5] = mode;
  pc_put_u32(p + 8, pre); pc_put_u32(p + 12, total); pc_put_u32(p + 16, hold); pc_put_u32(p + 20, ato);
  *out = req(PC_CMD_SET_TRIGGER, p, 24);
}

static void test_trigger(void)
{
  resp_t r;

  setup(0x1532);
  go_remote(0, 0);

  set_trigger(0x3F, 1, 1, 140, 1, 2000, 16000, 0, 0, &r);
  T_EQ(r.h.status, 0);
  T_EQ(r.p[0], 1); T_EQ(r.p[1], 1); T_EQ(r.p[2], 140); T_EQ(r.p[3], 1);
  T_EQ(mock.trg.ui_mode, 2);                                   //protocol normal = UI 2
  T_EQ(pc_get_u32(r.p + 4), 2000); T_EQ(pc_get_u32(r.p + 8), 16000);
  T_EQ(pc_get_u32(r.p + 16), 16000);                           //auto_timeout default = total
  T_EQ(pc_get_u16(r.p + 20), 1000); T_EQ(pc_get_u16(r.p + 22), 8000);

  set_trigger(0x20, 0, 0, 0, 0, 0, 16001, 0, 0, &r);  T_EQ(r.h.status, PC_E_BAD_ARG); T_EQ(pc_get_u16(r.p), 5);
  set_trigger(0x20, 0, 0, 0, 0, 0, 16384, 0, 0, &r);  T_EQ(r.h.status, PC_E_BAD_ARG); T_EQ(pc_get_u16(r.p), 5);
  set_trigger(0x20, 0, 0, 0, 0, 0, 78, 0, 0, &r);     T_EQ(r.h.status, PC_E_BAD_ARG);
  set_trigger(0x10, 0, 0, 0, 0, 15999, 0, 0, 0, &r);  T_EQ(r.h.status, PC_E_BAD_ARG); T_EQ(pc_get_u16(r.p), 4);
  set_trigger(0x10, 0, 0, 0, 0, 15999 + 1, 0, 0, 0, &r); T_EQ(r.h.status, PC_E_BAD_ARG);   //> total - 2
  set_trigger(0x10, 0, 0, 0, 0, 15998, 0, 0, 0, &r);  T_EQ(r.h.status, 0);
  set_trigger(0x40, 0, 0, 0, 0, 0, 0, 5, 0, &r);      T_EQ(r.h.status, PC_E_UNSUPPORTED); T_EQ(pc_get_u16(r.p), 6);
  set_trigger(0x40, 0, 0, 0, 0, 0, 0, 0, 0, &r);      T_EQ(r.h.status, 0);
  set_trigger(0x01, 2, 0, 0, 0, 0, 0, 0, 0, &r);      T_EQ(r.h.status, PC_E_UNSUPPORTED);
  set_trigger(0x08, 0, 0, 0, 3, 0, 0, 0, 0, &r);      T_EQ(r.h.status, PC_E_BAD_ARG); T_EQ(pc_get_u16(r.p), 3);
  set_trigger(0x80, 0, 0, 0, 0, 0, 0, 0, 12345, &r);  T_EQ(r.h.status, 0); T_EQ(pc_get_u32(r.p + 16), 12345);

  //Stock FPGA: only 1500 / 3000
  setup(0x1432);
  go_remote(0, 0);
  set_trigger(0x30, 0, 0, 0, 0, 1000, 3000, 0, 0, &r); T_EQ(r.h.status, PC_E_UNSUPPORTED);
  set_trigger(0x30, 0, 0, 0, 0, 1500, 3000, 0, 0, &r); T_EQ(r.h.status, 0);
  set_trigger(0x08, 0, 0, 0, 2, 0, 0, 0, 0, &r);       T_EQ(r.h.status, 0); T_EQ(mock.trg.ui_mode, 1);   //single
}

static void test_capture_and_raw(void)
{
  uint8_t p[16];
  resp_t r;
  uint32_t i, c;

  setup(0x1532);
  go_remote(0, 0);
  set_trigger(0x3B, 0, 0, 128, 1, 2000, 16000, 0, 0, &r);   //source edge mode pre total (level unchanged)

  //READ_RAW before any capture
  memset(p, 0, 16); p[0] = 1; pc_put_u32(p + 8, 16);
  r = req(PC_CMD_READ_RAW, p, 16);
  T_EQ(r.h.status, PC_E_STATE);

  //ARM writes the latched geometry
  r = req(PC_CMD_ARM, 0, 0);
  T_EQ(r.h.status, 0); T_EQ(r.p[1], PC_ACQ_ARMED);
  T_EQ(mock.arm_pre, 1000); T_EQ(mock.arm_total, 8000); T_EQ(mock.arm_auto, 16000); T_EQ(mock.arm_prog, 1); T_EQ(mock.arm_normal, 1);

  r = req(PC_CMD_ARM, 0, 0);
  T_EQ(r.h.status, PC_E_STATE);

  //WAIT is deferred until DONE
  {
    uint8_t w[4];
    pc_put_u32(w, 1000);
    mock.polls_to_done = 6;
    r = req(PC_CMD_WAIT, w, 4);
    T_EQ(r.h.status, 0); T_EQ(r.p[1], PC_ACQ_DONE);
    T_EQ(pc_get_u32(r.p + 4), 1);                              //capture_id
    T_EQ(pc_get_u16(r.p + 8), 100);                            //trigger_addr
    T_EQ(pc_get_u16(r.p + 10), (100 + 8192 - 1000) % 8192);    //modular start: 7292
    T_EQ(r.p[2] & 0x07, 0x03);                                 //triggered, done, no auto timeout
  }

  //Full ring, all four cores, relative to the capture start: wraps at 8192
  memset(p, 0, 16); p[0] = 0x0F; p[1] = 1; pc_put_u32(p + 8, 8192);
  r = req(PC_CMD_READ_RAW, p, 16);
  T_EQ(r.h.status, 0); T_EQ(r.len, 16 + 4 * 8192);
  T_EQ(pc_get_u16(r.p + 4), 7292); T_EQ(pc_get_u16(r.p + 6), 8192); T_EQ(pc_get_u32(r.p + 8), 8192);
  {
    int bad = 0;
    for(c=0;c<4;c++)
      for(i=0;i<8192;i++)
        if(r.p[16 + c * 8192 + i] != mock.ring[c][(7292 + i) % 8192]) bad++;
    T_EQ(bad, 0);
  }
  T_TRUE(mock.max_read_start < 8192);

  //Relative to trigger_addr, negative offset, interleaved CH2 stream: B before A
  memset(p, 0, 16); p[0] = 0x0C; p[1] = 2; p[2] = 1; pc_put_u32(p + 4, (uint32_t)-150); pc_put_u32(p + 8, 300);
  r = req(PC_CMD_READ_RAW, p, 16);
  T_EQ(r.h.status, 0); T_EQ(r.len, 16 + 600);
  T_EQ(pc_get_u16(r.p + 4), (100 - 150 + 8192) % 8192);
  {
    int bad = 0;
    for(i=0;i<300;i++)
    {
      uint32_t a = (8142 + i) % 8192;
      if(r.p[16 + 2 * i] != mock.ring[3][a] || r.p[16 + 2 * i + 1] != mock.ring[2][a]) bad++;
    }
    T_EQ(bad, 0);
  }

  //Layout 1 needs both cores of a channel
  p[0] = 0x04;
  r = req(PC_CMD_READ_RAW, p, 16);
  T_EQ(r.h.status, PC_E_BAD_ARG);

  //Absolute addresses reduce modulo ring_words
  memset(p, 0, 16); p[0] = 0x01; p[1] = 0; pc_put_u32(p + 4, 8192 * 3 + 5); pc_put_u32(p + 8, 4);
  r = req(PC_CMD_READ_RAW, p, 16);
  T_EQ(pc_get_u16(r.p + 4), 5); T_EQ(r.p[16], mock.ring[0][5]);

  p[1] = 0; pc_put_u32(p + 8, 8193);
  r = req(PC_CMD_READ_RAW, p, 16);
  T_EQ(r.h.status, PC_E_BAD_ARG);

  //Duplicate request (same seq + cmd) is answered from the cache: ARM is not executed twice
  {
    uint32_t arms;
    uint16_t seq = next_seq++;
    uint32_t j, n = 0;

    mock_clear_resps();
    push_frame(PC_CMD_ARM, seq, 0, 0);
    pc_poll(&S, PC_CTX_MAIN);
    arms = mock.arms;
    push_frame(PC_CMD_ARM, seq, 0, 0);
    pc_poll(&S, PC_CTX_MAIN);
    T_EQ(mock.arms, arms);
    for(j=0;j<mock_nresps;j++)
      if(decode(&mock_resps[j]).h.seq == seq) n++;
    T_EQ(n, 2);
    T_EQ(mock_resps[0].len, mock_resps[1].len);
    T_MEM(mock_resps[0].data, mock_resps[1].data, mock_resps[0].len);
    T_EQ(S.duplicates, 1);
  }
}

static void test_auto_timeout_and_stop(void)
{
  resp_t r;
  uint8_t w[4];

  setup(0x1532);
  go_remote(0, 0);

  //Auto mode with no trigger: DONE with 0x18 = 0 -> auto_timeout flag
  mock.polls_to_trigger = -1; mock.polls_to_done = 2;
  r = req(PC_CMD_ARM, 0, 0);
  T_EQ(mock.arm_normal, 0);
  pc_put_u32(w, 500);
  r = req(PC_CMD_WAIT, w, 4);
  T_EQ(r.p[1], PC_ACQ_DONE); T_EQ(r.p[2] & 0x04, 0x04);

  //WAIT times out with status OK, state still ARMED
  mock.polls_to_done = -1;
  r = req(PC_CMD_ARM, 0, 0);
  T_EQ(r.h.status, 0);
  {
    uint16_t seq = next_seq++;
    uint32_t j;
    int found = 0, k;

    mock_clear_resps();
    pc_put_u32(w, 20);
    push_frame(PC_CMD_WAIT, seq, w, 4);
    for(k=0;k<30 && !found;k++)
    {
      pc_poll(&S, PC_CTX_MAIN);
      for(j=0;j<mock_nresps;j++)
      {
        resp_t d = decode(&mock_resps[j]);
        if(d.h.seq == seq) { found = 1; T_EQ(d.h.status, 0); T_EQ(d.p[1], PC_ACQ_ARMED); T_TRUE(k >= 19); }
      }
      mock.now += 1;
    }
    T_TRUE(found);
  }

  //A normal request while WAIT is pending gets E_BUSY; STOP aborts the WAIT
  {
    uint16_t sw = next_seq++, sg, ss;
    uint32_t j;
    int got_wait = 0, got_stop = 0, got_busy = 0;

    mock_clear_resps();
    pc_put_u32(w, 5000);
    push_frame(PC_CMD_WAIT, sw, w, 4);
    pc_poll(&S, PC_CTX_MAIN);
    T_EQ(mock_nresps, 0);
    //A retransmitted pending WAIT (same seq) is ignored, not answered with E_BUSY
    push_frame(PC_CMD_WAIT, sw, w, 4);
    pc_poll(&S, PC_CTX_MAIN);
    T_EQ(mock_nresps, 0);
    sg = next_seq++;
    push_frame(PC_CMD_GET_TRIGGER, sg, 0, 0);
    ss = next_seq++;
    push_frame(PC_CMD_STOP, ss, 0, 0);
    pc_poll(&S, PC_CTX_MAIN);
    pc_poll(&S, PC_CTX_MAIN);

    for(j=0;j<mock_nresps;j++)
    {
      resp_t d = decode(&mock_resps[j]);
      if(d.h.seq == sg) { got_busy = 1; T_EQ(d.h.status, PC_E_BUSY); }
      if(d.h.seq == ss) { got_stop = 1; T_EQ(d.h.status, 0); T_EQ(d.p[1], PC_ACQ_ABORTED); }
      if(d.h.seq == sw) { got_wait = 1; T_EQ(d.h.status, PC_E_ABORTED); T_EQ(d.h.cmd, PC_CMD_WAIT); }
    }
    T_TRUE(got_busy); T_TRUE(got_stop); T_TRUE(got_wait);
  }

  //Release while WAIT pending: the WAIT completes with E_ABORTED
  r = req(PC_CMD_ARM, 0, 0);
  {
    uint16_t sw = next_seq++;
    uint8_t rel[4] = { 0, 0, 0, 0 };
    uint32_t j;
    int got = 0;

    mock_clear_resps();
    pc_put_u32(w, 5000);
    push_frame(PC_CMD_WAIT, sw, w, 4);
    pc_poll(&S, PC_CTX_MAIN);
    push_frame(PC_CMD_SET_REMOTE, next_seq++, rel, 4);
    pc_poll(&S, PC_CTX_MAIN);
    pc_poll(&S, PC_CTX_MAIN);
    for(j=0;j<mock_nresps;j++)
    {
      resp_t d = decode(&mock_resps[j]);
      if(d.h.seq == sw) { got = 1; T_EQ(d.h.status, PC_E_ABORTED); }
    }
    T_TRUE(got);
    T_EQ(pc_remote_active(&S), 0);
  }

  //FPGA never ready
  go_remote(0, 0);
  mock.arm_fail = 1;
  r = req(PC_CMD_ARM, 0, 0);
  T_EQ(r.h.status, PC_E_FPGA);
}

static void test_trace(void)
{
  uint8_t p[12];
  resp_t r;
  uint32_t k;

  setup(0x1532);

  //LOCAL: copy of the UI's trace
  memset(mock.trace[0], 0x42, 3000);
  memset(p, 0, 12); p[0] = 1;
  r = req(PC_CMD_READ_TRACE, p, 12);
  T_EQ(r.h.status, 0); T_EQ(r.len, 32 + 16 + 3000);
  T_EQ(pc_get_u32(r.p + 4), 3000); T_EQ(pc_get_u32(r.p + 16), 1500); T_EQ((int32_t)pc_get_u32(r.p + 20), 1502);
  T_EQ(r.p[29], 1); T_EQ(r.p[48], 0x42); T_EQ(mock.trace_builds, 0);

  //REMOTE: built once per capture from the ring
  go_remote(0, 0);
  {
    resp_t t;
    set_trigger(0x30, 0, 0, 0, 0, 2000, 4000, 0, 0, &t);
  }
  r = req(PC_CMD_READ_TRACE, p, 12);
  T_EQ(r.h.status, PC_E_STATE);
  r = req(PC_CMD_ARM, 0, 0);
  {
    uint8_t w[4]; pc_put_u32(w, 100);
    r = req(PC_CMD_WAIT, w, 4);
    T_EQ(r.p[1], PC_ACQ_DONE);
  }
  p[0] = 3; pc_put_u32(p + 4, 10); pc_put_u32(p + 8, 100);
  r = req(PC_CMD_READ_TRACE, p, 12);
  T_EQ(r.h.status, 0); T_EQ(r.len, 32 + 32 + 200);
  T_EQ(pc_get_u32(r.p + 4), 4000); T_EQ(pc_get_u32(r.p + 16), 2000); T_EQ(r.p[29], 0);
  T_EQ(mock.trace_builds, 1);
  {
    uint32_t start = (100 + 8192 - 1000) % 8192, bad = 0;
    for(k=0;k<100;k++)
    {
      uint32_t s = 10 + k, a = (start + s / 2) % 8192;
      uint8_t  exp = (s & 1) ? mock.ring[0][a] : mock.ring[1][a];
      if(r.p[64 + k] != exp) bad++;
    }
    T_EQ(bad, 0);
  }
  r = req(PC_CMD_READ_TRACE, p, 12);
  T_EQ(mock.trace_builds, 1);
  p[1] = 0x01;                                                  //REREAD
  r = req(PC_CMD_READ_TRACE, p, 12);
  T_EQ(mock.trace_builds, 2);

  //NO_INVERT undoes the firmware's inversion
  mock.ch[0].invert = 1;
  p[0] = 1; p[1] = 0x01; pc_put_u32(p + 4, 0); pc_put_u32(p + 8, 4);
  r = req(PC_CMD_READ_TRACE, p, 12);
  T_EQ(r.p[32 + 3] & 1, 1);
  {
    uint8_t inv0 = r.p[48];
    p[1] = 0x02;
    r = req(PC_CMD_READ_TRACE, p, 12);
    T_EQ(r.p[32 + 3] & 1, 0);
    T_EQ(r.p[48], 255 - inv0);
  }

  //count 0 = to the end; start past the end
  p[1] = 0; pc_put_u32(p + 4, 3990); pc_put_u32(p + 8, 0);
  r = req(PC_CMD_READ_TRACE, p, 12);
  T_EQ(pc_get_u32(r.p + 12), 10);
  pc_put_u32(p + 4, 4001);
  r = req(PC_CMD_READ_TRACE, p, 12);
  T_EQ(r.h.status, PC_E_BAD_ARG);
  T_EQ(pc_get_u16(r.p), 4);                                     //detail = byte offset of start
  T_TRUE(r.len > 2); T_MEM(r.p + 2, "start", 5);                //error text follows the detail
}

static void test_generator(void)
{
  uint8_t p[20];
  resp_t r;

  T_EQ(pc_gen_phase_from_mhz(10000000), 858993);                       //10 kHz
  T_EQ(pc_gen_phase_from_mhz(25000000000ull), 0x80000000u);            //25 MHz
  T_EQ(pc_gen_mhz_from_phase(858993), 9999995);                        //9999.995 Hz actual
  T_EQ(pc_gen_mhz_from_phase(0x80000000u), 25000000000ull);
  T_EQ(pc_gen_thr_from_bp(5000), 0x80000000u);
  T_EQ(pc_gen_thr_from_bp(10000), 0xFFFFFFFFu);
  T_EQ(pc_gen_bp_from_thr(0x80000000u), 5000);
  T_EQ(pc_gen_bp_from_thr(0xFFFFFFFFu), 10000);
  T_EQ(pc_gen_bp_from_thr(0), 0);

  setup(0x1532);
  r = req(PC_CMD_GET_GEN, 0, 0);
  T_EQ(r.h.status, 0); T_EQ(r.p[0], 0);
  T_EQ(pc_get_u32(r.p + 4), 85899);                                    //UI 1000 Hz, 1.00o5 integer math
  T_EQ(pc_get_u16(r.p + 20), 5000);

  go_remote(0, 0);
  memset(p, 0, 20); p[0] = 7; p[1] = 1; pc_put_u64(p + 4, 10000000); pc_put_u32(p + 12, 2500);
  r = req(PC_CMD_SET_GEN, p, 20);
  T_EQ(r.h.status, 0); T_EQ(r.p[0], 1);
  T_EQ(mock.gen_phase, 858993); T_EQ(mock.gen_thr, pc_gen_thr_from_bp(2500)); T_EQ(mock.gen_hw_en, 1);
  T_EQ(pc_get_u64(r.p + 12), 9999995); T_EQ(pc_get_u16(r.p + 20), 2500);

  pc_put_u64(p + 4, 25000000001ull); p[0] = 2;
  r = req(PC_CMD_SET_GEN, p, 20);
  T_EQ(r.h.status, PC_E_BAD_ARG);

  //Only duty changes; frequency stays
  memset(p, 0, 20); p[0] = 4; p[3] = 1; pc_put_u32(p + 12, 0x40000000u);
  r = req(PC_CMD_SET_GEN, p, 20);
  T_EQ(mock.gen_phase, 858993); T_EQ(mock.gen_thr, 0x40000000u);

  //Stock: no DDS
  setup(0x1432);
  go_remote(0, 0);
  r = req(PC_CMD_SET_GEN, p, 20);
  T_EQ(r.h.status, PC_E_UNSUPPORTED);
  r = req(PC_CMD_GET_GEN, 0, 0);
  T_EQ(r.h.status, 0); T_EQ(pc_get_u64(r.p + 12), 1000000);
}

static void test_cal_battery_pattern(void)
{
  uint8_t p[8];
  resp_t r;
  uint32_t i, x;

  setup(0x1532);
  r = req(PC_CMD_GET_CAL, "\1", 1);
  T_EQ(r.h.status, 0); T_EQ(r.len, 72); T_EQ(r.p[0], 1);
  T_EQ(pc_get_u16(r.p + 4 + 2 * 6), 1060); T_EQ(pc_get_u32(r.p + 20 + 4 * 6), 7600000);
  T_EQ((int16_t)pc_get_u16(r.p + 48), 2); T_EQ((int16_t)pc_get_u16(r.p + 50), -1);
  T_EQ(pc_get_u32(r.p + 60), 385);

  r = req(PC_CMD_GET_BATTERY, 0, 0);
  T_EQ(r.len, 4); T_EQ(r.p[0], 17); T_EQ(r.p[1], 1); T_EQ(r.p[2], 50);

  //TEST_PATTERN: the ZLP boundary sizes and the largest response
  memset(p, 0, 8); pc_put_u32(p, 4076);
  r = req(PC_CMD_TEST_PATTERN, p, 8);
  T_EQ(r.len, 4076); T_EQ(PC_FRAME_SIZE(r.len), 4096); T_EQ(r.p[300], 300 & 0xFF);
  pc_put_u32(p, 66560); p[4] = 3;
  r = req(PC_CMD_TEST_PATTERN, p, 8);
  T_EQ(r.h.status, 0); T_EQ(r.len, 66560);
  x = 66560; x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  T_EQ(r.p[0], x & 0xFF); T_EQ(r.p[3], x >> 24);
  for(i=0;i<1;i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; }
  T_EQ(r.p[4], x & 0xFF);
  pc_put_u32(p, 66561);
  r = req(PC_CMD_TEST_PATTERN, p, 8);
  T_EQ(r.h.status, PC_E_BAD_ARG);
  pc_put_u32(p, 0); p[4] = 0;
  r = req(PC_CMD_TEST_PATTERN, p, 8);
  T_EQ(r.h.status, 0); T_EQ(r.len, 0);
}

static void test_busy_context(void)
{
  uint8_t p[4] = { 1, 0, 0, 0 };
  resp_t r;
  uint16_t seq;
  uint32_t j;
  int found;

  setup(0x1532);
  pc_poll(&S, PC_CTX_MAIN);

  //A BUSY poll answers the safe set
  r = req_ctx(PC_CMD_GET_STATUS, 0, 0, PC_CTX_BUSY);
  T_EQ(r.h.status, 0); T_EQ(r.p[3], 0);

  //...and leaves anything else for the next MAIN poll while the UI is not modal
  mock.now += 10;
  pc_poll(&S, PC_CTX_MAIN);
  mock_clear_resps();
  seq = next_seq++;
  push_frame(PC_CMD_SET_REMOTE, seq, p, 4);
  pc_poll(&S, PC_CTX_BUSY);
  T_EQ(mock_nresps, 0); T_EQ(mock.enters, 0);
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock_nresps, 1); T_EQ(mock.enters, 1);
  p[0] = 0;
  r = req(PC_CMD_SET_REMOTE, p, 4);

  //When no MAIN poll came for PC_MODAL_MS the UI is modal: E_BUSY, and ui_busy in GET_STATUS
  mock_clear_resps();
  mock.now += PC_MODAL_MS + 1;
  seq = next_seq++;
  p[0] = 1;
  push_frame(PC_CMD_SET_REMOTE, seq, p, 4);
  pc_poll(&S, PC_CTX_BUSY);
  found = 0;
  for(j=0;j<mock_nresps;j++)
  {
    resp_t d = decode(&mock_resps[j]);
    if(d.h.seq == seq) { found = 1; T_EQ(d.h.status, PC_E_BUSY); }
  }
  T_TRUE(found); T_EQ(mock.enters, 1);
  r = req_ctx(PC_CMD_GET_STATUS, 0, 0, PC_CTX_BUSY);
  T_EQ(r.p[3], 1);
  r = req_ctx(PC_CMD_GET_BATTERY, 0, 0, PC_CTX_BUSY);
  T_EQ(r.h.status, 0);
}

static void test_transport_window(void)
{
  uint16_t s1, s2;

  setup(0x1532);

  //One response in flight: the next request waits until the TX staging buffer is free
  mock.tx_hold = 1;
  mock_clear_resps();
  s1 = next_seq++; s2 = next_seq++;
  push_frame(PC_CMD_PING, s1, "a", 1);
  push_frame(PC_CMD_PING, s2, "b", 1);
  pc_poll(&S, PC_CTX_MAIN);
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock_nresps, 1); T_EQ(decode(&mock_resps[0]).h.seq, s1);
  mock_tx_release();
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock_nresps, 2); T_EQ(decode(&mock_resps[1]).h.seq, s2);

  //Bad payload CRC: E_BAD_CRC with the request's cmd and seq
  {
    uint8_t f[64];
    uint32_t n;
    resp_t d;

    mock.tx_hold = 0;
    mock_tx_release();
    mock_clear_resps();
    memcpy(f + 16, "xyz", 3);
    n = pc_frame_finish(f, PC_CMD_PING, 4242, 0, 0, 3);
    f[17] ^= 1;
    mock_rx_push(f, n);
    pc_poll(&S, PC_CTX_MAIN);
    T_EQ(mock_nresps, 1);
    d = decode(&mock_resps[0]);
    T_TRUE(d.ok); T_EQ(d.h.status, PC_E_BAD_CRC); T_EQ(d.h.seq, 4242); T_EQ(d.h.cmd, PC_CMD_PING);

    //Header CRC error: no answer at all
    mock_clear_resps();
    n = pc_frame_finish(f, PC_CMD_PING, 4243, 0, 0, 0);
    f[15] ^= 1;
    mock_rx_push(f, n);
    pc_poll(&S, PC_CTX_MAIN);
    T_EQ(mock_nresps, 0);

    //Oversize: E_BAD_LEN
    n = pc_frame_finish(f, PC_CMD_PING, 4244, 0, 0, 0);
    pc_put_u32(f + 10, 600); pc_put_u16(f + 14, pc_crc16(f, 14));
    mock_rx_push(f, 16);
    pc_poll(&S, PC_CTX_MAIN);
    T_EQ(mock_nresps, 1); T_EQ(decode(&mock_resps[0]).h.status, PC_E_BAD_LEN);

    //The parser now skips the 604 announced bytes; the host stops sending, 100 ms later it hunts again
    mock.now += 150;
    pc_poll(&S, PC_CTX_MAIN);
    T_EQ(S.parser.stats.timeouts, 1);
  }

  //PING echoes up to 492 bytes
  {
    uint8_t big[492];
    resp_t r;
    uint32_t i;
    for(i=0;i<492;i++) big[i] = (uint8_t)(i ^ 0x5A);
    r = req(PC_CMD_PING, big, 492);
    T_EQ(r.len, 492); T_MEM(r.p, big, 492);
  }
}

static void test_ui_capture_and_status(void)
{
  resp_t r;

  setup(0x1532);
  pc_ui_capture_done(&S);
  pc_ui_capture_done(&S);
  r = req(PC_CMD_GET_STATUS, 0, 0);
  T_EQ(r.len, 28); T_EQ(r.p[0], 0); T_EQ(r.p[1], 0); T_EQ(pc_get_u32(r.p + 4), 2);
  T_EQ(r.p[2] & 0x18, 0x18);                                 //dtr, charging
  T_EQ(r.p[22], 17); T_EQ(pc_get_u16(r.p + 20), 0xFFFF);
  T_EQ(pc_get_u32(r.p + 16), mock.now);
}

int main(void)
{
  test_info();
  test_gating();
  test_remote_enter_release();
  test_exits();
  test_ascii_ver();
  test_channel();
  test_timebase();
  test_trigger();
  test_capture_and_raw();
  test_auto_timeout_and_stop();
  test_trace();
  test_generator();
  test_cal_battery_pattern();
  test_busy_context();
  test_transport_window();
  test_ui_capture_and_status();
  mock_clear_resps();
  return t_report("test_session");
}
