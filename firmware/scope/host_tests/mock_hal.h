//----------------------------------------------------------------------------------------------------------------------------------
// mock_hal.h - host model of the scope behind pc_hal.h, used by test_session.c
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef MOCK_HAL_H
#define MOCK_HAL_H

#include <stdint.h>
#include "pc_hal.h"
#include "pc_protocol.h"

#define MOCK_RING_MAX 8192

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
  uint32_t read_calls;
  uint32_t max_read_start;        //largest start address passed to read_core (must be < ring)
  uint32_t debug_cmds[16];
  uint32_t ndebug_cmds;

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

  //Ownership / UI
  uint32_t enters, exits;
  int      last_keep;
  int      banner_touch;
  uint32_t shows;

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
