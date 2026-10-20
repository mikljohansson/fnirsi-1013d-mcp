//----------------------------------------------------------------------------------------------------------------------------------
// pc_hal.h - the only interface pc_cmds.c uses to reach the scope (docs/plans/firmware-usb-protocol.md section 4).
//
// Target implementation: pc_hal_scope.c (scopesettings / fpga_control.c / display) and pc_transport.c (USB).
// Host implementation:   host_tests/mock_hal.c (FPGA ring model, fake transport, settable clock).
//
// Rules for the target implementation:
//  - functions marked [FPGA] drive the FPGA bus; pc_cmds.c only calls them from the main-loop poll context (top of
//    the UI loop, between UI passes; never from a BUSY or timer poll).
//  - functions marked [UI] draw or touch the scope settings; same restriction.
//  - all others are safe in any poll context.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_HAL_H
#define PC_HAL_H

#include <stdint.h>
#include "pc_decim.h"
#include "pc_seq.h"
#include "adc_cal.h"
#include "acal_track.h"
#include "pc_life.h"
#include "enc_heal.h"

//----------------------------------------------------------------------------------------------------------------------------------
// Front end channel state as the firmware keeps it (CHANNELSETTINGS subset)

#define PC_CH_ENABLE      0x01
#define PC_CH_RANGE       0x02
#define PC_CH_COUPLING    0x04
#define PC_CH_OFFSET      0x08
#define PC_CH_INVERT      0x10
#define PC_CH_PROBE       0x20

typedef struct
{
  uint8_t  enable;
  uint8_t  range;               //samplevoltperdiv 0..6 (6 = software zoom of hardware range 5)
  uint8_t  coupling;            //0 DC, 1 AC
  uint8_t  invert;
  uint8_t  probe;               //magnification index 0..6
  uint8_t  overload;            //ADoverload of the last readout: bit0 a sample hit 0, bits 1-7 a sample hit 255
  int16_t  dcoffset;            //UI offset added to the PWM centre
  uint16_t cal_offset[7];       //dc_calibration_offset: PWM centre per range
  int32_t  input_cal[7];        //input_calibration (2^21 scale)
  int16_t  adc_a_comp;          //adc1compensation (core A, FPGA 0x20/0x22)
  int16_t  adc_b_comp;          //adc2compensation (core B, FPGA 0x21/0x23)
  uint32_t dc_shift_center;
  int32_t  dc_shift_size;
  int32_t  dc_shift_value;
  uint32_t adc_a_rawavg;
  uint32_t adc_b_rawavg;
  uint8_t  cal2_loaded;         //sector 708 calibration data v3 block valid (in use only while bound to cal_offset)
  uint32_t divider;             //0x0D value last written (clock class of the correction)
  acal_channel cal2;            //the channel's v3 data (zeros when not loaded)
  acal_track   trk;             //1.14: calibration v4 A - B tracker of the channel (snapshot, acal_live.c)
  acal_cm      cm;              //1.14: full-class common mode from the class probe (snapshot)
} pc_hw_channel;

void     pc_hal_channel_read(int ch, pc_hw_channel *c);
void     pc_hal_channel_write(int ch, const pc_hw_channel *c, uint8_t mask);            //[UI][FPGA] masked PC_CH_* fields
uint32_t pc_hal_mul_factor_1x(uint8_t range);                                          //volt_calc_data[1x][range].mul_factor

//----------------------------------------------------------------------------------------------------------------------------------
// Timebase

typedef struct
{
  uint8_t  timeperdiv;          //UI index 0..34
  uint8_t  samplerate;          //UI sample rate table index
  uint32_t divider;             //N written to FPGA 0x0D
} pc_hw_timebase;

void     pc_hal_timebase_read(pc_hw_timebase *t);
int      pc_hal_timebase_lookup(uint8_t index, uint8_t *samplerate, uint32_t *divider);   //0 ok, -1 index out of range
void     pc_hal_timebase_write(const pc_hw_timebase *t, int raw);                      //[UI][FPGA] raw: only divider is valid
uint32_t pc_hal_divider_now(void);      //fw 0.2.87: the 0x0D value the FPGA holds (the last one anyone wrote; the UI
                                        //table's before the first): every decimation configuration is computed from it

//----------------------------------------------------------------------------------------------------------------------------------
// Trigger

#define PC_TRG_SOURCE     0x01
#define PC_TRG_EDGE       0x02
#define PC_TRG_LEVEL      0x04
#define PC_TRG_MODE       0x08
#define PC_TRG_PRE        0x10
#define PC_TRG_TOTAL      0x20

typedef struct
{
  uint8_t  source;              //0 CH1, 1 CH2
  uint8_t  edge;                //0 rising, 1 falling
  uint8_t  level;               //raw core A code
  uint8_t  ui_mode;             //scopesettings.triggermode: 0 auto, 1 single, 2 normal
  uint16_t pretrigger;          //fpgasettings.settriggerpoint (samples)
  uint16_t total;               //fpgasettings.totalsamples (samples)
} pc_hw_trigger;

void     pc_hal_trigger_read(pc_hw_trigger *t);
void     pc_hal_trigger_write(const pc_hw_trigger *t, uint8_t mask);                    //[UI][FPGA] masked PC_TRG_* fields
uint32_t pc_hal_auto_timeout_default(uint32_t total_samples);                          //what 1.00o5 writes to 0x0E

//----------------------------------------------------------------------------------------------------------------------------------
// Generator (Atlan4 DDS, FPGA 0x50-0x52)

void     pc_hal_gen_read_ui(uint8_t *enable, uint32_t *freq_hz, uint8_t *duty_pct);    //fpgasettings gen_*
void     pc_hal_gen_write(uint32_t phase_inc, uint32_t threshold, uint8_t enable);      //[UI][FPGA] duty, freq, enable; UI copy rounded

//----------------------------------------------------------------------------------------------------------------------------------
// Acquisition [FPGA]

//Reset the sample system, write the geometry (0x0B / 0x0C) before and inside the reset and 0x0E inside it, release.
//normal: trigger mode written to 0x1A (1 normal/single, 0 auto). dc: decimation filter configuration for this capture
//(written and settled before the reset, 16-bit trigger level from the UI level). sq: sequencer setting for this arm
//(pc_seq.h; written with 0x0B / 0x0C before the reset, mode 0 = a single capture). Returns 0, or -1 when 0x05 never
//became ready.
int      pc_hal_arm(uint16_t pre_words, uint16_t total_words, uint32_t auto_timeout, int normal,
                    const pc_decim_cfg *dc, const pc_seq_cfg *sq);
void     pc_hal_acq_poll(int *done, int *triggered);                                   //0x0A bit 0, 0x18 bit 0
uint16_t pc_hal_acq_finish(void);                                                      //post-done writes, returns 0x14 & 0x1FFF
void     pc_hal_force(void);                                                           //0x1A = 0
uint8_t  pc_hal_ext_status(void);                                                      //0x28: bit0 level, bit1 gen on, bit2 edge seen

//Read count bytes of one core (0..3 = FPGA 0x20..0x23) from ring word start, splitting at the ring end, one dummy read
//after every 0x1F write. dst advances by stride per byte.
void     pc_hal_read_core(uint8_t core, uint32_t start, uint32_t count, uint8_t *dst, uint32_t stride, uint32_t ring_words);

//Decimation filter (bitstream 0x1534+, pc_decim.h). The setting is the UI's acquisition "Filter" selector (saved with
//the settings): bits 1:0 requested K (0 off, 1 boxcar, 2 CIC2, 3 CIC3), PC_DECIM_SET_FIR (0x1535+), the falling-edge
//capture bits, plus PC_DECIM_SWAP (host only, not saved).
uint8_t  pc_hal_decim_setting(void);
void     pc_hal_decim_set(uint8_t setting);                                            //[UI]
//What the FPGA runs: the configuration written at the last arm (UI or host). *written = 0 before the first write.
void     pc_hal_decim_current(pc_decim_cfg *c, int *written);
uint16_t pc_hal_decim_readback(void);                                                  //[FPGA] 0x29: byte 0 << 8 | S
//16-bit samples of the last readout of a channel (decimated captures only), NULL when that readout was 8-bit
const uint16_t *pc_hal_trace16(int ch, uint32_t *count);

//Sequencer (bitstream 0x1536+, pc_seq.h) [FPGA]
void     pc_hal_seq_status(uint8_t raw[8]);                                           //0x2A, read until two reads agree
void     pc_hal_seq_stop(void);                                                        //0x5D = 1, then 0x1A = 0
void     pc_hal_seq_view(uint8_t mode57, uint8_t shift);                               //readout: 0x5B = shift, 0x57 = mode57
//count view-word bytes `byte` (0..7 = 0x2B..0x32) from accumulator word first: 0x1F, one dummy strobe, count strobes;
//dst advances by stride per byte. first + count <= 2048.
void     pc_hal_seq_read(uint8_t byte, uint32_t first, uint32_t count, uint8_t *dst, uint32_t stride);
//The UI's Average (any context, no FPGA access): state 0 off, 1 software, 2 hardware (0x1536 averaging run of 2^log2n
//records per displayed record)
void     pc_hal_ui_average(uint8_t *state, uint8_t *log2n);

//Raw bus access for DEBUG_FPGA
void     pc_hal_fpga_write_cmd(uint8_t cmd);
void     pc_hal_fpga_write_data(uint8_t data);
uint8_t  pc_hal_fpga_read_data(void);

//----------------------------------------------------------------------------------------------------------------------------------
// Processed trace (the UI's tracebuffer)

//[FPGA] Run the 1.00o5 readout (fpga_read_sample_data) for the channels in ch_mask starting at ring word start_word,
//total_samples per channel. Returns the software trigger index (scope_process_trigger) or -1.
int32_t  pc_hal_trace_build(uint32_t start_word, uint32_t total_samples, uint32_t pretrigger_samples, uint8_t ch_mask);

//Current UI trace buffer of a channel and the UI's own capture geometry (READ_TRACE of a UI capture)
const uint8_t *pc_hal_trace(int ch);
void     pc_hal_trace_ui_info(uint32_t *sample_count, uint32_t *pretrigger, int32_t *sw_trigger_index);
uint32_t pc_hal_trace_capacity(void);                                                  //bytes per channel buffer

//----------------------------------------------------------------------------------------------------------------------------------
// System

uint32_t pc_hal_ms(void);                                                              //1 ms ticks, wraps
void     pc_hal_delay_ms(uint32_t ms);
uint16_t pc_hal_fpga_version(void);                                                    //cached at init, no bus access
void     pc_hal_battery(uint8_t *level, uint8_t *charging, uint8_t *keyadc_raw);

//----------------------------------------------------------------------------------------------------------------------------------
// Shared control with the UI (protocol 1.3, docs/plans/collaborative-mode.md). The UI loop always runs; host SET_*
// write the live UI settings through the UI's own apply functions and redraw what they changed.

typedef struct
{
  uint8_t  run;                 //scopesettings.runstate
  uint8_t  ui_armed;            //a UI conversion is in flight (display_data_done == 0)
  uint8_t  long_mode;           //UI long (roll) time base: its own acquisition, no ring capture
} pc_hw_ui;

void     pc_hal_ui_read(pc_hw_ui *u);
void     pc_hal_ui_set_run(int run);             //[UI] as the RUN/STOP button; the next UI pass arms afresh
uint32_t pc_hal_settings_fingerprint(void);      //hash of the capture-relevant UI settings (protocol section 14)
void     pc_hal_indicator(int state);            //[UI] MCP chip in the top bar: 0 off, 1 on, 2 pulse
//1.23 (fw 0.2.78): what keeps the UI from yielding to a host request now: 0 nothing (the modal loops leave by their
//cancel paths), else the E_BUSY detail PC_BUSY_CAL / PC_BUSY_SAVE / PC_BUSY_USB_DRIVE. A memory read: any context
uint8_t  pc_hal_ui_hold(void);
//fw 0.2.96: a screen the main loop shows over the view (Diagnostics) is left by its Back path before a request that is
//neither F_BUSY nor read-only (F_MODAL) runs, so the request's redraws reach the view, not that screen. 1 when a
//screen was left, 0 when none showed. [UI] MAIN only
int      pc_hal_ui_leave_screen(void);

//----------------------------------------------------------------------------------------------------------------------------------
// Transport (pc_transport.c on the target). Safe in every context.

#define PC_LINK_DTR_UP    0x01
#define PC_LINK_DTR_DOWN  0x02
#define PC_LINK_RESET     0x04      //bus reset, suspend or disconnect

uint8_t *pc_hal_tx_buffer(uint32_t *capacity);   //staging buffer, 4-byte aligned
int      pc_hal_tx_busy(void);                   //1 while a submitted frame is still being copied to the FIFO
void     pc_hal_tx_submit(uint32_t len);         //send len bytes of the staging buffer (+ ZLP when needed)
uint32_t pc_hal_rx_read(uint8_t *dst, uint32_t max);
uint32_t pc_hal_link_take_events(void);          //PC_LINK_* since the last call
int      pc_hal_dtr(void);
int      pc_hal_tx_pump(void);                   //load the next packet(s) if the FIFO has room; returns pc_hal_tx_busy()
uint32_t pc_hal_tx_idle_ms(void);                //ms since the last packet of the frame in flight was loaded (0 if idle)
void     pc_hal_tx_abort(void);                  //drop the frame in flight and flush the endpoint FIFO

//Transport counters for GET_LINK_STATS, in this order
#define PC_TS_RX_PACKETS  0     //EP2 OUT packets taken from the FIFO
#define PC_TS_RX_BYTES    1
#define PC_TS_RX_STALLS   2     //ring full: packet left in the FIFO (NAK back-pressure)
#define PC_TS_RX_SWEPT    3     //packets taken by the poll sweep because their interrupt never came
#define PC_TS_TX_PACKETS  4     //EP2 IN packets handed to the controller (ZLPs included)
#define PC_TS_TX_ZLPS     5
#define PC_TS_TX_KICKED   6     //of those, loaded by the poll pump rather than the TX interrupt
#define PC_TS_TX_FLUSHES  7     //frames dropped in flight (DTR, stall watchdog, #RESET, link reset)
#define PC_TS_LINK_RESETS 8     //bus reset / suspend / disconnect
#define PC_TS_DTR_UP      9
#define PC_TS_DTR_DOWN    10
#define PC_TSTAT_COUNT    11
void     pc_hal_link_stats(uint32_t *v);         //PC_TSTAT_COUNT values

//USB device events (fw 0.2.5): bus resets, suspends, resumes, disconnects, EP0 setups, malformed EP0 setups
#define PC_USBEV_COUNT    6
void     pc_hal_usb_events(uint32_t *v);

//RX back-pressure branches (fw 0.2.7): ISR stalls, releases, re-stalls in a release, empty releases, max RXCOUNT,
//RXCOUNT > 512
#define PC_RXDBG_COUNT    6
void     pc_hal_rx_debug(uint32_t *v);

//fw 0.2.12: the event log (8 bytes per entry: u32 ms, u8 type, u8 a, u16 b; oldest first), returns the number of
//entries, *total = events logged since boot
uint32_t pc_hal_boot_splash_ms(void);
//fw 0.2.19 restart record: restarts since the last cold boot / ENTER_FEL, exception type (0 = jump to the image entry,
//1 undefined, 2 SWI, 3 prefetch abort, 4 data abort, 5 reserved, 6 FIQ), exception LR, its SPSR, its SP, then LR, SP and
//CPSR found at _reset
#define PC_RESTART_COUNT  8
void     pc_hal_restart_info(uint32_t *v);           //fw 0.2.17: ms spent on the boot splash step + 1 (0 = not run)
//fw 0.2.48: the restart's fault registers, GET_LINK_STATS 58-59: fault status (DFSR for a data abort, IFSR for a
//prefetch abort, else 0), fault address (FAR, data abort only, else 0)
#define PC_FAULT_COUNT    2
void     pc_hal_fault_info(uint32_t *v);
//fw 0.2.49: USB suspend watch (pc_usbw.h), GET_LINK_STATS 60-61: resumes inferred without a RESUME interrupt, soft
//reconnects
#define PC_USBW_COUNT     2
void     pc_hal_usb_watch(uint32_t *v);
//fw 0.2.61: full-rate capture check (enc_heal.h), GET_LINK_STATS 62-63: captures found bad, of those recovered by a
//re-arm (fw 0.2.62: every full-rate capture; pc_hal_enc_state)
#define PC_ENCH_COUNT     2
//fw 0.2.72: FPGA writes that set the capture state (fpga_bus.h), GET_LINK_STATS 64-66: 0x0D rate writes, 0x53 / 0x56
//filter writes (fw 0.2.87: 0x54 / 0x55 too), 0x01 = 1 sampling resets; 67: full-rate captures on 0x1538+ judged by their data because such a write
//came after their sampling reset (pc_hw_enc.replaced)
#define PC_FPGAW_COUNT    3
void     pc_hal_fpga_writes(uint32_t *v);                  //PC_FPGAW_COUNT values, any context
//Event type 22 (the writes one by one): set 0 = off (default), 1 = on, -1 = unchanged; returns the level now
uint8_t  pc_hal_fpga_trace(int set);
uint32_t pc_hal_event_log(uint8_t *dst, uint32_t max, uint32_t *total);
int32_t  pc_hal_event_snapshot(uint8_t *dst, uint32_t *reset_ms, int rearm);   //-1 = none frozen; rearm clears it
uint32_t pc_hal_event_bus(uint8_t *dst, uint32_t *total);      //fw 0.2.24: bus-level ring (<= 16 entries), *total since boot

//----------------------------------------------------------------------------------------------------------------------------------
// Diagnostics readers (fw 0.2.52, protocol 1.16, docs/protocol.md section 14 row 55). Any context unless marked.

//USB link state now (pc_transport.c)
#define PC_USB_OFF          0     //switched off by other code (mass storage, ENTER_FEL)
#define PC_USB_RECONNECTING 1     //suspend watch soft disconnect in progress
#define PC_USB_UNCONFIGURED 2     //pull-up on, no SET_CONFIGURATION since the last bus reset / boot ("disconnected")
#define PC_USB_CONFIGURED   3
#define PC_USB_SUSPENDED    4     //configured, bus idle (host asleep, idle port, or cable pulled: VBUS is forced valid)
#define PC_USBF_HIGH_SPEED  0x01
#define PC_USBF_CONFIGURED  0x02
#define PC_USBF_SUSPENDED   0x04
#define PC_USBF_DTR         0x08
#define PC_USBF_SOFTCONN    0x10

typedef struct
{
  uint8_t  state;               //PC_USB_*
  uint8_t  flags;               //PC_USBF_*
  uint8_t  power;               //POWER register
  uint8_t  faddr;               //FADDR
  uint32_t suspended_ms;        //in the current suspend, 0 = not suspended
  uint32_t since_config_ms;     //since the last SET_CONFIGURATION, 0xFFFFFFFF = none since boot
} pc_hw_usb;

void     pc_hal_usb_state(pc_hw_usb *u);

uint32_t pc_hal_cp15_control(void);                     //CP15 c1 control register now

//Unit life, settings save state and the stored restart record (pc_diag.c on the target, pc_life.h)
typedef struct
{
  uint32_t power_on;            //0 = unknown
  uint32_t minutes;             //operating minutes, all power-ons, now
  uint8_t  life_flags;          //GET_HEALTH byte 32
  uint8_t  last_end;            //PC_LIFE_END_*
  uint8_t  settings_flags;      //bit 0 changed since load / last save, bit 1 last in-session save failed
  uint8_t  saves;               //in-session saves since boot (capped 255)
  int32_t  last_save;           //0 ok, < 0 driver error, 1 none since boot
  uint32_t dirty_age_ms;        //since bit 0 of settings_flags was evaluated
  pc_life_restart rst;          //stored restart record (rst.valid)
} pc_hw_life;

#define PC_LIFEF_FOUND       0x01  //a life record was on the card at boot
#define PC_LIFEF_BOOT_WRITE  0x02  //the boot write of sector 709 succeeded
#define PC_LIFEF_BOOT_FAIL   0x04  //it failed
#define PC_LIFEF_VALID       0x08  //sector 709 was valid at boot
#define PC_SETF_DIRTY        0x01
#define PC_SETF_SAVE_FAILED  0x02

void     pc_hal_life(pc_hw_life *l);
void     pc_hal_life_clear_restart(void);              //the next settings save writes the record cleared
void     pc_hal_restart_test(void);                    //PC_DEBUG builds: jump to the image entry (a type 0 restart)

//Full-rate capture check (fw 0.2.62, enc_heal.h; GET_HEALTH 88..103, protocol 1.19 row 58). UI, calibration and probe
//conversions are checked on the target (enc_heal_hw.c), host ARM captures by the session (pc_acq_service); both count here
typedef struct
{
  uint8_t  mode;            //EH_MODE_FLAG, or EH_MODE_DATA when the boot 0x33 reported an unknown encode convention
  uint8_t  last33;          //last FPGA 0x33 byte read (PC_ENCF_READ)
  uint8_t  flags;           //PC_ENCF_*
  uint8_t  boot33;          //0x33 at boot
  uint32_t bad;             //captures found bad (GET_LINK_STATS 62)
  uint32_t recovered;       //of those, recovered by a re-arm (63)
  uint32_t checked;         //full-rate captures checked
  uint32_t replaced;        //fw 0.2.72: of those, on a FLAG bitstream judged by the data (eh_capture_mode; GET_LINK_STATS 67)
} pc_hw_enc;

#define PC_ENCF_READ         0x01  //last33 holds a byte read after a capture
#define PC_ENCF_CONV_BAD     0x02  //boot: 0x33 bits 3:2 were not 01 (encode convention unknown)
#define PC_ENCF_GARBLED      0x04  //the last checked capture was accepted bad after EH_MAX_TRIES re-arms
                                   //(0x08 was PC_ENCF_ORDER, AL3 0x1537's core order, up to fw 0.2.102)

void     pc_hal_enc_state(pc_hw_enc *e);                     //any context
uint8_t  pc_hal_enc_status(void);                            //[FPGA] read 0x33, remembered as last33
int      pc_hal_enc_written(void);                           //a 0x0D / 0x53 / 0x56 write since the last 0x01 = 1
void     pc_hal_enc_note(uint8_t why, const eh_note *n);     //count a check, log event 21 when n->done

//fw 0.2.79 (protocol 1.24): FPGA 0x153A capture stages (cap_stage.h, cap_hw.c): GET_HEALTH 104..123, GET_CAL 356..363
typedef struct
{
  uint16_t value;           //0x63 in effect (written at boot and by the wizard's training); 0 without the stages
  uint8_t  flags;           //PC_CAPF_*
  uint8_t  flagged;         //cores flagged by the last read (bit 0 CH1 A, 1 CH1 B, 2 CH2 A, 3 CH2 B; cs_flagged)
  uint16_t cnt[4];          //the last read 0x36 outlier counts, CH1 A, CH1 B, CH2 A, CH2 B
  uint32_t reads;           //full-rate captures whose counts were read
  uint32_t flagged_n;       //of those with a flagged core
  uint8_t  trailer;         //CS_T_* of the stored value (sector 708 bytes 488..503) as loaded or last saved
  uint16_t stored;          //the stored 0x63 value (0 unless the trailer is valid)
  uint16_t stored_fpga;     //the FPGA version word it was trained on
} pc_hw_cap;

#define PC_CAPF_HAS          0x01  //the bitstream has the capture stages (0x153A+)
#define PC_CAPF_TRAINED      0x02  //the value in effect is a trained one for this bitstream (else the power-on 0x08E4)
#define PC_CAPF_READ         0x04  //counts were read after a full-rate capture since boot
#define PC_CAPF_FLAGGED      0x08  //the last read flagged a core
#define PC_CAPF_OTHER        0x10  //a stored value exists for another bitstream (not applied: recalibrate)
#define PC_CAPF_REPEAT       0x20  //fw 0.2.81: a core was flagged in 3 captures within a minute since the last saved
                                   //training (cap_stage.h cs_watch): recalibrate

void     pc_hal_cap_state(pc_hw_cap *c);                     //any context
void     pc_hal_cap_check(void);                             //[FPGA] after a full-rate host capture: read 0x36

//CAL_PROBE (fw 0.2.67, MAIN, UI stopped): the calibration wizard's zero measurement of one channel and hardware range
//(cal_wiz_ui.c), nothing stored; settle extra data-port reads per sample (fpga_read_settle), prestrobe a write strobe
//before each sample strobe (fpga_read_prestrobe), both 0 = the wizard's readout; mode 0 one range, 1 the whole zero
//step in the wizard's screen context (range ignored, the response has ch's last measured range); writes
//ACAL_PROBE_BYTES (adc_cal.h) and returns that length, -1 for a bad argument
int32_t  pc_hal_cal_probe(uint8_t ch, uint8_t range, uint8_t settle, uint8_t prestrobe, uint8_t mode, uint8_t *dst);

//Calibration provenance (sector 708 bytes 432..487): ACAL_PROV_* state and the stored bytes (zeros when none)
int      pc_hal_cal_prov(uint8_t *raw);

//Report file (MAIN): write text to reports/report-PPPPP-NN.txt; returns 0 or the FatFs FRESULT (> 0), name NUL-terminated
int      pc_hal_report_write(const char *text, uint32_t len, uint32_t power_on, char *name, uint32_t name_max);

//----------------------------------------------------------------------------------------------------------------------------------
// SD card (MAIN context only: the driver is not reentrant). Return 0 or the negative driver error.

uint32_t pc_hal_sd_sectors(void);                                                      //card size in 512-byte sectors, 0 = no card
int      pc_hal_sd_read(uint32_t sector, uint32_t count, uint8_t *dst);               //dst 4-byte aligned
int      pc_hal_sd_write(uint32_t sector, uint32_t count, const uint8_t *src);        //power-key interrupt held off meanwhile
uint8_t *pc_hal_sd_stage(uint32_t *capacity);                                          //write staging buffer, 4-byte aligned

//----------------------------------------------------------------------------------------------------------------------------------
// FEL [UI]: wait until the last response left, detach USB, quiesce IRQs, jump to the boot ROM FEL entry. No return on
// the target.
void     pc_hal_enter_fel(void);

//----------------------------------------------------------------------------------------------------------------------------------
// Spectrum analyser (MAIN context). fw 0.2.27: FFT_BENCH times the FFT on the CH1 trace buffer, reps runs per size;
// writes the response body (docs/protocol.md section 14 row 44) and returns its length (<= PC_FFT_BENCH_MAX).
#define PC_FFT_BENCH_MAX  (8 + 8 * 24)
uint32_t pc_hal_fft_bench(uint8_t *dst, uint32_t reps);
//fw 0.2.28: the display mode (GET_STATUS byte 43: 0 scope, 1 spectrum; any context) and the displayed spectrum of
//channel ch (MAIN): 32-byte header + up to max_bins i16 centi-dBV from bin first; returns the bins written, -1 = none
uint8_t  pc_hal_display_mode(void);
#define PC_SPECTRUM_HDR   32
int32_t  pc_hal_spectrum_read(int ch, uint32_t first, uint32_t max_bins, uint8_t *hdr, uint8_t *levels);

//----------------------------------------------------------------------------------------------------------------------------------
// Screen (READ_SCREEN, fw 0.2.70, cap SCREENSHOT). MAIN context. The framebuffer the panel shows (maindisplaybuffer, the only
// buffer DEBE layer 0 scans): RGB565 little-endian, PC_SCREEN_WIDTH pixels per row, no row padding. Copies rows
// row .. row + nrows - 1 to dst (the caller checks the range).
#define PC_SCREEN_WIDTH   800
#define PC_SCREEN_HEIGHT  480
void     pc_hal_screen_rows(uint32_t row, uint32_t nrows, uint8_t *dst);

//----------------------------------------------------------------------------------------------------------------------------------
// Bode plot (fw 0.2.66, protocol 1.20, docs/protocol.md section 14 row 60; pc_bode.c / bode_view.c). MAIN context.
// BODE_CTRL: req is the zero-extended 16-byte request; returns a PC status (PC_E_BAD_ARG / PC_E_STATE with *detail) and
// fills the 40-byte response. READ_BODE: the 32-byte header and up to max_rows 20-byte rows from row first; returns bytes.
#define PC_BODE_HDR       32
#define PC_BODE_ROW       20
#define PC_BODE_CTRL_RESP 58         //row 60: 40, fw 0.2.91 48, 0.2.93 50 (planned switches), 0.2.94 58 (AUTO SET CH2)
int      pc_hal_bode_ctrl(const uint8_t *req, uint8_t *resp, uint16_t *detail);
uint32_t pc_hal_bode_read(uint32_t first, uint32_t max_rows, uint8_t *resp);

#endif /* PC_HAL_H */
