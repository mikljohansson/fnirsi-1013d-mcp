//----------------------------------------------------------------------------------------------------------------------------------
// ui_dlg.h - drawing of the full-screen pages and the modal dialogs of the UX makeover (fw 0.2.59): a ud_page (ui_diag.h)
// with the device's fonts and the mockups' look (black ground, outlined boxes and buttons, state words in colour;
// docs/plans/ux-makeover.md 6c), the dimmed background a dialog owns, the step dialog of the calibration wizard and the
// volts calibration, and the confirmation dialog (docs/ui-guidelines.md section 3 "Dialogs").
//
// Buffers: a dialog saves the screen in displaybuffer4, draws the dimmed copy and its panel in displaybuffertmp and
// copies them to the screen, and puts displaybuffer4 back when it closes. Pages are drawn wherever the screen buffer
// points (the callers draw into displaybuffertmp and copy).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef UI_DLG_H
#define UI_DLG_H

#include "types.h"
#include "ui_diag.h"

uint32 ui_dlg_rgb(int colour);                          //UD_C_* -> 0x00RRGGBB
void   ui_dlg_text(int x, int y, int font, int colour, const char *s);
void   ui_dlg_draw_page(const ud_page *pg);             //black ground, boxes, texts, buttons
void   ui_dlg_draw_button(const ud_button *b, int pressed);
void   ui_dlg_icon(int x, int y, int icon);             //16 x 16: 1 tick (green), 2 warning disc (amber), 3 pending ring

//Dialog background: the screen saved (displaybuffer4), dimmed to about 37 % in displaybuffertmp and shown. end puts the
//saved screen back.
void   ui_dlg_bg_begin(void);
void   ui_dlg_bg_end(void);

//The step dialog (calibration wizard, volts calibration): a 640 x 300 panel, title, two instruction lines, an amber
//warning line, two result lines with icons, Cancel left and the step's action right
#define UI_STEP_X        80
#define UI_STEP_Y        90
#define UI_STEP_W        640
#define UI_STEP_H        300
#define UI_STEP_TEXT_W   592

#define UI_ACT_PRIMARY   0        //green
#define UI_ACT_DISABLED  1        //greyed ("Measuring..."), not a button

typedef struct
{
  const char *title;
  const char *step;                 //"Step 2 of 6" at the right end of the title line (fw 0.2.63), 0 for none
  const char *line[2];
  const char *warn;                 //0 or "" for none
  const char *result[2];            //0 or "" for none
  uint8       result_icon[2];       //0 none, 1 tick, 2 warning, 3 pending
  uint8       result_colour[2];     //UD_C_*
  const char *note[2];              //0 or "": a small line under result i (fw 0.2.67: a zero step failure's numbers)
  const char *left;                 //"Cancel"
  const char *right;                //the action, 0 for none
  uint8       right_style;          //UI_ACT_*
} ui_step;

void   ui_dlg_step_draw(const ui_step *d);              //over the dimmed background (ui_dlg_bg_begin first)
int    ui_dlg_step_hit(const ui_step *d, int x, int y); //0 none, 1 left, 2 right (only when offered)
void   ui_dlg_step_press(const ui_step *d, int which);  //the pressed look of button 1 or 2, on the screen

//Confirmation (docs/ui-guidelines.md: a sentence that states the consequence, Cancel left and large, the action right,
//red for destructive ones). Modal over the current screen, which it restores. Returns 1 for the action, 0 for Cancel.
int    ui_dlg_confirm(const char *title, const char *const *lines, int nlines, const char *action, int danger);

//Touch: waits for the release of the current touch
void   ui_dlg_release(void);

#endif /* UI_DLG_H */
