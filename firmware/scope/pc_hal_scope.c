//----------------------------------------------------------------------------------------------------------------------------------
// pc_hal_scope.c - pc_hal.h on the FNIRSI 1013D (1.00o5 settings, fpga_control.c, display) and the firmware glue that owns
// the one pc_session. The transport part of pc_hal.h is in pc_transport.c.
//
// Only built with PC_PROTOCOL=1. Bench tests that must confirm an assumption are marked "BENCH: Tn" (protocol.md 12).
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>

#include "types.h"
#include "fnirsi_1013d_scope.h"
#include "fpga_control.h"
#include "scope_functions.h"
#include "power_and_battery.h"
#include "display_lib.h"
#include "touchpanel.h"
#include "timer.h"
#include "menu.h"
#include "statemachine.h"
#include "variables.h"
#include "usb_interface.h"
#include "ccu_control.h"
#include "interrupt.h"
#include "pc_transport.h"
#include "sd_card_interface.h"
#include "DS3231.h"
#include "build_time.h"

#include "pc_hal.h"
#include "pc_cmds.h"
#include "pc_hooks.h"

//----------------------------------------------------------------------------------------------------------------------------------

//MCP indicator chip: stacked directly above the RUN/STOP chip (x 127, y 22, 38 x 18), same width, in the slot of the
//RTC clock (x 123..170, y 3..15), which is not drawn while the chip shows. Menus open below the top bar (y >= 46).
#define PC_CHIP_X         RUN_STOP_TEXT_XPOS
#define PC_CHIP_Y         3
#define PC_CHIP_W         RUN_STOP_TEXT_WIDTH
#define PC_CHIP_H         17

//0x05 ready polls before ARM gives up (1.00o5 waits forever)
#define PC_READY_POLLS    2000000

static pc_session    pc_sess;
static uint8         pc_ready;
static uint8         pc_rtc_present;        //DS3231 answered its address at boot
static uint16        pc_fpga_word;

extern DISPLAYDATA   displaydata;           //display_lib.c: current buffer, colours, font
extern uint32        cardsectors;           //sd_card_interface.c: card size from the CSD, 0 = no card

//SD_STAGE / SD_WRITE_SECTORS staging buffer (protocol section 14 row 40)
static uint8         pc_sd_stage_buf[65536] __attribute__((aligned(4)));

//Power-key interrupt enable (PE11, power_interrupt_init): held off while the protocol writes SD sectors, so the
//power-off settings save cannot interleave with a write in progress (the SD driver is not reentrant)
#define PC_POWER_EINT_BIT 0x00000800

//----------------------------------------------------------------------------------------------------------------------------------
// Drawing from the protocol (MAIN context, between UI passes). The UI loop keeps its own drawing state in displaydata
//(buffer, colours, font); save it, draw on the visible screen, put it back.

static void pc_draw_begin(DISPLAYDATA *save)
{
  *save = displaydata;
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
}

static void pc_draw_end(const DISPLAYDATA *save)
{
  displaydata = *save;
}

//----------------------------------------------------------------------------------------------------------------------------------

static PCHANNELSETTINGS pc_ch(int ch)
{
  return ch ? &scopesettings.channel2 : &scopesettings.channel1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Front end

void pc_hal_channel_read(int ch, pc_hw_channel *c)
{
  PCHANNELSETTINGS s = pc_ch(ch);
  int i;

  c->enable   = s->enable;
  c->range    = s->samplevoltperdiv;
  c->coupling = s->coupling;
  c->invert   = s->invert;
  c->probe    = s->magnification;
  c->overload = s->ADoverload;
  c->dcoffset = s->dcoffset;

  for(i=0;i<7;i++)
  {
    c->cal_offset[i] = s->dc_calibration_offset[i];
    c->input_cal[i]  = s->input_calibration[i];
  }

  c->adc_a_comp      = s->adc1compensation;
  c->adc_b_comp      = s->adc2compensation;
  c->dc_shift_center = s->dc_shift_center;
  c->dc_shift_size   = s->dc_shift_size;
  c->dc_shift_value  = s->dc_shift_value;
  c->adc_a_rawavg    = s->adc1rawaverage;
  c->adc_b_rawavg    = s->adc2rawaverage;
}

void pc_hal_channel_write(int ch, const pc_hw_channel *c, uint8_t mask)
{
  PCHANNELSETTINGS s = pc_ch(ch);

  if(mask & PC_CH_ENABLE)
  {
    s->enable = c->enable;
    fpga_set_channel_enable(s);
  }

  if(mask & PC_CH_RANGE)
  {
    //Display and sample V/div equal: no software zoom on top of the host's choice
    s->samplevoltperdiv  = c->range;
    s->displayvoltperdiv = c->range;
    fpga_set_channel_voltperdiv(s);
  }

  if(mask & PC_CH_COUPLING)
  {
    s->coupling = c->coupling;
    fpga_set_channel_coupling(s);
  }

  if(mask & PC_CH_INVERT)
    s->invert = c->invert;

  if(mask & PC_CH_PROBE)
    s->magnification = c->probe;

  //The PWM centre depends on the range, so a range change rewrites the offset too (as match_volt_per_div_settings)
  if(mask & (PC_CH_OFFSET | PC_CH_RANGE))
  {
    s->dcoffset = c->dcoffset;
    fpga_set_channel_offset(s);

    //The UI keeps the trigger level as a marker position: the level follows range and offset, as on the screen
    fpga_set_trigger_level();
  }

  //Show it: channel button in the top bar (the traces redraw every UI pass)
  {
    DISPLAYDATA save;

    pc_draw_begin(&save);
    scope_channel_settings(s, 0);
    pc_draw_end(&save);
  }
}

uint32_t pc_hal_mul_factor_1x(uint8_t range)
{
  return volt_calc_data[1][(range < 7) ? range : 6].mul_factor;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Timebase

void pc_hal_timebase_read(pc_hw_timebase *t)
{
  t->timeperdiv = scopesettings.timeperdiv;
  t->samplerate = scopesettings.samplerate;
  t->divider    = (scopesettings.samplerate < (sizeof(sample_rate_settings) / sizeof(uint32))) ? sample_rate_settings[scopesettings.samplerate] : 0;
}

int pc_hal_timebase_lookup(uint8_t index, uint8_t *samplerate, uint32_t *divider)
{
  //Always the first table (time_per_div_sample_rate, not the _15k one): P-8.3
  if(index >= (sizeof(time_per_div_sample_rate) / sizeof(time_per_div_sample_rate[0])))
    return -1;

  *samplerate = time_per_div_sample_rate[index];

  if(*samplerate >= (sizeof(sample_rate_settings) / sizeof(uint32)))
    return -1;

  *divider = sample_rate_settings[*samplerate];
  return 0;
}

void pc_hal_timebase_write(const pc_hw_timebase *t, int raw)
{
  if(raw)
  {
    fpga_write_cmd(0x0D);
    fpga_write_int(t->divider);
    return;
  }

  //The target index in one step (owner, 0.2.1: going through scope_set_timebase flickered before settling): index,
  //rate and short mode first, then the FPGA, then one redraw of the label and the trace area. Same effect as the
  //short branch of scope_set_timebase in RUN, without its single-mode re-arm. Host indices are 11..34 (short only).
  {
    DISPLAYDATA save;

    scopesettings.timeperdiv = t->timeperdiv;
    scopesettings.samplerate = t->samplerate;
    scopesettings.long_mode  = 0;
    fpga_set_time_base(scopesettings.timeperdiv);
    fpga_set_sample_rate(scopesettings.samplerate);

    //The next UI pass arms a fresh conversion at the new rate
    scopesettings.display_data_done = 1;

    pc_draw_begin(&save);
    scope_preset_values();
    scope_acqusition_settings(0);
    pc_draw_end(&save);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Trigger

void pc_hal_trigger_read(pc_hw_trigger *t)
{
  int32 level = (int16)scopesettings.triggerlevel;      //the UI stores the unclamped level

  t->source     = scopesettings.triggerchannel;
  t->edge       = scopesettings.triggeredge;
  t->level      = (level < 0) ? 0 : ((level > 255) ? 255 : (uint8_t)level);
  t->ui_mode    = scopesettings.triggermode;
  t->pretrigger = fpgasettings.settriggerpoint;
  t->total      = fpgasettings.totalsamples;
}

//The UI derives the level from the marker position (fpga_set_trigger_level, before every UI conversion). Place the
//marker where the derivation gives the requested level, or the nearest reachable one (marker 0..401).
static int32 pc_level_from_marker(PCHANNELSETTINGS c, int32 marker)
{
  int32 dcoffset = c->dc_shift_size ? (c->dcoffset * 100) / c->dc_shift_size : 0;

  return ((((int32)(marker - dcoffset) - (int32)c->traceposition) * 2097152) / signal_adjusters[c->samplevoltperdiv]) + 128;
}

static void pc_set_trigger_marker(uint8_t level)
{
  PCHANNELSETTINGS c = scopesettings.triggerchannel ? &scopesettings.channel2 : &scopesettings.channel1;
  int32 dcoffset, est, m, best, bestd = 0x7FFFFFFF;

  if(c->samplevoltperdiv > 6)
    return;

  dcoffset = c->dc_shift_size ? (c->dcoffset * 100) / c->dc_shift_size : 0;
  est      = (int32)(((int64)((int32)level - 128) * signal_adjusters[c->samplevoltperdiv]) / 2097152) + (int32)c->traceposition + dcoffset;
  best     = (est < 0) ? 0 : ((est > 401) ? 401 : est);

  for(m=est-4;m<=est+4;m++)
  {
    int32 d;

    if((m < 0) || (m > 401))
      continue;

    d = pc_level_from_marker(c, m) - (int32)level;

    if(d < 0)
      d = -d;

    if(d < bestd)
    {
      bestd = d;
      best  = m;
    }
  }

  scopesettings.triggerverticalposition = (uint16)best;
  fpga_set_trigger_level();
}

void pc_hal_trigger_write(const pc_hw_trigger *t, uint8_t mask)
{
  DISPLAYDATA save;

  if(mask & PC_TRG_SOURCE)
  {
    //Atlan4 bitstreams compare core A only; the stock trigger core is unverified       BENCH: T10
    scopesettings.triggerchannel = t->source;
    fpga_set_trigger_channel();
    fpga_set_trigger_level();
  }

  if(mask & PC_TRG_EDGE)
  {
    scopesettings.triggeredge = t->edge;
    fpga_set_trigger_edge();
  }

  if(mask & PC_TRG_LEVEL)
    pc_set_trigger_marker(t->level);

  if(mask & PC_TRG_MODE)
  {
    scopesettings.triggermode = t->ui_mode;
    fpga_set_trigger_mode();
  }

  //Geometry is written at every arm (UI conversion or host ARM), inside the sample-system reset
  if(mask & PC_TRG_PRE)
    fpgasettings.settriggerpoint = t->pretrigger;

  if(mask & PC_TRG_TOTAL)
    fpgasettings.totalsamples = t->total;

  //Show it: trigger button, RUN/WAIT colour of the run chip
  pc_draw_begin(&save);
  scope_trigger_settings(0);
  scope_run_stop_text();
  pc_draw_end(&save);
}

uint32_t pc_hal_auto_timeout_default(uint32_t total_samples)
{
  //fpga_set_time_base: stock uses the time base table, Atlan4 bitstreams the sample count
  if((fpgasettings.fw_FPGA == 1) && (scopesettings.timeperdiv < (sizeof(timebase_settings) / sizeof(uint32))))
    return timebase_settings[scopesettings.timeperdiv];

  return total_samples;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Generator

void pc_hal_gen_read_ui(uint8_t *enable, uint32_t *freq_hz, uint8_t *duty_pct)
{
  *enable   = fpgasettings.gen_enable;
  *freq_hz  = fpgasettings.gen_freq;
  *duty_pct = fpgasettings.gen_duty;
}

void pc_hal_gen_write(uint32_t phase_inc, uint32_t threshold, uint8_t enable)
{
  //Duty first, then frequency, then enable: no burst at the old duty (P-8.6)
  fpga_write_cmd(0x52);
  fpga_write_int(threshold);
  fpga_write_cmd(0x51);
  fpga_write_int(phase_inc);
  fpga_write_cmd(0x50);
  fpga_write_int(enable ? 1 : 0);

  //UI copy, rounded to the UI's units (Hz, %), so the generator menu and the power-off save show the host's values
  {
    DISPLAYDATA save;

    fpgasettings.gen_enable = enable ? 1 : 0;
    fpgasettings.gen_freq   = (uint32)(((uint64)phase_inc * 50000000ull + 0x80000000ull) >> 32);
    fpgasettings.gen_duty   = (uint8)(((uint64)threshold * 100 + 0x7FFFFFFFull) / 0xFFFFFFFFull);

    pc_draw_begin(&save);
    scope_generator_settings(0);
    pc_draw_end(&save);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Acquisition

int pc_hal_arm(uint16_t pre_words, uint16_t total_words, uint32_t auto_timeout, int prog, int normal)
{
  uint32 n = 0;

  //Trigger level from the marker, as every UI conversion does (scope_acquire_trace_data)
  fpga_set_trigger_level();

  //Trigger enable (dead on Atlan4 bitstreams, meaningful on stock)                    BENCH: T6, T10
  fpga_write_cmd(0x0F);
  fpga_write_byte(0x00);

  //Configured trigger mode again: FORCE leaves 0x1A in auto
  fpga_write_cmd(0x1A);
  fpga_write_byte(normal ? 0x01 : 0x00);

  //Reset the sample system and wait for ready, bounded
  fpga_write_cmd(0x01);
  fpga_write_byte(0x01);
  fpga_write_cmd(0x05);

  while((fpga_read_byte() & 1) == 0)
  {
    if(++n >= PC_READY_POLLS)
    {
      fpga_write_cmd(0x01);
      fpga_write_byte(0x00);
      return -1;
    }
  }

  //Geometry while in reset (1.00o5: "send when the system is in reset")
  if(prog)
  {
    fpga_write_cmd(0x0B);
    fpga_write_short(pre_words);
    fpga_write_cmd(0x0C);
    fpga_write_short(total_words);
  }

  fpga_write_cmd(0x0E);
  fpga_write_int(auto_timeout);

  //Release
  fpga_write_cmd(0x01);
  fpga_write_byte(0x00);
  return 0;
}

void pc_hal_acq_poll(int *done, int *triggered)
{
  fpga_write_cmd(0x0A);
  *done = fpga_read_byte() & 1;

  fpga_write_cmd(0x18);                                                              //BENCH: T6 (0x18 on stock)
  *triggered = fpga_read_byte() & 1;
}

uint16_t pc_hal_acq_finish(void)
{
  //As fpga_done_conversion in trigger mode: disable the trigger system once done
  fpga_write_cmd(0x0F);
  fpga_write_byte(0x01);

  return fpga_prepare_for_transfer();
}

void pc_hal_force(void)
{
  //Auto mode: the Atlan4 stop condition ~trigger_mode && time_base_timeout becomes true    BENCH: T7
  fpga_write_cmd(0x1A);
  fpga_write_byte(0x00);
}

void pc_hal_read_core(uint8_t core, uint32_t start, uint32_t count, uint8_t *dst, uint32_t stride,
                      uint32_t ring_words, uint8_t read_lag)
{
  while(count)
  {
    //Never rely on the FPGA read pointer wrapping (EF2 wraps at 6144): restart at the ring end
    uint32 n = ring_words - start;

    if(n > count)
      n = count;

    //BENCH: T3 - pointer + read_lag, then one dummy strobe for the stale pipeline byte
    fpga_write_cmd(0x1F);
    fpga_write_short((uint16)((start + read_lag) % ring_words));
    fpga_write_cmd(0x20 + core);

    FPGA_BUS_DIR_IN();
    FPGA_DATA_READ();

    FPGA_PULSE_CLK();

    count -= n;

    while(n--)
    {
      FPGA_PULSE_CLK();
      *dst = FPGA_GET_DATA();
      dst += stride;
    }

    start = 0;
  }
}

void pc_hal_fpga_write_cmd(uint8_t cmd)   { fpga_write_cmd(cmd); }
void pc_hal_fpga_write_data(uint8_t data) { fpga_write_byte(data); }
uint8_t pc_hal_fpga_read_data(void)       { return fpga_read_byte(); }

//----------------------------------------------------------------------------------------------------------------------------------
// Processed trace

int32_t pc_hal_trace_build(uint32_t start_word, uint32_t total_samples, uint32_t pretrigger_samples, uint8_t ch_mask)
{
  //The 1.00o5 readout (no dummy read, so the output matches what the UI shows: P-7.4)   BENCH: T3, T4
  //Its interleave is B before A (fpga_read_sample_data), so READ_TRACE follows the 1.00o5 convention.
  scopesettings.nofsamples  = total_samples / 2;
  scopesettings.samplecount = total_samples;

  if(ch_mask & 0x01)
    fpga_read_sample_data(&scopesettings.channel1, start_word);

  if(ch_mask & 0x02)
    fpga_read_sample_data(&scopesettings.channel2, start_word);

  //scope_process_trigger looks at samples pre - 20 .. pre + 60
  if((pretrigger_samples >= 20) && (pretrigger_samples + 64 <= total_samples))
  {
    scope_process_trigger(pretrigger_samples);

    if(disp_have_trigger)
      return (int32_t)disp_trigger_index;
  }

  return -1;
}

const uint8_t *pc_hal_trace(int ch)
{
  return ch ? (const uint8_t *)channel2tracebuffer : (const uint8_t *)channel1tracebuffer;
}

void pc_hal_trace_ui_info(uint32_t *sample_count, uint32_t *pretrigger, int32_t *sw_trigger_index)
{
  *sample_count     = scopesettings.samplecount;
  *pretrigger       = fpgasettings.settriggerpoint;
  *sw_trigger_index = disp_have_trigger ? (int32_t)disp_trigger_index : -1;
}

uint32_t pc_hal_trace_capacity(void)
{
  return sizeof(channel1tracebuffer);
}

//----------------------------------------------------------------------------------------------------------------------------------
// System

uint32_t pc_hal_ms(void)              { return timer0ticks; }
void     pc_hal_delay_ms(uint32_t ms) { timer0_delay(ms); }
uint16_t pc_hal_fpga_version(void)    { return pc_fpga_word; }

void pc_hal_battery(uint8_t *level, uint8_t *charging, uint8_t *keyadc_raw)
{
  *level      = scopesettings.batterychargelevel;
  *charging   = scopesettings.batterycharging;
  *keyadc_raw = (uint8_t)(*KEY_ADC_DATA_REG & 0x3F);
}


//----------------------------------------------------------------------------------------------------------------------------------
// Shared control with the UI

void pc_hal_ui_read(pc_hw_ui *u)
{
  u->run       = scopesettings.runstate;
  u->ui_armed  = scopesettings.runstate && !scopesettings.display_data_done;
  u->long_mode = scopesettings.long_mode;
}

void pc_hal_ui_set_run(int run)
{
  DISPLAYDATA save;

  pc_draw_begin(&save);
  scopesettings.runstate = run ? 1 : 0;

  //What the RUN/STOP button does on RUN (statemachine.c)
  if(run)
  {
    if(scopesettings.long_mode)
      scope_preset_values();

    scope_acqusition_settings(0);
    match_volt_per_div_settings(&scopesettings.channel1);
    match_volt_per_div_settings(&scopesettings.channel2);
  }

  //The next UI pass arms a fresh conversion; one in flight is abandoned
  scopesettings.display_data_done = 1;
  scope_run_stop_text();
  pc_draw_end(&save);
}

//Capture-relevant settings (protocol section 14, settings_seq). The trigger level counts unless the UI's "always 50%"
//rewrites it after every capture; the flag counts instead.
uint32_t pc_hal_settings_fingerprint(void)
{
  uint8  b[40];
  uint32 n = 0;
  int    i;

  for(i=0;i<2;i++)
  {
    PCHANNELSETTINGS c = pc_ch(i);

    b[n++] = c->enable;
    b[n++] = c->samplevoltperdiv;
    b[n++] = c->coupling;
    b[n++] = c->invert;
    b[n++] = c->magnification;
    b[n++] = (uint8)c->dcoffset;
    b[n++] = (uint8)(c->dcoffset >> 8);
  }

  b[n++] = scopesettings.timeperdiv;
  b[n++] = scopesettings.samplerate;
  b[n++] = scopesettings.long_mode;
  b[n++] = scopesettings.triggerchannel;
  b[n++] = scopesettings.triggeredge;
  b[n++] = scopesettings.triggermode;
  b[n++] = scopesettings.alwaystrigger50;
  b[n++] = scopesettings.alwaystrigger50 ? 0 : (uint8)scopesettings.triggerlevel;
  b[n++] = scopesettings.alwaystrigger50 ? 0 : (uint8)(scopesettings.triggerlevel >> 8);
  b[n++] = (uint8)fpgasettings.settriggerpoint;
  b[n++] = (uint8)(fpgasettings.settriggerpoint >> 8);
  b[n++] = (uint8)fpgasettings.totalsamples;
  b[n++] = (uint8)(fpgasettings.totalsamples >> 8);

  return pc_crc32(b, n);
}

//The chip into the current draw buffer (the UI's own top-bar drawing calls this through PC_TOPBAR_DRAWN)
static void pc_draw_chip(int state)
{
  uint16 fg = displaydata.fg_color;
  PFONTDATA font = displaydata.font;

  if(state == 0)
  {
    //Clear the slot; the RTC clock (when enabled) redraws its time there within 0.5 s
    display_set_fg_color(BLACK_COLOR);
    display_fill_rect(TIME_TEXT_XPOS, TIME_TEXT_YPOS, TIME_TEXT_WIDTH, PC_CHIP_H);
  }
  else
  {
    //Steady green chip, same width as the RUN/STOP chip under it
    display_set_fg_color(BLACK_COLOR);
    display_fill_rect(TIME_TEXT_XPOS, TIME_TEXT_YPOS, TIME_TEXT_WIDTH, PC_CHIP_H);
    display_set_fg_color(GREEN_COLOR);
    display_fill_rounded_rect(PC_CHIP_X, PC_CHIP_Y, PC_CHIP_W, PC_CHIP_H, 2);
    display_set_fg_color(BLACK_COLOR);
    display_set_font(&font_3);
    display_text(PC_CHIP_X + 4, PC_CHIP_Y, "MCP");
  }

  displaydata.fg_color = fg;
  displaydata.font     = font;
}

void pc_hal_indicator(int state)
{
  DISPLAYDATA save;

  pc_draw_begin(&save);
  pc_draw_chip(state);
  pc_draw_end(&save);
}

void pc_glue_topbar_drawn(void)
{
  if(pc_ready && pc_ui_chip_shown(&pc_sess))
    pc_draw_chip(1);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Version text: the same PC_FW_VERSION / PC_PROTO_* as #VER and GET_INFO, so they can never disagree

static char pc_ver[3][112];

static char *pc_vcat(char *p, const char *s)
{
  while(*s)
    *p++ = *s++;

  *p = 0;
  return p;
}

static char *pc_vdec2(char *p, uint32 v)
{
  *p++ = (char)('0' + (v / 10) % 10);
  *p++ = (char)('0' + v % 10);
  *p = 0;
  return p;
}

static void pc_version_init(void)
{
  static const char hex[] = "0123456789abcdef";
  char *p;
  int   i;

  p = pc_vcat(pc_ver[0], "v");
  pc_vcat(p, PC_FW_VERSION);

  p = pc_vcat(pc_ver[1], "fw ");
  pc_vcat(p, PC_FW_VERSION);

  p = pc_vcat(pc_ver[2], "FNIRSI-1013D-AI fw " PC_FW_VERSION ", proto ");
  *p++ = (char)('0' + PC_PROTO_MAJOR);
  *p++ = '.';
  *p++ = (char)('0' + PC_PROTO_MINOR);
  p = pc_vcat(p, ", base Atlan4 1.00o5, git ");

  for(i=28;i>=0;i-=4)
    *p++ = hex[(PC_FW_BUILD >> i) & 0xF];

  p = pc_vcat(p, ", built 20");
  p = pc_vdec2(p, COMPILE_YEAR);
  p = pc_vcat(p, "-");
  p = pc_vdec2(p, COMPILE_MONTH);
  p = pc_vcat(p, "-");
  p = pc_vdec2(p, COMPILE_DAY);
  p = pc_vcat(p, " ");
  p = pc_vdec2(p, COMPILE_HOUR);
  p = pc_vcat(p, ":");
  pc_vdec2(p, COMPILE_MIN);
}

char *pc_glue_version(int form)
{
  if(!pc_ver[0][0])
    pc_version_init();

  return pc_ver[(form >= 0 && form < 3) ? form : 0];
}

//Diagnostics screen: the full identity in small type along the top edge (above the 1.00o5 layout, which starts at y 10)
void pc_glue_diag_drawn(void)
{
  uint16 fg = displaydata.fg_color;
  PFONTDATA font = displaydata.font;

  display_set_fg_color(GREEN_COLOR);
  display_set_font(&font_2);
  display_text(20, 0, (int8 *)pc_glue_version(2));
  displaydata.fg_color = fg;
  displaydata.font     = font;
}

//----------------------------------------------------------------------------------------------------------------------------------
// SD card (MAIN context). The driver's timeouts use timer0 ticks, so IRQs stay on.

uint32_t pc_hal_sd_sectors(void)
{
  return cardsectors;
}

int pc_hal_sd_read(uint32_t sector, uint32_t count, uint8_t *dst)
{
  return (int)sd_card_read(sector, count, dst);
}

int pc_hal_sd_write(uint32_t sector, uint32_t count, const uint8_t *src)
{
  uint32 enabled = *EINT_PE_ENB_REG & PC_POWER_EINT_BIT;
  int32  r;

  *EINT_PE_ENB_REG &= ~PC_POWER_EINT_BIT;
  r = sd_card_write(sector, count, (uint8 *)src);

  //A power-key edge meanwhile stays pending and fires here; the save then runs after this write   BENCH: SD
  *EINT_PE_ENB_REG |= enabled;
  return (int)r;
}

uint8_t *pc_hal_sd_stage(uint32_t *capacity)
{
  *capacity = sizeof(pc_sd_stage_buf);
  return pc_sd_stage_buf;
}

//----------------------------------------------------------------------------------------------------------------------------------
// ENTER_FEL
//
// Atlan's bootloader enters FEL (boot mode 2) by jumping to 0xFFFF0020 in SVC mode, IRQs never enabled, SP in SRAM
// (0x7C00), USB untouched, caches on (fw0.04 fnirsi_1014d_startup.c). The scope differs and is put back into that
// shape: IRQs, the timer and the power-key interrupt are live, USB is configured as our CDC device, the vector base is
// switched to low vectors with our table copied to SRAM 0 (start.s), and the I-cache holds lines of this image, which
// must not survive into an image that sunxi-fel loads to the same addresses.
// DRAM, clocks, display and FPGA stay as they are, which is what FEL mode from the boot menu also leaves.

static void __attribute__((naked, noreturn)) pc_fel_jump(void)
{
  __asm__ __volatile__ (
    "mrs  r0, cpsr\n"
    "bic  r0, r0, #0x1F\n"
    "orr  r0, r0, #0xD3\n"            //SVC mode, IRQ and FIQ masked
    "msr  cpsr_c, r0\n"
    "mov  r0, #0\n"
    "mcr  p15, 0, r0, c7, c10, 4\n"   //drain the write buffer
    "mrc  p15, 0, r1, c1, c0, 0\n"
    "bic  r1, r1, #0x1000\n"          //I-cache off
    "bic  r1, r1, #0x0004\n"          //D-cache off (MMU is off: no dirty lines)
    "orr  r1, r1, #0x2000\n"          //high vectors at 0xFFFF0000, the reset state the boot ROM runs with
    "mcr  p15, 0, r1, c1, c0, 0\n"
    "mcr  p15, 0, r0, c7, c5, 0\n"    //invalidate the I-cache
    "mcr  p15, 0, r0, c7, c6, 0\n"    //invalidate the D-cache
    "ldr  sp, =0x00007C00\n"          //SVC stack where the bootloader has it when it enters FEL
    "ldr  pc, =0xFFFF0020\n"          //boot ROM FEL entry
    ".ltorg\n"
  );
}

void pc_hal_enter_fel(void)
{
  uint32 t0 = timer0ticks;
  uint32 cpsr;

  //1. The OK response must reach the host before USB goes away (at most 200 ms)
  while(!pc_transport_tx_drained() && ((timer0ticks - t0) < 200));
  timer0_delay(10);

  //2. Tell the person at the scope
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_fg_color(BLACK_COLOR);
  display_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  display_set_fg_color(WHITE_COLOR);
  display_set_font(&font_3);
  display_text(300, 220, "FEL mode (USB boot)");
  display_text(300, 250, "power cycle to leave");

  //3. Detach from the host so it sees our CDC device leave before the boot ROM enumerates as 1f3a:efe8
  usb_device_disable();
  timer0_delay(300);

  //4. Quiesce: no interrupt source may fire into our handlers once the boot ROM runs
  __asm__ __volatile__ ("mrs %0, cpsr" : "=r" (cpsr));
  __asm__ __volatile__ ("msr cpsr_c, %0" : : "r" (cpsr | 0xC0) : "memory");

  *TMR_IRQ_EN_REG  = 0;
  *TMR_IRQ_STA_REG = 0xFFFFFFFF;
  *TMR0_CTRL_REG   = 0;
  *EINT_PE_ENB_REG = 0;

  INTC_EN_REG0[0]   = 0;
  INTC_EN_REG0[1]   = 0;
  INTC_PEND_REG0[0] = 0;
  INTC_PEND_REG0[1] = 0;

  //5. USB controller and PHY back to their power-on state (reset asserted, clocks gated): the boot ROM initialises
  //   them itself, as on a cold FEL boot                                                    BENCH: ENTER_FEL [U]
  *CCU_BUS_SOFT_RST0 &= ~CCU_BSRR1_USBOTG_EN;
  *CCU_BUS_CLK_GATE0 &= ~CCU_BCGR1_USBOTG_EN;
  *CCU_USBPHY_CFG    &= ~(CCU_USBPHY_CFG_RST | CCU_USBPHY_CFG_GAT);

  //6. Caches, vectors, mode, stack, jump
  pc_fel_jump();
}

//----------------------------------------------------------------------------------------------------------------------------------
// Glue called from the 1.00o5 code (pc_hooks.h)

//----------------------------------------------------------------------------------------------------------------------------------
// Hidden menu options (owner policy: the UI offers only what the running hardware can do)

//DS3231 present? Address 0xD0 (write) on the touch panel's bit-banged bus, sample the slave ACK, stop. The 1.00o5 I2C
//routines clock a master ACK and never look at the slave's, so they cannot tell.                     BENCH: RTC probe
static uint8 pc_probe_ds3231(void)
{
  uint8 data = 0xD0, ack;
  int   i;

  tp_i2c_send_start();
  *PORT_A_CFG_REG = 0xFFFF1101;

  for(i=0;i<8;i++)
  {
    if(data & 0x80)
      *PORT_A_DATA_REG |= 0x00000004;
    else
      *PORT_A_DATA_REG &= 0x0000000B;

    tp_delay(TP_DATA_DELAY);
    *PORT_A_DATA_REG |= 0x00000008;
    tp_delay(TP_CLOCK_DELAY);
    *PORT_A_DATA_REG &= 0x00000007;
    tp_delay(TP_DATA_DELAY);
    data <<= 1;
  }

  //Release SDA, clock the ACK bit, sample it (low = a device answered)
  *PORT_A_CFG_REG = 0xFFFF1001;
  tp_delay(TP_DATA_DELAY);
  *PORT_A_DATA_REG |= 0x00000008;
  tp_delay(TP_CLOCK_DELAY);
  ack = (*PORT_A_DATA_REG & 0x00000004) ? 0 : 1;
  *PORT_A_DATA_REG &= 0x00000007;
  tp_delay(TP_DATA_DELAY);

  tp_i2c_send_stop();
  return ack;
}

int pc_glue_ui_has(int feature)
{
  if(!pc_ready)
    return 1;

  switch(feature)
  {
    case PC_FEAT_GEN_DDS:
      return (pc_sess.var.caps & PC_CAP_GENERATOR_DDS) != 0;

    case PC_FEAT_RTC:
      return pc_rtc_present;

    case PC_FEAT_GEN_WAVE:
    case PC_FEAT_GEN_ANALOG:
    case PC_FEAT_LONG_MEMORY:
    case PC_FEAT_EXT_TRIGGER:
    case PC_FEAT_HOLDOFF:
      return 0;

    default:
      if((feature >= PC_FEAT_MEM_BASE) && (feature < PC_FEAT_MEM_BASE + 8))
        return (uint32)(3000 * (feature - PC_FEAT_MEM_BASE + 1)) <= pc_sess.var.max_total;

      return 1;
  }
}

static void pc_mask_rect(uint32 x, uint32 y, uint32 w, uint32 h)
{
  display_fill_rect(x, y, w, h);
}

//All these menus have a DARKGREY_COLOR background (scope_open_*_menu). Draws into the current buffer: a menu being
//built off screen before it slides in, or the screen when an item is redrawn.
void pc_glue_ui_mask(int menu)
{
  uint16 fg = displaydata.fg_color;
  uint32 i;

  if(!pc_ready)
    return;

  display_set_fg_color(DARKGREY_COLOR);

  switch(menu)
  {
    //PC_MASK_GEN_MENU, PC_MASK_TRIG_MENU: since 0.2.8 those rows are not laid out at all (pc_hooks.h compact layout)

    case PC_MASK_TRIG_CHANNEL:
      if(!pc_glue_ui_has(PC_FEAT_EXT_TRIGGER))
        pc_mask_rect(TRIGGER_MENU_XPOS + 170, TRIGGER_MENU_YPOS + 127, 37, 26);
      break;

    case PC_MASK_ACQ_LONG:
      if(!pc_glue_ui_has(PC_FEAT_LONG_MEMORY))
        pc_mask_rect(ACQ_MENU_XPOS + 202, ACQ_MENU_YPOS + 161, 94, 44);
      break;

    case PC_MASK_ACQ_MEMORY:
      for(i=0;i<5;i++)
        if(!pc_glue_ui_has(PC_FEAT_MEM_BASE + i))
          pc_mask_rect(ACQ_MENU_XPOS + 313, ACQ_MENU_YPOS + 31 + 36 * i, 58, 29);
      break;

    case PC_MASK_SYS_RTC:
      //"Time & Date settings" item, background (159, 410, 226, 38)
      if(!pc_glue_ui_has(PC_FEAT_RTC))
        pc_mask_rect(155, 406, 234, 46);
      break;
  }

  displaydata.fg_color = fg;
}

//Settings saved by an earlier firmware (or another unit) may select what this hardware cannot do
static void pc_ui_fix_settings(void)
{
  uint32 max = pc_sess.var.max_total, total;

  pc_rtc_present = pc_probe_ds3231();

  //Without the module the clock, file stamps and FAT times would show garbage
  if(!pc_rtc_present)
    onoffRTC = 0;

  //No external trigger input
  if(scopesettings.triggerchannel > 1)
    scopesettings.triggerchannel = 0;

  //Memory depth: the largest offered step (3000 x n) the bitstream can capture
  if(fpgasettings.totalsamples > max)
  {
    total = (max / 3000) * 3000;

    if(total < 3000)
      total = 3000;

    fpgasettings.totalsamples = total;

    if(fpgasettings.settriggerpoint + 2 > total)
      fpgasettings.settriggerpoint = total / 2;
  }
}

void pc_glue_init(void)
{
#if PC_FORCE_CDC
  //The control port is the point of this build: CDC at boot regardless of the saved USB mode setting
  USB_CH340 = 1;
#endif

  //Version word from FPGA command 0x06, read once (main() read it already; this read has no side effect)
  pc_fpga_word = fpga_get_version();
  pc_session_init(&pc_sess);
  pc_ready = 1;
  pc_ui_fix_settings();
}

void pc_glue_poll_main(void)
{
  if(pc_ready)
    pc_poll(&pc_sess, PC_CTX_MAIN);
}

void pc_glue_poll_busy(void)
{
  if(pc_ready)
    pc_poll(&pc_sess, PC_CTX_BUSY);
}

void pc_glue_poll_isr(void)
{
  //Timer0 IRQ, every ms: PING/GET_INFO/GET_STATUS/GET_BATTERY/#VER/#RESET and the TX pump without waiting for the
  //UI loop. Skipped while a main-loop or BUSY poll is running (in_poll), so it never interleaves with one.
  if(pc_ready)
    pc_poll(&pc_sess, PC_CTX_ISR);
}

void pc_glue_ui_service(void)
{
  if(pc_ready)
    pc_ui_service(&pc_sess);
}

void pc_glue_pass_mark(int n)
{
  if(pc_ready)
    pc_ui_pass_mark(&pc_sess, n);
}

//The RTC clock shares the chip's slot: not drawn while the chip shows
int pc_glue_rtc_allowed(void)
{
  return !pc_ready || !pc_ui_chip_shown(&pc_sess);
}

int pc_glue_ui_arm_allowed(void)
{
  return !pc_ready || pc_ui_arm_allowed(&pc_sess);
}

void pc_glue_ui_capture_start(void)
{
  if(pc_ready)
    pc_ui_capture_start(&pc_sess);
}

void pc_glue_ui_capture_addr(uint32 addr)
{
  if(pc_ready)
    pc_ui_capture_addr(&pc_sess, (uint16_t)addr);
}

void pc_glue_ui_capture(void)
{
  if(pc_ready)
    pc_ui_capture_done(&pc_sess);
}

//----------------------------------------------------------------------------------------------------------------------------------
