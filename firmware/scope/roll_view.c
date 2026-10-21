//----------------------------------------------------------------------------------------------------------------------------------
// roll_view.c - the roll trace on the Scope view (roll_view.h)
//
// Every main loop pass in long mode while running: the stream is started for the time base if it does not run for it
// (pc_glue_roll_start: 0x153D logger, both channels, roll_plan's divider), the words the FPGA wrote since the last pass are
// drained and turned into columns (roll.c), and when columns were added the trace area is redrawn: the newest column at
// the right edge (x 726), older ones to the left, one column per pixel, both enabled channels. No busy wait: the
// pass returns at once and the screen moves by the columns that came in (at 100 ms/div one column every 2 ms, so about
// 20 per pass; at 50 s/div one per second).
//
// The stream stops (pc_glue_roll_release) when the UI stops (STOP, Single done, the host's ARM / LOG_START, which stop
// the UI first) and whenever anything else arms the FPGA (every arm releases it); the next running pass starts it again,
// and the first words after a restart count as a gap (the time between is not on the trace).
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "variables.h"
#include "display_lib.h"
#include "menu.h"
#include "scope_functions.h"
#include "fpga_control.h"
#include "pc_hooks.h"
#include "ui_fmt.h"
#include "roll.h"
#include "roll_view.h"

#define ROLL_X_FIRST   4
#define ROLL_X_LAST    726
#define ROLL_COLS      (ROLL_X_LAST - ROLL_X_FIRST + 1)

static roll_model  roll_m;
static uint8       roll_cfg[4];             //mode, trigger channel, edge, time base the model was reset for
static uint8       roll_fresh;              //the stream was (re)started: its first words follow a gap
static const char *roll_fail;               //why the roll stopped (amber, until the next start)
static uint8       roll_words[8192];        //2048 words per read

static void roll_view_draw(void);

void roll_view_reset(void)
{
  uint32_t div, wpc = 1;

  //Viewer-only time bases (20 / 50 ms/div) have no plan: wpc 1, nothing streams there
  (void)roll_plan(scopesettings.timeperdiv, &div, &wpc);

  roll_reset(&roll_m, scopesettings.channel1.tracebuffer, scopesettings.channel2.tracebuffer, wpc,
             scopesettings.triggermode, scopesettings.triggerchannel, scopesettings.triggeredge);

  roll_cfg[0] = scopesettings.triggermode;
  roll_cfg[1] = scopesettings.triggerchannel;
  roll_cfg[2] = scopesettings.triggeredge;
  roll_cfg[3] = scopesettings.timeperdiv;
  triggerlong = (roll_m.state == ROLL_RUN);
}

//The run state's chip and button after the roll changed it
static void roll_view_run_state(void)
{
  scope_run_stop_text();

  if((scopesettings.rightmenustate == 0) && !scopesettings.waveviewmode)
    scope_run_stop_button(0);
}

static void roll_view_stop(const char *why)
{
  pc_glue_roll_release();
  scopesettings.runstate = 0;
  triggerlong = 0;
  roll_fail = why;
  roll_view_run_state();
  roll_view_draw();
}

static void roll_view_corr(roll_corr corr[2])
{
  PCHANNELSETTINGS ch[2] = { &scopesettings.channel1, &scopesettings.channel2 };
  acal_corr c;
  int i;

  for(i=0;i<2;i++)
  {
    fpga_channel_correction(ch[i], &c);
    corr[i].gain_a = (int32)c.gain_a;
    corr[i].gain_b = (int32)c.gain_b;
    corr[i].off_a  = c.off_a;
    corr[i].off_b  = c.off_b;
    corr[i].invert = ch[i]->invert ? 1 : 0;
  }
}

void roll_view_pass(void)
{
  roll_corr corr[2];
  uint32_t  wpc, added;
  uint32    ev = 0, total = 0, k;
  int32     n, level;
  int       gap, r;
  uint8     waiting = !triggerlong;

  if(!scopesettings.runstate)
  {
    pc_glue_roll_release();
    return;
  }

  //The trigger settings or the time base changed without a preset (the trigger menu), or RUN after a Single record: a
  //new record
  if((roll_m.state == ROLL_DONE) || (roll_cfg[0] != scopesettings.triggermode) || (roll_cfg[1] != scopesettings.triggerchannel) ||
     (roll_cfg[2] != scopesettings.triggeredge) || (roll_cfg[3] != scopesettings.timeperdiv))
  {
    roll_view_reset();
    total = 1;
  }

  if(!pc_glue_roll_running(scopesettings.timeperdiv))
  {
    r = pc_glue_roll_start(scopesettings.timeperdiv, &wpc);

    if(r)
    {
      roll_view_stop((r == -2) ? "Roll mode needs FPGA bitstream 0x153D" : "FPGA not ready: roll mode stopped");
      return;
    }

    roll_fail  = 0;
    roll_fresh = 1;
  }

  //The level as the trigger pointer stands now: fpga_set_trigger_level's code, as every short time base arm computes it
  //(the 0x17 write is ignored while streaming); unclamped in scopesettings
  if(roll_m.state == ROLL_WAIT)
    fpga_set_trigger_level();

  level = (int16)scopesettings.triggerlevel;
  roll_m.level = (level < 0) ? 0 : (level > 255) ? 255 : level;
  roll_view_corr(corr);

  for(k=0;k<8;k++)
  {
    n = pc_glue_roll_read(roll_words, sizeof(roll_words), &gap);

    if(n < 0)
    {
      roll_view_stop("FPGA stream stopped: tap RUN to restart");
      return;
    }

    if(n == 0)
      break;

    ev |= roll_feed(&roll_m, roll_words, (uint32)n / ROLL_WORD_BYTES, gap || roll_fresh, corr, &added);
    roll_fresh = 0;
    total += added;

    if(roll_m.state == ROLL_DONE)
      break;
  }

  triggerlong = (roll_m.state == ROLL_RUN);

  if(ev & ROLL_EV_DONE)
  {
    //Single: the record is full (1.00o5: the scope stops)
    pc_glue_roll_release();
    scopesettings.runstate = 0;
    triggerlong = 0;
    roll_view_run_state();
  }
  else if(waiting != !triggerlong)
    scope_run_stop_text();

  if(total || (ev & ROLL_EV_RESTART))
    roll_view_draw();
}

static void roll_view_text(const char *t, uint32 color)
{
  display_set_font(&font_2);
  display_set_fg_color(color);
  display_text(366 - ui_text_width(&font_2, t) / 2, 69, (int8 *)t);
}

static void roll_view_draw(void)
{
  PCHANNELSETTINGS ch[2] = { &scopesettings.channel1, &scopesettings.channel2 };
  uint32 vis = (roll_m.count < ROLL_COLS) ? roll_m.count : ROLL_COLS;
  uint32 first = roll_m.count - vis, i, c;
  int32  x0 = ROLL_X_LAST - (int32)vis + 1, y, ly = 0;

  display_set_screen_buffer(displaybuffertmp);
  display_set_fg_color(BLACK_COLOR);
  display_fill_rect(0, 48, 730, 432);

  if((!scopesettings.waveviewmode) || (scopesettings.gridenable))
    scope_draw_grid();

  for(c=0;c<2;c++)
  {
    if(!ch[c]->enable)
      continue;

    display_set_fg_color(ch[c]->color);

    for(i=0;i<vis;i++)
    {
      y = scope_code_to_y(ch[c], roll_m.buf[c][first + i]);

      if(i)
        display_draw_line(x0 + i - 1, ly, x0 + i, y);
      else
        display_draw_pixel(x0, y);

      ly = y;
    }
  }

  scope_draw_pointers();
  scope_draw_time_cursors();
  scope_draw_volt_cursors();
  scope_display_cursor_measurements();

  //Why it stopped, or that the EXT source does not exist on the stream (the roll runs as Auto then)
  if(roll_fail)
    roll_view_text(roll_fail, ORANGE_COLOR);
  else if(scopesettings.triggermode && (scopesettings.triggerchannel > 1))
    roll_view_text("No EXT trigger in roll mode: runs as Auto", ORANGE_COLOR);

  display_set_source_buffer(displaybuffertmp);
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_copy_rect_to_screen(0, 48, 730, 432);

  //1.00o5's roll left these for a saved roll picture / waveform: the record holds its columns from index 0
  disp_have_trigger = 1;
  disp_first_sample = roll_m.count;
}
