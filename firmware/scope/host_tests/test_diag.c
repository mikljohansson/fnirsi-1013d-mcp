//----------------------------------------------------------------------------------------------------------------------------------
// test_diag.c - fw 0.2.52 diagnostics readers (protocol row 55): unit life record (pc_life.c), calibration provenance
// (adc_cal.c), image identity (pc_ident.c, incl. the card through the mock SD), GET_HEALTH / GET_CAL / SD_INFO bodies and
// the report (pc_report.c). `test_diag --layouts` prints the packed layouts for check_vectors.py (diag_layouts).
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdlib.h>
#include "test_util.h"
#include "mock_hal.h"
#include "pc_cmds.h"
#include "pc_life.h"
#include "pc_ident.h"
#include "pc_report.h"
#include "adc_cal.h"

static pc_session S;

//----------------------------------------------------------------------------------------------------------------------------------
// Fixed layout inputs (the same values as gen_vectors.py diag_layouts)

static void layout_life(pc_life *l)
{
  memset(l, 0, sizeof(*l));
  l->flags    = PC_LIFE_F_OPEN;
  l->power_on = 12;
  l->minutes  = 2225;
  l->rst.valid      = 1;
  l->rst.type       = 4;
  l->rst.restarts   = 2;
  l->rst.power_on   = 11;
  l->rst.exc_lr     = 0x80012348u;
  l->rst.spsr       = 0x6000001Fu;
  l->rst.exc_sp     = 0x81F00000u;
  l->rst.reset_lr   = 0x80000020u;
  l->rst.reset_sp   = 0x81F80000u;
  l->rst.reset_cpsr = 0x600000D3u;
  l->rst.fsr        = 0x8;
  l->rst.far        = 0xDEADBEEFu;
  l->rst.minutes    = 2100;
}

static void layout_prov(acal_prov *p)
{
  memset(p, 0, sizeof(*p));
  p->build = 0x8614032Au;
  memcpy(p->version, "0.2.52", 6);
  p->fpga      = 0x1535;
  p->what      = 0x0F;
  p->rtc       = 1;
  p->uptime_ms = 420000;
  p->power_on  = 12;
  p->minutes   = 2225;
  p->time[0] = 26; p->time[1] = 10; p->time[2] = 19; p->time[3] = 12; p->time[4] = 34; p->time[5] = 56;
}

static const pc_image_id layout_id = { PC_ID_MAGIC0, PC_ID_MAGIC1, "0.2.52", 0x1234ABCDu, { 26, 10, 19, 12, 34, 56 }, 1, 16 };

static void hexline(const char *name, const uint8_t *p, uint32_t n)
{
  uint32_t i;

  printf("%s ", name);
  for(i=0;i<n;i++)
    printf("%02x", p[i]);
  printf("\n");
}

static int print_layouts(void)
{
  pc_life   l;
  acal_prov p;
  uint16_t  w[PC_LIFE_WORDS];
  uint8_t   b[ACAL_PROV_BYTES > 2 * PC_LIFE_WORDS ? ACAL_PROV_BYTES : 2 * PC_LIFE_WORDS];
  uint32_t  i;

  layout_life(&l);
  pc_life_pack(&l, w);
  for(i=0;i<PC_LIFE_WORDS;i++)
  {
    b[2 * i]     = (uint8_t)w[i];
    b[2 * i + 1] = (uint8_t)(w[i] >> 8);
  }
  hexline("life", b, 2 * PC_LIFE_WORDS);

  layout_prov(&p);
  acal_prov_pack(&p, b);
  hexline("prov", b, ACAL_PROV_BYTES);

  hexline("ident", (const uint8_t *)&layout_id, PC_ID_BYTES);
  return 0;
}

//----------------------------------------------------------------------------------------------------------------------------------

static void test_life(void)
{
  pc_life  l, u;
  uint16_t w[PC_LIFE_WORDS], s[256];
  uint32_t rv[10] = { 1, 4, 0x80001008u, 0x13, 0x81000000u, 0x8000002Cu, 0x81100000u, 0x600000D3u, 0x805, 0x10 };
  int      end;

  //Round trip
  layout_life(&l);
  pc_life_pack(&l, w);
  T_EQ(w[0], PC_LIFE_MARKER);
  T_EQ(pc_life_unpack(w, &u), 1);
  T_EQ(u.power_on, 12); T_EQ(u.minutes, 2225); T_EQ(u.flags, PC_LIFE_F_OPEN);
  T_EQ(u.rst.valid, 1); T_EQ(u.rst.type, 4); T_EQ(u.rst.restarts, 2); T_EQ(u.rst.power_on, 11);
  T_EQ(u.rst.exc_lr, 0x80012348u); T_EQ(u.rst.far, 0xDEADBEEFu); T_EQ(u.rst.minutes, 2100);
  T_EQ(u.rst.reset_cpsr, 0x600000D3u); T_EQ(u.rst.fsr, 8);

  //No marker: nothing
  w[0] = 0;
  T_EQ(pc_life_unpack(w, &u), 0);
  T_EQ(u.power_on, 0); T_EQ(u.rst.valid, 0);

  //First boot with a valid settings sector and no record: power-on 1, end unknown
  memset(&l, 0, sizeof(l));
  end = pc_life_boot(&l, 0, 1, NULL);
  T_EQ(end, PC_LIFE_END_UNKNOWN); T_EQ(l.power_on, 1); T_EQ(l.flags & PC_LIFE_F_OPEN, PC_LIFE_F_OPEN);

  //Power-off save clears the open flag; the next boot reports a clean end and counts on
  l.flags &= (uint16_t)~PC_LIFE_F_OPEN;
  l.minutes = 30;
  pc_life_pack(&l, w);
  T_EQ(pc_life_unpack(w, &l), 1);
  end = pc_life_boot(&l, 1, 1, NULL);
  T_EQ(end, PC_LIFE_END_SAVED); T_EQ(l.power_on, 2); T_EQ(l.minutes, 30); T_EQ(l.rst.valid, 0);

  //No power-off save (open flag still set): an unsaved end
  pc_life_pack(&l, w);
  pc_life_unpack(w, &l);
  end = pc_life_boot(&l, 1, 1, NULL);
  T_EQ(end, PC_LIFE_END_UNSAVED); T_EQ(l.power_on, 3);

  //Warm restart: same power-on, restart stored with it
  pc_life_pack(&l, w);
  pc_life_unpack(w, &l);
  end = pc_life_boot(&l, 1, 1, rv);
  T_EQ(end, PC_LIFE_END_UNSAVED); T_EQ(l.power_on, 3);
  T_EQ(l.rst.valid, 1); T_EQ(l.rst.power_on, 3); T_EQ(l.rst.type, 4); T_EQ(l.rst.exc_lr, 0x80001008u);
  T_EQ(l.rst.reset_lr, 0x8000002Cu); T_EQ(l.rst.fsr, 0x805); T_EQ(l.rst.far, 0x10); T_EQ(l.rst.minutes, 30);

  //The record survives the next cold boots until cleared
  l.flags = 0;
  pc_life_pack(&l, w);
  pc_life_unpack(w, &l);
  pc_life_boot(&l, 1, 1, NULL);
  T_EQ(l.power_on, 4); T_EQ(l.rst.valid, 1); T_EQ(l.rst.power_on, 3);

  //Restart count capped
  rv[0] = 70000;
  pc_life_set_restart(&l, rv, 5);
  T_EQ(l.rst.restarts, 0xFFFF);

  //Invalid settings sector: unknown count
  pc_life_boot(&l, 0, 0, NULL);
  T_EQ(l.power_on, 0);

  //1.00o5 checksum of words 2..255 in words 0-1
  memset(s, 0, sizeof(s));
  s[2] = 0x0100; s[3] = 0x0015; s[100] = 0xFFFF; s[255] = 7;
  pc_life_settings_seal(s);
  T_EQ(pc_life_settings_valid(s, 0x0100, 0x0015), 1);
  T_EQ(pc_life_settings_valid(s, 0x0100, 0x0016), 0);
  s[230] ^= 1;
  T_EQ(pc_life_settings_valid(s, 0x0100, 0x0015), 0);
}

static void test_prov(void)
{
  acal_prov    p, q;
  acal_channel a[2], b[2];
  uint8_t      raw[ACAL_PROV_BYTES];

  T_EQ(ACAL_PROV_OFFSET, 432);
  T_EQ(ACAL_PROV_OFFSET + ACAL_PROV_BYTES <= 512, 1);

  layout_prov(&p);
  acal_prov_pack(&p, raw);
  T_EQ(acal_prov_unpack(raw, &q), ACAL_PROV_VALID);
  T_EQ(q.build, 0x8614032Au); T_STR(q.version, "0.2.52"); T_EQ(q.fpga, 0x1535); T_EQ(q.what, 0x0F); T_EQ(q.rtc, 1);
  T_EQ(q.uptime_ms, 420000); T_EQ(q.power_on, 12); T_EQ(q.minutes, 2225); T_EQ(q.time[5], 56);

  raw[20] ^= 1;
  T_EQ(acal_prov_unpack(raw, &q), ACAL_PROV_BAD);
  T_EQ(q.build, 0);
  memset(raw, 0, sizeof(raw));
  T_EQ(acal_prov_unpack(raw, &q), ACAL_PROV_NONE);

  //What a calibration wrote
  memset(a, 0, sizeof(a));
  memcpy(b, a, sizeof(a));
  T_EQ(acal_prov_what(a, b), 0);
  b[0].zero[1][0][3] = 5;
  T_EQ(acal_prov_what(a, b), ACAL_PROV_CH1_ZERO);
  b[1].gain[0][1] = 33000;
  T_EQ(acal_prov_what(a, b), ACAL_PROV_CH1_ZERO | ACAL_PROV_CH2_GAIN);
  b[1].pwm[0] = 900;
  b[0].gain_lever = 1400;
  T_EQ(acal_prov_what(a, b), 0x0F);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Identity

//The tail of fw-0.2.50_e54d29df.bin (backups/, GCC 10): time 26-10-19 09:30:36, pad, build 0xcdc7e2f3, then padding
static const char tail_0250[] = "9cbb0580 60da0580 38de0580 40de0580 50de0580 5cde0580 33322021 34363522 23370000"
                                " 1a0a1309 1e240000 f3e2c7cd";

static void test_ident_pure(void)
{
  uint8_t     t[1024];
  pc_image_id r;
  uint32_t    build, i, n;
  uint8_t     time[6];
  pc_ident_scan sc;
  static const char ver[] = "junk FNIRSI-FNIRSI-1013D-AI,fw=0.2.25,proto=1.5,fpga=0x more";

  //Record at the tail, followed by padding
  memset(t, 0xEE, sizeof(t));
  memcpy(t + 1024 - 300 - PC_ID_BYTES, &layout_id, PC_ID_BYTES);
  memset(t + 1024 - 300, 0, 300);
  T_EQ(pc_ident_find_record(t, 1024, &r), 1);
  T_STR(r.version, "0.2.52"); T_EQ(r.build, 0x1234ABCDu); T_EQ(r.time[5], 56); T_EQ(r.proto_minor, 16);
  t[1024 - 300 - PC_ID_BYTES + 4] ^= 1;
  T_EQ(pc_ident_find_record(t, 1024, &r), 0);

  //Legacy tail of 0.2.50
  memset(t, 0, sizeof(t));
  n = (uint32_t)unhex(tail_0250, t + 200, 100);
  T_EQ(pc_ident_legacy_tail(t, 1024, &build, time), 1);
  T_EQ(build, 0xCDC7E2F3u); T_EQ(time[0], 26); T_EQ(time[1], 10); T_EQ(time[2], 19); T_EQ(time[3], 9); T_EQ(time[5], 36);
  //A hash whose top bytes are 0 still ends the image (the end rounds up to 4)
  t[200 + n - 1] = 0;
  T_EQ(pc_ident_legacy_tail(t, 1024, &build, time), 1);
  T_EQ(build, 0x00C7E2F3u);
  //An invalid date is not a build tail
  t[200 + n - 11] = 13;
  T_EQ(pc_ident_legacy_tail(t, 1024, &build, time), 0);
  memset(t, 0, sizeof(t));
  T_EQ(pc_ident_legacy_tail(t, 1024, &build, time), 0);

  T_EQ(pc_ident_version_at_least("0.2.47", 0, 2, 47), 1);
  T_EQ(pc_ident_version_at_least("0.2.50-nommu", 0, 2, 47), 1);
  T_EQ(pc_ident_version_at_least("0.2.25", 0, 2, 47), 0);
  T_EQ(pc_ident_version_at_least("0.3.0", 0, 2, 47), 1);
  T_EQ(pc_ident_version_at_least("x", 0, 2, 47), 0);

  //Version text at every split point between two chunks
  for(i=0;i<sizeof(ver);i++)
  {
    pc_ident_scan_init(&sc);
    pc_ident_scan_feed(&sc, (const uint8_t *)ver, i);
    pc_ident_scan_feed(&sc, (const uint8_t *)ver + i, sizeof(ver) - 1 - i);
    T_STR(sc.version, "0.2.25"); T_EQ(sc.major, 1); T_EQ(sc.minor, 5); T_EQ(sc.done, 1);
  }

  //"fw=" without a version is skipped; a later complete one is found
  pc_ident_scan_init(&sc);
  pc_ident_scan_feed(&sc, (const uint8_t *)"FNIRSI-1013D-AI,fw=,x FNIRSI-1013D-AI,fw=0.2.48-m1,proto=1.13,", 63);
  T_STR(sc.version, "0.2.48-m1"); T_EQ(sc.minor, 13);
}

//Writes an eGON.EXE image of `sectors` at 750: content, then the tail bytes ending at `end` (bytes), zero padding
static void card_image(uint32_t sectors, const char *text, uint32_t text_at, const uint8_t *tail, uint32_t tail_len,
                       uint32_t end)
{
  uint8_t *p = mock.sd + PC_ID_SECTOR * 512;
  uint32_t i;

  memset(p, 0, sectors * 512 + 4096);

  for(i=32;i<end;i++)
    p[i] = (uint8_t)(0x40 + i % 23);

  memcpy(p + 4, "eGON.EXE", 8);
  pc_put_u32(p + 16, sectors * 512);

  if(text)
    memcpy(p + text_at, text, strlen(text) + 1);

  if(tail)
    memcpy(p + end - tail_len, tail, tail_len);

  pc_ident_invalidate();
}

static void test_ident_card(void)
{
  static uint8_t scratch[65536];
  const pc_card_id *c;
  uint8_t tail[100];
  uint32_t n;

  mock_reset(0x1535);

  //Record image identical to the running one
  card_image(700, NULL, 0, (const uint8_t *)&pc_image_ident, PC_ID_BYTES, 700 * 512 - 128);
  c = pc_ident_card(0, scratch, sizeof(scratch));
  T_EQ(c->state, PC_ID_NOT_READ);
  c = pc_ident_card(1, scratch, sizeof(scratch));
  T_EQ(c->state, PC_ID_RECORD); T_EQ(c->same, 1); T_STR(c->version, "0.0.0-host"); T_EQ(c->proto_minor, 16);
  T_EQ(c->length, 700 * 512); T_TRUE(c->read_ms >= 1);
  c = pc_ident_card(1, scratch, sizeof(scratch));
  T_EQ(c->state, PC_ID_RECORD); T_EQ(c->read_ms, 0);                     //cached

  //A different record: not the running image
  card_image(700, NULL, 0, (const uint8_t *)&layout_id, PC_ID_BYTES, 700 * 512 - 4);
  c = pc_ident_card(1, scratch, sizeof(scratch));
  T_EQ(c->state, PC_ID_RECORD); T_EQ(c->same, 0); T_EQ(c->build, 0x1234ABCDu); T_STR(c->version, "0.2.52");

  //0.2.50: build tail + version text (found across the 64-sector chunks of a small scratch)
  n = (uint32_t)unhex(tail_0250, tail, sizeof(tail));
  card_image(986, "#OK,FNIRSI-1013D-AI,fw=0.2.50,proto=1.15,fpga=0x", 381980, tail, n, 504076);
  c = pc_ident_card(1, scratch, 32768);
  T_EQ(c->state, PC_ID_TAIL); T_STR(c->version, "0.2.50"); T_EQ(c->build, 0xCDC7E2F3u); T_EQ(c->time[4], 30);
  T_EQ(c->proto_major, 1); T_EQ(c->proto_minor, 15); T_EQ(c->same, 0);

  //0.2.25: version text only; a date-like tail does not count before 0.2.47
  card_image(776, "#OK,FNIRSI-1013D-AI,fw=0.2.25,proto=1.5,fpga=0x", 295672, tail, n, 397000);
  c = pc_ident_card(1, scratch, sizeof(scratch));
  T_EQ(c->state, PC_ID_VERSION_ONLY); T_STR(c->version, "0.2.25"); T_EQ(c->build, 0); T_EQ(c->time[0], 0);
  T_EQ(c->proto_minor, 5);

  //No identity at all
  card_image(100, NULL, 0, NULL, 0, 50000);
  c = pc_ident_card(1, scratch, sizeof(scratch));
  T_EQ(c->state, PC_ID_UNKNOWN); T_EQ(c->version[0], 0);

  //No image / implausible length / no card
  mock.sd[PC_ID_SECTOR * 512 + 4] = 'x';
  pc_ident_invalidate();
  T_EQ(pc_ident_card(1, scratch, sizeof(scratch))->state, PC_ID_NO_IMAGE);
  card_image(3000, NULL, 0, NULL, 0, 1000);
  T_EQ(pc_ident_card(1, scratch, sizeof(scratch))->state, PC_ID_NO_IMAGE);
  pc_ident_invalidate();
  mock.sd_sectors = 0;
  T_EQ(pc_ident_card(1, scratch, sizeof(scratch))->state, PC_ID_NO_IMAGE);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Bodies and the report

static void test_bodies(void)
{
  uint8_t   h[PC_HEALTH_LEN], cal[PC_CAL_RESP_LEN], sd[PC_SD_INFO_LEN];
  static uint8_t scratch[65536];
  acal_prov p;

  mock_reset(0x1535);
  pc_session_init(&S);

  {
    pc_life l;
    layout_life(&l);
    mock.life.rst = l.rst;
  }
  mock.life.settings_flags = PC_SETF_DIRTY;
  mock.life.saves = 2;
  mock.life.last_save = -3;
  mock.life.dirty_age_ms = 400;
  mock.now = 123456;

  pc_health_body(h);
  T_EQ(h[0], 1); T_EQ(pc_get_u16(h + 2), PC_HEALTH_LEN);
  T_EQ(h[4], PC_USB_CONFIGURED); T_EQ(h[5], PC_USBF_HIGH_SPEED | PC_USBF_CONFIGURED | PC_USBF_DTR | PC_USBF_SOFTCONN);
  T_EQ(h[6], 0x70); T_EQ(h[7], 5); T_EQ(pc_get_u32(h + 8), 0); T_EQ(pc_get_u32(h + 12), 4000);
  T_EQ(pc_get_u32(h + 16), 0x0005107Du);
  T_EQ(pc_get_u32(h + 20), 12); T_EQ(pc_get_u32(h + 24), 2225); T_EQ(pc_get_u32(h + 28), 123456);
  T_EQ(h[32], PC_LIFEF_FOUND | PC_LIFEF_BOOT_WRITE | PC_LIFEF_VALID); T_EQ(h[33], PC_LIFE_END_SAVED);
  T_EQ(h[34], PC_SETF_DIRTY); T_EQ(h[35], 2); T_EQ((int32_t)pc_get_u32(h + 36), -3); T_EQ(pc_get_u32(h + 40), 400);
  T_EQ(h[44], 1);                                           //stored, from power-on 11, not this one (12)
  T_EQ(h[45], 4); T_EQ(pc_get_u16(h + 46), 2); T_EQ(pc_get_u32(h + 48), 11); T_EQ(pc_get_u32(h + 52), 0x80012348u);
  T_EQ(pc_get_u32(h + 56), 0x6000001Fu); T_EQ(pc_get_u32(h + 60), 0x81F00000u); T_EQ(pc_get_u32(h + 64), 0x80000020u);
  T_EQ(pc_get_u32(h + 68), 0x81F80000u); T_EQ(pc_get_u32(h + 72), 0x600000D3u); T_EQ(pc_get_u32(h + 76), 8);
  T_EQ(pc_get_u32(h + 80), 0xDEADBEEFu); T_EQ(pc_get_u32(h + 84), 2100); T_EQ(h[95], 0);
  mock.life.rst.power_on = 12;
  pc_health_body(h);
  T_EQ(h[44], 3);
  mock.life.power_on = 0;                                   //unknown count: never "this power-on"
  pc_health_body(h);
  T_EQ(h[44], 1);
  mock.life.power_on = 12;
  mock.life.rst.valid = 0;
  pc_health_body(h);
  T_EQ(h[44], 0); T_EQ(pc_get_u32(h + 52), 0);

  //GET_CAL provenance tail
  pc_cal_body(0, cal);
  T_EQ(cal[288], ACAL_PROV_NONE); T_EQ(pc_get_u32(cal + 348), 0xFFFFFFFFu); T_EQ(pc_get_u32(cal + 352), 0xFFFFFFFFu);
  layout_prov(&p);
  p.power_on = 10;
  p.minutes  = 2000;
  acal_prov_pack(&p, mock.prov_raw);
  mock.prov_state = ACAL_PROV_VALID;
  pc_cal_body(1, cal);
  T_EQ(cal[0], 1); T_EQ(cal[288], ACAL_PROV_VALID); T_MEM(cal + 292, mock.prov_raw, ACAL_PROV_BYTES);
  T_EQ(pc_get_u32(cal + 348), 2); T_EQ(pc_get_u32(cal + 352), 225);
  mock.life.power_on = 0;
  pc_cal_body(1, cal);
  T_EQ(pc_get_u32(cal + 348), 0xFFFFFFFFu);
  mock.life.power_on = 12;

  //SD_INFO identity part
  T_EQ(pc_sd_info_body(&S, 0, 0, sd, scratch, sizeof(scratch)), 0);
  T_EQ(sd[64], PC_ID_NOT_READ);
  T_EQ(pc_sd_info_body(&S, 0, 1, sd, scratch, sizeof(scratch)), 0);
  T_EQ(sd[64], PC_ID_UNKNOWN);                              //mock image: pattern bytes only
  T_EQ(pc_get_u32(sd + 96), 750 * 512);
  card_image(700, NULL, 0, (const uint8_t *)&pc_image_ident, PC_ID_BYTES, 700 * 512 - 64);
  T_EQ(pc_sd_info_body(&S, 0, 1, sd, scratch, sizeof(scratch)), 0);
  T_EQ(sd[64], PC_ID_RECORD); T_EQ(sd[65], 1); T_EQ(sd[66], 1); T_EQ(sd[67], 16);
  T_EQ(memcmp(sd + 68, "0.0.0-host", 11), 0); T_EQ(pc_get_u32(sd + 84), 0); T_EQ(sd[88], 26); T_EQ(sd[93], 0);
  T_TRUE(pc_get_u32(sd + 100) >= 1);
  T_EQ(pc_sd_info_body(&S, 0, 0, sd, scratch, sizeof(scratch)), 0);
  T_EQ(sd[64], PC_ID_RECORD); T_EQ(pc_get_u32(sd + 100), 0);
  mock.sd_sectors = 0;
  T_EQ(pc_sd_info_body(&S, 0, 1, sd, scratch, sizeof(scratch)), 1);
}

static int has(const char *s)
{
  int ok = (mock.report && strstr(mock.report, s));

  t_checks++;
  if(!ok)
  {
    t_failures++;
    printf("FAIL: report lacks \"%s\"\n", s);
  }
  return ok;
}

static void test_report(void)
{
  static uint8_t ws[PC_MAX_RESPONSE_PAYLOAD];
  char     name[48];
  uint32_t written;
  pc_life  l;
  acal_prov p;

  mock_reset(0x1535);
  pc_session_init(&S);
  mock.now = 754321;
  layout_life(&l);
  mock.life.rst = l.rst;
  layout_prov(&p);
  acal_prov_pack(&p, mock.prov_raw);
  mock.prov_state = ACAL_PROV_VALID;

  T_EQ(pc_save_report(&S, ws, sizeof(ws) - 56, &written, name, sizeof(name)), 0);
  T_STR(name, "reports/report-00012-00.txt");
  T_EQ(written, mock.report_len); T_TRUE(written > 3000); T_TRUE(written < 40000);
  T_EQ(strlen(mock.report), written);

  has("FNIRSI-1013D-AI diagnostics report\r\n");
  has("Firmware running: " PC_FW_VERSION ", protocol 1.17, git 0x00000000 of 2026-10-19 12:00:00 UTC");
  has("Card image (sector 750): unknown");
  has("FPGA: version word 0x1535, variant 2");
  has("USB serial: 1013D-0001");
  has("CPU: CP15 control 0x0005107d: MMU on, D-cache on, I-cache on");
  has("uptime: 0 h 12 min 34 s (754321 ms)");
  has("power-on: #12, operating time 37 h 5 min");
  has("previous power-on ended: settings saved at power-off");
  has("state: configured, high speed, port open");
  has("  this power-on: ");
  has("stored record: an earlier power-on (#11, at 35 h 0 min operating time), 2 restart(s), last: data abort at PC "
      "0x80012340 (LR 0x80012348, SPSR 0x6000001f, SP 0x81f00000), fault status 0x008, fault address 0xdeadbeef");
  has("Provenance: calibrated by fw 0.2.52 (git 0x8614032a) on FPGA 0x1535, 7 min after power-on #12, 2026-10-19 12:34:56; "
      "wrote CH1 zeros, CH1 gains, CH2 zeros, CH2 gains; since then 0 power-ons, 0 h 0 min operating time");
  has("  CH1: no v3 calibration");
  has("Link counters (GET_LINK_STATS, n = 62)");
  has("  49 restarts = ");
  has("  61 usb_soft_reconnects = ");
  has("sector 750: eGON.EXE, length 384000, checksum not checked");
  has("Event log (last ");
  has("Bus-level events (last ");
  T_TRUE(strstr(mock.report, "[report truncated]") == NULL);

  //A card write error comes back as the FRESULT
  mock.report_fail = 7;
  T_EQ(pc_save_report(&S, ws, sizeof(ws) - 56, &written, name, sizeof(name)), 7);
  T_EQ(written, 0);

  //Text builder: truncation, fixed point
  {
    char    b[16];
    pc_text t;

    pc_text_init(&t, b, sizeof(b));
    pc_text_str(&t, "0123456789abcdefXYZ");
    T_EQ(t.len, 15); T_EQ(t.full, 1); T_EQ(b[15], 0);
    pc_text_init(&t, b, sizeof(b));
    pc_text_fix(&t, -40, 16, 2);
    T_STR(b, "-2.50");
    pc_text_init(&t, b, sizeof(b));
    pc_text_fix(&t, 65536 + 33, 65536, 5);
    T_STR(b, "1.00050");
    pc_text_init(&t, b, sizeof(b));
    pc_text_fix(&t, -1, 256, 1);
    T_STR(b, "0.0");
    pc_text_init(&t, b, sizeof(b));
    pc_text_dec(&t, -2147483647 - 1);
    T_STR(b, "-2147483648");
  }
}

int main(int argc, char **argv)
{
  if((argc > 1) && !strcmp(argv[1], "--layouts"))
    return print_layouts();

  //--image FILE...: identify real images as the card reader would (bench aid: backups/fw-*.bin)
  if((argc > 2) && !strcmp(argv[1], "--image"))
  {
    static uint8_t scratch[65536];
    int k;

    for(k=2;k<argc;k++)
    {
      FILE *f = fopen(argv[k], "rb");
      const pc_card_id *c;
      size_t n;

      mock_reset(0x1535);
      if(!f)
        return 1;
      n = fread(mock.sd + PC_ID_SECTOR * 512, 1, (MOCK_SD_SECTORS - PC_ID_SECTOR) * 512, f);
      fclose(f);
      (void)n;
      c = pc_ident_card(1, scratch, sizeof(scratch));
      printf("%s: state %u version %s proto %u.%u build %08x time %02u-%02u-%02u %02u:%02u:%02u length %u\n", argv[k],
             c->state, c->version, c->proto_major, c->proto_minor, c->build, c->time[0], c->time[1], c->time[2],
             c->time[3], c->time[4], c->time[5], c->length);
    }
    return 0;
  }

  test_life();
  test_prov();
  test_ident_pure();
  test_ident_card();
  test_bodies();
  test_report();
  return t_report("test_diag");
}
