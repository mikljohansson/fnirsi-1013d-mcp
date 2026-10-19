//----------------------------------------------------------------------------------------------------------------------------------
// fpga_bus.h - the MCU-FPGA bus sequences every reader and every arm shares (fw 0.2.70, firmware/AGENTS.md section 46):
// the readout start after a 0x1F pointer write and the sampling reset (arm). Per bitstream contract from the version word.
// Builds for the scope and on the host (host_tests/test_fpga_bus.c drives it against fpga_model.c, a bus-level model of
// zaklad.v's MCU interface; -DPC_HOST_TEST there).
//
// Readout (fpga/al3/README.md "Changes" section 7 finding 5, section 9): the read port of the sample memory latches on
// i_mcu_clk and the read pointer advances on each read strobe, so after 0x1F = S and a read command the strobes return
// mem[S], mem[S], mem[S + 1], ...: the first byte is a stale pipeline byte and the rest are one word late unless one
// dummy strobe is made first. The 1.00o5 readouts (the UI trace, READ_TRACE, the 16-bit trace) made none; READ_RAW, the
// sequencer view and the encode data check always did. Since fw 0.2.70 every reader starts through fpga_read_start.
//
// Arm (section 7 finding 2): on 0x1532..0x1536 the trigger read address (0x0B) and the segment slot mask load only on
// write clock edges while 0x01 = 1. Written after the 0x05 poll, a new value is lost when no write clock edge falls between
// the write and the release (slow legacy rates: 2 us per edge at 1 MSa/s, 20 us at 100 kSa/s), and the record start moves
// by (old - new pretrigger) words. 0x1537+ also load them on the first edge after the release. fpga_arm_reset writes the
// geometry before 0x01 = 1 as well (the write clock edges the 0x05 poll waits for load it), on every bitstream that takes
// it, and again inside the window as 1.00o5 did.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef FPGA_BUS_H
#define FPGA_BUS_H

#include <stdint.h>

//What a bitstream needs on the bus, from its version word (fpga_bus_contract_for)
typedef struct
{
  uint8_t read_dummy;       //strobes to discard after 0x1F + a read command before the first byte of start_word
  uint8_t prog;             //0x0B / 0x0C programmable (pretrigger and total words): AL3, EF2, EP4
  uint8_t geometry_early;   //0x0B / 0x0C written before 0x01 = 1 as well (every prog bitstream)
} fpga_bus_contract;

//Known bitstreams, with the source of each rule:
//  0x1432 stock: read_dummy 1 (stale first byte measured on the 1014D stock FPGA, research/notes/repo-donwulff.md);
//                fixed geometry, nothing written to 0x0B / 0x0C.
//  0x1532..0x153F AL3: read_dummy 1 (zaklad.v read register; fpga-gen6 simulation; 0x1539 keeps the lag by design,
//                README section 9); geometry early: needed on 0x1532..0x1536, harmless on 0x1537+.
//  0x1632 EF2, 0x1732 EP4: the same Atlan4 read and reset structure (inference, not benched).
//  other: treated as stock (protocol.md 5.1).
void fpga_bus_contract_for(uint16_t version, fpga_bus_contract *c);

//The running bitstream's contract (fpga_bus_init from fpga_get_version; before that: stock)
extern fpga_bus_contract fpga_bus;
void fpga_bus_init(uint16_t version);

//0x1F = start_word, the read command (0x20..0x23 sample lanes, 0x2B..0x32 sequencer view), the dummy strobes. The bus is
//left in data-read direction: the next strobe returns the byte of start_word, each further one the next word's.
void fpga_read_start(uint8_t cmd, uint16_t start_word);

//count bytes of one lane from ring word start, dst advancing by stride; restarts at the ring end instead of relying on
//the FPGA pointer wrap (EF2 wraps at 6144)
void fpga_read_lane(uint8_t cmd, uint32_t start, uint32_t count, uint8_t *dst, uint32_t stride, uint32_t ring_words);

//First word of a record: (trigger address 0x14 - the pretrigger words written to 0x0B) mod ring (the UI readout and the
//encode data check; READ_RAW / the host capture use the same rule in pc_cmds.c)
uint32_t fpga_ring_start(uint32_t addr, uint32_t pre_words, uint32_t ring_words);

//16-bit layout: count samples from ring word start, low bytes from cmdlo (0x20 / 0x22), high bytes from cmdhi (0x21 / 0x23)
void fpga_read_decim_data(uint8_t cmdlo, uint8_t cmdhi, uint32_t start, uint32_t count, uint16_t *dst);

//The sampling reset of one capture
typedef struct
{
  uint8_t  geometry;        //write 0x0B / 0x0C (the bitstream takes them: fpga_bus.prog)
  uint8_t  write_0e;        //write 0x0E inside the window
  uint16_t pre_words;       //0x0B
  uint16_t total_words;     //0x0C
  uint32_t auto_timeout;    //0x0E
  uint32_t max_polls;       //0x05 polls before giving up (0x01 = 0 written, -1 returned); 0 = wait forever (1.00o5)
  void     (*idle)(void);   //called every 65536 polls of an unbounded wait (USB BUSY answers); may be 0
} fpga_arm_cfg;

//Geometry (early), 0x01 = 1, the 0x05 ready wait, geometry again, 0x0E, 0x01 = 0. 0 = released, -1 = ready never came
int  fpga_arm_reset(const fpga_arm_cfg *c);

//----------------------------------------------------------------------------------------------------------------------------------
// Bus primitives: fpga_control.c / fpga_control.h on the scope, fpga_model.c in the host test

#ifdef PC_HOST_TEST
void    fpga_write_cmd(uint8_t command);
void    fpga_write_byte(uint8_t data);
void    fpga_write_short(uint16_t data);
void    fpga_write_int(uint32_t data);
uint8_t fpga_read_byte(void);
void    fpga_io_read_mode(void);          //bus in, data read (no strobe)
uint8_t fpga_io_strobe_read(void);        //one strobe, the port read after it
#else
#include "fpga_control.h"
static inline void    fpga_io_read_mode(void)   { FPGA_BUS_DIR_IN(); FPGA_DATA_READ(); }
static inline uint8_t fpga_io_strobe_read(void) { FPGA_PULSE_CLK(); return (uint8_t)FPGA_GET_DATA(); }
#endif

#endif /* FPGA_BUS_H */
