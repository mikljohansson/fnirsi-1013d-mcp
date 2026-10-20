//----------------------------------------------------------------------------------------------------------------------------------
// ui_diag_view.h - Diagnostics as a display mode of the main loop, its actions, and the first-run screen (fw 0.2.59,
// docs/plans/ux-makeover.md sections 6-6c, firmware/README.md section 5 "Screens"). The pages are built by ui_diag.c and
// drawn by ui_dlg.c; this file runs them on the scope.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef UI_DIAG_VIEW_H
#define UI_DIAG_VIEW_H

#include "types.h"

//Main menu Diagnostics row: the menus are closed and the scope screen is on the display (it is kept to return to)
void    ui_diag_open(void);

//Main loop, after the PC poll and the UI service: 1 while Diagnostics shows (the pass is done; the scope's acquisition,
//touch handling and clock are skipped), 0 otherwise
int     ui_diag_pass(void);

int     ui_diag_active(void);

//fw 0.2.96: a host request that changes state leaves Diagnostics as Back does (pc_hal_ui_leave_screen, MAIN context);
//1 when it showed
int     ui_diag_leave(void);

//The buffer the background redraws of the scope screen go to (pc_hal_scope.c): the kept scope screen while
//Diagnostics shows, so the PC's changes are there when Back returns to it
uint16 *ui_diag_scope_buffer(void);

//Boot, before the main loop: the first-run screen on an uncalibrated unit (settings_defaults: no valid settings sector)
void    ui_first_run(int settings_defaults);

#endif /* UI_DIAG_VIEW_H */
