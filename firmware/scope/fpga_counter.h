//----------------------------------------------------------------------------------------------------------------------------------
// fpga_counter.h - the FPGA 0x153C hardware counter (fw-c7, item C7 of docs/plans/fpga-0x153c.md; the RTL semantics in
// fpga/al3/AGENTS.md "0x153C C3 counter"). Registers: 0x61 bits 1..0 gate (0 off, 1 = 10 ms, 2 = 100 ms, 3 = 1 s: exactly
// 2e6 / 2e7 / 2e8 clocks of 5 ns), bits 3..2 source (0 = the trigger source as 0x15 was written, 1 CH1, 2 CH2, 3 EXT);
// 0x62 byte 0 level (raw ADC code, the Schmitt trigger is high at code >= level), byte 1 hysteresis (low below level -
// hysteresis); read 0x35 = 16 bytes from one snapshot taken at the command byte: { new, 0, EXT, CH2, 0, 0, gate[1:0] },
// seq, E, T, W (u32 each, 28 bits used), min, max. From the first rising edge of the gate to the last: E periods, T
// clocks, W high clocks; E = 0 = fewer than two edges. Any change of the effective source or gate restarts the gate
// (a 0x15 write with source 0 too), a same-value 0x61 write does not; a level change does not restart it.
//
// The firmware (fpga_counter.c, host-buildable against fpga_bus.h's primitives, host_tests/test_fpga_bus.c):
// - Boot (fpga_counter_boot, built-for bitstream only): 0x62 = 128 / 6, then 0x61 = gate 100 ms, source 0 (5 strobes).
// - One read per screen pass (fpga_counter_pass, after the capture readout: the command byte + 16 data strobes, 17 x 0.3
//   us = about 5 us): a snapshot with "new" set is the result of the last completed gate and is taken, derived
//   (frequency, period, high / low time, duty: integer arithmetic below) and time-stamped.
// - Rate changes: a 0x0D / 0x53 write while a gate runs puts a few pairs of mixed age into the stream (the cores'
//   pipelines re-align), which adds or drops one edge in the gate that spans the write. The pass that sees such a write
//   (fpga_bus_wr.wclk moved since the previous pass) writes 0x61 = 0 first (the running gate is aborted, nothing can be
//   published after this), then reads 0x35 and drops the result it may carry (the gate that spanned the write, or one
//   read late), then writes 0x61 = the value again: the next result is a clean full gate. Cost 4 strobes on such passes.
// - The level follows the UI's trigger level (the marker the user placed inside the signal) with hysteresis 6 codes
//   (about three times the 2 LSB noise) unless the host pinned it (GET_COUNTER what = 1 with level != 0); a change
//   writes 0x62 (3 strobes) and restarts the gate the same way, so no result mixes two levels.
// - A host configuration (GET_COUNTER what = 1) is only noted; the next pass writes it (0x62, 0x61 = 0, 0x61 = value).
// - A result is current while the gate is on and it is younger than three gates plus 100 ms (the configured gate);
//   with E >= 1 it feeds the measurement cards of its source channel (ui_meas.c) and GET_COUNTER's VALID bit.
//
// Arithmetic (64-bit integers, no floating point; one clock = 5 ns): frequency = E x 2e8 / T Hz, in uHz through a
// two-stage division (E x 2e8 fits, E x 2e14 does not); period = T x 5000 / E ps; high time = W x 5000 / E ps; low time
// = (T - W) x 5000 / E ps; duty = W x 1e6 / T ppm. All rounded to nearest. Resolution one clock over the gate: 5e-7
// relative at 10 ms, 5e-9 at 1 s (the 50 MHz crystal dominates); range about 1 Hz (two edges in a 1 s gate) to the
// front end's 30 MHz.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef FPGA_COUNTER_H
#define FPGA_COUNTER_H

#include <stdint.h>

#define FPGA_CNT_GATE_OFF    0
#define FPGA_CNT_GATE_10MS   1
#define FPGA_CNT_GATE_100MS  2
#define FPGA_CNT_GATE_1S     3

#define FPGA_CNT_SRC_TRIG    0        //0x61 bits 3..2: the trigger source (0x15)
#define FPGA_CNT_SRC_CH1     1
#define FPGA_CNT_SRC_CH2     2
#define FPGA_CNT_SRC_EXT     3

#define FPGA_CNT_RES_CH1     0        //a result's source (from the snapshot's EXT / CH2 bits)
#define FPGA_CNT_RES_CH2     1
#define FPGA_CNT_RES_EXT     2

#define FPGA_CNT_LEVEL_BOOT  128
#define FPGA_CNT_HYST_BOOT   6

typedef struct
{
  //Configuration asked for (boot, or GET_COUNTER what = 1)
  uint8_t  cfg_gate;            //FPGA_CNT_GATE_*
  uint8_t  cfg_source;          //FPGA_CNT_SRC_*
  uint8_t  cfg_level;           //0 = follow the UI trigger level
  uint8_t  cfg_hyst;
  uint8_t  pending;             //a configuration not yet written (the next pass writes it)

  //What the FPGA holds (the last values written)
  uint8_t  gate, source;        //0x61
  uint8_t  level, hyst;         //0x62
  uint32_t wclk_seen;           //fpga_bus_wr.wclk at the previous pass

  //The last snapshot read, and the last result taken from one
  uint8_t  raw[16];
  uint8_t  held;                //a result is held
  uint8_t  seq;                 //its sequence number
  uint8_t  res_src;             //FPGA_CNT_RES_*
  uint8_t  res_gate;            //its gate code
  uint32_t e, t, w;
  uint8_t  min, max;
  uint32_t res_ms;              //when it was read (the pass's clock)

  //Derived from E / T / W (fpga_counter_derive); 0 when E = 0
  uint8_t  has_period;          //E >= 1
  uint64_t freq_uhz;
  uint64_t period_ps;
  uint64_t width_ps;            //high time
  uint64_t low_ps;              //low time
  uint32_t duty_ppm;

  //Counters (tests, diagnostics)
  uint32_t reads;               //0x35 reads
  uint32_t results;             //snapshots with "new" taken
  uint32_t dropped;             //"new" snapshots dropped after a write clock change
  uint32_t restarts;            //0x61 = 0 / value sequences written by the pass
} fpga_counter_state;

extern fpga_counter_state fpga_cnt;

//Boot: 0x62 = 128 / 6, 0x61 = gate 100 ms with source 0 (the trigger source); the state reset, the write clock baseline
void fpga_counter_boot(void);

//A host configuration: noted, written by the next pass (level 0 = follow the UI trigger level). gate and source are
//FPGA_CNT_GATE_* / FPGA_CNT_SRC_* (the caller validates)
void fpga_counter_config(uint8_t gate, uint8_t source, uint8_t level, uint8_t hyst);

//Once per main-loop pass after the capture readout: the rate-change rule, the 0x35 read, the level follow and a pending
//configuration (see above). now_ms = the pass's clock, trig_level = the UI's trigger level clamped to 0..255
void fpga_counter_pass(uint32_t now_ms, uint8_t trig_level);

//The held result is current: the gate is on and it is younger than three configured gates plus 100 ms
int fpga_counter_current(uint32_t now_ms);

//The derived values of E / T / W as above; 0 when e == 0 (the outputs are then 0)
int fpga_counter_derive(uint32_t e, uint32_t t, uint32_t w, uint64_t *freq_uhz, uint64_t *period_ps, uint64_t *width_ps,
                        uint64_t *low_ps, uint32_t *duty_ppm);

//The gate in ms (0 for off)
uint32_t fpga_counter_gate_ms(uint8_t gate);

#endif /* FPGA_COUNTER_H */
