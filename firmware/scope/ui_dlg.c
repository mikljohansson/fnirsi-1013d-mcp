//----------------------------------------------------------------------------------------------------------------------------------
// ui_dlg.c - drawing of the full-screen pages and the modal dialogs (ui_dlg.h). Target only.
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "display_lib.h"
#include "touchpanel.h"
#include "fnirsi_1013d_scope.h"
#include "variables.h"
#include "pc_hooks.h"
#include "ui_fmt.h"
#include "ui_dlg.h"

//The mockups' palette (docs/plans/ux-makeover.md 6c)
#define RGB_CARD        0x000D0D0D
#define RGB_CARD_EDGE   0x00262626
#define RGB_HEADER      0x00000000      //fw 0.2.63: the Settings screen's header (ui_settings_ui.c), black with a grey rule
#define RGB_HEADER_EDGE 0x00262626
#define RGB_RULE        0x00333333
#define RGB_PANEL       0x00181818
#define RGB_PANEL_EDGE  0x00606060

static PFONTDATA ui_dlg_font(int f)
{
  return (f == UD_F_HEAD) ? &font_5 : ((f == UD_F_BODY) ? &font_3 : &font_2);
}

uint32 ui_dlg_rgb(int colour)
{
  switch(colour)
  {
    case UD_C_SOFT:  return 0x00C9C9C9;
    case UD_C_DIM:   return 0x009A9A9A;
    case UD_C_OK:    return 0x0035C26A;
    case UD_C_WARN:  return 0x00FFD262;
    case UD_C_FAULT: return 0x00FF6B6B;
    case UD_C_CH1:   return CHANNEL1_COLOR;
    case UD_C_CH2:   return CHANNEL2_COLOR;
    case UD_C_TITLE: return WHITE_COLOR;
    default:         return 0x00E6E6E6;
  }
}

void ui_dlg_text(int x, int y, int font, int colour, const char *s)
{
  display_set_font(ui_dlg_font(font));
  display_set_fg_color(ui_dlg_rgb(colour));
  display_text(x, y, (int8 *)s);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Icons, 16 x 16

void ui_dlg_icon(int x, int y, int icon)
{
  uint32 i;

  switch(icon)
  {
    case 1:
      display_set_fg_color(ui_dlg_rgb(UD_C_OK));

      for(i=0;i<3;i++)
      {
        display_draw_line(x + 1, y + 7 + i, x + 6, y + 12 + i);
        display_draw_line(x + 6, y + 12 + i, x + 15, y + 2 + i);
      }
      break;

    case 2:
      display_set_fg_color(ui_dlg_rgb(UD_C_WARN));
      display_fill_rounded_rect(x, y, 16, 16, 7);
      display_set_fg_color(BLACK_COLOR);
      display_fill_rect(x + 7, y + 3, 2, 7);
      display_fill_rect(x + 7, y + 11, 2, 2);
      break;

    case 3:
      display_set_fg_color(ui_dlg_rgb(UD_C_DIM));
      display_draw_rounded_rect(x + 2, y + 2, 12, 12, 5);
      break;

    default:
      break;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Buttons

typedef struct
{
  uint32 fill, edge, text;
  uint8  edge2;                   //a 2 px edge
} ui_btn_look;

static void ui_dlg_look(int style, ui_btn_look *l)
{
  l->edge2 = 0;
  l->text  = 0x00E6E6E6;

  switch(style)
  {
    case UD_B_PRIMARY: l->fill = 0x001B4D2E; l->edge = 0x002F8A52; l->text = WHITE_COLOR; break;
    case UD_B_DANGER:  l->fill = 0x004D1B1B; l->edge = 0x00A33A3A; l->text = 0x00FFD0D0; break;
    case UD_B_TAB_ON:  l->fill = 0x001F3A66; l->edge = 0x009ECBFF; l->text = WHITE_COLOR; l->edge2 = 1; break;
    case UD_B_GREY:    l->fill = 0x00333333; l->edge = 0x00606060; l->text = WHITE_COLOR; break;
    case UD_B_ROW:     l->fill = RGB_CARD;   l->edge = RGB_CARD_EDGE; break;
    default:           l->fill = 0x000F2447; l->edge = 0x001F3A66; break;          //neutral, tab
  }
}

static void ui_dlg_box(int x, int y, int w, int h, uint32 fill, uint32 edge, int edge2)
{
  display_set_fg_color(fill);
  display_fill_rounded_rect(x, y, w, h, 3);
  display_set_fg_color(edge);
  display_draw_rounded_rect(x, y, w, h, 3);

  if(edge2)
    display_draw_rounded_rect(x + 1, y + 1, w - 2, h - 2, 3);
}

static void ui_dlg_centred(int x, int y, int w, int h, int font, uint32 rgb, const char *s)
{
  PFONTDATA f = ui_dlg_font(font);
  int       tw = (int)ui_text_width(f, s);

  display_set_font(f);
  display_set_fg_color(rgb);
  display_text(x + (w - tw) / 2, y + (h - f->height) / 2, (int8 *)s);
}

void ui_dlg_draw_button(const ud_button *b, int pressed)
{
  ui_btn_look l;

  ui_dlg_look(b->style, &l);

  if(pressed)
  {
    l.fill = 0x00CCCCCC;
    l.text = BLACK_COLOR;
  }

  ui_dlg_box(b->x, b->y, b->w, b->h, l.fill, l.edge, l.edge2);

  if(b->style == UD_B_ROW)
  {
    //A settings row: the label at the left, the state word at the right (the line under the label is a page text)
    ui_dlg_text(b->x + 12, b->y + 8, UD_F_BODY, pressed ? UD_C_TEXT : UD_C_TEXT, b->label);

    if(b->note)
    {
      display_set_font(&font_3);
      display_set_fg_color(pressed ? BLACK_COLOR : ui_dlg_rgb(b->note_colour));
      display_text(b->x + b->w - 12 - (int)ui_text_width(&font_3, b->note), b->y + 8, (int8 *)b->note);
    }
    return;
  }

  ui_dlg_centred(b->x, b->y, b->w, b->h, UD_F_BODY, l.text, b->label);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Pages

void ui_dlg_draw_page(const ud_page *pg)
{
  uint32 i;

  display_set_fg_color(BLACK_COLOR);
  display_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

  for(i=0;i<pg->nbox;i++)
  {
    const ud_box *b = &pg->box[i];

    if(b->fill == 1)
    {
      display_set_fg_color(RGB_HEADER);
      display_fill_rect(b->x, b->y, b->w, b->h);
      display_set_fg_color(RGB_HEADER_EDGE);
      display_draw_horz_line(b->y + b->h - 1, b->x, b->x + b->w - 1);
    }
    else if(b->fill == 2)
    {
      display_set_fg_color(RGB_RULE);
      display_fill_rect(b->x, b->y, b->w, b->h);
    }
    else
      ui_dlg_box(b->x, b->y, b->w, b->h, RGB_CARD, RGB_CARD_EDGE, 0);
  }

  for(i=0;i<pg->nbtn;i++)
    ui_dlg_draw_button(&pg->btn[i], 0);

  for(i=0;i<pg->ntext;i++)
  {
    const ud_text *t = &pg->text[i];
    const char    *s = t->text;
    int            x = t->x;

    //Check lines of the first-run screen: an icon, then the text
    if((s[0] == UD_ICON_OK[0]) || (s[0] == UD_ICON_WARN[0]))
    {
      ui_dlg_icon(x, t->y + 1, (s[0] == UD_ICON_OK[0]) ? 1 : 2);
      s++;
      x += UD_ICON_W;
    }

    if(t->right)
      x -= (int)ui_text_width(ui_dlg_font(t->font), s);

    ui_dlg_text(x, t->y, t->font, t->colour, s);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Dialog background

void ui_dlg_bg_begin(void)
{
  uint16 *s = displaybuffer4, *d = displaybuffertmp;
  uint32  i, p;

  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_destination_buffer(displaybuffer4);
  display_copy_rect_from_screen(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

  //RGB565 at 3/8: a quarter plus an eighth, per channel without carries between them
  for(i=0;i<SCREEN_WIDTH * SCREEN_HEIGHT;i++)
  {
    p = s[i];
    d[i] = (uint16)(((p >> 2) & 0x39E7) + ((p >> 3) & 0x18E3));
  }

  display_set_source_buffer(displaybuffertmp);
  display_copy_rect_to_screen(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

void ui_dlg_bg_end(void)
{
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_source_buffer(displaybuffer4);
  display_copy_rect_to_screen(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Step dialog

#define STEP_BTN_Y      (UI_STEP_Y + 232)
#define STEP_BTN_H      52
#define STEP_LEFT_X     (UI_STEP_X + 16)
#define STEP_LEFT_W     160
#define STEP_RIGHT_X    (UI_STEP_X + UI_STEP_W - 16 - 180)
#define STEP_RIGHT_W    180

static void ui_dlg_step_button(const ui_step *d, int which, int pressed)
{
  ud_button b;

  b.y = STEP_BTN_Y;
  b.h = STEP_BTN_H;
  b.note = 0;
  b.note_colour = 0;
  b.id = 0;

  if(which == 1)
  {
    b.x = STEP_LEFT_X;
    b.w = STEP_LEFT_W;
    b.style = UD_B_GREY;
    b.label = d->left;
    ui_dlg_draw_button(&b, pressed);
    return;
  }

  b.x = STEP_RIGHT_X;
  b.w = STEP_RIGHT_W;
  b.label = d->right;

  if(d->right_style == UI_ACT_DISABLED)
  {
    ui_dlg_box(b.x, b.y, b.w, b.h, RGB_PANEL, 0x00333333, 0);
    ui_dlg_centred(b.x, b.y, b.w, b.h, UD_F_BODY, 0x00606060, b.label);
    return;
  }

  b.style = UD_B_PRIMARY;
  ui_dlg_draw_button(&b, pressed);
}

void ui_dlg_step_draw(const ui_step *d)
{
  int x = UI_STEP_X + 24, y = UI_STEP_Y, i;

  display_set_screen_buffer(displaybuffertmp);
  ui_dlg_box(UI_STEP_X, UI_STEP_Y, UI_STEP_W, UI_STEP_H, RGB_PANEL, RGB_PANEL_EDGE, 0);

  ui_dlg_text(x, y + 20, UD_F_HEAD, UD_C_TITLE, d->title);

  for(i=0;i<2;i++)
    if(d->line[i] && d->line[i][0])
      ui_dlg_text(x, y + 64 + 24 * i, UD_F_BODY, UD_C_TITLE, d->line[i]);

  if(d->warn && d->warn[0])
    ui_dlg_text(x, y + 128, UD_F_BODY, UD_C_WARN, d->warn);

  for(i=0;i<2;i++)
  {
    if(d->result[i] && d->result[i][0])
    {
      ui_dlg_icon(x, y + 160 + 30 * i, d->result_icon[i]);
      ui_dlg_text(x + 28, y + 160 + 30 * i, UD_F_BODY, d->result_colour[i], d->result[i]);
    }
  }

  ui_dlg_step_button(d, 1, 0);

  if(d->right)
    ui_dlg_step_button(d, 2, 0);

  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_source_buffer(displaybuffertmp);
  display_copy_rect_to_screen(UI_STEP_X, UI_STEP_Y, UI_STEP_W, UI_STEP_H);
}

int ui_dlg_step_hit(const ui_step *d, int x, int y)
{
  if((y < STEP_BTN_Y) || (y >= STEP_BTN_Y + STEP_BTN_H))
    return 0;

  if((x >= STEP_LEFT_X) && (x < STEP_LEFT_X + STEP_LEFT_W))
    return 1;

  if(d->right && (d->right_style == UI_ACT_PRIMARY) && (x >= STEP_RIGHT_X) && (x < STEP_RIGHT_X + STEP_RIGHT_W))
    return 2;

  return 0;
}

void ui_dlg_step_press(const ui_step *d, int which)
{
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  ui_dlg_step_button(d, which, 1);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Confirmation

#define CONF_X        130
#define CONF_Y        80
#define CONF_W        540
#define CONF_H        320

void ui_dlg_release(void)
{
  tp_i2c_wait_for_touch_release();
}

int ui_dlg_confirm(const char *title, const char *const *lines, int nlines, const char *action, int danger)
{
  ud_button cancel = { CONF_X + 24, CONF_Y + 232, 260, 64, UD_B_TAB_ON, 1, "Cancel", 0, 0 };
  ud_button act    = { CONF_X + 326, CONF_Y + 252, 190, 44, UD_B_PRIMARY, 2, 0, 0, 0 };
  int i, r = -1;

  act.label = action;
  act.style = danger ? UD_B_DANGER : UD_B_PRIMARY;

  ui_dlg_bg_begin();

  display_set_screen_buffer(displaybuffertmp);
  ui_dlg_box(CONF_X, CONF_Y, CONF_W, CONF_H, RGB_PANEL, RGB_PANEL_EDGE, 0);
  ui_dlg_text(CONF_X + 24, CONF_Y + 22, UD_F_HEAD, UD_C_TITLE, title);

  for(i=0;i<nlines;i++)
    ui_dlg_text(CONF_X + 24, CONF_Y + 66 + 24 * i, UD_F_BODY, UD_C_TITLE, lines[i]);

  //The large Cancel is the default; its label in the 24 px font
  ui_dlg_box(cancel.x, cancel.y, cancel.w, cancel.h, 0x001F3A66, 0x009ECBFF, 1);
  ui_dlg_centred(cancel.x, cancel.y, cancel.w, cancel.h, UD_F_HEAD, WHITE_COLOR, "Cancel");
  ui_dlg_draw_button(&act, 0);

  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_source_buffer(displaybuffertmp);
  display_copy_rect_to_screen(CONF_X, CONF_Y, CONF_W, CONF_H);

  //Modal: only the two buttons leave it
  while(r < 0)
  {
    tp_i2c_read_status();

    if(!havetouch)
      continue;

    if((xtouch >= cancel.x) && (xtouch < cancel.x + cancel.w) && (ytouch >= cancel.y) && (ytouch < cancel.y + cancel.h))
    {
      ui_dlg_box(cancel.x, cancel.y, cancel.w, cancel.h, 0x00CCCCCC, 0x009ECBFF, 1);
      ui_dlg_centred(cancel.x, cancel.y, cancel.w, cancel.h, UD_F_HEAD, BLACK_COLOR, "Cancel");
      r = 0;
    }
    else if((xtouch >= act.x) && (xtouch < act.x + act.w) && (ytouch >= act.y) && (ytouch < act.y + act.h))
    {
      ui_dlg_draw_button(&act, 1);
      r = 1;
    }

    ui_dlg_release();
  }

  ui_dlg_bg_end();
  return r;
}
