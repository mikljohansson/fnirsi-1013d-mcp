//----------------------------------------------------------------------------------------------------------------------------------
// mock_hal.h - host model of the scope behind pc_hal.h, used by test_session.c
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef MOCK_HAL_H
#define MOCK_HAL_H

#include <stdint.h>
#include "pc_hal.h"
#include "pc_protocol.h"

#define MOCK_RING_MAX 8192
#define MOCK_SD_SECTORS 4096                //2 MiB card model, partition at 2048 unless a test changes the MBR

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
  int      arm_prog, arm_normal;
  int      polls_to_trigger;      //acq_poll calls until 0x18 reads 1 (-1 never)
  int      polls_to_done;         //acq_poll calls until 0x0A reads 1
  uint16_t trigger_addr;
  uint32_t forces;
  uint8_t  ext_status;            //FPGA 0x28
  uint32_t ext_reads;
  uint32_t read_calls;
  uint32_t max_read_start;        //largest start address passed to read_core (must be < ring)
  uint32_t debug_cmds[16];
  uint32_t ndebug_cmds;
  uint32_t fpga_ops;              //calls of HAL functions that drive the FPGA bus on the scope (test_fast_path_no_fpga)

  //Settings
  pc_hw_channel  ch[2];
  uint32_t       ch_writes;
  pc_hw_timebase tb;
  uint32_t       tb_writes;
  pc_hw_trigger  trg;
  uint8_t        gen_en, gen_duty;
  uint32_t       gen_hz;
  uint32_t       gen_phase, gen_thr, gen_writes;
  uint8_t        gen_hw_en;

  //Trace
  uint8_t  trace[2][16384];
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

  //SD card
  uint8_t  *sd;                     //MOCK_SD_SECTORS x 512
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
} mock_state;

extern mock_state mock;

void mock_reset(uint16_t fpga_version);
void mock_rx_push(const uint8_t *data, uint32_t len);
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
