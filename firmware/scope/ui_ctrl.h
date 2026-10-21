//----------------------------------------------------------------------------------------------------------------------------------
// ui_ctrl.h - the right bar's CTRL page, its fold-outs, the warning line (fw 0.2.56, UX makeover
// step 2, docs/plans/ux-makeover.md section 3, docs/ui-guidelines.md sections 2-3). Pure C, no globals, host-buildable
// (host_tests/test_ui.c): geometry, hit areas, the name tables and the texts. ctrl_page.c draws with them and runs the
// fold-outs' touch loops; spectrum.c and phosphor.c take their value names from here.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef UI_CTRL_H
#define UI_CTRL_H

#include "types.h"
#include "pc_decim.h"

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

int ui_p1_shown(int slot, int spectrum);     //spectrum: 0 Scope, 1 Spectrum, 2 Frequency response (fw 0.2.66)

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

//RUN/STOP (fw 0.2.68, owner; reverses 0.2.63's state button): the top bar chip shows the STATE (STOP / RUN / WAIT), the
//right bar button the ACTION a tap does: green "RUN" while stopped, red "STOP" while running or waiting (an owner
//exception to red-is-destructive, guidelines section 3). One source of truth, ui_run_view: the UI's run state, its
//single-shot wait, and a host acquisition in progress (a host ARM armed or triggered, or one finished less than
//UI_RUN_HOST_HOLD_MS ago, so a burst of host captures reads RUN, not a flicker). Both draws take it; pc_glue_ui_service
//redraws whichever shows a stale value after every pass (pc_hal_scope.c pc_run_follow).
#define UI_C_AMBER            0x00FFD262
#define UI_RUN_STOP           0
#define UI_RUN_RUN            1
#define UI_RUN_WAIT           2
#define UI_RUN_HOST_HOLD_MS   1000
int ui_run_view(int runstate, int single_wait, int host_busy);
extern const char *const ui_run_chip[3];                          //by view: "STOP", "RUN", "WAIT" (16 px, chip)
extern const char *const ui_run_action[2];                        //[0] stopped: "RUN", [1] running / waiting: "STOP"

//ACQ menu (ui-sample, docs/plans/ui-rows.md section 3): 478 px wide (x 250 .. 728, clear of the right bar). The row under
//the sample rates holds four 92 x 40 boxes at y + 163 on a 98 px pitch, Roll, Average, Filter and Sample, each a label
//line at y + 168 and a value line at y + 182; the Memory column is right of its rule at x + 402 (1.00o5's x + 304 moved by
//one box pitch): the "Memory" header, the depth chips (54 x 25 at x + 413, y + 33 + 36 i), under the rule at y 211 the
//pass time and, from y + 254, the note that says why boxes are grey (four 18 px lines, 13 px font). Menu coordinates.
//fw-acqui (owner 2026-10-21): the Sample Rate and Time per Division grids in five columns, 74 x 25 cells on a 78 x 28
//pitch from x + 8, so the grids and the row of four boxes span the same x + 8 .. 394 (the rule at y + 155 ends there
//too); the rates (18) in four rows from y + 33, the time/divs (24) in five rows from y + 241 (the last ends at y + 378 in
//the 386 px menu). Each cell's text centred in the 13 px font.
#define UI_ACQ_W              478
#define UI_ACQ_COL_X          402
#define UI_ACQ_MEM_HEAD_X     415
#define UI_ACQ_CHIP_X         413
#define UI_ACQ_BOX_Y          163
#define UI_ACQ_BOX_W          92
#define UI_ACQ_BOX_H          40
#define UI_ACQ_BOX_X(i)       (8 + 98 * (i))
#define UI_ACQ_GRID_COLS      5
#define UI_ACQ_GRID_W         74
#define UI_ACQ_GRID_H         25
#define UI_ACQ_GRID_X(i)      (8 + 78 * ((i) % UI_ACQ_GRID_COLS))
#define UI_ACQ_GRID_DY(i)     (28 * ((i) / UI_ACQ_GRID_COLS))
#define UI_ACQ_GRID_TEXT_DY   6
#define UI_ACQ_GRID_RIGHT     394     //UI_ACQ_GRID_X(4) + UI_ACQ_GRID_W = UI_ACQ_BOX_X(3) + UI_ACQ_BOX_W
#define UI_ACQ_RATE_Y         33
#define UI_ACQ_RATE_N         18
#define UI_ACQ_TB_HEAD_Y      216
#define UI_ACQ_TB_Y           241
#define UI_ACQ_TB_N           24
#define UI_ACQ_BOX_LINE1_Y    168
#define UI_ACQ_BOX_LINE2_Y    182
#define UI_ACQ_ROW_HIT_BOTTOM 210     //the boxes' hit area runs down to the rule at y 211
#define UI_ACQ_NOTE_X         410
#define UI_ACQ_NOTE_Y         254
#define UI_ACQ_NOTE_DY        18
#define UI_ACQ_NOTE_LINES     4
#define UI_ACQ_NOTE_W         66      //x + 410 .. 475, inside the menu's edge at x + 477

#define UI_ACQ_ROLL           0
#define UI_ACQ_AVERAGE        1
#define UI_ACQ_FILTER         2
#define UI_ACQ_SAMPLE         3
#define UI_ACQ_BOXES          4

//A box's state: NONE not drawn (the Sample slot in the Spectrum: the slot stays empty), OFF light grey with white text,
//ON green with black text, GREY light grey with grey text: the value is kept but not applied and the box takes no taps
#define UI_ACQ_BOX_NONE       0
#define UI_ACQ_BOX_OFF        1
#define UI_ACQ_BOX_ON         2
#define UI_ACQ_BOX_GREY       3

typedef struct
{
  uint8 roll;                 //the long (roll) time base is on
  uint8 average;              //Average on (kept in roll and under Peak)
  uint8 filter_on;            //the mode's Filter is not off
  uint8 peak;                 //Sample Peak (the Scope's one setting, kept in the Spectrum and in roll)
  uint8 spectrum;             //the ACQ menu of the Spectrum analyser
  const char *filter_value;   //the Filter's value line ("off", "CIC2+FIR", ...)
} ui_acq_in;

typedef struct
{
  uint8       state[UI_ACQ_BOXES];
  const char *line1[UI_ACQ_BOXES];
  const char *line2[UI_ACQ_BOXES];
  const char *note[UI_ACQ_NOTE_LINES];     //0 = empty line
} ui_acq_row;

//Sample Peak is applied to the acquisition: Scope (the Spectrum acquires Normal), not in roll mode (no 16-bit path there)
int  ui_acq_peak_applied(const ui_acq_in *in);

//The row and the note as the menu draws them: with Peak applied the Filter and Average boxes are grey with their values
//and the note reads "Peak:" / "Filter and" / "Average" / "not applied"; in roll the Average and Sample boxes are grey and
//the note reads "Not in" / "roll mode" (ui_phos_note_roll on two lines: 88 px do not fit the column's 66)
void ui_acq_row_build(const ui_acq_in *in, ui_acq_row *r);

//The UI acquisition's decimation setting byte (pc_decim.h: bits 1:0 K, PC_DECIM_SET_FIR, PC_DECIM_SET_PEAK) from the
//mode's Filter setting as kept and Sample: the peak bit in the Scope view only, the Filter's K / FIR kept under it, so
//pc_decim_config arms peak detect (SET_DECIM filter 4's path) and the Filter comes back with Normal
uint8 ui_acq_decim_set(uint8 filter_set, int peak, int spectrum);

//The Filter box's three modes (fw-acqui, owner 2026-10-21; Tektronix / Rigol "Hi Res", Keysight "High Resolution"):
//Off, Hi-Res = the boxcar (the mean of the N samples per output, no overshoot) and Anti-alias = CIC3 + FIR (flat passband
//to fs / 4, the most alias rejection; CIC2 + FIR where CIC3 is not exact, GET_DECIM's why). Setting bytes as pc_decim.h
//(bits 1:0 K, PC_DECIM_SET_FIR); every SET_DECIM mode stays the host's
#define UI_FILTER_OFF         PC_DECIM_OFF
#define UI_FILTER_HIRES       PC_DECIM_BOXCAR
#define UI_FILTER_ANTIALIAS   (PC_DECIM_CIC3 | PC_DECIM_SET_FIR)

//The box's value line for a setting: "Off" (K 0, the FIR bit ignored), "Hi-Res", "Anti-alias", and any other mode the
//host set (or an older firmware saved) by its own name: "boxcar+FIR", "CIC2", "CIC3", "CIC2+FIR"
const char *ui_filter_name(uint8 filter_set);

//A press on the box: Off -> Hi-Res -> Anti-alias -> Off; from a host mode to Off (the start of the cycle: the press
//leaves a mode the box cannot offer for the one every scope has, and the next presses step as always)
uint8 ui_filter_next(uint8 filter_set);

//The box under a touch at menu coordinates x, y: UI_ACQ_ROLL .. UI_ACQ_SAMPLE, or -1 (the drawn box, down to the rule)
int  ui_acq_box_at(uint32 x, uint32 y);

//The grid cell under a touch at menu coordinates x, y in the grid of n cells from y0 (UI_ACQ_RATE_Y / UI_ACQ_TB_Y): its
//index (row by row, five per row), or -1 between the cells or outside (the hit area is the drawn cell)
int  ui_acq_grid_at(uint32 x, uint32 y, uint32 y0, uint32 n);

//Settings word 211 (fw 0.2.56, marker 0xC700): bit 0 Grid, bits 1-2 the Spectrum's Display variant, bit 3 Sample Peak
//(ui-sample). Restore: 0 and nothing written when the marker (mask 0xFFF0) does not match
uint16 ui_word_ui(int grid, int spec_view, int peak);
int    ui_word_ui_restore(uint16 w, uint8 *grid, uint8 *spec_view, uint8 *peak);

//The pass time in the Memory column, under its rule at y 211: a grey label line and the value under it (fw 0.2.63)
#define UI_ACQ_PASS_X         410
#define UI_ACQ_PASS_Y         218
#define UI_ACQ_PASS_W         62
extern const char *const ui_acq_pass_label;                      //"Pass time"

//Value names, as the fold-outs and the CTRL buttons show them
#define UI_TRACE_COUNT        3     //fw 0.2.63: 1.00o5's "Pro" (min / max outlines, no fill) removed, see README
#define UI_VIEW_NORMAL        0
#define UI_VIEW_XY            1
#define UI_SPEC_VIEW_COUNT    3
#define UI_PERSIST_COUNT      4

extern const char *const ui_trace_names[UI_TRACE_COUNT];         //Linear, Dots, Bars (scopesettings.ACQ_trace)
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
  uint8 trig_type;            //ui-type: the trigger type armed (fpga_bus.h FPGA_TRIG_*: 0 edge, 1 pulse width, 2 Nth edge)
  uint8 trig_idle;            //Nth edge with an idle time (H != 0)
} ui_warn_in;

const char *ui_scope_warning(const ui_warn_in *w);

//The "No trigger" hint per trigger type (ui-type): the settings to check
const char *ui_no_trigger_hint(uint8 trig_type, uint8 idle_on);

#endif
