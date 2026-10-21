//----------------------------------------------------------------------------------------------------------------------------------
// trig_ui.h - the trigger menu's Type row as data (ui-type, docs/plans/ui-rows.md section 2): Edge, Pulse (width) and Nth
// (edge) with their parameters, kept in time and armed in words of the FPGA 0x153C trigger stream (fpga_bus.h "Trigger
// types and hold-off"). Pure C, no hardware, host-buildable (host_tests/test_trig_ui.c).
//
// The Width (Pulse) and the Idle time (Nth) step through one 1-2-5 ladder, 10 ns .. 10 s (28 steps, the Span / Start /
// Stop ladder), the Idle with "Off" (H = 0: the count runs from the arm) below it. The FPGA counts samples of the trigger
// stream: one memory word on CH1 / CH2, 10 (D + 1) ns on the legacy 8-bit path and N x 5 ns (N = 2 (floor(D / 2) + 1))
// on the 16-bit path, one 5 ns clock on EXT. Everything here is in 5 ns units, so 10 s (2^31 units) and the longest word
// period (2 x 400000 at divider 399999) fit 32 bits and the conversion is one 32-bit division: words = round(t / Tw).
// A ladder step can be armed at a rate when its words lie in the FPGA's range: 2 .. 2^24 - 1 for the Width (N = 1
// degenerates to an edge trigger), 1 .. 2^24 - 1 for the Idle time. The range of a rate is contiguous on the ladder
// (tui_range: lo .. hi); a step outside it snaps to the nearest inside (tui_rate), which is the end of the range.
//
// N is the Nth edge's count, 1 .. 65535 (0x5F bits 15..0; 0 acts as 1). The polarity (0x16) and the source are the
// menu's edge and channel rows and stay outside this module.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef TRIG_UI_H
#define TRIG_UI_H

#include "types.h"

//The types the row offers (the FPGA's 0x5E codes; TUI_TIMEOUT only arrives from the host: no box is chosen for it)
#define TUI_EDGE          0
#define TUI_PULSE         1
#define TUI_NTH           2
#define TUI_TIMEOUT       3

//The ladder: index 0 = 10 ns .. 27 = 10 s
#define TUI_STEPS         28
//The Idle time: 0 = Off, 1 .. 28 = ladder 0 .. 27
#define TUI_IDLE_OFF      0

//The FPGA's ranges (0x5F / 0x60 are 24 bits)
#define TUI_WORDS_MAX     0xFFFFFFu
#define TUI_WIDTH_MIN     2u
#define TUI_IDLE_MIN      1u
#define TUI_N_MIN         1u
#define TUI_N_MAX         65535u

typedef struct
{
  uint8  type;              //TUI_*
  uint8  longer;            //Pulse: 1 = longer than the Width, 0 = shorter
  uint8  width_i;           //Pulse: the ladder index of the Width
  uint8  idle_i;            //Nth: TUI_IDLE_OFF or 1 + the ladder index of the Idle time
  uint16 n;                 //Nth: the count, TUI_N_MIN .. TUI_N_MAX
} tui_state;

//The one state the menu edits (defined here, owned by the glue in pc_hal_scope.c)
extern tui_state trig_ui;

//Defaults: Edge, Shorter, Width 2us, N 2, Idle 50us (the parameters of the types not chosen are kept for when they are)
void   tui_defaults(tui_state *s);

//The ladder step's time in 5 ns units (i < TUI_STEPS)
uint32 tui_step_t5(uint32 i);

//The trigger stream's word period in 5 ns units: EXT 1; the 16-bit path (wide: the Filter on) 2 (floor(D / 2) + 1);
//the legacy path 2 (D + 1)
uint32 tui_tw5(uint32 divider, int wide, int ext);

//round(t / Tw) for ladder step i at word period tw5
uint32 tui_words(uint32 i, uint32 tw5);

//The ladder steps a rate can arm: the first with at least min_words and the last within TUI_WORDS_MAX (lo <= hi always:
//every word period the firmware writes keeps at least one step)
void   tui_range(uint32 tw5, uint32 min_words, uint32 *lo, uint32 *hi);

//The ladder step nearest to `words` words at tw5, within lo .. hi (the host's exact value shown as the nearest step)
uint32 tui_nearest(uint32 words, uint32 tw5, uint32 lo, uint32 hi);

//A rate change: the chosen type's time parameter snaps into the rate's range when it is outside. Returns what moved
#define TUI_SNAP_NONE     0
#define TUI_SNAP_WIDTH    1
#define TUI_SNAP_IDLE     2
int    tui_rate(tui_state *s, uint32 tw5);

//The registers the state arms at tw5: the type, 0x5E bit 3, N (0x5F) and H (0x60); Edge (and the host's timeout) 0 / 0
void   tui_hw(const tui_state *s, uint32 tw5, uint8 *type, uint8 *longer, uint32 *n, uint32 *h);

//The state after a host SET_TRIGGER2 (the shared screen: the menu shows what the host set): a width in words as the
//nearest ladder step inside the rate's range, an idle time likewise (0 = Off), the count clamped; Edge and timeout
//keep the parameters of the other types
void   tui_from_hw(tui_state *s, uint8 type, uint8 longer, uint32 n, uint32 h, uint32 tw5);

//The menu's taps. Steps return 1 when the value changed (0 at an end of the range). A type chosen snaps its parameter
//into the range silently (the menu shows the value)
void   tui_set_type(tui_state *s, uint8 type, uint32 tw5);
int    tui_step_width(tui_state *s, int dir, uint32 tw5);
int    tui_step_idle(tui_state *s, int dir, uint32 tw5);
int    tui_step_n(tui_state *s, int dir);
void   tui_set_n(tui_state *s, int32 n);

//Settings sector words 202 / 203 (docs/plans/ui-rows.md section 4): word 0 = marker 0xA000 in bits 15..13, bits 0-1 the
//type (the host's timeout stored as Edge), bit 2 Longer, bits 3-7 the Width index, bits 8-12 the Idle index; word 1 = N.
//tui_restore takes the defaults (and returns 0) on a word without the marker or with a field out of range
uint16 tui_word0(const tui_state *s);
uint16 tui_word1(const tui_state *s);
int    tui_restore(tui_state *s, uint16 w0, uint16 w1);

//Texts: a ladder step ("10ns", "2us", "50us", "1ms", "10s"), the Idle value ("Off" or the step), N in decimal (p holds
//at least 6 bytes; returns the end)
const char *tui_step_text(uint32 i);
const char *tui_idle_text(uint32 idle_i);
char       *tui_n_text(char *p, uint32 n);

//The chip's glyph (docs/plans/ui-rows.md 2.5): 2 px lines in a 14 x 14 field, drawn in the firmware's pixels as the
//mockups (docs/mockups/gen_ux/rows.py glyph(): a polyline with a 2 px stroke on whole-pixel coordinates). Edge keeps the
//rising / falling edge, Pulse is one full pulse of its polarity, Nth two short edges and one tall. tui_glyph_rects gives
//the rectangles to fill (x, y, w, h inside the field) and their count
#define TUI_GLYPH_RISE    0
#define TUI_GLYPH_FALL    1
#define TUI_GLYPH_PULSE_P 2
#define TUI_GLYPH_PULSE_N 3
#define TUI_GLYPH_NTH     4
#define TUI_GLYPH_SIZE    14
#define TUI_GLYPH_RECTS   24

typedef struct { uint8 x, y, w, h; } tui_rect;

uint32 tui_glyph_kind(uint8 type, uint8 edge);
uint32 tui_glyph_rects(uint32 kind, tui_rect *out);

#endif /* TRIG_UI_H */
