//----------------------------------------------------------------------------------------------------------------------------------
// ui_settings.h - the Settings fold-out and the full Settings screen (fw 0.2.55, docs/plans/ux-makeover.md section 5,
// docs/ui-guidelines.md): row tables, geometry, hit areas, the value each row shows and its state colour, the start-up
// (boot-mode byte) choice, the time editor and the confirmation dialog's layout. Pure C, no globals, host-buildable
// (host_tests/test_ui.c). ui_settings_ui.c draws and runs the screen and the dialog, menu.c draws the fold-out,
// statemachine.c hit-tests the fold-out with this table.
//
// Every row maps to a setting that existed before (README section 5 "Screens" lists where each old control went):
//   Screen brightness        scopesettings.screenbrightness (0..100)      fold-out slider, Settings - / +
//   Grid brightness          scopesettings.gridbrightness (0..100)        fold-out slider, Settings - / +
//   File message timeout     scopesettings.confirmationmode (1 = wait for a tap, 0 = 0.7 s)
//   Cursors                  scopesettings.lockcursors (1 = move together, 0 = move freely)
//   Auto setup               scopesettings.new_autosetup (1 = improved, 0 = stock 1.00o5)
//   Stamp screenshots with   tag_in_BMP (the clock time with the clock on, else the file name)
//   Reference traces         scopesettings.ref_on_startup
//   Clock                    onoffRTC (DS3231 module used: top-bar clock, file stamps, FAT times)
//   Time and date            the DS3231 registers (hourUp .. yearDown)
//   At power-on              byte 31 of SD sector 710, DRAM 0x81BFFC1F (Atlan's bootloader)
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include "types.h"

//----------------------------------------------------------------------------------------------------------------------------------
//The fold-out beside the main menu: 1.00o5's column (x 150, 244 wide, from y 46, one row per 59 px), its rows top to
//bottom. Time and date only with a clock module. (fw 0.2.55-0.2.56 had an interim Baseline calibration row; fw 0.2.59
//moved it to Diagnostics, Calibrate. fw 0.2.63 (owner): File message timeout left the fold-out, it is set once and
//lives in Settings > Controls only.)

#define UI_FO_SCREEN          0
#define UI_FO_GRID            1
#define UI_FO_TIME            2
#define UI_FO_OTHER           3
#define UI_FO_MAX_ROWS        4

#define UI_FO_X               150
#define UI_FO_TOP             46
#define UI_FO_W               244
#define UI_FO_PITCH           59

int    ui_fo_rows(int rtc, uint8 *ids);              //the shown rows' ids, top to bottom; returns the count
uint32 ui_fo_height(int rtc);                        //rows x pitch (236 with the clock module, 177 without)
uint32 ui_fo_row_top(int rtc, int id);               //first y of the row's hit area, 0 for a row not shown
int    ui_fo_row_at(int rtc, uint32 x, uint32 y);    //the row id under a touch, -1 outside (rows are contiguous)

//----------------------------------------------------------------------------------------------------------------------------------
//The Settings screen: a header with the title and Back, two columns of groups, Restore default settings at the bottom.

#define UI_SET_SCREEN         0
#define UI_SET_GRID           1
#define UI_SET_FILEMSG        2
#define UI_SET_CURSORS        3
#define UI_SET_AUTOSET        4
#define UI_SET_STAMP          5
#define UI_SET_REFS           6
#define UI_SET_CLOCK          7
#define UI_SET_TIME           8
#define UI_SET_POWERON        9
#define UI_SET_ROWS           10

//Targets that are not rows
#define UI_SET_BACK           UI_SET_ROWS
#define UI_SET_RESTORE        (UI_SET_ROWS + 1)

//What a tap on a row does
#define UI_SET_K_STEP         0     //- and + buttons inside the row (the brightnesses)
#define UI_SET_K_TAP          1     //the whole row toggles or cycles the value
#define UI_SET_K_OPEN         2     //the whole row opens a page (time editor)

//Which part of a target was hit
#define UI_SET_P_ROW          0
#define UI_SET_P_MINUS        1
#define UI_SET_P_PLUS         2

//Geometry
#define UI_SET_HDR_H          52    //header band, title and Back
#define UI_SET_COL0_X         12
#define UI_SET_COL1_X         406
#define UI_SET_ROW_W          382
#define UI_SET_ROW_H          48
#define UI_SET_GROUP_DY       18    //group title 18 px above its first row
#define UI_SET_STEP_W         80    //- / + buttons: 80 x 44, 2 px inside the row's top and bottom, 4 px apart
#define UI_SET_STEP_H         44
#define UI_SET_STEP_GAP       4
#define UI_SET_BACK_X         692
#define UI_SET_BACK_Y         4
#define UI_SET_BACK_W         96
#define UI_SET_BACK_H         44
#define UI_SET_RESTORE_X      12
#define UI_SET_RESTORE_Y      430
#define UI_SET_RESTORE_W      240
#define UI_SET_RESTORE_H      44

typedef struct
{
  const char *label;
  uint8       group;
  uint8       kind;
  uint16      x;
  uint16      y;
} ui_set_row;

typedef struct
{
  const char *title;
  uint16      x;
  uint16      y;            //title line top (13 px font)
} ui_set_group;

#define UI_SET_GROUPS         5
extern const ui_set_row   ui_set_rows[UI_SET_ROWS];
extern const ui_set_group ui_set_groups[UI_SET_GROUPS];

//x of the - button of a step row (the + button follows after UI_SET_STEP_GAP); y is the row's + 2
uint32 ui_set_minus_x(int row);

//The target under a touch: a row (0 .. UI_SET_ROWS - 1), UI_SET_BACK, UI_SET_RESTORE or -1; *part says which part.
//A step row answers only on its two buttons; every hit area is the drawn rectangle.
int ui_set_hit(uint32 x, uint32 y, int *part);

//What the screen shows, gathered by the caller from the firmware's variables
typedef struct
{
  uint8 screen;             //0..100
  uint8 grid;               //0..100
  uint8 filemsg_wait;       //confirmationmode
  uint8 cursors_locked;     //lockcursors
  uint8 autoset_new;        //new_autosetup
  uint8 stamp;              //tag_in_BMP
  uint8 refs;               //ref_on_startup
  uint8 rtc_present;        //a DS3231 answered at boot
  uint8 rtc_on;             //onoffRTC
  uint8 hour, minute, day, month, year;   //year 0..99 = 2000..2099
  uint8 boot;               //boot-mode byte
} ui_set_state;

//State colours: words carry the meaning, the colour the state (docs/ui-guidelines.md section 3)
#define UI_SET_T_ON           0     //green: active / a value in use
#define UI_SET_T_OFF          1     //grey: switched off, not used
#define UI_SET_T_WARN         2     //amber: needs attention
#define UI_SET_T_NA           3     //grey, and the row does nothing: the hardware or a setting it needs is missing

//The value text of a row into buf (at least 32 bytes); returns its tone
int ui_set_value(int row, const ui_set_state *s, char *buf);

//Brightness step: 5 per tap, held to 0..100
uint8 ui_set_step(uint8 v, int up);

//Screen brightness (fw 0.2.68, owner: the top bar's sun and the Screen brightness slider are one setting).
//scopesettings.screenbrightness is the one value (0..100 %, what the backlight shows; the slider and the Settings
//screen show and set it). The sun toggles between 100 % and the value it came from (1.00o5's pair: its sun switched
//the backlight to maximum and back to the slider's value, through a separate flag the slider did not show).
//scopesettings.brightback keeps that value; the settings sector holds it in 1.00o5's max-light byte as 0x80 | value.
#define UI_BRIGHT_MAX         100
#define UI_BRIGHT_DEFAULT     20    //1.00o5's default screen brightness
#define UI_BRIGHT_SECTOR_NEW  0x80  //the max-light byte holds 0x80 | brightback (1.00o5: 0 or 1, the max-light flag)
//The sun tapped at brightness cur: the new brightness; *back is what the next tap returns to
uint8  ui_bright_sun(uint8 cur, uint8 *back);
//The settings sector's byte for brightback, and the load (1.00o5's flag 1: the backlight was at maximum)
uint16 ui_bright_byte(uint8 back);
void   ui_bright_load(uint16 byte, uint8 *cur, uint8 *back);

//----------------------------------------------------------------------------------------------------------------------------------
//At power-on: the boot-mode byte (README section 3). The screen offers three values, in this cycle:
//Open firmware (0), Boot menu, then open firmware (4), Boot menu, then stock firmware (5). The stock image always comes
//with the boot menu. FEL (2, 6) and the updater (3, 7) are never written from here; when the byte holds one of them (or
//1, the stock image without menu, which only the loader tool writes) the row says so in amber and a tap goes to Open
//firmware.
#define UI_BOOT_OPEN          0
#define UI_BOOT_MENU_OPEN     4
#define UI_BOOT_MENU_STOCK    5

uint8       ui_boot_next(uint8 b);
const char *ui_boot_text(uint8 b, int *tone);

//----------------------------------------------------------------------------------------------------------------------------------
//The time editor page (Settings > Time and date): one row per field with - and + buttons, written to the module at once

#define UI_TIME_HOUR          0
#define UI_TIME_MINUTE        1
#define UI_TIME_DAY           2
#define UI_TIME_MONTH         3
#define UI_TIME_YEAR          4
#define UI_TIME_FIELDS        5

#define UI_TIME_TOP           72    //first row's top, one row per 64 px
#define UI_TIME_PITCH         64
#define UI_TIME_LABEL_X       160
#define UI_TIME_MINUS_X       330
#define UI_TIME_VALUE_X       476   //centre of the value, between the buttons
#define UI_TIME_PLUS_X        526
#define UI_TIME_BTN_W         96
#define UI_TIME_BTN_H         44

extern const char *ui_time_labels[UI_TIME_FIELDS];

//The field and direction under a touch: field * 2 + (1 for +), -1 for none; Back is UI_SET_BACK's rectangle
int ui_time_hit(uint32 x, uint32 y);

//----------------------------------------------------------------------------------------------------------------------------------
//Confirmation dialog (docs/ui-guidelines.md section 3, plan section 6b): a title (24 px), one to three lines (16 px),
//two 44 px buttons 16 px apart, the dismissing one left, the action right. Only the buttons react.

#define UI_DLG_W              520
#define UI_DLG_TITLE_DY       18
#define UI_DLG_LINE0_DY       60
#define UI_DLG_LINE_PITCH     22
#define UI_DLG_BTN_W          160
#define UI_DLG_BTN_H          44
#define UI_DLG_BTN_GAP        16
#define UI_DLG_PAD            20

#define UI_DLG_NONE           0
#define UI_DLG_CANCEL         1
#define UI_DLG_ACTION         2

typedef struct
{
  uint16 x, y, w, h;
  uint16 btn_y;
  uint16 cancel_x;
  uint16 action_x;
} ui_dlg;

void ui_dlg_layout(int lines, ui_dlg *d);            //centred on the 800 x 480 screen
int  ui_dlg_hit(const ui_dlg *d, uint32 x, uint32 y);

//The texts of the two confirmations (here so the host test can check they fit the box in the device's fonts)
typedef struct
{
  const char *title;
  const char *lines[3];
  int         nlines;
  const char *cancel;
  const char *action;
  uint8       destructive;      //red action button
} ui_dlg_text;

extern const ui_dlg_text ui_dlg_restore;     //Settings > Restore default settings
extern const ui_dlg_text ui_dlg_delete;      //file browsers and the waveform viewer: delete

//----------------------------------------------------------------------------------------------------------------------------------
//On the device (ui_settings_ui.c)

//The Settings screen, modal until Back; leaves the screen as it found it (rebuilt after Restore default settings)
void ui_settings_run(void);

//The confirmation dialog over the screen; returns 1 for the action, 0 for Cancel. restore: save the screen under the box
//(displaybuffer4) and put it back, else the caller redraws. The display state (buffers, colours, font) is kept.
int  ui_confirm(const ui_dlg_text *t, int restore);

#endif
