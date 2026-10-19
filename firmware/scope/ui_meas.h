//----------------------------------------------------------------------------------------------------------------------------------
// ui_meas.h - Measurements v2 (fw 0.2.65): the MEASURES lists per mode, their display order, the card fit in the bottom
// row, the MEASURES menu geometry, the spectrum's card set and its THD / noise maths, and the card source hook for a
// future FPGA counter (docs/plans/ux-makeover.md section 3a, docs/plans/ux-review.md section 6 part B, firmware/AGENTS.md
// section 41). Pure C, host-buildable (host_tests/test_ui.c).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef UI_MEAS_H
#define UI_MEAS_H

#include "types.h"

//Modes with their own MEASURES list
#define UI_MS_SCOPE           0
#define UI_MS_SPECTRUM        1
#define UI_MS_BODE            2           //fw 0.2.69: Frequency response (bode_ui.h list)

//----------------------------------------------------------------------------------------------------------------------------------
//Selection and display order. A list of n items is an array of ranks: 0 = not chosen, otherwise the card's place in the
//row (1 first). The saved index stays the item (the Scope's ranks are 1.00o5's measuresstate bytes, which it treats
//as on / off, so old settings with every chosen byte 1 show in table order and a 1.00o5 image still reads ours). Ties and
//gaps are tolerated everywhere; a change first renumbers 1..k.

#define UI_MS_LIST_MAX        12

void ui_ms_normalise(uint8 *rank, int n);           //ranks 1..k in display order, ties broken by item index
int  ui_ms_order(const uint8 *rank, int n, uint8 *out);   //chosen items in display order; returns how many
int  ui_ms_toggle(uint8 *rank, int n, int item);    //off: removed, the later ones move up; on: last. Returns the new state
void ui_ms_select_all(uint8 *rank, int n);          //keeps the chosen order, appends the rest in table order
void ui_ms_clear(uint8 *rank, int n);

//Scope defaults (plan section 3a): Freq, Vpp, Vavg in that order on each channel
void ui_ms_scope_defaults(uint8 *rank12);

//----------------------------------------------------------------------------------------------------------------------------------
//Card fit. Cards are UI_CARD_W x UI_CARD_H (ui_fmt.h) at a 120 px pitch in one row: the Scope's at y 426 above the
//record-position slider, CH1's from x 5 and CH2's from x 366 (three each; CH1 takes the whole row, six, while the CH2
//block is not shown), the spectrum's from x 5 at y 446 (six). When more are chosen than fit, the last place shows
//"+N" (N = the cards not shown, it included) in the block's colour, so nothing is cut silently.

#define UI_MS_PITCH           120
#define UI_MS_SCOPE_Y         426
#define UI_MS_SPEC_Y          426                   //fw 0.2.70: the Scope's row (the slider row under it)
#define UI_MS_ROW_CARDS       6

int    ui_ms_fit(int n, int cap, int *more);        //value cards drawn (n or cap - 1); *more = the "+N" count or 0
int    ui_ms_scope_cap(int ch, int other_shown);    //places for a Scope channel block
uint32 ui_ms_card_x(int ch, int slot);              //Scope: x of a channel block's slot; ch 0 is also the spectrum's row
const char *ui_ms_more_text(char *t, int more);     //"+N"

//----------------------------------------------------------------------------------------------------------------------------------
//Spectrum list: per channel Peak, Level, THD, Noise (channel colour); for the view RBW, Span, Center, Time (neutral chip;
//Time only in the waterfall). Saved in settings words 223 (CH1), 222 (CH2), 221 (view): 0xA000 marker in bits 12-15,
//the four ranks 3 bits each from bit 0. A word without the marker or with a rank above 4 gives that group's defaults.

#define UI_MS_SP_PEAK         0
#define UI_MS_SP_LEVEL        1
#define UI_MS_SP_THD          2
#define UI_MS_SP_NOISE        3
#define UI_MS_SP_RBW          4
#define UI_MS_SP_SPAN         5
#define UI_MS_SP_CENTER       6
#define UI_MS_SP_TIME         7
#define UI_MS_SP_MORE         8                     //the "+N" place
#define UI_MS_SP_KINDS        8

#define UI_MS_SP_GROUPS       3                     //CH1, CH2, the view
#define UI_MS_SP_PER_GROUP    4
#define UI_MS_SP_VIEW         2                     //group index of RBW, Span, Center, Time

#define UI_MS_SPEC_WORD_CH1   223
#define UI_MS_SPEC_WORD_CH2   222
#define UI_MS_SPEC_WORD_VIEW  221
#define UI_MS_SPEC_MARKER     0xA000

typedef struct
{
  uint8 rank[UI_MS_SP_GROUPS][UI_MS_SP_PER_GROUP];
} ui_ms_spec;

typedef struct
{
  uint8 kind;                                       //UI_MS_SP_*
  uint8 ch;                                         //channel for Peak .. Noise; the "+N" count for UI_MS_SP_MORE
} ui_ms_card;

extern const char *const ui_ms_sp_names[UI_MS_SP_KINDS];

void   ui_ms_spec_defaults(ui_ms_spec *s);          //CH1 Peak, Level, THD; CH2 Peak, Level; RBW, Time
uint16 ui_ms_spec_word(const ui_ms_spec *s, int group);
int    ui_ms_spec_restore(ui_ms_spec *s, int group, uint16 w);   //0: the word was not valid, the group has its defaults
int    ui_ms_spec_kind(int group, int index);       //UI_MS_SP_* of a list place

//The spectrum's row: the shown channels' cards in their order, then the view's; Time only with time_ok. Fitted to
//UI_MS_ROW_CARDS (the last place "+N" when over). Returns the number of places used.
int    ui_ms_spec_cards(const ui_ms_spec *s, const uint8 shown[2], int time_ok, ui_ms_card *out);

//----------------------------------------------------------------------------------------------------------------------------------
//Spectrum maths on levels in centi-dB (spectrum.c spec_db / spec_raw, bins 0..last), integer only.

//10^(cdb / 1000) in Q24 (a power ratio from a level difference); cdb is clamped to -12000..+2000
uint64 ui_ms_pow_q24(int32 cdb);

//THD of the tone at bin k in hundredths of a percent: the power of the three bins around the fundamental against the
//power of the three bins around each harmonic's highest bin (harmonic n searched within n k +- (n / 2 + 1)), for
//harmonics 2..10 below bin `last`. UI_MS_THD_F0_LOW when k < 3 (no clean fundamental above DC), UI_MS_THD_F0_HIGH when
//the second harmonic is past the axis.
#define UI_MS_THD_F0_LOW      (-1)
#define UI_MS_THD_F0_HIGH     (-2)
int32  ui_ms_thd(const int16 *cdb, uint32 last, uint32 k);

//Noise floor in centi-dB at the RBW: the median of bins first..last of one record, +1.59 dB (the median of an
//exponentially distributed bin power lies 10 log10(ln 2) below its mean). 0.1 dB resolution.
#define UI_MS_NOISE_MEDIAN_CDB 159
int32  ui_ms_noise_cdb(const int16 *cdb, uint32 first, uint32 last);

//----------------------------------------------------------------------------------------------------------------------------------
//Card sources (roadmap section 8 item 6: an FPGA frequency / period counter). A Scope card's value comes from the
//record statistics of its source (the live channel or a reference trace, scope_functions.c measurementfunctions)
//unless a source is registered for its item: then a card of a live channel (CH1, CH2; never a reference trace) asks
//that source. A source writes the card's value text for channel ch (0 CH1, 1 CH2) into text, at most UI_MS_TEXT_MAX - 1
//characters, formatted like the others (ui_fmt.h: ui_fmt_sig4 for Freq and Period, UI_MEAS_NO_PERIOD with no signal),
//and must not touch the FPGA bus (it runs while the screen is drawn: read latched counter values only). The driver
//registers at start-up when the running bitstream has the counter (pc_sess.var.caps) and registers 0 otherwise; the
//cards, the MEASURES menu and the settings do not change.
#define UI_MS_TEXT_MAX        20                    //measurementtext[20] (variables.c)

typedef void (*ui_ms_source)(int ch, char *text);

void         ui_ms_set_source(int item, ui_ms_source fn);   //item: Scope list index (ui_fmt.h UI_MEAS_*); 0 = record
ui_ms_source ui_ms_get_source(int item);

//----------------------------------------------------------------------------------------------------------------------------------
//MEASURES menu (stock look: dark grey box, white edge, black separators, white names when chosen, grey when not;
//1.00o5's box widened to 44 px rows). A title band with the mode's name, then rows of 44 px:
//  Scope:    sources (CH1 / CH2 and Ref 1-4 / Ref 5-8), actions (Select All, Clear, Hide), three rows of four items
//  Spectrum: actions (the channel label, Select All, Clear), the four channel items, the view's four items (whole width)
//  Frequency response (fw 0.2.69): Select All, Clear (neutral grey), then its ten items four per row, whole width
//The left half belongs to CH1, the right to CH2, split by a double white line at x 480. The box ends at y 477.

#define UI_MSM_X              232
#define UI_MSM_W              499
#define UI_MSM_BOTTOM         478                   //one below the box
#define UI_MSM_TITLE_H        28
#define UI_MSM_ROW_H          44
#define UI_MSM_CELLS_MAX      40

#define UI_MSC_SOURCE         1                     //index 0 the channel, 1..4 its reference traces
#define UI_MSC_ALL            2
#define UI_MSC_CLEAR          3
#define UI_MSC_HIDE           4
#define UI_MSC_ITEM           5                     //group = channel (Scope), list group (Spectrum); index = item
#define UI_MSC_LABEL          6                     //the spectrum's channel label: no action
#define UI_MSC_EMPTY          7                     //Frequency response: the last row's unused places

typedef struct
{
  uint16 x, y, w, h;
  uint8  what;                                      //UI_MSC_*
  uint8  group;                                     //0 CH1, 1 CH2, UI_MS_SP_VIEW
  uint8  index;
} ui_msm_cell;

const char *ui_msm_title(int mode);
int    ui_msm_rows(int mode);
uint32 ui_msm_height(int mode);
uint32 ui_msm_top(int mode);
int    ui_msm_cells(int mode, ui_msm_cell *out);   //every cell; returns the count
const char *ui_msm_text(int mode, const ui_msm_cell *c);

//The cell under a touch: 1 = *c set, 0 = inside the box but on no cell (title band), -1 = outside the box (closes).
//A cell's hit area includes the separator lines around it (the left / upper cell first), so no tap below the title
//band falls between cells.
int    ui_msm_hit(int mode, uint32 x, uint32 y, ui_msm_cell *c);

#endif
