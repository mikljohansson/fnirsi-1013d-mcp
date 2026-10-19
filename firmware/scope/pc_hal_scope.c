//----------------------------------------------------------------------------------------------------------------------------------
// pc_hal_scope.c - pc_hal.h on the FNIRSI 1013D (1.00o5 settings, fpga_control.c, display) and the firmware glue that owns
// the one pc_session. The transport part of pc_hal.h is in pc_transport.c.
//
// Bench tests that must confirm an assumption are marked "BENCH: Tn" (protocol.md 12).
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>

#include "types.h"
#include "fnirsi_1013d_scope.h"
#include "fpga_control.h"
#include "enc_heal.h"
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
#include "spectrum.h"
#include "phosphor.h"
#include "spec_fmt.h"
#include "acal_live.h"
#include "ui_menu.h"
#include "ui_settings.h"
#include "ui_fmt.h"
#include "ui_ctrl.h"
#include "pc_seq_hw.h"
#include "ui_diag_view.h"

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
  c->divider         = fpga_divider;
  c->cal2            = adc_cal[ch ? 1 : 0];
  c->trk             = acal_trk[ch ? 1 : 0];
  c->cm              = acal_cms[ch ? 1 : 0];
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
    fpga_divider = t->divider;
    pc_glue_divider_written(t->divider);
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

static void pc_dec_apply(const pc_decim_cfg *c);

static uint8  pc_ui_auto;                             //defined with the firmware auto mode below

int pc_hal_arm(uint16_t pre_words, uint16_t total_words, uint32_t auto_timeout, int prog, int normal,
               const pc_decim_cfg *dc, const pc_seq_cfg *sq)
{
  uint32 n = 0;

  //fw 0.2.61: after a rate write, a full-rate check first (enc_heal.h; the host's legacy mode at divider 0 is the
  //check's, so pc_dec_apply below writes no 0x53)
  fpga_encode_check_dirty();

  //Trigger level from the marker, as every UI conversion does (scope_acquire_trace_data)
  fpga_set_trigger_level();

  //The host capture replaces any UI conversion in flight (and its firmware auto time-out)
  pc_ui_auto = 0;

  //Decimation filter mode, shift and 16-bit level, settled before the reset (0x1534+ only)
  if(dc)
    pc_dec_apply(dc);

  //Trigger enable (dead on Atlan4 bitstreams, meaningful on stock)                    BENCH: T6, T10
  fpga_write_cmd(0x0F);
  fpga_write_byte(0x00);

  //Configured trigger mode again: FORCE leaves 0x1A in auto
  fpga_write_cmd(0x1A);
  fpga_write_byte(normal ? 0x01 : 0x00);

  //Sequencer (0x1536+): geometry and run setting before the reset (pc_seq_hw.c); a host capture ends a UI averaging run
  if(sq && prog)
  {
    pc_seq_ui_conversion(0, dc, 0);
    pc_seq_hw_pre_reset(sq, pre_words, total_words);
  }

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

uint8_t pc_hal_ext_status(void)
{
  //Read only on 0x1533+ (pc_cmds checks EXT_TRIGGER): older bitstreams return the stale bus byte
  fpga_write_cmd(0x28);
  return fpga_read_byte();
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
  if(pc_ready && (pc_sess.var.caps & PC_CAP_EXT_TRIGGER))
  {
    fpga_set_trigger_channel();
    pc_ext_generator_off();
  }
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
  pc_seq_trace_replaced();

  if(ch_mask & 0x01)
    fpga_read_sample_data(&scopesettings.channel1, start_word);

  if(ch_mask & 0x02)
    fpga_read_sample_data(&scopesettings.channel2, start_word);

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

static uint8         pc_dec_setting = PC_DECIM_DEFAULT;   //bits 1:0 requested K (UI Filter), PC_DECIM_SET_FIR;
                                                      //saved in the settings sector
static uint8         pc_dec_flags = PC_INTERLEAVE_ORDER ? PC_DECIM_SWAP : 0;  //0x53 bits 2-4: swap (host only, not
                                                      //saved), falling-edge capture A / B (hidden, saved)
static uint8         pc_dec_ui_arm;                   //the next fpga_do_conversion is the UI acquisition's
static pc_decim_cfg  pc_dec_cur;                      //what the FPGA runs (k = 0 until written)
static uint8         pc_dec_written;                  //pc_dec_cur was written at least once
static uint8         pc_dec_dirty;                    //0x0D changed since the last write
static uint32        pc_dec_divider = 0xFFFFFFFF;     //last 0x0D value written
static uint16        pc_dec_ui_addr;                  //0x14 of the last UI capture
static uint16        pc_dec_raw[2][PC_DEC_RING];      //16-bit samples of the last readout per channel
static uint32        pc_dec_raw_n[2];                 //their count, 0 = that readout was 8-bit
static int16         pc_dec_feed[PC_DEC_RING + 2];    //converted samples for fpga_read_adc_data
static int16        *pc_dec_even_src;
int16               *pc_decim_src;

//PC_DECIM_HAS_*: what the bitstream offers (0 = no filter)
static int pc_dec_has(void)
{
  if(!pc_ready || !(pc_sess.var.caps & PC_CAP_DECIM))
    return PC_DECIM_HAS_NONE;

  return (pc_sess.var.caps & PC_CAP_FIR) ? PC_DECIM_HAS_FIR : PC_DECIM_HAS_CIC;
}

static uint32 pc_dec_div(void)
{
  if(pc_dec_divider != 0xFFFFFFFF)
    return pc_dec_divider;

  return (scopesettings.samplerate < (sizeof(sample_rate_settings) / sizeof(uint32))) ? sample_rate_settings[scopesettings.samplerate] : 0;
}

//set: bits 1:0 K, PC_DECIM_SET_FIR (pc_dec_setting, or 0 for the legacy path)
static void pc_dec_config(pc_decim_cfg *c, uint8 set)
{
  pc_decim_config(set & 3, (uint8)(pc_dec_flags | (set & PC_DECIM_SET_FIR)), pc_dec_div(), fpgasettings.totalsamples,
                  pc_dec_has(), c);
}

//Write c to the FPGA (only what changed, the 16-bit level always) and wait for the filter to settle
static void pc_dec_apply(const pc_decim_cfg *c)
{
  uint8 mode = pc_decim_mode(c);
  int32 level;

  if(!pc_dec_has())
    return;

  if(!pc_dec_written || (mode != pc_decim_mode(&pc_dec_cur)))
  {
    fpga_set_decim_mode(mode);
    pc_dec_dirty = 1;
  }

  //0x1535+: FIR enable (0x56); older bitstreams never get the command
  if((pc_dec_has() >= PC_DECIM_HAS_FIR) && (!pc_dec_written || (c->fir_en != pc_dec_cur.fir_en)))
  {
    fpga_set_decim_fir(c->fir_en);
    pc_dec_dirty = 1;
  }

  if(c->k)
  {
    if(!pc_dec_written || !pc_dec_cur.k || (c->shift != pc_dec_cur.shift))
    {
      fpga_set_decim_shift(c->shift);
      pc_dec_dirty = 1;
    }

    //0x17 is ignored while K >= 1: the UI level (8-bit code in the trace domain, as fpga_set_trigger_level computed it)
    //mapped back through the readout's 16-bit transform (fw 0.2.47): the trigger channel's gain16 folded into the
    //scale and its corr16 zero term, so the level sits where the corrected trace shows it (it ignored both before,
    //about 1 % level error at full rate). EXT has no samples: the plain conversion.
    level = (int16)scopesettings.triggerlevel;
    level = (level < 0) ? 0 : ((level > 255) ? 255 : level);

    if(scopesettings.triggerchannel < 2)
    {
      acal_corr k;

      fpga_channel_correction(scopesettings.triggerchannel ? &scopesettings.channel2 : &scopesettings.channel1, &k);
      fpga_set_decim_level(pc_decim_level16_corr((uint8)level, acal_scale(c->scale_q24, k.gain16), k.corr16));
    }
    else
      fpga_set_decim_level(pc_decim_level16((uint8)level, c->gain, c->shift));

    //(K + 2) output periods, (K + 10) N x 5 ns + 1 us with the FIR; timer0 ticks are 1 ms, + 1 for the partial tick
    if(pc_dec_dirty)
      timer0_delay((pc_decim_settle_us(c) + 999) / 1000 + 1);
  }

  pc_dec_dirty   = 0;
  pc_dec_cur     = *c;
  pc_dec_written = 1;
}

void pc_glue_divider_written(uint32 divider)
{
  if(divider != pc_dec_divider)
  {
    pc_dec_divider = divider;
    pc_dec_dirty   = 1;
  }
}

void pc_glue_ui_decim_arm(void)
{
  pc_dec_ui_arm = 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Firmware auto mode (fw 0.2.41). The FPGA's auto time-out (0x0E) counts samples (the record length), so at fast rates
// it expires in microseconds (3000 samples at 10 MS/s = 0.3 ms), before a slow signal's next edge; and 0x18 keeps
// watching after sampling stopped, so such a record read "triggered" when an edge came between the stop and the read
// (bench: flat records flagged triggered). On Atlan bitstreams the UI's auto mode now arms in normal mode and the main
// loop gives up after max(100 ms, two record lengths): 0x1A = 0 then ends the sampling at once (the time-out is long
// past) with an untriggered record, which is reported as such. Calibration and auto setup keep 1.00o5's FPGA auto.

static uint8  pc_ui_auto;                             //a UI conversion runs in firmware auto mode
static uint8  pc_ui_forced;                           //it was ended by the firmware time-out (untriggered)
static uint32 pc_ui_auto_t0, pc_ui_auto_ms;

static int pc_fw_auto_possible(void)
{
  return (fpgasettings.fw_FPGA != 1) && !scopesettings.long_mode;
}

void pc_glue_decim_conversion(void)
{
  pc_decim_cfg c;
  int          ui = pc_dec_ui_arm;

  pc_dec_config(&c, pc_dec_ui_arm ? pc_dec_setting : 0);
  pc_dec_ui_arm = 0;
  pc_dec_apply(&c);

  //Trigger mode for this conversion, written before the sampling reset
  pc_ui_auto   = 0;
  pc_ui_forced = 0;

  if(pc_fw_auto_possible())
  {
    int fw_auto = ui && (scopesettings.triggermode == 0);

    fpga_write_cmd(0x1A);
    fpga_write_byte((scopesettings.triggermode || fw_auto) ? 0x01 : 0x00);

    if(fw_auto)
    {
      uint32 d = (scopesettings.samplerate < (sizeof(sample_rate_settings) / sizeof(uint32))) ?
                 sample_rate_settings[scopesettings.samplerate] : 0;

      pc_ui_auto    = 1;
      pc_ui_auto_t0 = timer0ticks;
      pc_ui_auto_ms = pc_auto_deadline_ms(scopesettings.samplecount, d);
    }
  }

  //Average on a 0x1536 bitstream: this UI conversion may be a hardware averaging run, which has its own deadline
  //(pc_seq_ui_service; the firmware time-out must never end a record between the records of a run)
  if(pc_seq_ui_conversion(ui, &c, pc_dec_div()))
    pc_ui_auto = 0;
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
    fpga_write_cmd(0x1A);
    fpga_write_byte(0x00);
    pc_ui_forced = 1;
  }
  else
    pc_ui_auto = 0;                                   //it triggered: the record completes by itself
}

uint32 pc_glue_decim_words(uint32 samples)
{
  return pc_dec_cur.k ? samples : samples / 2;
}

//EXT trigger in the 16-bit layout: trigger_address is the last sample written before the edge, but the data lag it by
//120 ns + K (N - 1) 2.5 ns (fpga-gen3; with the FIR 250 ns + 17.5 N ns + 1.25 K (N - 2) ns, fpga-gen4). Writing that many samples fewer as pretrigger puts the edge back at sample
//settriggerpoint of the capture (GET_CAPTURE_INFO byte 70 reports the shift).
static uint32 pc_dec_ext_shift(void)
{
  return (pc_dec_cur.k && (scopesettings.triggerchannel == 2)) ? pc_decim_ext_shift(&pc_dec_cur) : 0;
}

uint32 pc_glue_decim_pre_words(uint32 samples)
{
  uint32 shift = pc_dec_ext_shift();

  if(!pc_dec_cur.k)
    return samples / 2;

  return (samples > shift) ? samples - shift : 0;
}

uint32 pc_glue_decim_ui_start(uint32 start)
{
  if(!pc_dec_cur.k)
    return start;

  return (uint32)(pc_dec_ui_addr + PC_DEC_RING - (pc_glue_decim_pre_words(fpgasettings.settriggerpoint) % PC_DEC_RING)) % PC_DEC_RING;
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

  pc_dec_raw_n[ch] = 0;

  //0x1536 UI averaging run: the corrected codes of the mean, both passes from memory as for the 16-bit path
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

  //Into the 8-bit trace domain with the readout's full-class correction (adc_cal.h): gain16 folded into the scale, corr16
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
  //0xDE00 marks a valid word; sectors written before 0.2.30 hold whatever 1.00o5 left there (PC_DECIM_DEFAULT). Bits
  //3-4: the hidden falling-edge capture settings (SET_DECIM only); bit 5: FIR (fw 0.2.44)
  return (uint16)(0xDE00 | (pc_dec_flags & (PC_DECIM_NEG_A | PC_DECIM_NEG_B)) | (pc_dec_setting & (PC_DECIM_SET_FIR | 3)));
}

static const char *pc_dec_names[8] = { "off", "boxcar", "CIC2", "CIC3", "off", "boxcar+FIR", "CIC2+FIR", "CIC3+FIR" };

//Index into pc_dec_names: "+FIR" only where the bitstream has it
static uint32 pc_dec_name_index(void)
{
  uint32 k = pc_dec_setting & 3;

  return ((pc_dec_has() >= PC_DECIM_HAS_FIR) && (pc_dec_setting & PC_DECIM_SET_FIR) && k) ? k + 4 : k;
}

//Acquisition menu: Filter box in 1.00o5's Long memory slot, same style as the Roll mode box
static void pc_dec_draw_box(void)
{
  uint16    fg   = displaydata.fg_color;
  PFONTDATA font = displaydata.font;
  int       on   = (pc_dec_setting & 3) != 0;
  uint32    len  = strlen(pc_dec_names[pc_dec_name_index()]);

  display_set_fg_color(on ? GREEN_COLOR : LIGHTGREY_COLOR);
  display_fill_rounded_rect(ACQ_MENU_XPOS + 204, ACQ_MENU_YPOS + 163, 90, 40, 3);
  display_set_font(&font_2);
  display_set_fg_color(on ? BLACK_COLOR : WHITE_COLOR);
  display_text(ACQ_MENU_XPOS + 231, ACQ_MENU_YPOS + 168, "Filter");
  display_text(ACQ_MENU_XPOS + 249 - 3 * len, ACQ_MENU_YPOS + 182, (int8 *)pc_dec_names[pc_dec_name_index()]);
  displaydata.fg_color = fg;
  displaydata.font     = font;
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

//A tap steps off, boxcar, CIC2, CIC3; on 0x1535+ each filter also with the FIR: off, boxcar, boxcar+FIR, CIC2,
//CIC2+FIR, CIC3, CIC3+FIR
//The DEFAULT button and a settings sector that failed to load (scope_reset_config_data)
void pc_glue_settings_defaults(void)
{
  pc_dec_setting = PC_DECIM_DEFAULT;

#if PC_FORCE_CDC
  //The defaults say mass storage (USB_CH340 0); this build's USB stays CDC, also after Restore default settings (the
  //USB drive screen's exit re-initialises USB from this flag)
  USB_CH340 = 1;
#endif

  //fw 0.2.56: the Display variants back to Normal and Spectrum (X-Y and Grid are 1.00o5's own defaults)
  phosphor_set_switch(0);
  spectrum_set_view(0);

  if(pc_dec_has() && (pc_dec_setting & 3))
    pc_dec_fit_memory();
}

void pc_glue_decim_tap(void)
{
  uint32 k = pc_dec_setting & 3, fir = (pc_dec_setting & PC_DECIM_SET_FIR) != 0;

  if(pc_dec_has() < PC_DECIM_HAS_FIR)
  {
    k   = (k + 1) & 3;
    fir = 0;
  }
  else if(k && !fir)
    fir = 1;
  else
  {
    k   = (k + 1) & 3;
    fir = 0;
  }

  pc_dec_setting = (uint8)(k | (fir ? PC_DECIM_SET_FIR : 0));

  if(k)
  {
    pc_dec_fit_memory();
    scope_acquisition_memory_deep_select();
  }

  pc_dec_draw_box();
  scopesettings.display_data_done = 1;
}

uint8_t pc_hal_decim_setting(void)
{
  return (uint8_t)((pc_dec_setting & (PC_DECIM_SET_FIR | 3)) | pc_dec_flags);
}

void pc_hal_decim_set(uint8_t setting)
{
  pc_dec_setting = setting & (PC_DECIM_SET_FIR | 3);
  pc_dec_flags   = setting & PC_DECIM_FLAGS;

  if(pc_dec_setting & 3)
    pc_dec_fit_memory();

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
  return pc_dec_has() ? fpga_get_decim_state() : 0;
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
  b[n++] = pc_dec_has() ? pc_hal_decim_setting() : 0;

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

    case PC_FEAT_EXT_TRIGGER:
      return (pc_sess.var.caps & PC_CAP_EXT_TRIGGER) != 0;

    case PC_FEAT_DECIM:
      return pc_dec_has();

    default:
      //With the Filter on, the 16-bit layout holds one sample per word: at most PC_DECIM_MAX_TOTAL
      if((feature >= PC_FEAT_MEM_BASE) && (feature < PC_FEAT_MEM_BASE + 8))
        return (uint32)(3000 * (feature - PC_FEAT_MEM_BASE + 1)) <= ((pc_dec_has() && (pc_dec_setting & 3)) ? PC_DECIM_MAX_TOTAL : pc_sess.var.max_total);

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

    case PC_MASK_TRIG_CHANNEL:
      if(!pc_glue_ui_has(PC_FEAT_EXT_TRIGGER))
        pc_mask_rect(TRIGGER_MENU_XPOS + 170, TRIGGER_MENU_YPOS + 127, 37, 26);
      break;

    case PC_MASK_ACQ_FILTER:
      //1.00o5's Long memory slot holds the decimation Filter selector on 0x1534+ (empty menu background otherwise)
      if(pc_glue_ui_has(PC_FEAT_DECIM))
        pc_dec_draw_box();
      break;

    case PC_MASK_ACQ_MEMORY:
      for(i=0;i<5;i++)
        if(!pc_glue_ui_has(PC_FEAT_MEM_BASE + i))
          pc_mask_rect(ACQ_MENU_XPOS + 313, ACQ_MENU_YPOS + 31 + 36 * i, 58, 29);

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
  display_fill_rounded_rect(ACQ_MENU_XPOS + 8, ACQ_MENU_YPOS + 163, 90, 40, 3);
  display_set_font(&font_2);
  display_set_fg_color(on ? BLACK_COLOR : WHITE_COLOR);
  //fw 0.2.40 (owner): the label only, the colour shows the state, as the Average box ("Average" / "mode")
  display_text(ACQ_MENU_XPOS + 41, ACQ_MENU_YPOS + 168, "Roll");
  display_text(ACQ_MENU_XPOS + 38, ACQ_MENU_YPOS + 182, "mode");
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
  return phosphor_mode() ? PHOSPHOR_DISPLAY_MODE : (uint8_t)spectrum_mode();
}

int32_t pc_hal_spectrum_read(int ch, uint32_t first, uint32_t max_bins, uint8_t *hdr, uint8_t *levels)
{
  return spectrum_read(ch, first, max_bins, hdr, levels);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Spectrum view glue (fw 0.2.28) and phosphor view (fw 0.2.46). Since fw 0.2.51 the main menu's mode rows (Scope,
// Spectrum analyser; ui_menu.h) are the only way between the modes; phosphor is a drawing variant of the scope switched
// in the CTRL page's Display fold-out (fw 0.2.56, ctrl_page.c). The spectrum and phosphor views exclude each other.

int pc_glue_spectrum_display(void) { return spectrum_display(); }
int pc_glue_spectrum_touch(void)   { return spectrum_touch(); }
int pc_glue_autoset(void)          { return spectrum_autoset(); }
int pc_glue_autoset_running(void)  { return spectrum_auto_running(); }

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
  int          sp = pc_spectrum_on();

  if(!ui_mm_choose(row, sp, scopesettings.long_mode != 0, phosphor_switch(), &a))
    return;

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

  if(a.warn_roll)
    spectrum_note_roll_off();
}

//The right bar differs between the modes (empty slots, the CTRL page's items): redrawn whenever the spectrum view
//starts or ends, whatever started or ended it (main menu, roll mode, a viewer, the host)
static uint8 pc_bar_spectrum;

static void pc_right_bar_follow(void)
{
  DISPLAYDATA save;
  uint8       sp = (uint8)pc_spectrum_on();

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

  t = ui_scope_warning(&w);
  if(!*t)
    return;

  display_set_font(&font_2);
  display_set_fg_color(ORANGE_COLOR);
  display_text(366 - ui_text_width(&font_2, t) / 2, 69, (int8 *)t);
}

//Settings sector word 211 (fw 0.2.56): 0xC700 marker, bit 0 Grid, bits 1-2 the spectrum's Display variant. The scope's
//variants are kept where they were (X-Y: 1.00o5's word; Phosphor: word 210 bit 3).
uint16 pc_glue_ui_setting_word(void)
{
  return (uint16)(0xC700 | ((spectrum_view() & 3) << 1) | (scopesettings.gridenable ? 1 : 0));
}

static void pc_ui_setting_restore(uint16 w)
{
  if((w & 0xFFF8) != 0xC700)
    return;

  scopesettings.gridenable = w & 1;
  spectrum_set_view((w >> 1) & 3);
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

  //EXT trigger only on bitstreams that have it
  if((scopesettings.triggerchannel > 1) && !(pc_sess.var.caps & PC_CAP_EXT_TRIGGER))
    scopesettings.triggerchannel = 0;

  //Acquisition Filter (decimation, 0x1534+): kept in a spare settings word; a sector without a valid word (new
  //settings, or one written before 0.2.30) keeps PC_DECIM_DEFAULT. With it on the 16-bit layout limits the memory
  //depth.
  if((settingsworkbuffer[PC_SETTING_WORD_DECIM] & 0xFFC4) == 0xDE00)
  {
    pc_dec_setting = (uint8)(settingsworkbuffer[PC_SETTING_WORD_DECIM] & (PC_DECIM_SET_FIR | 3));
    pc_dec_flags   = (uint8)((pc_dec_flags & PC_DECIM_SWAP) | (settingsworkbuffer[PC_SETTING_WORD_DECIM] & (PC_DECIM_NEG_A | PC_DECIM_NEG_B)));
  }

  if(pc_dec_has() && (pc_dec_setting & 3))
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
  pc_session_init(&pc_sess);
  pc_seq_hw_init((pc_sess.var.caps & PC_CAP_SEQ) != 0);
  pc_ready = 1;
  pc_ui_fix_settings();

  //A restart is logged once, as event type 15 (a = exception type, 0 = jump to the entry; b = low 16 bits of the
  //exception LR, or of the LR found at _reset)
  if(pc_restart_info[0])
    pc_usb_log_restart((uint8)pc_restart_info[1], (uint16)(pc_restart_info[1] ? pc_restart_info[2] : pc_restart_info[5]));
}

void pc_glue_poll_main(void)
{
  if(pc_ready)
  {
    uint8 who = fpga_trace_who;

    fpga_trace_who = FPGA_WHO_HOST;
    pc_poll(&pc_sess, PC_CTX_MAIN);
    fpga_trace_who = who;
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

void pc_glue_ui_service(void)
{
  if(pc_ready)
    pc_ui_service(&pc_sess);

  //fw 0.2.52: settings changed since the last save, once per second
  pc_diag_service();

  //Roll mode or a viewer ends the spectrum view and the phosphor view; the right bar follows the mode
  spectrum_service();
  phosphor_service();
  pc_right_bar_follow();

  //Calibration v4 class probe (acal_live.c): only in the scope and spectrum views (the phosphor view's record rate would
  //see the gap; not while Diagnostics shows), never while a host capture is armed or triggered (the probe resets the sample system) or the host
  //holds the UI off the ring (READ_RAW hold)
  acal_live_service((pc_hal_display_mode() < PHOSPHOR_DISPLAY_MODE) && !ui_diag_active() &&
                    (!pc_ready || (!pc_sess.ui_hold && (pc_sess.acq_state != PC_ACQ_ARMED) &&
                                   (pc_sess.acq_state != PC_ACQ_TRIGGERED))));
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
  pc_dec_ui_addr = (uint16)addr;

  if(pc_ready)
  {
    pc_ui_capture_addr(&pc_sess, (uint16_t)addr);

    //A record the firmware time-out ended is untriggered, whatever 0x18 says now
    if(pc_ui_forced)
      pc_sess.ui_trig = 0;

    //An averaging run is triggered when it holds a record (0x18 is the last record's)
    pc_seq_ui_done(&pc_sess.ui_trig);
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
