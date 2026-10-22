//----------------------------------------------------------------------------------------------------------------------------------
// pc_hal_scope.c - pc_hal.h on the FNIRSI 1013D (1.00o5 settings, fpga_control.c, display) and the firmware glue that owns
// the one pc_session. The transport part of pc_hal.h is in pc_transport.c.
//
// Bench tests that must confirm an assumption are marked "BENCH: Tn" (protocol.md 12).
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>

#include "cap_hw.h"
#include "types.h"
#include "fnirsi_1013d_scope.h"
#include "fpga_control.h"
#include "fpga_bus.h"
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
#include "ff.h"

#include "pc_hal.h"
#include "pc_cmds.h"
#include "pc_hooks.h"
#include "peak_bars.h"
#include "spectrum.h"
#include "phosphor.h"
#include "spec_fmt.h"
#include "acal_live.h"
#include "trig_ui.h"
#include "bode_view.h"
#include "ui_menu.h"
#include "ui_settings.h"
#include "ui_fmt.h"
#include "ui_ctrl.h"
#include "pc_seq_hw.h"
#include "ui_diag_view.h"
#include "acq_view.h"
#include "bode_view.h"
#include "fpga_bus.h"
#include "fpga_counter.h"     //C7 counter
#include "ui_meas.h"
#include "roll.h"           //fw-roll

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
static uint32        pc_trig_tw5_last;                //ui-type: the word period fpga_trig's words were derived for (0: none yet)

extern DISPLAYDATA   displaydata;           //display_lib.c: current buffer, colours, font
extern uint32        cardsectors;           //sd_card_interface.c: card size from the CSD, 0 = no card
//pc_build_time: build_id.c via pc_cmds.h

//SD_STAGE / SD_WRITE_SECTORS staging buffer (protocol section 14 row 40)
static uint8         pc_sd_stage_buf[65536] __attribute__((aligned(4)));

//Power-key interrupt enable (PE11, power_interrupt_init): held off while the protocol writes SD sectors, so the
//power-off settings save cannot interleave with a write in progress (the SD driver is not reentrant)
#define PC_POWER_EINT_BIT 0x00000800

//----------------------------------------------------------------------------------------------------------------------------------
// Drawing from the protocol (MAIN context, between UI passes). The UI loop keeps its own drawing state in displaydata
//(buffer, colours, font); save it, draw on the visible screen, put it back.

//fw 0.2.59: while Diagnostics shows, the scope screen it keeps (ui_diag_view.h), so Back returns to the current state
static void pc_draw_begin(DISPLAYDATA *save)
{
  *save = displaydata;
  display_set_screen_buffer(ui_diag_scope_buffer());
}

static void pc_draw_end(const DISPLAYDATA *save)
{
  displaydata = *save;
}

static void pc_ext_generator_off(void);
static void pc_restart_clear(void);
void pc_diag_boot(const uint32_t *restart_v);    //pc_diag.c (fw 0.2.52)
void pc_diag_service(void);

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
  c->cal2_loaded     = adc_cal_loaded;
  c->cal2            = adc_cal[ch ? 1 : 0];
  c->trk             = acal_trk[ch ? 1 : 0];
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
    //fw-stoprate: a rate written wins over one tapped in STOP for the next RUN (acq_view.h)
    acq_view_rate_drop();
    fpga_write_cmd(0x0D);
    fpga_write_int(t->divider);
    fpga_divider = t->divider;
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
    //fw 0.2.64: the same rule as the ACQ menu: a time/div that does not fit the record at this rate is snapped (the
    //response's timeperdiv_index shows it) and the trigger point kept inside the record (acq_view.c)
    acq_view_fit(1);
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

    if(t->source == 2)
      pc_ext_generator_off();
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

  //fw 0.2.64: a new record length or trigger sample: the time/div and the trigger point follow it (acq_view.c)
  if(mask & (PC_TRG_PRE | PC_TRG_TOTAL))
    acq_view_fit(1);

  //Show it: trigger button, RUN/WAIT colour of the run chip
  pc_draw_begin(&save);
  scope_acqusition_settings(0);
  scope_trigger_settings(0);
  scope_run_stop_text();
  pc_draw_end(&save);
}

uint32_t pc_hal_auto_timeout_default(uint32_t total_samples)
{
  //fpga_set_time_base: the sample count (Atlan4 bitstreams count samples)
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

void pc_hal_gen_write(uint64_t p16, uint64_t h16, uint8_t enable)
{
  //FPGA 0x153D: the two-step square, then the pin driver; a running square changes at its next step boundary
  if(p16)
  {
    gp_square sq;

    (void)gp_split(p16, h16, &sq);
    fpga_gen_square(&sq);
  }

  fpga_gen_drive(enable);

  //UI copy, rounded to the UI's units (Hz, %), so the generator menu and the power-off save show the host's values
  {
    DISPLAYDATA save;

    fpgasettings.gen_enable = enable ? 1 : 0;

    if(p16)
    {
      fpgasettings.gen_freq = (uint32)((gp_mhz_from_p16(p16) + 500) / 1000);
      fpgasettings.gen_duty = (uint8)((gp_bp_from_h16(p16, h16) + 50) / 100);
    }

    pc_draw_begin(&save);
    scope_generator_settings(0);
    pc_draw_end(&save);
  }
}

void pc_hal_gen_status(uint8_t st[2])
{
  fpga_gen_status(st);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Acquisition

static void pc_dec_apply(const pc_decim_cfg *c);

static uint8  pc_ui_auto;                             //defined with the firmware auto mode below

int pc_hal_arm(uint16_t pre_words, uint16_t total_words, uint32_t auto_timeout, int normal,
               const pc_decim_cfg *dc, const pc_seq_cfg *sq)
{
  fpga_arm_cfg arm;

  //fw-roll: the host capture takes the FPGA from the roll's stream
  pc_glue_roll_release();

  //Trigger level from the marker, as every UI conversion does (scope_acquire_trace_data)
  fpga_set_trigger_level();

  //The host capture replaces any UI conversion in flight (and its firmware auto time-out)
  pc_ui_auto = 0;

  //Decimation filter mode, shift and 16-bit level, settled before the reset
  pc_dec_apply(dc);

  //Configured trigger mode again: FORCE leaves 0x1A in auto
  fpga_write_cmd(0x1A);
  fpga_write_byte(normal ? 0x01 : 0x00);

  //Sequencer run setting before the reset (pc_seq_hw.c); a host capture ends a UI averaging run
  pc_seq_ui_conversion(0, dc, 0);
  pc_seq_hw_pre_reset(sq);

  //Sampling reset (fpga_bus.h): geometry before the reset and inside it, bounded ready wait, 0x0E, release
  arm.pre_words    = pre_words;
  arm.total_words  = total_words;
  arm.write_0e     = 1;
  arm.auto_timeout = auto_timeout;
  arm.max_polls    = PC_READY_POLLS;
  arm.idle         = 0;
  return fpga_arm_reset(&arm);
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
  return fpga_prepare_for_transfer();
}

uint8_t pc_hal_ext_status(void)
{
  //Read only on 0x1533+ (pc_cmds checks EXT_TRIGGER): older bitstreams return the stale bus byte
  fpga_write_cmd(0x28);
  return fpga_read_byte();
}

//----------------------------------------------------------------------------------------------------------------------------------
// Trigger types and hold-off (fw-c5, FPGA 0x153C, protocol 1.26): the firmware's copy lives in fpga_bus.c (fpga_trig),
// which every arm (UI conversion or host ARM) re-writes inside the sampling reset window (fpga_arm_reset)

void pc_hal_trig_ext_read(pc_hw_trig_ext *t)
{
  t->type    = fpga_trig.type;
  t->flags   = fpga_trig.longer ? PC_TRG_F_LONGER : 0;
  t->n       = fpga_trig.n;
  t->h       = fpga_trig.h;
  t->holdoff = fpga_trig.holdoff;
}

void pc_hal_trig_ext_write(const pc_hw_trig_ext *t)
{
  fpga_trig_ext x;

  x.type    = t->type;
  x.longer  = (uint8_t)((t->flags & PC_TRG_F_LONGER) ? 1 : 0);
  x.n       = t->n;
  x.h       = t->h;
  x.holdoff = t->holdoff;
  fpga_trig_write(&x, 1);

  //ui-type: the shared screen: the menu shows the host's type, a width or idle time as the nearest ladder step of the
  //rate (the host's exact words stay armed until the word period changes or the menu edits the row)
  pc_trig_tw5_last = pc_glue_trig_tw5();
  tui_from_hw(&trig_ui, x.type, x.longer, x.n, x.h, pc_trig_tw5_last);
}

void pc_hal_trig_status(uint8_t out[4])
{
  fpga_trig_status(out);
}

//----------------------------------------------------------------------------------------------------------------------------------
//C7 counter (fw-c7, FPGA 0x153C): the state lives in fpga_counter.c; the main loop's pass (pc_glue_counter_pass) does
//every bus access, these two are memory only

void pc_hal_counter_read(pc_hw_counter *c)
{
  c->gate       = fpga_cnt.cfg_gate;
  c->source     = fpga_cnt.cfg_source;
  c->level      = fpga_cnt.level;
  c->hyst       = fpga_cnt.hyst;
  c->held       = fpga_cnt.held;
  c->current    = (uint8_t)fpga_counter_current(timer0ticks);
  c->has_period = fpga_cnt.has_period;
  c->seq        = fpga_cnt.seq;
  c->res_src    = fpga_cnt.res_src;
  c->res_gate   = fpga_cnt.res_gate;
  c->e          = fpga_cnt.e;
  c->t          = fpga_cnt.t;
  c->w          = fpga_cnt.w;
  c->min        = fpga_cnt.min;
  c->max        = fpga_cnt.max;
}

void pc_hal_counter_config(uint8_t gate, uint8_t source, uint8_t level, uint8_t hyst)
{
  fpga_counter_config(gate, source, level, hyst);
}

//Main loop, after the capture readout (fnirsi_1013d_scope.c PC_COUNTER_PASS): the 0x35 read, the rate-change rule, the
//trigger level followed, a pending host configuration; then the measurement cards' copy (ui_meas.c). Built-for
//bitstream only: on a mismatch the counter was never configured and nothing is read
void pc_glue_counter_pass(void)
{
  ui_ms_counter u;
  int32 lvl;

  if(pc_fpga_word != PC_FW_FPGA_VERSION)
    return;

  lvl = (int16)scopesettings.triggerlevel;                 //the UI stores the unclamped level
  lvl = (lvl < 0) ? 0 : ((lvl > 255) ? 255 : lvl);
  fpga_counter_pass(timer0ticks, (uint8)lvl);

  u.valid     = (uint8)(fpga_counter_current(timer0ticks) && fpga_cnt.has_period && (fpga_cnt.res_src != FPGA_CNT_RES_EXT));
  u.ch        = fpga_cnt.res_src;
  u.freq_mhz  = (fpga_cnt.freq_uhz + 500) / 1000;
  u.period_ps = fpga_cnt.period_ps;
  u.width_ps  = fpga_cnt.width_ps;
  u.low_ps    = fpga_cnt.low_ps;
  u.duty_ppm  = fpga_cnt.duty_ppm;
  ui_ms_counter_set(&u);
}

//EXT selected: the generator drives the same pin, switch it off (UI copy and FPGA) and show it
static void pc_ext_generator_off(void)
{
  DISPLAYDATA save;

  if(fpgasettings.gen_enable)
  {
    fpgasettings.gen_enable = 0;
    fpga_on_off_generator();
    pc_draw_begin(&save);
    scope_generator_settings(0);
    pc_draw_end(&save);
  }
}

void pc_glue_ext_selected(void)
{
  if(pc_ready)
  {
    fpga_set_trigger_channel();
    pc_ext_generator_off();
  }
}

void pc_hal_force(void)
{
  //0x0E = 0, then auto mode: the Atlan4 stop condition ~trigger_mode && time_base_timeout becomes true at once  BENCH: T7
  fpga_force_stop();
}

void pc_hal_read_core(uint8_t core, uint32_t start, uint32_t count, uint8_t *dst, uint32_t stride, uint32_t ring_words)
{
  //BENCH: T3 - the dummy strobe of the bus contract (fpga_bus.h), ring end restart
  fpga_read_lane((uint8_t)(0x20 + core), start % ring_words, count, dst, stride, ring_words);
}

void pc_hal_fpga_write_cmd(uint8_t cmd)   { fpga_write_cmd(cmd); }
void pc_hal_fpga_write_data(uint8_t data) { fpga_write_byte(data); }
uint8_t pc_hal_fpga_read_data(void)       { return fpga_read_byte(); }

//----------------------------------------------------------------------------------------------------------------------------------
// Processed trace

int32_t pc_hal_trace_build(uint32_t start_word, uint32_t total_samples, uint32_t pretrigger_samples, uint8_t ch_mask)
{
  //The UI readout (fpga_read_sample_data; since fw 0.2.72 with the dummy strobe, so exact like READ_RAW: P-7.4)
  //BENCH: T3, T4. Its interleave is B before A, so READ_TRACE follows the 1.00o5 convention.
  scopesettings.nofsamples  = total_samples / 2;
  scopesettings.samplecount = total_samples;

  //fw 0.2.83: ADoverload (sticky in fpga_read_adc_data) describes this readout only: the host capture's flags
  //(pc_capture_finish, READ_TRACE meta, GET_STATUS) and the Bode engine's readouts never carry an earlier one (bench
  //2026-10-20 10:52). The Scope view clears it in its own passes as before.
  if(ch_mask & 0x01)
  {
    scopesettings.channel1.ADoverload = 0;
    fpga_read_sample_data(&scopesettings.channel1, start_word);
  }

  if(ch_mask & 0x02)
  {
    scopesettings.channel2.ADoverload = 0;
    fpga_read_sample_data(&scopesettings.channel2, start_word);
  }

  //The spectrum view shows host captures too (never dropped as untriggered)
  spectrum_capture_info(1);
  spectrum_new_data();

  //The phosphor view adds a host capture once, when it completes (a READ_TRACE re-read of it is not a new record)
  if((pc_sess.acq_state == PC_ACQ_ARMED) || (pc_sess.acq_state == PC_ACQ_TRIGGERED))
  {
    phosphor_capture_info(1);
    phosphor_new_data();
  }

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
// Decimation filter (bitstream 0x1534+, pc_decim.h; firmware/README.md "Decimation filter")
//
// The FPGA keeps its mode until written, so the firmware writes it at every conversion start: the UI acquisition
// (scope_acquire_trace_data -> fpga_do_conversion) and host ARM use the Filter setting, every other conversion
// (calibration, auto setup) the legacy mode K = 0, since they need the per-core 8-bit data. A changed mode, shift or
// divider waits (K + 2) output periods before the sampling reset (free-running filter). The readout of a 16-bit capture
// reads both bytes per word into pc_dec_raw and converts them into the 8-bit trace domain
// (code = out * 2^S / N^K + (comp A + comp B) / 2); fpga_read_adc_data then runs its normal per-sample work on those
// values instead of bus reads, so the trace, the measurements and the software trigger work unchanged.

#define PC_DEC_RING       8192

//fw 0.2.64: the Filter per mode, [0] Scope, [1] Spectrum (owner): bits 1:0 requested K (UI Filter), PC_DECIM_SET_FIR;
//saved in the settings sector (words 207 and 212). pc_dec_setting is the current mode's, so the Filter box, the arm,
//SET_DECIM and GET_DECIM all act on the mode on screen.
static uint8         pc_dec_set_mode[2] = { PC_DECIM_DEFAULT_SCOPE, PC_DECIM_DEFAULT_SPEC };
#define pc_dec_setting (pc_dec_set_mode[spectrum_mode() != SPECTRUM_MODE_SCOPE])
//C6 peak detect (0x153C, docs/plans/fpga-0x153c.md section 4; the ACQ Sample row of docs/plans/ui-rows.md section 3): one
//Scope setting, not per mode (the Spectrum always acquires Normal), saved in word 211 bit 3 (ui-sample). SET_DECIM
//filter 4 and the ACQ menu's Sample box set it and keep the Filter's K / FIR for the way back; filter 0..3 clears it
static uint8         pc_dec_peak;
static uint8         pc_dec_raw_peak[2];              //ui-sample: pc_dec_raw holds that channel's peak pairs
static uint32        pc_dec_peak_gain[2];             //and the correction their midpoints took (pc_peak_gain / _off)
static int32         pc_dec_peak_off[2];
static uint8         pc_dec_ui_arm;                   //the next fpga_do_conversion is the UI acquisition's
static uint8         pc_dec_last_ui;                  //the last one was (the full-rate check's re-arm repeats it)
static pc_decim_cfg  pc_dec_cur;                      //what the FPGA runs (k = 0 until written)
static uint8         pc_dec_written;                  //pc_dec_cur was written at least once
static uint16        pc_dec_raw[2][PC_DEC_RING];      //16-bit samples of the last readout per channel
static uint32        pc_dec_raw_n[2];                 //their count, 0 = that readout was 8-bit
static int16         pc_dec_feed[PC_DEC_RING + 2];    //converted samples for fpga_read_adc_data
static int16        *pc_dec_even_src;
int16               *pc_decim_src;

//fw 0.2.87: the divider the FPGA holds (the last 0x0D anyone wrote, tracked by fpga_bus_note_write), the UI table's
//before the first write. Every decimation configuration is computed from it: the UI time/div can be snapped without a
//0x0D write (acq_view_fit) and the session can forget a host's raw divider, the FPGA does neither (section 57).
uint32_t pc_hal_divider_now(void)
{
  if(fpga_bus_wr.held & FPGA_HELD_DIVIDER)
    return fpga_bus_wr.divider;

  return (scopesettings.samplerate < (sizeof(sample_rate_settings) / sizeof(uint32))) ? sample_rate_settings[scopesettings.samplerate] : 0;
}

//The UI acquisition's setting: the mode on screen's Filter, plus Sample Peak in the Scope view (C6)
static uint8 pc_dec_ui_set(void)
{
  return ui_acq_decim_set(pc_dec_setting, pc_dec_peak, spectrum_mode() != SPECTRUM_MODE_SCOPE);
}

//ui-sample: the UI record is 16-bit by its setting (the mode's Filter, or Sample Peak in the Scope view): the memory
//depth limit of the 16-bit layout applies (pc_dec_fit_memory, the ACQ menu's chips), in roll too as for the Filter
static int pc_dec_ui_wide(void)
{
  return (pc_dec_ui_set() & (3 | PC_DECIM_SET_PEAK)) != 0;
}

//set: bits 1:0 K, PC_DECIM_SET_FIR, PC_DECIM_SET_PEAK (pc_dec_ui_set, or 0 for the legacy path)
static void pc_dec_config(pc_decim_cfg *c, uint8 set)
{
  pc_decim_config(set & 3, (uint8)(set & (PC_DECIM_SET_FIR | PC_DECIM_SET_PEAK)), pc_hal_divider_now(),
                  fpgasettings.totalsamples, pc_decim_has(), c);
}

//Write c to the FPGA and wait for the filter to settle: 0x53 / 0x56 where they differ from what the FPGA holds, 0x54 and
//the 16-bit level at every arm with K >= 1 (fw 0.2.87, fpga_decim_write: a shift written for another divider cannot
//survive an arm)
//The 16-bit trigger level (0x55) for the 8-bit level on channel chan (0 CH1, 1 CH2, 2 EXT) under the decimation c, as
//the readout converts
static uint16 pc_dec_level16(const pc_decim_cfg *c, uint8 chan, int32 level)
{
  level = (level < 0) ? 0 : ((level > 255) ? 255 : level);

  if(c->peak)
  {
    uint16 raw = (uint16)level;

    if(chan < 2)
    {
      acal_corr k;

      fpga_channel_correction(chan ? &scopesettings.channel2 : &scopesettings.channel1, &k);
      raw = pc_peak_level((uint8)level, pc_peak_gain(k.gain_a, k.gain_b), pc_peak_off(k.off_a, k.off_b));
    }

    return (uint16)(raw << 8);
  }

  if(chan < 2)
  {
    acal_corr k;

    fpga_channel_correction(chan ? &scopesettings.channel2 : &scopesettings.channel1, &k);
    return pc_decim_level16_corr((uint8)level, acal_scale(c->scale_q24, k.gain16), k.corr16);
  }

  return pc_decim_level16((uint8)level, c->gain, c->shift);
}

static void pc_dec_apply(const pc_decim_cfg *c)
{
  int settle;

  settle = fpga_decim_write(pc_decim_mode(c), pc_decim_cmd56(c), c->shift);

  if(pc_decim_wide(c))
  {

    //0x17 is ignored while K >= 1: the UI level (8-bit code in the trace domain, as fpga_set_trigger_level computed it)
    //mapped back through the readout's 16-bit transform (fw 0.2.47): the trigger channel's gain16 folded into the
    //scale and its corr16 zero term, so the level sits where the corrected trace shows it (it ignored both before,
    //about 1 % level error at full rate). EXT has no samples: the plain conversion.
    //C6 peak detect: the FPGA compares the window max (rising) or min (falling) as the high byte, so 0x55 = raw << 8 with
    //the raw code whose mean-core correction (pc_glue_decim_read's) gives the UI level
    fpga_set_decim_level(pc_dec_level16(c, scopesettings.triggerchannel, (int16)scopesettings.triggerlevel));

    //(K + 2) output periods, (K + 10) N x 5 ns + 1 us with the FIR; timer0 ticks are 1 ms, + 1 for the partial tick
    if(settle)
      timer0_delay((pc_decim_settle_us(c) + 999) / 1000 + 1);
  }

  pc_dec_cur     = *c;
  pc_dec_written = 1;
}

void pc_glue_ui_decim_arm(void)
{
  pc_dec_ui_arm = 1;
}

void pc_glue_decim_rearm(void)
{
  pc_dec_ui_arm = pc_dec_last_ui;
}

//----------------------------------------------------------------------------------------------------------------------------------
// The trigger menu's Type row (ui-type, docs/plans/ui-rows.md section 2; trig_ui.h). The menu keeps its Width and Idle
// time as ladder steps (time); fpga_trig holds the words armed. The words are derived whenever the trigger stream's
// word period changes (a rate, Filter or source change: at the UI arm that follows, or when the menu opens), the step
// snapped into the rate's range with the notice; a host SET_TRIGGER2's exact words stay armed until then and show as
// the nearest step (pc_hal_trig_ext_write). A UI edit writes the registers at once through fpga_trig_write, the path
// SET_TRIGGER2 takes, so GET_TRIGGER reflects the menu and the fingerprint (settings_seq) sees both sides' changes.

uint32 pc_glue_trig_tw5(void)
{
  pc_decim_cfg c;

  pc_dec_config(&c, pc_dec_ui_set());                 //ui-sample: Sample Peak runs the 16-bit word period too
  return tui_tw5(pc_hal_divider_now(), c.k != 0, scopesettings.triggerchannel == 2);
}

//The words for word period tw5 into fpga_trig when it changed since the last derivation: the chosen type's time
//parameter snaps into the range (notice: "Width set to 5us for this rate" / "Idle set to ..." queued after the
//time/div's). Edge has no words; the host's timeout keeps its own N
static void pc_trig_rate(uint32 tw5, int notice)
{
  int    snap;
  uint32 n, h;

  if(tw5 == pc_trig_tw5_last)
    return;

  snap             = tui_rate(&trig_ui, tw5);
  pc_trig_tw5_last = tw5;

  if((trig_ui.type != TUI_PULSE) && (trig_ui.type != TUI_NTH))
    return;

  tui_hw(&trig_ui, tw5, &fpga_trig.type, &fpga_trig.longer, &n, &h);
  fpga_trig.n = n;
  fpga_trig.h = h;

  if(!notice)
    return;

  if(snap == TUI_SNAP_WIDTH)
    acq_view_notice_queue(1, "Width set to ", tui_step_text(trig_ui.width_i), " for this rate");
  else if(snap == TUI_SNAP_IDLE)
    acq_view_notice_queue(1, "Idle set to ", tui_idle_text(trig_ui.idle_i), " for this rate");
}

void pc_glue_trig_rate_check(void)
{
  //Roll mode does not use the trigger: the rows keep their values until the next armed rate (ui-rows.md 2.4)
  if(scopesettings.long_mode)
    return;

  pc_trig_rate(pc_glue_trig_tw5(), 1);
}

//The registers for the state at the current word period: 0x5E / 0x5F / 0x60 at once, 0x1B kept (the host's hold-off is
//not the menu's to clear) and 0x66 = 1 (a new definition is a new session, as SET_TRIGGER2)
static void pc_trig_write(void)
{
  fpga_trig_ext x = fpga_trig;
  uint32        tw5 = pc_glue_trig_tw5(), n, h;

  (void)tui_rate(&trig_ui, tw5);
  pc_trig_tw5_last = tw5;
  tui_hw(&trig_ui, tw5, &x.type, &x.longer, &n, &h);
  x.n = n;
  x.h = h;
  fpga_trig_write(&x, 1);
}

void pc_glue_trig_ui_apply(void)
{
  pc_trig_write();

  //The next UI pass arms a fresh conversion with it (a Normal-mode wait armed with the old type would hold the screen)
  if(scopesettings.runstate)
    scopesettings.display_data_done = 1;
}

int pc_glue_trig_params(void)
{
  return ((trig_ui.type == TUI_PULSE) || (trig_ui.type == TUI_NTH)) && !bode_view_active();
}

//Power-on and Restore default settings: the state as restored or reset, the Width / Idle snapped into the rate's range
//silently (nothing was armed yet), the registers written on the built-for bitstream only
static void pc_trig_restore_apply(void)
{
  uint32 tw5 = pc_glue_trig_tw5();

  (void)tui_rate(&trig_ui, tw5);
  pc_trig_tw5_last = tw5;

  if(pc_fpga_word == PC_FW_FPGA_VERSION)
    pc_trig_write();
}

//----------------------------------------------------------------------------------------------------------------------------------
// Firmware auto mode (fw 0.2.41). The FPGA's auto time-out (0x0E) counts samples (the record length), so at fast rates
// it expires in microseconds (3000 samples at 10 MS/s = 0.3 ms), before a slow signal's next edge; and 0x18 keeps
// watching after sampling stopped, so such a record read "triggered" when an edge came between the stop and the read
// (bench: flat records flagged triggered). The UI's auto mode arms in normal mode and the main loop gives up after
// max(100 ms, two record lengths): 0x1A = 0 then ends the sampling at once (the time-out is long past) with an
// untriggered record, which is reported as such; 0x153B latches the newest total words at that stop, so the record is
// contiguous. Calibration and auto setup keep 1.00o5's FPGA auto.

static uint8  pc_ui_auto;                             //a UI conversion runs in firmware auto mode
static uint8  pc_ui_forced;                           //it was ended by the firmware time-out (untriggered)
static uint32 pc_ui_auto_t0, pc_ui_auto_ms;

void pc_glue_decim_conversion(void)
{
  pc_decim_cfg c;
  int          ui = pc_dec_ui_arm, seq_run;

  pc_dec_config(&c, pc_dec_ui_arm ? pc_dec_ui_set() : 0);
  pc_dec_last_ui = pc_dec_ui_arm;
  pc_dec_ui_arm  = 0;
  pc_dec_apply(&c);

  //Average: this UI conversion may be a hardware averaging run, which has its own deadline (pc_seq_ui_service; the
  //firmware time-out must never end a record between the records of a run). No bus access.
  seq_run = pc_seq_ui_conversion(ui, &c, pc_hal_divider_now());

  //Trigger mode for this conversion, written before the sampling reset
  pc_ui_auto   = 0;
  pc_ui_forced = 0;

  if(!scopesettings.long_mode)
  {
    int fw_auto = ui && (scopesettings.triggermode == 0);

    //ui-type: the Width / Idle time in words of this rate's trigger stream (fpga_trig, written inside the arm window)
    if(ui)
      pc_trig_rate(tui_tw5(pc_hal_divider_now(), c.k != 0, scopesettings.triggerchannel == 2), 1);

    fpga_write_cmd(0x1A);
    fpga_write_byte((scopesettings.triggermode || fw_auto) ? 0x01 : 0x00);

    if(fw_auto && !seq_run)
    {
      pc_ui_auto    = 1;
      pc_ui_auto_t0 = timer0ticks;
      pc_ui_auto_ms = pc_auto_deadline_ms(scopesettings.samplecount, pc_hal_divider_now());
    }
  }
}

//Main loop: give up on a trigger that did not come in time
static void pc_ui_auto_service(void)
{
  int done, trig;

  if(!pc_ui_auto || pc_ui_forced || !scopesettings.runstate || ((timer0ticks - pc_ui_auto_t0) < pc_ui_auto_ms))
    return;

  pc_hal_acq_poll(&done, &trig);

  if(!done && !trig)
  {
    pc_hal_force();
    pc_ui_forced = 1;
  }
  else
    pc_ui_auto = 0;                                   //it triggered: the record completes by itself
}

uint32 pc_glue_decim_words(uint32 samples)
{
  return pc_dec_cur.k ? samples : samples / 2;
}

//EXT trigger: trigger_address is the last word written before the edge, but the data lag it. 16-bit layout: 120 ns +
//K (N - 1) 2.5 ns (fpga-gen3; with the FIR 250 ns + 17.5 N ns + 1.25 K (N - 2) ns, fpga-gen4); legacy layout (fw 0.2.72):
//the ADC pipeline's 4 words (2 at divider 0). Writing that many samples fewer as pretrigger puts the edge back at sample
//settriggerpoint of the capture (GET_CAPTURE_INFO byte 70 reports the shift; pc_decim_ext_shift_at).
static uint32 pc_dec_ext_shift(void)
{
  return (scopesettings.triggerchannel == 2) ? pc_decim_ext_shift_at(&pc_dec_cur, pc_hal_divider_now()) : 0;
}

uint32 pc_glue_decim_pre_words(uint32 samples)
{
  uint32 shift = pc_dec_ext_shift();

  if(shift > samples)
    shift = pc_dec_cur.k ? samples : (samples & ~1u);

  return pc_dec_cur.k ? samples - shift : (samples - shift) / 2;
}

uint32 pc_glue_ui_read_start(uint32 addr)
{
  //The pretrigger words the conversion wrote to 0x0B; 16-bit layout: one sample per word, in the same 8192-word ring
  return fpga_ring_start(addr, pc_glue_decim_pre_words(fpgasettings.settriggerpoint), PC_DEC_RING);
}

void pc_glue_decim_read(void *settings, uint32 start)
{
  PCHANNELSETTINGS s = (PCHANNELSETTINGS)settings;
  uint32 ch, n, i, scale;

  pc_decim_src    = 0;
  pc_dec_even_src = 0;

  if(s == &scopesettings.channel1)
    ch = 0;
  else if(s == &scopesettings.channel2)
    ch = 1;
  else
    return;

  pc_dec_raw_n[ch]    = 0;
  pc_dec_raw_peak[ch] = 0;

  //UI averaging: the corrected codes of the mean (the hardware run's lane sums or the MCU's sums), both passes from
  //memory as for the 16-bit path
  if(pc_seq_ui_feed(settings, pc_dec_feed, scopesettings.nofsamples * 2))
  {
    pc_decim_src    = &pc_dec_feed[1];
    pc_dec_even_src = &pc_dec_feed[0];
    return;
  }

  if(!pc_dec_cur.k)
    return;

  //fpga_read_adc_data takes 2 x nofsamples values
  n = scopesettings.nofsamples * 2;

  if(n > PC_DEC_RING)
    n = PC_DEC_RING;

  fpga_read_decim_data(s->adc1command, s->adc2command, start % PC_DEC_RING, n, pc_dec_raw[ch]);

  if(pc_dec_cur.peak)
  {
    //C6 peak record: each word = { max (high byte), min (low byte) } raw codes from either core. Both go through the
    //mean of the two cores' 8-bit corrections (fpga_channel_correction), and the trace takes the corrected midpoint
    //(the host's READ_TRACE too; the pairs stay in pc_dec_raw for READ_TRACE WIDE and the Scope view's bars). Overload
    //rule: either byte at a rail (pc_peak_overload; fpga_read_adc_data's own check sees the midpoint only)
    acal_corr k;
    uint32    gain;
    int32     off;
    uint8     ov = 0;

    fpga_channel_correction(s, &k);
    gain = pc_peak_gain(k.gain_a, k.gain_b);
    off  = pc_peak_off(k.off_a, k.off_b);

    for(i=0;i<n;i++)
    {
      uint8 mn = (uint8)pc_dec_raw[ch][i], mx = (uint8)(pc_dec_raw[ch][i] >> 8);

      pc_dec_feed[i] = (int16)pc_peak_mid(mn, mx, gain, off);
      ov |= pc_peak_overload(mn, mx);
    }

    s->ADoverload |= ov;
    pc_dec_raw_n[ch]     = n;
    pc_dec_raw_peak[ch]  = 1;
    pc_dec_peak_gain[ch] = gain;
    pc_dec_peak_off[ch]  = off;
    pc_decim_src      = &pc_dec_feed[1];
    pc_dec_even_src   = &pc_dec_feed[0];
    return;
  }

  //Into the 8-bit trace domain with the readout's correction (adc_cal.h): gain16 folded into the scale, corr16
  //added (each output sample weighs both cores equally for even N). fpga_read_adc_data then passes the codes through
  //unchanged (unity gain, no offset for a 16-bit readout)
  scale = acal_scale(pc_dec_cur.scale_q24, s->gain16);

  for(i=0;i<n;i++)
    pc_dec_feed[i] = (int16)pc_decim_to_code(pc_dec_raw[ch][i], scale, s->corr16);

  pc_dec_raw_n[ch] = n;
  pc_decim_src     = &pc_dec_feed[1];
  pc_dec_even_src  = &pc_dec_feed[0];
}

void pc_glue_decim_even(void)
{
  pc_decim_src = pc_dec_even_src;
}

uint16 pc_glue_spectrum_setting_word(int i)
{
  return spectrum_settings_word(i);
}

uint16 pc_glue_phosphor_setting_word(void)
{
  return phosphor_settings_word();
}

uint16 pc_glue_decim_setting_word(void)
{
  //0xDE00 marks a valid word; sectors written before 0.2.30 hold whatever 1.00o5 left there (the defaults). Bits 3-4
  //held the falling-edge capture settings up to fw 0.2.102 (written 0 now, read back ignored); bit 5: FIR (fw 0.2.44)
  return (uint16)(0xDE00 | (pc_dec_set_mode[0] & (PC_DECIM_SET_FIR | 3)));
}

//Word 212 (fw 0.2.64): the Spectrum's Filter, 0xD500 marker (word 207 holds the Scope's)
uint16 pc_glue_decim_spec_setting_word(void)
{
  return (uint16)(0xD500 | (pc_dec_set_mode[1] & (PC_DECIM_SET_FIR | 3)));
}

//The mode on screen changed (pc_glue_ui_service): its Filter may need the 16-bit layout's shorter record, and the
//time/div then follows the record (acq_view.c)
static uint8 pc_dec_mode_seen;
static void  pc_dec_fit_memory(void);

static void pc_dec_follow_mode(void)
{
  uint8 m = (uint8)(spectrum_mode() != SPECTRUM_MODE_SCOPE);

  if(m == pc_dec_mode_seen)
    return;

  pc_dec_mode_seen = m;

  if(pc_dec_ui_wide())
  {
    pc_dec_fit_memory();
    acq_view_fit(0);
  }

  scopesettings.display_data_done = 1;
}

//ui-sample (docs/plans/ui-rows.md section 3): the ACQ menu's row as data (ui_ctrl.h ui_acq_row_build)
static void pc_acq_row(ui_acq_row *r)
{
  ui_acq_in in;

  in.roll         = scopesettings.long_mode != 0;
  in.average      = scopesettings.average_mode != 0;
  in.filter_on    = (pc_dec_setting & 3) != 0;
  in.peak         = pc_dec_peak;
  in.spectrum     = spectrum_mode() != SPECTRUM_MODE_SCOPE;
  in.filter_value = ui_filter_name(pc_dec_setting);
  ui_acq_row_build(&in, r);
}

//A box of the row in the menu's construction (Roll mode's): the label line and the value line centred on their measured
//widths; a NONE box leaves its slot as the menu's background
static void pc_acq_box(const ui_acq_row *r, int i)
{
  uint32 x = ACQ_MENU_XPOS + UI_ACQ_BOX_X(i), xc = x + UI_ACQ_BOX_W / 2;

  if(r->state[i] == UI_ACQ_BOX_NONE)
  {
    display_set_fg_color(DARKGREY_COLOR);
    display_fill_rect(x, ACQ_MENU_YPOS + UI_ACQ_BOX_Y, UI_ACQ_BOX_W, UI_ACQ_BOX_H);
    return;
  }

  display_set_fg_color((r->state[i] == UI_ACQ_BOX_ON) ? GREEN_COLOR : LIGHTGREY_COLOR);
  display_fill_rounded_rect(x, ACQ_MENU_YPOS + UI_ACQ_BOX_Y, UI_ACQ_BOX_W, UI_ACQ_BOX_H, 3);
  display_set_font(&font_2);
  display_set_fg_color((r->state[i] == UI_ACQ_BOX_ON) ? BLACK_COLOR : (r->state[i] == UI_ACQ_BOX_GREY) ? LIGHTGREY1_COLOR : WHITE_COLOR);
  display_text(xc - ui_text_width(&font_2, r->line1[i]) / 2, ACQ_MENU_YPOS + UI_ACQ_BOX_LINE1_Y, (int8 *)r->line1[i]);
  display_text(xc - ui_text_width(&font_2, r->line2[i]) / 2, ACQ_MENU_YPOS + UI_ACQ_BOX_LINE2_Y, (int8 *)r->line2[i]);
}

//The Average, Filter and Sample boxes and the Memory column's note (Roll mode is pc_glue_roll_label's): on opening, after
//every tap in the row and after the roll toggle (scope_acquisition_Average_mode_select). Into the current buffer.
void pc_glue_acq_row_draw(void)
{
  uint16     fg   = displaydata.fg_color;
  PFONTDATA  font = displaydata.font;
  ui_acq_row r;
  int        i;

  pc_acq_row(&r);

  for(i = UI_ACQ_AVERAGE; i < UI_ACQ_BOXES; i++)
    pc_acq_box(&r, i);

  display_set_fg_color(DARKGREY_COLOR);
  display_fill_rect(ACQ_MENU_XPOS + UI_ACQ_NOTE_X, ACQ_MENU_YPOS + UI_ACQ_NOTE_Y, UI_ACQ_NOTE_W, UI_ACQ_NOTE_LINES * UI_ACQ_NOTE_DY);
  display_set_font(&font_2);
  display_set_fg_color(0x00A0A0A0);                   //the pass time label's grey: a note, not a value

  for(i = 0; i < UI_ACQ_NOTE_LINES; i++)
    if(r.note[i])
      display_text(ACQ_MENU_XPOS + UI_ACQ_NOTE_X, ACQ_MENU_YPOS + UI_ACQ_NOTE_Y + i * UI_ACQ_NOTE_DY, (int8 *)r.note[i]);

  displaydata.fg_color = fg;
  displaydata.font     = font;
}

//A tap in the row is taken by a box that is not grey (and drawn)
int pc_glue_acq_box_live(int box)
{
  ui_acq_row r;

  pc_acq_row(&r);
  return (box >= 0) && (box < UI_ACQ_BOXES) && ((r.state[box] == UI_ACQ_BOX_ON) || (r.state[box] == UI_ACQ_BOX_OFF));
}

//Sample Peak applied to the Scope's acquisition (the CTRL page greys Trace style with it: the record is bars)
int pc_glue_sample_peak(void)
{
  ui_acq_in in = { 0 };

  in.roll     = scopesettings.long_mode != 0;
  in.peak     = pc_dec_peak;
  in.spectrum = spectrum_mode() != SPECTRUM_MODE_SCOPE;
  return ui_acq_peak_applied(&in);
}

//The Sample box: Normal <-> Peak. Peak runs the 16-bit path (the depth limit as with the Filter); Normal brings the
//Filter back as it was (the setting byte kept it) and the depths it allows. The next UI pass arms with it.
void pc_glue_sample_tap(void)
{
  pc_dec_peak ^= 1;

  if(pc_dec_ui_wide())
    pc_dec_fit_memory();

  scope_acquisition_memory_deep_select();
  pc_glue_acq_row_draw();
  scopesettings.display_data_done = 1;
}

//ui-sample: the record on screen is a peak-detect record for channel ch: its pairs and the correction their midpoints
//took (the Scope view's bars and the phosphor map, peak_bars.h); 0 in roll mode and in the waveform viewer
int pc_glue_peak_record(int ch, const uint16 **pairs, uint32 *n, uint32 *gain, int32 *off)
{
  ch &= 1;

  if(!pc_dec_raw_peak[ch] || !pc_dec_raw_n[ch] || scopesettings.long_mode || scopesettings.waveviewmode)
    return 0;

  *pairs = pc_dec_raw[ch];
  *n     = pc_dec_raw_n[ch];
  *gain  = pc_dec_peak_gain[ch];
  *off   = pc_dec_peak_off[ch];
  return 1;
}

//fw-small: a peak record's Vmax / Vmin / Vpp (settings max / min / peakpeak, the cards' sources) from the lowest min byte and
//the highest max byte, as the bars draw them (docs/plans/ui-rows.md 3.3): the midpoint trace shows a narrow spike at about
//half its height. Called after the readout; center, average, rms and the timing values stay the midpoint's
void pc_glue_peak_extremes(void *settings)
{
  PCHANNELSETTINGS s = (PCHANNELSETTINGS)settings;
  uint32  ch;
  int32_t lo, hi;

  if(s == &scopesettings.channel1)
    ch = 0;
  else if(s == &scopesettings.channel2)
    ch = 1;
  else
    return;

  if(!pc_dec_raw_peak[ch] || !pc_dec_raw_n[ch])
    return;

  pb_extremes(pc_dec_raw[ch], pc_dec_raw_n[ch], pc_dec_peak_gain[ch], pc_dec_peak_off[ch], s->invert, &lo, &hi);

  //The same dc shift the readout adds to every sample before its min / max (fpga_read_adc_data)
  s->min      = lo + s->dcshift;
  s->max      = hi + s->dcshift;
  s->peakpeak = s->max - s->min;
}

//Memory depth for the 16-bit layout: the largest offered step that fits, trigger point kept at the same fraction
static void pc_dec_fit_memory(void)
{
  uint32 old = fpgasettings.totalsamples, total = (PC_DECIM_MAX_TOTAL / 3000) * 3000, pre = fpgasettings.settriggerpoint;

  if(old <= PC_DECIM_MAX_TOTAL)
    return;

  if(pre == old / 4)
    pre = total / 4;
  else if(pre == old - 1000)
    pre = total - 1000;
  else
    pre = total / 2;

  fpgasettings.totalsamples     = total;
  fpgasettings.settriggerpoint  = pre;
  scopesettings.samplecount     = total;
  scopesettings.nofsamples      = total / 2;
}

//The DEFAULT button and a settings sector that failed to load (scope_reset_config_data)
void pc_glue_settings_defaults(void)
{
  pc_dec_set_mode[0] = PC_DECIM_DEFAULT_SCOPE;
  pc_dec_set_mode[1] = PC_DECIM_DEFAULT_SPEC;
  pc_dec_peak        = 0;                                   //C6: Sample Normal (word 211 bit 3 clear when saved)

#if PC_FORCE_CDC
  //The defaults say mass storage (USB_CH340 0); this build's USB stays CDC, also after Restore default settings (the
  //USB drive screen's exit re-initialises USB from this flag)
  USB_CH340 = 1;
#endif

  //fw 0.2.56: the Display variants back to Normal and Spectrum (X-Y and Grid are 1.00o5's own defaults)
  phosphor_set_switch(0);
  spectrum_set_view(0);

  //fw 0.2.69: the Frequency response settings (words 219, 220, 213..215) back to their defaults
  bode_view_defaults();

  //ui-type: the trigger Type row back to Edge (words 202 / 203); armed when the FPGA is up (the Settings screen's action)
  tui_defaults(&trig_ui);
  if(pc_ready)
    pc_trig_restore_apply();

  if(pc_dec_setting & 3)
    pc_dec_fit_memory();
}

//A tap steps Off, Hi-Res, Anti-alias; a mode the host set (CIC2, CIC3, boxcar+FIR, CIC2+FIR) goes to Off (fw-acqui,
//ui_ctrl.h ui_filter_next)
void pc_glue_decim_tap(void)
{
  pc_dec_setting = ui_filter_next(pc_dec_setting);

  if(pc_dec_setting & 3)
    pc_dec_fit_memory();

  //fw 0.2.84: the Memory column again on every change (acq_view_mem_chips): Off brings back the depths a filter hid
  //(owner, 0.2.76: they stayed masked until something else redrew the menu)
  scope_acquisition_memory_deep_select();

  pc_glue_acq_row_draw();
  scopesettings.display_data_done = 1;
}

uint8_t pc_hal_decim_setting(void)
{
  return pc_dec_ui_set();
}

void pc_hal_decim_set(uint8_t setting)
{
  pc_dec_setting = setting & (PC_DECIM_SET_FIR | 3);
  pc_dec_peak    = (setting & PC_DECIM_SET_PEAK) ? 1 : 0;                  //C6: the Sample row's state

  if((pc_dec_setting & 3) || pc_dec_peak)
  {
    pc_dec_fit_memory();
    acq_view_fit(1);
  }

  //The next UI pass arms a fresh conversion with it
  scopesettings.display_data_done = 1;
}

void pc_hal_decim_current(pc_decim_cfg *c, int *written)
{
  *c       = pc_dec_cur;
  *written = pc_dec_written;
}

uint16_t pc_hal_decim_readback(void)
{
  return fpga_get_decim_state();
}

const uint16_t *pc_hal_trace16(int ch, uint32_t *count)
{
  *count = pc_dec_raw_n[ch & 1];
  return *count ? pc_dec_raw[ch & 1] : 0;
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

  //fw 0.2.63 (owner): the right bar's RUN/STOP shows the state too; it stayed green when the host stopped
  if((scopesettings.rightmenustate == 0) && !scopesettings.waveviewmode)
    scope_run_stop_button(0);

  pc_draw_end(&save);
}

//Capture-relevant settings (protocol section 14, settings_seq). The trigger level counts unless the UI's "always 50%"
//rewrites it after every capture; the flag counts instead.
uint32_t pc_hal_settings_fingerprint(void)
{
  uint8  b[64];
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
  b[n++] = pc_hal_decim_setting();
  //1.26: the trigger extension (a type, N, H or hold-off change is a capture-relevant setting)
  b[n++] = fpga_trig.type;
  b[n++] = fpga_trig.longer;
  b[n++] = (uint8)fpga_trig.n;       b[n++] = (uint8)(fpga_trig.n >> 8);       b[n++] = (uint8)(fpga_trig.n >> 16);
  b[n++] = (uint8)fpga_trig.h;       b[n++] = (uint8)(fpga_trig.h >> 8);       b[n++] = (uint8)(fpga_trig.h >> 16);
  b[n++] = (uint8)fpga_trig.holdoff; b[n++] = (uint8)(fpga_trig.holdoff >> 8); b[n++] = (uint8)(fpga_trig.holdoff >> 16);
  b[n++] = (uint8)(fpga_trig.holdoff >> 24);

  return pc_crc32(b, n);
}

//The chip as final pixels, rendered once (into a top-bar corner of displaybuffertmp, which the UI only uses below
//y 46), then copied as one blit: every pixel is written once with its final value, so redrawing it over itself can
//not flicker (0.2.3-0.2.7 cleared the slot and drew the chip every 100 ms, directly on the screen).
static uint16 pc_chip_px[PC_CHIP_H][PC_CHIP_W];
static uint8  pc_chip_ready;

static void pc_draw_chip_into(int state);

static void pc_chip_render(void)
{
  DISPLAYDATA save;
  uint32 y, x;

  save = displaydata;
  display_set_screen_buffer(displaybuffertmp);
  pc_draw_chip_into(1);

  for(y=0;y<PC_CHIP_H;y++)
    for(x=0;x<PC_CHIP_W;x++)
      pc_chip_px[y][x] = displaybuffertmp[(PC_CHIP_Y + y) * SCREEN_WIDTH + PC_CHIP_X + x];

  displaydata = save;
  pc_chip_ready = 1;
}

static void pc_chip_blit(uint16 *dst)
{
  uint32 y, x;

  if(!pc_chip_ready)
    pc_chip_render();

  for(y=0;y<PC_CHIP_H;y++)
    for(x=0;x<PC_CHIP_W;x++)
      dst[(PC_CHIP_Y + y) * SCREEN_WIDTH + PC_CHIP_X + x] = pc_chip_px[y][x];
}

//State 1: the blit, into the current draw buffer. State 0: clear the slot (once, on the transition)
static void pc_draw_chip(int state)
{
  if(state)
  {
    pc_chip_blit(displaydata.screenbuffer);
    return;
  }

  pc_draw_chip_into(0);
}

//Drawing primitives behind the blit (used once for the render, and for the clear)
static void pc_draw_chip_into(int state)
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
  static uint8 shown;
  DISPLAYDATA save;

  pc_draw_begin(&save);

  //Clear the whole clock slot only on a transition; while on, every call is the same blit
  if(!state || !shown)
    pc_draw_chip(0);

  if(state)
    pc_draw_chip(1);

  shown = state ? 1 : 0;
  pc_draw_end(&save);
}

void pc_glue_topbar_drawn(void)
{
  if(pc_ready && pc_ui_chip_shown(&pc_sess))
    pc_draw_chip(1);
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
#if PC_MMU
    "bl   pc_mmu_off\n"               //fw 0.2.48: clean + invalidate the D-cache, MMU and caches off (start.s)
#endif
    "mov  r0, #0\n"
    "mcr  p15, 0, r0, c7, c10, 4\n"   //drain the write buffer
    "mrc  p15, 0, r1, c1, c0, 0\n"
    "bic  r1, r1, #0x1000\n"          //I-cache off
    "bic  r1, r1, #0x0004\n"          //D-cache off (MMU is off by now: no dirty lines)
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
  pc_restart_clear();

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

int pc_glue_fpga_mismatch(void)
{
  return pc_ready && pc_sess.var.mismatch;
}

int pc_glue_ui_has(int feature)
{
  if(!pc_ready)
    return 1;

  switch(feature)
  {
    case PC_FEAT_RTC:
      return pc_rtc_present;

    default:
      //With the Filter on or Sample Peak, the 16-bit layout holds one sample per word: at most PC_DECIM_MAX_TOTAL
      if((feature >= PC_FEAT_MEM_BASE) && (feature < PC_FEAT_MEM_BASE + ACQ_MEM_CHIPS))
        return (acq_view_mem_chips(pc_sess.var.max_total, pc_dec_ui_wide(), PC_DECIM_MAX_TOTAL) >>
                (feature - PC_FEAT_MEM_BASE)) & 1;

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

    case PC_MASK_ACQ_FILTER:
      //1.00o5's Long memory slot holds the decimation Filter selector; ui-sample: the Average, Filter and Sample boxes
      //and the Memory column's note, drawn together (Peak greys two of them)
      pc_glue_acq_row_draw();
      break;

    case PC_MASK_ACQ_MEMORY:
      for(i=0;i<5;i++)
        if(!pc_glue_ui_has(PC_FEAT_MEM_BASE + i))
          pc_mask_rect(ACQ_MENU_XPOS + UI_ACQ_CHIP_X - 2, ACQ_MENU_YPOS + 31 + 36 * i, 58, 29);

      //fw 0.2.38 (owner): the main-loop pass time in the Memory column, so the cost of a longer record shows where it
      //is chosen. It is the last pass before the menu opened (the menu is modal, nothing captures while it is open),
      //so a new depth shows its cost the next time the menu opens. fw 0.2.63: under the column's rule (y 211), two
      //lines; between the header and the first depth chip it collided with both (owner)
      {
        PFONTDATA font = displaydata.font;
        char t[20], *q;

        display_fill_rect(ACQ_MENU_XPOS + UI_ACQ_PASS_X, ACQ_MENU_YPOS + UI_ACQ_PASS_Y, UI_ACQ_PASS_W, 32);
        q = fmt_uint(t, pc_sess.pass_ms);
        fmt_str(q, " ms");
        display_set_font(&font_2);
        display_set_fg_color(0x00A0A0A0);
        display_text(ACQ_MENU_XPOS + UI_ACQ_PASS_X, ACQ_MENU_YPOS + UI_ACQ_PASS_Y, (int8 *)ui_acq_pass_label);
        display_set_fg_color(WHITE_COLOR);
        display_text(ACQ_MENU_XPOS + UI_ACQ_PASS_X, ACQ_MENU_YPOS + UI_ACQ_PASS_Y + 16, (int8 *)t);
        display_set_font(font);
      }
      break;
  }

  displaydata.fg_color = fg;
}

//Acquisition menu: Atlan's "ACQ / Long mode" box names the mode a tap switches to, which reads like an option
//(owner, 0.2.8). Redraw it as a state toggle in the menu's style (Average / Long memory): green with black text when
//the long (roll) time base is on, light grey with white text when off. Called after both 1.00o5 draw sites.
void pc_glue_roll_label(void)
{
  uint16 fg = displaydata.fg_color;
  PFONTDATA font = displaydata.font;
  int on = scopesettings.long_mode != 0;

  if(!pc_ready)
    return;

  display_set_fg_color(on ? GREEN_COLOR : LIGHTGREY_COLOR);
  display_fill_rounded_rect(ACQ_MENU_XPOS + UI_ACQ_BOX_X(0), ACQ_MENU_YPOS + UI_ACQ_BOX_Y, UI_ACQ_BOX_W, UI_ACQ_BOX_H, 3);
  display_set_font(&font_2);
  display_set_fg_color(on ? BLACK_COLOR : WHITE_COLOR);
  //fw 0.2.40 (owner): the label only, the colour shows the state, as the Average box ("Average" / "mode"); fw-acqui:
  //centred as the row's other boxes (pc_acq_box)
  display_text(ACQ_MENU_XPOS + UI_ACQ_BOX_X(0) + (UI_ACQ_BOX_W - ui_text_width(&font_2, "Roll")) / 2, ACQ_MENU_YPOS + UI_ACQ_BOX_LINE1_Y, "Roll");
  display_text(ACQ_MENU_XPOS + UI_ACQ_BOX_X(0) + (UI_ACQ_BOX_W - ui_text_width(&font_2, "mode")) / 2, ACQ_MENU_YPOS + UI_ACQ_BOX_LINE2_Y, "mode");
  displaydata.fg_color = fg;
  displaydata.font     = font;
}

//Boot splash (fw 0.2.17). 1.00o5's load_picture shows "Failed to open file" for 700 ms (or until a touch, with the
//confirmation setting on) when the card has no scope.bmp, which is the normal case: probe silently first. With the
//file present it runs as in 1.00o5 (shown for 3 s). The time spent here goes to GET_LINK_STATS counter 48.
static uint32 pc_boot_splash_ms;

void pc_glue_boot_splash(void)
{
  static FIL fp;
  uint32 t0 = timer0ticks;

  if(f_open(&fp, "scope.bmp", FA_READ) == FR_OK)
  {
    f_close(&fp);
    load_picture("scope.bmp");
  }

  pc_boot_splash_ms = (timer0ticks - t0) + 1;
}

uint32_t pc_hal_boot_splash_ms(void)
{
  return pc_boot_splash_ms;
}

uint32_t pc_hal_fft_bench(uint8_t *dst, uint32_t reps)
{
  return spectrum_bench(dst, reps);
}

uint8_t pc_hal_display_mode(void)
{
  //fw 0.2.66: 5 while the Bode plot shows
  if(bode_view_active())
    return BODE_DISPLAY_MODE;

  return phosphor_mode() ? PHOSPHOR_DISPLAY_MODE : (uint8_t)spectrum_mode();
}

//fw 0.2.70: READ_SCREEN. maindisplaybuffer is what DEBE layer 0 scans (sys_init_display); views that draw into
//displaybuffertmp copy into it, so this is what the panel shows. With the MMU on its pages are write-through (pc_mmu.c,
//firmware/AGENTS.md section 26): the CPU's cached copy equals DDR, a read needs no cache maintenance
typedef char pc_screen_geometry_check[((SCREEN_WIDTH == PC_SCREEN_WIDTH) && (SCREEN_HEIGHT == PC_SCREEN_HEIGHT)) ? 1 : -1];

void pc_hal_screen_rows(uint32_t row, uint32_t nrows, uint8_t *dst)
{
  memcpy(dst, (const uint16 *)maindisplaybuffer + row * SCREEN_WIDTH, nrows * SCREEN_WIDTH * 2);
}

int32_t pc_hal_spectrum_read(int ch, uint32_t first, uint32_t max_bins, uint8_t *hdr, uint8_t *levels)
{
  return spectrum_read(ch, first, max_bins, hdr, levels);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Spectrum view glue (fw 0.2.28) and phosphor view (fw 0.2.46). Since fw 0.2.51 the main menu's mode rows (Scope,
// Spectrum analyser; ui_menu.h) are the only way between the modes; phosphor is a drawing variant of the scope switched
// in the CTRL page's Display fold-out (fw 0.2.56, ctrl_page.c). The spectrum and phosphor views exclude each other.

//fw 0.2.66: the Bode plot (bode_view.c) is a mode too: it draws the trace area, takes its touches and AUTO SET
int pc_glue_spectrum_display(void) { return bode_view_display() || spectrum_display(); }
int pc_glue_spectrum_touch(void)   { return bode_view_touch() || spectrum_touch(); }
int pc_glue_autoset(void)          { return bode_view_autoset() || spectrum_autoset(); }
int pc_glue_autoset_running(void)  { return bode_view_auto_running() || spectrum_auto_running(); }

//fw 0.2.66: scope_acquire_trace_data while the Bode plot shows: no UI acquisition, the view's drawing instead
int pc_glue_bode_pass(void)        { return bode_view_pass(); }
int pc_glue_bode_available(void)   { return bode_view_available(); }
uint16 pc_glue_bode_setting_word(int i)
{
  return (i < 2) ? bode_view_setting_word(i) : (i < 5) ? bode_view_ms_word(i - 2) : bode_view_setting_word(2);
}
void pc_glue_view_opening(void)    { if(bode_view_active()) bode_view_close(); }

//fw 0.2.66 (pc_bode.c): the running bitstream's capabilities, the ring size, a host capture in flight, and the ring
//taken by a Bode capture (the PC's last capture is gone)
uint32 pc_glue_ring_words(void)    { return (pc_ready && pc_sess.var.ring_words) ? pc_sess.var.ring_words : 8192; }

int pc_glue_host_capture(void)
{
  return pc_ready && ((pc_sess.acq_state == PC_ACQ_ARMED) || (pc_sess.acq_state == PC_ACQ_TRIGGERED));
}

void pc_glue_ring_taken(void)
{
  if(pc_ready)
    pc_sess.ring_valid = 0;
}

int  pc_glue_phosphor_on(void)          { return phosphor_owns_traces(); }
void pc_glue_phosphor_draw(void)        { phosphor_draw(); }
int  pc_glue_phosphor_xy(uint32 index)  { return phosphor_xy(index); }

static int pc_spectrum_on(void)
{
  return spectrum_mode() != SPECTRUM_MODE_SCOPE;
}

//Roll off as the acquisition menu's Roll box does (1.00o5 scope_acquisition_ACQ_mode_select): time base 20, its rate
#define PC_ROLL_OFF_TIMEBASE  20

//The phosphor variant is the Display fold-out's Phosphor switch (phosphor_switch, fw 0.2.56): it stays on while the
//spectrum shows, so Scope brings the picture back
void pc_glue_main_menu_mode(int row)
{
  ui_mm_action a;
  int          sp;

  sp = pc_spectrum_on();

  if(!ui_mm_choose(row, sp, scopesettings.long_mode != 0, phosphor_switch(), &a))
    return;

  //fw 0.2.68: the Bode plot is the main menu's third mode row; any other mode row ends it (it gives its settings back)
  if(!a.bode && bode_view_active())
    bode_view_close();

  if(a.roll_off)
  {
    pc_hw_timebase t;

    t.timeperdiv = PC_ROLL_OFF_TIMEBASE;
    t.samplerate = time_per_div_sample_rate[PC_ROLL_OFF_TIMEBASE];
    t.divider    = 0;
    pc_hal_timebase_write(&t, 0);
  }

  if(!a.phosphor && phosphor_mode())
    phosphor_set_active(0);

  if(a.spectrum != sp)
    spectrum_set_active(a.spectrum);

  if(a.phosphor && !phosphor_mode())
    phosphor_set_active(1);

  if(a.warn_roll && !a.bode)
    spectrum_note_roll_off();

  if(a.bode && !bode_view_active())
    bode_view_open();

  //fw 0.2.69: entered from roll mode, the frequency response says so
  if(a.warn_roll && a.bode)
    bode_view_note_roll();
}

//The right bar differs between the modes (empty slots, the CTRL page's items): redrawn whenever the spectrum view
//starts or ends, whatever started or ended it (main menu, roll mode, a viewer, the host)
static uint8 pc_bar_spectrum;

//fw 0.2.68 (owner): a host acquisition in progress shows as RUN (ui_ctrl.h ui_run_view): a host ARM armed or triggered,
//or one that finished less than UI_RUN_HOST_HOLD_MS ago (a burst of host captures holds RUN). A host STOP or
//SET_ACQ_STATE aborts the single (ABORTED), so STOP shows at once.
static uint8  pc_host_seen;
static uint32 pc_host_seen_ms;

int pc_glue_host_busy(void)
{
  if(!pc_ready)
    return 0;

  if((pc_sess.acq_state == PC_ACQ_ARMED) || (pc_sess.acq_state == PC_ACQ_TRIGGERED))
  {
    pc_host_seen    = 1;
    pc_host_seen_ms = timer0ticks;
    return 1;
  }

  if(pc_host_seen && (pc_sess.acq_state == PC_ACQ_DONE) && ((timer0ticks - pc_host_seen_ms) < UI_RUN_HOST_HOLD_MS))
    return 1;

  pc_host_seen = 0;
  return 0;
}

//The RUN/STOP button reads STOP during a host acquisition: a tap ends it (the host's WAIT answers E_ABORTED) and the
//UI stays stopped
void pc_glue_host_stop(void)
{
  if(pc_ready)
    pc_ui_host_stop(&pc_sess);

  pc_host_seen = 0;
}

//fw 0.2.68: the chip and the button follow the run state, whoever changed it (host RUN / STOP / ARM, a single shot that
//completed, the UI); each pass redraws whichever shows a stale value. Not in the file viewer; the button only on page
//one and not in the waveform viewer (its slot holds the previous / next buttons there).
static void pc_run_follow(void)
{
  DISPLAYDATA save;
  int         view = scope_run_view();
  int         chip, btn;

  if(viewactive != VIEW_NOT_ACTIVE)
    return;

  chip = (scope_run_chip_shown != 0xFF) && (scope_run_chip_shown != view);
  btn  = (scopesettings.rightmenustate == 0) && !scopesettings.waveviewmode && (scope_run_button_shown != 0xFF) &&
         (scope_run_button_shown != view);

  if(!chip && !btn)
    return;

  pc_draw_begin(&save);

  if(chip)
    scope_run_stop_text();

  if(btn)
    scope_run_stop_button(0);

  pc_draw_end(&save);
}

static void pc_right_bar_follow(void)
{
  DISPLAYDATA save;
  uint8       sp = (uint8)(pc_spectrum_on() | (bode_view_active() << 1));   //fw 0.2.66: the Bode plot too

  if(sp == pc_bar_spectrum)
    return;

  pc_bar_spectrum = sp;
  save = displaydata;
  display_set_screen_buffer(ui_diag_scope_buffer());           //fw 0.2.59: the kept scope screen under Diagnostics
  scope_setup_right_control_menu();
  displaydata = save;
}

//The scope view's warning line (fw 0.2.56; ui_scope_warning): at the top middle of the plot in every variant, the
//consequence and the action. The overrange flags are taken at the start of the pass, before 1.00o5 clears them.
static uint8 pc_scope_over[2], pc_scope_untrig;

void pc_glue_scope_pass(void)
{
  pc_scope_over[0] = scopesettings.channel1.ADoverload ? 1 : 0;
  pc_scope_over[1] = scopesettings.channel2.ADoverload ? 1 : 0;
}

void pc_glue_scope_warning(void)
{
  ui_warn_in  w;
  const char *t;

  if(scopesettings.waveviewmode)
    return;

  w.en[0]   = scopesettings.channel1.enable ? 1 : 0;
  w.en[1]   = scopesettings.channel2.enable ? 1 : 0;
  w.over[0] = pc_scope_over[0];
  w.over[1] = pc_scope_over[1];
  w.untrig  = pc_scope_untrig;
  w.running = scopesettings.runstate ? 1 : 0;
  w.xy      = scopesettings.xymodedisplay ? 1 : 0;
  //ui-type: the hint names the settings of the trigger type armed (ui-rows.md 2.6)
  w.trig_type = fpga_trig.type;
  w.trig_idle = fpga_trig.h ? 1 : 0;

  t = ui_scope_warning(&w);

  //fw 0.2.64: no warning: the snap notice of acq_view.c ("Time/div set to 1us for this rate"), white
  if(!*t)
  {
    t = acq_view_notice();
    if(!t)
      return;

    display_set_font(&font_2);
    display_set_fg_color(WHITE_COLOR);
    display_text(366 - ui_text_width(&font_2, t) / 2, 69, (int8 *)t);
    return;
  }

  display_set_font(&font_2);
  display_set_fg_color(ORANGE_COLOR);
  display_text(366 - ui_text_width(&font_2, t) / 2, 69, (int8 *)t);
}

//Settings sector word 211 (fw 0.2.56): 0xC700 marker, bit 0 Grid, bits 1-2 the spectrum's Display variant. The scope's
//variants are kept where they were (X-Y: 1.00o5's word; Phosphor: word 210 bit 3). ui-sample: bit 3 Sample Peak (the
//codec is ui_ctrl.c's ui_word_ui, marker check 0xFFF0; a sector written before reads Normal)
uint16 pc_glue_ui_setting_word(void)
{
  return ui_word_ui(scopesettings.gridenable, spectrum_view(), pc_dec_peak);
}

static void pc_ui_setting_restore(uint16 w)
{
  uint8 grid, view, peak;

  if(!ui_word_ui_restore(w, &grid, &view, &peak))
    return;

  scopesettings.gridenable = grid;
  spectrum_set_view(view);
  pc_dec_peak = peak;

  //Sample Peak runs the 16-bit layout: the depth limit as for the Filter (fixed up before this for the Filter alone)
  if(pc_dec_ui_wide())
    pc_dec_fit_memory();
}

//System settings menu height (variables.h SYSTEM_MENU_HEIGHT): the fold-out rows of ui_settings.h, Time and date only
//with the clock module
int pc_glue_sys_menu_height(void)
{
  return (int)ui_fo_height(pc_glue_ui_has(PC_FEAT_RTC));
}

//Battery level for the indicator: a running average (x16, 1/8 per pass), then the 1.00o5 rule on it (level only drops
//while not charging) or, while charging, a change only when the average moves 1.25 steps away from what is shown
int pc_glue_battery_level(int raw, int charging, int shown)
{
  static int32 avg16;
  static uint8 init;
  int32 lvl;

  if(!init)
  {
    avg16 = raw * 16;
    init  = 1;
  }

  avg16 += (raw * 16 - avg16) / 8;
  lvl    = (avg16 + 8) / 16;

  if(lvl < 0)
    lvl = 0;

  if(charging)
    return ((avg16 >= shown * 16 + 20) || (avg16 <= shown * 16 - 20)) ? lvl : shown;

  return (lvl < shown) ? lvl : shown;
}

//Settings saved by an earlier firmware (or another unit) may select what this hardware cannot do
//----------------------------------------------------------------------------------------------------------------------------------
// Restart record (fw 0.2.19). The bench saw 0.2.16 restart from RAM (uptime reset, version unchanged): a jump to the
// image entry. start.s stores LR/SP/CPSR at _reset and, for an exception, its type/LR/SPSR/SP, at this address. The
// previous run leaves PC_RST_ALIVE in it; DDR keeps it across a warm restart, a power cycle does not.

#define PC_RST_ADDR       ((volatile uint32 *)0x81A00000)
#define PC_RST_ALIVE      0x4C495645u      //'EVIL' little-endian: the previous run was alive
#define PC_RST_EXC_MAGIC  0x45584321u      //exception fields valid

static uint32 pc_restart_info[8];          //restarts, type, exception LR, SPSR, exception SP, reset LR, reset SP, reset CPSR
static uint32 pc_fault_info[PC_FAULT_COUNT];   //fw 0.2.48: fault status (DFSR / IFSR by type), fault address (FAR)

static void pc_restart_check(void)
{
  volatile uint32 *r = PC_RST_ADDR;
  uint32 restarts = 0;

  //[0] alive, [1] restart count, [2] reset LR, [3] reset SP, [4] reset CPSR, [5] exc type, [6] exc LR, [7] SPSR,
  //[8] exc SP, [9] exc magic, fw 0.2.48: [10] DFSR, [11] IFSR, [12] FAR
  if(r[0] == PC_RST_ALIVE)
  {
    restarts = r[1] + 1;

    pc_restart_info[1] = (r[9] == PC_RST_EXC_MAGIC) ? r[5] : 0;
    pc_restart_info[2] = (r[9] == PC_RST_EXC_MAGIC) ? r[6] : 0;
    pc_restart_info[3] = (r[9] == PC_RST_EXC_MAGIC) ? r[7] : 0;
    pc_restart_info[4] = (r[9] == PC_RST_EXC_MAGIC) ? r[8] : 0;
    pc_restart_info[5] = r[2];
    pc_restart_info[6] = r[3];
    pc_restart_info[7] = r[4];

    //The fault registers mean something only for the abort that wrote them
    if((r[9] == PC_RST_EXC_MAGIC) && (r[5] == 4))
    {
      pc_fault_info[0] = r[10];
      pc_fault_info[1] = r[12];
    }
    else if((r[9] == PC_RST_EXC_MAGIC) && (r[5] == 3))
      pc_fault_info[0] = r[11];
  }

  pc_restart_info[0] = restarts;
  r[1] = restarts;
  r[9] = 0;
  r[0] = PC_RST_ALIVE;
}

void pc_hal_restart_info(uint32_t *v)
{
  uint32 i;

  for(i=0;i<8;i++)
    v[i] = pc_restart_info[i];
}

void pc_hal_fault_info(uint32_t *v)
{
  uint32 i;

  for(i=0;i<PC_FAULT_COUNT;i++)
    v[i] = pc_fault_info[i];
}

//ENTER_FEL is a deliberate exit: the next image must not count it as a restart
static void pc_restart_clear(void)
{
  PC_RST_ADDR[0] = 0;
  PC_RST_ADDR[1] = 0;
}

static void pc_ui_fix_settings(void)
{
  uint32 max = pc_sess.var.max_total, total;

  pc_rtc_present = pc_probe_ds3231();

  //Without the module the clock, file stamps and FAT times would show garbage
  if(!pc_rtc_present)
    onoffRTC = 0;

  //Acquisition Filter (decimation, 0x1534+): kept in a spare settings word; a sector without a valid word (new
  //settings, or one written before 0.2.30) keeps the per-mode defaults. With it on the 16-bit layout limits the memory
  //depth.
  //fw 0.2.64: per mode. Word 207 is the Scope's, word 212 (0xD500 marker) the Spectrum's. A sector of 0.2.30..0.2.63 has
  //one Filter for both: the old default (CIC2 + FIR) becomes the new per-mode defaults, any other choice is kept for both.
  if((settingsworkbuffer[PC_SETTING_WORD_DECIM] & 0xFFC4) == 0xDE00)
  {
    uint8 old = (uint8)(settingsworkbuffer[PC_SETTING_WORD_DECIM] & (PC_DECIM_SET_FIR | 3));

    if((settingsworkbuffer[PC_SETTING_WORD_DECIM_SPEC] & 0xFFDC) == 0xD500)
    {
      //fw-acqui (owner 2026-10-21): the 0.2.64..0.2.105 defaults (Scope CIC2, Spectrum CIC2+FIR) become the new ones
      //(Hi-Res, Anti-alias); any other saved mode loads as it is (ui_ctrl.h ui_filter_migrate)
      pc_dec_set_mode[0] = ui_filter_migrate(old, 0);
      pc_dec_set_mode[1] = ui_filter_migrate((uint8)(settingsworkbuffer[PC_SETTING_WORD_DECIM_SPEC] & (PC_DECIM_SET_FIR | 3)), 1);
    }
    else if(old != PC_DECIM_DEFAULT_OLD)
    {
      pc_dec_set_mode[0] = ui_filter_migrate(old, 0);
      pc_dec_set_mode[1] = ui_filter_migrate(old, 1);
    }
  }

  if(pc_dec_setting & 3)
    pc_dec_fit_memory();

  //Trace style 3 was 1.00o5's "Pro", removed in fw 0.2.63: a sector saved with it draws Linear
  if(scopesettings.ACQ_trace >= UI_TRACE_COUNT)
    scopesettings.ACQ_trace = 0;

  //Spectrum view settings (fw 0.2.39); the display mode itself is not restored: the scope boots into the scope view
  spectrum_settings_restore(settingsworkbuffer[PC_SETTING_WORD_SPEC0], settingsworkbuffer[PC_SETTING_WORD_SPEC1]);

  //Phosphor view settings (fw 0.2.46): persistence and palette, the same rule (marker 0x9A00, fields in range); since
  //fw 0.2.56 also the Phosphor switch
  phosphor_settings_restore(settingsworkbuffer[PC_SETTING_WORD_PHOS]);

  //fw 0.2.56: Grid and the spectrum's Display variant (word 211)
  pc_ui_setting_restore(settingsworkbuffer[PC_SETTING_WORD_UI]);

  //fw 0.2.66: the Bode plot's sweep settings (words 219, 220); the mode itself is not restored
  bode_view_settings_restore(settingsworkbuffer[PC_SETTING_WORD_BODE0], settingsworkbuffer[PC_SETTING_WORD_BODE1],
                             settingsworkbuffer[PC_SETTING_WORD_BODE2]);
  //fw 0.2.69: its MEASURES list (words 213..215; no marker: the defaults)
  bode_view_ms_restore(&settingsworkbuffer[PC_SETTING_WORD_BODE_MS]);

  //ui-type: the trigger Type row (words 202 / 203, marker 0xA000): Edge on a sector written before it (0 there); the
  //Width / Idle time snapped into the rate's range silently and the registers written (trig_ui.h, ui-rows.md section 4)
  (void)tui_restore(&trig_ui, settingsworkbuffer[PC_SETTING_WORD_TRIG], settingsworkbuffer[PC_SETTING_WORD_TRIG + 1]);
  pc_trig_restore_apply();

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

  //fw 0.2.64: a sector written by an older firmware may hold a time/div that does not fit its rate and record
  pc_dec_mode_seen = 0;
  acq_view_fit(0);
}

void pc_glue_init(void)
{
#if PC_FORCE_CDC
  //The control port is the point of this build: CDC at boot regardless of the saved USB mode setting
  USB_CH340 = 1;
#endif

  //Version word from FPGA command 0x06, read once (main() read it already; this read has no side effect)
  pc_fpga_word = fpga_get_version();
  pc_restart_check();

  //fw 0.2.52: unit life record (power-on count, restart record kept over power cycles), one write of sector 709
  {
    uint32_t v[10], i;

    for(i=0;i<8;i++)
      v[i] = pc_restart_info[i];
    v[8] = pc_fault_info[0];
    v[9] = pc_fault_info[1];
    pc_diag_boot(pc_restart_info[0] ? v : 0);
  }
  //The FPGA features are set up only on the bitstream this firmware is built for (PC_FW_FPGA_VERSION): on a mismatch
  //nothing is written to registers the firmware does not know, the session refuses the acquisition commands and the main
  //loop shows the notice screen (ui_fpga_mismatch_pass) instead of the scope
  if(pc_fpga_word == PC_FW_FPGA_VERSION)
  {
    //fw 0.2.62: the full-rate capture check (enc_heal.h): the 0x33 encode convention, which the session reads for its
    //core-order cap (fw 0.2.72, pc_variant_core_order), so first
    fpga_encode_init();
    //fw 0.2.79: the capture stages, the stored value when it was trained on this bitstream (cap_hw.c)
    fpga_capture_init(pc_fpga_word);
    //fw-c5 (0x153C): the trigger extension at its power-on state (edge, no hold-off) and the hold-off clear request for
    //the first arm: a FEL load leaves the FPGA as the previous firmware left it (fpga_bus.h)
    fpga_trig_write(&fpga_trig, 1);
    //C7 counter: 0x62 = 128 / 6, 0x61 = gate 100 ms on the trigger source (fpga_counter.h); the timing cards take the
    //counter's values from here on (ui_meas.c, the pass in pc_glue_counter_pass)
    fpga_counter_boot();
    ui_ms_counter_register();
  }
  pc_session_init(&pc_sess);
  pc_ready = 1;
  pc_ui_fix_settings();

  //A restart is logged once, as event type 15 (a = exception type, 0 = jump to the entry; b = low 16 bits of the
  //exception LR, or of the LR found at _reset)
  if(pc_restart_info[0])
    pc_usb_log_restart((uint8)pc_restart_info[1], (uint16)(pc_restart_info[1] ? pc_restart_info[2] : pc_restart_info[5]));
}

//----------------------------------------------------------------------------------------------------------------------------------
//D4 logger (fw-d4, FPGA 0x153D, protocol 1.27 row 67; pc_hal.h, fpga_log.h). One log: the FPGA side and the DDR ring in the
//NOLOAD `.logring` section after .bss (fnirsi_1013d.ld: not in the image, not cleared at boot, nothing reads it before a
//LOG_START resets the ring). While it runs the main loop is pc_log_loop: a drain step and a MAIN poll per iteration (the
//poll answers LOG_READ, GET_STATUS, ...; the rest E_STATE 9), the Scope view frozen with its notice slot reading "Logging to
//the host: 12 s, 6.0 MB" (redrawn every 500 ms). No UI pass runs: the FPGA ring holds 16 ms at the fastest tier, a pass
//takes 15-40 ms. The person at the scope ends a log only through the host (LOG_STOP, or the host going away: pc_cmds.c).
#include "fpga_log.h"

static fpga_log pc_log;
static uint8    pc_log_mem[PC_LOG_RING_BYTES] __attribute__((section(".logring"), aligned(64)));
static uint32   pc_log_ms0, pc_log_ms1;
static uint32   pc_log_div_saved;          //0x0D the UI had: written back when the log ends
static uint8    pc_log_open;               //started and not yet finished (the divider to restore)

//Decimal digits of v at p; returns their count
static uint32 pc_log_u32(char *p, uint32 v)
{
  char   d[10];
  uint32 n = 0, i;

  do
  {
    d[n++] = (char)('0' + v % 10);
    v /= 10;
  } while(v);

  for(i=0;i<n;i++)
    p[i] = d[n - 1 - i];

  return n;
}

//"12 s, 6.0 MB, 3 gaps" into t (at least 48 bytes)
static void pc_log_stats(char *t)
{
  uint32 s  = ((pc_log.state == FPGA_LOG_RUNNING) ? timer0ticks : pc_log_ms1) - pc_log_ms0;
  uint32 mb = (uint32)(pc_log.ring.wr / 104858);          //tenths of a MiB
  char  *p  = t;

  p += pc_log_u32(p, s / 1000);
  *p++ = ' '; *p++ = 's'; *p++ = ','; *p++ = ' ';
  p += pc_log_u32(p, mb / 10);
  *p++ = '.';
  *p++ = (char)('0' + mb % 10);
  *p++ = ' '; *p++ = 'M'; *p++ = 'B';

  if(pc_log.gaps)
  {
    *p++ = ','; *p++ = ' ';
    p += pc_log_u32(p, pc_log.gaps);
    memcpy(p, (pc_log.gaps == 1) ? " gap" : " gaps", 6);
    p += strlen(p);
  }

  *p = 0;
}

//The Scope view's notice slot (pc_glue_scope_warning's place), white on black
static void pc_log_notice(void)
{
  DISPLAYDATA save;
  char t[80] = "Logging to the host: ";

  pc_log_stats(t + strlen(t));
  pc_draw_begin(&save);
  display_set_fg_color(BLACK_COLOR);
  display_fill_rect(146, 66, 440, 18);
  display_set_font(&font_2);
  display_set_fg_color(WHITE_COLOR);
  display_text(366 - ui_text_width(&font_2, t) / 2, 69, (int8 *)t);
  pc_draw_end(&save);
}

//The UI's divider back; the end time
static void pc_log_finish(void)
{
  if(!pc_log_open)
    return;

  pc_log_open = 0;
  pc_log_ms1  = timer0ticks;
  fpga_write_cmd(0x0D);
  fpga_write_int(pc_log_div_saved);
}

int pc_hal_log_start(uint8_t ch_mask, uint8_t format, uint32_t divider, const pc_decim_cfg *dc, uint64_t limit)
{
  pc_seq_cfg off;
  int r;

  //A UI conversion in flight and its firmware auto time-out are abandoned (the UI is in STOP: pc_cmds.c); fw-roll: so is
  //the roll's stream
  pc_glue_roll_release();
  pc_ui_auto = 0;

  pc_log_div_saved = fpga_divider;
  pc_log_open      = 1;
  fpga_write_cmd(0x0D);
  fpga_write_int(divider);

  //The filter / peak registers settled before the reset, the sequencer off
  pc_dec_apply(dc);
  memset(&off, 0, sizeof(off));
  pc_seq_ui_conversion(0, dc, 0);
  pc_seq_hw_pre_reset(&off);

  r = fpga_log_start(&pc_log, ch_mask, format, limit, pc_log_mem, sizeof(pc_log_mem), PC_READY_POLLS);
  pc_log_ms0 = pc_log_ms1 = timer0ticks;

  if(r)
    pc_log_finish();

  return r;
}

void pc_hal_log_stop(uint8_t state)
{
  fpga_log_stop(&pc_log, state);
  pc_log_finish();
}

void pc_hal_log_info(pc_hw_log *l)
{
  memset(l, 0, sizeof(*l));
  l->state      = pc_log.state;
  l->ch_mask    = pc_log.ch_mask;
  l->format     = pc_log.format;
  l->flags      = pc_log.flags;
  l->produced   = pc_log.ring.wr;
  l->sent       = pc_log.ring.rd;
  l->gaps       = pc_log.gaps;
  l->ring_bytes = PC_LOG_RING_BYTES;
  l->ms         = ((pc_log.state == FPGA_LOG_RUNNING) ? timer0ticks : pc_log_ms1) - pc_log_ms0;
}

uint32_t pc_hal_log_read(uint8_t *dst, uint32_t max, uint64_t *word, uint8_t *flags)
{
  return pc_logring_read(&pc_log.ring, dst, max, word, flags);
}

//The log loop (from pc_glue_poll_main after the MAIN poll that started a log)
static void pc_log_loop(void)
{
  uint32 next;
  uint8  who;
  char   t[48];

  if(pc_log.state != FPGA_LOG_RUNNING)
    return;

  who            = fpga_trace_who;
  fpga_trace_who = FPGA_WHO_HOST;
  pc_log_notice();
  next = timer0ticks + 500;

  while(pc_log.state == FPGA_LOG_RUNNING)
  {
    fpga_log_drain(&pc_log);
    pc_poll(&pc_sess, PC_CTX_MAIN);

    if((int32)(timer0ticks - next) >= 0)
    {
      pc_log_notice();
      next += 500;
    }
  }

  //Ended (duration, LOG_STOP, an error): the UI's divider back, the summary as the slot's notice for a while
  pc_log_finish();
  fpga_trace_who = who;
  pc_log_stats(t);
  acq_view_notice_queue(1, "Log ended: ", t, "");
}
//D4 logger end

//----------------------------------------------------------------------------------------------------------------------------------
//fw-roll: the roll mode's stream (roll.h, roll_view.c). The long time bases take their columns from the 0x153D logger
//(fpga_log.c): both channels, legacy 8-bit layout, the divider of roll_plan, into a small ring of its own (the host log's
//8 MiB ring stays readable after a log). The FPGA has one owner: every arm (fpga_do_conversion, pc_hal_arm,
//pc_hal_log_start) releases the stream first, so a host ARM or LOG_START, the calibration or AUTO SET take the FPGA from
//the roll as a host capture takes it from the UI's conversions; the roll pass starts it again while the UI rolls.
static fpga_log pc_roll;
static uint8    pc_roll_mem[32768] __attribute__((aligned(64)));     //4 FPGA rings' worth of chunks; read every pass
static uint8    pc_roll_open;              //started and not yet released (0x0D to restore)
static uint8    pc_roll_tpd;               //the time base the stream runs for
static uint32   pc_roll_ui_div;            //fpga_divider at the start: a UI rate write since (it also wrote 0x0D) restarts

int pc_glue_roll_running(uint8 tpd)
{
  return pc_roll_open && (pc_roll.state == FPGA_LOG_RUNNING) && (pc_roll_tpd == tpd) && (pc_roll_ui_div == fpga_divider);
}

void pc_glue_roll_release(void)
{
  if(!pc_roll_open)
    return;

  //0x69 = 0 (no tail: the roll does not need the last words); the UI's divider back
  fpga_log_stop(&pc_roll, FPGA_LOG_ENDED);
  fpga_write_cmd(0x0D);
  fpga_write_int(fpga_divider);
  pc_roll_open = 0;
}

int pc_glue_roll_start(uint8 tpd, uint32_t *wpc)
{
  pc_decim_cfg dc;
  pc_seq_cfg   off;
  uint32_t     div;
  int          r;

  pc_glue_roll_release();

  if(roll_plan(tpd, &div, wpc))
    return -3;

  //The UI resets the sample system: a host single in flight ends, the ring no longer holds the last capture
  pc_glue_ui_capture_start();
  pc_ui_auto = 0;

  pc_roll_open   = 1;
  pc_roll_tpd    = tpd;
  pc_roll_ui_div = fpga_divider;
  fpga_write_cmd(0x0D);
  fpga_write_int(div);

  //K = 0 (the legacy layout, two samples per word per channel), the sequencer off
  pc_decim_config(0, 0, div, 2, pc_decim_has(), &dc);
  pc_dec_apply(&dc);
  memset(&off, 0, sizeof(off));
  pc_seq_ui_conversion(0, &dc, 0);
  pc_seq_hw_pre_reset(&off);

  r = fpga_log_start(&pc_roll, 3, FPGA_LOG_F_LEGACY, 0, pc_roll_mem, sizeof(pc_roll_mem), PC_READY_POLLS);

  if(r)
    pc_glue_roll_release();

  return r;
}

int32 pc_glue_roll_read(uint8 *dst, uint32 max, int *gap)
{
  uint64_t w;
  uint8_t  f;
  uint32   n, k;

  if(!pc_roll_open)
    return -1;

  for(k=0;k<4;k++)
  {
    n = pc_logring_read(&pc_roll.ring, dst, max, &w, &f);

    if(n)
    {
      *gap = (f & PC_LOG_CF_GAP) != 0;
      return (int32)n;
    }

    if(pc_roll.state != FPGA_LOG_RUNNING)
      return -1;

    if(!fpga_log_drain(&pc_roll))
      return (pc_roll.state == FPGA_LOG_RUNNING) ? 0 : -1;
  }

  return 0;
}

void pc_glue_poll_main(void)
{
  if(pc_ready)
  {
    uint8 who = fpga_trace_who;

    fpga_trace_who = FPGA_WHO_HOST;
    pc_poll(&pc_sess, PC_CTX_MAIN);
    fpga_trace_who = who;

    //D4 logger: a LOG_START in this poll: the main loop is the log loop until the log ends
    pc_log_loop();
  }

  pc_ui_auto_service();
  pc_seq_ui_service();
}

void pc_glue_poll_busy(void)
{
  if(pc_ready)
  {
    uint8 who = fpga_trace_who;

    fpga_trace_who = FPGA_WHO_HOST;
    pc_poll(&pc_sess, PC_CTX_BUSY);
    fpga_trace_who = who;
  }
}

void pc_glue_poll_isr(void)
{
  //Timer0 IRQ, every ms: PING/GET_INFO/GET_STATUS/GET_BATTERY/#VER/#RESET and the TX pump without waiting for the
  //UI loop. Skipped while a main-loop or BUSY poll is running (in_poll), so it never interleaves with one.
  //fw 0.2.49: the USB suspend watch first, also before pc_ready and while a menu is modal
  pc_usb_watch_tick();

  if(pc_ready)
  {
    uint8 who = fpga_trace_who;

    fpga_trace_who = FPGA_WHO_HOST;
    pc_poll(&pc_sess, PC_CTX_ISR);
    fpga_trace_who = who;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//fw 0.2.78 (protocol 1.23, docs/protocol.md section 14 row 63): host requests close the modal screens. pc_cmds.c sets
//pc_sess.yield_req from the timer interrupt or a BUSY poll when a request needs the main loop while the UI is modal; every
//modal loop asks PC_UI_YIELD() once per iteration and leaves by its own cancel path (nested ones one after the other) until
//the main loop answers the request. pc_glue_ui_hold marks the steps that must finish (E_BUSY with the reason instead).

typedef char pc_hold_codes_check[((PC_HOLD_CAL == PC_BUSY_CAL) && (PC_HOLD_SAVE == PC_BUSY_SAVE) &&
                                  (PC_HOLD_USB_DRIVE == PC_BUSY_USB_DRIVE)) ? 1 : -1];

static volatile uint8 pc_ui_hold_why;   //PC_HOLD_* while a step that must not be cancelled runs, 0 none

uint8_t pc_hal_ui_hold(void)
{
  return pc_ui_hold_why;
}

//fw 0.2.96: Diagnostics (a main-loop screen) goes Back before a state-changing request runs (pc_cmds.c pc_dispatch);
//the restored scope screen is the one the background redraws kept up to date, the request's redraws follow on it
int pc_hal_ui_leave_screen(void)
{
  return ui_diag_leave();
}

int pc_glue_ui_hold(int why)
{
  int prev = pc_ui_hold_why;

  pc_ui_hold_why = (uint8)why;
  return prev;
}

int pc_glue_ui_yield(void)
{
  if(!pc_ready)
    return 0;

  //fw 0.2.94: a read-only request (READ_SCREEN, GET_*) is answered here, between the modal loop's iterations, and the
  //screen stays open; a state-changing one still asks it to close (1.23)
  if(pc_ui_modal_pending(&pc_sess))
  {
    uint8 who = fpga_trace_who;

    fpga_trace_who = FPGA_WHO_HOST;
    pc_poll(&pc_sess, PC_CTX_MODAL);
    fpga_trace_who = who;
  }

  return pc_ui_yield_pending(&pc_sess);
}

void pc_glue_ui_service(void)
{
  if(pc_ready)
    pc_ui_service(&pc_sess);

  //fw 0.2.52: settings changed since the last save, once per second
  pc_diag_service();

  //fw 0.2.105: a capture stage the runtime guard moved to, into sector 708
  fpga_capture_service();

  //Roll mode or a viewer ends the spectrum view and the phosphor view; the right bar follows the mode
  spectrum_service();
  phosphor_service();

  //fw 0.2.66: the Bode plot: a pending open / close, its sweep (a few engine steps per pass), RUN / STOP
  bode_view_service();
  pc_right_bar_follow();
  pc_dec_follow_mode();
  pc_run_follow();
}

//fw 0.2.52 (pc_diag.c): the session for the report, the DS3231 for the calibration provenance
pc_session *pc_glue_session(void) { return &pc_sess; }
int pc_glue_rtc_present(void)     { return pc_rtc_present; }

void pc_glue_pass_mark(int n)
{
  if(pc_ready)
    pc_ui_pass_mark(&pc_sess, n);
}

//The RTC clock shares the chip's slot: not drawn while the chip shows, nor over Diagnostics (fw 0.2.59: the pass that
//opens it still reaches the clock)
int pc_glue_rtc_allowed(void)
{
  return !ui_diag_active() && (!pc_ready || !pc_ui_chip_shown(&pc_sess));
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
  {
    pc_ui_capture_addr(&pc_sess, (uint16_t)addr);

    //A record the firmware time-out ended is untriggered, whatever 0x18 says now
    if(pc_ui_forced)
      pc_sess.ui_trig = 0;

    //An averaging run is triggered when it holds a record (0x18 is the last record's); the MCU path's further records
    //are armed and summed here (pc_seq_hw.c)
    pc_seq_ui_done(&pc_sess.ui_trig, addr);
  }

  pc_scope_untrig = (!pc_ui_forced && (!pc_ready || pc_sess.ui_trig || scopesettings.triggermode)) ? 0 : 1;
  spectrum_capture_info(!pc_scope_untrig);
  phosphor_capture_info(!pc_scope_untrig);
  pc_ui_auto = 0;
}

void pc_glue_ui_capture(void)
{
  pc_seq_ui_read_done();
  spectrum_new_data();
  phosphor_new_data();

  if(pc_ready)
    pc_ui_capture_done(&pc_sess);
}

//----------------------------------------------------------------------------------------------------------------------------------
