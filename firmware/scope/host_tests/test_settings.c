//----------------------------------------------------------------------------------------------------------------------------------
// test_settings.c - the stored settings record (sector 709) checked before it is applied (settings_check.c, fw-settings0),
// against the maintainer's card: the record found on 2026-11-04 (checksum fine, written by the power-off interrupt while
// the boot was still loading the settings: PWM centres, compensations, generator, memory depth 0; firmware/AGENTS.md
// section 93), the records of 2026-10-18..22 (must load as before) and made-up damage. Also the 11-04 calibration
// sector: its v3 block is valid and binds to the centres it stores (acal_centres), not to the 11-04 record's zeros.
//----------------------------------------------------------------------------------------------------------------------------------

#include "test_util.h"
#include "settings_check.h"
#include "adc_cal.h"

#define VER_HI 0x0100      //variables.h SETTING_SECTOR_VERSION_HIGH / _LOW
#define VER_LO 0x0015

//The sector 709 records (512 bytes each) and the 11-04 sector 708, from the SD backups in backups/ (gitignored)
//sd_700-710_pre-encphase_a.bin
static const char rec_1104[] =
  "090056f000011500000000000000000001000000010000000200020000008d008c00000000000000000000000000000000000000000000000000000000000000"
  "01000000010000000200020000007e00640000000000000000000000000000000000000000000000000000000000000014000600000000000000800000000000"
  "8c000000000000000000000000000000000000000000000000000000000000000000000000006400190000000000000000000000010094000000000000000000"
  "0000000000000000000000000100000001000000000001000000000000000000000000000000000000000000000000000100b70023020000a700480101000000"
  "00000000000000000000000000000000000000000000000000000000000000000000000001000000000000000200000000000000000000000000000000000000"
  "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
  "000000000000000000000000000000000000000038ac020000000000000001de535e0010019a01c723d514e03200000000b203ca00004ab301b101a411a0d1a0"
  "fe110100020000000600000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

//sd_700-710_20261022-pre-gate.bin
static const char rec_1022_gate[] =
  "0e00101000011500000000000000000001000000010000000200020000008d00ad00000000000000000000000000000000000000000000000000000000000000"
  "01000000010000000200020000007e00640000000000000000000000000000000000000000000000000000000000000014000600000000000000920000005d01"
  "ad000000000000000000000000000000000000000000000000000000000000000000000000006400190000000000000000000000010094000000000000000000"
  "0000000000000000000000000100000001000000000001000000000000000000000000000000000000000000000000000100b70023020000a700480101000000"
  "00000000000000000000000000000000000000000000000000000000000000000000000001000000000000000200000000000000000000000000000000000000"
  "00000000000000000000000000000000680363036c0366036c0366035e03590361035d0361035d030000000000000000fdff0300ffff01000000000000000000"
  "010000000000e80332000000000000007017b80b71ae020000000000000003de645e1410019a03c723d514e03200000000b203ca000049b301b101a411a0d1a0"
  "fe11010053000000540200000100020000000000530000000000000000000000000000002008ffff085e00005300006000000000000000005402000000000000";

//sd_700-710_20261022-pre-capwin.bin
static const char rec_1022_capwin[] =
  "0e007e500001150000000000000000000100000001000000020002000000ca00ea00000000000000000000000000000000000000000000000000000000000000"
  "01000000010000000200020000007e00640000000000000000000000000000000000000000000000000000000000000014000600000000000000920000007601"
  "ea000000000000000000000000000000000000000000000000000000000000000000000000006400190000000000000000000000010094000000000000000000"
  "0000000000000000000000000100000001000000000001000000000000000000000000000000000000000000000000000000b70023020000a700480101000000"
  "00000000000000000000000000000000000000000000000000000000000000000000000001000000000000000200000000000000000000000000000000000000"
  "00000000000000000000000000000000680362036b0365036b0365035e03590361035c0361035c030000000000000000fdff0300ffff01000000000000000000"
  "010000000000e80332000000000000007017b80b71ae020000000000000000de645e1410019a03c723d514e03200000000b203ca000049b301b101a411a0d1a0"
  "fe1101004f0000000f0200000100ffff000000004a000000000000000000000000000000142a0000d87b0000d300006000000000000000000d02000000000000";

//sd_700-710_20261022-pre-capstage.bin
static const char rec_1022_capstage[] =
  "0e00c24c0001150000000000000000000100000001000000010001000000ca00e300000000000000000000000000000000000000000000000000000000000000"
  "01000000010000000100010000007e00640000000000000000000000000000000000000000000000000000000000000010000b000000000000008e0000007601"
  "e3000000000000000000000000000000000000000000000000000000000000000000000000006400190000000000000000000000010094000000000000000000"
  "0000000000000000000000000100000001000000000001000000000000000000000000000000000000000000000000000000b70023020000a700480101000000"
  "00000000000000000000000000000000000000000000000000000000000000000000000001000000000000000200000000000000000000000000000000000000"
  "000000000000000000000000000000006503600369036303680362035b0357035e035a035e035a030000000000000000fdff0300ffff02000000000000000000"
  "010000000000640032000000000000007017b80b71ae020000000000000000de645e1410019a03c723d514e03200000000b203ca000049b301b101a411a0d1a0"
  "fe1101004b0000000d0200000100ffff000000004a000000000000000000000000000000142a0000d87b0000d300006000000000000000000d02000000000000";

//sd_700-710_20261019-0812_pre-calv3.bin
static const char rec_1019_calv3[] =
  "0200d6a90001150000000000000000000100000001000000020002000000f9000201000000000000000000000000000000000000000000000000000000000000"
  "01000000010000000600060000006100640000000000000000000000000000000000000000000000000000000000000012000a00000000000000eb0000007102"
  "02010000000000000000000000000000000000000000000000000000000000000000000000001400190000000000000000000000010001000000000000000000"
  "0000000000000000000000000100000001000000000001000000000000000000000000000000000000000000000000000000b70023020000a700480101000000"
  "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
  "0000000000000000000000000000000063035f0365035f0365036003580354035c0357035c0357030000000000000000fdff0300000001000000000000000000"
  "010001000000e8033200d00c5e010000b80bdc050000e80300000000000000de535e001000000000000000000000000000000000000000000000000000000000"
  "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

//sd_700-710_20261018-224105_pre-calv2.bin
static const char rec_1018_calv2[] =
  "0200cc3b000115000000000000000000010000000100000002000200000018012901280000000000000000000000000000000000000000000000000000000000"
  "0100000001000000060006000000610064000600000000000000000000000000000000000000000000000000000000001500070000000000000080000000bd02"
  "29010000000000000000000000000000000000000000000000000000000000000000000000001400190000000000000000000000010001000000000000000000"
  "0000000000000000000000000100000001000000000001000000000000000000000000000000000000000000000000000000b70023020000a700480101000000"
  "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
  "000000000000000000000000000000005c03580360035b0360035b035703510359035403590354030000000000000000feff0200000000000000000000000000"
  "010000000000e8033200d00c5e010000b80bdc050000e80300000000000000de0000000000000000000000000000000000000000000000000000000000000000"
  "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

//sd_700-710_pre-encphase_a.bin
static const char cal_1104[] =
  "060045c9000000000000000000000000beee3500a38b3600242c370060d03700a38b3600242c370048586e00000000005655350056553500242c370060d03700"
  "beee3500a38b360046176d0000000000ae000000dc0000008101000000000000ae000000dc000000810100000000000000000000000000000000000000000000"
  "4143414c030030010200000063035e03660361036603600325082b082408290825082f08fd07f707fd07f507ff07fa07200820082008200821082108e007df07"
  "df07df07e007e00722beb5bd4bbe14be4bbe07bed4beefbe26bf18bf33bfefbeb5bd9abd14bedebdf9bdc3bd9abd71bdb5bd7ebddebdd0bd0c090c080c090f0d"
  "0f0b0e0c030303030404020403040202e080e582dd7f178002007a055a0355035d0359035d035903080802080a08ff070808fe0714081d0816081a0817081c08"
  "e307e507e507e207e407e2071208160812081208160813087ebd55bda7bdf9bdb5bd8cbdf9bd8cbd14be7ebd30bec3bd55bd1fbd71bd2cbd8cbd55bd6dbc6dbc"
  "89bc89bca4bc7bbc11040c04090711050b080909060707050605050705050706848178811981e07e02008205504e6e864150525601003800f7bbc207302e322e"
  "3130350000000000000000003d150f00d0230100540000006502000000000000000000002e50a6494143415001001000db063d158c8302a1abfb00008f80a4d5";


static void load(const char *hex, uint16_t *w)
{
  uint8_t b[512];
  int     i;

  T_EQ(unhex(hex, b, sizeof(b)), 512);

  for(i=0;i<256;i++)
    w[i] = (uint16_t)(b[2 * i] | (b[2 * i + 1] << 8));
}

static void seal(uint16_t *w)
{
  uint32_t sum = 0, i;

  for(i=2;i<256;i++)
    sum += w[i];

  w[0] = (uint16_t)(sum >> 16);
  w[1] = (uint16_t)sum;
}

static int check(const uint16_t *w, uint16_t *bad)
{
  return srec_check(w, VER_HI, VER_LO, bad);
}

//The 11-04 record: checksum and version fine, rejected for its values; the first failing check is the CH1 range 0 centre
static void test_1104(void)
{
  uint16_t w[256], bad = 0xFFFF;
  int      i;

  load(rec_1104, w);
  T_EQ(srec_check_header(w, VER_HI, VER_LO), SREC_OK);
  T_EQ(check(w, &bad), SREC_BAD_FIELD);
  T_EQ(bad, 168);

  //Each of its zeroed groups alone is enough: with the centres back (the compensations are 0, legal) the memory depth 0
  for(i=0;i<12;i++)
    w[168 + i] = 860;
  seal(w);
  T_EQ(check(w, &bad), SREC_BAD_FIELD);
  T_EQ(bad, 200);

  //With a memory depth too it would load: nothing else in it is out of range (the zero generator frequency is legal)
  w[200] = 3000;
  w[201] = 1500;
  seal(w);
  T_EQ(check(w, &bad), SREC_OK);
  T_EQ(bad, 0);
}

//The earlier records load unchanged (the check reads, it never writes)
static void test_good(void)
{
  static const char *const recs[] = { rec_1022_gate, rec_1022_capwin, rec_1022_capstage, rec_1019_calv3, rec_1018_calv2 };
  uint16_t w[256], copy[256], bad = 0xFFFF;
  uint32_t i;

  for(i=0;i<sizeof(recs)/sizeof(recs[0]);i++)
  {
    load(recs[i], w);
    memcpy(copy, w, sizeof(w));
    T_EQ(check(w, &bad), SREC_OK);
    T_EQ(bad, 0);
    T_MEM(w, copy, sizeof(w));
  }
}

//A zeroed body under a valid header: the checksum of words 2..255 is just the version, so the header check passes and
//the fields reject it (first: time/div index 0 is a roll time base without roll mode)
static void test_zero_body(void)
{
  uint16_t w[256] = { 0 }, bad = 0;

  w[2] = VER_HI;
  w[3] = VER_LO;
  seal(w);
  T_EQ(w[0], 0);
  T_EQ(w[1], VER_HI + VER_LO);
  T_EQ(srec_check_header(w, VER_HI, VER_LO), SREC_OK);
  T_EQ(check(w, &bad), SREC_BAD_FIELD);
  T_EQ(bad, 56);

  //An all-zero sector (a fresh card) is another version, not damage
  memset(w, 0, sizeof(w));
  T_EQ(check(w, &bad), SREC_BAD_VERSION);
  T_EQ(bad, 0);
}

static void test_damage(void)
{
  uint16_t w[256], bad = 0;
  int      i;

  //Truncated: the tail of a good record lost (zeros from word 128 on): the checksum fails
  load(rec_1022_gate, w);
  for(i=128;i<256;i++)
    w[i] = 0;
  T_EQ(check(w, &bad), SREC_BAD_SUM);
  T_EQ(bad, 0);

  //Wrong version, either word (another firmware's record), even with its checksum right
  load(rec_1022_gate, w);
  w[3] = 0x0014;
  seal(w);
  T_EQ(check(w, &bad), SREC_BAD_VERSION);
  load(rec_1022_gate, w);
  w[2] = 0x0200;
  seal(w);
  T_EQ(check(w, &bad), SREC_BAD_VERSION);

  //Bad checksum: one bit flipped in the body, or in the checksum itself
  load(rec_1022_gate, w);
  w[56] ^= 0x0001;
  T_EQ(check(w, &bad), SREC_BAD_SUM);
  load(rec_1022_gate, w);
  w[1] ^= 0x8000;
  T_EQ(check(w, &bad), SREC_BAD_SUM);
}

//One field out of range at a time, checksum resealed: each is rejected at its word; the edge values load
static void test_fields(void)
{
  static const struct { uint16_t word, ok, bad; } cases[] =
  {
    { 8, 1, 2 },              //CH1 enable
    { 9, 1, 2 },              //CH1 coupling
    { 10, 6, 7 },             //CH1 probe
    { 12, 6, 7 },             //CH1 display V/div
    { 13, 6, 7 },             //CH1 sample V/div
    { 18, 1, 2 },             //CH1 invert
    { 37, 6, 7 },             //CH2 sample V/div
    { 56, 34, 35 },           //time/div index
    { 57, 17, 18 },           //sample rate index (short mode: 18 rates)
    { 58, 2, 3 },             //trigger mode
    { 59, 1, 2 },             //trigger edge
    { 60, 2, 3 },             //trigger source
    { 81, 4, 5 },             //right menu state
    { 83, 100, 101 },         //screen brightness
    { 84, 100, 101 },         //grid brightness
    { 88, 1, 2 },             //average
    { 92, 1, 2 },             //REF1
    { 102, 1, 2 },            //USB mode
    { 120, 1, 2 },            //time cursors
    { 138, 1, 2 },            //CH1 cards hidden
    { 168, 200, 199 },        //CH1 range 0 centre, low end
    { 179, 1800, 1801 },      //CH2 range 5 centre, high end
    { 184, 127, 128 },        //CH1 core A compensation
    { 187, 0xFF81, 0xFF80 },  //CH2 core B compensation: -127 loads, -128 does not
    { 192, 1, 2 },            //generator on
    { 196, 100, 101 },        //generator duty
    { 200, 80, 78 },          //memory depth, lowest
    { 200, 6002, 6001 },      //memory depth even
  };
  uint16_t w[256], bad = 0;
  uint32_t i;

  for(i=0;i<sizeof(cases)/sizeof(cases[0]);i++)
  {
    load(rec_1022_gate, w);
    w[cases[i].word] = cases[i].ok;
    if(cases[i].word == 200)
      w[201] = cases[i].ok / 2;
    seal(w);
    T_EQ(check(w, &bad), SREC_OK);

    w[cases[i].word] = cases[i].bad;
    seal(w);
    T_EQ(check(w, &bad), SREC_BAD_FIELD);
    T_EQ(bad, cases[i].word);
  }

  //Roll mode: a 0/1 switch; it takes the slow time bases (0..10) and the slow rates, the short mode does not
  load(rec_1022_gate, w);
  w[87] = 2;
  seal(w);
  T_EQ(check(w, &bad), SREC_BAD_FIELD);
  T_EQ(bad, 87);
  load(rec_1022_gate, w);
  w[56] = 5;
  seal(w);
  T_EQ(check(w, &bad), SREC_BAD_FIELD);
  T_EQ(bad, 56);
  w[87] = 1;
  w[57] = 28;
  seal(w);
  T_EQ(check(w, &bad), SREC_OK);
  w[57] = 29;
  seal(w);
  T_EQ(check(w, &bad), SREC_BAD_FIELD);
  T_EQ(bad, 57);

  //Generator frequency 0..10 MHz (two words, high first)
  load(rec_1022_gate, w);
  w[194] = 10000000 >> 16;
  w[195] = 10000000 & 0xFFFF;
  seal(w);
  T_EQ(check(w, &bad), SREC_OK);
  w[195]++;
  seal(w);
  T_EQ(check(w, &bad), SREC_BAD_FIELD);
  T_EQ(bad, 194);

  //Trigger point: pretrigger + 2 <= depth
  load(rec_1022_gate, w);
  w[201] = w[200] - 2;
  seal(w);
  T_EQ(check(w, &bad), SREC_OK);
  w[201] = w[200] - 1;
  seal(w);
  T_EQ(check(w, &bad), SREC_BAD_FIELD);
  T_EQ(bad, 201);

  //What the load clamps or migrates stays accepted: a trace position off the drag range, Trace style 3, a depth over
  //any bitstream's, words 202..223 without their markers
  load(rec_1022_gate, w);
  w[15] = 0xFFFF;
  w[89] = 3;
  w[200] = 60000;
  w[201] = 30000;
  for(i=202;i<224;i++)
    w[i] = 0x1234;
  seal(w);
  T_EQ(check(w, &bad), SREC_OK);

  T_STR(srec_text(SREC_BAD_FIELD), "value out of range");
  T_STR(srec_text(SREC_BAD_SUM), "checksum");
}

//Sector 708 of 11-04: the v3 block is valid; against the 11-04 record's centres (0) it does not bind (GET_CAL then
//reported cal_version 1, flags 0x0d: loaded, not bound); the centres acal_centres takes from it bind
static void test_cal_1104(void)
{
  uint8_t      s[512];
  uint16_t     w[256], off[7];
  int16_t      ca, cb;
  acal_channel ch[2];
  int          c, r;

  T_EQ(unhex(cal_1104, s, sizeof(s)), 512);
  T_EQ(acal_unpack(s + ACAL_SECTOR_OFFSET, ch), ACAL_OK);
  load(rec_1104, w);

  for(c=0;c<2;c++)
  {
    T_FALSE(acal_bound(&ch[c], w + 168 + 6 * c));

    acal_centres(&ch[c], off, &ca, &cb);
    T_TRUE(acal_bound(&ch[c], off));
    T_EQ(off[6], off[5]);

    for(r=0;r<6;r++)
      T_TRUE((off[r] >= SREC_PWM_MIN) && (off[r] <= SREC_PWM_MAX));

    T_TRUE((ca >= -127) && (ca <= 127) && (cb >= -127) && (cb <= 127));
  }

  //The stored centres (CH1 range 0 0x363, CH2 range 0 0x35A)
  acal_centres(&ch[0], off, &ca, &cb);
  T_EQ(off[0], 0x363);
  acal_centres(&ch[1], off, &ca, &cb);
  T_EQ(off[0], 0x35A);

  //No block: the 1.00o5 defaults
  acal_centres(0, off, &ca, &cb);
  for(r=0;r<7;r++)
    T_EQ(off[r], ACAL_PWM_DEFAULT);
  T_EQ(ca, 0);
  T_EQ(cb, 0);
}

int main(void)
{
  test_1104();
  test_good();
  test_zero_body();
  test_damage();
  test_fields();
  test_cal_1104();
  return t_report("test_settings");
}
