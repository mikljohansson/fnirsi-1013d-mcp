//----------------------------------------------------------------------------------------------------------------------------------
// fpga_model.h - a bus-level model of the MCU interface of zaklad.v for test_fpga_bus.c (fw 0.2.72). It implements the bus
// primitives fpga_bus.c is built against (fpga_write_cmd, fpga_write_byte / short / int, fpga_read_byte and the readout
// strobes) and models, from fpga/al3/README.md "Changes" section 7 and the RTL it cites:
//   - the sample memory read: the word read on a strobe is the one latched by the previous strobe, and the read pointer
//     (0x1F) advances on each read strobe: mem[S], mem[S], mem[S + 1], ... without a dummy strobe (finding 5);
//   - the sampling reset: 0x01 = 1 / 0 with write clock edges every 10 (D + 1) ns (legacy layout, one word per edge),
//     read 0x05 ready once an edge has passed in reset, and the trigger read address loaded from the 0x0B register only on
//     write clock edges while in reset (0x1532..0x1536), plus on the first edge after the release (0x1537+, `ssr_d`)
//     (finding 2);
//   - every bus strobe takes a random 41..298 ns (the testbench's strobe periods), so each arm starts at a random phase;
//   - fw 0.2.97, with `capture` set: the capture logic of fpga/al3/README.md section 11 (legacy layout, one word per write
//     clock edge after the release): each word written carries its write index (lanes 0x20..0x23 = bytes 0..3 of it), the
//     ring wraps, a trigger at edge `trig_at` (after the pretrigger), the stop when the post-trigger region is complete
//     or, in auto mode (0x1A = 0), when the words written exceed 0x0E + 1 (W = 0x0E + 2: the forced stop too, since
//     0x0E is long past then). 0x14 per bitstream: 0x1532..0x153A latch the pretrigger on a time-out stop and let it cut
//     a triggered capture, and an edge after the stop still sets 0x18; 0x153B+ latch W - total + pretrigger, end only
//     untriggered captures and take no trigger after the stop. Reads 0x0A (done), 0x18 (triggered), 0x14 (13 bit).
//   - fw-c5 (0x153C, protocol 1.26): the trigger extension registers 0x5E (type, longer), 0x5F / 0x60 (u24 N / H),
//     0x1B (u32 hold-off, 5 ns), 0x66 (bit 0: the next 0x01 = 1 clears the hold-off, consumed), reads 0x34 and 0x37
//     (fpga/al3/AGENTS.md "0x153C C1 triggers"). The hold-off is modelled on the capture logic: a trigger taken while
//     the hold-off of the previous trigger runs is not taken (the edge passes, as on the FPGA); 0x1B = 0 releases a
//     running one at once; the timer survives arms unless 0x66 was written before the 0x01 = 1. The qualifier state
//     machines (pulse width, Nth edge, timeout) are not modelled: the model triggers at trig_at whatever the type, and
//     0x34 reports the reset state of the counters (edges to count = N - 1, counting = H == 0).
//   - fw-c7 (0x153C, C7 counter): 0x61 (gate, source), 0x62 (level, hysteresis), the trigger source copy 0x15, read 0x35
//     (16 bytes from a snapshot taken at the command byte, fpga/al3/AGENTS.md "0x153C C3 counter"). The stream is a
//     synthetic square wave per source (cnt_sig: period, high time and phase in ps, all multiples of the 5 ns clock, and
//     the low / high codes; period 0 = a constant low level): a gate of exactly 2e6 / 2e7 / 2e8 clocks publishes E, T, W
//     (the edges inside the gate: E = their count - 1, T = E x period, W = E x high time in clocks), min / max (the
//     codes for CH1 / CH2 when the Schmitt thresholds are crossed, 255 / 0 for EXT and before the first gate) and a
//     sequence number, and restarts; any change of the effective source or gate (0x61, or 0x15 with source 0)
//     restarts the gate without publishing, a same-value write does not. CH1 / CH2 see edges only when the low code is
//     below level - hysteresis and the high code at or above the level. The mixed-age transient of a 0x0D / 0x53 write
//     is not modelled (the firmware's rule is what the tests check).
// Every completed write is recorded in a trace (command, value) for the order checks, and passed to fpga_bus_note_write
// as fpga_control.c's trace does on the scope (fw 0.2.87: the values the FPGA holds, fpga_bus_wr.held).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef FPGA_MODEL_H
#define FPGA_MODEL_H

#include <stdint.h>

#define FM_RING_MAX   8192
#define FM_TRACE_MAX  64

typedef struct
{
  uint8_t  cmd;
  uint32_t value;
} fm_write;

typedef struct
{
  uint16_t version;
  uint32_t ring;                    //read pointer wrap (4096 stock, 8192 AL3, 6144 EF2)
  uint8_t  mem[FM_RING_MAX][4];     //lanes 0x20..0x23 of each word

  //bus
  uint8_t  cmd;                     //last command written
  uint8_t  nbytes;                  //data bytes since the command
  uint32_t acc;                     //big-endian accumulator of those bytes
  uint32_t rd_ptr;                  //0x1F read pointer
  uint32_t rd_q;                    //word latched by the last strobe (index into mem)
  uint32_t strobes;

  //time and the sampling reset
  uint64_t t_ps;                    //now, ps
  uint64_t next_edge_ps;            //next write clock edge
  uint32_t divider;                 //0x0D (legacy: write clock period 10 (D + 1) ns)
  uint8_t  in_reset;
  uint8_t  load_after_release;      //0x1537+: load 0x0B on the first edge after the release
  uint32_t edges_in_reset;
  uint16_t reg_0b, reg_0c;          //MCU-side registers
  uint8_t  reg_53, reg_54, reg_56;  //decimation mode, shift, FIR enable (fw 0.2.87 tests)
  uint16_t pre_applied;             //trigger read address preload actually used by the capture
  uint8_t  ready_never;             //0x05 never reads 1 (a hung FPGA)
  uint32_t ready_polls;

  //fw 0.2.97 capture logic (capture = 1 enables it)
  uint8_t  capture;
  uint32_t reg_0e;                  //auto time-out (words)
  uint8_t  reg_1a;                  //1 normal, 0 auto
  uint8_t  released;                //a capture runs or ran since the last release
  uint8_t  we;                      //write enable
  uint32_t n;                       //words written since the release
  uint32_t e;                       //write clock edges since the release
  int64_t  trig_at;                 //edge index of the trigger crossing (-1 none)
  uint8_t  triggered;               //0x18
  uint32_t trig_word;               //write index of the trigger (when it was taken while writing)
  uint16_t taddr;                   //0x14
  uint32_t stop_n;                  //W, words written at the stop

  //fw-c5 (0x153C): the trigger extension registers and the hold-off timer
  uint8_t  reg_5e;                  //type (bits 2..0), longer (bit 3)
  uint32_t reg_5f, reg_60;          //N, H (24 bits)
  uint32_t reg_1b;                  //hold-off, 5 ns units
  uint8_t  ho_clr_req;              //0x66 bit 0, consumed by the next 0x01 = 1
  uint64_t ho_until_ps;             //the hold-off of the last trigger ends here (0 = none)
  uint32_t ho_blocked;              //triggers not taken because the hold-off ran

  //fw-c7 (0x153C): the counter (fpga_model.h header)
  uint8_t  reg_15;                  //trigger source as written (0 CH1, 1 CH2, 2 EXT)
  uint8_t  reg_61;                  //gate bits 1..0, source bits 3..2
  uint8_t  reg_62_lvl, reg_62_hys;
  struct
  {
    uint64_t period_ps, high_ps, phase_ps;
    uint8_t  lo, hi;
  } cnt_sig[3];                     //CH1, CH2, EXT
  uint8_t  cnt_cfg;                 //{ 0, 0, 0, 0, ext, ch2, gate[1:0] } the gate runs with (0 = off)
  uint64_t cnt_start_ps, cnt_end_ps;//the running gate
  uint32_t cnt_publishes, cnt_restarts;
  uint32_t pub_e, pub_t, pub_w;     //published
  uint8_t  pub_min, pub_max, pub_seq, pub_cfg;
  uint8_t  snap[16];                //the 0x35 snapshot
  uint8_t  snap_seq;                //m_seq: the sequence number of the previous snapshot

  fm_write trace[FM_TRACE_MAX];
  uint32_t ntrace;
  uint32_t seed;
} fpga_model;

extern fpga_model fm;

void fm_reset(uint16_t version, uint32_t divider, uint32_t seed);
int  fm_holdoff_running(void);              //0x37 bit 4 as the model sees it now
void fm_fill(uint32_t salt);                //distinct bytes per word and lane
void fm_advance_ns(uint32_t ns);            //idle time (e.g. the capture after the release)
int  fm_trace_find(uint8_t cmd, uint32_t value, uint32_t from);  //index of the first matching write at or after from, -1
//fw-c7: the counter's stream on source src (0 CH1, 1 CH2, 2 EXT): a square wave of period_ns / high_ns (multiples of
//5 ns) starting at phase_ns, codes lo / hi; period 0 = a constant low level
void fm_cnt_signal(int src, uint64_t period_ns, uint64_t high_ns, uint64_t phase_ns, uint8_t lo, uint8_t hi);

#endif /* FPGA_MODEL_H */
