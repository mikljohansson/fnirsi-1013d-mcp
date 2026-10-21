//----------------------------------------------------------------------------------------------------------------------------------
// ui_ctrl.c - the CTRL page's geometry, the fold-outs, the value names and the warning line (see
// ui_ctrl.h; the spectrum's card set is in ui_meas.c since fw 0.2.65). Host-tested: host_tests/test_ui.c.
//----------------------------------------------------------------------------------------------------------------------------------

#include "ui_ctrl.h"
#include "pc_decim.h"

int ui_p1_shown(int slot, int spectrum)
{
  //fw 0.2.65: MEASURES opens the spectrum's own list there; the cursor slots stay empty
  if((spectrum == 1) && ((slot == UI_P1_TCURSOR) || (slot == UI_P1_VCURSOR)))
    return 0;

  //fw 0.2.69: Frequency response (spectrum = 2): T CURSOR shows the frequency cursors C1 and C2, MEASURES the mode's
  //list; there is no level cursor (V CURSOR empty)
  if((spectrum == 2) && (slot == UI_P1_VCURSOR))
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

const char *const ui_acq_pass_label = "Pass time";
const char *const ui_run_chip[3]     = { "STOP", "RUN", "WAIT" };
const char *const ui_run_action[2]   = { "RUN", "STOP" };

int ui_run_view(int runstate, int single_wait, int host_busy)
{
  if(runstate)
    return single_wait ? UI_RUN_WAIT : UI_RUN_RUN;

  return host_busy ? UI_RUN_RUN : UI_RUN_STOP;
}

const char *const ui_trace_names[UI_TRACE_COUNT]         = { "Linear", "Dots", "Bars" };   //ui-sample: was "Peak"
const char *const ui_view_names[2]                       = { "Normal", "X-Y" };
const char *const ui_spec_view_names[UI_SPEC_VIEW_COUNT] = { "Spectrum", "Waterfall", "Density" };
const char *const ui_spec_chan_names[3]                  = { "CH1", "CH2", "CH1 + CH2" };
const char *const ui_spec_window_names[3]                = { "Rect", "Hann", "Flat top" };
const char *const ui_spec_avg_names[5]                   = { "Off", "4", "16", "64", "Max hold" };
const char *const ui_persist_names[UI_PERSIST_COUNT]     = { "0.2 s", "1 s", "5 s", "Infinite" };
const char *const ui_palette_names[2]                    = { "Mono", "Heat" };
const char *const ui_phos_note_roll                      = "Not in roll mode";

//----------------------------------------------------------------------------------------------------------------------------------
// The ACQ menu's row and note (ui-sample, docs/plans/ui-rows.md section 3)

int ui_acq_peak_applied(const ui_acq_in *in)
{
  return in->peak && !in->spectrum && !in->roll;
}

void ui_acq_row_build(const ui_acq_in *in, ui_acq_row *r)
{
  static const char *const peak_note[UI_ACQ_NOTE_LINES] = { "Peak:", "Filter and", "Average", "not applied" };
  static const char *const roll_note[UI_ACQ_NOTE_LINES] = { "Not in", "roll mode", 0, 0 };
  int peak = ui_acq_peak_applied(in), i;

  r->line1[UI_ACQ_ROLL]    = "Roll";
  r->line2[UI_ACQ_ROLL]    = "mode";
  r->state[UI_ACQ_ROLL]    = in->roll ? UI_ACQ_BOX_ON : UI_ACQ_BOX_OFF;

  //Average: not in roll (1.00o5), not with Peak (averaging max and min words would mix them; SET_SEQ refuses it too)
  r->line1[UI_ACQ_AVERAGE] = "Average";
  r->line2[UI_ACQ_AVERAGE] = "mode";
  r->state[UI_ACQ_AVERAGE] = (in->roll || peak) ? UI_ACQ_BOX_GREY : (in->average ? UI_ACQ_BOX_ON : UI_ACQ_BOX_OFF);

  //Filter: kept behind Peak and back as it was with Normal (the setting byte keeps K / FIR, pc_hal_scope.c)
  r->line1[UI_ACQ_FILTER]  = "Filter";
  r->line2[UI_ACQ_FILTER]  = in->filter_value ? in->filter_value : "";
  r->state[UI_ACQ_FILTER]  = peak ? UI_ACQ_BOX_GREY : (in->filter_on ? UI_ACQ_BOX_ON : UI_ACQ_BOX_OFF);

  //Sample: a Scope setting; the Spectrum's menu has no box (a peak record is pairs of codes, not a signal)
  r->line1[UI_ACQ_SAMPLE]  = "Sample";
  r->line2[UI_ACQ_SAMPLE]  = in->peak ? "Peak" : "Normal";
  r->state[UI_ACQ_SAMPLE]  = in->spectrum ? UI_ACQ_BOX_NONE :
                             (in->roll ? UI_ACQ_BOX_GREY : (in->peak ? UI_ACQ_BOX_ON : UI_ACQ_BOX_OFF));

  for(i = 0; i < UI_ACQ_NOTE_LINES; i++)
    r->note[i] = peak ? peak_note[i] : ((in->roll && !in->spectrum) ? roll_note[i] : 0);
}

uint8 ui_acq_decim_set(uint8 filter_set, int peak, int spectrum)
{
  return (uint8)((filter_set & (PC_DECIM_SET_FIR | 3)) | ((peak && !spectrum) ? PC_DECIM_SET_PEAK : 0));
}

const char *ui_filter_name(uint8 filter_set)
{
  static const char *const names[8] = { "Off", "Hi-Res", "CIC2", "CIC3", "Off", "boxcar+FIR", "CIC2+FIR", "Anti-alias" };

  return names[(filter_set & 3) | ((filter_set & PC_DECIM_SET_FIR) ? 4 : 0)];
}

uint8 ui_filter_next(uint8 filter_set)
{
  filter_set &= PC_DECIM_SET_FIR | 3;

  if(!(filter_set & 3))
    return UI_FILTER_HIRES;

  if(filter_set == UI_FILTER_HIRES)
    return UI_FILTER_ANTIALIAS;

  return UI_FILTER_OFF;
}

uint8 ui_filter_migrate(uint8 filter_set, int spectrum)
{
  if(!spectrum && (filter_set == PC_DECIM_CIC2))
    return PC_DECIM_DEFAULT_SCOPE;

  if(spectrum && (filter_set == (PC_DECIM_CIC2 | PC_DECIM_SET_FIR)))
    return PC_DECIM_DEFAULT_SPEC;

  return filter_set;
}

int ui_acq_box_at(uint32 x, uint32 y)
{
  uint32 i;

  if((y < UI_ACQ_BOX_Y) || (y > UI_ACQ_ROW_HIT_BOTTOM))
    return -1;

  for(i = 0; i < UI_ACQ_BOXES; i++)
    if((x >= UI_ACQ_BOX_X(i)) && (x < UI_ACQ_BOX_X(i) + UI_ACQ_BOX_W))
      return (int)i;

  return -1;
}

int ui_acq_grid_at(uint32 x, uint32 y, uint32 y0, uint32 n)
{
  uint32 i;

  for(i = 0; i < n; i++)
    if((x >= UI_ACQ_GRID_X(i)) && (x < UI_ACQ_GRID_X(i) + UI_ACQ_GRID_W) &&
       (y >= y0 + UI_ACQ_GRID_DY(i)) && (y < y0 + UI_ACQ_GRID_DY(i) + UI_ACQ_GRID_H))
      return (int)i;

  return -1;
}

uint16 ui_word_ui(int grid, int spec_view, int peak)
{
  return (uint16)(0xC700 | (peak ? 8 : 0) | ((spec_view & 3) << 1) | (grid ? 1 : 0));
}

int ui_word_ui_restore(uint16 w, uint8 *grid, uint8 *spec_view, uint8 *peak)
{
  if((w & 0xFFF0) != 0xC700)
    return 0;

  *grid      = (uint8)(w & 1);
  *spec_view = (uint8)((w >> 1) & 3);
  *peak      = (uint8)((w >> 3) & 1);
  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------

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
    return ui_no_trigger_hint(w->trig_type, w->trig_idle);

  return "";
}

//ui-type (docs/plans/ui-rows.md 2.6): the firmware knows only that no trigger came, so the hint names the settings
//of the trigger type to check; the Spectrum's own warning line takes it too (spectrum.c)
const char *ui_no_trigger_hint(uint8 trig_type, uint8 idle_on)
{
  switch(trig_type)
  {
    case 1:
      return "No trigger: check the level and Width";

    case 2:
      return idle_on ? "No trigger: check the level, N and Idle" : "No trigger: check the level and N";

    default:
      return "No trigger: check the trigger level";
  }
}
