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

//Firmware auto mode (fw 0.2.41): give up waiting for a trigger after max(100 ms, two record lengths)
#define PC_AUTO_MIN_MS    100
uint32_t pc_auto_deadline_ms(uint32_t samples, uint32_t divider);

//The FPGA bitstream this firmware is built for (owner decision 2026-10-20: every firmware version supports the latest
//bitstream only, fpga/al3/dist/manifest.json "recommended"). The capability word, the geometry and every feature
//decision derive from this constant; the version word the FPGA reports (command 0x06) is read once at boot and only
//compared with it. A different word (older or newer) is a mismatch: the UI shows the notice screen (ui_diag.c
//ud_build_fpga_mismatch), GET_INFO 78 / GET_HEALTH 126 bit 0 report it, and every command that would touch the
//acquisition answers E_UNSUPPORTED detail PC_UNSUP_FPGA_MISMATCH (F_BASIC commands keep working: pc_cmds.c).
#define PC_FW_FPGA_VERSION  0x153C
#define PC_FW_FPGA_VERSION_TEXT "0x153C"  //the same word as text (the mismatch error's text, pc_dispatch); keep in step
#define PC_FPGA_F_MISMATCH  0x01      //GET_INFO 78 / GET_HEALTH 126 bit 0
#define PC_UNSUP_FPGA_MISMATCH 1      //E_UNSUPPORTED detail: the running bitstream is not PC_FW_FPGA_VERSION

#ifndef PC_FW_VERSION
#if PC_FIFO_MAP == 1
#define PC_FW_VERSION_MAP "-m1"         //alternative USB FIFO layout (pc_transport.h)
#else
#define PC_FW_VERSION_MAP ""
#endif
#if defined(PC_MMU) && !PC_MMU
#define PC_FW_VERSION_MMU "-nommu"      //fw 0.2.48 A/B build: MMU and D-cache left off (pc_mmu.h)
#else
#define PC_FW_VERSION_MMU ""
#endif
#define PC_FW_VERSION     "0.2.104" PC_FW_VERSION_MAP PC_FW_VERSION_MMU
#endif

//The build identity record (build_id.c, pc_ident.h; host tests: mock_hal.c)
#include "pc_ident.h"
#define pc_fw_build       (pc_image_ident.build)          //short git hash of the build
#define pc_build_time     (pc_image_ident.time)           //commit time (UTC) yy, mm, dd, hh, mm, ss

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
#define PC_CTX_BUSY       1     //inside a UI busy loop: answers the F_BUSY set only, no FPGA (1.23: the yield request)
#define PC_CTX_ISR        2     //1 ms timer interrupt: same safe set and the TX pump (1.23: the yield request), never the UI/FPGA
#define PC_CTX_MODAL      3     //fw 0.2.94: a modal loop's yield check (PC_UI_YIELD): the read-only F_MODAL set is answered
                                //there, the screen stays open (READ_SCREEN shows the menu); anything else asks to yield

//Time without a MAIN poll after which the UI counts as modal (LOCAL_BUSY). Before protocol 1.23 other requests then got
//E_BUSY; since 1.23 (fw 0.2.78, docs/protocol.md section 14 row 63) such a request asks the UI to yield: every modal
//loop leaves by its cancel path down to the base screen (PC_UI_YIELD in pc_hooks.h) and the main loop answers it
#define PC_MODAL_MS       250

//1.23: a request waiting for the UI to yield gets E_BUSY detail PC_BUSY_NO_YIELD when no main-loop pass comes in this
//time (a long UI pass that has no yield check: auto set, a slow capture, a step that must finish)
#define PC_YIELD_MS       1500

//E_BUSY detail (1.23). 0: a deferred request is pending (and, before 1.23, a modal UI); 1: READ_RAW, the UI capture in
//flight did not finish (PC_HOLD_MS). 2..5: the UI did not yield; the reason is also the text
#define PC_BUSY_UI        0
#define PC_BUSY_CAPTURE   1
#define PC_BUSY_NO_YIELD  2     //"UI did not return to the main screen"
#define PC_BUSY_CAL       3     //"calibration measurement running" (wizard step, volts calibration step)
#define PC_BUSY_SAVE      4     //"file save in progress"
#define PC_BUSY_USB_DRIVE 5     //"USB drive mode"

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
#define PC_ACQ_LOGGING    5     //D4 logger (1.27): the streaming logger runs (LOG_START .. its end)

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
//Writes end at the first partition, and never at or above PC_SD_LIMIT whatever the partition table says (fw 0.2.101:
//16384 sectors = 8 MiB, was 2048). The scope image at 750 may end there: 15634 sectors, about 7.6 MiB (0.2.100 is 1510
//sectors). The bootloader loads the image by its eGON length with no size limit of its own (firmware/AGENTS.md section
//67), so the cap only bounds what a host may write. Cards for this firmware have their FAT32 partition at 16384 or later
//(firmware/README.md section 3). SD_INFO's check 3 and the card image identity use the same bound.
#define PC_SD_LIMIT       16384
#define PC_SD_WF_CONFIG   0x01
#define PC_SD_WF_BOOT     0x02

//Relay settling time after a range or coupling change (P-8.3)
#define PC_SETTLE_MS      50

//READ_RAW / READ_TRACE interleave order: 0 = core B before core A in each word (1.00o5 convention, P-7.3). Fixed since
//fw 0.2.62: FPGA 0x1538 guarantees it at every rate (0x33 bits 3:2 = 01, checked at boot), the calibration's per-core
//zeros and gains and acal_track's A - B model assume it (0x153C has no core-order swap bit)
#define PC_INTERLEAVE_ORDER 0

//----------------------------------------------------------------------------------------------------------------------------------

//The bitstream's geometry and capabilities (P-5.1). Fixed per firmware since fw-one: AL3A10 PC_FW_FPGA_VERSION, variant 2
//on the wire, ring 8192 words, 16382 samples per channel; caps from the constant, never from the version word. The version
//word read at boot is kept for GET_INFO 8 and the mismatch flag only.
#define PC_FPGA_RING_WORDS  8192
#define PC_FPGA_MAX_TOTAL   16382
#define PC_FPGA_VARIANT     2

typedef struct
{
  uint16_t version;             //raw word from FPGA command 0x06 (what the scope runs)
  uint8_t  variant;             //PC_FPGA_VARIANT (wire encoding of P-5.1 kept)
  uint8_t  mismatch;            //version != PC_FW_FPGA_VERSION: acquisition refused, notice screen
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
  uint8_t    flags;             //bit0 triggered, bit1 auto timeout, bit2/3 overload CH1/CH2, bit4 settings changed,
                                //bit5 EXT edge (bit6 was "may be spliced" on fw 0.2.97..0.2.102, 0x1532..0x153A only)
  uint16_t   trigger_addr;
  uint16_t   capture_start;
  uint8_t    timeperdiv;        //0xFF: raw divider
  uint8_t    source, edge, level, mode;
  uint32_t   divider;
  uint32_t   pretrigger;        //samples
  uint32_t   total;
  uint8_t    decim;             //1.9: decimation filter as written for it (0x29 byte 0 layout: bit 7 16-bit, K)
  uint8_t    decim_shift;       //S
  uint32_t   decim_n;           //N (0 when K = 0)
  uint16_t   ext_shift;         //EXT trigger with K >= 1: latency correction in samples (pc_decim_ext_shift)
  //1.17: sequencer run of this capture (host ARM after SET_SEQ, FPGA 0x1536+); mode 0 = a single capture
  uint8_t    seq_mode;
  uint8_t    seq_flags;         //bit 0 circular, bit 1 stopped by the host (STOP), bit 2 overflow (a circular run wrapped)
  uint8_t    seq_m;
  uint8_t    seq_fmt16;         //records in the 16-bit layout
  uint16_t   seq_count;         //segments / positions requested
  uint16_t   seq_untrig;        //untriggered segments or skipped records (0x2A at DONE)
  uint32_t   seq_done;          //segments completed / records accumulated (0x2A at DONE)
  uint32_t   seq_n;             //records per position
  uint32_t   seq_holdoff_ns;
  uint16_t   seq_pre_words;     //0x0B / 0x0C as written
  uint16_t   seq_total_words;
  pc_hw_channel ch[2];
  //1.26: the trigger extension in effect at the arm (GET_CAPTURE_INFO 96..111); the polarity is `edge`, the level `level`
  uint8_t    trig_type;
  uint8_t    trig_flags;
  uint32_t   trig_n;
  uint32_t   trig_h;
  uint32_t   trig_holdoff;
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
  uint8_t    trig_st[4];        //1.26: FPGA 0x37 and the 3 bytes of 0x34, read at every MAIN poll while a host capture is
                                //in flight (GET_STATUS 48..51); zeroed at ARM
  //C7 counter: GET_COUNTER's NEW bit = the result's sequence number differs from the one the previous GET_COUNTER
  //returned (or none was returned yet)
  uint8_t    cnt_seen;
  uint8_t    cnt_seen_seq;
  //D4 logger: what LOG_START took, the chunk sequence and the host watchdog
  uint8_t    log_used;          //a LOG_START ran since boot (LOG_READ refused before)
  uint8_t    log_link_lost;     //a link reset or DTR drop while logging: stopped at the next MAIN poll
  uint32_t   log_divider;
  uint32_t   log_duration_ms;
  uint32_t   log_clocks;        //5 ns clocks per sample: D + 1 (format 0), N (16-bit path)
  uint32_t   log_seq;           //non-empty chunks returned
  uint32_t   log_read_ms;       //the last LOG_READ (or the start)
  uint8_t    ui_trig;           //0x18 at the UI capture's DONE
  uint16_t   ui_addr;           //0x14 of the UI capture

  //READ_RAW hold while the UI capture in flight finishes
  uint8_t    ui_hold;           //UI must not re-arm
  uint8_t    hold_active;
  uint32_t   hold_deadline;

  //1.17: sequencer setting for the next host ARM (SET_SEQ), and the run in flight
  pc_seq_cfg seq;
  uint8_t    seq_stop;          //STOP ended the run in flight: it completes as DONE with what it holds

  //Host single capture
  uint8_t    acq_state;
  uint8_t    fpga_triggered;
  uint8_t    fpga_done;
  uint8_t    auto_timeout_hit;
  uint8_t    fw_auto;           //fw 0.2.41: host auto mode armed normal, firmware time-out at auto_ms
  uint8_t    host_forced;       //the firmware time-out ended it (untriggered)
  uint32_t   auto_ms;
  uint8_t    armed_once;
  uint32_t   capture_id;
  uint32_t   arm_ms;
  uint16_t   trigger_addr;
  uint16_t   capture_start;
  uint16_t   cap_pre_words;     //geometry latched at ARM
  uint16_t   cap_total_words;
  uint16_t   cap_ext_shift;     //EXT latency correction of the host capture (samples)
  uint8_t    cap_wide;          //1.9: 16-bit layout (decimation filter on): one sample per word
  uint32_t   cap_divider;
  //fw 0.2.62: what the arm wrote, for a re-arm after a garbled full-rate capture (enc_heal.h)
  pc_decim_cfg arm_dc;
  pc_seq_cfg arm_seq;
  uint32_t   arm_auto;
  uint8_t    arm_normal;
  eh_cap     enc_cap;
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

  //Generator state set by SET_GEN / SET_PATTERN (1.27, fw-d5: the two-step square as P16 / H16, 1/65536 of a 5 ns clock)
  uint8_t    gen_valid;
  uint8_t    gen_enable;
  uint8_t    gen_src;           //0 the generator's square, 1 a SET_PATTERN pattern (GEN byte 23)
  uint8_t    gen_duty_raw;      //1: the duty is gen_h16 as given (duty_mode 1), 0: gen_duty_bp of the period
  uint16_t   gen_duty_bp;
  uint64_t   gen_p16;
  uint64_t   gen_h16;
  uint8_t    gen_ui_en;         //UI copy right after SET_GEN: a UI change of it drops the host's values
  uint8_t    gen_ui_pct;
  uint32_t   gen_ui_hz;

  //D5 pattern (fw-d5)
  uint8_t    ctx;               //the dispatch context of the running request (PC_CTX_*)
  uint8_t    pat_st[4];         //0x39 as last read
  uint8_t    ptrig_st[3];       //0x42 as last read
  pc_hw_pattern pat;            //the latest SET_PATTERN

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
  uint8_t    restart_pending;   //PC_DEBUG GET_HEALTH flag 0x80 answered: restart from RAM once the response has left

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

  //1.23 (fw 0.2.78): a request that needs the main loop while the UI is modal asks the UI to leave its modal screens
  uint8_t    yield_req;         //set from the ISR / BUSY poll, read by the modal loops (pc_hal), cleared by the MAIN poll
  uint32_t   yield_ms;          //when it was set (PC_YIELD_MS)
  uint32_t   yields;            //requests that asked the UI to yield (GET_LINK_STATS 68)
  uint32_t   yield_refused;     //E_BUSY answers instead: a hold reason or the yield time-out (GET_LINK_STATS 69)
  uint8_t    modal_req;         //fw 0.2.94: a read-only request waits for a modal loop's yield check (PC_CTX_MODAL)
  uint32_t   modal_ms;          //when it was set (PC_YIELD_MS)

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
void     pc_ui_host_stop(pc_session *s);                  //fw 0.2.68: the screen's STOP aborts a host single
void     pc_ui_capture_start(pc_session *s);              //fpga_do_conversion started a conversion
void     pc_ui_capture_addr(pc_session *s, uint16_t addr);//0x14 read of the finished UI capture
void     pc_ui_capture_done(pc_session *s);               //the UI read and processed it
void     pc_ui_pass_mark(pc_session *s, int n);            //main loop stage n (0..3) reached, kept for the first pass
int      pc_ui_yield_pending(const pc_session *s);        //1.23: 1 while a request waits for the modal UI to close
int      pc_ui_modal_pending(const pc_session *s);        //fw 0.2.94: 1 while a read-only request waits for a yield check
                                                          //(and nothing holds it): modal loops leave by their cancel path

//Diagnostics (fw 0.2.52, protocol row 55): the GET_LINK_STATS counters (returns n), the GET_HEALTH body (96 bytes),
//one channel's GET_CAL body (PC_CAL_RESP_LEN bytes), the SD_INFO body (112 bytes; ident: identify the card image,
//MAIN context; returns 0, or 1 without a card). The report: pc_report.h
#define PC_LINK_COUNTERS  70
#define PC_INFO_LEN       80      //76 up to 1.24: 76 u16 built-for FPGA word, 78 u8 fpga_flags (PC_FPGA_F_MISMATCH), 79 0
#define PC_STATUS_LEN     52      //48 in 1.17-1.25; 1.26: 48 u8 0x37, 49 u8 0x34 byte 0, 50 u16 edges still to count
#define PC_TRIGGER_LEN    52      //28 up to 1.25; 1.26: 28.. the SET_TRIGGER2 body that reproduces the trigger
#define PC_CAPTURE_INFO_LEN 112   //96 in 1.17-1.25; 1.26: 96 type, 97 flags, 98 u16 0, 100 u32 N, 104 u32 H, 108 u32 hold-off
#define PC_LOG_LEN        48      //D4 logger (1.27): LOG_START / LOG_STOP response (protocol row 67)
#define PC_LOG_HDR        16      //D4 logger (1.27): LOG_READ chunk header
#define PC_LOG_READ_MAX   65536u  //D4 logger: LOG_READ max_bytes 1..65536
#define PC_LOG_HOST_MS    10000u  //D4 logger: a running log with no LOG_READ for this long is stopped (state 4): the host
                                  //went away, and the frozen screen must not stay frozen
#define PC_STATE_LOGGING  9       //D4 logger: E_STATE detail of a command refused while logging
#define PC_STATE_LOG_VIEW 10      //D4 logger: E_STATE detail of LOG_START outside the Scope view
#define PC_GEN_LEN        40      //24 up to 1.26; 1.27 (fw-d5): 0x39 status, source, P16, H16 (docs/protocol.md row 67)
#define PC_COUNTER_LEN    24      //1.26 (fw-c7): GET_COUNTER: flags, seq, gate, source, E, T, W, min, max, level, hysteresis, freq_mHz
#define PC_HEALTH_LEN     128     //96 in 1.16-1.18, 104 in 1.19-1.23, 124 in 1.24 (layout 2); layout 3: 124 built-for word,
                                  //126 fpga_flags, 127 0
#define PC_CAL_RESP_LEN   364     //356 in 1.16-1.23, 72 bytes up to 1.9, 248 in 1.10-1.11 (v2), 256 in 1.12-1.13 (v3), 288 in 1.14-1.15 (v4)
#define PC_SD_INFO_LEN    112     //64 before 1.16
uint32_t pc_link_counters(pc_session *s, uint32_t *v);
void     pc_health_body(uint8_t *p);
void     pc_cal_body(int ch, uint8_t *resp);
int      pc_sd_info_body(pc_session *s, int verify, int ident, uint8_t *resp, uint8_t *scratch, uint32_t scratch_bytes);

//Exposed for tests
void     pc_variant_init(uint16_t version, pc_variant *v);    //the fixed geometry and caps; mismatch from the word read
void     pc_variant_core_order(pc_variant *v);               //clears INTERLEAVE_UNVERIFIED when 0x33 confirmed the order at boot
int      pc_decim_has(void);                                 //PC_DECIM_HAS_FIR | PC_DECIM_HAS_CAPSTAGES: what pc_decim_config gets
uint32_t pc_ring_mod(int64_t addr, uint32_t ring_words);
void     pc_test_pattern(uint8_t *dst, uint32_t length, uint8_t pattern);

#endif /* PC_CMDS_H */
