//----------------------------------------------------------------------------------------------------------------------------------
// ui_diag.h - the Diagnostics pages and the first-run screen as data (fw 0.2.59, docs/plans/ux-makeover.md sections 6, 6b,
// 6c; docs/plans/diagnostics-review.md; docs/ui-guidelines.md). Pure C, no globals, host-buildable (host_tests/test_ui.c).
//
// The builders turn the protocol bodies the PC sees (GET_HEALTH, GET_CAL, GET_LINK_STATS, the card image identity of
// SD_INFO) into a page: outlined boxes, text items with a font and a state colour, and buttons with their hit areas.
// The target view (ui_diag_view.c) draws a page and hit-tests touches against it; the host tests check every text fits
// its box with the device's own font tables and every button meets the 44 x 80 px rule. One source for the screen,
// the report file (pc_report.c) and the MCP: the same bytes.
//
// Verdicts: a word in a state colour next to the reason ("OK", "Check", "Fault"); trust decisions are a value against a
// limit, raw numbers live on the Calibration page; every unavailable state says why (guidelines section 4).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef UI_DIAG_H
#define UI_DIAG_H

#include <stdint.h>
#include "font_structs.h"
#include "pc_ident.h"
#include "adc_cal.h"

//Colours (the target maps them to RGB; docs/plans/ux-makeover.md 6c: black ground, outlined boxes, state words in colour)
#define UD_C_TEXT        0        //normal text
#define UD_C_SOFT        1        //secondary text
#define UD_C_DIM         2        //labels, "not available"
#define UD_C_OK          3        //green: verified or active
#define UD_C_WARN        4        //amber: needs attention
#define UD_C_FAULT       5        //red: fault
#define UD_C_CH1         6
#define UD_C_CH2         7
#define UD_C_TITLE       8        //white headline

//Fonts: the device's 13, 16 and 24 px bitmap fonts (font_2, font_3, font_5)
#define UD_F_SMALL       0
#define UD_F_BODY        1
#define UD_F_HEAD        2

//Button styles
#define UD_B_NEUTRAL     0        //dark blue, outlined
#define UD_B_PRIMARY     1        //green (Calibrate, Calibrate now, Enter FEL)
#define UD_B_DANGER      2        //red (Clear calibration): always behind a confirmation
#define UD_B_TAB         3        //page tab
#define UD_B_TAB_ON      4        //the page shown
#define UD_B_GREY        5        //Later, Cancel in the wizard
#define UD_B_ROW         6        //a full-width settings row (Developer page): the outlined box is the button
#define UD_B_ON          7        //a toggle that is on: green (fw 0.2.63; off is UD_B_GREY), the label stays the same

//Button ids
#define UD_ID_NONE        0
#define UD_ID_TAB_STATUS  1
#define UD_ID_TAB_CAL     2
#define UD_ID_TAB_LOG     3
#define UD_ID_TAB_DEV     4
#define UD_ID_BACK        5
#define UD_ID_CALIBRATE   6
#define UD_ID_REPORT      7
#define UD_ID_VOLTS       8
#define UD_ID_CLEAR       9
#define UD_ID_OLDER      10
#define UD_ID_OVERLAY    11
#define UD_ID_FEL        12
#define UD_ID_LATER      13
#define UD_ID_CAL_NOW    14
#define UD_ID_DCSHIFT    15

//Pages
#define UD_PAGE_STATUS    0
#define UD_PAGE_CAL       1
#define UD_PAGE_LOG       2
#define UD_PAGE_DEV       3
#define UD_PAGES          4

#define UD_TEXT_LEN     128
#define UD_MAX_TEXTS    112
#define UD_MAX_BOXES     12
#define UD_MAX_BUTTONS   10

typedef struct
{
  int16_t x, y, w, h;
  uint8_t fill;                   //0 outlined card, 1 header band, 2 a rule (filled, 1 px high)
} ud_box;

typedef struct
{
  int16_t  x, y;
  uint16_t maxw;                  //the space it was laid out for (the builder cuts a longer text to fit)
  uint8_t  font, colour;
  uint8_t  right;                 //1: x is the right edge
  uint8_t  cut;                   //1: the builder had to shorten it
  uint8_t  trim;                  //1: may be shortened (Log page event lines: the saved report has them whole)
  char     text[UD_TEXT_LEN];
} ud_text;

typedef struct
{
  int16_t     x, y, w, h;         //drawn area = hit area
  uint8_t     style, id;
  const char *label;              //static text, centred
  const char *note;               //UD_B_ROW: the state word at the right ("on", "asks first")
  uint8_t     note_colour;
} ud_button;

typedef struct
{
  uint32_t  nbox, ntext, nbtn;
  ud_box    box[UD_MAX_BOXES];
  ud_text   text[UD_MAX_TEXTS];
  ud_button btn[UD_MAX_BUTTONS];
} ud_page;

//Everything the pages show, as the firmware has it. Bodies as the protocol returns them (docs/protocol.md row 55).
typedef struct
{
  const uint8_t     *health;          //GET_HEALTH body, 104 bytes (PC_HEALTH_LEN)
  const uint8_t     *cal[2];          //GET_CAL bodies, 356 bytes each
  const uint32_t    *link;            //GET_LINK_STATS counters
  uint32_t           nlink;
  const pc_image_id *run;             //the running image
  const pc_card_id  *card;            //the card image (SD_INFO 64..), 0 = not read yet
  uint32_t           sd_sectors;      //0: no card
  uint16_t           fpga;            //FPGA version word (command 0x06), as read
  uint8_t            variant;         //protocol variant byte (2, AL3; the wire encoding of P-5.1)
  uint8_t            mismatch;        //fpga != PC_FW_FPGA_VERSION: the firmware does not run on it (pc_cmds.h)
  uint32_t           caps;            //GET_INFO caps (PC_CAP_*)
  uint8_t            battery;         //0..20
  uint8_t            charging;
  uint8_t            rtc;             //DS3231 module fitted
  int8_t             temp_c;          //its temperature (with rtc)
  const int32_t     *volts_default;   //1.00o5's volts factors per range (signal_adjusters[7])
  const char        *serial;          //USB serial string
  uint8_t            dev_mode;        //Debug overlay on
  uint8_t            dc_shift;        //1.00o5's DC shift trim shown on the CTRL page (dc_shift_cal)
  //Log page: the event log decoded in words (newest first), the time of each in ms
  uint32_t           nevents, events_total, events_first;   //shown, in the log since boot, index of the first shown
  uint8_t            events_more;     //older stored entries exist beyond the ones shown
  const char *const *event_text;
  const uint32_t    *event_ms;
} ud_input;

//Status messages from the actions (Save report, Clear), shown on the page until the next action
typedef struct
{
  char    text[UD_TEXT_LEN];
  uint8_t colour;
} ud_note;

//Page builders. note may be 0. Each starts with the common header (title, four tabs, Back).
void ud_build(ud_page *pg, int page, const ud_input *in, const ud_note *note);
void ud_build_first_run(ud_page *pg, const ud_input *in, int settings_defaults, int volts_due);

//The button under a touch: its id, UD_ID_NONE when none (hit area = drawn area)
int  ud_hit(const ud_page *pg, int x, int y);

//Layout of the Log page: events per screen
#define UD_LOG_LINES     17

//Check lines of the first-run screen start with one of these: the target draws the icon and the text 28 px right of it
#define UD_ICON_OK       ""      //green tick
#define UD_ICON_WARN     ""      //amber disc with "!"
#define UD_ICON_W        28

//Verdicts used by the pages (exposed for the tests)
#define UD_V_OK          0
#define UD_V_CHECK       1
#define UD_V_FAULT       2
#define UD_V_NA          3            //not available (grey)

typedef struct
{
  uint8_t state;                      //UD_V_*
  char    word[16];
  char    reason[UD_TEXT_LEN];
} ud_verdict;

void ud_cal_verdict(const ud_input *in, ud_verdict *v);
void ud_cal_provenance(const ud_input *in, char *p);             //the provenance sentence
int  ud_cal_calibrated(const uint8_t *cal0, const uint8_t *cal1); //both channels use a bound v3 block

//The calibration limits (diagnostics-review.md finding 6). The noise rows measure the zero step's open-input condition,
//so their limits are the zero step's own (adc_cal.h ACAL_MAX_NOISE16_*, owner 2026-10-20 18:13, fw 0.2.100), here in
//hundredths of a code: gated 1.00, full rate 3.00
#define UD_LIM_CLASS_TENTHS      20   //class gain ratio full / gated: |ratio - 1| <= 2.0 %
#define UD_LIM_NOISE_GATED      (ACAL_MAX_NOISE16_GATED * 100 / 16)
#define UD_LIM_NOISE_FULL       (ACAL_MAX_NOISE16_FULL * 100 / 16)
#define UD_LIM_PWM_DEV          100   //PWM centre within 860 +- 100
#define UD_LIM_VOLTS_TENTHS      30   //volts factor within 3.0 % of 1.00o5's default
#define UD_WARMUP_MS         300000   //calibrated less than 5 minutes after power-on: amber

#endif /* UI_DIAG_H */
