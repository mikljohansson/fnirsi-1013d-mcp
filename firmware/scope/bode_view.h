//----------------------------------------------------------------------------------------------------------------------------------
// bode_view.h - the Frequency response mode (fw 0.2.66 as "Bode plot"; fw 0.2.69 redesigned per docs/plans/bode-ui.md,
// docs/ui-guidelines.md): a display mode of the main loop like the spectrum view. While it shows, the scope's own
// acquisition is off and the sweep engine (bode_sweep.c) drives the generator, the channels and the captures. The trace
// area is one pane with the magnitude in dB on the left axis and the phase in degrees on the right axis over a log (or
// linear) frequency axis, the markers, the cursors C1 and C2 with their readout box, and the cards chosen through
// MEASURES. The right bar keeps its meanings: RUN / STOP = continuous sweeps / stop after the point, AUTO SET = an
// overview sweep that sets the span and Settle and then runs, T CURSOR = C1 / C2, MEASURES = the mode's list, SAVE WAVE =
// the sweep table as CSV. The CTRL page holds Display (Overlaid, Magnitude only, Phase only; Frequency axis Log /
// Linear), Sweep (start, stop, points, settle, CH2 range, Single sweep), Thru (measure, apply, clear) and Grid.
// Entered from the main menu's "Frequency response" row; left through the other mode rows. Everything the view changed
// (generator, channels, time base, trigger source, RUN / STOP) is given back on leaving. Pure parts: bode_ui.c.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef BODE_VIEW_H
#define BODE_VIEW_H

#include "types.h"
#include "bode_sweep.h"

#define BODE_DISPLAY_MODE     5     //GET_STATUS byte 43

int    bode_view_active(void);
int    bode_view_available(void);   //the bitstream has the decimation filter and the DDS, no viewer is open
void   bode_view_open(void);
void   bode_view_close(void);

//Main loop (pc_glue_ui_service): the engine, RUN / STOP, the repeat, redraws
void   bode_view_service(void);
//scope_acquire_trace_data: 1 while the view shows (it drew; the UI acquisition is skipped)
int    bode_view_pass(void);
//scope_display_trace_data / scope_preset_values: 1 when the view drew the trace area instead
int    bode_view_display(void);
//A touch in the trace area (xtouch, ytouch): 1 = handled (cursor)
int    bode_view_touch(void);
//AUTO SET in the view: the overview sweep, then span, Settle and RUN; a second tap cancels (1 = handled); its label
//reads "Auto..." while the overview runs
int    bode_view_autoset(void);
int    bode_view_auto_running(void);
//T CURSOR in the view: C1 / C2 on or off
void   bode_view_tcursor(void);
//SAVE WAVE in the view: the sweep table as bode/FR00001.CSV (bode-ui.md 3.3)
void   bode_view_save(void);
//The MEASURES list's ranks (bode_ui.h BU_MS_*); the cards left of the open menu drawn again after a change
uint8 *bode_view_ms_ranks(void);
void   bode_view_cards_refresh(void);
//Why the main menu's row is grey (bode_view_available() is 0): a warning text
const char *bode_view_unavailable_text(void);
//Entered from roll mode: the roll note
void   bode_view_note_roll(void);

//fw 0.2.70, the top bar (bode-ui.md 7): 1 while a sweep runs, with the tone being measured in Hz (hz may be 0); the
//one-line note a tap on the ACQ chip (0) or, while sweeping, on the generator chip (1) opens; any tap closes it
int    bode_view_gen_tone(uint32 *hz);
void   bode_view_top_note(int which);

//The CTRL page in the view (ctrl_page.c hands it over)
void   bode_ctrl_draw(void);
void   bode_ctrl_touch(void);

//Settings sector words 219 and 220 (start, stop, points; settle, CH2 range, Display variant, cursors, frequency axis),
//216 (fw 0.2.70: Trace style; i 2) and the MEASURES words 213..215 (bode_ui.h); the defaults (Restore default settings)
uint16 bode_view_setting_word(int i);
void   bode_view_settings_restore(uint16 w0, uint16 w1, uint16 w2);
uint16 bode_view_ms_word(int i);
void   bode_view_ms_restore(const uint16 *w3);
void   bode_view_defaults(void);

//Protocol (pc_bode.c): BODE_CTRL acts on the view; returns a PC status and fills the 40-byte response
int    bode_view_ctrl(const uint8 *req, uint8 *resp, uint16 *detail);
//READ_BODE: header + rows from first, at most max_rows; returns bytes
uint32 bode_view_read(uint32 first, uint32 max_rows, uint8 *resp);

#endif /* BODE_VIEW_H */
