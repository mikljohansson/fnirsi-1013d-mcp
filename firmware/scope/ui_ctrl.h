//----------------------------------------------------------------------------------------------------------------------------------
// ui_ctrl.h - the right bar's CTRL page, its fold-outs, the warning line and the spectrum's cards (fw 0.2.56, UX makeover
// step 2, docs/plans/ux-makeover.md section 3, docs/ui-guidelines.md sections 2-3). Pure C, no globals, host-buildable
// (host_tests/test_ui.c): geometry, hit areas, the name tables and the texts. ctrl_page.c draws with them and runs the
// fold-outs' touch loops; spectrum.c and phosphor.c take their value names from here.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef UI_CTRL_H
#define UI_CTRL_H

#include "types.h"

//Right bar buttons: 51 x 50 at x 739 (1.00o5's slots)
#define UI_BAR_X              739
#define UI_BAR_W              51
#define UI_BTN_H              50

//First page slots, top to bottom (1.00o5's order and y). In Spectrum the cursor and measurement slots are empty until
//spectrum markers and the spectrum's own measurement list exist.
#define UI_P1_CTRL            0
#define UI_P1_RUN             1
#define UI_P1_AUTO            2
#define UI_P1_TCURSOR         3
#define UI_P1_VCURSOR         4
#define UI_P1_MEASURES        5
#define UI_P1_SAVEPIC         6
#define UI_P1_SAVEWAVE        7

int ui_p1_shown(int slot, int spectrum);

//CTRL page (page two), the same order in every mode: Display, the mode's analysis control (Trace style in Scope,
//Analyse in Spectrum), Grid, the V+ / CH / V- steppers of both channels, 50 % TRIG (Scope only; the slot stays empty in
//Spectrum, owner). CTRL itself keeps 1.00o5's hit area (y 3..57, scan_for_touch).
#define UI_CTRL_NONE          0
#define UI_CTRL_DISPLAY       1
#define UI_CTRL_ANALYSIS      2
#define UI_CTRL_GRID          3
#define UI_CTRL_CH1_UP        4
#define UI_CTRL_CH1_DOWN      5
#define UI_CTRL_CH2_UP        6
#define UI_CTRL_CH2_DOWN      7
#define UI_CTRL_TRIG50        8

#define UI_CTRL_DISPLAY_Y     65
#define UI_CTRL_ANALYSIS_Y    125
#define UI_CTRL_GRID_Y        185
#define UI_CTRL_STEP1_Y       244
#define UI_CTRL_STEP2_Y       334
#define UI_CTRL_TRIG50_Y      425

//Stepper (owner): a 28 px V+ bar, a 26 px centre in the channel colour reading "CH1" / "CH2", a 28 px V- bar. Each bar's
//hit area runs to the middle of the centre (41 px) and on into its half of the gap to the next control, so V+ and V-
//get 45 px of touch each.
#define UI_STEP_BAR_H         28
#define UI_STEP_MID_H         26
#define UI_STEP_H             (2 * UI_STEP_BAR_H + UI_STEP_MID_H)

//The item under a touch at y on the right bar (x > 730); UI_CTRL_NONE above y 58 (CTRL) and on an empty slot
int ui_ctrl_item_at(uint32 y, int spectrum);

//First and last y (inclusive) of an item's hit area
uint32 ui_ctrl_hit_top(int item);
uint32 ui_ctrl_hit_bottom(int item);

//Fold-outs: a panel left of the bar over the plot's right edge (it never reaches the bar, x < 730), a grey title,
//rows of 44 px at a 50 px pitch, an optional separator before row `sep`. A row's hit area takes half of each gap,
//so the rows' areas join; a touch inside the panel between rows does nothing, a touch outside closes the panel.
#define UI_FOLD_RIGHT         728
#define UI_FOLD_PAD           8
#define UI_FOLD_TITLE         30
#define UI_FOLD_ROW_H         44
#define UI_FOLD_PITCH         50
#define UI_FOLD_SEP           12
#define UI_FOLD_TOP_MIN       48
#define UI_FOLD_BOTTOM_MAX    478
#define UI_FOLD_ROWS_MAX      8

#define UI_FOLD_IN            (-1)
#define UI_FOLD_OUT           (-2)

typedef struct
{
  uint32 x, y, w, h;
  uint32 rows;
  uint32 sep;                 //first row below the separator, 0 = none
} ui_fold;

//Panel w px wide with `rows` rows, its top 13 px above the button at anchor_y, moved up to fit above y 478
void   ui_fold_layout(ui_fold *f, uint32 w, uint32 rows, uint32 sep, uint32 anchor_y);
uint32 ui_fold_row_x(const ui_fold *f);
uint32 ui_fold_row_w(const ui_fold *f);
uint32 ui_fold_row_y(const ui_fold *f, uint32 row);
int    ui_fold_hit(const ui_fold *f, uint32 x, uint32 y);   //row, UI_FOLD_IN or UI_FOLD_OUT
int    ui_fold_dir(const ui_fold *f, uint32 x);              //two-way rows: -1 left half (down), +1 right half (up)

//Row layouts (ctrl_page.c draws them; test_ui checks the texts fit): a value row has its label at the left and the value
//right-aligned, 10 px in; a two-way row has a 30 px - box and + box at its ends (4 px in) and its texts 40 px in; a
//switch row has the slide switch at the left and its label, with a second grey line under it, from x UI_FOLD_TEXT_X.
#define UI_FOLD_VALUE_IN      10
#define UI_FOLD_TWOWAY_IN     40
#define UI_FOLD_TEXT_X        90

//ACQ menu, Memory column (x 305..377 of the menu, under its rule at y 211): the main-loop pass time, a grey label line
//and the value under it (fw 0.2.63; it sat between the "Memory" header and the first depth chip before)
#define UI_ACQ_PASS_X         312
#define UI_ACQ_PASS_Y         218
#define UI_ACQ_PASS_W         62
extern const char *const ui_acq_pass_label;                      //"Pass time"

//Value names, as the fold-outs and the CTRL buttons show them
#define UI_TRACE_COUNT        3     //fw 0.2.63: 1.00o5's "Pro" (min / max outlines, no fill) removed, see README
#define UI_VIEW_NORMAL        0
#define UI_VIEW_XY            1
#define UI_SPEC_VIEW_COUNT    3
#define UI_PERSIST_COUNT      4

extern const char *const ui_trace_names[UI_TRACE_COUNT];         //Linear, Dots, Peak (scopesettings.ACQ_trace)
extern const char *const ui_view_names[2];                        //Normal, X-Y
extern const char *const ui_spec_view_names[UI_SPEC_VIEW_COUNT];  //Spectrum, Waterfall, Density
extern const char *const ui_spec_chan_names[3];                   //CH1, CH2, CH1 + CH2
extern const char *const ui_spec_window_names[3];                 //Rect, Hann, Flat top (FX_WIN_*)
extern const char *const ui_spec_avg_names[5];                    //Off, 4, 16, 64, Max hold
extern const char *const ui_persist_names[UI_PERSIST_COUNT];      //0.2 s, 1 s, 5 s, Infinite (PHOS_PERSIST_*)
extern const char *const ui_palette_names[2];                     //Mono, Heat
extern const char *const ui_phos_note_roll;                       //why the Phosphor switch is grey in roll mode

//The Display button's value line: the view, and Phosphor when its switch is on (Scope); the variant (Spectrum)
const char *ui_display_value(int spectrum, int view, int phosphor);

//The warning line at the top middle of the plot (Scope view, all variants): consequence and action, the most urgent
//one only; "" when nothing applies
typedef struct
{
  uint8 en[2];                //channel on
  uint8 over[2];              //the latest record clipped
  uint8 untrig;               //the latest record ended on the auto time-out
  uint8 running;
  uint8 xy;                   //X-Y view
} ui_warn_in;

const char *ui_scope_warning(const ui_warn_in *w);

//Spectrum cards (bottom row; plan section 3a): per shown channel Peak and Level in the channel colour, then RBW and, in
//the waterfall, the time its rows span, both with a neutral chip. One row from x 5 at a 120 px pitch: six fit.
#define UI_SC_PEAK            0
#define UI_SC_LEVEL           1
#define UI_SC_RBW             2
#define UI_SC_TIME            3
#define UI_SC_MAX             6
#define UI_SC_Y               446

typedef struct
{
  uint8 kind;
  uint8 ch;
} ui_spec_card;

extern const char *const ui_sc_names[4];                         //Peak, Level, RBW, Time

int    ui_spec_cards(const uint8 shown[2], int time_ok, ui_spec_card *out);
uint32 ui_spec_card_x(int i);

#endif
