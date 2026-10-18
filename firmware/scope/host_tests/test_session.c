//----------------------------------------------------------------------------------------------------------------------------------
// Session / handler tests for pc_cmds.c against mock_hal.c. Host build: make -C firmware/scope/host_tests test
//
// Protocol 1.3: shared control with the UI (docs/protocol.md section 14, docs/plans/collaborative-mode.md).
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

//Response with this seq among the captured ones, or NULL
static int find_resp(uint16_t seq, resp_t *out)
{
  uint32_t j;

  for(j=0;j<mock_nresps;j++)
  {
    resp_t d = decode(&mock_resps[j]);

    if(d.ok && d.h.seq == seq)
    {
      *out = d;
      return 1;
    }
  }

  return 0;
}

//Send one request, poll until a response with that seq appears (or give up)
static resp_t req_ctx(uint16_t cmd, const void *payload, uint32_t len, int ctx)
{
  uint16_t seq = next_seq++;
  resp_t d;
  int i;

  mock_clear_resps();
  push_frame(cmd, seq, payload, len);

  for(i=0;i<50;i++)
  {
    pc_poll(&S, ctx);

    if(find_resp(seq, &d))
      return d;

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

//Fresh session; the main loop has run its first passes (not STARTING)
static void setup(uint16_t version)
{
  mock_reset(version);
  pc_session_init(&S);
  next_seq = 1;
  pc_poll(&S, PC_CTX_MAIN);
  mock.now += 30;
  pc_poll(&S, PC_CTX_MAIN);
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

//One UI capture through the hooks the 1.00o5 code calls: fpga_do_conversion, 0x14 read, readout done
static void ui_capture(uint16_t addr)
{
  pc_ui_capture_start(&S);
  mock.ui_armed = 1;
  pc_ui_capture_addr(&S, addr);
  pc_ui_capture_done(&S);
  mock.ui_armed = 0;
}

static resp_t status(void)
{
  return req(PC_CMD_GET_STATUS, 0, 0);
}

static resp_t wait_done(uint32_t ms)
{
  uint8_t w[4];

  pc_put_u32(w, ms);
  return req(PC_CMD_WAIT, w, 4);
}

//----------------------------------------------------------------------------------------------------------------------------------

static void test_info(void)
{
  resp_t r;

  setup(0x1532);
  r = req(PC_CMD_GET_INFO, 0, 0);
  T_TRUE(r.ok); T_EQ(r.h.status, 0); T_EQ(r.len, 76);
  T_EQ(r.p[0], 1); T_EQ(r.p[1], 5); T_EQ(pc_get_u16(r.p + 2), 512); T_EQ(pc_get_u32(r.p + 4), 66560);
  T_EQ(pc_get_u16(r.p + 8), 0x1532); T_EQ(r.p[10], 2); T_EQ(pc_get_u16(r.p + 12), 8192);
  T_EQ(pc_get_u32(r.p + 20) & (PC_CAP_PROG_TRIGGER_POS | PC_CAP_GENERATOR_DDS), PC_CAP_PROG_TRIGGER_POS | PC_CAP_GENERATOR_DDS);
  T_EQ(pc_get_u32(r.p + 20) & (PC_CAP_READ_LAG_UNVERIFIED | PC_CAP_INTERLEAVE_UNVERIFIED), PC_CAP_READ_LAG_UNVERIFIED | PC_CAP_INTERLEAVE_UNVERIFIED);
  T_EQ(pc_get_u32(r.p + 20) & PC_CAP_SHARED_MODE, PC_CAP_SHARED_MODE);
  T_EQ(pc_get_u32(r.p + 20) & PC_CAP_HOLDOFF, 0);
  T_EQ(pc_get_u32(r.p + 24), 50000000); T_EQ(pc_get_u16(r.p + 28), 16382); T_EQ(pc_get_u16(r.p + 30), 80);
  T_STR((char *)r.p + 36, PC_FW_VERSION);
  T_EQ(pc_get_u16(r.p + 72), 800);

  //Stock: no programmable geometry, no generator
  setup(0x1432);
  r = req(PC_CMD_GET_INFO, 0, 0);
  T_EQ(r.p[10], 1); T_EQ(pc_get_u16(r.p + 12), 4096);
  T_EQ(pc_get_u32(r.p + 20) & (PC_CAP_PROG_TRIGGER_POS | PC_CAP_GENERATOR_DDS), 0);
  T_EQ(pc_get_u32(r.p + 20) & PC_CAP_SHARED_MODE, PC_CAP_SHARED_MODE);
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
  r = req(PC_CMD_ARM, 0, 0);
  T_EQ(r.h.status, PC_E_UNSUPPORTED); T_EQ(mock.arms, 0);
}

//No ownership: every command works without SET_REMOTE; SET_REMOTE and #LOCAL are compatibility no-ops
static void test_shared_basics(void)
{
  uint8_t p[12];
  resp_t r;

  setup(0x1532);
  memset(p, 0, sizeof(p));

  //SET_CHANNEL writes the live UI settings at once (1.2: E_NOT_REMOTE)
  p[0] = 0; p[1] = PC_CH_RANGE; p[3] = 3;
  r = req(PC_CMD_SET_CHANNEL, p, 12);
  T_EQ(r.h.status, 0); T_EQ(r.p[2], 3); T_EQ(mock.ch[0].range, 3); T_EQ(mock.ch_writes, 1);

  r = req(PC_CMD_GET_CHANNEL, "\1", 1);
  T_EQ(r.h.status, 0); T_EQ(r.len, 24); T_EQ(r.p[0], 1); T_EQ(r.p[2], 5); T_EQ(r.p[3], 5);
  T_EQ(pc_get_u16(r.p + 10), 1050); T_EQ(pc_get_u16(r.p + 12), 1050);
  //uv_per_lsb_q8 = 3800000 * 200 * 10 / 8192 = 927734.375 -> 927734
  T_EQ(pc_get_u32(r.p + 16), 927734); T_EQ((int32_t)pc_get_u32(r.p + 20), 128 * 256);

  r = req(PC_CMD_GET_TIMEBASE, 0, 0);
  T_EQ(r.p[0], 24); T_EQ(pc_get_u32(r.p + 4), 19); T_EQ(pc_get_u32(r.p + 8), 10000000); T_EQ(pc_get_u32(r.p + 12), 100000);

  //Settings changed by the user on the touch screen are what GET_* return
  mock.tb.timeperdiv = 21;
  r = req(PC_CMD_GET_TIMEBASE, 0, 0);
  T_EQ(r.p[0], 21);

  r = req(0x7777, 0, 0);
  T_EQ(r.h.status, PC_E_UNKNOWN_CMD);

  //SET_REMOTE: OK no-op with any action or flags, answers the 1.3 status body
  {
    uint8_t sr[4] = { 1, 0x07, 15, 0 };
    r = req(PC_CMD_SET_REMOTE, sr, 4);
    T_EQ(r.h.status, 0); T_EQ(r.len, 44); T_EQ(r.p[0], PC_SHARED); T_EQ(pc_get_u16(r.p + 20), 0xFFFF); T_EQ(r.p[23], 0);
    sr[0] = 0;
    r = req(PC_CMD_SET_REMOTE, sr, 4);
    T_EQ(r.h.status, 0); T_EQ(r.p[0], PC_SHARED);
    sr[0] = 2;
    r = req(PC_CMD_SET_REMOTE, sr, 4);
    T_EQ(r.h.status, 0);
    T_EQ(mock.run_sets, 0);                                      //no effect on the UI
  }

  //#LOCAL answers #OK and changes nothing
  mock_clear_resps();
  mock_rx_push((const uint8_t *)"#LOCAL\n", 7);
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock_nresps, 1); T_EQ(mock_resps[0].len, 4); T_MEM(mock_resps[0].data, "#OK\n", 4);

  //Not built in this firmware: capability bits are 0 and the commands fail closed
  r = req(PC_CMD_READ_SCREEN, 0, 0);    T_EQ(r.h.status, PC_E_UNSUPPORTED);
  r = req(PC_CMD_ENTER_FEL, 0, 0);      T_EQ(r.h.status, PC_E_BAD_ARG);   //magic missing; FEL itself in test_fel_reset_stall
  r = req(PC_CMD_ACQUIRE_AVG, 0, 0);    T_EQ(r.h.status, PC_E_UNSUPPORTED);
  r = req(PC_CMD_READ_LIVE, 0, 0);      T_EQ(r.h.status, PC_E_UNSUPPORTED);
#if !PC_DEBUG
  r = req(PC_CMD_FORCE, 0, 0);          T_EQ(r.h.status, PC_E_UNSUPPORTED);
  r = req(PC_CMD_DEBUG_FPGA, 0, 0);     T_EQ(r.h.status, PC_E_UNSUPPORTED);
#endif
}

static void test_ascii_ver(void)
{
  setup(0x1532);
  mock_clear_resps();
  mock_rx_push((const uint8_t *)"#VER\r\n#FOO\n", 11);
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock_nresps, 2);
  mock_resps[0].data[mock_resps[0].len - 1] = 0;
  T_STR((char *)mock_resps[0].data, "#OK,FNIRSI-1013D-AI,fw=" PC_FW_VERSION ",proto=1.5,fpga=0x1532");
  T_MEM(mock_resps[1].data, "#ERR,UNKNOWN\n", 13);
}

static void test_channel(void)
{
  uint8_t p[12];
  resp_t r;

  setup(0x1532);

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

  //The raw divider holds until the UI selects a time base again
  mock.tb.timeperdiv = 23; mock.tb.samplerate = 5; mock.tb.divider = 39;
  r = req(PC_CMD_GET_TIMEBASE, 0, 0);
  T_EQ(r.p[0], 23); T_EQ(pc_get_u32(r.p + 4), 39); T_EQ(S.tb_raw, 0);
}

static void test_trigger(void)
{
  resp_t r;

  setup(0x1532);

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
  set_trigger(0x30, 0, 0, 0, 0, 1000, 3000, 0, 0, &r); T_EQ(r.h.status, PC_E_UNSUPPORTED);
  set_trigger(0x30, 0, 0, 0, 0, 1500, 3000, 0, 0, &r); T_EQ(r.h.status, 0);
  set_trigger(0x08, 0, 0, 0, 2, 0, 0, 0, 0, &r);       T_EQ(r.h.status, 0); T_EQ(mock.trg.ui_mode, 1);   //single
}

//Host single capture: ARM stops the UI, DONE builds the trace, READ_RAW reads the ring
static void test_capture_and_raw(void)
{
  uint8_t p[16];
  resp_t r;
  uint32_t i, c;

  setup(0x1532);
  set_trigger(0x3B, 0, 0, 128, 1, 2000, 16000, 0, 0, &r);   //source edge mode pre total (level unchanged)

  //READ_RAW before any capture with the UI stopped
  mock.ui_run = 0;
  memset(p, 0, 16); p[0] = 1; pc_put_u32(p + 8, 16);
  r = req(PC_CMD_READ_RAW, p, 16);
  T_EQ(r.h.status, PC_E_STATE); T_EQ(pc_get_u16(r.p), 0);
  r = req(PC_CMD_GET_CAPTURE_INFO, 0, 0);
  T_EQ(r.h.status, PC_E_STATE);
  mock.ui_run = 1;

  //ARM stops the UI acquisition and writes the latched geometry; run_state SINGLE while armed
  r = req(PC_CMD_ARM, 0, 0);
  T_EQ(r.h.status, 0); T_EQ(r.p[1], PC_ACQ_ARMED); T_EQ(r.p[40], PC_RUN_SINGLE);
  T_EQ(mock.ui_run, 0); T_EQ(mock.last_run_set, 0);
  T_EQ(mock.arm_pre, 1000); T_EQ(mock.arm_total, 8000); T_EQ(mock.arm_auto, 16000); T_EQ(mock.arm_prog, 1); T_EQ(mock.arm_normal, 1);

  r = req(PC_CMD_ARM, 0, 0);
  T_EQ(r.h.status, PC_E_STATE);

  //READ_RAW while the host single is armed
  r = req(PC_CMD_READ_RAW, p, 16);
  T_EQ(r.h.status, PC_E_STATE);

  //WAIT is deferred until DONE; DONE fills the trace buffers so the screen shows the capture
  mock.polls_to_done = 6;
  r = wait_done(1000);
  T_EQ(r.h.status, 0); T_EQ(r.p[1], PC_ACQ_DONE);
  T_EQ(pc_get_u32(r.p + 4), 1);                              //capture_id
  T_EQ(pc_get_u16(r.p + 8), 100);                            //trigger_addr
  T_EQ(pc_get_u16(r.p + 10), (100 + 8192 - 1000) % 8192);    //modular start: 7292
  T_EQ(r.p[2] & 0x07, 0x03);                                 //triggered, done, no auto timeout
  T_EQ(r.p[40], PC_RUN_STOP); T_EQ(r.p[41], PC_ORIGIN_HOST);
  T_EQ(pc_get_u32(r.p + 36), pc_get_u32(r.p + 32));          //capture_settings_seq = settings_seq
  T_EQ(mock.trace_builds, 1);

  //Full ring, all four cores, relative to the capture start: wraps at 8192
  memset(p, 0, 16); p[0] = 0x0F; p[1] = 1; pc_put_u32(p + 8, 8192);
  r = req(PC_CMD_READ_RAW, p, 16);
  T_EQ(r.h.status, 0); T_EQ(r.len, 16 + 4 * 8192);
  T_EQ(pc_get_u32(r.p), 1);
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

  //GET_CAPTURE_INFO: the settings of that capture
  r = req(PC_CMD_GET_CAPTURE_INFO, 0, 0);
  T_EQ(r.h.status, 0); T_EQ(r.len, 68);
  T_EQ(pc_get_u32(r.p), 1); T_EQ(r.p[8], PC_ORIGIN_HOST); T_EQ(r.p[9] & 0x13, 0x01);
  T_EQ(pc_get_u16(r.p + 10), 100); T_EQ(pc_get_u16(r.p + 12), 7292);
  T_EQ(pc_get_u32(r.p + 16), 19); T_EQ(r.p[20], 24); T_EQ(r.p[24], 1);
  T_EQ(pc_get_u32(r.p + 28), 2000); T_EQ(pc_get_u32(r.p + 32), 16000);
  T_EQ(r.p[36], 1); T_EQ(r.p[37], 5); T_EQ(pc_get_u16(r.p + 42), 1050); T_EQ(pc_get_u32(r.p + 44), 927734);
  T_EQ(r.p[52 + 1], 5);

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

  //ARM in the UI long time base mode
  setup(0x1532);
  mock.long_mode = 1;
  r = req(PC_CMD_ARM, 0, 0);
  T_EQ(r.h.status, PC_E_STATE); T_EQ(pc_get_u16(r.p), 2); T_EQ(mock.arms, 0);
}

static void test_auto_timeout_and_stop(void)
{
  resp_t r;
  uint8_t w[4];

  setup(0x1532);

  //Auto mode with no trigger: DONE with 0x18 = 0 -> auto_timeout flag
  mock.polls_to_trigger = -1; mock.polls_to_done = 2;
  r = req(PC_CMD_ARM, 0, 0);
  T_EQ(mock.arm_normal, 0);
  r = wait_done(500);
  T_EQ(r.p[1], PC_ACQ_DONE); T_EQ(r.p[2] & 0x04, 0x04);
  r = req(PC_CMD_GET_CAPTURE_INFO, 0, 0);
  T_EQ(r.p[9] & 0x03, 0x02);

  //WAIT times out with status OK, state still ARMED
  mock.polls_to_done = -1;
  r = req(PC_CMD_ARM, 0, 0);
  T_EQ(r.h.status, 0);
  {
    uint16_t seq = next_seq++;
    resp_t d;
    int found = 0, k;

    mock_clear_resps();
    pc_put_u32(w, 20);
    push_frame(PC_CMD_WAIT, seq, w, 4);
    for(k=0;k<30 && !found;k++)
    {
      pc_poll(&S, PC_CTX_MAIN);
      if(find_resp(seq, &d)) { found = 1; T_EQ(d.h.status, 0); T_EQ(d.p[1], PC_ACQ_ARMED); T_TRUE(k >= 19); }
      mock.now += 1;
    }
    T_TRUE(found);
  }

  //A normal request while WAIT is pending gets E_BUSY; STOP aborts the WAIT
  {
    uint16_t sw = next_seq++, sg, ss;
    resp_t d;

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

    T_TRUE(find_resp(sg, &d)); T_EQ(d.h.status, PC_E_BUSY);
    T_TRUE(find_resp(ss, &d)); T_EQ(d.h.status, 0); T_EQ(d.p[1], PC_ACQ_ABORTED); T_EQ(d.p[40], PC_RUN_STOP);
    T_TRUE(find_resp(sw, &d)); T_EQ(d.h.status, PC_E_ABORTED); T_EQ(d.h.cmd, PC_CMD_WAIT);
  }

  //SET_ACQ_STATE RUN while WAIT is pending: the host single is aborted, the UI runs
  r = req(PC_CMD_ARM, 0, 0);
  {
    uint16_t sw = next_seq++, sr = next_seq++;
    uint8_t run[4] = { 1, 0, 0, 0 };
    resp_t d;

    mock_clear_resps();
    pc_put_u32(w, 5000);
    push_frame(PC_CMD_WAIT, sw, w, 4);
    pc_poll(&S, PC_CTX_MAIN);
    push_frame(PC_CMD_SET_ACQ_STATE, sr, run, 4);
    pc_poll(&S, PC_CTX_MAIN);
    pc_poll(&S, PC_CTX_MAIN);
    T_TRUE(find_resp(sw, &d)); T_EQ(d.h.status, PC_E_ABORTED);
    T_TRUE(find_resp(sr, &d)); T_EQ(d.h.status, 0); T_EQ(d.p[1], PC_ACQ_ABORTED); T_EQ(d.p[40], PC_RUN_RUN);
    T_EQ(mock.ui_run, 1);
  }

  //RUN pressed on the screen while a host single is armed: aborted at the next poll
  r = req(PC_CMD_ARM, 0, 0);
  T_EQ(mock.ui_run, 0);
  {
    uint16_t sw = next_seq++;
    resp_t d;

    mock_clear_resps();
    pc_put_u32(w, 5000);
    push_frame(PC_CMD_WAIT, sw, w, 4);
    pc_poll(&S, PC_CTX_MAIN);
    mock.ui_run = 1;
    pc_poll(&S, PC_CTX_MAIN);
    pc_poll(&S, PC_CTX_MAIN);
    T_TRUE(find_resp(sw, &d)); T_EQ(d.h.status, PC_E_ABORTED);
  }

  //The UI arming a conversion (any path through fpga_do_conversion) also aborts it
  mock.ui_run = 0;
  r = req(PC_CMD_ARM, 0, 0);
  T_EQ(S.acq_state, PC_ACQ_ARMED);
  pc_ui_capture_start(&S);
  T_EQ(S.acq_state, PC_ACQ_ABORTED);

  //SET_ACQ_STATE argument check; STOP from the host stops the UI
  {
    uint8_t st[4] = { 2, 0, 0, 0 };
    r = req(PC_CMD_SET_ACQ_STATE, st, 4);
    T_EQ(r.h.status, PC_E_BAD_ARG);
    st[0] = 0;
    mock.ui_run = 1;
    r = req(PC_CMD_SET_ACQ_STATE, st, 4);
    T_EQ(r.h.status, 0); T_EQ(r.len, 44); T_EQ(mock.ui_run, 0); T_EQ(r.p[40], PC_RUN_STOP);
    st[0] = 1;
    mock.trg.ui_mode = 1;                                        //UI single trigger mode: RUN is the UI single "WAIT"
    r = req(PC_CMD_SET_ACQ_STATE, st, 4);
    T_EQ(r.p[40], PC_RUN_SINGLE);
    mock.trg.ui_mode = 0;
  }

  //FPGA never ready
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

  //No capture yet: the UI's trace buffer and geometry
  memset(mock.trace[0], 0x42, 3000);
  memset(p, 0, 12); p[0] = 1;
  r = req(PC_CMD_READ_TRACE, p, 12);
  T_EQ(r.h.status, 0); T_EQ(r.len, 32 + 16 + 3000);
  T_EQ(pc_get_u32(r.p + 4), 3000); T_EQ(pc_get_u32(r.p + 16), 1500); T_EQ((int32_t)pc_get_u32(r.p + 20), 1502);
  T_EQ(r.p[29], PC_ORIGIN_UI_RUN); T_EQ(r.p[48], 0x42); T_EQ(mock.trace_builds, 0);

  //A UI capture: still the UI buffer, origin and capture_id of that capture
  ui_capture(200);
  r = req(PC_CMD_READ_TRACE, p, 12);
  T_EQ(r.h.status, 0); T_EQ(pc_get_u32(r.p), 1); T_EQ(r.p[29], PC_ORIGIN_UI_RUN); T_EQ(mock.trace_builds, 0);

  //Host single: built from the ring once, at DONE
  {
    resp_t t;
    set_trigger(0x30, 0, 0, 0, 0, 2000, 4000, 0, 0, &t);
  }
  r = req(PC_CMD_ARM, 0, 0);
  r = wait_done(100);
  T_EQ(r.p[1], PC_ACQ_DONE);
  T_EQ(mock.trace_builds, 1);
  p[0] = 3; pc_put_u32(p + 4, 10); pc_put_u32(p + 8, 100);
  r = req(PC_CMD_READ_TRACE, p, 12);
  T_EQ(r.h.status, 0); T_EQ(r.len, 32 + 32 + 200);
  T_EQ(pc_get_u32(r.p), 2); T_EQ(pc_get_u32(r.p + 4), 4000); T_EQ(pc_get_u32(r.p + 16), 2000); T_EQ(r.p[29], PC_ORIGIN_HOST);
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

  //A channel disabled at DONE is built on request
  mock.ch[1].enable = 0;
  r = req(PC_CMD_ARM, 0, 0);
  r = wait_done(100);
  T_EQ(mock.trace_builds, 3); T_EQ(S.trace_mask, 0x01);
  p[0] = 2; p[1] = 0;
  r = req(PC_CMD_READ_TRACE, p, 12);
  T_EQ(r.h.status, 0); T_EQ(mock.trace_builds, 4); T_EQ(S.trace_mask, 0x03);
  mock.ch[1].enable = 1;

  //NO_INVERT undoes the firmware's inversion (invert as set when the capture was armed)
  mock.ch[0].invert = 1;
  r = req(PC_CMD_ARM, 0, 0);
  r = wait_done(100);
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

  //count 0 = to the end; a count past the end is clipped; start past the end is an error
  p[1] = 0; pc_put_u32(p + 4, 3990); pc_put_u32(p + 8, 0);
  r = req(PC_CMD_READ_TRACE, p, 12);
  T_EQ(pc_get_u32(r.p + 12), 10);
  pc_put_u32(p + 8, 16000);
  r = req(PC_CMD_READ_TRACE, p, 12);
  T_EQ(r.h.status, 0); T_EQ(pc_get_u32(r.p + 4), 4000); T_EQ(pc_get_u32(r.p + 12), 10); T_EQ(r.len, 32 + 16 + 10);
  pc_put_u32(p + 4, 4000); pc_put_u32(p + 8, 5);
  r = req(PC_CMD_READ_TRACE, p, 12);
  T_EQ(r.h.status, 0); T_EQ(pc_get_u32(r.p + 12), 0);
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

  memset(p, 0, 20); p[0] = 7; p[1] = 1; pc_put_u64(p + 4, 10000000); pc_put_u32(p + 12, 2500);
  r = req(PC_CMD_SET_GEN, p, 20);
  T_EQ(r.h.status, 0); T_EQ(r.p[0], 1);
  T_EQ(mock.gen_phase, 858993); T_EQ(mock.gen_thr, pc_gen_thr_from_bp(2500)); T_EQ(mock.gen_hw_en, 1);
  T_EQ(pc_get_u64(r.p + 12), 9999995); T_EQ(pc_get_u16(r.p + 20), 2500);
  T_EQ(mock.gen_hz, 10000); T_EQ(mock.gen_duty, 25); T_EQ(mock.gen_en, 1);   //UI copy, rounded

  pc_put_u64(p + 4, 25000000001ull); p[0] = 2;
  r = req(PC_CMD_SET_GEN, p, 20);
  T_EQ(r.h.status, PC_E_BAD_ARG);

  //Only duty changes; frequency stays
  memset(p, 0, 20); p[0] = 4; p[3] = 1; pc_put_u32(p + 12, 0x40000000u);
  r = req(PC_CMD_SET_GEN, p, 20);
  T_EQ(mock.gen_phase, 858993); T_EQ(mock.gen_thr, 0x40000000u);
  r = req(PC_CMD_GET_GEN, 0, 0);
  T_EQ(pc_get_u32(r.p + 4), 858993);

  //The user changes the generator on the screen: the UI values win
  mock.gen_hz = 2000;
  r = req(PC_CMD_GET_GEN, 0, 0);
  T_EQ(pc_get_u32(r.p + 4), 171798); T_EQ(S.gen_valid, 0);

  //Stock: no DDS
  setup(0x1432);
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
  resp_t r;
  uint16_t seq;

  setup(0x1532);

  //A BUSY poll answers the safe set, SET_REMOTE (a no-op now) included
  r = req_ctx(PC_CMD_GET_STATUS, 0, 0, PC_CTX_BUSY);
  T_EQ(r.h.status, 0); T_EQ(r.p[3], PC_UI_IDLE);
  r = req_ctx(PC_CMD_SET_REMOTE, "\1", 1, PC_CTX_BUSY);
  T_EQ(r.h.status, 0);

  //...and leaves anything else for the next MAIN poll while the UI is not modal
  mock.now += 10;
  pc_poll(&S, PC_CTX_MAIN);
  mock_clear_resps();
  seq = next_seq++;
  push_frame(PC_CMD_GET_CHANNEL, seq, "\0", 1);
  pc_poll(&S, PC_CTX_BUSY);
  T_EQ(mock_nresps, 0);
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock_nresps, 1);

  //When no MAIN poll came for PC_MODAL_MS the UI is modal: E_BUSY, and ui_busy in GET_STATUS
  mock_clear_resps();
  mock.now += PC_MODAL_MS + 1;
  seq = next_seq++;
  push_frame(PC_CMD_SET_CHANNEL, seq, "\0\x01\0\x03", 4);
  pc_poll(&S, PC_CTX_BUSY);
  T_TRUE(find_resp(seq, &r)); T_EQ(r.h.status, PC_E_BUSY); T_EQ(mock.ch_writes, 0);
  r = req_ctx(PC_CMD_GET_STATUS, 0, 0, PC_CTX_BUSY);
  T_EQ(r.p[3], PC_UI_MODAL);
  r = req_ctx(PC_CMD_GET_BATTERY, 0, 0, PC_CTX_BUSY);
  T_EQ(r.h.status, 0);
  r = req_ctx(PC_CMD_GET_CAPTURE_INFO, 0, 0, PC_CTX_BUSY);
  T_EQ(r.h.status, PC_E_STATE);                                  //answered (no capture yet), not E_BUSY

  //STARTING: USB is up but the main loop has not finished its first pass
  mock_reset(0x1532);
  pc_session_init(&S);
  r = req_ctx(PC_CMD_GET_STATUS, 0, 0, PC_CTX_ISR);
  T_EQ(r.p[3], PC_UI_STARTING);
  pc_poll(&S, PC_CTX_MAIN);
  r = req_ctx(PC_CMD_GET_STATUS, 0, 0, PC_CTX_ISR);
  T_EQ(r.p[3], PC_UI_STARTING);
  mock.now += 30;                                                //a slow first pass: battery ...
  pc_ui_pass_mark(&S, 0);
  mock.now += 1170;                                              //... 0x05 wait ...
  pc_ui_capture_start(&S);
  mock.now += 300;                                               //... capture ...
  pc_ui_capture_addr(&S, 1);
  pc_ui_capture_done(&S);
  pc_ui_pass_mark(&S, 1);
  mock.now += 3300;                                              //... touch
  pc_ui_pass_mark(&S, 2);
  pc_ui_pass_mark(&S, 3);
  pc_poll(&S, PC_CTX_MAIN);
  pc_ui_pass_mark(&S, 3);                                        //second pass: not recorded
  r = req_ctx(PC_CMD_GET_STATUS, 0, 0, PC_CTX_ISR);
  T_EQ(r.p[3], PC_UI_IDLE);
  r = req_ctx(PC_CMD_GET_LINK_STATS, 0, 0, PC_CTX_ISR);
  T_EQ(pc_get_u16(r.p), 58); T_EQ(pc_get_u32(r.p + 4 + 4 * 28), 4800);       //duration of the first pass
  T_EQ(pc_get_u32(r.p + 4 + 4 * 29), 1200 + 1); T_EQ(pc_get_u32(r.p + 4 + 4 * 30), 1500 + 1);
  T_EQ(pc_get_u32(r.p + 4 + 4 * 31), 30 + 1); T_EQ(pc_get_u32(r.p + 4 + 4 * 32), 1500 + 1);
  T_EQ(pc_get_u32(r.p + 4 + 4 * 33), 4800 + 1); T_EQ(pc_get_u32(r.p + 4 + 4 * 34), 4800 + 1);
}

static void test_isr_context(void)
{
  resp_t r;
  uint32_t k;

  //The 1 ms tick poll answers the safe set at once
  setup(0x1532);
  r = req_ctx(PC_CMD_PING, "x", 1, PC_CTX_ISR);
  T_EQ(r.h.status, 0); T_EQ(r.len, 1);
  r = req_ctx(PC_CMD_GET_STATUS, 0, 0, PC_CTX_ISR);
  T_EQ(r.h.status, 0); T_EQ(r.len, 44); T_EQ(r.p[0], PC_SHARED);

  //...and leaves everything else for the main loop, never E_BUSY, however long the main loop takes
  mock_clear_resps();
  push_frame(PC_CMD_SET_CHANNEL, 2000, "\0\x01\0\x03", 4);
  for(k=0;k<1000;k++)
  {
    mock.now++;
    pc_poll(&S, PC_CTX_ISR);
  }
  T_EQ(mock_nresps, 0); T_EQ(mock.ch_writes, 0);
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock_nresps, 1); T_EQ(decode(&mock_resps[0]).h.status, 0); T_EQ(mock.ch_writes, 1);

  //FPGA commands stay on the main loop
  mock_clear_resps();
  push_frame(PC_CMD_ARM, 2001, 0, 0);
  pc_poll(&S, PC_CTX_ISR);
  T_EQ(mock_nresps, 0); T_EQ(mock.arms, 0);
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock.arms, 1);

  //Never nests into a running poll or a UI hook
  S.in_poll = 1;
  mock_clear_resps();
  push_frame(PC_CMD_PING, 2002, "y", 1);
  pc_poll(&S, PC_CTX_ISR);
  T_EQ(mock_nresps, 0);
  S.in_poll = 0;
  pc_poll(&S, PC_CTX_ISR);
  T_EQ(mock_nresps, 1);
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

    //GET_BAD_FRAME returns the damaged request byte for byte
    {
      resp_t b = req(PC_CMD_GET_BAD_FRAME, 0, 0);
      T_EQ(b.h.status, 0); T_EQ(pc_get_u32(b.p), 1); T_EQ(pc_get_u16(b.p + 8), n);
      T_MEM(b.p + 12, f, n);
      mock_clear_resps();
    }

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

//settings_seq follows changes from either side; captures carry the seq they were armed with
static void test_settings_seq(void)
{
  uint8_t p[12];
  resp_t r;
  uint32_t s0, s1;

  setup(0x1532);
  r = status();
  s0 = pc_get_u32(r.p + 32);
  T_EQ(pc_get_u32(r.p + 36), PC_SEQ_INVALID);                  //no capture yet
  r = status();
  T_EQ(pc_get_u32(r.p + 32), s0);                              //nothing changed

  //The user changes the range on the screen
  mock.ch[0].range = 2;
  r = status();
  s1 = pc_get_u32(r.p + 32);
  T_EQ(s1, s0 + 1);

  //The host changes the timebase; the next status shows it
  memset(p, 0, 12); pc_put_u32(p + 4, 20);
  r = req(PC_CMD_SET_TIMEBASE, p, 8);
  r = status();
  T_EQ(pc_get_u32(r.p + 32), s1 + 1);

  //A raw divider counts, and so does dropping it when the UI selects a time base again
  p[0] = 1; pc_put_u32(p + 4, 7);
  r = req(PC_CMD_SET_TIMEBASE, p, 8);
  r = status();
  T_EQ(pc_get_u32(r.p + 32), s1 + 2);
  mock.tb.timeperdiv = 25;
  r = status();
  T_EQ(pc_get_u32(r.p + 32), s1 + 3); T_EQ(S.tb_raw, 0);

  //Never the invalid value: the counter skips it
  S.settings_seq = PC_SEQ_INVALID - 1;
  mock.ch[1].coupling = 1;
  r = status();
  T_EQ(pc_get_u32(r.p + 32), 0);
}

//UI captures through the 1.00o5 hooks: capture_id, origin, settings, GET_CAPTURE_INFO
static void test_ui_capture(void)
{
  resp_t r;
  uint32_t seq;

  setup(0x1532);
  mock.polls_to_trigger = 0;                                   //0x18 reads 1 at the UI capture's DONE
  r = status();
  seq = pc_get_u32(r.p + 32);
  T_EQ(r.p[40], PC_RUN_RUN);

  ui_capture(500);
  r = status();
  T_EQ(pc_get_u32(r.p + 4), 1); T_EQ(r.p[1], PC_ACQ_IDLE);    //acq_state is the host single's only
  T_EQ(pc_get_u16(r.p + 8), 500); T_EQ(pc_get_u16(r.p + 10), 500 + 8192 - 750);
  T_EQ(pc_get_u32(r.p + 36), seq); T_EQ(r.p[41], PC_ORIGIN_UI_RUN);

  r = req(PC_CMD_GET_CAPTURE_INFO, 0, 0);
  T_EQ(r.h.status, 0); T_EQ(pc_get_u32(r.p), 1); T_EQ(pc_get_u32(r.p + 4), seq); T_EQ(r.p[8], PC_ORIGIN_UI_RUN);
  T_EQ(r.p[9] & 0x13, 0x01); T_EQ(pc_get_u16(r.p + 10), 500); T_EQ(pc_get_u16(r.p + 12), 500 + 8192 - 750);
  T_EQ(pc_get_u32(r.p + 28), 1500); T_EQ(pc_get_u32(r.p + 32), 3000);

  //Settings changed while the capture ran: capture_settings_seq invalid, flag bit 4
  pc_ui_capture_start(&S);
  mock.ch[0].range = 1;
  pc_ui_capture_addr(&S, 600);
  pc_ui_capture_done(&S);
  r = status();
  T_EQ(pc_get_u32(r.p + 4), 2); T_EQ(pc_get_u32(r.p + 36), PC_SEQ_INVALID);
  r = req(PC_CMD_GET_CAPTURE_INFO, 0, 0);
  T_EQ(r.p[9] & 0x10, 0x10); T_EQ(r.p[37], 5);                 //CH1 range as armed, not the new 1

  //A DONE without a conversion start (1.00o5 re-processes a finished conversion while the arm gate holds) is not a new
  //capture
  pc_ui_capture_addr(&S, 600);
  pc_ui_capture_done(&S);
  r = status();
  T_EQ(pc_get_u32(r.p + 4), 2);

  //UI single trigger mode
  mock.trg.ui_mode = 1;
  ui_capture(10);
  r = status();
  T_EQ(r.p[41], PC_ORIGIN_UI_SINGLE); T_EQ(pc_get_u16(r.p + 10), (10 + 8192 - 750) % 8192);
}

//READ_RAW of a UI capture: served while the ring holds it, held until the capture in flight finishes in RUN
static void test_read_raw_hold(void)
{
  uint8_t p[16];
  resp_t r;
  uint16_t seq;
  int k;

  setup(0x1532);
  memset(p, 0, 16); p[0] = 0x01; p[1] = 1; pc_put_u32(p + 8, 8);

  //Finished UI capture, not yet re-armed: answered at once
  ui_capture(300);
  r = req(PC_CMD_READ_RAW, p, 16);
  T_EQ(r.h.status, 0); T_EQ(pc_get_u32(r.p), 1); T_EQ(pc_get_u16(r.p + 4), 300 - 750 + 8192);

  //The UI armed the next one: the request waits and the UI may not re-arm after it
  pc_ui_capture_start(&S);
  mock.ui_armed = 1;
  mock_clear_resps();
  seq = next_seq++;
  push_frame(PC_CMD_READ_RAW, seq, p, 16);
  for(k=0;k<20;k++) { pc_poll(&S, PC_CTX_MAIN); mock.now++; }
  T_EQ(mock_nresps, 0); T_EQ(pc_ui_arm_allowed(&S), 0);

  //PING meanwhile waits too (one request at a time), but the timer poll keeps the link alive
  pc_ui_capture_addr(&S, 1000);
  pc_ui_capture_done(&S);
  mock.ui_armed = 0;
  pc_poll(&S, PC_CTX_MAIN);
  T_TRUE(find_resp(seq, &r)); T_EQ(r.h.status, 0); T_EQ(pc_get_u32(r.p), 2); T_EQ(pc_get_u16(r.p + 4), 250);
  T_EQ(pc_ui_arm_allowed(&S), 1);

  //No capture finishes within PC_HOLD_MS: E_BUSY detail 1, the UI may arm again
  pc_ui_capture_start(&S);
  mock.ui_armed = 1;
  mock_clear_resps();
  seq = next_seq++;
  push_frame(PC_CMD_READ_RAW, seq, p, 16);
  pc_poll(&S, PC_CTX_MAIN);
  mock.now += PC_HOLD_MS - 1;
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock_nresps, 0);
  mock.now += 1;
  pc_poll(&S, PC_CTX_MAIN);
  T_TRUE(find_resp(seq, &r)); T_EQ(r.h.status, PC_E_BUSY); T_EQ(pc_get_u16(r.p), 1);
  T_EQ(pc_ui_arm_allowed(&S), 1);

  //A USB reset drops a held request and releases the UI (a "#RESET" line is only parsed after the hold ends)
  mock_clear_resps();
  push_frame(PC_CMD_READ_RAW, next_seq++, p, 16);
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(pc_ui_arm_allowed(&S), 0);
  mock.link_events = PC_LINK_RESET;
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(pc_ui_arm_allowed(&S), 1); T_EQ(mock_nresps, 0);

  //Stopped with the ring overwritten (STOP while a UI capture was armed): E_STATE detail 1
  mock.ui_run = 0;
  r = req(PC_CMD_READ_RAW, p, 16);
  T_EQ(r.h.status, PC_E_STATE); T_EQ(pc_get_u16(r.p), 1);

  //ANY reads the ring as it is
  p[3] = 0x01;
  r = req(PC_CMD_READ_RAW, p, 16);
  T_EQ(r.h.status, 0);
  p[3] = 0;

  //Long time base mode
  mock.long_mode = 1;
  r = req(PC_CMD_READ_RAW, p, 16);
  T_EQ(r.h.status, PC_E_STATE); T_EQ(pc_get_u16(r.p), 2);
}

//MCP chip: steady while DTR is up or traffic within 2 s (no pulse)
static void test_indicator(void)
{
  setup(0x1532);
  //Off at boot: nothing to draw (the version text is there)
  mock.dtr = 0;
  pc_ui_service(&S);
  T_EQ(mock.indicator, -1);

  mock.dtr = 1;
  pc_ui_service(&S);
  T_EQ(mock.indicator, 1);

  T_EQ(pc_ui_chip_shown(&S), 1);
  (void)req(PC_CMD_PING, "z", 1);
  pc_ui_service(&S);
  T_EQ(mock.indicator, 1);                                       //no pulse on traffic

  //Redrawn periodically, not every pass
  {
    uint32_t n = mock.indicator_draws;
    pc_ui_service(&S);
    T_EQ(mock.indicator_draws, n);
    mock.now += PC_CHIP_REDRAW_MS;
    pc_ui_service(&S);
    T_EQ(mock.indicator_draws, n + 1);
  }

  //Port open without DTR: traffic keeps it on for 2 s
  mock.dtr = 0;
  pc_ui_service(&S);
  T_EQ(mock.indicator, 1);
  mock.now += PC_HOST_ACTIVE_MS;
  pc_ui_service(&S);
  T_EQ(mock.indicator, 0); T_EQ(pc_ui_chip_shown(&S), 0);
}

//SD sector commands: info, read, stage + write with every safety limit
static void sd_stage_all(const uint8_t *data, uint32_t n)
{
  uint8_t p[8 + 480];
  uint32_t off;
  resp_t r;

  for(off=0;off<n;off+=480)
  {
    uint32_t k = (n - off > 480) ? 480 : n - off;

    pc_put_u32(p, off); pc_put_u16(p + 4, (uint16_t)k); pc_put_u16(p + 6, 0);
    memcpy(p + 8, data + off, k);
    r = req(PC_CMD_SD_STAGE, p, 8 + k);
    T_EQ(r.h.status, 0);
    if(pc_get_u32(r.p + 8) != pc_crc32(data + off, k)) { T_TRUE(0); break; }
  }
}

static resp_t sd_write(uint32_t sector, uint16_t count, uint8_t flags, uint32_t crc, uint32_t magic)
{
  uint8_t p[16];

  memset(p, 0, 16);
  pc_put_u32(p, sector); pc_put_u16(p + 4, count); p[6] = flags; pc_put_u32(p + 8, crc); pc_put_u32(p + 12, magic);
  return req(PC_CMD_SD_WRITE_SECTORS, p, 16);
}

static void test_sd(void)
{
  static uint8_t img[128 * 512];
  uint8_t p[16];
  resp_t r;
  uint32_t i, crc;

  setup(0x1532);
  r = req(PC_CMD_GET_INFO, 0, 0);
  T_EQ(pc_get_u32(r.p + 20) & PC_CAP_SD_SECTORS, PC_CAP_SD_SECTORS);

  //SD_INFO: card, partition, write window, stage size, the three eGON headers
  r = req(PC_CMD_SD_INFO, "\x01", 1);
  T_EQ(r.h.status, 0); T_EQ(r.len, 64);
  T_EQ(pc_get_u32(r.p), MOCK_SD_SECTORS); T_EQ(pc_get_u32(r.p + 4), 2048); T_EQ(pc_get_u32(r.p + 8), 2048);
  T_EQ(pc_get_u32(r.p + 12), 65536);
  T_EQ(pc_get_u32(r.p + 16), 16);  T_EQ(r.p[20], 1); T_EQ(r.p[21], 1); T_EQ(pc_get_u32(r.p + 24), 8 * 512);
  T_EQ(pc_get_u32(r.p + 32), 80);  T_EQ(r.p[36], 0); T_EQ(r.p[37], 0);
  T_EQ(pc_get_u32(r.p + 48), 750); T_EQ(r.p[52], 2); T_EQ(r.p[53], 1); T_EQ(pc_get_u32(r.p + 56), 750 * 512);
  mock.sd[750 * 512 + 9000] ^= 1;                               //damaged image: checksum bad
  r = req(PC_CMD_SD_INFO, "\x01", 1);
  T_EQ(r.p[53], 2);
  r = req(PC_CMD_SD_INFO, "\x00", 1);
  T_EQ(r.p[53], 0); T_EQ(r.p[52], 2);                           //not checked without VERIFY
  r = req(PC_CMD_SD_INFO, "\x02", 1);
  T_EQ(r.h.status, PC_E_BAD_ARG);

  //SD_READ_SECTORS
  memset(p, 0, 16); pc_put_u32(p, 750); pc_put_u16(p + 4, 128);
  r = req(PC_CMD_SD_READ_SECTORS, p, 8);
  T_EQ(r.h.status, 0); T_EQ(r.len, 16 + 128 * 512);
  T_MEM(r.p + 16, mock.sd + 750 * 512, 128 * 512);
  T_EQ(pc_get_u32(r.p + 8), pc_crc32(mock.sd + 750 * 512, 128 * 512));
  pc_put_u16(p + 4, 129);
  r = req(PC_CMD_SD_READ_SECTORS, p, 8);      T_EQ(r.h.status, PC_E_BAD_ARG); T_EQ(pc_get_u16(r.p), 4);
  pc_put_u32(p, MOCK_SD_SECTORS - 1); pc_put_u16(p + 4, 2);
  r = req(PC_CMD_SD_READ_SECTORS, p, 8);      T_EQ(r.h.status, PC_E_BAD_ARG); T_EQ(pc_get_u16(r.p), 0);

  //Stage 128 sectors and write them to 750: read back equal, CRC answered
  for(i=0;i<sizeof(img);i++) img[i] = (uint8_t)(i * 7 + 3);
  sd_stage_all(img, sizeof(img));
  crc = pc_crc32(img, sizeof(img));
  r = sd_write(750, 128, 0, crc, PC_SD_WRITE_MAGIC);
  T_EQ(r.h.status, 0); T_EQ(pc_get_u32(r.p + 8), crc);
  T_MEM(mock.sd + 750 * 512, img, sizeof(img));

  //SD_STAGE is answered from the timer poll (no main-loop pass per 484-byte piece)
  {
    uint8_t s[12];
    pc_put_u32(s, 0); pc_put_u16(s + 4, 4); pc_put_u16(s + 6, 0); memcpy(s + 8, img, 4);
    r = req_ctx(PC_CMD_SD_STAGE, s, 12, PC_CTX_ISR);
    T_EQ(r.h.status, 0); T_EQ(pc_get_u32(r.p + 8), pc_crc32(img, 4));
  }

  //Stage limits
  {
    uint8_t s[12];
    pc_put_u32(s, 65536 - 2); pc_put_u16(s + 4, 4); pc_put_u16(s + 6, 0); memset(s + 8, 0, 4);
    r = req(PC_CMD_SD_STAGE, s, 12);          T_EQ(r.h.status, PC_E_BAD_ARG);
    pc_put_u32(s, 0); pc_put_u16(s + 4, 3);
    r = req(PC_CMD_SD_STAGE, s, 12);          T_EQ(r.h.status, PC_E_BAD_LEN);
  }

  //Every refusal leaves the card untouched
  {
    uint32_t w = mock.sd_writes;
    r = sd_write(750, 1, 0, pc_crc32(img, 512), 0x12345678);      T_EQ(r.h.status, PC_E_BAD_ARG); T_EQ(pc_get_u16(r.p), 12);
    r = sd_write(750, 1, 0, crc, PC_SD_WRITE_MAGIC);              T_EQ(r.h.status, PC_E_BAD_ARG); T_EQ(pc_get_u16(r.p), 8);   //stage crc
    r = sd_write(709, 1, 0, pc_crc32(img, 512), PC_SD_WRITE_MAGIC); T_EQ(r.h.status, PC_E_BAD_ARG);                         //config
    r = sd_write(700, 16, 0, pc_crc32(img, 8192), PC_SD_WRITE_MAGIC); T_EQ(r.h.status, PC_E_BAD_ARG);                       //spans 708
    r = sd_write(16, 1, 0, pc_crc32(img, 512), PC_SD_WRITE_MAGIC);  T_EQ(r.h.status, PC_E_BAD_ARG);                         //bootloader
    r = sd_write(15, 1, PC_SD_WF_BOOT, pc_crc32(img, 512), PC_SD_WRITE_MAGIC); T_EQ(r.h.status, PC_E_BAD_ARG);              //below 16
    r = sd_write(2047, 2, 0, pc_crc32(img, 1024), PC_SD_WRITE_MAGIC); T_EQ(r.h.status, PC_E_BAD_ARG);                       //partition
    r = sd_write(800, 0, 0, 0, PC_SD_WRITE_MAGIC);                  T_EQ(r.h.status, PC_E_BAD_ARG); T_EQ(pc_get_u16(r.p), 4);
    r = sd_write(800, 1, 0x04, pc_crc32(img, 512), PC_SD_WRITE_MAGIC); T_EQ(r.h.status, PC_E_BAD_ARG); T_EQ(pc_get_u16(r.p), 6);
    T_EQ(mock.sd_writes, w);
  }

  //With the flags the protected areas can be written
  r = sd_write(709, 1, PC_SD_WF_CONFIG, pc_crc32(img, 512), PC_SD_WRITE_MAGIC);   T_EQ(r.h.status, 0);
  r = sd_write(16, 1, PC_SD_WF_BOOT, pc_crc32(img, 512), PC_SD_WRITE_MAGIC);      T_EQ(r.h.status, 0);
  r = sd_write(2047, 1, 0, pc_crc32(img, 512), PC_SD_WRITE_MAGIC);                T_EQ(r.h.status, 0);

  //A partition below 2048 moves the window down
  pc_put_u32(mock.sd + 0x1BE + 8, 1500);
  r = req(PC_CMD_SD_INFO, "\0", 1);           T_EQ(pc_get_u32(r.p + 8), 1500);
  r = sd_write(1499, 2, 0, pc_crc32(img, 1024), PC_SD_WRITE_MAGIC);               T_EQ(r.h.status, PC_E_BAD_ARG);
  r = sd_write(1499, 1, 0, pc_crc32(img, 512), PC_SD_WRITE_MAGIC);                T_EQ(r.h.status, 0);

  //No partition table: writes off
  mock.sd[510] = 0;
  r = req(PC_CMD_SD_INFO, "\0", 1);           T_EQ(pc_get_u32(r.p + 4), 0); T_EQ(pc_get_u32(r.p + 8), 0);
  r = sd_write(800, 1, 0, pc_crc32(img, 512), PC_SD_WRITE_MAGIC);                 T_EQ(r.h.status, PC_E_STATE);
  mock.sd[510] = 0x55;

  //Driver error and verify mismatch
  mock.sd_fail_write = -2;
  r = sd_write(800, 1, 0, pc_crc32(img, 512), PC_SD_WRITE_MAGIC);                 T_EQ(r.h.status, PC_E_IO); T_EQ(pc_get_u16(r.p), 2);
  mock.sd_fail_write = 0; mock.sd_corrupt = 1;
  r = sd_write(800, 1, 0, pc_crc32(img, 512), PC_SD_WRITE_MAGIC);                 T_EQ(r.h.status, PC_E_IO); T_EQ(pc_get_u16(r.p), 0xFFFF);
  mock.sd_corrupt = 0;

  //No card
  mock.sd_sectors = 0;
  r = req(PC_CMD_SD_INFO, "\0", 1);           T_EQ(r.h.status, PC_E_IO);

  //Unknown bitstream: fails closed like everything with a capability
  setup(0x9999);
  r = req(PC_CMD_SD_INFO, "\0", 1);           T_EQ(r.h.status, PC_E_UNSUPPORTED);
}

//EXT trigger (FPGA 0x1533, protocol 1.4)
static void test_ext_trigger(void)
{
  resp_t r;
  uint8_t g[20];

  //0x1532: no EXT
  setup(0x1532);
  r = req(PC_CMD_GET_INFO, 0, 0);
  T_EQ(pc_get_u32(r.p + 20) & PC_CAP_EXT_TRIGGER, 0);
  set_trigger(0x01, 2, 0, 0, 0, 0, 0, 0, 0, &r);
  T_EQ(r.h.status, PC_E_UNSUPPORTED);
  T_EQ(mock.ext_reads, 0);                                      //0x28 never read on 0x1532

  //0x1533: AL3 geometry plus EXT
  setup(0x1533);
  r = req(PC_CMD_GET_INFO, 0, 0);
  T_EQ(r.p[10], 2); T_EQ(pc_get_u16(r.p + 12), 8192); T_EQ(pc_get_u16(r.p + 28), 16382);
  T_EQ(pc_get_u32(r.p + 20) & PC_CAP_EXT_TRIGGER, PC_CAP_EXT_TRIGGER);
  T_EQ(r.p[1], 5);                                              //proto 1.5

  //Generator on, then EXT: the generator goes off
  memset(g, 0, 20); g[0] = 1; g[1] = 1;
  r = req(PC_CMD_SET_GEN, g, 20);
  T_EQ(r.h.status, 0); T_EQ(mock.gen_hw_en, 1);
  set_trigger(0x01, 2, 0, 0, 0, 0, 0, 0, 0, &r);
  T_EQ(r.h.status, 0); T_EQ(r.p[0], 2); T_EQ(mock.gen_hw_en, 0); T_EQ(mock.gen_en, 0);

  //...and cannot be switched on while EXT is the source
  r = req(PC_CMD_SET_GEN, g, 20);
  T_EQ(r.h.status, PC_E_STATE);
  g[1] = 0;
  r = req(PC_CMD_SET_GEN, g, 20);
  T_EQ(r.h.status, 0);

  //GET_STATUS byte 42: the FPGA 0x28 bits, read at MAIN polls
  mock.ext_status = 0x05;
  r = status();
  T_EQ(r.p[42], 0x05); T_TRUE(mock.ext_reads > 0);

  //A capture during which an EXT edge was seen: GET_CAPTURE_INFO flags bit 5
  r = req(PC_CMD_ARM, 0, 0);
  r = wait_done(100);
  T_EQ(r.p[1], PC_ACQ_DONE);
  r = req(PC_CMD_GET_CAPTURE_INFO, 0, 0);
  T_EQ(r.p[9] & 0x20, 0x20); T_EQ(r.p[21], 2);
  mock.ext_status = 0;
  ui_capture(10);
  r = req(PC_CMD_GET_CAPTURE_INFO, 0, 0);
  T_EQ(r.p[9] & 0x20, 0);

  //Back to CH1: generator allowed again
  set_trigger(0x01, 0, 0, 0, 0, 0, 0, 0, 0, &r);
  g[1] = 1;
  r = req(PC_CMD_SET_GEN, g, 20);
  T_EQ(r.h.status, 0);
}

//The timer-tick (ISR) and BUSY polls must never drive the FPGA bus: they can interrupt the main loop, or a UI busy
//loop, in the middle of a bit-banged FPGA transaction. Worst case for 0x1533: EXT caps, generator on, an armed
//capture, main-loop requests queued behind the safe set, the UI modal. Counts every mock HAL call that drives the bus.
static void test_fast_path_no_fpga(void)
{
  static const uint16_t safe[] = { PC_CMD_PING, PC_CMD_GET_INFO, PC_CMD_GET_STATUS, PC_CMD_GET_BATTERY,
                                   PC_CMD_GET_LINK_STATS, PC_CMD_GET_BAD_FRAME, PC_CMD_GET_EVENT_LOG,
                                   PC_CMD_GET_CAPTURE_INFO };
  static const int ctxs[] = { PC_CTX_ISR, PC_CTX_BUSY };
  resp_t r;
  uint8_t g[20];
  uint32_t ops, i, c, k;

  setup(0x1533);
  r = req(PC_CMD_SET_REMOTE, "\1", 1);
  T_EQ(r.h.status, 0);
  memset(g, 0, 20); g[0] = 1; g[1] = 1;
  r = req(PC_CMD_SET_GEN, g, 20);
  T_EQ(r.h.status, 0);
  mock.ext_status = 0x06;
  mock.polls_to_trigger = -1; mock.polls_to_done = -1;
  r = req(PC_CMD_ARM, 0, 0);
  T_EQ(r.h.status, 0);
  r = status();
  T_EQ(r.p[42], 0x06);                                           //cached at the MAIN poll

  ops = mock.fpga_ops;

  for(c=0;c<2;c++)
  {
    for(i=0;i<sizeof(safe)/sizeof(safe[0]);i++)
    {
      r = req_ctx(safe[i], 0, 0, ctxs[c]);
      T_TRUE(r.h.status != 0xEEEE);
    }
    r = req_ctx(PC_CMD_SET_REMOTE, "\1", 1, ctxs[c]);
    T_EQ(r.h.status, 0);
    r = req_ctx(PC_CMD_GET_STATUS, 0, 0, ctxs[c]);
    T_EQ(r.p[42], 0x06);                                         //the cached byte, no 0x28 read
  }

  //FPGA requests queued while only fast polls run (UI not modal, then modal: E_BUSY from BUSY polls)
  for(k=0;k<2;k++)
  {
    mock_clear_resps();
    push_frame(PC_CMD_ARM, next_seq++, 0, 0);
    push_frame(PC_CMD_SET_CHANNEL, next_seq++, "\0\x01\0\x03", 4);
    push_frame(PC_CMD_FORCE, next_seq++, 0, 0);

    for(i=0;i<200;i++)
    {
      mock.now++;
      pc_poll(&S, PC_CTX_ISR);
      pc_poll(&S, PC_CTX_BUSY);
      mock_tx_release();
    }

    if(k == 0)
      mock.now += PC_MODAL_MS + 1;
  }

  T_EQ(mock.fpga_ops, ops);
  T_EQ(mock.arms, 1); T_EQ(mock.ch_writes, 0); T_EQ(mock.forces, 0);

  //Sanity: the counter sees the MAIN poll's own reads (acquisition service, 0x28)
  pc_poll(&S, PC_CTX_MAIN);
  T_TRUE(mock.fpga_ops > ops);
}

//A host opens the port (DTR 0 -> 1) and sends a main-loop request at once. The timer poll parses it before the
//main loop takes the DTR event: the request must still be answered (fw 0.2.10 and older dropped it: bench 0.2.8/0.2.9,
//the first TEST_PATTERN after a FEL load timed out)
static void test_dtr_up_keeps_request(void)
{
  uint8_t p[8];
  resp_t r;
  uint16_t seq;

  setup(0x1532);
  memset(p, 0, 8); pc_put_u32(p, 492);

  mock_clear_resps();
  mock.link_events = PC_LINK_DTR_UP;
  seq = next_seq++;
  push_frame(PC_CMD_TEST_PATTERN, seq, p, 8);
  pc_poll(&S, PC_CTX_ISR);                                       //parsed, held for the main loop
  T_EQ(mock_nresps, 0);
  pc_poll(&S, PC_CTX_MAIN);
  T_TRUE(find_resp(seq, &r)); T_EQ(r.h.status, 0); T_EQ(r.len, 492);

  //The same with the DTR edge arriving while the request is already pending
  mock_clear_resps();
  seq = next_seq++;
  push_frame(PC_CMD_TEST_PATTERN, seq, p, 8);
  pc_poll(&S, PC_CTX_ISR);
  mock.link_events = PC_LINK_DTR_UP;
  pc_poll(&S, PC_CTX_MAIN);
  T_TRUE(find_resp(seq, &r)); T_EQ(r.h.status, 0);

  //A partial frame from before the DTR edge is still discarded: the next complete frame is parsed cleanly
  {
    uint8_t f[64];
    uint32_t n;

    mock_clear_resps();
    n = pc_frame_finish(f, PC_CMD_PING, 0x7000, 0, 0, 0);
    mock_rx_push(f, 10);                                         //10 of 16 header bytes, then the port is reopened
    pc_poll(&S, PC_CTX_ISR);
    mock.link_events = PC_LINK_DTR_UP;
    seq = next_seq++;
    push_frame(PC_CMD_PING, seq, "q", 1);
    pc_poll(&S, PC_CTX_ISR);
    pc_poll(&S, PC_CTX_ISR);
    T_TRUE(find_resp(seq, &r)); T_EQ(r.h.status, 0);
    (void)n;
  }
}

//GET_EVENT_LOG (protocol 1.5): answered from the timer poll, entries oldest first
static void test_event_log(void)
{
  resp_t r;

  setup(0x1532);
  r = req_ctx(PC_CMD_GET_EVENT_LOG, 0, 0, PC_CTX_ISR);
  T_EQ(r.h.status, 0); T_EQ(pc_get_u16(r.p), 3); T_EQ(pc_get_u32(r.p + 4), 77); T_EQ(pc_get_u32(r.p + 8), mock.now);
  T_EQ(r.len, 12 + 3 * 8 + 8 + 2 * 8); T_EQ(pc_get_u32(r.p + 12), 100); T_EQ(r.p[12 + 4], 1); T_EQ(r.p[12 + 8 + 4], 2);
  T_EQ(r.p[12 + 5], 0xA0); T_EQ(pc_get_u16(r.p + 12 + 6), 0x1234);

  //0.2.15: the reset snapshot after the entries
  T_EQ(pc_get_u16(r.p + 36), 2); T_EQ(pc_get_u16(r.p + 38), 1); T_EQ(pc_get_u32(r.p + 40), 95);
  T_EQ(pc_get_u32(r.p + 44), 90); T_EQ(r.p[48], 5); T_EQ(r.p[52 + 4], 1);

  //0.2.18: a plain read keeps the snapshot frozen; flag bit 0 re-arms it; other bits are refused
  {
    extern int mock_snap_rearms;
    int before = mock_snap_rearms;

    r = req_ctx(PC_CMD_GET_EVENT_LOG, "\x01", 1, PC_CTX_ISR);
    T_EQ(r.h.status, 0); T_EQ(mock_snap_rearms, before + 1);
    r = req_ctx(PC_CMD_GET_EVENT_LOG, 0, 0, PC_CTX_ISR);
    T_EQ(mock_snap_rearms, before + 1);
    r = req_ctx(PC_CMD_GET_EVENT_LOG, "\x02", 1, PC_CTX_ISR);
    T_EQ(r.h.status, PC_E_BAD_ARG);
  }
}

static void test_fel_reset_stall(void)
{
  uint8_t p[4];
  resp_t r;
  uint32_t j;
  int found;

  //ENTER_FEL: cap set, wrong magic refused, right magic answers OK first and jumps after the response is out
  setup(0x1532);
  r = req(PC_CMD_GET_INFO, 0, 0);
  T_TRUE(pc_get_u32(r.p + 20) & PC_CAP_ENTER_FEL);
  T_EQ(r.p[1], 5);                                              //proto_minor 1.5
  pc_put_u32(p, 0x12345678);
  r = req(PC_CMD_ENTER_FEL, p, 4);
  T_EQ(r.h.status, PC_E_BAD_ARG); T_EQ(mock.fel_entries, 0);
  pc_put_u32(p, PC_FEL_MAGIC);
  mock.tx_hold = 1;
  mock_clear_resps();
  push_frame(PC_CMD_ENTER_FEL, 777, p, 4);
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock_nresps, 1); T_EQ(decode(&mock_resps[0]).h.status, 0);
  T_EQ(mock.fel_entries, 0);                                    //response still in flight
  mock_tx_release();
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock.fel_entries, 1);
  mock.tx_hold = 0;

  //Unknown bitstream: no FEL cap (fail closed)
  setup(0x9999);
  r = req(PC_CMD_GET_INFO, 0, 0);
  T_EQ(pc_get_u32(r.p + 20), 0);

  //TX stall watchdog: a frame that makes no progress for 2 s is dropped, the next request is served
  setup(0x1532);
  mock.tx_hold = 1;
  mock_clear_resps();
  push_frame(PC_CMD_PING, 900, "a", 1);
  pc_poll(&S, PC_CTX_MAIN);
  push_frame(PC_CMD_PING, 901, "b", 1);
  mock.now += PC_TX_STALL_MS - 1;
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock_nresps, 1); T_EQ(mock.tx_aborts, 0);
  mock.now += 1;
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock.tx_aborts, 1); T_EQ(S.tx_aborts, 1);
  T_EQ(mock_nresps, 2); T_EQ(decode(&mock_resps[1]).h.seq, 901);

  //#RESET aborts a stuck response at once, drops a pending WAIT and answers #OK
  setup(0x1532);
  mock.polls_to_done = -1;
  r = req(PC_CMD_ARM, 0, 0);
  pc_put_u32(p, 5000);
  mock_clear_resps();
  push_frame(PC_CMD_WAIT, 950, p, 4);
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(S.def_active, 1);
  mock.tx_hold = 1;
  push_frame(PC_CMD_TEST_PATTERN, 951, "\x00\x10\x00\x00\x00\x00\x00\x00", 8);
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock.tx_busy, 1);
  mock_rx_push((const uint8_t *)"#RESET\n", 7);
  pc_poll(&S, PC_CTX_MAIN);
  T_EQ(mock.tx_aborts, 1); T_EQ(S.def_active, 0);
  found = 0;
  for(j=0;j<mock_nresps;j++)
    if(mock_resps[j].len == 4 && memcmp(mock_resps[j].data, "#OK\n", 4) == 0) found = 1;
  T_TRUE(found);
  T_TRUE((S.acq_state == PC_ACQ_ARMED) || (S.acq_state == PC_ACQ_TRIGGERED));   //acquisition state unchanged
  mock.tx_hold = 0; mock_tx_release();
  r = req(PC_CMD_GET_STATUS, 0, 0);
  T_EQ(r.h.status, 0); T_EQ(r.p[2] & 0x20, 0);                  //no deferred request pending

  //GET_LINK_STATS: 11 transport + 8 parser + 8 session + 2 startup counters, answered from every context
  setup(0x1532);
  r = req_ctx(PC_CMD_GET_LINK_STATS, 0, 0, PC_CTX_ISR);
  T_EQ(r.h.status, 0); T_EQ(pc_get_u16(r.p), 58); T_EQ(r.len, 4 + 58 * 4);
  T_EQ(pc_get_u32(r.p + 4 + 4 * 49), 6000); T_EQ(pc_get_u32(r.p + 4 + 4 * 56), 6007);   //restart record
  T_EQ(pc_get_u32(r.p + 4 + 4 * 57), 0);                         //retired (usb_irq_races, fw 0.2.20)
  T_EQ(pc_get_u32(r.p + 4 + 4 * 47), 0);                         //retired (usb_irq_polled, fw 0.2.12-0.2.20)
  T_EQ(pc_get_u32(r.p + 4 + 4 * 48), 5000);                      //boot splash ms + 1
  T_EQ(pc_get_u32(r.p + 4 + 4 * 41), 3000); T_EQ(pc_get_u32(r.p + 4 + 4 * 46), 3005);   //RX back-pressure
  T_EQ(pc_get_u32(r.p + 4 + 4 * 35), 2000); T_EQ(pc_get_u32(r.p + 4 + 4 * 40), 2005);   //USB events
  T_EQ(pc_get_u32(r.p + 4), 1000); T_EQ(pc_get_u32(r.p + 4 + 4 * 10), 1010);
  T_EQ(pc_get_u32(r.p + 4 + 4 * 11), S.parser.stats.frames);
  T_EQ(pc_get_u32(r.p + 4 + 4 * 24), 0);                         //disp_isr counted after this response
  T_EQ(pc_get_u32(r.p + 4 + 4 * 28), 30);                        //first pass: setup()'s 30 ms
  r = req_ctx(PC_CMD_GET_LINK_STATS, 0, 0, PC_CTX_ISR);
  T_EQ(pc_get_u32(r.p + 4 + 4 * 24), 1);
  r = req(PC_CMD_GET_LINK_STATS, 0, 0);
  T_EQ(pc_get_u32(r.p + 4 + 4 * 22), 0);                         //disp_main, also counted after the response
  r = req(PC_CMD_GET_LINK_STATS, 0, 0);
  T_EQ(pc_get_u32(r.p + 4 + 4 * 22), 1);

  //First MAIN interval (boot) is not a pass
  mock_reset(0x1532);
  pc_session_init(&S);
  mock.now += 1500;
  pc_poll(&S, PC_CTX_MAIN);
  r = req(PC_CMD_GET_STATUS, 0, 0);
  T_TRUE(pc_get_u16(r.p + 30) < 100);

  //GET_STATUS 1.1: main-loop pass time, last and max since the previous GET_STATUS
  setup(0x1532);
  r = req(PC_CMD_GET_STATUS, 0, 0);
  mock.now += 40; pc_poll(&S, PC_CTX_MAIN);
  mock.now += 90; pc_poll(&S, PC_CTX_MAIN);
  mock.now += 30; pc_poll(&S, PC_CTX_MAIN);
  r = req(PC_CMD_GET_STATUS, 0, 0);
  T_EQ(r.len, 44); T_EQ(pc_get_u16(r.p + 30), 90);
  r = req(PC_CMD_GET_STATUS, 0, 0);
  T_TRUE(pc_get_u16(r.p + 30) < 90);
}

int main(void)
{
  test_info();
  test_shared_basics();
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
  test_isr_context();
  test_transport_window();
  test_settings_seq();
  test_ui_capture();
  test_read_raw_hold();
  test_indicator();
  test_sd();
  test_ext_trigger();
  test_fast_path_no_fpga();
  test_dtr_up_keeps_request();
  test_event_log();
  test_fel_reset_stall();
  mock_clear_resps();
  return t_report("test_session");
}
