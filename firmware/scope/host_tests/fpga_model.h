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
//   - every bus strobe takes a random 41..298 ns (the testbench's strobe periods), so each arm starts at a random phase.
// Every completed write is recorded in a trace (command, value) for the order checks.
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
  uint16_t pre_applied;             //trigger read address preload actually used by the capture
  uint8_t  ready_never;             //0x05 never reads 1 (a hung FPGA)
  uint32_t ready_polls;

  fm_write trace[FM_TRACE_MAX];
  uint32_t ntrace;
  uint32_t seed;
} fpga_model;

extern fpga_model fm;

void fm_reset(uint16_t version, uint32_t divider, uint32_t seed);
void fm_fill(uint32_t salt);                //distinct bytes per word and lane
void fm_advance_ns(uint32_t ns);            //idle time (e.g. the capture after the release)
int  fm_trace_find(uint8_t cmd, uint32_t value, uint32_t from);  //index of the first matching write at or after from, -1

#endif /* FPGA_MODEL_H */
