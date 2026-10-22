//----------------------------------------------------------------------------------------------------------------------------------
// ui_settings.c - the Settings fold-out's rows and the Settings screen's tables, values and hit areas (see ui_settings.h).
// Host-tested: host_tests/test_ui.c.
//----------------------------------------------------------------------------------------------------------------------------------

#include "ui_settings.h"

//----------------------------------------------------------------------------------------------------------------------------------
//Fold-out

int ui_fo_rows(int rtc, uint8 *ids)
{
  int n = 0;

  ids[n++] = UI_FO_SCREEN;
  ids[n++] = UI_FO_GRID;

  if(rtc)
    ids[n++] = UI_FO_TIME;

  ids[n++] = UI_FO_OTHER;

  return n;
}

uint32 ui_fo_height(int rtc)
{
  uint8 ids[UI_FO_MAX_ROWS];

  return (uint32)ui_fo_rows(rtc, ids) * UI_FO_PITCH;
}

uint32 ui_fo_row_top(int rtc, int id)
{
  uint8 ids[UI_FO_MAX_ROWS];
  int   i, n = ui_fo_rows(rtc, ids);

  for(i=0;i<n;i++)
    if(ids[i] == id)
      return UI_FO_TOP + (uint32)i * UI_FO_PITCH;

  return 0;
}

int ui_fo_row_at(int rtc, uint32 x, uint32 y)
{
  uint8 ids[UI_FO_MAX_ROWS];
  int   n = ui_fo_rows(rtc, ids);

  if((x < UI_FO_X) || (x >= UI_FO_X + UI_FO_W) || (y < UI_FO_TOP) || (y >= UI_FO_TOP + (uint32)n * UI_FO_PITCH))
    return -1;

  return ids[(y - UI_FO_TOP) / UI_FO_PITCH];
}

//----------------------------------------------------------------------------------------------------------------------------------
//Settings screen

#define G_DISPLAY     0
#define G_CONTROLS    1
#define G_FILES       2
#define G_CLOCK       3
#define G_STARTUP     4

#define C0            UI_SET_COL0_X
#define C1            UI_SET_COL1_X

//Rows at y 74, 128 | 198, 252, 306 | 376: a group title 18 px above its first row, 6 px between rows. fw 0.2.59: the
//interim Calibration group (DC shift trim) went to the Diagnostics Calibration page
const ui_set_group ui_set_groups[UI_SET_GROUPS] =
{
  { "Display",                             C0,  56 },
  { "Controls",                            C0, 180 },
  { "Files",                               C1,  56 },
  { "Clock",                               C1, 180 },
  { "Start-up",                            C1, 358 },
};

const ui_set_row ui_set_rows[UI_SET_ROWS] =
{
  { "Screen brightness",            G_DISPLAY,  UI_SET_K_STEP, C0,  74 },
  { "Grid brightness",              G_DISPLAY,  UI_SET_K_STEP, C0, 128 },
  { "File message timeout",         G_CONTROLS, UI_SET_K_TAP,  C0, 198 },
  { "Cursors",                      G_CONTROLS, UI_SET_K_TAP,  C0, 252 },
  { "Auto setup",                   G_CONTROLS, UI_SET_K_TAP,  C0, 306 },
  { "Stamp screenshots with",       G_FILES,    UI_SET_K_TAP,  C1,  74 },
  { "Reference traces at power-on", G_FILES,    UI_SET_K_TAP,  C1, 128 },
  { "Clock",                        G_CLOCK,    UI_SET_K_TAP,  C1, 198 },
  { "Time and date",                G_CLOCK,    UI_SET_K_OPEN, C1, 252 },
  { "At power-on",                  G_STARTUP,  UI_SET_K_TAP,  C1, 376 },
};

static int inside(uint32 x, uint32 y, uint32 x0, uint32 y0, uint32 w, uint32 h)
{
  return (x >= x0) && (x < x0 + w) && (y >= y0) && (y < y0 + h);
}

int ui_page_back_hit(uint32 x, uint32 y)
{
  return inside(x, y, UI_SET_BACK_X, UI_SET_BACK_Y, UI_SET_BACK_W, UI_SET_BACK_H);
}

uint32 ui_set_minus_x(int row)
{
  return ui_set_rows[row].x + UI_SET_ROW_W - 2 * UI_SET_STEP_W - UI_SET_STEP_GAP - 2;
}

int ui_set_hit(uint32 x, uint32 y, int *part)
{
  const ui_set_row *r;
  uint32            mx;
  int               i;

  *part = UI_SET_P_ROW;

  if(ui_page_back_hit(x, y))
    return UI_SET_BACK;

  if(inside(x, y, UI_SET_RESTORE_X, UI_SET_RESTORE_Y, UI_SET_RESTORE_W, UI_SET_RESTORE_H))
    return UI_SET_RESTORE;

  for(i=0;i<UI_SET_ROWS;i++)
  {
    r = &ui_set_rows[i];

    if(!inside(x, y, r->x, r->y, UI_SET_ROW_W, UI_SET_ROW_H))
      continue;

    if(r->kind != UI_SET_K_STEP)
      return i;

    mx = ui_set_minus_x(i);

    if(inside(x, y, mx, r->y + 2, UI_SET_STEP_W, UI_SET_STEP_H))
    {
      *part = UI_SET_P_MINUS;
      return i;
    }

    if(inside(x, y, mx + UI_SET_STEP_W + UI_SET_STEP_GAP, r->y + 2, UI_SET_STEP_W, UI_SET_STEP_H))
    {
      *part = UI_SET_P_PLUS;
      return i;
    }

    return -1;
  }

  return -1;
}

static char *put(char *p, const char *s)
{
  while(*s)
    *p++ = *s++;

  *p = 0;
  return p;
}

static char *put2(char *p, uint32 v)
{
  *p++ = (char)('0' + (v / 10) % 10);
  *p++ = (char)('0' + v % 10);
  *p   = 0;
  return p;
}

static char *putu(char *p, uint32 v)
{
  char   t[10];
  int    n = 0;

  do
  {
    t[n++] = (char)('0' + v % 10);
    v /= 10;
  } while(v);

  while(n)
    *p++ = t[--n];

  *p = 0;
  return p;
}

static int text(char *buf, const char *s, int tone)
{
  put(buf, s);
  return tone;
}

int ui_set_value(int row, const ui_set_state *s, char *buf)
{
  char *p;
  int   tone;

  buf[0] = 0;

  switch(row)
  {
    case UI_SET_SCREEN:
    case UI_SET_GRID:
      p = putu(buf, (row == UI_SET_SCREEN) ? s->screen : s->grid);
      put(p, " %");
      return UI_SET_T_ON;

    case UI_SET_FILEMSG:
      return text(buf, s->filemsg_wait ? "wait for a tap" : "0.7 s", UI_SET_T_ON);

    case UI_SET_CURSORS:
      return text(buf, s->cursors_locked ? "move together" : "move freely", UI_SET_T_ON);

    case UI_SET_AUTOSET:
      return text(buf, s->autoset_new ? "improved" : "stock", UI_SET_T_ON);

    case UI_SET_STAMP:
      if(!s->stamp)
        return text(buf, "nothing", UI_SET_T_OFF);

      return text(buf, (s->rtc_present && s->rtc_on) ? "time" : "file name", UI_SET_T_ON);

    case UI_SET_REFS:
      return s->refs ? text(buf, "loaded", UI_SET_T_ON) : text(buf, "not loaded", UI_SET_T_OFF);

    case UI_SET_CLOCK:
      if(!s->rtc_present)
        return text(buf, "no clock module", UI_SET_T_NA);

      return s->rtc_on ? text(buf, "shown", UI_SET_T_ON) : text(buf, "not used", UI_SET_T_OFF);

    case UI_SET_TIME:
      if(!s->rtc_present)
        return text(buf, "no clock module", UI_SET_T_NA);

      if(!s->rtc_on)
        return text(buf, "needs the clock", UI_SET_T_NA);

      p = put(buf, "20");
      p = put2(p, s->year);
      *p++ = '-';
      p = put2(p, s->month);
      *p++ = '-';
      p = put2(p, s->day);
      *p++ = ' ';
      p = put2(p, s->hour);
      *p++ = ':';
      put2(p, s->minute);
      return UI_SET_T_ON;

    case UI_SET_POWERON:
      put(buf, ui_boot_text(s->boot, &tone));
      return tone;

    default:
      return UI_SET_T_NA;
  }
}

uint8 ui_set_step(uint8 v, int up)
{
  if(up)
    return (v >= 95) ? 100 : (uint8)(v + 5);

  return (v <= 5) ? 0 : (uint8)(v - 5);
}

uint8 ui_bright_sun(uint8 cur, uint8 *back)
{
  if(cur >= UI_BRIGHT_MAX)
    return (*back < UI_BRIGHT_MAX) ? *back : UI_BRIGHT_DEFAULT;

  *back = cur;
  return UI_BRIGHT_MAX;
}

uint16 ui_bright_byte(uint8 back)
{
  return (uint16)(UI_BRIGHT_SECTOR_NEW | ((back < UI_BRIGHT_MAX) ? back : UI_BRIGHT_DEFAULT));
}

void ui_bright_load(uint16 byte, uint8 *cur, uint8 *back)
{
  if(*cur > UI_BRIGHT_MAX)
    *cur = UI_BRIGHT_DEFAULT;

  if(byte & UI_BRIGHT_SECTOR_NEW)
  {
    byte &= 0x7F;
    *back = (byte < UI_BRIGHT_MAX) ? (uint8)byte : UI_BRIGHT_DEFAULT;
    return;
  }

  //1.00o5 / fw <= 0.2.67: the flag. On: the backlight was at maximum and the slider's value is what the sun returns to
  *back = (*cur < UI_BRIGHT_MAX) ? *cur : UI_BRIGHT_DEFAULT;

  if(byte == 1)
    *cur = UI_BRIGHT_MAX;
}

//----------------------------------------------------------------------------------------------------------------------------------
//At power-on

uint8 ui_boot_next(uint8 b)
{
  switch(b)
  {
    case UI_BOOT_OPEN:      return UI_BOOT_MENU_OPEN;
    case UI_BOOT_MENU_OPEN: return UI_BOOT_MENU_STOCK;
    default:                return UI_BOOT_OPEN;
  }
}

const char *ui_boot_text(uint8 b, int *tone)
{
  *tone = UI_SET_T_ON;

  switch(b)
  {
    case UI_BOOT_OPEN:       return "Open firmware";
    case UI_BOOT_MENU_OPEN:  return "Boot menu, then open firmware";
    case UI_BOOT_MENU_STOCK: return "Boot menu, then stock firmware";
  }

  *tone = UI_SET_T_WARN;

  switch(b & 7)
  {
    case 1:  return "Stock firmware, no boot menu";
    case 2:  return "FEL, no boot menu";
    case 6:  return "Boot menu, then FEL";
    default: return "Updater";
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//Time editor

const char *ui_time_labels[UI_TIME_FIELDS] = { "Hour", "Minute", "Day", "Month", "Year" };

int ui_time_hit(uint32 x, uint32 y)
{
  int f;

  for(f=0;f<UI_TIME_FIELDS;f++)
  {
    uint32 y0 = UI_TIME_TOP + (uint32)f * UI_TIME_PITCH;

    if(inside(x, y, UI_TIME_MINUS_X, y0, UI_TIME_BTN_W, UI_TIME_BTN_H))
      return f * 2;

    if(inside(x, y, UI_TIME_PLUS_X, y0, UI_TIME_BTN_W, UI_TIME_BTN_H))
      return f * 2 + 1;
  }

  return -1;
}

//----------------------------------------------------------------------------------------------------------------------------------
//Confirmation dialog

//scope_reset_config_data: every setting in sector 709 back to its default, the calibration (sector 708) reloaded, the
//boot-mode byte untouched since 0.2.55
const ui_dlg_text ui_dlg_restore =
{
  "Restore default settings?",
  { "Channels, trigger, time base, display and these settings go back to their",
    "defaults. Calibration and start-up are kept." },
  2, "Cancel", "Restore", 1
};

const ui_dlg_text ui_dlg_delete =
{
  "Delete from the SD card?",
  { "This cannot be undone." },
  1, "Cancel", "Delete", 1
};

void ui_dlg_layout(int lines, ui_dlg *d)
{
  uint32 pair = 2 * UI_DLG_BTN_W + UI_DLG_BTN_GAP;

  d->w        = UI_DLG_W;
  d->h        = (uint16)(UI_DLG_LINE0_DY + lines * UI_DLG_LINE_PITCH + UI_DLG_PAD + UI_DLG_BTN_H + UI_DLG_PAD);
  d->x        = (uint16)((800 - d->w) / 2);
  d->y        = (uint16)((480 - d->h) / 2);
  d->btn_y    = (uint16)(d->y + d->h - UI_DLG_PAD - UI_DLG_BTN_H);
  d->cancel_x = (uint16)(d->x + (d->w - pair) / 2);
  d->action_x = (uint16)(d->cancel_x + UI_DLG_BTN_W + UI_DLG_BTN_GAP);
}

int ui_dlg_hit(const ui_dlg *d, uint32 x, uint32 y)
{
  if(inside(x, y, d->cancel_x, d->btn_y, UI_DLG_BTN_W, UI_DLG_BTN_H))
    return UI_DLG_CANCEL;

  if(inside(x, y, d->action_x, d->btn_y, UI_DLG_BTN_W, UI_DLG_BTN_H))
    return UI_DLG_ACTION;

  return UI_DLG_NONE;
}
