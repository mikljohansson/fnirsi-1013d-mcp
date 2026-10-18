//----------------------------------------------------------------------------------------------------------------------------------
// pc_cmds.h - PC protocol session: dispatcher, command handlers, shared control with the UI (protocol 1.3,
// docs/plans/collaborative-mode.md) and the host single-capture state machine (docs/protocol.md sections 5, 6, 8, 14).
// Hardware is reached only through pc_hal.h, so this builds on the host.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_CMDS_H
#define PC_CMDS_H

#include <stdint.h>
#include "pc_protocol.h"
#include "pc_hal.h"

//----------------------------------------------------------------------------------------------------------------------------------
// Build identity (overridden from the Makefile)

#ifndef PC_FW_VERSION
#if PC_FIFO_MAP == 1
#define PC_FW_VERSION     "0.2.13-m1"   //alternative USB FIFO layout (pc_transport.h)
#else
#define PC_FW_VERSION     "0.2.13"
#endif
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

#define PC_CTX_MAIN       0     //top of the main loop, between UI passes: may touch the FPGA and the UI
#define PC_CTX_BUSY       1     //inside a UI busy loop: answers PING/GET_INFO/GET_STATUS/GET_BATTERY only, no FPGA
#define PC_CTX_ISR        2     //1 ms timer interrupt: same safe set and the TX pump, never E_BUSY, never the UI/FPGA

//Time without a MAIN poll after which the UI counts as modal (LOCAL_BUSY): other requests then get E_BUSY
#define PC_MODAL_MS       250

//GET_STATUS control byte. 1.3 has one behaviour, shared control (1.2 used 0 LOCAL, 1 REMOTE)
#define PC_SHARED         2

//GET_STATUS ui_busy byte
#define PC_UI_IDLE        0
#define PC_UI_MODAL       1     //a UI menu is open (no main-loop pass for PC_MODAL_MS)
#define PC_UI_STARTING    2     //USB is up but the main loop has not completed its first pass

//run_state (GET_STATUS offset 40)
#define PC_RUN_STOP       0
#define PC_RUN_RUN        1
#define PC_RUN_SINGLE     2     //a single capture (host ARM or UI single) is armed and waiting

//Capture origin (GET_STATUS offset 41, GET_CAPTURE_INFO, READ_TRACE byte 29)
#define PC_ORIGIN_HOST    0
#define PC_ORIGIN_UI_RUN  1
#define PC_ORIGIN_UI_SINGLE 2

//capture_settings_seq of a capture whose settings changed while it ran; settings_seq never takes this value
#define PC_SEQ_INVALID    0xFFFFFFFFu

//READ_RAW of a UI capture in RUN waits at most this long for the capture in flight
#define PC_HOLD_MS        1000

//MCP indicator: steady while DTR is up or a frame came within this time (owner: no pulse); redrawn this often, since
//UI top-bar redraws paint over it
#define PC_HOST_ACTIVE_MS 2000
#define PC_CHIP_REDRAW_MS 100

//Acquisition states (P-6.2)
#define PC_ACQ_IDLE       0
#define PC_ACQ_ARMED      1
#define PC_ACQ_TRIGGERED  2
#define PC_ACQ_DONE       3
#define PC_ACQ_ABORTED    4

//Responses cached for duplicate requests (P-4.3)
#define PC_CACHE_PAYLOAD  256

//A frame in flight that makes no progress for this long is dropped (host gone or stuck), so the device never stays
//blocked on one response. Same as the host's write timeout (P-3.2).
#define PC_TX_STALL_MS    2000

//ENTER_FEL magic, 'FEL_' little-endian (P-8.8)
#define PC_FEL_MAGIC      0x4C45465Fu

//SD sector commands (protocol 1.3, section 14 row 40)
#define PC_SD_WRITE_MAGIC 0x52574453u   //'SDWR' little-endian, required by SD_WRITE_SECTORS
#define PC_SD_MAX_COUNT   128           //sectors per SD_READ_SECTORS / SD_WRITE_SECTORS (64 KiB)
#define PC_SD_FIRST       16            //writes never below the boot area (MBR at 0)
#define PC_SD_BOOT_END    80            //16..79 bootloader: needs SD_WF_BOOT
#define PC_SD_CFG_FIRST   708           //708 calibration, 709 settings, 710 display/boot byte: need SD_WF_CONFIG
#define PC_SD_CFG_LAST    710
#define PC_SD_LIMIT       2048          //never at or above, whatever the partition table says
#define PC_SD_VERIFY_MAX  2048          //eGON images longer than this many sectors are not checksummed
#define PC_SD_WF_CONFIG   0x01
#define PC_SD_WF_BOOT     0x02

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

//Settings a capture was taken with (GET_CAPTURE_INFO)
typedef struct
{
  uint32_t   capture_id;
  uint32_t   seq;               //settings_seq at arm, PC_SEQ_INVALID when it changed before DONE
  uint8_t    origin;
  uint8_t    flags;             //bit0 triggered, bit1 auto timeout, bit2/3 overload CH1/CH2, bit4 settings changed
  uint16_t   trigger_addr;
  uint16_t   capture_start;
  uint8_t    timeperdiv;        //0xFF: raw divider
  uint8_t    source, edge, level, mode;
  uint32_t   divider;
  uint32_t   pretrigger;        //samples
  uint32_t   total;
  pc_hw_channel ch[2];
} pc_capture;

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

  //Contexts and link activity
  uint8_t    ui_busy;
  uint8_t    in_poll;
  uint8_t    frame_seen;        //a valid frame arrived since boot (MCP indicator)
  uint8_t    chip_state;        //what the indicator shows now
  uint32_t   chip_ms;           //when it was last drawn
  uint32_t   last_frame_ms;
  uint32_t   last_main_ms;
  uint32_t   init_ms;
  uint32_t   first_poll_ms;     //boot -> first MAIN poll, first pass duration (GET_LINK_STATS)
  uint32_t   first_pass_ms;
  uint32_t   first_poll_at;     //pc_hal_ms of the first MAIN poll
  uint32_t   first_arm_ms;      //first MAIN poll -> first UI conversion started (0x05 wait done), ms + 1, 0 = not yet
  uint32_t   first_done_ms;     //first MAIN poll -> first UI capture read and processed, ms + 1, 0 = not yet
  uint32_t   first_mark[4];     //first MAIN poll -> first pass after battery, acquisition, touch, RTC; ms + 1

  //Shared settings tracking
  uint32_t   settings_fp;
  uint32_t   settings_seq;

  //Latest finished capture (any origin) and the one in flight
  pc_capture cap;
  pc_capture cap_next;
  uint8_t    ring_valid;        //the FPGA ring still holds capture cap (no arm since its DONE)
  uint8_t    ui_started;        //a UI conversion started and has not been counted yet
  uint8_t    ext_flags;         //FPGA 0x28 at the last MAIN poll (EXT_TRIGGER only): bit0 level, bit1 gen on, bit2 edge
  uint8_t    ui_trig;           //0x18 at the UI capture's DONE
  uint16_t   ui_addr;           //0x14 of the UI capture

  //READ_RAW hold while the UI capture in flight finishes
  uint8_t    ui_hold;           //UI must not re-arm
  uint8_t    hold_active;
  uint32_t   hold_deadline;

  //Host single capture
  uint8_t    acq_state;
  uint8_t    fpga_triggered;
  uint8_t    fpga_done;
  uint8_t    auto_timeout_hit;
  uint8_t    armed_once;
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

  //Raw divider set by SET_TIMEBASE mode 1 (no UI index); dropped once the UI changes its time base
  uint8_t    tb_raw;
  uint8_t    tb_raw_tpd;
  uint8_t    tb_raw_sr;
  uint32_t   tb_divider;

  //Generator values set by SET_GEN (raw DDS words)
  uint8_t    gen_valid;
  uint8_t    gen_enable;
  uint32_t   gen_phase;
  uint32_t   gen_thr;
  uint8_t    gen_ui_en;         //UI copy right after SET_GEN: a UI change of it drops the raw values
  uint8_t    gen_ui_pct;
  uint32_t   gen_ui_hz;

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

  //SD write staging buffer (HAL owned)
  uint8_t   *sd_stage;
  uint32_t   sd_stage_cap;

  //Dispatch counters (GET_LINK_STATS)
  uint32_t   main_polls;
  uint32_t   disp_main;         //events handled per poll context
  uint32_t   disp_busy;
  uint32_t   disp_isr;
  uint32_t   held;              //polls that left a request queued for the main loop
  uint32_t   isr_skipped;       //timer-tick polls skipped because another poll was running

  //Last request with a payload CRC error, exactly as received (header + payload + CRC), for GET_BAD_FRAME
  uint32_t   bad_count;
  uint32_t   bad_len;
  uint32_t   bad_rx_bytes;      //transport rx_bytes when it completed: locates it in the byte stream
  uint8_t    bad[PC_HDR_SIZE + PC_MAX_REQUEST_PAYLOAD + PC_CRC_SIZE];
} pc_session;

//----------------------------------------------------------------------------------------------------------------------------------

void     pc_session_init(pc_session *s);                 //reads the FPGA version through the HAL cache
void     pc_poll(pc_session *s, int ctx);
void     pc_ui_service(pc_session *s);                    //every main-loop pass after the MAIN poll: MCP indicator
int      pc_ui_chip_shown(const pc_session *s);           //1 while the chip occupies the RTC clock slot

//UI acquisition hooks (scope_functions.c / fpga_control.c, main-loop code)
int      pc_ui_arm_allowed(pc_session *s);                //0 while a READ_RAW waits for the UI capture in flight
void     pc_ui_capture_start(pc_session *s);              //fpga_do_conversion started a conversion
void     pc_ui_capture_addr(pc_session *s, uint16_t addr);//0x14 read of the finished UI capture
void     pc_ui_capture_done(pc_session *s);               //the UI read and processed it
void     pc_ui_pass_mark(pc_session *s, int n);            //main loop stage n (0..3) reached, kept for the first pass

//Exposed for tests
void     pc_variant_from_version(uint16_t version, pc_variant *v);
uint32_t pc_gen_phase_from_mhz(uint64_t freq_mhz);
uint64_t pc_gen_mhz_from_phase(uint32_t phase_inc);
uint32_t pc_gen_thr_from_bp(uint32_t duty_bp);
uint16_t pc_gen_bp_from_thr(uint32_t threshold);
uint32_t pc_ring_mod(int64_t addr, uint32_t ring_words);
void     pc_test_pattern(uint8_t *dst, uint32_t length, uint8_t pattern);

#endif /* PC_CMDS_H */
