//----------------------------------------------------------------------------------------------------------------------------------
// Unit tests for pc_protocol.c (frame layer). Host build: make -C firmware/scope/host_tests test
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "pc_protocol.h"
#include "test_util.h"

//Example frames from docs/protocol.md section 13
static const char *EX_GET_INFO  = "a51301000200010000000000000022a6";
static const char *EX_PING      = "a5130100010002000000020000 00f032 6869 ac2a93d8";
static const char *EX_PING_RESP = "a513010101000200000002000000b9ea6869ac2a93d8";
static const char *EX_SET_TB    = "a51301000301030000000800000021280000000013000000 18278e27";
static const char *EX_ERR       = "a513010101010400060000000000d8e1";

static int feed_all(pc_parser *p, const uint8_t *d, uint32_t n, uint32_t now, int *events, int max)
{
  int count = 0, ev;
  uint32_t i;

  for(i=0;i<n;i++)
  {
    ev = pc_parser_feed(p, d[i], now);
    if(ev && count < max) events[count++] = ev;
    while((ev = pc_parser_service(p, now)) != PC_EV_NONE)
      if(count < max) events[count++] = ev;
  }

  return count;
}

static void test_crc(void)
{
  T_EQ(pc_crc16((const uint8_t *)"123456789", 9), 0x29B1);
  T_EQ(pc_crc32((const uint8_t *)"123456789", 9), 0xCBF43926u);
  T_EQ(pc_crc32_update(pc_crc32((const uint8_t *)"1234", 4), (const uint8_t *)"56789", 5), 0xCBF43926u);
  T_EQ(pc_crc32((const uint8_t *)"", 0), 0);
}

static void test_encode_examples(void)
{
  uint8_t f[64], ex[64];
  uint32_t n;

  n = pc_frame_finish(f, PC_CMD_GET_INFO, 1, 0, 0, 0);
  T_EQ(n, 16);
  T_EQ(unhex(EX_GET_INFO, ex, sizeof(ex)), 16);
  T_MEM(f, ex, 16);

  f[16] = 'h'; f[17] = 'i';
  n = pc_frame_finish(f, PC_CMD_PING, 2, 0, 0, 2);
  T_EQ(n, 22);
  T_EQ(unhex(EX_PING, ex, sizeof(ex)), 22);
  T_MEM(f, ex, 22);

  n = pc_frame_finish(f, PC_CMD_PING, 2, 0, PC_FLAG_RESPONSE, 2);
  T_EQ(unhex(EX_PING_RESP, ex, sizeof(ex)), 22);
  T_MEM(f, ex, n);

  memset(f + 16, 0, 8); f[20] = 19;
  n = pc_frame_finish(f, PC_CMD_SET_TIMEBASE, 3, 0, 0, 8);
  T_EQ(unhex(EX_SET_TB, ex, sizeof(ex)), 28);
  T_MEM(f, ex, n);

  n = pc_frame_finish(f, PC_CMD_SET_CHANNEL, 4, PC_E_NOT_REMOTE, PC_FLAG_RESPONSE, 0);
  T_EQ(unhex(EX_ERR, ex, sizeof(ex)), 16);
  T_MEM(f, ex, n);
}

static void test_parse_examples(void)
{
  static pc_parser p;
  uint8_t ex[64];
  int ev[8], n;

  pc_parser_init(&p);
  n = feed_all(&p, ex, unhex(EX_GET_INFO, ex, sizeof(ex)), 0, ev, 8);
  T_EQ(n, 1); T_EQ(ev[0], PC_EV_FRAME);
  T_EQ(p.hdr.cmd, PC_CMD_GET_INFO); T_EQ(p.hdr.seq, 1); T_EQ(p.hdr.len, 0);

  n = feed_all(&p, ex, unhex(EX_SET_TB, ex, sizeof(ex)), 0, ev, 8);
  T_EQ(n, 1); T_EQ(ev[0], PC_EV_FRAME);
  T_EQ(p.hdr.cmd, PC_CMD_SET_TIMEBASE); T_EQ(p.hdr.len, 8); T_EQ(p.payload[4], 19);

  //Responses are dropped by the device parser
  n = feed_all(&p, ex, unhex(EX_PING_RESP, ex, sizeof(ex)), 0, ev, 8);
  T_EQ(n, 0); T_EQ(p.stats.responses_dropped, 1);
}

static void test_split_and_timeout(void)
{
  static pc_parser p;
  uint8_t ex[64];
  int ev[8], n, len;

  pc_parser_init(&p);
  len = unhex(EX_PING, ex, sizeof(ex));

  //Bytes 50 ms apart are fine
  n = feed_all(&p, ex, 10, 0, ev, 8);
  T_EQ(n, 0);
  T_EQ(pc_parser_tick(&p, 50), 0);
  n = feed_all(&p, ex + 10, len - 10, 60, ev, 8);
  T_EQ(n, 1); T_EQ(ev[0], PC_EV_FRAME);

  //100 ms of silence drops a partial frame, the next frame parses normally
  n = feed_all(&p, ex, 18, 1000, ev, 8);
  T_EQ(pc_parser_busy(&p), 1);
  T_EQ(pc_parser_tick(&p, 1099), 0);
  T_EQ(pc_parser_tick(&p, 1100), 1);
  T_EQ(pc_parser_busy(&p), 0);
  n = feed_all(&p, ex, len, 1200, ev, 8);
  T_EQ(n, 1); T_EQ(ev[0], PC_EV_FRAME);

  //Timer wrap
  n = feed_all(&p, ex, 5, 0xFFFFFFF0u, ev, 8);
  T_EQ(pc_parser_tick(&p, 0x00000010u), 0);
  T_EQ(pc_parser_tick(&p, 0x00000060u), 1);
}

static void test_errors(void)
{
  static pc_parser p;
  static uint8_t f[600];
  uint32_t n;
  int ev[8], c;

  pc_parser_init(&p);

  //Bad payload CRC answers with the request's cmd and seq
  f[16] = 1; f[17] = 2; f[18] = 3;
  n = pc_frame_finish(f, PC_CMD_PING, 77, 0, 0, 3);
  f[n - 1] ^= 0xFF;
  c = feed_all(&p, f, n, 0, ev, 8);
  T_EQ(c, 1); T_EQ(ev[0], PC_EV_BAD_CRC); T_EQ(p.hdr.seq, 77);

  //Oversize: header valid, len 493, device does not wait for the payload
  n = pc_frame_finish(f, PC_CMD_PING, 78, 0, 0, 0);
  pc_put_u32(f + 10, 493);
  pc_put_u16(f + 14, pc_crc16(f, 14));
  c = feed_all(&p, f, 16, 0, ev, 8);
  T_EQ(c, 1); T_EQ(ev[0], PC_EV_BAD_LEN); T_EQ(pc_parser_busy(&p), 1);
  //...and then discards exactly len + 4 bytes, even if they look like frames or lines
  memset(f, '#', 497); f[10] = '\n';
  c = feed_all(&p, f, 497, 0, ev, 8);
  T_EQ(c, 0); T_EQ(pc_parser_busy(&p), 0);

  //Version
  n = pc_frame_finish(f, PC_CMD_GET_INFO, 79, 0, 0, 0);
  f[2] = 2; pc_put_u16(f + 14, pc_crc16(f, 14));
  c = feed_all(&p, f, n, 0, ev, 8);
  T_EQ(c, 1); T_EQ(ev[0], PC_EV_BAD_VERSION);

  //Header CRC error then a good frame starting inside the bad header
  n = pc_frame_finish(f + 2, PC_CMD_GET_STATUS, 80, 0, 0, 0);
  f[0] = PC_MAGIC0; f[1] = PC_MAGIC1;
  c = feed_all(&p, f, n + 2, 0, ev, 8);
  T_EQ(c, 1); T_EQ(ev[0], PC_EV_FRAME); T_EQ(p.hdr.seq, 80);

  //Max request
  {
    uint32_t i;
    for(i=0;i<PC_MAX_REQUEST_PAYLOAD;i++) f[16 + i] = (uint8_t)(i * 3);
    n = pc_frame_finish(f, PC_CMD_PING, 81, 0, 0, PC_MAX_REQUEST_PAYLOAD);
    T_EQ(n, 512);
    c = feed_all(&p, f, n, 0, ev, 8);
    T_EQ(c, 1); T_EQ(ev[0], PC_EV_FRAME); T_EQ(p.hdr.len, 492);
    T_MEM(p.payload, f + 16, 492);
  }
}

static void test_ascii(void)
{
  static pc_parser p;
  int ev[8], c;
  char buf[128];

  pc_parser_init(&p);
  c = feed_all(&p, (const uint8_t *)"#VER\n", 5, 0, ev, 8);
  T_EQ(c, 1); T_EQ(ev[0], PC_EV_ASCII); T_STR(p.line, "VER");

  //An ASCII line hidden in the bytes replayed after a header CRC failure
  memcpy(buf, "\xa5\x13#LOCAL\n12345678", 17);
  c = feed_all(&p, (const uint8_t *)buf, 17, 0, ev, 8);
  T_EQ(c, 1); T_EQ(ev[0], PC_EV_ASCII); T_STR(p.line, "LOCAL");

  memset(buf, 'x', sizeof(buf)); buf[0] = '#'; buf[100] = '\r';
  c = feed_all(&p, (const uint8_t *)buf, 101, 0, ev, 8);
  T_EQ(c, 1); T_EQ(ev[0], PC_EV_ASCII_OVERFLOW);
}

static void test_zlp(void)
{
  T_EQ(pc_frame_needs_zlp(PC_FRAME_SIZE(492), 512), 1);   //512
  T_EQ(pc_frame_needs_zlp(PC_FRAME_SIZE(495), 512), 0);   //515
  T_EQ(pc_frame_needs_zlp(PC_FRAME_SIZE(4076), 512), 1);  //4096
  T_EQ(pc_frame_needs_zlp(PC_FRAME_SIZE(1008), 512), 0);  //1028
  T_EQ(pc_frame_needs_zlp(16, 512), 0);
}

int main(void)
{
  test_crc();
  test_encode_examples();
  test_parse_examples();
  test_split_and_timeout();
  test_errors();
  test_ascii();
  test_zlp();
  return t_report("test_protocol");
}
