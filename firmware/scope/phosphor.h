//----------------------------------------------------------------------------------------------------------------------------------
// phosphor.h - digital phosphor view (protocol builds only; fw 0.2.46, docs/plans/phosphor-design.md)
//
// A main-loop display mode like the spectrum view, entered and left from the main menu (row 7) or the strip's "Scope"
// field. The scope view stays 1.00o5's (grid, cursors, measurements, pointers, REF / MATH); only the CH1 / CH2 traces
// are drawn from per-channel hit-count maps of the recent records (phos_map.c), coloured by how often a pixel was hit.
// A one-line strip under the grid (y 452..479, where the record-position slider was) holds Persistence, Palette,
// Clear, Scope and a status field.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PHOSPHOR_H
#define PHOSPHOR_H

#include "types.h"

//GET_STATUS byte 43 display_mode value (protocol section 14 row 52)
#define PHOSPHOR_DISPLAY_MODE   4

int    phosphor_mode(void);                  //1 while the view is on
int    phosphor_available(void);             //0 in roll mode and in the picture / waveform viewers
void   phosphor_set_active(int on);
void   phosphor_new_data(void);              //a capture refilled the trace buffers
void   phosphor_capture_info(int triggered); //the capture about to arrive triggered (0: auto time-out)
void   phosphor_service(void);               //every main-loop pass: leave the mode when it is no longer available

//scope_display_trace_data hooks
int    phosphor_owns_traces(void);           //1: the CH1 / CH2 polylines are replaced by the maps (YT display)
void   phosphor_draw(void);                  //after both channel blocks: add a new record, paint, decay, strip
int    phosphor_xy(uint32 index);            //XY branch, 1.00o5's first index: 1 = drawn from the map
void   phosphor_strip(void);                 //scope_preset_values: the strip over the grid's slider
int    phosphor_touch(void);                 //touch in the trace area (xtouch, ytouch): 1 = handled

//Settings sector word 210: 0x9A00 marker, palette bit 2, persistence bits 0-1
uint16 phosphor_settings_word(void);
void   phosphor_settings_restore(uint16 w);

#endif
