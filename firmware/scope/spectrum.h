//----------------------------------------------------------------------------------------------------------------------------------
// spectrum.h - on-scope spectrum analyser (protocol builds only; docs/plans/dpos350p-features.md section 2)
//
// A main-loop display mode, not a modal menu: the UI loop, the MCP polls and RUN/STOP keep running. While it is on, the
// trace area (728 x 432 at 2,48) shows the spectrum of each new capture instead of the traces; the top bar and the right
// column stay 1.00o5's. Entered and left from the main menu (row "Spectrum analyzer") or the strip's "Scope" field.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef SPECTRUM_H
#define SPECTRUM_H

#include "types.h"

//Display modes as GET_STATUS byte 43 reports them (protocol section 14 row 45)
#define SPECTRUM_MODE_SCOPE       0
#define SPECTRUM_MODE_SPECTRUM    1
#define SPECTRUM_MODE_WATERFALL   2     //fw 0.2.32
#define SPECTRUM_MODE_DENSITY     3     //fw 0.2.32

//Highest frequency the view shows (fw 0.2.36, owner): the front end rolls off near 30 MHz. READ_SPECTRUM still
//returns every bin up to fs/2.
#define PC_SPECTRUM_FMAX_HZ       25000000u

//Microseconds since boot from timer0 (1 ms ticks + the 24 MHz down-counter), wraps after 71 minutes
uint32 spectrum_us(void);

//FFT_BENCH (fw 0.2.27): time the FFT on the CH1 trace buffer, layout in docs/protocol.md section 14 row 44.
//Returns the response length (<= SPECTRUM_BENCH_MAX).
#define SPECTRUM_BENCH_MAX    (8 + 8 * 24)
uint32 spectrum_bench(uint8 *out, uint32 reps);

//UI glue (pc_hal_scope.c)
int    spectrum_mode(void);                  //SPECTRUM_MODE_*
int    spectrum_available(void);             //0 in roll mode and in the picture / waveform viewers
void   spectrum_set_active(int on);
void   spectrum_new_data(void);              //a capture (UI or host) refilled the trace buffers
void   spectrum_capture_info(int triggered); //fw 0.2.41: the UI capture about to arrive triggered (0: auto time-out)
int    spectrum_display(void);               //from scope_display_trace_data: 1 = drew the spectrum view instead
int    spectrum_touch(void);                 //touch in the trace area (xtouch, ytouch): 1 = handled
void   spectrum_service(void);               //every main-loop pass: leave the mode when it is no longer available
void   spectrum_note_roll_off(void);         //fw 0.2.51: the main menu switched Roll off for the view: say so once
int    spectrum_autoset(void);               //fw 0.2.51: AUTO SET in the view: start the view's Auto, or cancel it (1 = handled)
int    spectrum_auto_running(void);          //fw 0.2.51: the view's Auto runs (AUTO SET reads "Auto...")

//READ_SPECTRUM (protocol section 14 row 45): the displayed spectrum of channel ch. Fills the 32-byte header and up to
//max_bins levels (i16 centi-dBV, from bin first on); returns the number of bins written, or -1 when there is none.
#define SPECTRUM_READ_HDR     32
int32  spectrum_read(int ch, uint32 first, uint32 max_bins, uint8 *hdr, uint8 *levels);

//Settings sector words (fw 0.2.39): FFT length, window, scale (word 0) and ref level (word 1)
uint16 spectrum_settings_word(int i);
void   spectrum_settings_restore(uint16 w0, uint16 w1);

#endif
