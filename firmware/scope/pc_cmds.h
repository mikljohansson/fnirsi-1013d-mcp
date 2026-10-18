//----------------------------------------------------------------------------------------------------------------------------------
// pc_cmds.h - PC protocol session: dispatcher, command handlers, LOCAL/REMOTE ownership and the remote acquisition state
// machine (docs/protocol.md sections 5, 6, 8). Hardware is reached only through pc_hal.h, so this builds on the host.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_CMDS_H
#define PC_CMDS_H

#include <stdint.h>
#include "pc_protocol.h"

//----------------------------------------------------------------------------------------------------------------------------------
// Build identity (overridden from the Makefile)

#ifndef PC_FW_VERSION
#define PC_FW_VERSION     "0.1.4"
#endif

#ifndef PC_FW_BUILD
#define PC_FW_BUILD       0x00000000u
#endif

#ifndef PC_SERIAL
#define PC_SERIAL         "1013D-0001"
#endif

//1 enables DEBUG_FPGA and the not yet bench-verified FORCE (caps DEBUG_RAW, FORCE_TRIGGER). Bench builds only.
#ifndef PC_DEBUG
#define PC_DEBUG          0
#endif

//----------------------------------------------------------------------------------------------------------------------------------
// Poll contexts (plan section 5.2)

#define PC_CTX_MAIN       0     //top of the main loop: may touch the FPGA while REMOTE
#define PC_CTX_BUSY       1     //inside a UI busy loop: answers PING/GET_INFO/GET_STATUS/GET_BATTERY only, no FPGA
#define PC_CTX_ISR        2     //1 ms timer interrupt: same safe set and the TX pump, never E_BUSY, never the UI/FPGA

//Time without a MAIN poll after which the UI counts as modal (LOCAL_BUSY): other requests then get E_BUSY
#define PC_MODAL_MS       250

//Ownership
#define PC_LOCAL          0
#define PC_REMOTE         1

//Acquisition states (P-6.2)
#define PC_ACQ_IDLE       0
#define PC_ACQ_ARMED      1
#define PC_ACQ_TRIGGERED  2
#define PC_ACQ_DONE       3
#define PC_ACQ_ABORTED    4

//Exit reasons (P-6.1)
#define PC_EXIT_NONE      0
#define PC_EXIT_RELEASE   1     //SET_REMOTE release or "#LOCAL"
#define PC_EXIT_TAP       2
#define PC_EXIT_WATCHDOG  3
#define PC_EXIT_DTR       4
#define PC_EXIT_USB       5

//SET_REMOTE flags
#define PC_RF_KEEP_SETTINGS 0x01
#define PC_RF_SHOW_TRACES   0x02
#define PC_RF_IGNORE_DTR    0x04

//Responses cached for duplicate requests (P-4.3)
#define PC_CACHE_PAYLOAD  256

//A frame in flight that makes no progress for this long is dropped (host gone or stuck), so the device never stays
//blocked on one response. Same as the host's write timeout (P-3.2).
#define PC_TX_STALL_MS    2000

//ENTER_FEL magic, 'FEL_' little-endian (P-8.8)
#define PC_FEL_MAGIC      0x4C45465Fu

//Relay settling time after a range or coupling change (P-8.3)
#define PC_SETTLE_MS      50

//READ_RAW / READ_TRACE interleave order: 0 = core B before core A in each word (1.00o5 convention, P-7.3)
#ifndef PC_INTERLEAVE_ORDER
#define PC_INTERLEAVE_ORDER 0
#endif

//----------------------------------------------------------------------------------------------------------------------------------

typedef struct
{
  uint16_t version;             //raw word from FPGA command 0x06
  uint8_t  variant;             //P-5.1: 0 unknown, 1 stock, 2 AL3, 3 EF2, 4 EP4
  uint8_t  read_lag;
  uint16_t ring_words;
  uint16_t max_total;
  uint32_t caps;
} pc_variant;

typedef struct
{
  pc_parser  parser;
  int        pending_ev;        //parser event waiting for dispatch (TX busy, or needs the MAIN context)

  uint8_t    rx_buf[64];        //bytes taken from the transport but not yet parsed
  uint32_t   rx_len;
  uint32_t   rx_pos;

  uint8_t   *tx;
  uint32_t   tx_cap;

  pc_variant var;

  //Duplicate cache
  uint8_t    cache_valid;
  uint16_t   cache_cmd;
  uint16_t   cache_seq;
  uint32_t   cache_len;
  uint8_t    cache[PC_FRAME_SIZE(PC_CACHE_PAYLOAD)] __attribute__((aligned(4)));

  //Ownership
  uint8_t    control;
  uint8_t    remote_flags;
  uint8_t    exit_reason;
  uint8_t    ui_busy;
  uint8_t    in_poll;
  uint16_t   watchdog_s;
  uint32_t   last_frame_ms;
  uint32_t   last_main_ms;
  uint32_t   last_touch_ms;
  uint32_t   last_battery_ms;

  //Remote acquisition
  uint8_t    acq_state;
  uint8_t    fpga_triggered;
  uint8_t    fpga_done;
  uint8_t    auto_timeout_hit;
  uint8_t    armed_once;
  uint8_t    shown;             //SHOW_TRACES: capture already drawn
  uint32_t   capture_id;
  uint32_t   arm_ms;
  uint16_t   trigger_addr;
  uint16_t   capture_start;
  uint16_t   cap_pre_words;     //geometry latched at ARM
  uint16_t   cap_total_words;
  uint32_t   cap_divider;
  uint32_t   settle_until;
  uint8_t    settle_pending;
  uint32_t   auto_timeout_cfg;  //0 = firmware default

  //Processed trace built from the ring for this capture
  uint32_t   trace_capture_id;
  uint8_t    trace_mask;
  int32_t    trace_sw_index;

  //Raw divider set by SET_TIMEBASE mode 1 (no UI index)
  uint8_t    tb_raw;
  uint32_t   tb_divider;

  //Generator values set by SET_GEN (raw DDS words)
  uint8_t    gen_valid;
  uint8_t    gen_enable;
  uint32_t   gen_phase;
  uint32_t   gen_thr;

  //Deferred request (WAIT)
  uint8_t    def_active;
  uint8_t    def_ready;         //result decided, response waits for the TX buffer
  uint16_t   def_cmd;
  uint16_t   def_seq;
  uint16_t   def_status;
  uint32_t   def_deadline;

  //Request payload copy, zero extended: shorter payloads read missing trailing fields as zero (P-5)
  uint8_t    req[PC_MAX_REQUEST_PAYLOAD + 8] __attribute__((aligned(4)));
  uint16_t   err_detail;
  const char *err_text;

  //Counters
  uint32_t   responses;
  uint32_t   duplicates;
  uint32_t   tx_aborts;         //frames dropped by the TX stall watchdog or #RESET

  //Main-loop pass time (interval between MAIN polls), reported in GET_STATUS
  uint16_t   pass_ms;
  uint16_t   pass_max_ms;       //maximum since the previous GET_STATUS

  uint8_t    fel_pending;       //ENTER_FEL answered: jump once the response has left

  //Dispatch counters (GET_LINK_STATS)
  uint32_t   main_polls;
  uint32_t   disp_main;         //events handled per poll context
  uint32_t   disp_busy;
  uint32_t   disp_isr;
  uint32_t   held;              //polls that left a request queued for the main loop
  uint32_t   isr_skipped;       //timer-tick polls skipped because another poll was running
} pc_session;

//----------------------------------------------------------------------------------------------------------------------------------

void     pc_session_init(pc_session *s);                 //reads the FPGA version through the HAL cache
void     pc_poll(pc_session *s, int ctx);
int      pc_remote_active(const pc_session *s);
void     pc_remote_service(pc_session *s);                //MAIN loop while REMOTE: banner tap, battery, trace display
void     pc_ui_capture_done(pc_session *s);               //the UI finished a capture (LOCAL capture_id)
int      pc_power_off_restore_needed(const pc_session *s);//power-off ISR: 1 when the UI snapshot must be restored first

//Exposed for tests
void     pc_variant_from_version(uint16_t version, pc_variant *v);
uint32_t pc_gen_phase_from_mhz(uint64_t freq_mhz);
uint64_t pc_gen_mhz_from_phase(uint32_t phase_inc);
uint32_t pc_gen_thr_from_bp(uint32_t duty_bp);
uint16_t pc_gen_bp_from_thr(uint32_t threshold);
uint32_t pc_ring_mod(int64_t addr, uint32_t ring_words);
void     pc_test_pattern(uint8_t *dst, uint32_t length, uint8_t pattern);

#endif /* PC_CMDS_H */
