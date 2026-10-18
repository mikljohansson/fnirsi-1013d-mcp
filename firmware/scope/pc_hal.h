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
#include "adc_cal.h"

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
  uint8_t  cal2_loaded;         //sector 708 calibration data v2 block valid (in use only while bound to cal_offset)
  uint32_t divider;             //0x0D value last written (clock class of the zero correction)
  acal_channel cal2;            //the channel's v2 data (zeros when not loaded)
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

//Reset the sample system, write the geometry while in reset (prog != 0: 0x0B/0x0C; always 0x0E), release.
//normal: trigger mode written to 0x1A (1 normal/single, 0 auto). dc: decimation filter configuration for this capture
//(bitstream 0x1534+, written and settled before the reset, 16-bit trigger level from the UI level), NULL on bitstreams
//without the filter (nothing written). Returns 0, or -1 when 0x05 never became ready.
int      pc_hal_arm(uint16_t pre_words, uint16_t total_words, uint32_t auto_timeout, int prog, int normal,
                    const pc_decim_cfg *dc);
void     pc_hal_acq_poll(int *done, int *triggered);                                   //0x0A bit 0, 0x18 bit 0
uint16_t pc_hal_acq_finish(void);                                                      //post-done writes, returns 0x14 & 0x1FFF
void     pc_hal_force(void);                                                           //0x1A = 0
uint8_t  pc_hal_ext_status(void);                                                      //0x28 (0x1533+): bit0 level, bit1 gen on, bit2 edge seen

//Read count bytes of one core (0..3 = FPGA 0x20..0x23) from ring word start, splitting at the ring end, one dummy read
//after every 0x1F write, read_lag added to the address written. dst advances by stride per byte.
void     pc_hal_read_core(uint8_t core, uint32_t start, uint32_t count, uint8_t *dst, uint32_t stride,
                          uint32_t ring_words, uint8_t read_lag);

//Decimation filter (bitstream 0x1534+, pc_decim.h). The setting is the UI's acquisition "Filter" selector (saved with
//the settings): bits 1:0 requested K (0 off, 1 boxcar, 2 CIC2, 3 CIC3), plus PC_DECIM_SWAP (host only, not saved).
uint8_t  pc_hal_decim_setting(void);
void     pc_hal_decim_set(uint8_t setting);                                            //[UI]
//What the FPGA runs: the configuration written at the last arm (UI or host). *written = 0 before the first write.
void     pc_hal_decim_current(pc_decim_cfg *c, int *written);
uint16_t pc_hal_decim_readback(void);                                                  //[FPGA] 0x29: byte 0 << 8 | S
//16-bit samples of the last readout of a channel (decimated captures only), NULL when that readout was 8-bit
const uint16_t *pc_hal_trace16(int ch, uint32_t *count);

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
uint32_t pc_hal_event_log(uint8_t *dst, uint32_t max, uint32_t *total);
int32_t  pc_hal_event_snapshot(uint8_t *dst, uint32_t *reset_ms, int rearm);   //-1 = none frozen; rearm clears it
uint32_t pc_hal_event_bus(uint8_t *dst, uint32_t *total);      //fw 0.2.24: bus-level ring (<= 16 entries), *total since boot

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

#endif /* PC_HAL_H */
