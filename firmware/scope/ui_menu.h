//----------------------------------------------------------------------------------------------------------------------------------
// ui_menu.h - the main menu (fw 0.2.51, docs/plans/ux-makeover.md section 2): the row table, the hit areas and what
// choosing a mode row does. Pure C, no globals, host-buildable (host_tests/test_ui.c). menu.c draws the rows from the
// table, statemachine.c hit-tests with it, the glue (pc_hal_scope.c) carries out ui_mm_choose's actions.
//
// Rows, top to bottom: modes first (Scope, Spectrum analyser, Bode plot; fw 0.2.68, owner: Bode left the generator
// menu), utilities below, the row that takes the scope off-line last. Every row is always shown (the Spectrum and Bode
// rows also in roll mode; Bode grey and inert where the bitstream lacks the filter and DDS); no current-mode marker.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef UI_MENU_H
#define UI_MENU_H

#include "types.h"

#define UI_MM_SCOPE           0
#define UI_MM_SPECTRUM        1
#define UI_MM_BODE            2
#define UI_MM_PICTURES        3
#define UI_MM_WAVEFORMS       4
#define UI_MM_SETTINGS        5
#define UI_MM_DIAGNOSTICS     6
#define UI_MM_USB             7
#define UI_MM_ROWS            8

//Geometry: the column at x 2..148 from y 46, one row per 52 px (fw 0.2.68: eight rows; 1.00o5's pitch was 59); the
//row's button is 131 x 35 at x 9, 9 px below the row top (8 px from its bottom to the separator); black separators on
//the last line of every row but the last
#define UI_MM_X               2
#define UI_MM_TOP             46
#define UI_MM_W               147
#define UI_MM_PITCH           52
#define UI_MM_H               (UI_MM_ROWS * UI_MM_PITCH)   //416: y 46..461
#define UI_MM_BTN_X           9
#define UI_MM_BTN_DY          9
#define UI_MM_BTN_W           131
#define UI_MM_BTN_H           35
#define UI_MM_AXIS_COLOR      0x00707070   //fw 0.2.68: the Scope row icon's axes, behind its sine (menu.c)

typedef struct
{
  const char *line1;
  const char *line2;          //empty for a one-line label
} ui_mm_label;

extern const ui_mm_label ui_mm_labels[UI_MM_ROWS];

uint32 ui_mm_row_top(int row);                 //first y of the row's hit area
int    ui_mm_row_at(uint32 x, uint32 y);       //the row under a touch, -1 outside the column

//What choosing a mode row does, given the current state. The rule (owner, 2026-10-19): Spectrum is always offered;
//in roll mode it switches Roll off and says so once; leaving Spectrum does not switch Roll back on. Scope returns to
//the scope with its Display variant (Phosphor back on when it was chosen). Bode plot (fw 0.2.68) excludes the other
//two as Spectrum does: spectrum and phosphor off, Roll off in roll mode (the spectrum's roll note is not shown).
typedef struct
{
  uint8 spectrum;             //1: the spectrum view on, 0: off
  uint8 bode;                 //1: the Bode plot on, 0: off
  uint8 phosphor;             //1: the phosphor view on, 0: off
  uint8 roll_off;             //1: switch Roll off first
  uint8 warn_roll;            //1: show "Roll off for the spectrum" once
} ui_mm_action;

//Returns 0 for a row that is not a mode row (nothing to do here)
int ui_mm_choose(int row, int spectrum_on, int roll_on, int phosphor_wanted, ui_mm_action *a);

#endif
