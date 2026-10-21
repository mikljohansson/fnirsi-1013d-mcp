//----------------------------------------------------------------------------------------------------------------------------------
// ui_diag_view.c - Diagnostics as a display mode of the main loop (fw 0.2.59, ui_diag_view.h).
//
// Opened from the main menu, it replaces the scope's pass: the main loop keeps polling the PC protocol and servicing
// the UI, then calls ui_diag_pass instead of the acquisition, the touch handler and the clock. The page is rebuilt from
// the protocol bodies and redrawn once a second ("Updates every second"); taps are hit-tested against the page.
// Only Calibrate (the wizard), the volts calibration and the confirmations are modal (docs/ui-guidelines.md section 4).
//
// The scope screen under it is kept in displaybuffer2 (as the 1.00o5 diagnostics screen did); background redraws of
// the scope (the PC changing a setting, the MCP chip, the battery icon) go there while Diagnostics shows
// (ui_diag_scope_buffer), so Back returns to an up-to-date screen.
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "fnirsi_1013d_scope.h"
#include "variables.h"
#include "display_lib.h"
#include "touchpanel.h"
#include "timer.h"
#include "fpga_control.h"
#include "menu.h"
#include "scope_functions.h"
#include "statemachine.h"
#include "power_and_battery.h"
#include "DS3231.h"
#include "ff.h"
#include "pc_hooks.h"
#include "pc_cmds.h"
#include "pc_hal.h"
#include "pc_ident.h"
#include "pc_report.h"
#include "adc_cal.h"
#include "cal_wiz.h"
#include "ui_diag.h"
#include "ui_dlg.h"
#include "ui_diag_view.h"

#include <string.h>

//pc_diag.c / pc_hal_scope.c (fw 0.2.52 readers, firmware/AGENTS.md section 30); ui_settings_ui.c (its header's ui_dlg_text
//type clashes with ui_dlg.h's function)
void              ui_settings_run(void);
const pc_card_id *pc_glue_card_ident(void);
int               pc_glue_save_report(char *name, uint32 name_max);
void              pc_glue_health(uint8 *body);
pc_session       *pc_glue_session(void);

static uint8   ud_open;
static uint8   ud_page_no;
static uint8   ud_redraw;
static uint8   ud_card_ok;             //the card image was identified (the first page shows "Reading" until then)
static uint32  ud_last_ms;
static uint32  ud_ev_first;            //Log page: newest entries skipped
static ud_page ud_pg;                  //the page on the screen (hit areas)
static ud_note ud_msg;                 //result of the last action, until the page changes

//Snapshot of the protocol bodies
static uint8   ud_health[PC_HEALTH_LEN];
static uint8   ud_cal[2][PC_CAL_RESP_LEN];
static uint32_t ud_link[PC_LINK_COUNTERS];
static uint8   ud_ev_raw[128 * 8];
static char    ud_ev_text[UD_LOG_LINES][96];
static const char *ud_ev_ptr[UD_LOG_LINES];
static uint32_t ud_ev_ms[UD_LOG_LINES];

int ui_diag_active(void)
{
  return ud_open;
}

uint16 *ui_diag_scope_buffer(void)
{
  return ud_open ? displaybuffer2 : (uint16 *)maindisplaybuffer;
}

static void ud_note_set(int colour, const char *a, const char *b)
{
  char *p = ud_msg.text;

  while(*a && (p < ud_msg.text + UD_TEXT_LEN - 1))
    *p++ = *a++;

  while(b && *b && (p < ud_msg.text + UD_TEXT_LEN - 1))
    *p++ = *b++;

  *p = 0;
  ud_msg.colour = (uint8)colour;
}

//----------------------------------------------------------------------------------------------------------------------------------
// The page's inputs

static void ud_events(ud_input *in)
{
  uint32_t total, k, i, j;
  pc_text t;

  k = pc_hal_event_log(ud_ev_raw, 128, &total);

  if(ud_ev_first >= k)
    ud_ev_first = 0;

  in->nevents      = 0;
  in->events_total = total;
  in->events_first = ud_ev_first;

  //Newest first
  for(i=0;(i<UD_LOG_LINES) && (ud_ev_first + i < k);i++)
  {
    j = k - 1 - ud_ev_first - i;
    pc_text_init(&t, ud_ev_text[i], sizeof(ud_ev_text[i]));
    pc_report_event(&t, ud_ev_raw + 8 * j);
    ud_ev_ptr[i] = ud_ev_text[i];
    ud_ev_ms[i]  = ud_ev_raw[8 * j] | (ud_ev_raw[8 * j + 1] << 8) | (ud_ev_raw[8 * j + 2] << 16) |
                   ((uint32)ud_ev_raw[8 * j + 3] << 24);
    in->nevents++;
  }

  in->events_more = (ud_ev_first + in->nevents) < k;
  in->event_text  = ud_ev_ptr;
  in->event_ms    = ud_ev_ms;
}

static void ud_gather(ud_input *in, int card)
{
  pc_session *s = pc_glue_session();
  uint8 level, charging, raw, t;

  memset(in, 0, sizeof(*in));
  pc_glue_health(ud_health);
  pc_cal_body(0, ud_cal[0]);
  pc_cal_body(1, ud_cal[1]);

  in->health        = ud_health;
  in->cal[0]        = ud_cal[0];
  in->cal[1]        = ud_cal[1];
  in->nlink         = pc_link_counters(s, ud_link);
  in->link          = ud_link;
  in->run           = &pc_image_ident;
  in->card          = card ? pc_glue_card_ident() : 0;
  in->sd_sectors    = pc_hal_sd_sectors();
  in->fpga          = s->var.version;
  in->variant       = s->var.variant;
  in->mismatch      = s->var.mismatch;
  in->caps          = s->var.caps;
  in->volts_default = (const int32_t *)signal_adjusters;          //int32 is int, int32_t long on the target: same size
  in->serial        = PC_SERIAL;
  in->dev_mode      = dev_mode;
  in->dc_shift      = dc_shift_cal;

  pc_hal_battery(&level, &charging, &raw);
  in->battery  = level;
  in->charging = charging;

  if(pc_glue_ui_has(PC_FEAT_RTC))
  {
    //DS3231 temperature: sign and magnitude (the 1.00o5 diagnostics screen's reading)
    t = readtemperature();
    in->rtc    = 1;
    in->temp_c = (int8_t)((t & 0x80) ? -(int)(t & 0x7F) : (int)(t & 0x7F));
  }

  if(ud_page_no == UD_PAGE_LOG)
    ud_events(in);
}

//Built in displaybuffertmp, then copied in one go (no black frame)
static void ud_draw(void)
{
  ud_input in;

  ud_gather(&in, ud_card_ok);
  ud_build(&ud_pg, ud_page_no, &in, &ud_msg);

  display_set_screen_buffer(displaybuffertmp);
  ui_dlg_draw_page(&ud_pg);
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_source_buffer(displaybuffertmp);
  display_copy_rect_to_screen(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

  ud_last_ms = timer0ticks;
  ud_redraw  = 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Open and close

void ui_diag_open(void)
{
  //The scope screen to return to
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_destination_buffer(displaybuffer2);
  display_copy_rect_from_screen(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

  ud_open     = 1;
  ud_page_no  = UD_PAGE_STATUS;
  ud_ev_first = 0;
  ud_card_ok  = 0;
  ud_msg.text[0] = 0;
  ud_draw();
}

static void ud_close(void)
{
  ud_open = 0;
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_source_buffer(displaybuffer2);
  display_copy_rect_to_screen(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

int ui_diag_leave(void)
{
  if(!ud_open)
    return 0;

  ud_close();
  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Actions

static void ud_save_report(void)
{
  static char name[48];
  const char *why;
  int r;

  ud_note_set(UD_C_DIM, "Saving the report...", 0);
  ud_draw();

  //fw 0.2.78: a host request meanwhile gets E_BUSY "file save in progress" (protocol 1.23)
  {
    int hold = pc_glue_ui_hold(PC_HOLD_SAVE);

    r = pc_glue_save_report(name, sizeof(name));
    pc_glue_ui_hold(hold);
  }

  if(r == FR_OK)
  {
    ud_note_set(UD_C_OK, "Saved to the card: ", name);
    return;
  }

  switch(r)
  {
    case FR_NOT_READY:       why = "no card"; break;
    case FR_DENIED:          why = "the card is full"; break;
    case FR_EXIST:           why = "100 reports already for this power-on"; break;
    case FR_WRITE_PROTECTED: why = "the card is write-protected"; break;
    default:                 why = "card error"; break;
  }

  ud_note_set(UD_C_WARN, "Report not saved: ", why);
}

//Clear calibration (owner: zeros and gains; the volts factors stay). The 1.00o5 RESET without its volts reset: PWM
//centres 860, no core compensations, the v3 block unbound. Sector 709 gets the centres at the next power-off, after
//which the block on the card no longer binds; 708 is not written.
static void ud_clear_calibration(void)
{
  PCHANNELSETTINGS chs[2] = { &scopesettings.channel1, &scopesettings.channel2 };
  uint32 ch, i;

  for(ch=0;ch<2;ch++)
  {
    for(i=0;i<7;i++)
      chs[ch]->dc_calibration_offset[i] = 860;

    chs[ch]->adc1compensation = 0;
    chs[ch]->adc2compensation = 0;
    fpga_set_channel_offset(chs[ch]);
  }

  adc_cal_loaded = 0;
  cal_wizard_committed();
}

//Appends s at p (NUL-terminated), returns the end
static char *ud_cat(char *p, const char *s)
{
  while(*p)
    p++;

  while(*s)
    *p++ = *s++;

  *p = 0;
  return p;
}

//The scope's settings a volts calibration changes (scope_input_calibration sets them for the measurement)
typedef struct
{
  uint8  enable[2], invert[2], coupling[2], magnification[2], displayvpd[2], samplevpd[2];
  int16  dcoffset[2];
  int32  volts[2][7];
  uint8  triggermode, fail;
  uint32 timeperdiv, samplerate;
} ud_volts_backup;

static void ud_volts_backup_take(ud_volts_backup *b)
{
  PCHANNELSETTINGS chs[2] = { &scopesettings.channel1, &scopesettings.channel2 };
  uint32 ch;

  for(ch=0;ch<2;ch++)
  {
    b->enable[ch]        = chs[ch]->enable;
    b->invert[ch]        = chs[ch]->invert;
    b->coupling[ch]      = chs[ch]->coupling;
    b->magnification[ch] = chs[ch]->magnification;
    b->displayvpd[ch]    = chs[ch]->displayvoltperdiv;
    b->samplevpd[ch]     = chs[ch]->samplevoltperdiv;
    b->dcoffset[ch]      = chs[ch]->dcoffset;
    memcpy(b->volts[ch], chs[ch]->input_calibration, sizeof(b->volts[ch]));
  }

  b->triggermode = scopesettings.triggermode;
  b->timeperdiv  = scopesettings.timeperdiv;
  b->samplerate  = scopesettings.samplerate;
  b->fail        = calibrationfail;
}

//The scope as it was (both on Save and Cancel; volts = 1 also puts the factors back), into the FPGA, and the top bar
//redrawn where the scope screen is kept
static void ud_volts_restore(const ud_volts_backup *b, int volts)
{
  PCHANNELSETTINGS chs[2] = { &scopesettings.channel1, &scopesettings.channel2 };
  uint32 ch;

  for(ch=0;ch<2;ch++)
  {
    chs[ch]->enable            = b->enable[ch];
    chs[ch]->invert            = b->invert[ch];
    chs[ch]->coupling          = b->coupling[ch];
    chs[ch]->magnification     = b->magnification[ch];
    chs[ch]->displayvoltperdiv = b->displayvpd[ch];
    chs[ch]->samplevoltperdiv  = b->samplevpd[ch];
    chs[ch]->dcoffset          = b->dcoffset[ch];

    if(volts)
      memcpy(chs[ch]->input_calibration, b->volts[ch], sizeof(b->volts[ch]));

    fpga_set_channel_enable(chs[ch]);
    fpga_set_channel_coupling(chs[ch]);
    fpga_set_channel_voltperdiv(chs[ch]);
    fpga_set_channel_offset(chs[ch]);
  }

  scopesettings.triggermode = b->triggermode;
  scopesettings.timeperdiv  = b->timeperdiv;
  scopesettings.samplerate  = b->samplerate;
  fpga_set_sample_rate(scopesettings.samplerate);

  if(!scopesettings.long_mode)
    fpga_set_time_base(scopesettings.timeperdiv);

  fpga_set_trigger_mode();

  if(volts)
    calibrationfail = b->fail;

  display_set_screen_buffer(ui_diag_scope_buffer());
  scope_channel_settings(&scopesettings.channel1, 0);
  scope_channel_settings(&scopesettings.channel2, 0);
  scope_acqusition_settings(0);
  scope_trigger_settings(0);
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
}

//Volts calibration (1.00o5's input-divider calibration, scope_input_calibration): both inputs on one DC supply, six
//levels from 300 mV (100 mV/div) to 15 V (5 V/div). A service action (owner): Clear never touches these factors.
//Step dialog like the wizard; Cancel puts the factors back, Save writes sector 708. Returns 1 when it saved.
static uint32 ud_volts_flow(void)
{
  static const char *levels[6] = { "300 mV", "600 mV", "1.5 V", "3 V", "7.5 V", "15 V" };
  ud_volts_backup b;
  ui_step d;
  char   l1[96], st[16], res[96];
  uint32 step = 0, saved = 0, done = 0;
  int    hit, failed = 0;

  ud_volts_backup_take(&b);
  ui_dlg_bg_begin();

  while(!done)
  {
    memset(&d, 0, sizeof(d));
    l1[0] = st[0] = res[0] = 0;
    d.title = "Volts calibration";
    d.left  = "Cancel";

    if(step < 6)
    {
      ud_cat(ud_cat(ud_cat(l1, "Connect CH1 and CH2 (probes at 1X) to a DC supply set to "), levels[step]), ".");
      ud_cat(st, "Step 1 of 6");
      st[5] = (char)('1' + step);
      d.step    = st;
      d.line[0] = l1;
      d.line[1] = "Tap Calibrate.";
      d.right   = "Calibrate";
    }
    else
    {
      d.line[0] = "All six ranges calibrated.";
      d.line[1] = "Save calibration writes the factors to the card.";
      d.right   = "Save calibration";
    }

    if(failed)
    {
      ud_cat(ud_cat(ud_cat(res, "No reading near "), levels[step]), ": check the supply and the probes");
      d.result[0] = res;
      d.result_icon[0] = 2;
      d.result_colour[0] = UD_C_WARN;
    }
    else if(step)
    {
      ud_cat(ud_cat(res, levels[step - 1]), " done");
      d.result[0] = res;
      d.result_icon[0] = 1;
      d.result_colour[0] = UD_C_TITLE;
    }

    ui_dlg_step_draw(&d);

    tp_i2c_read_status();
    while(!havetouch && !PC_UI_YIELD())
      tp_i2c_read_status();

    //fw 0.2.78: a host request cancels it between steps as Cancel does: the factors go back (protocol 1.23)
    if(!havetouch)
    {
      done = 1;
      continue;
    }

    hit = ui_dlg_step_hit(&d, xtouch, ytouch);

    if(!hit)
    {
      ui_dlg_release();
      continue;
    }

    ui_dlg_step_press(&d, hit);
    ui_dlg_release();

    if(hit == 1)
      done = 1;
    else if(step == 6)
    {
      scope_save_input_calibration_data();
      calibrationfail = 0;
      saved = 1;
      done  = 1;
    }
    else
    {
      d.right = "Measuring...";
      d.right_style = UI_ACT_DISABLED;
      ui_dlg_step_draw(&d);

      //1.00o5's step (it draws its own boxes and the top bar: into a spare buffer, the dialog stays). fw 0.2.78: it
      //runs to its end; a host request meanwhile gets E_BUSY "calibration measurement running"
      {
        int hold = pc_glue_ui_hold(PC_HOLD_CAL);

        calibrationopen = (uint8)(3 + step);
        display_set_screen_buffer(displaybuffer3);
        scope_input_calibration();
        display_set_screen_buffer((uint16 *)maindisplaybuffer);
        calibrationopen = 0;
        pc_glue_ui_hold(hold);
      }

      failed = calibrationfail ? 1 : 0;

      if(!failed)
        step++;
    }
  }

  ud_volts_restore(&b, !saved);
  ui_dlg_bg_end();
  return saved;
}

//Calibrate: the wizard, then the volts calibration when its factors are missing (1.00o5 hands over the same way)
static void ud_calibrate(void)
{
  if(cal_wizard_run() && calibrationfail)
    ud_volts_flow();
}

static void ud_touch(void)
{
  static const char *clear_lines[3] = { "Zeros and gains are cleared; the volts-per-range factors are kept.",
                                        "Readings use defaults until you calibrate again.",
                                        "The calibration on the card stops applying at the next power-off." };
  static const char *fel_lines[3]   = { "The scope restarts into the USB boot loader at once.",
                                        "It waits for a PC to load firmware with sunxi-fel.",
                                        "A power cycle boots the card as usual." };
  uint32 i;
  int    id = ud_hit(&ud_pg, xtouch, ytouch);

  if(id == UD_ID_NONE)
  {
    ui_dlg_release();
    return;
  }

  for(i=0;i<ud_pg.nbtn;i++)
    if(ud_pg.btn[i].id == id)
      ui_dlg_draw_button(&ud_pg.btn[i], 1);

  ui_dlg_release();
  ud_redraw = 1;

  switch(id)
  {
    case UD_ID_TAB_STATUS:
    case UD_ID_TAB_CAL:
    case UD_ID_TAB_LOG:
    case UD_ID_TAB_DEV:
      ud_page_no     = (uint8)(id - UD_ID_TAB_STATUS);
      ud_ev_first    = 0;
      ud_msg.text[0] = 0;
      break;

    case UD_ID_BACK:
      ud_close();
      break;

    case UD_ID_CALIBRATE:
      ud_msg.text[0] = 0;
      ud_calibrate();
      break;

    case UD_ID_REPORT:
      ud_save_report();
      break;

    case UD_ID_VOLTS:
      ud_msg.text[0] = 0;

      if(ud_volts_flow())
        ud_note_set(UD_C_OK, "Volts calibration saved.", 0);
      break;

    case UD_ID_CLEAR:
      if(ui_dlg_confirm("Clear calibration?", clear_lines, 3, "Clear calibration", 1))
      {
        ud_clear_calibration();
        ud_note_set(UD_C_WARN, "Calibration cleared: run Calibrate.", 0);
      }
      break;

    case UD_ID_OLDER:
      ud_ev_first += UD_LOG_LINES;     //ud_events wraps to the newest past the oldest
      break;

    case UD_ID_OVERLAY:
      dev_mode = 0;
      break;

    case UD_ID_DCSHIFT:
      dc_shift_cal ^= 1;
      break;

    case UD_ID_FEL:
      if(ui_dlg_confirm("Enter FEL now?", fel_lines, 3, "Enter FEL", 0))
        pc_hal_enter_fel();
      break;

    default:
      break;
  }
}

int ui_diag_pass(void)
{
  if(!ud_open)
    return 0;

  //The battery level for the Battery tile; its icon goes to the kept scope screen
  display_set_screen_buffer(displaybuffer2);
  battery_check_status();
  display_set_screen_buffer((uint16 *)maindisplaybuffer);

  //The first page was drawn before the card was read (one read of the image; cached afterwards)
  if(!ud_card_ok)
  {
    ud_card_ok = 1;
    ud_redraw  = 1;
  }

  if(ud_redraw || ((timer0ticks - ud_last_ms) >= 1000))
    ud_draw();

  tp_i2c_read_status();

  if(havetouch)
    ud_touch();

  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// First run (plan section 6): on a unit without a bound calibration, what was found and what to do

void ui_first_run(int settings_defaults)
{
  ud_input in;
  int      id = UD_ID_NONE, volts_due = calibrationfail;

  ud_page_no = UD_PAGE_STATUS;
  ud_gather(&in, 0);

  if(ud_cal_calibrated(in.cal[0], in.cal[1]))
    return;

  //The scope screen to return to
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_destination_buffer(displaybuffer2);
  display_copy_rect_from_screen(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

  ud_build_first_run(&ud_pg, &in, settings_defaults, volts_due);
  display_set_screen_buffer(displaybuffertmp);
  ui_dlg_draw_page(&ud_pg);
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_source_buffer(displaybuffertmp);
  display_copy_rect_to_screen(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

  //Only its two buttons leave it
  while(id == UD_ID_NONE)
  {
    tp_i2c_read_status();

    //fw 0.2.78: a host request answers it with Later (protocol 1.23)
    if(PC_UI_YIELD())
    {
      id = UD_ID_LATER;
      break;
    }

    if(!havetouch)
      continue;

    id = ud_hit(&ud_pg, xtouch, ytouch);

    if(id != UD_ID_NONE)
    {
      uint32 i;

      for(i=0;i<ud_pg.nbtn;i++)
        if(ud_pg.btn[i].id == id)
          ui_dlg_draw_button(&ud_pg.btn[i], 1);
    }

    ui_dlg_release();
  }

  if(id == UD_ID_CAL_NOW)
    ud_calibrate();

  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_source_buffer(displaybuffer2);
  display_copy_rect_to_screen(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

//----------------------------------------------------------------------------------------------------------------------------------
// FPGA mismatch (fw-one): the notice instead of the scope. Not modal: the main loop keeps polling the PC (GET_INFO and
// GET_HEALTH carry the flag, READ_SCREEN shows this page, the acquisition commands answer E_UNSUPPORTED) and this pass
// draws the page and takes its three buttons. Diagnostics opens over it as from the main menu and its Back comes back
// here; Settings and USB drive are the modal screens of the main menu, the page is redrawn when they return.

static uint8 fm_open, fm_redraw;

void ui_fpga_mismatch_open(void)
{
  fm_open   = 1;
  fm_redraw = 1;
}

static void fm_draw(void)
{
  ud_input in;

  ud_page_no = UD_PAGE_STATUS;
  ud_gather(&in, 0);
  ud_build_fpga_mismatch(&ud_pg, &in);

  display_set_screen_buffer(displaybuffertmp);
  ui_dlg_draw_page(&ud_pg);
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_source_buffer(displaybuffertmp);
  display_copy_rect_to_screen(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  fm_redraw = 0;
}

int ui_fpga_mismatch_pass(void)
{
  uint32 i;
  int    id;

  //Diagnostics took the pass (ui_diag_pass runs first); its Back put this page's picture back on the screen and
  //fm_redraw (set when it opened) builds the page and its hit areas again
  if(!fm_open || ud_open)
    return 0;

  //The battery level, as Diagnostics does: its icon goes to a buffer nobody shows
  display_set_screen_buffer(displaybuffer2);
  battery_check_status();
  display_set_screen_buffer((uint16 *)maindisplaybuffer);

  if(fm_redraw)
    fm_draw();

  tp_i2c_read_status();

  if(!havetouch)
    return 1;

  id = ud_hit(&ud_pg, xtouch, ytouch);

  if(id == UD_ID_NONE)
  {
    ui_dlg_release();
    return 1;
  }

  for(i=0;i<ud_pg.nbtn;i++)
    if(ud_pg.btn[i].id == id)
      ui_dlg_draw_button(&ud_pg.btn[i], 1);

  ui_dlg_release();

  switch(id)
  {
    case UD_ID_DIAG:
      //Keeps this screen to return to (displaybuffer2), takes the passes until Back. It builds its own pages into
      //ud_pg: the notice is built again on the first pass after it, or every tap would be hit-tested against the
      //Diagnostics page (fw 0.2.105: Back left the notice deaf to its three buttons)
      ui_diag_open();
      fm_redraw = 1;
      break;

    case UD_ID_SETTINGS:
      ui_settings_run();
      fm_redraw = 1;
      break;

    case UD_ID_USB:
      //As the main menu's USB drive row (statemachine.c): the screen is kept and put back
      display_set_screen_buffer((uint16 *)maindisplaybuffer);
      display_set_destination_buffer(displaybuffer2);
      display_copy_rect_from_screen(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
      scope_setup_usb_screen();
      display_set_source_buffer(displaybuffer2);
      display_copy_rect_to_screen(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
      fm_redraw = 1;
      break;

    default:
      fm_redraw = 1;
      break;
  }

  return 1;
}
