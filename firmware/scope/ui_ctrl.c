//----------------------------------------------------------------------------------------------------------------------------------
// ui_ctrl.c - the CTRL page's geometry, the fold-outs, the value names and the warning line (see
// ui_ctrl.h; the spectrum's card set is in ui_meas.c since fw 0.2.65). Host-tested: host_tests/test_ui.c.
//----------------------------------------------------------------------------------------------------------------------------------

#include "ui_ctrl.h"

int ui_p1_shown(int slot, int spectrum)
{
  //fw 0.2.65: MEASURES opens the spectrum's own list there; the cursor slots stay empty
  if(spectrum && ((slot == UI_P1_TCURSOR) || (slot == UI_P1_VCURSOR)))
    return 0;

  return (slot >= UI_P1_CTRL) && (slot <= UI_P1_SAVEWAVE);
}

//Hit areas: each control from the middle of the gap above it to the middle of the gap below it; the steppers split at
//the middle of their centre block
uint32 ui_ctrl_hit_top(int item)
{
  switch(item)
  {
    case UI_CTRL_DISPLAY:   return 58;
    case UI_CTRL_ANALYSIS:  return 120;
    case UI_CTRL_GRID:      return 180;
    case UI_CTRL_CH1_UP:    return 240;
    case UI_CTRL_CH1_DOWN:  return UI_CTRL_STEP1_Y + UI_STEP_H / 2;
    case UI_CTRL_CH2_UP:    return 330;
    case UI_CTRL_CH2_DOWN:  return UI_CTRL_STEP2_Y + UI_STEP_H / 2;
    case UI_CTRL_TRIG50:    return 421;
    default:                return 0;
  }
}

uint32 ui_ctrl_hit_bottom(int item)
{
  switch(item)
  {
    case UI_CTRL_DISPLAY:   return 119;
    case UI_CTRL_ANALYSIS:  return 179;
    case UI_CTRL_GRID:      return 239;
    case UI_CTRL_CH1_UP:    return UI_CTRL_STEP1_Y + UI_STEP_H / 2 - 1;
    case UI_CTRL_CH1_DOWN:  return 329;
    case UI_CTRL_CH2_UP:    return UI_CTRL_STEP2_Y + UI_STEP_H / 2 - 1;
    case UI_CTRL_CH2_DOWN:  return 420;
    case UI_CTRL_TRIG50:    return 479;
    default:                return 0;
  }
}

int ui_ctrl_item_at(uint32 y, int spectrum)
{
  int item;

  for(item = UI_CTRL_DISPLAY; item <= UI_CTRL_TRIG50; item++)
    if((y >= ui_ctrl_hit_top(item)) && (y <= ui_ctrl_hit_bottom(item)))
      return ((item == UI_CTRL_TRIG50) && spectrum) ? UI_CTRL_NONE : item;

  return UI_CTRL_NONE;
}

//----------------------------------------------------------------------------------------------------------------------------------

void ui_fold_layout(ui_fold *f, uint32 w, uint32 rows, uint32 sep, uint32 anchor_y)
{
  uint32 y = (anchor_y > 13 + UI_FOLD_TOP_MIN) ? anchor_y - 13 : UI_FOLD_TOP_MIN;

  if(rows > UI_FOLD_ROWS_MAX)
    rows = UI_FOLD_ROWS_MAX;

  if(sep >= rows)
    sep = 0;

  f->rows = rows;
  f->sep  = sep;
  f->w    = w;
  f->x    = UI_FOLD_RIGHT - w;
  f->h    = UI_FOLD_TITLE + rows * UI_FOLD_PITCH - (UI_FOLD_PITCH - UI_FOLD_ROW_H) + UI_FOLD_PAD + (sep ? UI_FOLD_SEP : 0);

  if(y + f->h > UI_FOLD_BOTTOM_MAX)
    y = UI_FOLD_BOTTOM_MAX - f->h;

  if(y < UI_FOLD_TOP_MIN)
    y = UI_FOLD_TOP_MIN;

  f->y = y;
}

uint32 ui_fold_row_x(const ui_fold *f)
{
  return f->x + UI_FOLD_PAD;
}

uint32 ui_fold_row_w(const ui_fold *f)
{
  return f->w - 2 * UI_FOLD_PAD;
}

uint32 ui_fold_row_y(const ui_fold *f, uint32 row)
{
  return f->y + UI_FOLD_TITLE + row * UI_FOLD_PITCH + ((f->sep && (row >= f->sep)) ? UI_FOLD_SEP : 0);
}

int ui_fold_hit(const ui_fold *f, uint32 x, uint32 y)
{
  uint32 row, gap = (UI_FOLD_PITCH - UI_FOLD_ROW_H) / 2;

  if((x < f->x) || (x >= f->x + f->w) || (y < f->y) || (y >= f->y + f->h))
    return UI_FOLD_OUT;

  for(row = 0; row < f->rows; row++)
  {
    uint32 ry = ui_fold_row_y(f, row);

    if((y + gap >= ry) && (y < ry + UI_FOLD_ROW_H + gap))
      return (int)row;
  }

  return UI_FOLD_IN;
}

int ui_fold_dir(const ui_fold *f, uint32 x)
{
  return (x < f->x + f->w / 2) ? -1 : 1;
}

//----------------------------------------------------------------------------------------------------------------------------------

const char *const ui_trace_names[UI_TRACE_COUNT]         = { "Linear", "Dots", "Peak", "Pro" };
const char *const ui_view_names[2]                       = { "Normal", "X-Y" };
const char *const ui_spec_view_names[UI_SPEC_VIEW_COUNT] = { "Spectrum", "Waterfall", "Density" };
const char *const ui_spec_chan_names[3]                  = { "CH1", "CH2", "CH1 + CH2" };
const char *const ui_spec_window_names[3]                = { "Rect", "Hann", "Flat top" };
const char *const ui_spec_avg_names[5]                   = { "Off", "4", "16", "64", "Max hold" };
const char *const ui_persist_names[UI_PERSIST_COUNT]     = { "0.2 s", "1 s", "5 s", "Infinite" };
const char *const ui_palette_names[2]                    = { "Mono", "Heat" };
const char *const ui_phos_note_roll                      = "Not in roll mode";

const char *ui_display_value(int spectrum, int view, int phosphor)
{
  static const char *const spec[UI_SPEC_VIEW_COUNT] = { "Spec", "Wfall", "Density" };

  if(spectrum)
    return ((view >= 0) && (view < UI_SPEC_VIEW_COUNT)) ? spec[view] : "";

  if(view == UI_VIEW_XY)
    return phosphor ? "XY Ph" : "X-Y";

  return phosphor ? "Phos" : "Normal";
}

//----------------------------------------------------------------------------------------------------------------------------------

const char *ui_scope_warning(const ui_warn_in *w)
{
  if(!w->en[0] && !w->en[1])
    return "No channel enabled: switch on CH1 or CH2";

  if(w->xy && !(w->en[0] && w->en[1]))
    return "X-Y needs both channels: switch on CH1 and CH2";

  if(w->en[0] && w->over[0])
    return "CH1 overrange: increase V/div";

  if(w->en[1] && w->over[1])
    return "CH2 overrange: increase V/div";

  if(w->untrig && w->running)
    return "No trigger: check the trigger level";

  return "";
}
