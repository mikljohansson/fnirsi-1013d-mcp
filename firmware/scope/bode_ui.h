//----------------------------------------------------------------------------------------------------------------------------------
// bode_ui.h - the Frequency response screen as data (fw 0.2.69, docs/plans/bode-ui.md, owner's final decisions of
// 2026-10-19 evening): the chart's geometry and axis scales, the Log / Linear frequency axis, the C1 / C2 readout rows,
// the MEASURES list and its cards, the warning and note texts, the fold-out texts. Pure C, host-buildable
// (host_tests/test_ui.c checks the texts fit their boxes with the device's fonts); bode_view.c draws with it.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef BODE_UI_H
#define BODE_UI_H

#include "types.h"
#include "bode_dsp.h"
#include "bode_sweep.h"

//----------------------------------------------------------------------------------------------------------------------------------
//The chart: one pane x 6..726, y 60..396, six divisions of 56 px shared by both y axes. Magnitude labels at the left
//(x 9, CH2 cyan, the top one with " dB"), phase labels right-aligned at x 723 (the phase colour, the top one with
//" deg"), frequency labels at y 400, the cards at y 426 (the Scope's row), the slider row at y 452..471. The top
//division (y 60..116) is kept free of data when the scale allows it: the warning line (y 69, centred at x 366) lives
//there.
#define BU_PX                 6
#define BU_PW                 720
#define BU_Y                  60
#define BU_DIV                56
#define BU_NDIV               6
#define BU_H                  (BU_NDIV * BU_DIV)
#define BU_LABEL_L_X          9
#define BU_LABEL_R_X          723
#define BU_FL_Y               400
#define BU_CARDS_Y            426
#define BU_WARN_XC            366
#define BU_WARN_Y             69
#define BU_WARN_MAX           50          //characters (at most 355 px in 16 px; font_2 is narrower)

//Display variants (settings word 220 bits 3..4) and the frequency axis (bit 6)
#define BU_SHOW_OVERLAY       0           //both curves, both axes (default)
#define BU_SHOW_MAG           1
#define BU_SHOW_PHASE         2
#define BU_SHOW_COUNT         3
#define BU_AXIS_LOG           0
#define BU_AXIS_LINEAR        1

extern const char *const bu_show_names[BU_SHOW_COUNT];     //fold-out rows: "Overlaid", "Magnitude only", "Phase only"
extern const char *const bu_show_values[BU_SHOW_COUNT];    //Display button: "Overlay", "Mag", "Phase"
extern const char *const bu_axis_names[2];                 //"Log", "Linear"
extern const char *const bu_axis_row;                      //"Frequency axis"

//fw 0.2.71: Trace style (the CTRL page, settings word 216): valid points joined (Lines, default) or loose (Dots)
#define BU_TRACE_LINES        0
#define BU_TRACE_DOTS         1
#define BU_TRACE_COUNT        2
extern const char *const bu_trace_names[BU_TRACE_COUNT];   //"Lines", "Dots"

//One axis scale: the top line's value and the value per division (mdB or mdeg). The first division of divs[] (in
//increasing order) that holds mn..mx in five divisions under a free top division; else the first that holds it in six;
//nothing fits: the last division, top above the maximum (the low end is clipped).
typedef struct
{
  int32 top, div;
} bu_scale;

void  bu_scale_pick(int32 mx, int32 mn, const int32 *divs, int ndivs, bu_scale *s);
void  bu_scale_mag(int32 mx, int32 mn, bu_scale *s);       //5, 10, 20 dB per division
void  bu_scale_phase(int32 mx, int32 mn, bu_scale *s);     //45, 90 deg per division
//Before any point: the scales of an empty chart (0 .. -40 dB, +90 .. -90 deg with their headroom)
void  bu_scale_empty(bu_scale *mag, bu_scale *ph);
int32 bu_scale_y(const bu_scale *s, int32 v);              //pane y of a value, clamped to the pane

//Frequency axis, clamped to the pane: log (the frequencies as bode_log2_q32 values) or linear (mHz)
int32 bu_x_log(int64 l0, int64 l1, int64 lf);
int32 bu_x_lin(uint64 f0, uint64 f1, uint64 f);

//Linear axis grid: the 1-2-5 step (mHz) that gives at most 10 lines over f0..f1 (and at least one when the span allows)
uint64 bu_lin_step(uint64 f0, uint64 f1);

//fw 0.2.71, the grid rule (docs/ui-guidelines.md section 3: a grid line sits at a value its axis names). The vertical
//lines of a frequency axis f0..f1 (mHz), low to high:
//  linear: every bu_lin_step step; each labelled, or every other one (the first, third, ...) when there are more than six
//  log:    every decade (major, labelled); the 2 and 5 lines (minor) when the span is under three decades (f1 < 1000 f0),
//          labelled when it is under 1.5 decades (f1 < 30 f0); a span holding fewer than two such lines takes the linear
//          steps (all major)
//Majors are drawn with dots every 3 px, minors every 8 px. The Spectrum view uses the linear rule for its axis.
#define BU_GRID_MAX           24
#define BU_GRID_DOTS_MAJOR    3
#define BU_GRID_DOTS_MINOR    8

typedef struct
{
  uint64 f;                   //mHz
  uint8  major, label;
} bu_gline;

int    bu_fgrid(uint64 f0, uint64 f1, int linear, bu_gline *g);   //lines written (<= BU_GRID_MAX)

//Short frequency label: 3 significant digits at most, no trailing zeros, "", "k", "M": 100000 -> "100", 2.5e6 -> "2.5k"
char *bu_freq_short(char *t, uint64 mhz);

//Values: "+1.21dB" (mdB, two decimals), "-60.8deg" (mdeg, one decimal), signed frequency "-33.27kHz"
char *bu_fmt_db(char *t, int32 mdb);
char *bu_fmt_deg(char *t, int32 mdeg);
char *bu_fmt_fdiff(char *t, int64 dmhz);

//----------------------------------------------------------------------------------------------------------------------------------
//The slider row (owner, bode-ui.md section 2; fw 0.2.72 slider_ov.h): the Scope's construction (a line at y 452, the
//track x 2..727 / y 453..471, the window its two edge lines). The track is 1 Hz .. 10 MHz on a log scale (seven
//decades); the window spans Start..Stop. A drag moves Start and Stop together by whole 1-2-5 steps (bode_step_mhz
//indices 0..BODE_STEPS - 1) and stops at the ends.
#define BU_SL_X               2            //= SOV_X
#define BU_SL_W               726          //= SOV_W
#define BU_SL_FRAME_X         2
#define BU_SL_FRAME_Y         452
#define BU_SL_FRAME_W         726
#define BU_SL_FRAME_H         20
#define BU_SL_WIN_Y           455
#define BU_SL_WIN_H           14
#define BU_SL_HIT_Y            450          //a touch from here down takes the slider

int32 bu_slider_x(int step);                                 //track x of a Start / Stop step
int32 bu_slider_x_mhz(uint64 mhz);                           //track x of any frequency (AUTO SET's passes)
//Steps to shift Start / Stop (start0 < stop0) for a drag of dx px from where it began: the window's left edge follows
//the finger to the nearest step, both ends kept on the track
int   bu_slider_shift(int start0, int stop0, int32 dx);

//----------------------------------------------------------------------------------------------------------------------------------
//Cursors C1 and C2 (T CURSOR): flags above the pane (24 x 13 at y 48, "C1" / "C2" in black on the cursor colour; hit
//area 44 x 44 around the flag), dotted lines through the pane. The readout box at the bottom left of the pane (variant 2,
//owner): x 10..261, y 342..393, grey, rows C1, C2, C2-C1 in 13 px at a 16 px pitch: the name in the cursor colour (the
//difference row white), f white, the level in the magnitude colour, the phase in the phase colour; a flagged point shows
//its reason in grey instead of the phase, and the difference row is then grey.
#define BU_FLAG_W             24
#define BU_FLAG_H             13
#define BU_FLAG_Y             48          //the trace area starts at y 48
#define BU_FLAG_HIT           22          //half of the 44 px hit area
#define BU_RO_X               10
#define BU_RO_Y               342
#define BU_RO_W               252
#define BU_RO_H               52
#define BU_RO_ROW0            3
#define BU_RO_PITCH           16
#define BU_RO_COL_NAME        6
#define BU_RO_COL_F           52
#define BU_RO_COL_DB          122
#define BU_RO_COL_PH          184

typedef struct
{
  char name[8], f[16], db[16], ph[24];
  uint8 grey;                 //the row's values grey (a flagged point, or the difference of one)
  uint8 flagged;              //ph holds the reason
} bu_ro_row;

//The reason a flagged point is flagged, for the readout ("unsettled", "near floor", "CH2 clipped", ...); "" when valid
const char *bu_flag_reason(uint16 flags);

//Rows for the cursor points (f in mHz, H in mdB / mdeg, flags as bode_point): r[0] C1, r[1] C2, r[2] C2-C1
void bu_readout(uint64 f1, int32 m1, int32 p1, uint16 fl1, uint64 f2, int32 m2, int32 p2, uint16 fl2, bu_ro_row r[3]);

//----------------------------------------------------------------------------------------------------------------------------------
//MEASURES ("Frequency response measurements"): ten items, saved in settings words 213 (0xE000 marker, items 0..2),
//214 (items 3..6) and 215 (items 7..9), 4-bit ranks; a rank above ten or a missing marker gives the defaults (owner:
//-3dB, Peak, Q, Level in that order).
#define BU_MS_LEVEL           0
#define BU_MS_3DB             1
#define BU_MS_BW              2
#define BU_MS_PEAK            3
#define BU_MS_Q               4
#define BU_MS_PH45            5
#define BU_MS_PH90            6
#define BU_MS_PH180           7
#define BU_MS_SWEEP           8
#define BU_MS_THRU            9
#define BU_MS_COUNT           10
#define BU_MS_WORD0           213
#define BU_MS_WORDS           3
#define BU_MS_MARKER          0xE000

//Card block colours
#define BU_BLOCK_MAG          0           //CH2 cyan (the magnitude curve)
#define BU_BLOCK_PHASE        1           //the phase curve's pink
#define BU_BLOCK_NEUTRAL      2

//Value colours
#define BU_VAL_WHITE          0
#define BU_VAL_GREY           1
#define BU_VAL_GREEN          2
#define BU_VAL_AMBER          3

extern const char *const bu_ms_names[BU_MS_COUNT];
int    bu_ms_block(int item);
void   bu_ms_defaults(uint8 *rank10);
uint16 bu_ms_word(const uint8 *rank10, int w);
int    bu_ms_restore(uint8 *rank10, const uint16 *w3);   //0: not valid, the defaults were set

//What the cards show
#define BU_TABLE_NONE         0           //no sweep yet
#define BU_TABLE_FILLING      1           //a new table is being filled: markers would mislead ("sweeping")
#define BU_TABLE_COMPLETE     2

#define BU_THRU_NONE          0
#define BU_THRU_APPLIED       1
#define BU_THRU_STALE         2
#define BU_THRU_OFF           3

typedef struct
{
  uint8  table;               //BU_TABLE_*
  uint8  autoset;             //the AUTO SET overview runs
  uint8  thru;                //BU_THRU_*
  int    done, count;         //points measured / planned
  const bode_markers *mk;
} bu_card_in;

//The value text of item `item` into t (at most 20 characters); returns the value colour (BU_VAL_*)
int bu_card_text(int item, const bu_card_in *in, char *t);

//----------------------------------------------------------------------------------------------------------------------------------
//Texts. Warnings are at most BU_WARN_MAX characters; the instruction before the first sweep is two lines in the
//warning's place and colour.
#define BU_W_OVERLOAD         0
#define BU_W_ARM              1
#define BU_W_TIMEOUT          2
#define BU_W_HOST             3
#define BU_W_PLAN             4
#define BU_W_THRU_SWEEP       5
#define BU_W_CH2_OVER         6
#define BU_W_CH1_LOW          7
#define BU_W_THRU_STALE       8
#define BU_W_OFFSET           9
#define BU_W_NO_RESPONSE      10
#define BU_W_EXT              11      //note
#define BU_W_ROLL             12      //note
#define BU_W_NO_CORNER        13      //note
#define BU_W_AUTO_CANCEL      14      //note
#define BU_W_NOTHING_SAVED    15
#define BU_W_NEEDS_FPGA       16
#define BU_W_NOT_IN_VIEWER    17
#define BU_W_SAVE_FAILED      18
#define BU_W_READOUT          19      //fw 0.2.71: BODE_ERR_READOUT
#define BU_W_COUNT            20

extern const char *const bu_warn[BU_W_COUNT];
extern const char *const bu_instruction[2];

//"N points not settled: choose a longer Settle", "N points near the floor: shown grey" (N >= 1)
char *bu_warn_unsettled(char *t, int n);
char *bu_warn_floor(char *t, int n);

//fw 0.2.71, the top bar in this mode (bode-ui.md section 7). The ACQ chip's grey box reads "Set by" over "the sweep"
//(font_2 at the chip's value column, x + 38 of the 103 px chip); a tap on the ACQ chip, or on the generator chip while a
//sweep runs, opens a one-line note (16 px, a box BU_NOTE_H tall at y 48: the ACQ's from x BU_NOTE_ACQ_X, the
//generator's ending at x BU_NOTE_GEN_XR) that any tap closes; the trigger menu gains a grey 13 px line at its bottom.
extern const char *const bu_acq_chip[2];
extern const char *const bu_acq_note;                     //"The sweep sets the sample rate for each point"
extern const char *const bu_gen_note;                     //"Driven by the frequency response sweep"
extern const char *const bu_trig_note;                    //"Not used by the sweep"
#define BU_ACQ_CHIP_TEXT_X    38
#define BU_ACQ_CHIP_W         103
#define BU_NOTE_ACQ_X         250
#define BU_NOTE_GEN_XR        728
#define BU_NOTE_Y             48
#define BU_NOTE_H             40
#define BU_NOTE_PAD           12
#define BU_TRIG_NOTE_H        24          //added to the trigger menu's height in this mode

//Single sweep's second line: "about 3 s", "about 45 s", "about 2 min", or the running text
extern const char *const bu_single_running;               //"running, then STOP"
char *bu_estimate_text(char *t, uint32 ms);

#endif
