//----------------------------------------------------------------------------------------------------------------------------------
// mock_hal.h - host model of the scope behind pc_hal.h, used by test_session.c
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef MOCK_HAL_H
#define MOCK_HAL_H

#include <stdint.h>
#include "pc_hal.h"
#include "pc_protocol.h"
#include "pc_ident.h"

#define MOCK_RING_MAX 8192
#define MOCK_SD_SECTORS 4096                //2 MiB card model, partition at 2048 unless a test changes the MBR
#define MOCK_SD_MAX_SECTORS 20480           //the array behind it: a test may report up to 10 MiB (mock.sd_sectors)

typedef struct
{
  //Clock
  uint32_t now;

  //FPGA
  uint16_t version;
  uint8_t  ring[4][MOCK_RING_MAX];
  uint32_t ring_words;
  int      arm_fail;
  uint32_t arms;
  uint16_t arm_pre, arm_total;
  uint32_t arm_auto;
  int      arm_normal;
  int      polls_to_trigger;      //acq_poll calls until 0x18 reads 1 (-1 never)
  int      polls_to_done;         //acq_poll calls until 0x0A reads 1
  uint16_t trigger_addr;
  uint32_t forces;
  uint8_t  ext_status;            //FPGA 0x28
  uint32_t ext_reads;
  //1.26 (fw-c5, FPGA 0x153C): the trigger extension the firmware wrote (every write asks for the hold-off clear), and
  //what the 0x37 / 0x34 status reads answer
  pc_hw_trig_ext trig_ext;
  uint32_t trig_ext_writes;
  uint8_t  trig_st[4];
  uint32_t trig_status_reads;
  uint32_t read_calls;
  uint32_t max_read_start;        //largest start address passed to read_core (must be < ring)
  uint32_t debug_cmds[16];
  uint32_t ndebug_cmds;
  uint32_t fpga_ops;              //calls of HAL functions that drive the FPGA bus on the scope (test_fast_path_no_fpga)

  //Decimation filter (0x1534+)
  uint8_t  decim_setting;         //pc_hal_decim_setting (bits 1:0 K, PC_DECIM_SWAP)
  pc_decim_cfg decim_cur;         //written at the last arm
  int      decim_written;
  uint32_t decim_arms;            //arms with a non-NULL configuration
  uint16_t decim_rb;              //0x29 read-back

  //Sequencer (0x1536+, pc_seq.h): the last arm's setting, the accumulator words, the 0x2A status, the readout view
  uint32_t   seq_arms;            //arms with a non-NULL setting
  pc_seq_cfg seq_cfg;
  uint8_t    seq_run_mode;        //q_mode of the run (0 after an arm with mode 0)
  uint8_t    seq_fmt16;
  uint8_t    seq_57;              //0x57 as written (raw bit live)
  uint8_t    seq_shift;           //0x5B
  uint64_t   acc[2048];           //accumulator words as stored (table entries, lane sums)
  uint8_t    seq_status[8];       //0x2A
  uint32_t   seq_stops, seq_status_reads, seq_reads, seq_views;
  uint8_t    ui_avg_state, ui_avg_log2n;

  //Settings
  pc_hw_channel  ch[2];
  uint32_t       ch_writes;
  pc_hw_timebase tb;
  uint32_t fpga_divider;          //0x0D the FPGA holds (pc_hal_divider_now): every timebase write; a UI snap is tb only
  uint32_t       tb_writes;
  pc_hw_trigger  trg;
  uint8_t        gen_en, gen_duty;
  uint32_t       gen_hz;
  uint32_t       gen_phase, gen_thr, gen_writes;
  uint8_t        gen_hw_en;

  //Trace
  uint8_t  trace[2][16384];
  uint16_t trace16[2][8192];      //16-bit samples of a decimated build
  uint32_t trace16_n[2];
  uint32_t trace_builds;
  uint32_t ui_samples, ui_pre;
  int32_t  ui_sw;

  //UI (shared control)
  uint8_t  ui_run;                //scopesettings.runstate
  uint8_t  ui_armed;              //UI conversion in flight
  uint8_t  long_mode;
  uint32_t run_sets;              //pc_hal_ui_set_run calls
  int      last_run_set;
  int      indicator;             //last pc_hal_indicator state
  uint32_t indicator_draws;
  uint8_t  ui_hold;               //1.23: pc_hal_ui_hold (PC_BUSY_CAL / _SAVE / _USB_DRIVE, 0 none)
  //1.23: the mock UI's modal screens, nested (a menu, a fold-out over it, a dialog over that). mock_ui_modal_pass runs one
  //iteration of the innermost one as every firmware modal loop does: the touch scan (a BUSY poll), then PC_UI_YIELD
  uint8_t  ui_modal;              //modal screens open (0 = the base screen, the main loop runs)
  uint32_t ui_cancels;            //screens left by their cancel path because a host request waited
  //fw 0.2.96: a main-loop screen over the view (Diagnostics): left by Back before a state-changing request runs
  //(pc_hal_ui_leave_screen). under_screen counts setting writes / view redraws made while it showed (must stay 0),
  //screen_read_under is ui_screen at the last READ_SCREEN rows read
  uint8_t  ui_screen;
  uint32_t ui_screen_leaves;
  uint32_t under_screen;
  uint8_t  screen_read_under;

  //SD card
  uint8_t  *sd;                     //MOCK_SD_MAX_SECTORS x 512
  uint32_t  sd_sectors;             //reported size (0 = no card)
  uint32_t  sd_reads, sd_writes;
  int       sd_fail_write;          //driver error to return from writes (0 = ok)
  int       sd_corrupt;             //1: writes flip a bit (verify must catch it)

  //Battery
  uint8_t  batt_level, batt_charging, batt_raw;

  //Transport
  uint8_t  rx[1 << 16];
  uint32_t rx_head, rx_tail;
  uint32_t link_events;
  int      dtr;
  int      tx_hold;               //1: a submitted frame stays "busy" until mock_tx_release()
  int      tx_busy;
  uint32_t submits;
  uint32_t tx_since;
  uint32_t tx_aborts;
  uint32_t fel_entries;

  //Diagnostics readers (1.16)
  pc_hw_usb  usb;
  uint32_t   cp15;
  pc_hw_life life;
  uint32_t   life_clears;
  uint32_t   restart_tests;         //pc_hal_restart_test calls
  int        prov_state;
  uint8_t    prov_raw[ACAL_PROV_BYTES];
  char      *report;                //last report text written (NUL-terminated)
  uint32_t   report_len;
  uint32_t   report_writes;
  int        report_fail;           //FRESULT to return

  //Full-rate capture check (fw 0.2.62, enc_heal.h)
  int        enc_bad_arms;          //the next arms whose capture is garbled (-1: every one)
  uint8_t    enc_cur_bad;           //the capture of the last arm is garbled
  uint8_t    enc33_bad;             //0x33 of a garbled capture (0x1538+), else EH_33_GOOD
  uint32_t   enc_status_reads, enc_notes, enc_events;
  uint8_t    enc_last_why;
  eh_note    enc_last_note;
  pc_hw_enc  enc;                   //what pc_hal_enc_state answers (counted as the target counts)
  pc_hw_cap  cap;                   //fw 0.2.79: what pc_hal_cap_state answers (0x153A capture stages)
  uint32_t   cap_checks;            //pc_hal_cap_check calls (0x36 reads after full-rate host captures)
  //fw 0.2.72: FPGA writes (fpga_bus.h): a rate / filter write since the last arm, counters, event 22 level
  uint8_t    enc_written;
  uint32_t   fpga_rate_writes, fpga_filter_writes, fpga_resets;
  uint8_t    fpga_trace;
} mock_state;

extern mock_state mock;

void mock_reset(uint16_t fpga_version);
uint32_t mock_sr_div(uint32_t i);        //the mock's copy of variables.c's sample_rate_settings[i]
void mock_rx_push(const uint8_t *data, uint32_t len);

//1.23: one iteration of the innermost modal loop (see ui_modal): BUSY poll, then leave by the cancel path when a host
//request waits for the base screen. Returns 1 when that screen closed
int  mock_ui_modal_pass(void *session);
void mock_tx_release(void);
void mock_sd_image(uint32_t sector, const char *magic, uint32_t sectors);   //eGON header + valid checksum

//Captured responses (most recent last)
#define MOCK_MAX_RESP 32
typedef struct
{
  uint8_t  *data;
  uint32_t  len;
} mock_resp;

extern mock_resp mock_resps[MOCK_MAX_RESP];
extern uint32_t  mock_nresps;
void mock_clear_resps(void);

#endif
