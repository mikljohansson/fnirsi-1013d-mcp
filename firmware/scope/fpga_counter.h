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
// - The thresholds follow the signal (fw-ctrauto) unless the host pinned them (GET_COUNTER what = 1: a non-zero level
//   pins the level, a non-zero hysteresis pins the hysteresis; 0 = auto, the boot configuration). After each gate the
//   result's raw min / max (CH1 / CH2) give the thresholds the signal asks for: span = max - min; hysteresis h =
//   max(6, round(span / 6)), at most span / 2; the band centred on the midpoint: level (the upper threshold, 0x62 byte
//   0) = ceil((min + max + h) / 2), the low threshold level - h. Why span / 6 (bench 2026-10-22, 0x153D, CH1 on an RC
//   node, 1 kHz, 89..184, fast jumps then slow exponential settles with a few codes rms of noise): hysteresis 6 read
//   2.6 to 13.7 kHz (the noise re-crossing the band on the slow settles), 16 and 32 read 1 kHz; span / 8 = 12 sits
//   between the failing 6 and the passing 16 (4 x the 3 codes rms, on a slow edge that dwells thousands of samples in
//   the band), span / 6 rounds to 16 there. A
//   hysteresis centred on the midpoint delays both edges alike, so a symmetric signal's duty is not biased by it.
//   A gate whose thresholds are within a deadband of what it asks for (level and hysteresis each within max(2, h / 4)
//   codes: the extremes of the noise move min / max by a code or two between gates) gives a valid result; one outside
//   it (a new signal, a range change, the first gate after boot) is marked misfit and the next thresholds are written
//   (0x62, 3 strobes, then the gate restarted as below, so no result mixes two threshold pairs). A span below 12 codes
//   (FPGA_CNT_AUTO_MIN_SPAN: no signal, the noise alone) keeps the thresholds and, with an auto level, marks the result
//   misfit. Misfit results are not VALID and do not feed the cards (the record's measurement shows). EXT results and
//   results dropped by the rate-change rule do not move the thresholds.
// - A host configuration (GET_COUNTER what = 1) is only noted; the next pass writes it (0x62, 0x61 = 0, 0x61 = value).
// - A result is current while the gate is on and it is younger than three gates plus 100 ms (the configured gate);
//   with E >= 1 and not misfit it feeds the measurement cards of its source channel (ui_meas.c) and GET_COUNTER's VALID
//   bit (fpga_counter_usable).
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

#define FPGA_CNT_LEVEL_BOOT  128      //the first gate's thresholds (written at boot; the auto rule replaces them)
#define FPGA_CNT_HYST_BOOT   6

#define FPGA_CNT_HYST_MIN       6     //auto hysteresis floor (about twice the analog noise rms on a quiet input)
#define FPGA_CNT_AUTO_MIN_SPAN  12    //auto: a gate's max - min below this is no signal

typedef struct
{
  //Configuration asked for (boot, or GET_COUNTER what = 1)
  uint8_t  cfg_gate;            //FPGA_CNT_GATE_*
  uint8_t  cfg_source;          //FPGA_CNT_SRC_*
  uint8_t  cfg_level;           //0 = auto (the midpoint of the previous gate's min / max)
  uint8_t  cfg_hyst;            //0 = auto (round(span / 6), at least 6)
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
  uint8_t  misfit;              //auto: counted at thresholds its own min / max do not ask for, or no signal (not VALID)

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
  uint32_t retunes;             //auto thresholds written after a misfit result
} fpga_counter_state;

extern fpga_counter_state fpga_cnt;

//Boot: 0x62 = 128 / 6, 0x61 = gate 100 ms with source 0 (the trigger source), level and hysteresis auto; the state
//reset, the write clock baseline
void fpga_counter_boot(void);

//A host configuration: noted, written by the next pass (level 0 / hysteresis 0 = auto, from the signal). gate and
//source are FPGA_CNT_GATE_* / FPGA_CNT_SRC_* (the caller validates)
void fpga_counter_config(uint8_t gate, uint8_t source, uint8_t level, uint8_t hyst);

//Once per main-loop pass after the capture readout: the rate-change rule, the 0x35 read, the auto thresholds and a
//pending configuration (see above). now_ms = the pass's clock
void fpga_counter_pass(uint32_t now_ms);

//The held result is current: the gate is on and it is younger than three configured gates plus 100 ms
int fpga_counter_current(uint32_t now_ms);

//The held result is usable (VALID, the cards): current, with a period, not misfit
int fpga_counter_usable(uint32_t now_ms);

//The derived values of E / T / W as above; 0 when e == 0 (the outputs are then 0)
int fpga_counter_derive(uint32_t e, uint32_t t, uint32_t w, uint64_t *freq_uhz, uint64_t *period_ps, uint64_t *width_ps,
                        uint64_t *low_ps, uint32_t *duty_ppm);

//The gate in ms (0 for off)
uint32_t fpga_counter_gate_ms(uint8_t gate);

#endif /* FPGA_COUNTER_H */
