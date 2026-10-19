//----------------------------------------------------------------------------------------------------------------------------------
// phosphor.h - digital phosphor view (protocol builds only; fw 0.2.46, docs/plans/phosphor-design.md)
//
// A main-loop drawing variant of the scope, switched by the Phosphor switch in the CTRL page's Display fold-out (fw 0.2.53;
// the acquisition menu's Display row in 0.2.51, the main menu's row 7 before). It combines with either view, Normal or
// X-Y. The scope view stays 1.00o5's (grid, cursors, measurements, pointers, REF / MATH, the record-position slider);
// only the CH1 / CH2 traces (the X-Y figure) are drawn from per-channel hit-count maps of the recent records
// (phos_map.c), coloured by how often a pixel was hit. Persistence, Palette, Clear and the wfm/s readout live in the
// Display fold-out (ctrl_page.c); warnings are the scope view's warning line at the top middle of the plot.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PHOSPHOR_H
#define PHOSPHOR_H

#include "types.h"

//GET_STATUS byte 43 display_mode value (protocol section 14 row 52)
#define PHOSPHOR_DISPLAY_MODE   4

int    phosphor_mode(void);                  //1 while the picture is drawn (the switch is on and the scope view shows)
int    phosphor_available(void);             //0 in roll mode and in the picture / waveform viewers
void   phosphor_set_active(int on);
void   phosphor_new_data(void);              //a capture refilled the trace buffers
void   phosphor_capture_info(int triggered); //the capture about to arrive triggered (0: auto time-out)
void   phosphor_service(void);               //every main-loop pass: follow the switch and the availability

//The Display fold-out's Phosphor switch (fw 0.2.53, saved with the settings): on means the picture is drawn whenever
//the scope view shows and phosphor_available(); the spectrum keeps it off without changing the switch
int    phosphor_switch(void);
void   phosphor_set_switch(int on);

//The fold-out's rows
#define PHOSPHOR_P_PERSIST      0
#define PHOSPHOR_P_PALETTE      1
#define PHOSPHOR_P_CLEAR        2
void   phosphor_param_text(int p, char *t); //value names from ui_ctrl.h
void   phosphor_param_step(int p);          //next persistence (clears), other palette, or clear
void   phosphor_wfm_text(char *t);          //"55 wfm/s" while records are added, "" otherwise

//scope_display_trace_data hooks
int    phosphor_owns_traces(void);           //1: the CH1 / CH2 polylines are replaced by the maps (YT display)
void   phosphor_draw(void);                  //after both channel blocks: add a new record, paint, decay
int    phosphor_xy(uint32 index);            //XY branch, 1.00o5's first index: 1 = drawn from the map

//Settings sector word 210: 0x9A00 marker, the switch bit 3 (fw 0.2.53), palette bit 2, persistence bits 0-1
uint16 phosphor_settings_word(void);
void   phosphor_settings_restore(uint16 w);

#endif
