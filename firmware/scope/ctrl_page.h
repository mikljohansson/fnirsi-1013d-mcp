//----------------------------------------------------------------------------------------------------------------------------------
// ctrl_page.h - the right bar's CTRL page (page two) and its fold-outs (fw 0.2.56, UX makeover step 2; docs/ui-guidelines.md
// section 2, docs/plans/ux-makeover.md section 3). Geometry, hit areas and texts are in ui_ctrl.h (host-tested).
//
// Top to bottom, the same in every mode: CTRL (menu.c), Display (fold-out: the variants with icons; in Scope also the
// Phosphor switch with Persistence, Palette, Clear and the wfm/s readout), the mode's analysis control (Scope: Trace
// style; Spectrum: Analyse with the channel, FFT length, Window, Average, Scale, Ref level, Reset), Grid, the V+ / CH1 /
// V- and V+ / CH2 / V- steppers, 50 % TRIG (Scope; the slot is empty in Spectrum). A fold-out opens left of the bar over
// the plot's right edge, is modal like the top bar's dropdowns, and closes on a tap outside it (that tap does nothing
// else) or on a choice. The waveform viewer's page two and the DC shift pages stay 1.00o5's (menu.c, statemachine.c).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef CTRL_PAGE_H
#define CTRL_PAGE_H

int  ctrl_page_spectrum(void);   //1 while the spectrum view shows: the bar shows the spectrum's slots
void ctrl_page_draw(void);       //page two's items below CTRL, in the current screen buffer (scope_setup_right_control_menu)
void ctrl_page_touch(void);      //a touch on the bar below CTRL while page two shows (xtouch, ytouch)

#endif
