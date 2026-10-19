//----------------------------------------------------------------------------------------------------------------------------------
// bode_view.h - the Bode plot mode (fw 0.2.66, docs/plans/bode-design.md section 4, docs/ui-guidelines.md): a display mode
// of the main loop like the spectrum view. While it shows, the scope's own acquisition is off and the sweep engine
// (bode_sweep.c) drives the generator, the channels and the captures; the trace area shows the magnitude and phase panes
// over a log frequency axis with the markers, the cards show the cursor's f, |H| and phase and the -3 dB frequency, and the
// right bar's CTRL page holds Sweep (start, stop, points, settle, CH2 range, run) and Thru (measure, apply, clear).
// Entered from the generator menu's "Bode plot" row (owner); left through the main menu (Scope, Spectrum analyser) or the
// same row. RUN repeats sweeps, STOP ends after the point in progress, AUTO SET runs one sweep. Everything the view changed
// (generator, channels, time base, trigger source, RUN / STOP) is given back on leaving.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef BODE_VIEW_H
#define BODE_VIEW_H

#include "types.h"
#include "bode_sweep.h"

#define BODE_DISPLAY_MODE     5     //GET_STATUS byte 43

int    bode_view_active(void);
int    bode_view_available(void);   //the bitstream has the decimation filter and the DDS, no viewer is open
void   bode_view_request(int open); //generator menu row: open or close at the next main-loop pass
void   bode_view_open(void);
void   bode_view_close(void);

//Main loop (pc_glue_ui_service): pending open / close, the engine, RUN / STOP, the repeat, redraws
void   bode_view_service(void);
//scope_acquire_trace_data: 1 while the view shows (it drew; the UI acquisition is skipped)
int    bode_view_pass(void);
//scope_display_trace_data / scope_preset_values: 1 when the view drew the trace area instead
int    bode_view_display(void);
//A touch in the trace area (xtouch, ytouch): 1 = handled (cursor)
int    bode_view_touch(void);
//AUTO SET in the view: one sweep, or end the one running (1 = handled); its label reads "Auto..." meanwhile
int    bode_view_autoset(void);
int    bode_view_auto_running(void);

//The CTRL page in the view (ctrl_page.c hands it over)
void   bode_ctrl_draw(void);
void   bode_ctrl_touch(void);

//The generator menu's fourth row (menu.c draws, statemachine.c taps)
void   bode_gen_row_draw(void);
int    bode_gen_row_hit(uint32 y);

//Settings sector words 220 and 221 (start, stop, points; settle, CH2 range)
uint16 bode_view_setting_word(int i);
void   bode_view_settings_restore(uint16 w0, uint16 w1);

//Protocol (pc_bode.c): BODE_CTRL acts on the view; returns a PC status and fills the 40-byte response
int    bode_view_ctrl(const uint8 *req, uint8 *resp, uint16 *detail);
//READ_BODE: header + rows from first, at most max_rows; returns bytes
uint32 bode_view_read(uint32 first, uint32 max_rows, uint8 *resp);

#endif /* BODE_VIEW_H */
