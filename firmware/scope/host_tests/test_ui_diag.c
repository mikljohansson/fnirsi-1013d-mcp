//----------------------------------------------------------------------------------------------------------------------------------
// test_ui_diag.c - the Diagnostics pages and the first-run screen (ui_diag.c, fw 0.2.59): every text fits the space it
// was laid out for in the device's own fonts (also with long versions and large counters), the buttons meet the 44 x 80
// rule and do not overlap, the hit areas, the calibration verdict rules and the provenance sentence, the tiles' states.
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "ui_diag.h"
#include "pc_hal.h"
#include "ui_fmt.h"
#include "adc_cal.h"
#include "pc_protocol.h"
#include "test_util.h"

extern FONTDATA font_2, font_3, font_5;

static uint8_t     health[124];
static uint8_t     cal[2][364];
static uint32_t    link[64];
static pc_image_id run;
static pc_card_id  card;
static const int32_t volts_def[7] = { 3600000, 3600000, 3700000, 3700000, 3800000, 3800000, 7600000 };
static const char *ev_text[UD_LOG_LINES];
static uint32_t    ev_ms[UD_LOG_LINES];
static ud_page     pg;

static void p16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *p, uint32_t v) { p16(p, v & 0xFFFF); p16(p + 2, v >> 16); }

static const FONTDATA *font_of(int f)
{
  return (f == UD_F_HEAD) ? &font_5 : ((f == UD_F_BODY) ? &font_3 : &font_2);
}

//The lines of tile i (0 Firmware, 1 Restarts, 2 USB, 3 Battery), for the log
static void dump_tile(int i)
{
  uint32_t k;
  int x = 12 + 196 * i + 10;

  for(k=0;k<pg.ntext;k++)
    if((pg.text[k].x == x) && (pg.text[k].y >= 96) && (pg.text[k].y < 222))
      printf("tile %d y %d: %s\n", i, pg.text[k].y, pg.text[k].text);
}

//A healthy, calibrated unit as the owner's: fw from RAM equal to the card, AL3 0x1535, USB configured
static void healthy(ud_input *in)
{
  uint32_t ch, r, k, m;

  memset(health, 0, sizeof(health));
  memset(cal, 0, sizeof(cal));
  memset(link, 0, sizeof(link));
  memset(&run, 0, sizeof(run));
  memset(&card, 0, sizeof(card));

  strcpy(run.version, "0.2.59");
  run.build = 0x1234abcd;
  run.time[0] = 26; run.time[1] = 10; run.time[2] = 19; run.time[3] = 8; run.time[4] = 44;
  run.proto_major = 1;
  run.proto_minor = 16;

  card.state = PC_ID_RECORD;
  card.same = 1;
  strcpy(card.version, "0.2.59");

  health[4] = 3;                          //configured
  health[5] = 0x0B;                       //high speed, configured, port open
  p32(health + 16, 0x0005107D);           //MMU and caches on
  p32(health + 20, 12);                   //power-on 12
  p32(health + 24, 2225);                 //37 h 5 min
  p32(health + 28, 6120000);              //1 h 42 min
  health[32] = 0x0B;
  health[33] = 1;

  link[36] = 1;                           //one suspend

  for(ch=0;ch<2;ch++)
  {
    uint8_t *c = cal[ch], *b = c + 112;

    c[0] = (uint8_t)ch;
    c[72] = 3;
    c[73] = 0x01 | 0x02 | 0x08;

    for(r=0;r<7;r++)
    {
      p16(c + 4 + 2 * r, 865 + r);
      p32(c + 20 + 4 * r, (uint32_t)(volts_def[r] + volts_def[r] / 500));   //+0.2 %
    }

    p32(c + 52, 870);
    p32(c + 56, 100);
    p32(c + 60, 59136);

    for(m=0;m<2;m++)
      for(k=0;k<2;k++)
        for(r=0;r<6;r++)
        {
          p16(b + 12 + 2 * (12 * m + 6 * k + r), (uint32_t)(int16_t)(16 * 131 + k * 90));
          b[108 + 12 * m + 6 * k + r] = m ? 2 : 12;      //gated 0.13, full 0.75
        }

    p16(b + 132, 33000);                  //full A
    p16(b + 134, 32570);                  //full B: 1.3 % apart
    p16(b + 136, 32400);                  //gated: full/gated 1.1 % off
    p16(b + 138, 32400);
    b[140] = 2;

    //Tracking data
    c[256] = 0x01 | 0x20;
    p16(c + 258, 31);                     //+0.12 code
    p32(c + 260, 52000);                  //+0.31 %
    p32(c + 268, 3000);
    p16(c + 280, 179);                    //+0.70 code
    p16(c + 282, 4);
    p32(c + 284, 12000);

    //Provenance (both channels carry the same)
    c[288] = ACAL_PROV_VALID;
    memcpy(c + 292 + 12, "0.2.59", 6);
    p16(c + 292 + 28, 0x1535);
    p32(c + 292 + 32, 12 * 60000);
    p32(c + 292 + 36, 11);
    p32(c + 348, 1);
  }

  memset(in, 0, sizeof(*in));
  in->health        = health;
  in->cal[0]        = cal[0];
  in->cal[1]        = cal[1];
  in->link          = link;
  in->nlink         = 62;
  in->run           = &run;
  in->card          = &card;
  in->sd_sectors    = 15523840;           //7.9 GB
  in->fpga          = 0x1535;
  in->variant       = 2;
  in->caps          = PC_CAP_GENERATOR_DDS | PC_CAP_EXT_TRIGGER | PC_CAP_DECIM | PC_CAP_FIR;
  in->battery       = 18;
  in->charging      = 1;
  in->rtc           = 1;
  in->temp_c        = 31;
  in->volts_default = volts_def;
  in->serial        = "1013D-0001";
}

//Every text fits its space and the screen; buttons are at least 44 x 80, on the screen, apart from each other
static void check_layout(const char *what, int allow_cut)
{
  uint32_t i, j;

  for(i=0;i<pg.ntext;i++)
  {
    const ud_text *t = &pg.text[i];
    const char    *s = t->text;
    int            x = t->x, w, maxw = t->maxw;

    if((s[0] == UD_ICON_OK[0]) || (s[0] == UD_ICON_WARN[0]))
    {
      s++;
      maxw -= UD_ICON_W;
    }

    w = (int)ui_text_width(font_of(t->font), s);

    if(!allow_cut && !t->trim && t->cut)
      printf("%s: cut \"%s\" (maxw %d)\n", what, t->text, t->maxw);
    if(!allow_cut && !t->trim)
      T_EQ(t->cut, 0);
    if(w > maxw)
      printf("%s: \"%s\" %d px > %d\n", what, s, w, maxw);
    T_TRUE(w <= maxw);

    if(t->right)
    {
      T_TRUE(x - t->maxw >= 0);
      T_TRUE(x <= 800);
    }
    else
    {
      T_TRUE(x >= 0);
      if(x + maxw > 800)
        printf("%s: \"%s\" at %d + %d\n", what, s, x, maxw);
      T_TRUE(x + maxw <= 800);
    }

    T_TRUE((t->y >= 0) && (t->y + font_of(t->font)->height <= 480));
  }

  for(i=0;i<pg.nbtn;i++)
  {
    const ud_button *b = &pg.btn[i];

    T_TRUE(b->h >= 44);
    T_TRUE(b->w >= 80);
    T_TRUE((b->x >= 0) && (b->y >= 0) && (b->x + b->w <= 800) && (b->y + b->h <= 480));
    T_TRUE(ui_text_width(&font_3, b->label) + 16 <= (uint32_t)b->w);

    //Hit area = drawn area: the corners hit, one pixel outside does not (unless a neighbour is there)
    T_EQ(ud_hit(&pg, b->x, b->y), b->id);
    T_EQ(ud_hit(&pg, b->x + b->w - 1, b->y + b->h - 1), b->id);

    for(j=i+1;j<pg.nbtn;j++)
    {
      const ud_button *c = &pg.btn[j];
      int apart = (b->x + b->w <= c->x) || (c->x + c->w <= b->x) || (b->y + b->h <= c->y) || (c->y + c->h <= b->y);

      T_TRUE(apart);
    }
  }

  T_TRUE(pg.ntext < UD_MAX_TEXTS);
}

static int has_text(const char *s)
{
  uint32_t i;

  for(i=0;i<pg.ntext;i++)
    if(strstr(pg.text[i].text, s))
      return 1;

  return 0;
}

static int text_colour(const char *s)
{
  uint32_t i;

  for(i=0;i<pg.ntext;i++)
    if(strstr(pg.text[i].text, s))
      return pg.text[i].colour;

  return -1;
}

static void events(ud_input *in, uint32_t n)
{
  static const char *words[4] = { "port opened (DTR on)", "SET_CONFIGURATION (enumerated)",
                                  "bus reset after a suspend of 4 ms", "soft reconnect (long suspend), 17 suspends, suspended 3600 s" };
  uint32_t i;

  for(i=0;i<n && i<UD_LOG_LINES;i++)
  {
    ev_text[i] = words[i % 4];
    ev_ms[i] = 4000000000u - 1000 * i;
  }

  in->nevents = (n < UD_LOG_LINES) ? n : UD_LOG_LINES;
  in->events_total = n;
  in->events_more = n > UD_LOG_LINES;
  in->event_text = ev_text;
  in->event_ms = ev_ms;
}

static void test_pages(void)
{
  ud_input in;
  ud_note  note;
  int      p;

  healthy(&in);
  events(&in, 51);

  for(p=0;p<UD_PAGES;p++)
  {
    ud_build(&pg, p, &in, 0);
    check_layout("healthy", 0);
  }

  //Status as the mockup reads
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("fw 0.2.59, git 1234abcd, 2026-10-19 08:44 UTC, protocol 1.16, serial 1013D-0001, MMU and caches on"));
  //fw 0.2.61: 0x1532..0x1536 have the ADC capture timing race (amber); fw 0.2.62: 0x1537 the other core order
  T_TRUE(has_text("FPGA AL3A10 bitstream 0x1535: ADC capture timing race, fixed in 0x1538"));
  T_EQ(text_colour("FPGA AL3A10 bitstream 0x1535"), UD_C_WARN);
  in.fpga = 0x1537;
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  check_layout("0x1537", 0);
  T_TRUE(has_text("FPGA AL3A10 bitstream 0x1537: ADC core order differs: recalibrate after flashing 0x1538"));
  T_EQ(text_colour("FPGA AL3A10 bitstream 0x1537"), UD_C_WARN);

  //0x1538: the line reads GET_HEALTH 88..103 (the FPGA's 0x33 after the last full-rate capture)
  in.fpga = 0x1538;
  health[88] = 2;
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("FPGA AL3A10 bitstream 0x1538: encode check, no 200 MSa/s capture yet"));
  T_EQ(text_colour("FPGA AL3A10 bitstream 0x1538"), UD_C_SOFT);
  health[89] = 0xF6; health[90] = 0x01; health[91] = 0xF6;
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("FPGA AL3A10 bitstream 0x1538: encode check OK"));
  T_EQ(text_colour("FPGA AL3A10 bitstream 0x1538"), UD_C_SOFT);
  p32(health + 92, 3);
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("FPGA AL3A10 bitstream 0x1538: encode check OK, 3 captures re-taken"));
  p32(health + 92, 1);
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("FPGA AL3A10 bitstream 0x1538: encode check OK, 1 capture re-taken"));
  health[89] = 0x67; health[90] = 0x05;                         //CH2 A and CH1 B wrong, accepted after the re-arms
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  check_layout("0x1538 failed", 0);
  T_TRUE(has_text("FPGA AL3A10 bitstream 0x1538: encode check failed on CH2 A, CH1 B"));
  T_EQ(text_colour("FPGA AL3A10 bitstream 0x1538"), UD_C_WARN);
  health[89] = 0xF7;                                            //failed, pins right (an asynchronous write)
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("FPGA AL3A10 bitstream 0x1538: encode check failed"));
  T_TRUE(!has_text("failed on"));
  health[90] = 0x03; health[91] = 0xFA;                         //boot: convention bits 10
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("FPGA AL3A10 bitstream 0x1538: unknown encode convention, 0x33 = 0xfa"));
  T_EQ(text_colour("FPGA AL3A10 bitstream 0x1538"), UD_C_WARN);
  memset(health + 88, 0, 16);
  in.fpga = 0x1535;
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("Running 0.2.59"));
  T_TRUE(has_text("the image the card"));
  T_TRUE(has_text("No crashes."));
  T_TRUE(has_text("Up 1 h 42 min."));
  T_TRUE(has_text("PC connected, high speed."));
  T_TRUE(has_text("0 bad frames"));
  T_TRUE(has_text("1 suspend"));
  T_TRUE(has_text("0 reconnects"));
  T_TRUE(has_text("18 of 20"));
  T_TRUE(has_text("RTC module 31 C."));
  T_TRUE(has_text("Calibrated, every value within its limit."));
  T_TRUE(has_text("1.2 %"));
  T_TRUE(has_text("0.13 code"));
  T_TRUE(has_text("0.75 code"));
  T_TRUE(has_text("Calibrated on fw 0.2.59, FPGA 0x1535, 12 min after power-on, 1 power-on ago."));
  T_TRUE(has_text("Updates every second"));
  T_EQ(text_colour("18 of 20"), UD_C_OK);

  //A note replaces the hint
  strcpy(note.text, "Saved to the card: reports/report-00012-00.txt");
  note.colour = UD_C_OK;
  ud_build(&pg, UD_PAGE_STATUS, &in, &note);
  check_layout("note", 0);
  T_TRUE(has_text("reports/report-00012-00.txt"));
  T_TRUE(!has_text("Updates every second"));

  //Calibration page: the raw numbers
  ud_build(&pg, UD_PAGE_CAL, &in, 0);
  T_TRUE(has_text("Calibration block (sector 708): CH1 in use, CH2 in use."));
  T_TRUE(has_text("131.0 / 136.6"));
  T_TRUE(has_text("+0.2 %"));
  T_TRUE(has_text("Gains full A 1.0071, B 0.9940"));
  T_TRUE(has_text("Gains gated A 0.9888, B 0.9888, range 1 V"));
  T_TRUE(has_text("Tracking A-B +0.12 code, slope +0.31 %, 3 s ago"));
  T_TRUE(has_text("Full-rate offset +0.70 code, 4 probes, 12 s ago"));
  T_TRUE(has_text("DC shift: centre 870, size 100, value 59136"));
  T_TRUE(has_text("1.00o5 core compensations: not used (v3 in use)"));
  //fw 0.2.63: no supply hedge; the DC shift trim's state is the button's colour, no "off" line
  T_TRUE(!has_text("Needs a DC supply up to 15 V."));
  T_TRUE(!has_text("DC shift trim off."));
  T_TRUE(!has_text("On: CTRL on the scope opens the trim."));
  //fw 0.2.68: one bottom row, equal buttons 12 px apart, Clear calibration last
  T_EQ(ud_hit(&pg, 20, 440), UD_ID_CALIBRATE);
  T_EQ(ud_hit(&pg, 300, 440), UD_ID_DCSHIFT);
  T_EQ(ud_hit(&pg, 500, 440), UD_ID_VOLTS);
  T_EQ(ud_hit(&pg, 700, 440), UD_ID_CLEAR);
  T_EQ(ud_hit(&pg, 20, 380), UD_ID_NONE);
  T_EQ(ud_hit(&pg, 200, 440), UD_ID_NONE);
  {
    uint32_t b, n = 0;
    int      x = 12;

    for(b = 0; b < pg.nbtn; b++)
      if(pg.btn[b].y == 420)
      {
        T_EQ(pg.btn[b].x, x);
        T_EQ(pg.btn[b].h, 44);
        T_EQ(pg.btn[b].w, 185);
        x += 185 + 12;
        n++;
      }
    T_EQ(n, 4);
    T_EQ(x - 12, 788);
  }
  {
    uint32_t b;

    for(b = 0; b < pg.nbtn; b++)
      if(pg.btn[b].id == UD_ID_DCSHIFT)
        T_EQ(pg.btn[b].style, UD_B_GREY);
  }
  in.dc_shift = 1;
  ud_build(&pg, UD_PAGE_CAL, &in, 0);
  T_EQ(text_colour("On: CTRL on the scope opens the trim."), UD_C_WARN);
  {
    uint32_t b;

    for(b = 0; b < pg.nbtn; b++)
      if(pg.btn[b].id == UD_ID_DCSHIFT)
        T_EQ(pg.btn[b].style, UD_B_ON);
  }
  strcpy(note.text, "Calibration cleared: run Calibrate.");
  note.colour = UD_C_WARN;
  ud_build(&pg, UD_PAGE_CAL, &in, &note);
  check_layout("cal note", 0);
  T_TRUE(has_text("Calibration cleared: run Calibrate."));

  //fw 0.2.79: FPGA 0x153A capture stages: none shown without them; power-on, trained, another bitstream's; the
  //self-check as a residual against its limit, with the trim line and the note under it (all three lines fit)
  T_FALSE(has_text("ADC capture"));
  health[106] = PC_CAPF_HAS; health[104] = 0xE4; health[105] = 0x08;
  ud_build(&pg, UD_PAGE_CAL, &in, &note);
  check_layout("cal capture", 0);
  T_TRUE(has_text("ADC capture stages CH1 A4 B4, CH2 A3 B4 (not trained: calibrate): no 200 MSa/s capture yet"));
  health[106] = PC_CAPF_HAS | PC_CAPF_TRAINED | PC_CAPF_READ;
  ud_build(&pg, UD_PAGE_CAL, &in, 0);
  T_TRUE(has_text("CH2 A3 B4 (trained): capture clean"));
  T_EQ(text_colour("ADC capture stages CH1 A4 B4, CH2 A3 B4 (trained): capture clean"), UD_C_SOFT);
  health[106] |= PC_CAPF_FLAGGED; health[107] = 0x04; health[112] = 37;
  ud_build(&pg, UD_PAGE_CAL, &in, 0);
  T_EQ(text_colour("ADC capture stages CH1 A4 B4, CH2 A3 B4 (trained): CH2 A: 37 outliers"), UD_C_WARN);
  health[106] |= PC_CAPF_REPEAT;
  ud_build(&pg, UD_PAGE_CAL, &in, 0);
  T_EQ(text_colour("ADC capture stages CH1 A4 B4, CH2 A3 B4 (trained): CH2 A: 37 outliers; calibrate"), UD_C_WARN);
  health[106] &= (uint8_t)~PC_CAPF_FLAGGED;
  ud_build(&pg, UD_PAGE_CAL, &in, &note);
  check_layout("cal capture repeat", 0);
  T_EQ(text_colour("ADC capture stages CH1 A4 B4, CH2 A3 B4 (trained): last capture clean, flagged 3 times in a minute: "
                   "calibrate"), UD_C_WARN);
  health[106] = PC_CAPF_HAS | PC_CAPF_OTHER; cal[0][360] = 0x3B; cal[0][361] = 0x15;
  ud_build(&pg, UD_PAGE_CAL, &in, &note);
  check_layout("cal capture other", 0);
  T_TRUE(has_text("(trained on 0x153b: calibrate): no 200 MSa/s capture yet"));
  memset(health + 104, 0, 20);
  memset(cal[0] + 356, 0, 8);
  in.dc_shift = 0;

  //Log page
  ud_build(&pg, UD_PAGE_LOG, &in, 0);
  T_TRUE(has_text("1..17 of 51, newest first"));
  T_TRUE(has_text("4000000.0 s"));
  T_TRUE(has_text("Card image is the running image"));
  T_TRUE(has_text("Power-on #12, up 1 h 42 min."));
  T_TRUE(has_text("Operating time 37 h 5 min."));
  T_EQ(ud_hit(&pg, 230, 430), UD_ID_OLDER);

  //Developer page (fw 0.2.63): Enter FEL; the Debug overlay row only while the developer flag is set, to switch it off
  ud_build(&pg, UD_PAGE_DEV, &in, 0);
  T_EQ(ud_hit(&pg, 400, 100), UD_ID_FEL);
  T_EQ(ud_hit(&pg, 400, 170), UD_ID_NONE);
  T_TRUE(!has_text("Developer flag: raw trigger address and touch coordinates on the scope"));
  in.dev_mode = 1;
  ud_build(&pg, UD_PAGE_DEV, &in, 0);
  check_layout("dev flag", 0);
  T_EQ(ud_hit(&pg, 400, 100), UD_ID_OVERLAY);
  T_EQ(ud_hit(&pg, 400, 170), UD_ID_FEL);
  T_STR(pg.btn[pg.nbtn - 2].note, "Switch off");
  T_EQ(pg.btn[pg.nbtn - 2].note_colour, UD_C_WARN);
  in.dev_mode = 0;

  //Header: tabs and Back on every page, the shown one highlighted
  for(p=0;p<UD_PAGES;p++)
  {
    ud_build(&pg, p, &in, 0);
    T_EQ(ud_hit(&pg, 165, 10), UD_ID_TAB_STATUS);
    T_EQ(ud_hit(&pg, 300, 40), UD_ID_TAB_CAL);
    T_EQ(ud_hit(&pg, 420, 20), UD_ID_TAB_LOG);
    T_EQ(ud_hit(&pg, 630, 20), UD_ID_TAB_DEV);
    T_EQ(ud_hit(&pg, 700, 20), UD_ID_BACK);
    T_EQ(ud_hit(&pg, 692, 4), UD_ID_BACK);      //fw 0.2.63: Back where the Settings screen has it (692, 4, 96 x 44)
    T_EQ(ud_hit(&pg, 787, 47), UD_ID_BACK);
    T_EQ(ud_hit(&pg, 691, 20), UD_ID_NONE);
    T_EQ(ud_hit(&pg, 100, 20), UD_ID_NONE);
    T_EQ(ud_hit(&pg, 400, 300), UD_ID_NONE);
    T_EQ(pg.btn[p].style, UD_B_TAB_ON);
  }
}

static void test_stress(void)
{
  ud_input in;
  int      p;

  //Long versions, large counters, a restart, everything at its widest: nothing overflows (texts may be cut only where
  //the builder says so, and then still fit)
  healthy(&in);
  strcpy(run.version, "0.2.59-m1-nommu");
  strcpy(card.version, "0.2.50-nommu");
  card.same = 0;
  link[12] = 998;
  link[13] = 1;
  link[36] = 9999;
  link[61] = 99;
  link[49] = 3;
  link[50] = 4;
  link[51] = 0x80012354;
  link[58] = 0x00F;
  link[59] = 0xC0DEC0DE;
  p32(health + 20, 99999);
  p32(health + 24, 9999999);
  p32(health + 28, 0xFFFFFFFF);
  health[44] = 1;
  health[45] = 3;
  p32(health + 48, 99998);
  in.battery = 2;
  in.charging = 0;
  in.temp_c = -12;
  in.sd_sectors = 0xFFFFFFFF;
  events(&in, 100000);

  for(p=0;p<UD_PAGES;p++)
  {
    ud_build(&pg, p, &in, 0);
    check_layout("stress", 0);
  }

  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("Running 0.2.59-m1-nommu"));
  T_TRUE(has_text("starts 0.2.50-nommu"));
  T_TRUE(has_text("Details on the Log page."));
  T_TRUE(has_text("Crashed 3 times this run:"));
  T_TRUE(has_text("data abort at 0x8001234c."));
  T_EQ(text_colour("Crashed 3 times this run:"), UD_C_SOFT);
  T_EQ(text_colour("Fault"), UD_C_FAULT);
  T_TRUE(has_text("Low: readings and SD"));
  T_TRUE(has_text("RTC module -12 C."));
  T_TRUE(has_text("999 bad frames"));
  T_TRUE(has_text("9999 suspends"));
  T_TRUE(has_text("99 reconnects"));

  ud_build(&pg, UD_PAGE_LOG, &in, 0);
  T_TRUE(has_text("Restarted 3 times since power-on"));
  T_TRUE(has_text("data abort at 0x8001234c"));
  T_TRUE(has_text("fault status 0x00f, address 0xc0dec0de"));

  //Stock bitstream, no card, no calibration, no RTC, USB off
  healthy(&in);
  in.variant = 1;
  in.fpga = 0x1432;
  in.caps = 0;
  in.sd_sectors = 0;
  in.rtc = 0;
  health[4] = 2;
  cal[0][72] = cal[1][72] = 1;
  cal[0][73] = cal[1][73] = 0;
  cal[0][288] = cal[1][288] = 0;

  for(p=0;p<UD_PAGES;p++)
  {
    ud_build(&pg, p, &in, 0);
    check_layout("stock", 0);
  }

  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("FPGA stock bitstream 0x1432: no generator, no filter; gain calibration not possible"));
  T_EQ(text_colour("FPGA stock bitstream"), UD_C_WARN);
  T_TRUE(has_text("from RAM. No SD card:"));
  T_TRUE(has_text("No PC"));
  T_TRUE(has_text("Uncalibrated: readings may be several % off. Run Calibrate."));
  T_TRUE(has_text("not calibrated"));
  T_TRUE(has_text("No calibration on the card: 1.00o5's defaults are in use."));
  T_TRUE(!has_text("RTC module"));

  ud_build(&pg, UD_PAGE_CAL, &in, 0);
  T_TRUE(has_text("CH1 none, CH2 none"));
  T_TRUE(has_text("No calibration block: run Calibrate."));
  T_TRUE(has_text("1.00o5 core compensations in use: A 0, B 0"));

  ud_build(&pg, UD_PAGE_LOG, &in, 0);
  T_TRUE(has_text("No card: settings will not be saved"));
  T_TRUE(has_text("No events logged since power-on."));
  T_EQ(ud_hit(&pg, 230, 430), UD_ID_NONE);                    //nothing to page through
}

#define OVER "A value is over its limit (below): move chargers and the USB cable away and calibrate again.\n" \
             "If it stays, the input stage is noisy."

static void verdict(ud_input *in, int state, const char *reason)
{
  ud_verdict v;

  ud_cal_verdict(in, &v);
  T_EQ(v.state, state);

  if(strcmp(v.reason, reason))
    printf("verdict: \"%s\" != \"%s\"\n", v.reason, reason);
  T_STR(v.reason, reason);
}

static void test_verdicts(void)
{
  ud_input in;
  char     t[UD_TEXT_LEN];

  healthy(&in);
  verdict(&in, UD_V_OK, "Calibrated, every value within its limit.");

  //Over a limit: the class ratio (2 %), the noise with the inputs open: gated 1.0 code, full rate 3.0 codes, the zero
  //step's own limits (fw 0.2.100, owner 2026-10-20 18:13; 0.30 / 1.00 up to 0.2.99)
  T_EQ(UD_LIM_NOISE_GATED, 100);
  T_EQ(UD_LIM_NOISE_FULL, 300);
  p16(cal[1] + 112 + 136, 31800);
  verdict(&in, UD_V_CHECK, OVER);
  healthy(&in);
  cal[0][112 + 108 + 12 + 3] = 16;                           //1.00 code gated: on the limit
  verdict(&in, UD_V_OK, "Calibrated, every value within its limit.");
  cal[0][112 + 108 + 12 + 3] = 17;                           //1.06 code gated
  verdict(&in, UD_V_CHECK, OVER);
  healthy(&in);
  cal[0][112 + 108 + 7] = 48;                                //3.00 codes full: on the limit
  verdict(&in, UD_V_OK, "Calibrated, every value within its limit.");
  cal[0][112 + 108 + 7] = 49;                                //3.06
  verdict(&in, UD_V_CHECK, OVER);

  //The owner's unit (bench 2026-10-20): stored open-input full rate CH1 core B 1.19, CH2 core A 2.75 codes (5 V/div),
  //gated at most 0.50 / 0.38: OK on the Status page and the Calibration tab, in the wizard's own terms
  healthy(&in);
  cal[0][112 + 108 + 6 + 0] = 19;                            //CH1 full, core B, 5 V
  cal[1][112 + 108 + 0 + 0] = 44;                            //CH2 full, core A, 5 V
  cal[0][112 + 108 + 12 + 0] = 8;                            //CH1 gated 0.50
  cal[1][112 + 108 + 12 + 6] = 6;                            //CH2 gated 0.38
  verdict(&in, UD_V_OK, "Calibrated, every value within its limit.");
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  check_layout("bench noise", 0);
  T_TRUE(has_text("Noise RMS, inputs open, gated"));
  T_TRUE(has_text("Noise RMS, inputs open, full"));
  T_TRUE(has_text("limit 1.0 code"));
  T_TRUE(has_text("limit 3.0 code"));
  T_TRUE(has_text("1.19 code"));
  T_TRUE(has_text("2.75 code"));
  T_TRUE(!has_text("provisional"));
  T_EQ(text_colour("Check"), -1);
  ud_build(&pg, UD_PAGE_CAL, &in, 0);
  check_layout("bench noise cal", 0);
  T_TRUE(has_text("Noise RMS, inputs open: gated 0.50, full 1.19 code"));
  T_TRUE(has_text("Noise RMS, inputs open: gated 0.38, full 2.75 code"));
  T_TRUE(has_text("Gated limit 1.0 code, full limit 3.0 code"));
  T_EQ(text_colour("Noise RMS, inputs open: gated 0.38, full 2.75 code"), UD_C_TEXT);

  //Over: the advice in two lines on the Status page
  cal[1][112 + 108 + 0 + 0] = 49;
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  check_layout("noise over", 0);
  T_TRUE(has_text("A value is over its limit (below): move chargers and the USB cable away and calibrate again."));
  T_TRUE(has_text("If it stays, the input stage is noisy."));
  T_EQ(text_colour("3.06 code"), UD_C_TEXT);
  ud_build(&pg, UD_PAGE_CAL, &in, 0);
  T_EQ(text_colour("Noise RMS, inputs open: gated 0.38, full 3.06 code"), UD_C_WARN);

  //Warm-up
  healthy(&in);
  p32(cal[0] + 292 + 32, 3 * 60000 + 59000);
  verdict(&in, UD_V_CHECK, "Calibrated 3 min after power-on: recalibrate after 5 minutes of warm-up.");

  //Gains carried over, not measured (open bitstream, stock bitstream)
  healthy(&in);
  cal[1][73] |= 0x10;
  verdict(&in, UD_V_CHECK, "Gains carried over from an earlier calibration: run Calibrate.");
  cal[1][73] &= (uint8_t)~0x08;
  verdict(&in, UD_V_CHECK, "Gains not measured: run Calibrate with the probes on the post.");
  in.variant = 1;
  in.fpga = 0x1432;
  verdict(&in, UD_V_CHECK, "FPGA changed since calibration (0x1535 to 0x1432): recalibrate.");
  p16(cal[0] + 292 + 28, 0x1432);
  verdict(&in, UD_V_CHECK, "FPGA stock 0x1432: no generator, gain calibration not possible.");

  //fw 0.2.81: a 0x153A core flagged 3 times within a minute: amber, calibrate (the last capture may be clean)
  healthy(&in);
  health[106] = PC_CAPF_HAS | PC_CAPF_TRAINED | PC_CAPF_READ;
  verdict(&in, UD_V_OK, "Calibrated, every value within its limit.");
  health[106] |= PC_CAPF_REPEAT;
  verdict(&in, UD_V_CHECK, "ADC capture flagged 3 times within a minute: run Calibrate.");
  in.fpga = 0x1536;
  verdict(&in, UD_V_CHECK, "FPGA changed since calibration (0x1535 to 0x1536): recalibrate.");

  //FPGA changed
  healthy(&in);
  in.fpga = 0x1536;
  verdict(&in, UD_V_CHECK, "FPGA changed since calibration (0x1535 to 0x1536): recalibrate.");

  //Not bound, then no block
  healthy(&in);
  cal[0][73] &= (uint8_t)~0x02;
  cal[0][72] = 1;
  verdict(&in, UD_V_CHECK, "Saved calibration does not match the current offsets: run Calibrate.");
  T_EQ(ud_cal_calibrated(cal[0], cal[1]), 0);
  cal[0][73] = 0;
  verdict(&in, UD_V_FAULT, "Uncalibrated: readings may be several % off. Run Calibrate.");
  healthy(&in);
  T_EQ(ud_cal_calibrated(cal[0], cal[1]), 1);

  //Provenance
  ud_cal_provenance(&in, t);
  T_STR(t, "Calibrated on fw 0.2.59, FPGA 0x1535, 12 min after power-on, 1 power-on ago.");
  p32(cal[0] + 348, 0);
  p32(cal[0] + 292 + 32, 59999);
  ud_cal_provenance(&in, t);
  T_STR(t, "Calibrated on fw 0.2.59, FPGA 0x1535, under a minute after power-on, this power-on.");
  p32(cal[0] + 348, 7);
  cal[0][292 + 31] = 1;
  cal[0][292 + 44] = 26; cal[0][292 + 45] = 10; cal[0][292 + 46] = 19;
  ud_cal_provenance(&in, t);
  T_STR(t, "Calibrated on fw 0.2.59, FPGA 0x1535, 2026-10-19, under a minute after power-on, 7 power-ons ago.");
  p32(cal[0] + 348, 0xFFFFFFFF);
  cal[0][292 + 31] = 0;
  ud_cal_provenance(&in, t);
  T_STR(t, "Calibrated on fw 0.2.59, FPGA 0x1535, under a minute after power-on.");
  cal[0][288] = ACAL_PROV_NONE;
  ud_cal_provenance(&in, t);
  T_STR(t, "Calibrated before fw 0.2.52: no record of when.");
  cal[0][288] = ACAL_PROV_BAD;
  ud_cal_provenance(&in, t);
  T_STR(t, "The record of when it was calibrated is unreadable.");
}

static void test_tiles(void)
{
  ud_input in;

  //Firmware: the card differs, has no image, unknown, not read yet
  healthy(&in);
  card.same = 0;
  strcpy(card.version, "0.2.25");
  card.state = PC_ID_VERSION_ONLY;
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("from RAM. A power cycle"));
  T_TRUE(has_text("starts 0.2.25"));
  T_TRUE(has_text("from the card."));
  T_EQ(text_colour("Check"), UD_C_WARN);
  card.state = PC_ID_NO_IMAGE;
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("firmware on the card."));
  card.state = PC_ID_UNKNOWN;
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("image is unknown."));
  in.card = 0;
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("Reading the card image."));
  T_EQ(text_colour("Reading"), UD_C_DIM);

  //Restarts (fw 0.2.100, owner 2026-10-20 18:12). Check: a stored record of a crash before this run
  healthy(&in);
  health[44] = 1;
  health[45] = 4;
  p16(health + 46, 1);
  p32(health + 48, 11);
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  check_layout("restarts check", 0);
  T_EQ(text_colour("Check"), UD_C_WARN);
  T_TRUE(has_text("Crashed once before this run:"));
  T_TRUE(has_text("data abort."));
  T_TRUE(has_text("If it happens again,"));
  T_TRUE(has_text("save a report."));
  T_TRUE(has_text("Up 1 h 42 min."));
  T_TRUE(!has_text("Power-on #"));
  T_TRUE(!has_text("LR"));
  p16(health + 46, 2);
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("Crashed 2 times before this"));
  T_TRUE(has_text("run: data abort."));

  //Every exception name and the largest counts: the last line still fits the tile (five lines at most)
  {
    uint32_t type;

    for(type=1;type<=6;type++)
    {
      healthy(&in);
      health[44] = 1;
      health[45] = (uint8_t)type;
      p16(health + 46, 65535);
      p32(health + 28, 0xFFFFFFFF);
      ud_build(&pg, UD_PAGE_STATUS, &in, 0);
      check_layout("restarts check, every type", 0);
      T_TRUE(has_text("save a report."));
          T_TRUE(has_text("Up 1193 h 2 min."));
      T_EQ(text_colour("Check"), UD_C_WARN);
      healthy(&in);
      link[49] = 0xFFFFFFFF;
      link[50] = type;
      link[51] = 0xFFFFFFFF;
      ud_build(&pg, UD_PAGE_STATUS, &in, 0);
      check_layout("restarts fault, every type", 0);
      T_TRUE(has_text("Details on the Log page."));
      T_EQ(text_colour("Fault"), UD_C_FAULT);
    }
  }

  //Fault: this run follows a crash (counter 49 > 0, a real exception); once
  healthy(&in);
  link[49] = 1;
  link[50] = 4;
  link[51] = 0x80014A34;
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("Crashed once this run: data"));
  T_TRUE(has_text("abort at 0x80014a2c."));
  T_TRUE(has_text("Details on the Log page."));
  T_TRUE(!has_text("Up 1 h 42 min."));
  ud_build(&pg, UD_PAGE_LOG, &in, 0);
  T_TRUE(has_text("Restarted 1 time since power-on"));         //the Log page keeps its words and details
  T_TRUE(has_text("data abort at 0x80014a2c"));

  //OK: a deliberate restart (type 0, a jump to the image entry: a FEL load, Enter FEL, an update) is normal, this run
  //or stored; the Log page still shows it in full
  healthy(&in);
  link[49] = 1;
  link[50] = 0;
  link[54] = 0x80000010;
  health[44] = 1 | 2;
  health[45] = 0;
  p16(health + 46, 1);
  p32(health + 48, 12);
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_EQ(text_colour("OK"), UD_C_OK);
  T_TRUE(has_text("No crashes."));
  T_TRUE(has_text("Up 1 h 42 min."));
  T_TRUE(!has_text("Crashed"));
  T_TRUE(!has_text("jump to the entry"));
  T_TRUE(!has_text("Check"));
  ud_build(&pg, UD_PAGE_LOG, &in, 0);
  T_TRUE(has_text("Restarted 1 time since power-on"));
  T_TRUE(has_text("jump to the entry, LR 0x00000000"));
  T_TRUE(has_text("at _reset LR 0x80000010"));
  healthy(&in);
  health[44] = 1;
  health[45] = 0;
  p32(health + 48, 11);
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("No crashes."));
  T_TRUE(!has_text("Check"));
  ud_build(&pg, UD_PAGE_LOG, &in, 0);
  T_TRUE(has_text("Stored from power-on #11:"));
  T_TRUE(has_text("jump to the entry, LR 0x00000000"));

  //The stored crash on the Log page as before
  healthy(&in);
  health[44] = 1;
  health[45] = 4;
  p32(health + 48, 11);
  ud_build(&pg, UD_PAGE_LOG, &in, 0);
  check_layout("stored", 0);
  T_TRUE(has_text("Stored from power-on #11:"));
  T_TRUE(has_text("data abort at 0x"));

  //USB: reconnects and bad frames are amber, suspended, full speed
  healthy(&in);
  link[61] = 2;
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_EQ(text_colour("Check"), UD_C_WARN);
  T_TRUE(has_text("2 reconnects"));
  healthy(&in);
  health[4] = 4;
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("Suspended"));
  T_TRUE(has_text("Host asleep or cable out."));
  healthy(&in);
  health[5] = 0x0A;
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("PC connected, full speed."));

  //CP15 words
  healthy(&in);
  p32(health + 16, 0x0005107C);
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("MMU off, D-cache on, I-cache on"));
  p32(health + 16, 0x00050078);
  ud_build(&pg, UD_PAGE_STATUS, &in, 0);
  T_TRUE(has_text("MMU and caches off"));
}

static void test_first_run(void)
{
  ud_input in;

  healthy(&in);
  cal[0][72] = cal[1][72] = 1;
  cal[0][73] = cal[1][73] = 0;
  ud_build_first_run(&pg, &in, 1, 0);
  check_layout("first run", 0);
  T_TRUE(has_text("Open firmware 0.2.59, protocol 1.16. FPGA bitstream 0x1535."));
  T_TRUE(has_text(UD_ICON_WARN "FPGA AL3A10 bitstream 0x1535: ADC capture timing race, fixed in 0x1538"));
  T_TRUE(has_text(UD_ICON_OK "No saved settings on the card: defaults used."));
  T_TRUE(has_text(UD_ICON_WARN "Calibration missing: readings may be several % off."));
  T_TRUE(!has_text("Volts calibration"));
  T_EQ(ud_hit(&pg, 30, 410), UD_ID_LATER);
  T_EQ(ud_hit(&pg, 700, 450), UD_ID_CAL_NOW);
  T_EQ(ud_hit(&pg, 400, 430), UD_ID_NONE);

  //Stock bitstream, settings loaded, a block that does not bind, volts factors missing too
  in.variant = 1;
  in.fpga = 0x1432;
  cal[0][73] = cal[1][73] = 0x01;
  ud_build_first_run(&pg, &in, 0, 1);
  check_layout("first run stock", 0);
  T_TRUE(has_text(UD_ICON_WARN "FPGA stock bitstream 0x1432: no generator, no filter; gain calibration not possible"));
  T_TRUE(has_text(UD_ICON_OK "Settings loaded from the card."));
  T_TRUE(has_text("Saved calibration does not match the current offsets: calibrate again."));
  T_TRUE(has_text("Volts calibration missing too: it needs a DC supply up to 15 V."));
  T_TRUE(has_text("The gain step needs the open FPGA bitstream: this one keeps the gains."));
  T_EQ(ud_hit(&pg, 700, 450), UD_ID_CAL_NOW);
}

int main(void)
{
  test_pages();
  test_stress();
  test_verdicts();
  test_tiles();
  test_first_run();
  return t_report("test_ui_diag");
}
