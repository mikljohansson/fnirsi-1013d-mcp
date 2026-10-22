//----------------------------------------------------------------------------------------------------------------------------------
// ui_settings_ui.c - the Settings screen (Settings fold-out > Other settings; docs/plans/ux-makeover.md section 5) and
// the confirmation dialog (docs/ui-guidelines.md section 3), drawn and run on the device. The tables, values and hit
// areas are in ui_settings.c (host-tested).
//
// The screen is a modal page like the menus (fw 0.2.78: a host request leaves it as Back does): it saves the screen in
// displaybuffer4, owns it until Back, and restores it. Changes take effect at once and are saved with the settings at
// power-off, as before; the start-up choice is written to sector 710 when the screen is left, only if it changed.
// Restore default settings rebuilds the scope screen instead of restoring the saved one.
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "menu.h"
#include "scope_functions.h"
#include "touchpanel.h"
#include "fpga_control.h"
#include "sd_card_interface.h"
#include "display_lib.h"
#include "DS3231.h"
#include "pc_hooks.h"
#include "variables.h"
#include "ui_fmt.h"
#include "ui_settings.h"

extern DISPLAYDATA displaydata;          //display_lib.c: current buffers, colours, font

//The boot-mode byte: byte 31 of sector 710, which the bootloader leaves at DRAM 0x81BFFC00 (firmware/README.md section 3)
#define UI_BOOT_BYTE           ((volatile uint8 *)0x81BFFC1F)
#define UI_BOOT_SECTOR_RAM     ((uint8 *)0x81BFFC00)

//The full-screen pages' look (plan section 6c): black ground, outlined boxes, the state word in colour
#define C_ROW_FILL             0x000D0D0D
#define C_ROW_EDGE             0x00262626
#define C_BTN_FILL             0x000F2447
#define C_BTN_EDGE             0x001F3A66
#define C_PRESSED              0x00CCCCCC
#define C_TEXT_DIM             0x009A9A9A
#define C_ON                   0x0035C26A
#define C_RED_FILL             0x004D1B1B
#define C_RED_EDGE             0x00A33A3A
#define C_GREEN_FILL           0x00145A2E
#define C_BOX_FILL             0x00181818
#define C_BOX_EDGE             0x00606060

static ui_set_state st;

//----------------------------------------------------------------------------------------------------------------------------------
//Drawing helpers

static void text_at(uint32 x, uint32 y, uint32 colour, PFONTDATA font, const char *s)
{
  display_set_font(font);
  display_set_fg_color(colour);
  display_text(x, y, (int8 *)s);
}

static void text_centred(uint32 x, uint32 w, uint32 y, uint32 colour, PFONTDATA font, const char *s)
{
  text_at(x + (w - ui_text_width(font, s)) / 2, y, colour, font, s);
}

static void box(uint32 x, uint32 y, uint32 w, uint32 h, uint32 fill, uint32 edge)
{
  display_set_fg_color(fill);
  display_fill_rounded_rect(x, y, w, h, 4);
  display_set_fg_color(edge);
  display_draw_rounded_rect(x, y, w, h, 4);
}

//A button: label centred in the 16 px font (24 px for one-character labels such as - and +); pressed = light, black text
static void button(uint32 x, uint32 y, uint32 w, uint32 h, const char *label, uint32 fill, uint32 edge, int pressed)
{
  PFONTDATA font = label[1] ? &font_3 : &font_5;

  box(x, y, w, h, pressed ? C_PRESSED : fill, pressed ? C_PRESSED : edge);
  text_centred(x, w, y + (h - (label[1] ? 16 : 24)) / 2, pressed ? BLACK_COLOR : WHITE_COLOR, font, label);
}

static uint32 tone_colour(int tone)
{
  switch(tone)
  {
    case UI_SET_T_ON:   return C_ON;
    case UI_SET_T_WARN: return ORANGE_COLOR;
    default:            return C_TEXT_DIM;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//The firmware's variables in and out

static void gather(void)
{
  st.screen         = scopesettings.screenbrightness;
  st.grid           = scopesettings.gridbrightness;
  st.filemsg_wait   = scopesettings.confirmationmode;
  st.cursors_locked = scopesettings.lockcursors;
  st.autoset_new    = scopesettings.new_autosetup;
  st.stamp          = tag_in_BMP;
  st.refs           = scopesettings.ref_on_startup;
  st.rtc_present    = pc_glue_ui_has(PC_FEAT_RTC) ? 1 : 0;
  st.rtc_on         = onoffRTC;

  //get_fattime reads the module into hour .. year while the clock is on
  if(st.rtc_present && st.rtc_on)
    get_fattime();

  st.hour   = hour;
  st.minute = minute;
  st.day    = day;
  st.month  = month;
  st.year   = year;
}

//----------------------------------------------------------------------------------------------------------------------------------
//The page

void ui_page_back(int pressed)
{
  button(UI_SET_BACK_X, UI_SET_BACK_Y, UI_SET_BACK_W, UI_SET_BACK_H, "Back", C_BTN_FILL, C_BTN_EDGE, pressed);
}

void ui_page_header(const char *title, const char *subtitle)
{
  display_set_fg_color(BLACK_COLOR);
  display_fill_rect(0, 0, 800, 480);

  text_at(12, 14, WHITE_COLOR, &font_5, title);

  if(subtitle)
    text_at(130, 20, C_TEXT_DIM, &font_2, subtitle);

  ui_page_back(0);

  display_set_fg_color(C_ROW_EDGE);
  display_draw_horz_line(UI_SET_HDR_H - 1, 0, 799);
}

//part: which part is drawn pressed (UI_SET_P_MINUS / _PLUS), or the whole row with UI_SET_P_ROW and pressed set
static void draw_row(int row, int part, int pressed)
{
  const ui_set_row *r = &ui_set_rows[row];
  char              v[32];
  int               tone = ui_set_value(row, &st, v);
  uint32            right = r->x + UI_SET_ROW_W - 12;
  uint32            mx;

  box(r->x, r->y, UI_SET_ROW_W, UI_SET_ROW_H, (pressed && (part == UI_SET_P_ROW)) ? C_BTN_EDGE : C_ROW_FILL, C_ROW_EDGE);
  text_at(r->x + 12, r->y + 16, (tone == UI_SET_T_NA) ? C_TEXT_DIM : WHITE_COLOR, &font_3, r->label);

  if(r->kind == UI_SET_K_STEP)
  {
    mx    = ui_set_minus_x(row);
    right = mx - 10;
    button(mx, r->y + 2, UI_SET_STEP_W, UI_SET_STEP_H, "-", C_BTN_FILL, C_BTN_EDGE, pressed && (part == UI_SET_P_MINUS));
    button(mx + UI_SET_STEP_W + UI_SET_STEP_GAP, r->y + 2, UI_SET_STEP_W, UI_SET_STEP_H, "+", C_BTN_FILL, C_BTN_EDGE,
           pressed && (part == UI_SET_P_PLUS));
  }

  text_at(right - ui_text_width(&font_3, v), r->y + 16, tone_colour(tone), &font_3, v);
}

static void draw_page(void)
{
  int i;

  ui_page_header("Settings", 0);

  for(i=0;i<UI_SET_GROUPS;i++)
    text_at(ui_set_groups[i].x + 2, ui_set_groups[i].y, C_TEXT_DIM, &font_2, ui_set_groups[i].title);

  for(i=0;i<UI_SET_ROWS;i++)
    draw_row(i, UI_SET_P_ROW, 0);

  button(UI_SET_RESTORE_X, UI_SET_RESTORE_Y, UI_SET_RESTORE_W, UI_SET_RESTORE_H, "Restore default settings",
         C_BTN_FILL, C_BTN_EDGE, 0);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Time and date: one row per field, - and + write to the module at once (the 1.00o5 functions, as the fold-out's editor)

static void draw_time_value(int f)
{
  char   t[8];
  uint32 v, y = UI_TIME_TOP + (uint32)f * UI_TIME_PITCH;
  uint32 x0 = UI_TIME_MINUS_X + UI_TIME_BTN_W + 1, w = UI_TIME_PLUS_X - x0 - 1;

  switch(f)
  {
    case UI_TIME_HOUR:   v = hour;   break;
    case UI_TIME_MINUTE: v = minute; break;
    case UI_TIME_DAY:    v = day;    break;
    case UI_TIME_MONTH:  v = month;  break;
    default:             v = 2000 + (uint32)year; break;
  }

  if(v >= 1000)
  {
    t[0] = (char)('0' + v / 1000);
    t[1] = (char)('0' + (v / 100) % 10);
    t[2] = (char)('0' + (v / 10) % 10);
    t[3] = (char)('0' + v % 10);
    t[4] = 0;
  }
  else
  {
    t[0] = (char)('0' + (v / 10) % 10);
    t[1] = (char)('0' + v % 10);
    t[2] = 0;
  }

  display_set_fg_color(BLACK_COLOR);
  display_fill_rect(x0, y, w, UI_TIME_BTN_H);
  text_centred(x0, w, y + 10, C_ON, &font_5, t);
}

static void draw_time_page(void)
{
  int    f;
  uint32 y;

  ui_page_header("Time and date", "Each change is written to the clock module at once.");

  for(f=0;f<UI_TIME_FIELDS;f++)
  {
    y = UI_TIME_TOP + (uint32)f * UI_TIME_PITCH;
    text_at(UI_TIME_LABEL_X, y + 14, WHITE_COLOR, &font_3, ui_time_labels[f]);
    button(UI_TIME_MINUS_X, y, UI_TIME_BTN_W, UI_TIME_BTN_H, "-", C_BTN_FILL, C_BTN_EDGE, 0);
    button(UI_TIME_PLUS_X, y, UI_TIME_BTN_W, UI_TIME_BTN_H, "+", C_BTN_FILL, C_BTN_EDGE, 0);
    draw_time_value(f);
  }
}

static void run_time_page(void)
{
  int    hit, f;
  uint32 y;

  get_fattime();
  draw_time_page();

  while(1)
  {
    tp_i2c_read_status();

    //fw 0.2.78: a host request leaves the page as Back does (protocol 1.23); the screen around it leaves too
    if(PC_UI_YIELD())
      return;

    if(!havetouch)
      continue;

    if(ui_page_back_hit(xtouch, ytouch))
    {
      ui_page_back(1);
      tp_i2c_wait_for_touch_release();
      return;
    }

    hit = ui_time_hit(xtouch, ytouch);

    if(hit >= 0)
    {
      f = hit / 2;
      y = UI_TIME_TOP + (uint32)f * UI_TIME_PITCH;

      button((hit & 1) ? UI_TIME_PLUS_X : UI_TIME_MINUS_X, y, UI_TIME_BTN_W, UI_TIME_BTN_H, (hit & 1) ? "+" : "-",
             C_BTN_FILL, C_BTN_EDGE, 1);

      switch(hit)
      {
        case 0: hourDown();   break;
        case 1: hourUp();     break;
        case 2: minuteDown(); break;
        case 3: minuteUp();   break;
        case 4: dayDown();    break;
        case 5: dayUp();      break;
        case 6: monthDown();  break;
        case 7: monthUp();    break;
        case 8: yearDown();   break;
        default: yearUp();    break;
      }

      draw_time_value(f);
      tp_i2c_wait_for_touch_release();
      button((hit & 1) ? UI_TIME_PLUS_X : UI_TIME_MINUS_X, y, UI_TIME_BTN_W, UI_TIME_BTN_H, (hit & 1) ? "+" : "-",
             C_BTN_FILL, C_BTN_EDGE, 0);
      continue;
    }

    tp_i2c_wait_for_touch_release();
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//Restore default settings: scope_reset_config_data (sector 709's settings to defaults, the calibration reloaded from
//sector 708, the boot-mode byte untouched), then the hardware set up from them as at power-on

static void apply_all(void)
{
  fpga_set_channel_enable(&scopesettings.channel1);
  fpga_set_channel_enable(&scopesettings.channel2);
  fpga_set_channel_voltperdiv(&scopesettings.channel1);
  fpga_set_channel_voltperdiv(&scopesettings.channel2);
  fpga_set_channel_coupling(&scopesettings.channel1);
  fpga_set_channel_coupling(&scopesettings.channel2);

  fpga_set_sample_rate(scopesettings.samplerate);

  if(!scopesettings.long_mode)
    fpga_set_time_base(scopesettings.timeperdiv);

  fpga_set_trigger_channel();
  fpga_set_trigger_edge();
  fpga_set_trigger_level();
  fpga_set_trigger_mode();

  fpga_set_channel_offset(&scopesettings.channel1);
  fpga_set_channel_offset(&scopesettings.channel2);

  fpga_on_off_generator();

  fpga_set_translated_brightness();
}

//----------------------------------------------------------------------------------------------------------------------------------

int ui_confirm(const ui_dlg_text *t, int restore)
{
  DISPLAYDATA save = displaydata;
  ui_dlg      d;
  int         i, hit;
  uint32      afill = t->destructive ? C_RED_FILL : C_GREEN_FILL, aedge = t->destructive ? C_RED_EDGE : C_ON;

  ui_dlg_layout(t->nlines, &d);

  if(restore)
  {
    display_set_destination_buffer(displaybuffer4);
    display_copy_rect_from_screen(d.x, d.y, d.w, d.h);
  }

  box(d.x, d.y, d.w, d.h, C_BOX_FILL, C_BOX_EDGE);
  text_at(d.x + 24, d.y + UI_DLG_TITLE_DY, WHITE_COLOR, &font_5, t->title);

  for(i=0;i<t->nlines;i++)
    text_at(d.x + 24, d.y + UI_DLG_LINE0_DY + i * UI_DLG_LINE_PITCH, WHITE_COLOR, &font_3, t->lines[i]);

  button(d.cancel_x, d.btn_y, UI_DLG_BTN_W, UI_DLG_BTN_H, t->cancel, C_BTN_FILL, C_BTN_EDGE, 0);
  button(d.action_x, d.btn_y, UI_DLG_BTN_W, UI_DLG_BTN_H, t->action, afill, aedge, 0);

  //A modal dialog owns the screen: only its two buttons react
  while(1)
  {
    tp_i2c_read_status();

    //fw 0.2.78: a host request answers the dialog with its cancel button (protocol 1.23)
    if(PC_UI_YIELD())
    {
      hit = UI_DLG_CANCEL;
      break;
    }

    if(!havetouch)
      continue;

    hit = ui_dlg_hit(&d, xtouch, ytouch);

    if(hit == UI_DLG_CANCEL)
      button(d.cancel_x, d.btn_y, UI_DLG_BTN_W, UI_DLG_BTN_H, t->cancel, C_BTN_FILL, C_BTN_EDGE, 1);
    else if(hit == UI_DLG_ACTION)
      button(d.action_x, d.btn_y, UI_DLG_BTN_W, UI_DLG_BTN_H, t->action, afill, aedge, 1);

    tp_i2c_wait_for_touch_release();

    if(hit != UI_DLG_NONE)
      break;
  }

  if(restore)
  {
    display_set_source_buffer(displaybuffer4);
    display_copy_rect_to_screen(d.x, d.y, d.w, d.h);
  }

  displaydata = save;
  return hit == UI_DLG_ACTION;
}

//----------------------------------------------------------------------------------------------------------------------------------

void ui_settings_run(void)
{
  DISPLAYDATA save = displaydata;
  uint8       boot_open, rtc_open = onoffRTC, sun_open = scopesettings.screenbrightness, restored = 0;
  int         t, part;
  char        v[32];

  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_destination_buffer(displaybuffer4);
  display_copy_rect_from_screen(0, 0, 800, 480);

  gather();
  st.boot   = *UI_BOOT_BYTE & 0x07;
  boot_open = st.boot;
  draw_page();

  while(1)
  {
    tp_i2c_read_status();

    //fw 0.2.78: a host request leaves the screen as Back does (protocol 1.23); what was changed here is kept, as with Back
    if(PC_UI_YIELD())
      break;

    if(!havetouch)
      continue;

    t = ui_set_hit(xtouch, ytouch, &part);

    if(t == UI_SET_BACK)
    {
      ui_page_back(1);
      tp_i2c_wait_for_touch_release();
      break;
    }

    if(t == UI_SET_RESTORE)
    {
      button(UI_SET_RESTORE_X, UI_SET_RESTORE_Y, UI_SET_RESTORE_W, UI_SET_RESTORE_H, "Restore default settings",
             C_BTN_FILL, C_BTN_EDGE, 1);
      tp_i2c_wait_for_touch_release();

      if(ui_confirm(&ui_dlg_restore, 0))
      {
        scope_reset_config_data();
        apply_all();
        restored = 1;
        gather();
      }

      draw_page();
      continue;
    }

    //A row that has nothing to change (no clock module, clock off) or a touch between targets: nothing happens
    if((t < 0) || (ui_set_value(t, &st, v) == UI_SET_T_NA))
    {
      tp_i2c_wait_for_touch_release();
      continue;
    }

    draw_row(t, part, 1);

    switch(t)
    {
      case UI_SET_SCREEN:
        scope_set_brightness(ui_set_step(scopesettings.screenbrightness, part == UI_SET_P_PLUS), 0);
        break;

      case UI_SET_GRID:
        scopesettings.gridbrightness = ui_set_step(scopesettings.gridbrightness, part == UI_SET_P_PLUS);
        break;

      case UI_SET_FILEMSG:  scopesettings.confirmationmode ^= 1; break;
      case UI_SET_CURSORS:  scopesettings.lockcursors ^= 1;      break;
      case UI_SET_AUTOSET:  scopesettings.new_autosetup ^= 1;    break;
      case UI_SET_STAMP:    tag_in_BMP ^= 1;                     break;
      case UI_SET_REFS:     scopesettings.ref_on_startup ^= 1;   break;
      case UI_SET_CLOCK:    onoffRTC ^= 1;                       break;
      case UI_SET_POWERON:  st.boot = ui_boot_next(st.boot);     break;

      case UI_SET_TIME:
        tp_i2c_wait_for_touch_release();
        run_time_page();
        gather();
        draw_page();
        continue;
    }

    gather();
    tp_i2c_wait_for_touch_release();
    draw_row(t, part, 0);

    //The stamp row names the clock
    if(t == UI_SET_CLOCK)
    {
      draw_row(UI_SET_STAMP, UI_SET_P_ROW, 0);
      draw_row(UI_SET_TIME, UI_SET_P_ROW, 0);
    }
  }

  //Start-up: written only when it changed, and never FEL or the updater (ui_boot_next offers 0, 4 and 5)
  if(st.boot != boot_open)
  {
    *UI_BOOT_BYTE = st.boot;
    sd_card_write(DISPLAY_CONFIG_SECTOR, 1, UI_BOOT_SECTOR_RAM);
  }

  if(restored)
  {
    //The settings changed under the saved picture: build the scope screen again
    display_set_screen_buffer((uint16 *)maindisplaybuffer);
    scope_setup_main_screen();
  }
  else
  {
    display_set_source_buffer(displaybuffer4);
    display_copy_rect_to_screen(0, 0, 800, 480);

    //The top bar's clock: cleared when it was switched off here (as the RTC menu does), the main loop draws it while on
    if(rtc_open && !onoffRTC)
    {
      display_set_fg_color(BLACK_COLOR);
      display_fill_rect(TIME_TEXT_XPOS, TIME_TEXT_YPOS, TIME_TEXT_WIDTH, TIME_TEXT_HEIGHT);
    }

    //fw 0.2.68: the top bar sun shows the brightness set here (yellow at 100 %)
    if(sun_open != scopesettings.screenbrightness)
      scope_maxlight_item(0);
  }

  //The main menu's caller restores the area under the menu from displaybuffer1: give it what is on screen now
  display_set_destination_buffer(displaybuffer1);
  display_copy_rect_from_screen(MAIN_MENU_XPOS, MAIN_MENU_YPOS, MAIN_MENU_WIDTH, MAIN_MENU_HEIGHT);

  displaydata = save;
}
