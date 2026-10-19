//----------------------------------------------------------------------------------------------------------------------------------
// ctrl_page.c - the right bar's CTRL page and its fold-outs (fw 0.2.56, see ctrl_page.h). Drawing and the touch loops;
// the geometry, hit areas and texts come from ui_ctrl.c (host_tests/test_ui.c).
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>

#include "ctrl_page.h"
#include "ui_ctrl.h"
#include "ui_fmt.h"
#include "types.h"
#include "variables.h"
#include "display_lib.h"
#include "touchpanel.h"
#include "menu.h"
#include "scope_functions.h"
#include "statemachine.h"
#include "fnirsi_1013d_scope.h"
#include "spectrum.h"
#include "phosphor.h"
#include "spec_fmt.h"

#define CP_XC               (UI_BAR_X + UI_BAR_W / 2)
#define CP_EDGE_COLOR       0x00505050
#define CP_NOTE_COLOR       0x00A0A0A0

//The panels lie in x 500..727 (the widest, Analyse, is 228 px), y 48..478: that part of the trace area is saved when a
//fold-out opens and put back when it closes
#define CP_SAVE_X           (UI_FOLD_RIGHT - 228)

int ctrl_page_spectrum(void)
{
  return spectrum_mode() != SPECTRUM_MODE_SCOPE;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Text helpers (the fonts are variable width; ui_text_width measures them)

static void cp_text(FONTDATA *font, uint32 x, uint32 y, const char *s)
{
  display_set_font(font);
  display_text(x, y, (int8 *)s);
}

static void cp_text_c(FONTDATA *font, uint32 xc, uint32 y, const char *s)
{
  cp_text(font, xc - ui_text_width(font, s) / 2, y, s);
}

static void cp_text_r(FONTDATA *font, uint32 xr, uint32 y, const char *s)
{
  cp_text(font, xr - ui_text_width(font, s), y, s);
}

//----------------------------------------------------------------------------------------------------------------------------------
// The bar's buttons: 1.00o5's 51 x 50 body; a button that holds a value shows its name in white and the value in pale
// yellow under it; orange (black text) while touched or while its fold-out is open

static void cp_button(uint32 y, int active)
{
  display_set_fg_color(active ? ITEM_ACTIVE_COLOR : DARKGREY_COLOR);
  display_fill_rounded_rect(UI_BAR_X, y, UI_BAR_W, UI_BTN_H, 2);
  display_set_fg_color(CP_EDGE_COLOR);
  display_draw_rounded_rect(UI_BAR_X, y, UI_BAR_W, UI_BTN_H, 2);
}

static void cp_value_button(uint32 y, const char *l1, const char *l2, const char *value, int active)
{
  cp_button(y, active);

  display_set_fg_color(active ? BLACK_COLOR : WHITE_COLOR);

  if(l2)
  {
    cp_text_c(&font_2, CP_XC, y + 4, l1);
    cp_text_c(&font_2, CP_XC, y + 17, l2);
  }
  else
    cp_text_c(&font_2, CP_XC, y + 8, l1);

  display_set_fg_color(active ? BLACK_COLOR : PALEYELLOW_COLOR);
  cp_text_c(&font_2, CP_XC, y + (l2 ? 32 : 27), value);
}

static void cp_display_button(int active)
{
  int sp = ctrl_page_spectrum();

  cp_value_button(UI_CTRL_DISPLAY_Y, "Display", 0,
                  ui_display_value(sp, sp ? spectrum_view() : (scopesettings.xymodedisplay ? UI_VIEW_XY : UI_VIEW_NORMAL),
                                   phosphor_switch()), active);
}

static void cp_analysis_button(int active)
{
  char t[16];

  if(ctrl_page_spectrum())
  {
    spectrum_param_text(SPEC_P_FFT, t);
    fmt_str(t + strlen(t), " pt");
    cp_value_button(UI_CTRL_ANALYSIS_Y, "Analyse", 0, t, active);
  }
  else if(scopesettings.xymodedisplay)
  {
    //X-Y draws the Lissajous with its own line: Trace style does not apply there (fw 0.2.63, owner), grey and inert
    cp_button(UI_CTRL_ANALYSIS_Y, 0);
    display_set_fg_color(LIGHTGREY1_COLOR);
    cp_text_c(&font_2, CP_XC, UI_CTRL_ANALYSIS_Y + 4, "Trace");
    cp_text_c(&font_2, CP_XC, UI_CTRL_ANALYSIS_Y + 17, "style");
    cp_text_c(&font_2, CP_XC, UI_CTRL_ANALYSIS_Y + 32, ui_trace_names[scopesettings.ACQ_trace % UI_TRACE_COUNT]);
  }
  else
    cp_value_button(UI_CTRL_ANALYSIS_Y, "Trace", "style", ui_trace_names[scopesettings.ACQ_trace % UI_TRACE_COUNT], active);
}

//Grid (fw 0.2.63, owner): the whole button is the toggle, green with black text while the grid is on, grey with white
//text while it is off (colour carries the state, the word stays "Grid"); orange while touched
static void cp_grid_button(int active)
{
  uint32 fill = active ? ITEM_ACTIVE_COLOR : scopesettings.gridenable ? GREEN_COLOR : GREY_COLOR;

  display_set_fg_color(fill);
  display_fill_rounded_rect(UI_BAR_X, UI_CTRL_GRID_Y, UI_BAR_W, UI_BTN_H, 2);
  display_set_fg_color(CP_EDGE_COLOR);
  display_draw_rounded_rect(UI_BAR_X, UI_CTRL_GRID_Y, UI_BAR_W, UI_BTN_H, 2);

  display_set_fg_color((active || scopesettings.gridenable) ? BLACK_COLOR : WHITE_COLOR);
  cp_text_c(&font_3, CP_XC, UI_CTRL_GRID_Y + 17, "Grid");
}

//Stepper (owner): a 28 px grey V+ bar, a 26 px centre in the channel colour (grey while the channel is off), a 28 px V-
//bar; pressed 1 = V+, 2 = V-
static void cp_stepper(PCHANNELSETTINGS s, uint32 y, int pressed)
{
  uint32 mid = y + UI_STEP_BAR_H, low = mid + UI_STEP_MID_H;

  display_set_fg_color((pressed == 1) ? ITEM_ACTIVE_COLOR : GREY_COLOR);
  display_fill_rounded_rect(UI_BAR_X, y, UI_BAR_W, UI_STEP_BAR_H, 2);
  display_fill_rect(UI_BAR_X, mid - 2, UI_BAR_W, 2);

  display_set_fg_color((pressed == 2) ? ITEM_ACTIVE_COLOR : GREY_COLOR);
  display_fill_rounded_rect(UI_BAR_X, low, UI_BAR_W, UI_STEP_BAR_H, 2);
  display_fill_rect(UI_BAR_X, low, UI_BAR_W, 2);

  display_set_fg_color(s->enable ? s->color : LIGHTGREY1_COLOR);
  display_fill_rect(UI_BAR_X, mid, UI_BAR_W, UI_STEP_MID_H);

  display_set_fg_color((pressed == 1) ? BLACK_COLOR : WHITE_COLOR);
  cp_text_c(&font_3, CP_XC, y + 6, "V+");
  display_set_fg_color((pressed == 2) ? BLACK_COLOR : WHITE_COLOR);
  cp_text_c(&font_3, CP_XC, low + 6, "V-");
  display_set_fg_color(BLACK_COLOR);
  cp_text_c(&font_3, CP_XC, mid + 5, (const char *)s->buttontext);
}

void ctrl_page_draw(void)
{
  cp_display_button(0);
  cp_analysis_button(0);
  cp_grid_button(0);
  cp_stepper(&scopesettings.channel1, UI_CTRL_STEP1_Y, 0);
  cp_stepper(&scopesettings.channel2, UI_CTRL_STEP2_Y, 0);

  //50 % TRIG in Scope; the slot stays empty in Spectrum (owner)
  if(!ctrl_page_spectrum())
    scope_50_percent_trigger_button(0);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Icons for the Display rows (owner): 24 x 20 px drawn with lines

#define CP_ICON_NONE        0
#define CP_ICON_NORMAL      1
#define CP_ICON_XY          2
#define CP_ICON_PHOSPHOR    3
#define CP_ICON_SPECTRUM    4
#define CP_ICON_WATERFALL   5
#define CP_ICON_DENSITY     6

//A clean square trace
static void cp_icon_trace(uint32 x, uint32 y, uint32 color)
{
  display_set_fg_color(color);
  display_draw_horz_line(y + 15, x, x + 6);
  display_draw_vert_line(x + 6, y + 4, y + 15);
  display_draw_horz_line(y + 4, x + 6, x + 16);
  display_draw_vert_line(x + 16, y + 4, y + 15);
  display_draw_horz_line(y + 15, x + 16, x + 23);
}

static void cp_icon(int icon, uint32 x, uint32 y)
{
  //X-Y (fw 0.2.63, owner): a figure 8 leaning right, the lemniscate x = cos t / (1 + sin^2 t), y = 1.3 sin t cos t /
  //(1 + sin^2 t) turned by 55 degrees, 24 points
  static const uint8 loop[25][2] = { { 17, 18 }, { 14, 19 }, { 12, 18 }, { 11, 16 }, { 10, 14 }, { 11, 12 }, { 12, 9 },
                                     { 12, 7 }, { 13, 5 }, { 12, 3 }, { 11, 1 }, { 9, 0 }, { 6, 1 }, { 4, 4 }, { 4, 6 },
                                     { 5, 8 }, { 7, 9 }, { 9, 9 }, { 11, 9 }, { 14, 10 }, { 16, 10 }, { 18, 11 },
                                     { 19, 13 }, { 19, 15 }, { 17, 18 } };
  static const uint8 bars[8]    = { 3, 5, 16, 6, 4, 10, 3, 2 };
  static const uint8 dots[8]    = { 16, 13, 6, 3, 5, 11, 15, 17 };
  static const uint32 stripes[4] = { 0x00000090, 0x000090FF, 0x00FFFF00, 0x00FF4000 };
  uint32 i;

  switch(icon)
  {
    case CP_ICON_NORMAL:
      cp_icon_trace(x, y, CHANNEL1_COLOR);
      break;

    case CP_ICON_XY:
      display_set_fg_color(XYMODE_COLOR);
      for(i = 0; i < 24; i++)
        display_draw_line(x + 2 + loop[i][0], y + loop[i][1], x + 2 + loop[i + 1][0], y + loop[i + 1][1]);
      break;

    case CP_ICON_PHOSPHOR:
      //A soft graded trace: the same edge three times, dim to bright
      cp_icon_trace(x, y + 2, 0x00505000);
      cp_icon_trace(x, y + 1, 0x00A0A000);
      cp_icon_trace(x, y, CHANNEL1_COLOR);
      break;

    case CP_ICON_SPECTRUM:
      display_set_fg_color(CHANNEL1_COLOR);
      for(i = 0; i < 8; i++)
        display_draw_vert_line(x + 1 + 3 * i, y + 19 - bars[i], y + 19);
      break;

    case CP_ICON_WATERFALL:
      for(i = 0; i < 4; i++)
      {
        display_set_fg_color(stripes[i]);
        display_fill_rect(x, y + 2 + 4 * i, 24, 3);
      }
      break;

    case CP_ICON_DENSITY:
      for(i = 0; i < 8; i++)
      {
        display_set_fg_color(0x00303030 + 0x00181818 * ((i < 4) ? i : 7 - i) + 0x00202000);
        display_fill_rect(x + 3 * i, y + dots[i], 3, 3);
        display_fill_rect(x + 3 * i, y + dots[i] - 3, 2, 2);
      }
      break;

    default:
      break;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Fold-outs

#define CP_ROW_CHOICE       0     //one of a set: green while chosen; a tap chooses and closes
#define CP_ROW_SWITCH       1     //slide switch, a second grey line under the label
#define CP_ROW_VALUE        2     //a tap steps to the next value
#define CP_ROW_VALUE2       3     //- and + at the ends: the half tapped steps down or up
#define CP_ROW_ACTION       4

#define CP_F_DISPLAY        0     //Scope: Normal, X-Y | Phosphor, Persistence, Palette, Clear
#define CP_F_TRACE          1     //Scope: Trace style
#define CP_F_SPEC_DISPLAY   2     //Spectrum: Spectrum, Waterfall, Density
#define CP_F_ANALYSE        3     //Spectrum: Analyse, FFT length, Window, Average | Scale, Ref level, Reset

typedef struct
{
  uint8       kind;
  uint8       on;                 //choice chosen, switch on
  uint8       icon;
  uint8       dim;                //switch not available now: grey, a tap does nothing, the second line says why
  const char *label;
  char        value[20];          //value rows; the switch's second line
} cp_row;

typedef struct
{
  int         id;
  const char *title;
  ui_fold     f;
  uint32      n;
  cp_row      r[UI_FOLD_ROWS_MAX];
} cp_fold;

//The Analyse rows' parameters, top to bottom
static const uint8 cp_analyse_p[7] = { SPEC_P_CHANNEL, SPEC_P_FFT, SPEC_P_WINDOW, SPEC_P_AVG, SPEC_P_SCALE, SPEC_P_REF, SPEC_P_RESET };
static const char *const cp_analyse_l[7] = { "Analyse", "FFT length", "Window", "Average", "Scale", "Ref level", "Reset" };

static cp_row *cp_add(cp_fold *d, int kind, const char *label, int on, int icon)
{
  cp_row *r = &d->r[d->n++];

  r->kind  = (uint8)kind;
  r->label = label;
  r->on    = on ? 1 : 0;
  r->icon  = (uint8)icon;

  return r;
}

//The rows as the settings are now, and the panel for them
static void cp_build(cp_fold *d)
{
  uint32 sep = 0, w = 206, anchor = UI_CTRL_DISPLAY_Y, i;
  cp_row *r;

  memset(d->r, 0, sizeof(d->r));
  d->n = 0;

  switch(d->id)
  {
    case CP_F_DISPLAY:
      d->title = "Display";
      cp_add(d, CP_ROW_CHOICE, ui_view_names[UI_VIEW_NORMAL], !scopesettings.xymodedisplay, CP_ICON_NORMAL);
      cp_add(d, CP_ROW_CHOICE, ui_view_names[UI_VIEW_XY], scopesettings.xymodedisplay, CP_ICON_XY);
      sep = d->n;

      r = cp_add(d, CP_ROW_SWITCH, "Phosphor", phosphor_switch() && phosphor_available(), CP_ICON_PHOSPHOR);

      if(!phosphor_available())
      {
        r->dim = 1;
        fmt_str(r->value, ui_phos_note_roll);
      }
      else
      {
        phosphor_wfm_text(r->value);

        if(phosphor_switch())
        {
          phosphor_param_text(PHOSPHOR_P_PERSIST, cp_add(d, CP_ROW_VALUE, "Persistence", 0, 0)->value);
          phosphor_param_text(PHOSPHOR_P_PALETTE, cp_add(d, CP_ROW_VALUE, "Palette", 0, 0)->value);
          cp_add(d, CP_ROW_ACTION, "Clear", 0, 0);
        }
      }
      break;

    case CP_F_TRACE:
      d->title = "Trace style";
      anchor = UI_CTRL_ANALYSIS_Y;
      for(i = 0; i < UI_TRACE_COUNT; i++)
        cp_add(d, CP_ROW_CHOICE, ui_trace_names[i], scopesettings.ACQ_trace == i, CP_ICON_NONE);
      break;

    case CP_F_SPEC_DISPLAY:
      d->title = "Display";
      for(i = 0; i < UI_SPEC_VIEW_COUNT; i++)
        cp_add(d, CP_ROW_CHOICE, ui_spec_view_names[i], spectrum_view() == (int)i, CP_ICON_SPECTRUM + i);

      //Waterfall and Density show one channel's history (fw 0.2.63, owner): which one, a tap takes the other one; grey
      //while the other channel is off
      if(spectrum_view() != 0)
      {
        sep = d->n;
        r = cp_add(d, CP_ROW_VALUE, "Channel", 0, 0);
        fmt_str(r->value, ui_spec_chan_names[spectrum_hist_channel()]);
        r->dim = !spectrum_hist_can_choose(spectrum_hist_channel() ^ 1);
      }
      break;

    default:
      d->title = "Analyse";
      anchor = UI_CTRL_ANALYSIS_Y;
      w = 228;
      for(i = 0; i < 7; i++)
      {
        r = cp_add(d, (cp_analyse_p[i] == SPEC_P_RESET) ? CP_ROW_ACTION : (cp_analyse_p[i] == SPEC_P_REF) ? CP_ROW_VALUE2 :
                   CP_ROW_VALUE, cp_analyse_l[i], 0, 0);
        spectrum_param_text(cp_analyse_p[i], r->value);
      }
      sep = 4;
      break;
  }

  ui_fold_layout(&d->f, w, d->n, sep, anchor);
}

static void cp_row_draw(const cp_row *r, uint32 rx, uint32 ry, uint32 rw)
{
  int    chosen = (r->kind == CP_ROW_CHOICE) && r->on;
  uint32 ty = ry + (UI_FOLD_ROW_H - 16) / 2;

  display_set_fg_color(chosen ? GREEN_COLOR : LIGHTGREY_COLOR);
  display_fill_rounded_rect(rx, ry, rw, UI_FOLD_ROW_H, 3);

  switch(r->kind)
  {
    case CP_ROW_CHOICE:
      cp_icon(r->icon, rx + 10, ry + 12);
      display_set_fg_color(chosen ? BLACK_COLOR : WHITE_COLOR);
      cp_text(&font_3, rx + (r->icon ? 46 : 14), ty, r->label);
      break;

    case CP_ROW_SWITCH:
      scope_display_slide_button(rx + 8, ry + 11, r->on, GREEN_COLOR);
      cp_icon(r->icon, rx + 60, ry + 4);
      display_set_fg_color(r->dim ? CP_NOTE_COLOR : WHITE_COLOR);
      cp_text(&font_3, rx + UI_FOLD_TEXT_X, ry + 5, r->label);
      display_set_fg_color(CP_NOTE_COLOR);
      cp_text(&font_2, rx + UI_FOLD_TEXT_X, ry + 25, r->value);
      break;

    case CP_ROW_VALUE:
      display_set_fg_color(r->dim ? CP_NOTE_COLOR : WHITE_COLOR);
      cp_text(&font_3, rx + UI_FOLD_VALUE_IN, ty, r->label);
      display_set_fg_color(r->dim ? CP_NOTE_COLOR : PALEYELLOW_COLOR);
      cp_text_r(&font_3, rx + rw - UI_FOLD_VALUE_IN, ty, r->value);
      break;

    case CP_ROW_VALUE2:
      display_set_fg_color(GREY_COLOR);
      display_fill_rounded_rect(rx + 4, ry + 4, 30, UI_FOLD_ROW_H - 8, 2);
      display_fill_rounded_rect(rx + rw - 34, ry + 4, 30, UI_FOLD_ROW_H - 8, 2);
      display_set_fg_color(WHITE_COLOR);
      cp_text_c(&font_3, rx + 19, ty, "-");
      cp_text_c(&font_3, rx + rw - 19, ty, "+");
      cp_text(&font_3, rx + UI_FOLD_TWOWAY_IN, ty, r->label);
      display_set_fg_color(PALEYELLOW_COLOR);
      cp_text_r(&font_3, rx + rw - UI_FOLD_TWOWAY_IN, ty, r->value);
      break;

    default:
      display_set_fg_color(WHITE_COLOR);
      cp_text_c(&font_3, rx + rw / 2, ty, r->label);
      break;
  }
}

static void cp_fold_draw(const cp_fold *d)
{
  const ui_fold *f = &d->f;
  uint32 rx = ui_fold_row_x(f), rw = ui_fold_row_w(f), i;

  display_set_fg_color(DARKGREY_COLOR);
  display_fill_rect(f->x, f->y, f->w, f->h);
  display_set_fg_color(LIGHTGREY_COLOR);
  display_draw_rect(f->x, f->y, f->w, f->h);

  display_set_fg_color(LIGHTGREY2_COLOR);
  cp_text(&font_2, f->x + 10, f->y + 9, d->title);

  if(f->sep)
  {
    display_set_fg_color(GREY_COLOR);
    display_draw_horz_line(ui_fold_row_y(f, f->sep) - 9, rx, rx + rw - 1);
  }

  for(i = 0; i < d->n; i++)
    cp_row_draw(&d->r[i], rx, ui_fold_row_y(f, i), rw);
}

//A tap on row `row` (dir: the half of a two-way row). Returns 1 when the fold-out closes.
static int cp_fold_tap(cp_fold *d, int row, int dir)
{
  switch(d->id)
  {
    case CP_F_DISPLAY:
      if(row < 2)
      {
        scopesettings.xymodedisplay = (row == 1) ? 1 : 0;
        return 1;
      }

      if(row == 2)
      {
        if(!d->r[2].dim)
          phosphor_set_switch(!phosphor_switch());
        return 0;
      }

      //Clear closes, so the picture can be seen refilling
      phosphor_param_step((row == 3) ? PHOSPHOR_P_PERSIST : (row == 4) ? PHOSPHOR_P_PALETTE : PHOSPHOR_P_CLEAR);
      return row == 5;

    case CP_F_TRACE:
      scopesettings.ACQ_trace = (uint8)row;
      return 1;

    case CP_F_SPEC_DISPLAY:
      if(row < UI_SPEC_VIEW_COUNT)
      {
        spectrum_set_view(row);
        return 1;
      }

      if(!d->r[row].dim)
        spectrum_set_hist_channel(spectrum_hist_channel() ^ 1);
      return 0;

    default:
      spectrum_param_step(cp_analyse_p[row], (d->r[row].kind == CP_ROW_VALUE2) ? dir : 1);
      return 0;
  }
}

//Modal like the top bar's dropdowns: the button stays orange while the panel is open; a tap outside the panel closes it
//and does nothing else; a tap between rows does nothing
static void cp_fold_run(int id, void (*button)(int))
{
  cp_fold d;
  int     hit;

  display_set_screen_buffer((uint16 *)maindisplaybuffer);

  d.id = id;
  cp_build(&d);

  button(1);
  tp_i2c_wait_for_touch_release();

  display_set_destination_buffer(displaybuffer2);
  display_copy_rect_from_screen(CP_SAVE_X, UI_FOLD_TOP_MIN, UI_FOLD_RIGHT - CP_SAVE_X, 480 - UI_FOLD_TOP_MIN);
  display_set_source_buffer(displaybuffer2);

  cp_fold_draw(&d);

  while(1)
  {
    tp_i2c_read_status();

    if(!havetouch)
      continue;

    hit = ui_fold_hit(&d.f, xtouch, ytouch);

    if(hit == UI_FOLD_OUT)
      break;

    if(hit >= 0)
    {
      ui_fold old = d.f;
      uint32  lx = xtouch, ly = ytouch;

      //fw 0.2.64: a row acts on release, like every other button, and only when the finger is still on it
      while(havetouch)
      {
        lx = xtouch;
        ly = ytouch;
        tp_i2c_read_status();
      }

      if(ui_fold_hit(&d.f, lx, ly) != hit)
        continue;

      if(cp_fold_tap(&d, hit, ui_fold_dir(&d.f, lx)))
        break;

      //Rows may come or go (the Phosphor switch): put the plot back under the old panel, then draw the new one
      cp_build(&d);
      display_set_source_buffer(displaybuffer2);
      display_copy_rect_to_screen(old.x, old.y, old.w, old.h);
      cp_fold_draw(&d);
    }

    tp_i2c_wait_for_touch_release();
  }

  tp_i2c_wait_for_touch_release();

  display_set_source_buffer(displaybuffer2);
  display_copy_rect_to_screen(CP_SAVE_X, UI_FOLD_TOP_MIN, UI_FOLD_RIGHT - CP_SAVE_X, 480 - UI_FOLD_TOP_MIN);

  //The whole page: a value may show on another button too (Analyse's Reset sets the Display variant back)
  ctrl_page_draw();
}

//----------------------------------------------------------------------------------------------------------------------------------

//V+ / V- on a stepper: the same setting and the same steps as 1.00o5's CTRL page and the channel menu's V/div chips
static void cp_step_vdiv(PCHANNELSETTINGS s, int up)
{
  if(!s->enable)
    return;

  if(up && (s->displayvoltperdiv < 6))
    s->displayvoltperdiv++;
  else if(!up && (s->displayvoltperdiv > 0))
    s->displayvoltperdiv--;
  else
    return;

  scope_channel_settings(s, 0);

  //Only update the FPGA in run mode
  if(scopesettings.runstate)
    match_volt_per_div_settings(s);
}

static void cp_stepper_tap(PCHANNELSETTINGS s, uint32 y, int up)
{
  cp_stepper(s, y, up ? 1 : 2);
  tp_i2c_wait_for_touch_release();
  cp_stepper(s, y, 0);
  cp_step_vdiv(s, up);
}

void ctrl_page_touch(void)
{
  int sp = ctrl_page_spectrum();

  display_set_screen_buffer((uint16 *)maindisplaybuffer);

  switch(ui_ctrl_item_at(ytouch, sp))
  {
    case UI_CTRL_DISPLAY:
      cp_fold_run(sp ? CP_F_SPEC_DISPLAY : CP_F_DISPLAY, cp_display_button);
      break;

    case UI_CTRL_ANALYSIS:
      if(!sp && scopesettings.xymodedisplay)
        tp_i2c_wait_for_touch_release();
      else
        cp_fold_run(sp ? CP_F_ANALYSE : CP_F_TRACE, cp_analysis_button);
      break;

    case UI_CTRL_GRID:
      cp_grid_button(1);
      tp_i2c_wait_for_touch_release();
      scopesettings.gridenable ^= 1;
      cp_grid_button(0);
      break;

    case UI_CTRL_CH1_UP:   cp_stepper_tap(&scopesettings.channel1, UI_CTRL_STEP1_Y, 1); break;
    case UI_CTRL_CH1_DOWN: cp_stepper_tap(&scopesettings.channel1, UI_CTRL_STEP1_Y, 0); break;
    case UI_CTRL_CH2_UP:   cp_stepper_tap(&scopesettings.channel2, UI_CTRL_STEP2_Y, 1); break;
    case UI_CTRL_CH2_DOWN: cp_stepper_tap(&scopesettings.channel2, UI_CTRL_STEP2_Y, 0); break;

    case UI_CTRL_TRIG50:
      scope_50_percent_trigger_button(1);
      tp_i2c_wait_for_touch_release();
      scope_50_percent_trigger_button(0);

      //Only when the scope is running (1.00o5)
      if(scopesettings.runstate)
        scope_do_50_percent_trigger_setup();
      break;

    default:
      tp_i2c_wait_for_touch_release();
      break;
  }
}
