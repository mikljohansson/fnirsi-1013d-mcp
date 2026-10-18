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
#include "variables.h"
#include "usb_interface.h"
#include "ccu_control.h"
#include "interrupt.h"
#include "pc_transport.h"

#include "pc_hal.h"
#include "pc_cmds.h"
#include "pc_hooks.h"

//----------------------------------------------------------------------------------------------------------------------------------

//Banner in the top bar, clear of the battery symbol at x 701..726
#define PC_BANNER_X       150
#define PC_BANNER_Y       0
#define PC_BANNER_W       540
#define PC_BANNER_H       46

//0x05 ready polls before ARM gives up (1.00o5 waits forever)
#define PC_READY_POLLS    2000000

static pc_session    pc_sess;
static uint8         pc_ready;
static uint16        pc_fpga_word;

//UI state saved on entering REMOTE (plan 6.4)
static SCOPESETTINGS pc_shadow_scope;
static FPGASETTINGS  pc_shadow_fpga;

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

  scopesettings.timeperdiv = t->timeperdiv;
  scopesettings.samplerate = t->samplerate;
  fpga_set_sample_rate(scopesettings.samplerate);
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

void pc_hal_trigger_write(const pc_hw_trigger *t, uint8_t mask)
{
  if(mask & PC_TRG_SOURCE)
  {
    //Atlan4 bitstreams compare core A only; the stock trigger core is unverified       BENCH: T10
    scopesettings.triggerchannel = t->source;
    fpga_set_trigger_channel();
  }

  if(mask & PC_TRG_EDGE)
  {
    scopesettings.triggeredge = t->edge;
    fpga_set_trigger_edge();
  }

  if(mask & PC_TRG_LEVEL)
  {
    //Raw level as given; the UI's marker derivation (fpga_set_trigger_level) is not run in REMOTE
    scopesettings.triggerlevel = t->level;
    fpga_write_cmd(0x17);
    fpga_write_byte(t->level);
  }

  if(mask & PC_TRG_MODE)
  {
    scopesettings.triggermode = t->ui_mode;
    fpga_set_trigger_mode();
  }

  //Geometry is written at ARM, inside the sample-system reset
  if(mask & PC_TRG_PRE)
    fpgasettings.settriggerpoint = t->pretrigger;

  if(mask & PC_TRG_TOTAL)
    fpgasettings.totalsamples = t->total;
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
}

//----------------------------------------------------------------------------------------------------------------------------------
// Acquisition

int pc_hal_arm(uint16_t pre_words, uint16_t total_words, uint32_t auto_timeout, int prog, int normal)
{
  uint32 n = 0;

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

void pc_hal_battery_refresh(void)
{
  battery_check_status();
}

//----------------------------------------------------------------------------------------------------------------------------------
// Ownership and UI

static void pc_draw_banner(void)
{
  display_set_screen_buffer((uint16 *)maindisplaybuffer);
  display_set_fg_color(RED_COLOR);
  display_fill_rect(PC_BANNER_X, PC_BANNER_Y, PC_BANNER_W, PC_BANNER_H);
  display_set_fg_color(WHITE_COLOR);
  display_set_font(&font_3);
  display_text(PC_BANNER_X + 110, PC_BANNER_Y + 14, "REMOTE - tap here to release");
}

void pc_hal_remote_enter(void)
{
  memcpy(&pc_shadow_scope, &scopesettings, sizeof(SCOPESETTINGS));
  memcpy(&pc_shadow_fpga, &fpgasettings, sizeof(FPGASETTINGS));

  //The UI acquisition is simply not called any more; the next ARM resets the sample system
  pc_draw_banner();
}

void pc_hal_remote_exit(int keep_settings)
{
  if(!keep_settings)
  {
    memcpy(&scopesettings, &pc_shadow_scope, sizeof(SCOPESETTINGS));
    memcpy(&fpgasettings, &pc_shadow_fpga, sizeof(FPGASETTINGS));
  }

  //Push every setting with the sequence main() uses at boot (fnirsi_1013d_scope.c)        BENCH: plan 11.3 step 3
  fpga_set_channel_enable(&scopesettings.channel1);
  fpga_set_channel_enable(&scopesettings.channel2);
  fpga_set_channel_voltperdiv(&scopesettings.channel1);
  fpga_set_channel_voltperdiv(&scopesettings.channel2);
  fpga_set_channel_coupling(&scopesettings.channel1);
  fpga_set_channel_coupling(&scopesettings.channel2);
  fpga_set_sample_rate(scopesettings.samplerate);

  if(scopesettings.long_mode)
    fpga_set_long_timebase(scopesettings.timeperdiv);
  else
    fpga_set_time_base(scopesettings.timeperdiv);

  fpga_set_trigger_channel();
  fpga_set_trigger_edge();
  fpga_set_trigger_level();
  fpga_set_trigger_mode();
  fpga_set_channel_offset(&scopesettings.channel1);
  fpga_set_channel_offset(&scopesettings.channel2);
  fpga_on_off_generator();

  //Let the relays settle as main() does before the first conversion
  timer0_delay(50);

  //Redraw and resume the UI loop with a fresh conversion
  scope_setup_main_screen();
  scopesettings.display_data_done = 1;
  scopesettings.conversion_done   = 0;
}

int pc_hal_banner_touched(void)
{
  tp_i2c_read_status();

  if(havetouch && (xtouch >= PC_BANNER_X) && (xtouch < PC_BANNER_X + PC_BANNER_W) && (ytouch < PC_BANNER_Y + PC_BANNER_H))
  {
    //Do not let the UI see this touch after the release
    tp_i2c_wait_for_touch_release();
    havetouch = 0;
    return 1;
  }

  return 0;
}

void pc_hal_show_trace(void)
{
  scope_display_trace_data();                                                        //BENCH: plan 11.3 step 3 (SHOW_TRACES cost [U])
  pc_draw_banner();
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

int pc_glue_remote(void)
{
  return pc_ready && pc_remote_active(&pc_sess);
}

void pc_glue_remote_service(void)
{
  pc_remote_service(&pc_sess);
}

void pc_glue_ui_capture(void)
{
  if(pc_ready)
    pc_ui_capture_done(&pc_sess);
}

void pc_glue_power_off(void)
{
  //The power-off ISR saves scopesettings to SD next: it must save the user's settings, not the host's (plan 6.4)
  if(pc_ready && pc_power_off_restore_needed(&pc_sess))
  {
    memcpy(&scopesettings, &pc_shadow_scope, sizeof(SCOPESETTINGS));
    memcpy(&fpgasettings, &pc_shadow_fpga, sizeof(FPGASETTINGS));
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
