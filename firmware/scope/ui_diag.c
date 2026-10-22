//----------------------------------------------------------------------------------------------------------------------------------
// ui_diag.c - the Diagnostics pages and the first-run screen as data (ui_diag.h). Pure C, no globals, no libc.
// Host-tested: host_tests/test_ui.c (every text inside its box with the real font tables, the buttons, the verdicts).
//----------------------------------------------------------------------------------------------------------------------------------

#include "cap_stage.h"
#include "ui_diag.h"
#include "ui_fmt.h"
#include "pc_protocol.h"
#include "adc_cal.h"
#include "enc_heal.h"
#include "pc_hal.h"
#include "pc_cmds.h"

extern FONTDATA font_2;
extern FONTDATA font_3;
extern FONTDATA font_5;

//GET_CAL layout (docs/protocol.md 8.7, rows 51, 54, 55)
#define C_OFFSET     4            //u16[7] PWM centres in use (sector 709)
#define C_VOLTS     20            //u32[7] volts factors (input_calibration)
#define C_COMP      48            //i16 A, B: 1.00o5 core compensations
#define C_SHIFT     52            //u32 centre, i32 size, i32 value
#define C_VERSION   72            //correction in use: 3 = v3
#define C_FLAGS     73            //bit 0 block loaded, 1 bound, 3 gains measured, 4 gains carried over
#define C_BLOCK    112            //the v3 channel block (acal_pack_channel; its class slot 0)
#define B_ZERO      12            //i16 [core][range], 1/16 code
#define B_NOISE    108            //u8 [core][range], 1/16 code
#define B_GAIN     132            //u16 [core], Q15
#define B_GRANGE   140            //u8 range the gains were measured on (0xFF never)
#define C_TRK      256            //u8 flags (bit 0 data), d, e, q, age, records, ...
#define C_PROV     288            //u8 state, then 292.. the 56 stored bytes
#define C_ONS      348            //u32 power-ons since the calibration

#define F_LOADED     0x01
#define F_BOUND      0x02
#define F_GAINS      0x08
#define F_KEPT       0x10

//GET_HEALTH layout (row 55)
#define H_USB_STATE   4
#define H_USB_FLAGS   5
#define H_CP15       16
#define H_POWER_ON   20
#define H_MINUTES    24
#define H_UPTIME     28
#define H_LIFE       32
#define H_LAST_END   33
#define H_SETTINGS   34
#define H_REC        44
#define H_REC_TYPE   45
#define H_REC_COUNT  46
#define H_REC_PON    48
#define H_REC_LR     52
#define H_REC_FAR    80
#define H_ENC_LAST   89           //1.19 (row 58): full-rate capture check
#define H_ENC_FLAGS  90
#define H_ENC_BOOT   91
#define H_ENC_BAD    92
#define H_CAP        104          //1.24: u16 0x63, u8 flags, u8 flagged cores, u16[4] counts, u32 reads, u32 flagged
#define C_CAP        356          //1.24: u8 trailer state, u8 flags, u16 stored, u16 its FPGA word, u16 in effect

//----------------------------------------------------------------------------------------------------------------------------------
// Small text writers (each returns the end, NUL-terminated)

static uint32_t g16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t g32(const uint8_t *p) { return g16(p) | (g16(p + 2) << 16); }

static char *s_str(char *p, const char *s)
{
  while(*s)
    *p++ = *s++;

  *p = 0;
  return p;
}

static char *s_uint(char *p, uint32_t v)
{
  char     t[10];
  uint32_t n = 0;

  do
  {
    t[n++] = (char)('0' + v % 10);
    v /= 10;
  } while(v);

  while(n)
    *p++ = t[--n];

  *p = 0;
  return p;
}

//v / denom with `decimals` decimals, rounded half away from zero; sign: 0 only "-", 1 also "+"
static char *s_fix(char *p, int32_t v, uint32_t denom, uint32_t decimals, int plus)
{
  uint32_t scale = 1, i, a, ip, fp;
  uint64_t q;

  for(i=0;i<decimals;i++)
    scale *= 10;

  a = (v < 0) ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
  q = ((uint64_t)a * scale * 2 + denom) / (2 * (uint64_t)denom);
  ip = (uint32_t)(q / scale);
  fp = (uint32_t)(q % scale);

  if(q && (v < 0))
    *p++ = '-';
  else if(plus && q)
    *p++ = '+';

  p = s_uint(p, ip);

  if(decimals)
  {
    *p++ = '.';

    for(i=scale/10;i;i/=10)
      *p++ = (char)('0' + (fp / i) % 10);
  }

  *p = 0;
  return p;
}

//digits hex digits, no prefix
static char *s_hexd(char *p, uint32_t v, uint32_t digits)
{
  static const char h[] = "0123456789abcdef";

  while(digits--)
    *p++ = h[(v >> (4 * digits)) & 0xF];

  *p = 0;
  return p;
}

static char *s_hex(char *p, uint32_t v, uint32_t digits)
{
  *p++ = '0';
  *p++ = 'x';
  return s_hexd(p, v, digits);
}

//Upper-case hex, as the bitstreams are named in fpga/al3/dist and the guide (the notice screen quotes them)
static char *s_hexu(char *p, uint32_t v, uint32_t digits)
{
  static const char h[] = "0123456789ABCDEF";

  *p++ = '0';
  *p++ = 'x';

  while(digits--)
    *p++ = h[(v >> (4 * digits)) & 0xF];

  *p = 0;
  return p;
}

static char *s_dec2(char *p, uint32_t v)
{
  *p++ = (char)('0' + (v / 10) % 10);
  *p++ = (char)('0' + v % 10);
  *p = 0;
  return p;
}

static char *s_ver(char *p, const char *v16)
{
  uint32_t i;

  for(i=0;(i<15) && v16[i];i++)
    *p++ = v16[i];

  if(i == 0)
    *p++ = '?';

  *p = 0;
  return p;
}

//"1 h 42 min" / "12 min"
static char *s_minutes(char *p, uint32_t m)
{
  if(m >= 60)
  {
    p = s_uint(p, m / 60);
    p = s_str(p, " h ");
  }

  p = s_uint(p, m % 60);
  return s_str(p, " min");
}

static char *s_plural(char *p, uint32_t n, const char *one, const char *many)
{
  p = s_uint(p, n);
  *p++ = ' ';
  return s_str(p, (n == 1) ? one : many);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Page items

static const FONTDATA *ud_font(uint32_t f)
{
  return (f == UD_F_HEAD) ? &font_5 : ((f == UD_F_BODY) ? &font_3 : &font_2);
}

static ud_box *ud_boxa(ud_page *pg, int x, int y, int w, int h, int fill)
{
  ud_box *b;

  if(pg->nbox >= UD_MAX_BOXES)
    return 0;

  b = &pg->box[pg->nbox++];
  b->x = (int16_t)x;
  b->y = (int16_t)y;
  b->w = (int16_t)w;
  b->h = (int16_t)h;
  b->fill = (uint8_t)fill;
  return b;
}

//A text item, cut at the end until it fits maxw
static ud_text *ud_txt(ud_page *pg, int x, int y, int maxw, int font, int colour, const char *s)
{
  ud_text *t;
  uint32_t n = 0;

  if(pg->ntext >= UD_MAX_TEXTS)
    return 0;

  t = &pg->text[pg->ntext++];
  t->x = (int16_t)x;
  t->y = (int16_t)y;
  t->maxw = (uint16_t)maxw;
  t->font = (uint8_t)font;
  t->colour = (uint8_t)colour;
  t->right = 0;
  t->cut = 0;
  t->trim = 0;

  while(s[n] && (n < UD_TEXT_LEN - 1))
  {
    t->text[n] = s[n];
    n++;
  }

  t->text[n] = 0;

  if(s[n])
    t->cut = 1;

  while(n && (ui_text_width(ud_font(font), t->text) > (uint32_t)maxw))
  {
    t->text[--n] = 0;
    t->cut = 1;
  }

  return t;
}

static ud_text *ud_txt_r(ud_page *pg, int xr, int y, int maxw, int font, int colour, const char *s)
{
  ud_text *t = ud_txt(pg, xr, y, maxw, font, colour, s);

  if(t)
    t->right = 1;

  return t;
}

static ud_button *ud_btn(ud_page *pg, int x, int y, int w, int h, int style, int id, const char *label)
{
  ud_button *b;

  if(pg->nbtn >= UD_MAX_BUTTONS)
    return 0;

  b = &pg->btn[pg->nbtn++];
  b->x = (int16_t)x;
  b->y = (int16_t)y;
  b->w = (int16_t)w;
  b->h = (int16_t)h;
  b->style = (uint8_t)style;
  b->id = (uint8_t)id;
  b->label = label;
  b->note = 0;
  b->note_colour = UD_C_DIM;
  return b;
}

int ud_hit(const ud_page *pg, int x, int y)
{
  uint32_t i;

  for(i=0;i<pg->nbtn;i++)
  {
    const ud_button *b = &pg->btn[i];

    if((x >= b->x) && (x < b->x + b->w) && (y >= b->y) && (y < b->y + b->h))
      return b->id;
  }

  return UD_ID_NONE;
}

static void ud_clear(ud_page *pg)
{
  pg->nbox = 0;
  pg->ntext = 0;
  pg->nbtn = 0;
}

static int ud_state_colour(int state)
{
  switch(state)
  {
    case UD_V_OK:    return UD_C_OK;
    case UD_V_CHECK: return UD_C_WARN;
    case UD_V_FAULT: return UD_C_FAULT;
    default:         return UD_C_DIM;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Header: title, the four page tabs, Back (all 44 px tall). fw 0.2.63 (owner): the Settings screen's header, black with
// a grey rule under it (ui_dlg.c draws the band) and Back at the same place (x 692)

#define UD_HDR_H     52
#define UD_TAB_X     160
#define UD_TAB_W     112
#define UD_TAB_PITCH 120
#define UD_BACK_X    692
#define UD_BACK_W    96
#define UD_BTN_Y     420          //bottom button row
#define UD_BTN_H     44

static void ud_header(ud_page *pg, int page)
{
  static const char *tabs[UD_PAGES] = { "Status", "Calibration", "Log", "Developer" };
  int i;

  ud_boxa(pg, 0, 0, 800, UD_HDR_H, 1);
  ud_txt(pg, 12, 14, 140, UD_F_HEAD, UD_C_TITLE, "Diagnostics");

  for(i=0;i<UD_PAGES;i++)
    ud_btn(pg, UD_TAB_X + UD_TAB_PITCH * i, 4, UD_TAB_W, 44, (i == page) ? UD_B_TAB_ON : UD_B_TAB, UD_ID_TAB_STATUS + i,
           tabs[i]);

  ud_btn(pg, UD_BACK_X, 4, UD_BACK_W, 44, UD_B_NEUTRAL, UD_ID_BACK, "Back");
}

//----------------------------------------------------------------------------------------------------------------------------------
// Identity

static char *s_cp15(char *p, uint32_t cp15)
{
  uint32_t mmu = cp15 & 1, d = (cp15 >> 2) & 1, i = (cp15 >> 12) & 1;

  if(mmu && d && i)
    return s_str(p, "MMU and caches on");

  if(!mmu && !d && !i)
    return s_str(p, "MMU and caches off");

  p = s_str(p, mmu ? "MMU on, D-cache " : "MMU off, D-cache ");
  p = s_str(p, d ? "on, I-cache " : "off, I-cache ");
  return s_str(p, i ? "on" : "off");
}

//"fw 0.2.59, git 1234abcd, 2026-10-19 08:44 UTC, protocol 1.16, serial 1013D-0001, MMU and caches on"
static void ud_identity_line(const ud_input *in, char *p)
{
  const pc_image_id *r = in->run;

  p = s_str(p, "fw ");
  p = s_ver(p, r->version);
  p = s_str(p, ", git ");
  p = s_hexd(p, r->build, 8);
  p = s_str(p, ", 20");
  p = s_dec2(p, r->time[0]);
  *p++ = '-';
  p = s_dec2(p, r->time[1]);
  *p++ = '-';
  p = s_dec2(p, r->time[2]);
  *p++ = ' ';
  p = s_dec2(p, r->time[3]);
  *p++ = ':';
  p = s_dec2(p, r->time[4]);
  p = s_str(p, " UTC, protocol ");
  p = s_uint(p, r->proto_major);
  *p++ = '.';
  p = s_uint(p, r->proto_minor);

  if(in->serial && in->serial[0])
  {
    p = s_str(p, ", serial ");
    p = s_str(p, in->serial);
  }

  p = s_str(p, ", ");
  s_cp15(p, g32(in->health + H_CP15));
}

//The FPGA's own encode check (read 0x33 after each full-rate capture, GET_HEALTH 88..103)
static int ud_enc_line(const ud_input *in, char *p)
{
  static const uint8_t pin[4]  = { EH_33_PIN_CH1B, EH_33_PIN_CH1A, EH_33_PIN_CH2B, EH_33_PIN_CH2A };
  static const char   *name[4] = { "CH1 B", "CH1 A", "CH2 B", "CH2 A" };
  const uint8_t *h = in->health;
  uint8_t  f = h[H_ENC_FLAGS], s = h[H_ENC_LAST];
  uint32_t bad = g32(h + H_ENC_BAD), i, n = 0;

  if(f & PC_ENCF_CONV_BAD)
  {
    p = s_str(p, ": unknown encode convention, 0x33 = ");
    s_hex(p, h[H_ENC_BOOT], 2);
    return UD_C_WARN;
  }

  if(!(f & PC_ENCF_READ))
  {
    s_str(p, ": encode check, no 200 MSa/s capture yet");
    return UD_C_SOFT;
  }

  if(s & EH_33_FAILED)
  {
    p = s_str(p, ": encode check failed");

    for(i=4;i--;)
      if(!(s & pin[i]))
      {
        p = s_str(p, n++ ? ", " : " on ");
        p = s_str(p, name[i]);
      }

    return UD_C_WARN;
  }

  p = s_str(p, ": encode check OK");

  if(bad)
  {
    p = s_str(p, ", ");
    s_plural(p, bad, "capture re-taken", "captures re-taken");
  }

  return UD_C_SOFT;
}

//The FPGA line; returns its colour: amber when the scope does not run the bitstream this firmware is built for
//(PC_FW_FPGA_VERSION; the notice screen says what to do), else the AL3A10 line with the FPGA's own encode check
static int ud_fpga_line(const ud_input *in, char *p)
{
  if(in->mismatch)
  {
    p = s_str(p, "FPGA bitstream ");
    p = s_hex(p, in->fpga, 4);
    p = s_str(p, ": this firmware needs ");
    s_hex(p, PC_FW_FPGA_VERSION, 4);
    return UD_C_WARN;
  }

  p = s_str(p, "FPGA AL3A10 bitstream ");
  p = s_hex(p, in->fpga, 4);
  return ud_enc_line(in, p);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Status tiles

#define UD_TILE_Y     96
#define UD_TILE_W     188
#define UD_TILE_H     126
#define UD_TILE_PITCH 196
#define UD_TILE_IN    168         //text width inside a tile
#define UD_TILE_LINES 5           //fw 0.2.100: five 13 px lines 15 px apart (y + 50 .. y + 123 of 126)
#define UD_TILE_LY    50
#define UD_TILE_LP    15

typedef struct
{
  uint8_t state;
  char    word[16];
  char    line[UD_TILE_LINES][40];
} ud_tile;

//fw 0.2.100: a sentence on the next free lines of a tile, broken at spaces to fit UD_TILE_IN in the 13 px font (a word
//that does not fit a line on its own is cut by ud_txt and the layout test fails). Returns 0 when it did not fit
static int ud_tile_say(ud_tile *t, const char *s)
{
  uint32_t k = 0, n, best, i;

  while((k < UD_TILE_LINES) && t->line[k][0])
    k++;

  while(*s)
  {
    if(k >= UD_TILE_LINES)
      return 0;

    //the longest prefix ending before a space (or at the end) that fits
    best = 0;

    for(n=0;s[n] && (n < sizeof(t->line[0]) - 1);n++)
    {
      if((s[n + 1] == ' ') || !s[n + 1])
      {
        for(i=0;i<=n;i++)
          t->line[k][i] = s[i];

        t->line[k][n + 1] = 0;

        if(ui_text_width(&font_2, t->line[k]) > UD_TILE_IN)
          break;

        best = n + 1;
      }
    }

    if(!best)
      best = n;

    for(i=0;i<best;i++)
      t->line[k][i] = s[i];

    t->line[k][best] = 0;
    s += best;

    while(*s == ' ')
      s++;

    k++;
  }

  return 1;
}

static void ud_tile_init(ud_tile *t, int state, const char *word)
{
  uint32_t k;

  t->state = (uint8_t)state;
  s_str(t->word, word);

  for(k=0;k<UD_TILE_LINES;k++)
    t->line[k][0] = 0;
}

static int ud_card_known(const pc_card_id *c)
{
  return c && ((c->state == PC_ID_RECORD) || (c->state == PC_ID_TAIL) || (c->state == PC_ID_VERSION_ONLY));
}

//Firmware: the running image against the one the card starts (a version can be 15 characters: its own line)
static void ud_tile_firmware(const ud_input *in, ud_tile *t)
{
  const pc_card_id *c = in->card;
  char *p;

  if(in->sd_sectors == 0)
  {
    ud_tile_init(t, UD_V_CHECK, "Check");
    p = s_str(t->line[0], "Running ");
    s_ver(p, in->run->version);
    s_str(t->line[1], "from RAM. No SD card:");
    s_str(t->line[2], "settings will not be");
    s_str(t->line[3], "saved.");
    return;
  }

  if(!c || (c->state == PC_ID_NOT_READ))
  {
    ud_tile_init(t, UD_V_NA, "Reading");
    s_str(t->line[0], "Reading the card image.");
    return;
  }

  ud_tile_init(t, UD_V_CHECK, "Check");
  p = s_str(t->line[0], "Running ");
  s_ver(p, in->run->version);

  if(ud_card_known(c) && c->same)
  {
    t->state = UD_V_OK;
    s_str(t->word, "OK");             //owner 2026-10-20 19:24: just the version; "the image the card starts" was clutter
  }
  else if(ud_card_known(c) && c->version[0])
  {
    s_str(t->line[1], "from RAM. A power cycle");
    p = s_str(t->line[2], "starts ");
    s_ver(p, c->version);
    s_str(t->line[3], "from the card.");
  }
  else if(c->state == PC_ID_NO_IMAGE)
  {
    s_str(t->line[1], "from RAM. No open");
    s_str(t->line[2], "firmware on the card.");
  }
  else
  {
    s_str(t->line[1], "from RAM. The card's");
    s_str(t->line[2], "image is unknown.");
  }
}

static const char *ud_exc_name(uint32_t type)
{
  static const char *n[7] = { "jump to the entry", "undefined instruction", "SWI", "prefetch abort", "data abort",
                              "reserved vector", "FIQ" };

  return (type < 7) ? n[type] : "unknown exception";
}

//The PC of an exception from its LR (row 38: data abort LR - 8, undefined and prefetch abort LR - 4)
static uint32_t ud_exc_pc(uint32_t type, uint32_t lr)
{
  return lr - ((type == 4) ? 8 : (((type == 1) || (type == 3)) ? 4 : 0));
}

static char *s_exc_at(char *p, uint32_t type, uint32_t lr)
{
  p = s_str(p, ud_exc_name(type));
  p = s_str(p, (type == 0) ? ", LR " : " at ");
  return s_hex(p, (type == 0) ? lr : ud_exc_pc(type, lr), 8);
}

static uint32_t ud_link(const ud_input *in, uint32_t i)
{
  return (in->link && (i < in->nlink)) ? in->link[i] : 0;
}

//"Up 27 min."
static int ud_tile_up(ud_tile *t, uint32_t minutes)
{
  char s[24], *p;

  p = s_str(s, "Up ");
  p = s_minutes(p, minutes);
  s_str(p, ".");
  return ud_tile_say(t, s);
}

//"once", "2 times"
static char *s_times(char *p, uint32_t n)
{
  if(n == 1)
    return s_str(p, "once");

  p = s_uint(p, n);
  return s_str(p, " times");
}

//A real exception (types 1..6): a crash. Type 0, a jump to the image entry, is a deliberate restart (a firmware load
//over FEL, Enter FEL, an update, a self-restart): normal, never a crash (owner 2026-10-20 18:12)
static int ud_exc_crash(uint32_t type)
{
  return (type >= 1) && (type <= 6);
}

//fw 0.2.100 (owner 2026-10-20 18:12): OK = no crash; Check = a stored record of a crash before this run, Fault = this
//run follows a crash since the power-on (counter 49 > 0 with a real exception in counter 50). Plain words only: the Log
//page has the exception type, LR, PC and fault registers
static void ud_tile_restarts(const ud_input *in, ud_tile *t)
{
  const uint8_t *h = in->health;
  uint32_t n = ud_link(in, 49), up = g32(h + H_UPTIME) / 60000, type = ud_link(in, 50);
  char s[UD_TEXT_LEN], *p;

  if(n && ud_exc_crash(type))
  {
    ud_tile_init(t, UD_V_FAULT, "Fault");
    p = s_str(s, "Crashed ");
    p = s_times(p, n);
    p = s_str(p, " this run: ");
    p = s_str(p, ud_exc_name(type));
    p = s_str(p, " at ");
    p = s_hex(p, ud_exc_pc(type, ud_link(in, 51)), 8);
    s_str(p, ".");
    ud_tile_say(t, s);
    ud_tile_say(t, "Details on the Log page.");
    return;
  }

  if(!n && (h[H_REC] & 1) && ud_exc_crash(h[H_REC_TYPE]))
  {
    uint32_t pass, fits = 0;

    //A large count with a long exception name takes three lines: then without the count, so "Up" keeps its line
    for(pass=0;(pass<2) && !fits;pass++)
    {
      ud_tile_init(t, UD_V_CHECK, "Check");
      p = s_str(s, "Crashed ");

      if(!pass)
      {
        p = s_times(p, g16(h + H_REC_COUNT) ? g16(h + H_REC_COUNT) : 1);
        p = s_str(p, " ");
      }

      p = s_str(p, "before this run: ");
      p = s_str(p, ud_exc_name(h[H_REC_TYPE]));
      s_str(p, ".");
      fits = ud_tile_say(t, s);
      fits &= ud_tile_say(t, "If it happens again,");       //broken by hand: "save a" / "report." otherwise
      fits &= ud_tile_say(t, "save a report.");
      fits &= ud_tile_up(t, up);
    }

    return;
  }

  ud_tile_init(t, UD_V_OK, "OK");
  ud_tile_say(t, "No crashes.");
  ud_tile_up(t, up);
}

static void ud_tile_usb(const ud_input *in, ud_tile *t)
{
  const uint8_t *h = in->health;
  uint32_t bad = ud_link(in, 12) + ud_link(in, 13) + ud_link(in, 14) + ud_link(in, 15);
  uint32_t susp = ud_link(in, 36), rec = ud_link(in, 61);

  switch(h[H_USB_STATE])
  {
    case 3:
      ud_tile_init(t, (bad || rec) ? UD_V_CHECK : UD_V_OK, (bad || rec) ? "Check" : "OK");
      s_str(t->line[0], (h[H_USB_FLAGS] & 1) ? "PC connected, high speed." : "PC connected, full speed.");
      break;
    case 4:
      ud_tile_init(t, UD_V_CHECK, "Suspended");
      s_str(t->line[0], "Host asleep or cable out.");
      break;
    case 2:
      ud_tile_init(t, UD_V_NA, "No PC");
      s_str(t->line[0], "Not configured: no PC.");
      break;
    case 1:
      ud_tile_init(t, UD_V_CHECK, "Reconnecting");
      s_str(t->line[0], "Soft reconnect running.");
      break;
    default:
      ud_tile_init(t, UD_V_NA, "Off");
      s_str(t->line[0], "USB is switched off.");
      break;
  }

  //Bad frames: the protocol's CRC, oversize and version errors (counters 12-15); reconnects: the suspend watch's (61)
  s_plural(t->line[1], bad, "bad frame", "bad frames");
  s_plural(t->line[2], susp, "suspend", "suspends");
  s_plural(t->line[3], rec, "reconnect", "reconnects");
}

static void ud_tile_battery(const ud_input *in, ud_tile *t)
{
  char *p;
  int   low = (in->battery < 4) && !in->charging;

  ud_tile_init(t, low ? UD_V_CHECK : UD_V_OK, "");
  p = s_uint(t->word, in->battery);
  s_str(p, " of 20");

  if(low)
  {
    s_str(t->line[0], "Low: readings and SD");
    s_str(t->line[1], "writes may fail.");
  }
  else
    s_str(t->line[0], in->charging ? "Charging." : "On battery.");

  if(in->rtc)
  {
    p = s_str(t->line[low ? 2 : 1], "RTC module ");
    p = s_fix(p, in->temp_c, 1, 0, 0);
    s_str(p, " C.");
  }
}

static void ud_tile_put(ud_page *pg, int i, const char *title, const ud_tile *t)
{
  int x = 12 + UD_TILE_PITCH * i, k;

  ud_boxa(pg, x, UD_TILE_Y, UD_TILE_W, UD_TILE_H, 0);
  ud_txt(pg, x + 10, UD_TILE_Y + 6, UD_TILE_IN, UD_F_SMALL, UD_C_DIM, title);
  ud_txt(pg, x + 10, UD_TILE_Y + 22, UD_TILE_IN, UD_F_HEAD, ud_state_colour(t->state), t->word);

  for(k=0;k<UD_TILE_LINES;k++)
    if(t->line[k][0])
      ud_txt(pg, x + 10, UD_TILE_Y + UD_TILE_LY + UD_TILE_LP * k, UD_TILE_IN, UD_F_SMALL, UD_C_SOFT, t->line[k]);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Calibration: verdict, the values against their limits, provenance

int ud_cal_calibrated(const uint8_t *cal0, const uint8_t *cal1)
{
  return (cal0[C_VERSION] == 3) && (cal1[C_VERSION] == 3);
}

static const uint8_t *ud_prov(const ud_input *in)
{
  return (in->cal[0][C_PROV] == ACAL_PROV_VALID) ? in->cal[0] + 292 : 0;
}

//Core gain split |G_A - G_B| / G_B in tenths of a percent (the gain step's check); -1 without measured gains
static int32_t ud_split_tenths(const uint8_t *c)
{
  const uint8_t *b = c + C_BLOCK;
  uint32_t a, gb;

  if(!(c[C_FLAGS] & F_LOADED) || !(c[C_FLAGS] & F_GAINS))
    return -1;

  a  = g16(b + B_GAIN);
  gb = g16(b + B_GAIN + 2);

  if(!gb)
    return -1;

  return (int32_t)((((a > gb) ? a - gb : gb - a) * 1000 + gb / 2) / gb);
}

//Worst noise RMS over both cores and all ranges, hundredths of a code; -1 without a block
static int32_t ud_noise_hundredths(const uint8_t *c)
{
  const uint8_t *b = c + C_BLOCK + B_NOISE;
  uint32_t i, m = 0;

  if(!(c[C_FLAGS] & F_LOADED))
    return -1;

  for(i=0;i<12;i++)
    if(b[i] > m)
      m = b[i];

  return (int32_t)((m * 100 + 8) / 16);
}


static int ud_over_limits(const uint8_t *c)
{
  return (ud_split_tenths(c) > UD_LIM_SPLIT_TENTHS) || (ud_noise_hundredths(c) > UD_LIM_NOISE);
}

void ud_cal_verdict(const ud_input *in, ud_verdict *v)
{
  const uint8_t *c0 = in->cal[0], *c1 = in->cal[1], *pv = ud_prov(in);
  uint8_t all = c0[C_FLAGS] & c1[C_FLAGS];
  char *p = v->reason;

  v->state = UD_V_CHECK;
  s_str(v->word, "Check");

  if(!(all & F_LOADED))
  {
    v->state = UD_V_FAULT;
    s_str(v->word, "Fault");
    s_str(p, "Uncalibrated: readings may be several % off. Run Calibrate.");
  }
  else if(!(all & F_BOUND))
    s_str(p, "Saved calibration does not match the current offsets: run Calibrate.");
  else if(pv && (g16(pv + 28) != in->fpga))
  {
    p = s_str(p, "FPGA changed since calibration (");
    p = s_hex(p, g16(pv + 28), 4);
    p = s_str(p, " to ");
    p = s_hex(p, in->fpga, 4);
    s_str(p, "): recalibrate.");
  }
  else if(in->health[H_CAP + 2] & PC_CAPF_REPEAT)
    s_str(p, "ADC capture flagged 3 times within a minute: run Calibrate.");
  else if(!(all & F_GAINS))
    s_str(p, "Gains not measured: run Calibrate with the probes on the post.");
  else if((c0[C_FLAGS] | c1[C_FLAGS]) & F_KEPT)
    s_str(p, "Gains carried over from an earlier calibration: run Calibrate.");
  else if(pv && (g32(pv + 32) < UD_WARMUP_MS))
  {
    p = s_str(p, "Calibrated ");
    p = s_uint(p, g32(pv + 32) / 60000);
    s_str(p, " min after power-on: recalibrate after 5 minutes of warm-up.");
  }
  else if(ud_over_limits(c0) || ud_over_limits(c1))
    s_str(p, "A value is over its limit (below): move chargers and the USB cable away and calibrate again.\n"
             "If it stays, the input stage is noisy.");
  else
  {
    v->state = UD_V_OK;
    s_str(v->word, "OK");
    s_str(p, "Calibrated, every value within its limit.");
  }
}

void ud_cal_provenance(const ud_input *in, char *p)
{
  const uint8_t *c = in->cal[0], *pv = ud_prov(in);
  uint32_t up, ons;

  if(!((c[C_FLAGS] | in->cal[1][C_FLAGS]) & F_LOADED))
  {
    s_str(p, "No calibration on the card: 1.00o5's defaults are in use.");
    return;
  }

  if(c[C_PROV] == ACAL_PROV_BAD)
  {
    s_str(p, "The record of when it was calibrated is unreadable.");
    return;
  }

  if(!pv)
  {
    s_str(p, "Calibrated before fw 0.2.52: no record of when.");
    return;
  }

  up  = g32(pv + 32);
  ons = g32(c + C_ONS);

  p = s_str(p, "Calibrated on fw ");
  p = s_ver(p, (const char *)pv + 12);
  p = s_str(p, ", FPGA ");
  p = s_hex(p, g16(pv + 28), 4);

  if(pv[31])
  {
    p = s_str(p, ", 20");
    p = s_dec2(p, pv[44]);
    *p++ = '-';
    p = s_dec2(p, pv[45]);
    *p++ = '-';
    p = s_dec2(p, pv[46]);
  }

  if(up < 60000)
    p = s_str(p, ", under a minute after power-on");
  else
  {
    p = s_str(p, ", ");
    p = s_uint(p, up / 60000);
    p = s_str(p, " min after power-on");
  }

  if(ons == 0)
    p = s_str(p, ", this power-on");
  else if(ons != 0xFFFFFFFFu)
  {
    p = s_str(p, ", ");
    p = s_plural(p, ons, "power-on ago", "power-ons ago");
  }

  s_str(p, ".");
}

//One table cell: "1.1 % OK" (value, then the verdict word in its colour)
static void ud_cell(ud_page *pg, int x, int y, int32_t v, uint32_t denom, uint32_t dec, const char *unit, int32_t limit,
                    const char *na)
{
  char t[32], *p;
  int  over;

  if(v < 0)
  {
    ud_txt(pg, x, y, 210, UD_F_BODY, UD_C_DIM, na);
    return;
  }

  over = v > limit;
  p = s_fix(t, v, denom, dec, 0);
  s_str(p, unit);
  ud_txt(pg, x, y, 150, UD_F_BODY, UD_C_TEXT, t);
  ud_txt(pg, x + 8 + (int)ui_text_width(&font_3, t), y, 60, UD_F_BODY, over ? UD_C_WARN : UD_C_OK, over ? "Check" : "OK");
}

#define UD_CAL_Y      230
#define UD_CAL_H      182

//"limit 1.0 code" from a limit in hundredths of a code
static char *s_limit_code(char *p, int32_t hundredths)
{
  p = s_str(p, "limit ");
  p = s_fix(p, hundredths, 100, 1, 0);
  return s_str(p, " code");
}

static void ud_status_cal(ud_page *pg, const ud_input *in)
{
  //fw 0.2.100 (owner 2026-10-20 18:13): the noise row is the zero step's open-input condition, with its limit. fw
  //0.2.105: one clock class, so the class gain ratio and the gated noise columns became the core gain split and one noise
  static const char *heads[2] = { "Core gain split, A / B", "Noise RMS, inputs open" };
  ud_verdict v;
  char t[UD_TEXT_LEN], *nl;
  uint32_t ch, i;
  int x, y = UD_CAL_Y;

  ud_boxa(pg, 12, y, 776, UD_CAL_H, 0);
  ud_cal_verdict(in, &v);
  ud_txt(pg, 22, y + 10, 80, UD_F_SMALL, UD_C_DIM, "Calibration");
  ud_txt(pg, 104, y + 8, 60, UD_F_BODY, ud_state_colour(v.state), v.word);

  //The reason, one line or two (broken by hand at a newline)
  for(nl=v.reason;*nl && (*nl != '\n');nl++)
    ;

  if(*nl)
  {
    *nl = 0;
    ud_txt(pg, 170, y + 25, 608, UD_F_SMALL, UD_C_SOFT, nl + 1);
  }

  ud_txt(pg, 170, y + 10, 608, UD_F_SMALL, UD_C_SOFT, v.reason);

  for(i=0;i<2;i++)
  {
    x = 80 + 232 * (int)i;
    ud_txt(pg, x, y + 46, 224, UD_F_SMALL, UD_C_DIM, heads[i]);

    if(i == 0)
    {
      char *q = s_str(t, "limit ");

      q = s_fix(q, UD_LIM_SPLIT_TENTHS, 10, 1, 0);
      s_str(q, " %");
    }
    else
      s_limit_code(t, UD_LIM_NOISE);

    ud_txt(pg, x, y + 62, 224, UD_F_SMALL, UD_C_DIM, t);
  }

  for(ch=0;ch<2;ch++)
  {
    const uint8_t *c = in->cal[ch];
    int yy = y + 86 + 28 * (int)ch;

    ud_txt(pg, 22, yy, 50, UD_F_BODY, ch ? UD_C_CH2 : UD_C_CH1, ch ? "CH2" : "CH1");

    if(!(c[C_FLAGS] & F_LOADED))
    {
      ud_txt(pg, 80, yy, 690, UD_F_BODY, UD_C_DIM, "not calibrated");
      continue;
    }

    ud_cell(pg, 80, yy, ud_split_tenths(c), 10, 1, " %", UD_LIM_SPLIT_TENTHS, "gains not measured");
    ud_cell(pg, 312, yy, ud_noise_hundredths(c), 100, 2, " code", UD_LIM_NOISE, "");
  }

  ud_cal_provenance(in, t);
  ud_txt(pg, 22, y + 152, 756, UD_F_SMALL, UD_C_SOFT, t);
}

static void ud_note_or_hint(ud_page *pg, const ud_note *note, int x, const char *hint)
{
  if(note && note->text[0])
    ud_txt(pg, x, UD_BTN_Y + 14, 788 - x, UD_F_SMALL, note->colour, note->text);
  else if(hint)
    ud_txt_r(pg, 788, UD_BTN_Y + 14, 788 - x, UD_F_SMALL, UD_C_DIM, hint);
}

static void ud_page_status(ud_page *pg, const ud_input *in, const ud_note *note)
{
  static const char *titles[4] = { "Firmware", "Restarts", "USB", "Battery" };
  char t[UD_TEXT_LEN];
  ud_tile tile;
  int i, colour;

  ud_identity_line(in, t);
  ud_txt(pg, 12, 58, 776, UD_F_SMALL, UD_C_SOFT, t);
  colour = ud_fpga_line(in, t);
  ud_txt(pg, 12, 75, 776, UD_F_SMALL, colour, t);

  for(i=0;i<4;i++)
  {
    switch(i)
    {
      case 0:  ud_tile_firmware(in, &tile); break;
      case 1:  ud_tile_restarts(in, &tile); break;
      case 2:  ud_tile_usb(in, &tile); break;
      default: ud_tile_battery(in, &tile); break;
    }

    ud_tile_put(pg, i, titles[i], &tile);
  }

  ud_status_cal(pg, in);

  //No calibration on a bitstream the firmware is not built for (it would run the acquisition): the report still saves
  if(!in->mismatch)
    ud_btn(pg, 12, UD_BTN_Y, 140, UD_BTN_H, UD_B_PRIMARY, UD_ID_CALIBRATE, "Calibrate");
  ud_btn(pg, 164, UD_BTN_Y, 196, UD_BTN_H, UD_B_NEUTRAL, UD_ID_REPORT, "Save report to card");
  ud_note_or_hint(pg, note, 376, "Updates every second");
}

//----------------------------------------------------------------------------------------------------------------------------------
// Calibration page: the raw numbers per channel

static const char *ud_range_names[ACAL_RANGES] = { "5 V", "2.5 V", "1 V", "500 mV", "200 mV", "100 mV" };

#define UD_CB_Y   96
#define UD_CB_W   382
#define UD_CB_H   270

static void ud_cal_channel(ud_page *pg, const ud_input *in, uint32_t ch)
{
  const uint8_t *c = in->cal[ch], *b = c + C_BLOCK;
  int x = 12 + 394 * (int)ch, x0 = x + 10, y = UD_CB_Y, loaded = c[C_FLAGS] & F_LOADED, yy;
  char t[UD_TEXT_LEN], *p;
  uint32_t r, k, flags = c[C_TRK];

  ud_boxa(pg, x, y, UD_CB_W, UD_CB_H, 0);
  ud_txt(pg, x0, y + 6, 40, UD_F_BODY, ch ? UD_C_CH2 : UD_C_CH1, ch ? "CH2" : "CH1");
  ud_txt(pg, x0 + 44, y + 8, 318, UD_F_SMALL, UD_C_DIM, "per hardware range (V/div); zeros in codes, core A / B");

  yy = y + 30;
  ud_txt(pg, x0, yy, 52, UD_F_SMALL, UD_C_DIM, "Range");
  ud_txt_r(pg, x0 + 86, yy, 34, UD_F_SMALL, UD_C_DIM, "PWM");
  ud_txt_r(pg, x0 + 144, yy, 52, UD_F_SMALL, UD_C_DIM, "Volts");
  ud_txt(pg, x0 + 156, yy, 100, UD_F_SMALL, UD_C_DIM, "Zero");

  for(r=0;r<ACAL_RANGES;r++)
  {
    int32_t pwm = (int32_t)g16(c + C_OFFSET + 2 * r), vf = (int32_t)g32(c + C_VOLTS + 4 * r), dv = 0, def;

    yy = y + 46 + 15 * (int)r;
    ud_txt(pg, x0, yy, 52, UD_F_SMALL, UD_C_SOFT, ud_range_names[r]);

    s_uint(t, (uint32_t)pwm);
    ud_txt_r(pg, x0 + 86, yy, 34, UD_F_SMALL, ((pwm < 860 - UD_LIM_PWM_DEV) || (pwm > 860 + UD_LIM_PWM_DEV)) ?
             UD_C_WARN : UD_C_TEXT, t);

    def = in->volts_default ? in->volts_default[r] : 0;

    if(def > 0)
    {
      dv = (int32_t)(((int64_t)(vf - def) * 1000) / def);
      p = s_fix(t, dv, 10, 1, 1);
      s_str(p, " %");
      ud_txt_r(pg, x0 + 144, yy, 56, UD_F_SMALL, ((dv > UD_LIM_VOLTS_TENTHS) || (dv < -UD_LIM_VOLTS_TENTHS)) ?
               UD_C_WARN : UD_C_TEXT, t);
    }

    if(loaded)
    {
      p = t;
      for(k=0;k<2;k++)
      {
        p = s_fix(p, (int16_t)g16(b + B_ZERO + 2 * (6 * k + r)), 16, 1, 0);
        if(!k)
          p = s_str(p, " / ");
      }
    }
    else
      s_str(t, "-");

    ud_txt(pg, x0 + 156, yy, 100, UD_F_SMALL, loaded ? UD_C_TEXT : UD_C_DIM, t);
  }

  yy = y + 46 + 15 * ACAL_RANGES + 8;

  if(!loaded)
  {
    ud_txt(pg, x0, yy, 362, UD_F_SMALL, UD_C_WARN, "No calibration block: run Calibrate.");
    yy += 16;
  }
  else
  {
    //Gains per core (their mean is the volts calibration's scale)
    p = s_str(t, "Gains A ");
    p = s_fix(p, (int32_t)g16(b + B_GAIN), 32768, 4, 0);
    p = s_str(p, ", B ");
    p = s_fix(p, (int32_t)g16(b + B_GAIN + 2), 32768, 4, 0);

    if(b[B_GRANGE] == ACAL_GAIN_NONE)
      s_str(p, ", not measured");
    else
    {
      p = s_str(p, ", range ");
      p = s_str(p, ud_range_names[b[B_GRANGE] % ACAL_RANGES]);
      s_str(p, (c[C_FLAGS] & F_KEPT) ? ", carried over" : "");
    }

    ud_txt(pg, x0, yy, 362, UD_F_SMALL, (c[C_FLAGS] & F_GAINS) ? UD_C_TEXT : UD_C_DIM, t);
    yy += 16;

    //fw 0.2.100: the zero step's open-input condition and its limit, as on the Status page
    p = s_str(t, "Noise RMS, inputs open: ");
    p = s_fix(p, ud_noise_hundredths(c), 100, 2, 0);
    p = s_str(p, " code, ");
    s_limit_code(p, UD_LIM_NOISE);
    ud_txt(pg, x0, yy, 362, UD_F_SMALL, ud_over_limits(c) ? UD_C_WARN : UD_C_TEXT, t);
    yy += 16;
  }

  //Calibration v4 tracking (acal_live.c): the A - B correction from the 200 MSa/s records
  if(flags & 0x01)
  {
    p = s_str(t, "Tracking A-B ");
    p = s_fix(p, (int16_t)g16(c + C_TRK + 2), 256, 2, 1);
    p = s_str(p, " code, slope ");
    p = s_fix(p, (int32_t)g32(c + C_TRK + 4), 167772, 2, 1);      //e x 100 / 2^24
    p = s_str(p, " %, ");
    p = s_uint(p, g32(c + C_TRK + 12) / 1000);
    s_str(p, " s ago");
  }
  else
    s_str(t, "Tracking A-B: no full-rate record yet");

  ud_txt(pg, x0, yy, 362, UD_F_SMALL, (flags & 0x01) ? UD_C_TEXT : UD_C_DIM, t);
  yy += 16;

  p = s_str(t, "DC shift: centre ");
  p = s_uint(p, g32(c + C_SHIFT));
  p = s_str(p, ", size ");
  p = s_fix(p, (int32_t)g32(c + C_SHIFT + 4), 1, 0, 0);
  p = s_str(p, ", value ");
  s_fix(p, (int32_t)g32(c + C_SHIFT + 8), 1, 0, 0);
  ud_txt(pg, x0, yy, 362, UD_F_SMALL, UD_C_TEXT, t);
  yy += 16;

  if(c[C_VERSION] == 3)
    s_str(t, "1.00o5 core compensations: not used (v3 in use)");
  else
  {
    p = s_str(t, "1.00o5 core compensations in use: A ");
    p = s_fix(p, (int16_t)g16(c + C_COMP), 1, 0, 0);
    p = s_str(p, ", B ");
    s_fix(p, (int16_t)g16(c + C_COMP + 2), 1, 0, 0);
  }

  ud_txt(pg, x0, yy, 362, UD_F_SMALL, UD_C_SOFT, t);
}

//fw 0.2.79: FPGA 0x153A capture stages (GET_HEALTH 104..123, GET_CAL 356..363): the stages in effect, where they come
//from, and the self-check of the last 200 MSa/s capture as a residual against its limit (a core's outliers beyond its
//chip partner's, cap_stage.h). 0 when the bitstream has no stages (nothing shown)
static int ud_cap_line(const ud_input *in, char *p)
{
  const uint8_t *h = in->health + H_CAP;
  uint16_t cnt[CS_CORES], ex[CS_CORES];
  uint32_t k, n = 0;
  int colour = UD_C_SOFT;

  if(!(h[2] & PC_CAPF_HAS))
    return 0;

  p = s_str(p, "ADC capture stages ");
  p = cs_text(p, (uint16_t)g16(h));

  //fw 0.2.105: the runtime guard moved a flagged core to another stage of its trained clean set (event 23)
  if((h[2] & (PC_CAPF_TRAINED | PC_CAPF_MOVED)) == (PC_CAPF_TRAINED | PC_CAPF_MOVED))
    p = s_str(p, " (trained, a flagged core moved): ");
  else if(h[2] & PC_CAPF_TRAINED)
    p = s_str(p, " (trained): ");
  else if(h[2] & PC_CAPF_OTHER)
  {
    p = s_str(p, " (trained on ");
    p = s_hex(p, g16(in->cal[0] + C_CAP + 4), 4);
    p = s_str(p, ": calibrate): ");
    colour = UD_C_WARN;
  }
  else
    p = s_str(p, " (not trained: calibrate): ");

  if(!(h[2] & PC_CAPF_READ))
  {
    s_str(p, "no 200 MSa/s capture yet");
    return colour;
  }

  //fw 0.2.81: flagged 3 times in a minute (since the last saved training) asks for Calibrate even after a clean capture
  if(!(h[2] & PC_CAPF_FLAGGED))
  {
    if(h[2] & PC_CAPF_REPEAT)
    {
      s_str(p, "last capture clean, flagged 3 times in a minute: calibrate");
      return UD_C_WARN;
    }

    s_str(p, "capture clean");
    return colour;
  }

  for(k=0;k<CS_CORES;k++)
    cnt[k] = (uint16_t)g16(h + 4 + 2 * k);

  cs_excess(cnt, ex);

  for(k=0;k<CS_CORES;k++)
    if(h[3] & (1u << k))
    {
      if(n++)
        p = s_str(p, ", ");
      p = cs_core_name(p, k);
      p = s_str(p, ": ");
      p = s_plural(p, ex[k], "outlier", "outliers");
    }

  if(h[2] & PC_CAPF_REPEAT)
    s_str(p, "; calibrate");

  return UD_C_WARN;
}

static const char *ud_block_state(const uint8_t *c)
{
  if(c[C_VERSION] == 3)
    return "in use";

  return (c[C_FLAGS] & F_LOADED) ? "not bound to the offsets" : "none";
}

#define UD_CAL_BTN_W    185                             //(776 - 3 x 12) / 4
#define UD_CAL_BTN_X(i) (12 + (i) * (UD_CAL_BTN_W + 12))    //12, 209, 406, 603 (the last ends at 788)
#define UD_CAL_TEXT_Y   374                             //under the channel blocks (y 96..366), over the row (420)

static void ud_page_cal(ud_page *pg, const ud_input *in, const ud_note *note)
{
  char t[UD_TEXT_LEN], *p;
  const uint8_t *pv = ud_prov(in);

  p = s_str(t, "Calibration block (sector 708): CH1 ");
  p = s_str(p, ud_block_state(in->cal[0]));
  p = s_str(p, ", CH2 ");
  p = s_str(p, ud_block_state(in->cal[1]));
  s_str(p, ".");
  ud_txt(pg, 12, 58, 776, UD_F_SMALL, UD_C_SOFT, t);

  ud_cal_provenance(in, t);
  ud_txt(pg, 12, 75, 776, UD_F_SMALL, (pv && (g16(pv + 28) != in->fpga)) ? UD_C_WARN : UD_C_SOFT, t);

  ud_cal_channel(pg, in, 0);
  ud_cal_channel(pg, in, 1);

  //fw 0.2.68 (owner): the service actions in one row at the bottom, as on the other tabs (UD_BTN_Y), four equal 44 px
  //buttons 12 px apart across the page: Calibrate, the DC shift trim (1.00o5's manual trim of the trace scale, run on
  //the scope's CTRL page while on; green on, grey off), Volts calibration, and last Clear calibration. The trim's line
  //(only while on) and the page's note sit between the channel blocks and the row.
  //fw-one: the measuring actions need the built-for bitstream; Clear calibration touches the card only
  if(!in->mismatch)
  {
    ud_btn(pg, UD_CAL_BTN_X(0), UD_BTN_Y, UD_CAL_BTN_W, UD_BTN_H, UD_B_PRIMARY, UD_ID_CALIBRATE, "Calibrate");
    ud_btn(pg, UD_CAL_BTN_X(1), UD_BTN_Y, UD_CAL_BTN_W, UD_BTN_H, in->dc_shift ? UD_B_ON : UD_B_GREY, UD_ID_DCSHIFT,
           "DC shift trim");
    ud_btn(pg, UD_CAL_BTN_X(2), UD_BTN_Y, UD_CAL_BTN_W, UD_BTN_H, UD_B_NEUTRAL, UD_ID_VOLTS, "Volts calibration");
  }
  ud_btn(pg, UD_CAL_BTN_X(3), UD_BTN_Y, UD_CAL_BTN_W, UD_BTN_H, UD_B_DANGER, UD_ID_CLEAR, "Clear calibration");

  //fw 0.2.79: the 0x153A capture line first, then the trim's line and the note, 15 px apart (the row starts at 420)
  {
    int y = UD_CAL_TEXT_Y, c = ud_cap_line(in, t);

    if(c)
    {
      ud_txt(pg, 12, y, 776, UD_F_SMALL, c, t);
      y += 15;
    }

    if(in->dc_shift)
    {
      ud_txt(pg, 12, y, 776, UD_F_SMALL, UD_C_WARN, "On: CTRL on the scope opens the trim.");
      y += 15;
    }

    if(note && note->text[0])
      ud_txt(pg, 12, y, 776, UD_F_SMALL, note->colour, note->text);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Log page

#define UD_LOG_W     480
#define UD_RCOL_X    500
#define UD_RCOL_W    288

static void ud_log_restart(ud_page *pg, const ud_input *in, int y)
{
  const uint8_t *h = in->health;
  uint32_t n = ud_link(in, 49), type = ud_link(in, 50);
  char t[UD_TEXT_LEN], *p;
  int x = UD_RCOL_X + 10;

  ud_boxa(pg, UD_RCOL_X, y, UD_RCOL_W, 100, 0);
  ud_txt(pg, x, y + 8, 280, UD_F_SMALL, UD_C_DIM, "Last restart");

  if(n)
  {
    p = s_str(t, "Restarted ");
    s_plural(p, n, "time since power-on", "times since power-on");
    ud_txt(pg, x, y + 26, 280, UD_F_BODY, UD_C_FAULT, t);
    s_exc_at(t, type, ud_link(in, 51));
    ud_txt(pg, x, y + 48, 280, UD_F_SMALL, UD_C_SOFT, t);

    if((type == 3) || (type == 4))
    {
      p = s_str(t, "fault status ");
      p = s_hex(p, ud_link(in, 58), 3);
      if(type == 4)
      {
        p = s_str(p, ", address ");
        s_hex(p, ud_link(in, 59), 8);
      }
    }
    else
    {
      p = s_str(t, "at _reset LR ");
      s_hex(p, ud_link(in, 54), 8);
    }
    ud_txt(pg, x, y + 64, 280, UD_F_SMALL, UD_C_SOFT, t);
  }
  else
  {
    ud_txt(pg, x, y + 26, 280, UD_F_BODY, UD_C_OK, "None since power-on");

    if(h[H_REC] & 1)
    {
      p = s_str(t, "Stored from power-on #");
      p = s_uint(p, g32(h + H_REC_PON));
      s_str(p, ":");
      ud_txt(pg, x, y + 48, 280, UD_F_SMALL, UD_C_WARN, t);
      s_exc_at(t, h[H_REC_TYPE], g32(h + H_REC_LR));
      ud_txt(pg, x, y + 64, 280, UD_F_SMALL, UD_C_WARN, t);
    }
    else
    {
      ud_txt(pg, x, y + 48, 280, UD_F_SMALL, UD_C_SOFT, "A restart records its cause, program");
      ud_txt(pg, x, y + 64, 280, UD_F_SMALL, UD_C_SOFT, "address and fault address here.");
    }
  }
}

static void ud_log_sd(ud_page *pg, const ud_input *in, int y)
{
  const pc_card_id *c = in->card;
  const uint8_t *h = in->health;
  char t[UD_TEXT_LEN], *p;
  int x = UD_RCOL_X + 10, colour = UD_C_WARN;

  ud_boxa(pg, UD_RCOL_X, y, UD_RCOL_W, 100, 0);
  ud_txt(pg, x, y + 8, 280, UD_F_SMALL, UD_C_DIM, "SD card");

  if(in->sd_sectors == 0)
    s_str(t, "No card: settings will not be saved");
  else if(!c || (c->state == PC_ID_NOT_READ))
  {
    s_str(t, "Card image not read yet");
    colour = UD_C_DIM;
  }
  else if(ud_card_known(c) && c->same)
  {
    s_str(t, "Card image is the running image");
    colour = UD_C_OK;
  }
  else if(ud_card_known(c) && c->version[0])
  {
    p = s_str(t, "Card image ");
    s_ver(p, c->version);
  }
  else
    s_str(t, (c->state == PC_ID_NO_IMAGE) ? "No open firmware at sector 750" : "Card image unknown");

  ud_txt(pg, x, y + 24, 280, UD_F_BODY, colour, t);

  if(in->sd_sectors)
  {
    p = s_str(t, "Running ");
    p = s_ver(p, in->run->version);
    p = s_str(p, ", ");
    p = s_fix(p, (int32_t)(in->sd_sectors / 1953), 1000, 1, 0);     //512-byte sectors -> GB
    s_str(p, " GB card.");
    ud_txt(pg, x, y + 46, 280, UD_F_SMALL, UD_C_SOFT, t);
    ud_txt(pg, x, y + 62, 280, UD_F_SMALL, UD_C_SOFT, "Settings and calibration");
    ud_txt(pg, x, y + 78, 280, UD_F_SMALL, UD_C_SOFT, (h[H_SETTINGS] & 1) ? "changed: saved at the next power-off." : "unchanged since the last save.");
  }
}

static void ud_log_unit(ud_page *pg, const ud_input *in, int y)
{
  static const char *ends[3] = { "Previous power-on: no record yet.", "Previous power-on: saved at power-off.",
                                 "Previous power-on: ended unsaved." };
  const uint8_t *h = in->health;
  char t[UD_TEXT_LEN], *p;
  int x = UD_RCOL_X + 10;

  ud_boxa(pg, UD_RCOL_X, y, UD_RCOL_W, 134, 0);
  ud_txt(pg, x, y + 8, 280, UD_F_SMALL, UD_C_DIM, "Unit");

  if(g32(h + H_POWER_ON))
  {
    p = s_str(t, "Power-on #");
    p = s_uint(p, g32(h + H_POWER_ON));
    p = s_str(p, ", up ");
  }
  else
    p = s_str(t, "Power-on count unknown, up ");
  p = s_minutes(p, g32(h + H_UPTIME) / 60000);
  s_str(p, ".");
  ud_txt(pg, x, y + 28, 280, UD_F_SMALL, UD_C_SOFT, t);

  p = s_str(t, "Operating time ");
  p = s_minutes(p, g32(h + H_MINUTES));
  s_str(p, ".");
  ud_txt(pg, x, y + 46, 280, UD_F_SMALL, UD_C_SOFT, t);

  ud_txt(pg, x, y + 64, 280, UD_F_SMALL, (h[H_LAST_END] == 2) ? UD_C_WARN : UD_C_SOFT, ends[(h[H_LAST_END] < 3) ?
         h[H_LAST_END] : 0]);
  ud_txt(pg, x, y + 82, 280, UD_F_SMALL, (h[H_LIFE] & 0x08) ? UD_C_SOFT : UD_C_WARN, (h[H_LIFE] & 0x08) ?
         "Settings sector valid at boot." : "Settings sector invalid: defaults.");
}

static void ud_page_log(ud_page *pg, const ud_input *in, const ud_note *note)
{
  char t[UD_TEXT_LEN], *p;
  ud_text *ev;
  uint32_t i;
  int y;

  ud_boxa(pg, 12, 60, UD_LOG_W, 350, 0);
  ud_txt(pg, 22, 68, 140, UD_F_SMALL, UD_C_DIM, "USB and link events");
  p = t;

  if(in->nevents)
  {
    p = s_uint(p, in->events_first + 1);
    p = s_str(p, "..");
    p = s_uint(p, in->events_first + in->nevents);
    p = s_str(p, " of ");
  }

  p = s_uint(p, in->events_total);
  s_str(p, ", newest first");
  ud_txt(pg, 162, 68, 300, UD_F_SMALL, UD_C_DIM, t);

  for(i=0;(i<in->nevents) && (i<UD_LOG_LINES);i++)
  {
    y = 88 + 16 * (int)i;
    p = s_fix(t, (int32_t)(in->event_ms[i] / 100), 10, 1, 0);
    s_str(p, " s");
    ud_txt_r(pg, 96, y, 74, UD_F_SMALL, UD_C_DIM, t);
    ev = ud_txt(pg, 104, y, 366, UD_F_SMALL, UD_C_TEXT, in->event_text[i]);

    if(ev)
      ev->trim = 1;
  }

  if(!in->nevents)
    ud_txt(pg, 22, 88, 448, UD_F_SMALL, UD_C_DIM, "No events logged since power-on.");

  p = s_str(t, "Since power-on: ");
  p = s_plural(p, ud_link(in, 36), "suspend, ", "suspends, ");
  p = s_plural(p, ud_link(in, 37), "resume, ", "resumes, ");
  s_plural(p, ud_link(in, 60), "inferred resume,", "inferred resumes,");
  ud_txt(pg, 22, 372, 448, UD_F_SMALL, UD_C_SOFT, t);
  p = s_plural(t, ud_link(in, 61), "reconnect, ", "reconnects, ");
  s_plural(p, ud_link(in, 12) + ud_link(in, 13) + ud_link(in, 14) + ud_link(in, 15), "bad frame.", "bad frames.");
  ud_txt(pg, 22, 388, 448, UD_F_SMALL, UD_C_SOFT, t);

  ud_log_restart(pg, in, 60);
  ud_log_sd(pg, in, 168);
  ud_log_unit(pg, in, 276);

  ud_btn(pg, 12, UD_BTN_Y, 196, UD_BTN_H, UD_B_NEUTRAL, UD_ID_REPORT, "Save report to card");
  //Paging through the stored entries: Older while there are more, then back to the newest
  if(in->events_more || in->events_first)
    ud_btn(pg, 220, UD_BTN_Y, 160, UD_BTN_H, UD_B_NEUTRAL, UD_ID_OLDER, in->events_more ? "Older events" : "Newest events");
  ud_note_or_hint(pg, note, 392, 0);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Developer page

//fw 0.2.63 (owner, item n): the Debug overlay switch is gone. 1.00o5's dev_mode draws raw numbers (the FPGA's trigger
//address over the top bar, the trigger position, touch coordinates in two menus) that mean nothing to a user and that
//the PC reads through the protocol anyway; the flag stays in the settings for firmware work. A unit that switched it
//on with 0.2.59-0.2.62 gets a row to switch it off, shown only while the flag is set.
static void ud_page_dev(ud_page *pg, const ud_input *in)
{
  ud_button *b;
  int        y = 86;

  ud_txt(pg, 12, 62, 776, UD_F_SMALL, UD_C_SOFT, "For firmware work. Normal use needs none of this.");

  if(in->dev_mode)
  {
    b = ud_btn(pg, 12, y, 776, 56, UD_B_ROW, UD_ID_OVERLAY, "Debug overlay");
    if(b)
    {
      b->note = "Switch off";
      b->note_colour = UD_C_WARN;
    }
    ud_txt(pg, 24, y + 30, 640, UD_F_SMALL, UD_C_DIM, "Developer flag: raw trigger address and touch coordinates on the scope");
    y += 64;
  }

  b = ud_btn(pg, 12, y, 776, 56, UD_B_ROW, UD_ID_FEL, "Enter FEL now");
  if(b)
  {
    b->note = "asks first";
    b->note_colour = UD_C_WARN;
  }
  ud_txt(pg, 24, y + 30, 640, UD_F_SMALL, UD_C_DIM, "Restart into the USB boot loader (sunxi-fel); the card is not touched");
}

void ud_build(ud_page *pg, int page, const ud_input *in, const ud_note *note)
{
  ud_clear(pg);
  ud_header(pg, page);

  switch(page)
  {
    case UD_PAGE_CAL: ud_page_cal(pg, in, note); break;
    case UD_PAGE_LOG: ud_page_log(pg, in, note); break;
    case UD_PAGE_DEV: ud_page_dev(pg, in); break;
    default:          ud_page_status(pg, in, note); break;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// First run (plan section 6, FirstRun mockup): what the unit found, what to do, Later left, Calibrate now right

void ud_build_first_run(ud_page *pg, const ud_input *in, int settings_defaults, int volts_due)
{
  const uint8_t *c0 = in->cal[0], *c1 = in->cal[1];
  char t[UD_TEXT_LEN], f[UD_TEXT_LEN], *p;
  int y = 104, colour;

  ud_clear(pg);
  ud_boxa(pg, 0, 0, 800, UD_HDR_H, 1);
  ud_txt(pg, 14, 14, 300, UD_F_HEAD, UD_C_TITLE, "First start");

  p = s_str(t, "Open firmware ");
  p = s_ver(p, in->run->version);
  p = s_str(p, ", protocol ");
  p = s_uint(p, in->run->proto_major);
  *p++ = '.';
  p = s_uint(p, in->run->proto_minor);
  p = s_str(p, ". FPGA bitstream ");
  p = s_hex(p, in->fpga, 4);
  s_str(p, ".");
  ud_txt(pg, 24, 66, 752, UD_F_SMALL, UD_C_SOFT, t);

  //Check lines: the first character picks the icon. The FPGA: "FPGA recognised: AL3A10 bitstream 0x153b: ..." (a
  //mismatch never reaches this screen: the notice screen comes first, ui_fpga_mismatch_pass)
  colour = ud_fpga_line(in, f);
  if(colour == UD_C_WARN)
    p = s_str(t, UD_ICON_WARN);
  else
    p = s_str(t, UD_ICON_OK "FPGA recognised: ");
  s_str(p, (colour == UD_C_WARN) ? f : f + 5);
  ud_txt(pg, 24, y, 752, UD_F_BODY, (colour == UD_C_WARN) ? UD_C_WARN : UD_C_TEXT, t);
  y += 36;

  ud_txt(pg, 24, y, 752, UD_F_BODY, UD_C_TEXT, settings_defaults ? UD_ICON_OK "No saved settings on the card: defaults used." :
         UD_ICON_OK "Settings loaded from the card.");
  y += 36;

  if(((c0[C_FLAGS] & c1[C_FLAGS]) & F_LOADED) && !((c0[C_FLAGS] & c1[C_FLAGS]) & F_BOUND))
    ud_txt(pg, 24, y, 752, UD_F_BODY, UD_C_WARN,
           UD_ICON_WARN "Saved calibration does not match the current offsets: calibrate again.");
  else
    ud_txt(pg, 24, y, 752, UD_F_BODY, UD_C_WARN, UD_ICON_WARN "Calibration missing: readings may be several % off.");
  y += 36;

  if(volts_due)
  {
    ud_txt(pg, 24, y, 752, UD_F_BODY, UD_C_WARN,
           UD_ICON_WARN "Volts calibration missing too: it needs a DC supply up to 15 V.");
    y += 36;
  }

  ud_boxa(pg, 24, y + 10, 752, 1, 2);
  y += 28;
  ud_txt(pg, 24, y, 752, UD_F_BODY, UD_C_TEXT, "Calibrate after the scope has been on for 5 minutes.");
  ud_txt(pg, 24, y + 24, 752, UD_F_BODY, UD_C_TEXT, "The zero step takes 8 seconds and needs no probes.");
  ud_txt(pg, 24, y + 48, 752, UD_F_BODY, UD_C_TEXT, "The gain step needs a probe on the calibration post, one channel at a time.");
  ud_txt(pg, 24, y + 84, 752, UD_F_SMALL, UD_C_SOFT, "Later opens the scope. Calibrate is in Menu, Diagnostics.");

  ud_btn(pg, 24, 404, 160, 52, UD_B_GREY, UD_ID_LATER, "Later");
  ud_btn(pg, 576, 404, 200, 52, UD_B_PRIMARY, UD_ID_CAL_NOW, "Calibrate now");
}

//----------------------------------------------------------------------------------------------------------------------------------
// FPGA mismatch (fw-one; owner decision 2026-10-20: every firmware version supports the latest bitstream only, with a
// loud notice otherwise). Shown instead of the scope at boot and after every screen it offers; the acquisition, the
// Scope, Spectrum and Frequency response views and the generator never run. Diagnostics (the version word, the report),
// Settings and the USB drive stay reachable from its three buttons.

void ud_build_fpga_mismatch(ud_page *pg, const ud_input *in)
{
  char t[UD_TEXT_LEN], *p;

  ud_clear(pg);
  ud_boxa(pg, 0, 0, 800, UD_HDR_H, 1);
  ud_txt(pg, 14, 14, 400, UD_F_HEAD, UD_C_TITLE, "FPGA bitstream");

  p = s_str(t, "This firmware needs FPGA bitstream ");
  p = s_hexu(p, PC_FW_FPGA_VERSION, 4);
  s_str(p, ".");
  ud_txt(pg, 24, 84, 752, UD_F_BODY, UD_C_WARN, t);

  p = s_str(t, "This scope has ");
  p = s_hexu(p, in->fpga, 4);
  s_str(p, ".");
  ud_txt(pg, 24, 112, 752, UD_F_BODY, UD_C_WARN, t);

  ud_txt(pg, 24, 160, 752, UD_F_BODY, UD_C_TEXT, "Flash it as the guide says (docs/guide/prepare.md),");
  ud_txt(pg, 24, 184, 752, UD_F_BODY, UD_C_TEXT, "or remove the card to run the stock firmware.");

  ud_boxa(pg, 24, 230, 752, 1, 2);

  p = s_str(t, "Open firmware ");
  p = s_ver(p, in->run->version);
  p = s_str(p, ", protocol ");
  p = s_uint(p, in->run->proto_major);
  *p++ = '.';
  p = s_uint(p, in->run->proto_minor);
  s_str(p, ". The scope, the spectrum analyser, the frequency response and the generator do not run");
  ud_txt(pg, 24, 248, 752, UD_F_SMALL, UD_C_SOFT, t);
  ud_txt(pg, 24, 266, 752, UD_F_SMALL, UD_C_SOFT, "until the bitstream matches. A PC gets the version word and the report over USB.");
  ud_txt(pg, 24, 300, 752, UD_F_SMALL, UD_C_SOFT, "Diagnostics shows the version word and saves a report to the card.");
  ud_txt(pg, 24, 318, 752, UD_F_SMALL, UD_C_SOFT, "USB drive opens the card on a PC, to change the firmware image.");

  ud_btn(pg, 24, 404, 200, 52, UD_B_NEUTRAL, UD_ID_DIAG, "Diagnostics");
  ud_btn(pg, 300, 404, 200, 52, UD_B_NEUTRAL, UD_ID_SETTINGS, "Settings");
  ud_btn(pg, 576, 404, 200, 52, UD_B_NEUTRAL, UD_ID_USB, "USB drive");
}
