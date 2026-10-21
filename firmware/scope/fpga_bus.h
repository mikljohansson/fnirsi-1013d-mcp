//----------------------------------------------------------------------------------------------------------------------------------
// fpga_bus.h - the MCU-FPGA bus sequences every reader and every arm shares (fw 0.2.72, firmware/AGENTS.md section 48):
// the readout start after a 0x1F pointer write and the sampling reset (arm). One contract: the built-for AL3 bitstream's
// (PC_FW_FPGA_VERSION, pc_cmds.h). Builds for the scope and on the host (host_tests/test_fpga_bus.c drives it against
// fpga_model.c, a bus-level model of zaklad.v's MCU interface; -DPC_HOST_TEST there).
//
// Readout (fpga/al3/README.md "Changes" section 7 finding 5, section 9): the read port of the sample memory latches on
// i_mcu_clk and the read pointer advances on each read strobe, so after 0x1F = S and a read command the strobes return
// mem[S], mem[S], mem[S + 1], ...: the first byte is a stale pipeline byte and the rest are one word late unless one
// dummy strobe is made first. The 1.00o5 readouts (the UI trace, READ_TRACE, the 16-bit trace) made none; READ_RAW, the
// sequencer view and the encode data check always did. Since fw 0.2.72 every reader starts through fpga_read_start.
//
// Arm (section 7 finding 2): the trigger read address (0x0B) and the segment slot mask load on write clock edges while
// 0x01 = 1 and (since 0x1537) on the first edge after the release. fpga_arm_reset writes the geometry before 0x01 = 1
// (the write clock edges the 0x05 poll waits for load it) and again inside the window as 1.00o5 did; harmless on the
// built-for bitstream, and the one order every arm uses.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef FPGA_BUS_H
#define FPGA_BUS_H

#include <stdint.h>

//The built-for bitstream's bus contract: one dummy strobe after 0x1F + a read command before the first byte of start_word
//(zaklad.v read register; fpga-gen6 simulation; 0x1539 keeps the lag by design, fpga/al3/README.md section 9). A future
//bitstream that drops the lag changes this constant with PC_FW_FPGA_VERSION.
#define FPGA_READ_DUMMY   1

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
  uint8_t  write_0e;        //write 0x0E inside the window
  uint16_t pre_words;       //0x0B
  uint16_t total_words;     //0x0C
  uint32_t auto_timeout;    //0x0E
  uint32_t max_polls;       //0x05 polls before giving up (0x01 = 0 written, -1 returned); 0 = wait forever (1.00o5)
  void     (*idle)(void);   //called every 65536 polls of an unbounded wait (USB BUSY answers); may be 0
} fpga_arm_cfg;

//Geometry (early), 0x01 = 1, the 0x05 ready wait, geometry again, the trigger type registers (fpga_trig_arm), 0x0E,
//0x01 = 0. 0 = released, -1 = ready never came
int  fpga_arm_reset(const fpga_arm_cfg *c);

//----------------------------------------------------------------------------------------------------------------------------------
// Trigger types and hold-off (FPGA 0x153C, fw-c5, protocol 1.26; docs/plans/fpga-0x153c.md sections 2.1, 3, 3.6; the RTL
// semantics in fpga/al3/AGENTS.md "0x153C C1 triggers"). Registers: 0x5E type (bits 2..0: 0 edge, 1 pulse width, 2 Nth
// edge, 3 timeout; 4 runt and 5 window are 0x153E and act as edge on 0x153C) with bit 3 "longer than N"; 0x5F u24 N;
// 0x60 u24 H (Nth edge idle time, 0 = count from the arm); 0x1B u32 hold-off in 5 ns clocks (0 = off: from the trigger
// of a record no trigger is accepted until it elapsed, across sequencer restarts and MCU re-arms; 0 written releases a
// running one at once); 0x66 bit 0 = 1: the next 0x01 = 1 also clears the hold-off timer (one-shot, consumed). 0x16 is
// the polarity per type and stays the UI's trigger edge (fpga_set_trigger_edge). Reads: 0x34 (3 bytes: { 0, 0,
// counting, pulse began, EXT, longer, type[1:0] }, u16 edges still to count) and 0x37 ({ 0, 0, 0, hold-off running,
// type bit 2, 0, 0, 0 }), asynchronous.
//
// The firmware holds one copy (fpga_trig): SET_TRIGGER2 / SET_TRIGGER's hold-off write it through fpga_trig_write (the
// registers at once, like 0x15..0x17, plus 0x66 = 1 so the next arm starts a clean hold-off: a new trigger definition
// is a new session; re-arms between writes keep the hold-off running, which is what skips the pulses of a train), and
// every arm re-writes the type registers inside the 0x01 = 1 window (fpga_trig_arm, after the geometry: the qualifier
// is held in reset until the first write clock edge after the release, so they apply at every rate), which also puts
// a FEL-loaded firmware's FPGA into the state the firmware believes. N and H are samples of the trigger stream (CH1 /
// CH2: one per memory word; EXT: 5 ns clocks); the firmware converts nothing.

#define FPGA_TRIG_EDGE     0
#define FPGA_TRIG_PULSE    1
#define FPGA_TRIG_NTH      2
#define FPGA_TRIG_TIMEOUT  3
#define FPGA_TRIG_N_MAX    0xFFFFFFu   //0x5F / 0x60 are 24 bits
#define FPGA_TRIG_NTH_MAX  0xFFFFu     //Nth edge counts bits 15..0 of N

typedef struct
{
  uint8_t  type;            //0x5E bits 2..0
  uint8_t  longer;          //0x5E bit 3 (pulse width: 1 = longer than N, 0 = shorter)
  uint32_t n;               //0x5F (24 bits)
  uint32_t h;               //0x60 (24 bits)
  uint32_t holdoff;         //0x1B, 5 ns units, 0 = off
} fpga_trig_ext;

extern fpga_trig_ext fpga_trig;      //what the firmware holds; power-on / boot: all 0 (the edge type, no hold-off)

//0x5E, and with a type other than edge 0x5F and 0x60: the writes of every arm, inside the 0x01 = 1 window
void fpga_trig_arm(void);
//Take t: the type registers, 0x1B, and 0x66 = 1 when clear (the next 0x01 = 1 clears a running hold-off)
void fpga_trig_write(const fpga_trig_ext *t, int clear);
//Read 0x37 into out[0] and the 3 bytes of 0x34 into out[1..3] (6 strobes)
void fpga_trig_status(uint8_t out[4]);

//----------------------------------------------------------------------------------------------------------------------------------
// Writes that set the capture state (fw 0.2.72): 0x0D rate, 0x53 / 0x56 filter, 0x01 = 1 sampling reset. Counted
// (GET_LINK_STATS 64-66; the per-write event 22 is a debug level since 0.2.72, fpga_trace_level in fpga_control.c), and
// a rate or filter write after the last sampling reset is remembered: FPGA 0x1538+ read 0x33 bit 0 is sticky until the
// next 0x01 = 1, and such a write can make one encode prediction miss (fpga/al3/README.md section 8), so the capture in
// flight cannot be judged by it (bench 0x1538, fw 0.2.64: 112 of 5798 captures re-taken after host rate writes).
//
// fw 0.2.87 (fw-decfix, firmware/AGENTS.md section 57): also the decimator shift 0x54 and the 16-bit level 0x55 (counted
// with the filter writes and traced as event 22; they do not touch the encode clocks, so since_reset stays as it was), and
// the values the FPGA holds: the last 0x0D, 0x53, 0x54 and 0x56 written by anyone. This is the one place the firmware
// knows the divider the FPGA runs at (pc_hal_divider_now): a UI time/div snap that writes no 0x0D cannot change it.

#define FPGA_HELD_DIVIDER  0x01    //held.divider is valid (a 0x0D was written since boot)
#define FPGA_HELD_MODE     0x02    //0x53
#define FPGA_HELD_SHIFT    0x04    //0x54
#define FPGA_HELD_FIR      0x08    //0x56

typedef struct
{
  uint32_t rate;            //0x0D writes
  uint32_t filter;          //0x53, 0x54, 0x55 and 0x56 writes (0x54 / 0x55 since fw 0.2.87)
  uint32_t resets;          //0x01 = 1 writes
  uint8_t  since_reset;     //a 0x0D / 0x53 / 0x56 write since the last 0x01 = 1

  //What the FPGA holds (fw 0.2.87): the last value written to each register, valid per FPGA_HELD_* bit
  uint8_t  held;
  uint8_t  mode;            //0x53
  uint8_t  shift;           //0x54
  uint8_t  fir;             //0x56
  uint32_t divider;         //0x0D
} fpga_bus_writes;

extern fpga_bus_writes fpga_bus_wr;

//Called with every completed write (command, value). 1 = one of the writes above (event 22 material)
int  fpga_bus_note_write(uint8_t cmd, uint32_t value);

//Decimation registers before a sampling reset (fw 0.2.87, from pc_dec_apply): 0x53 = mode and, with has_fir, 0x56 =
//fir_en when they differ from what the FPGA holds; with K = mode bits 1:0 >= 1, 0x54 = shift at every call, so a shift
//written for another divider (or lost, or written by someone else) cannot survive an arm. 1 = the filter must settle:
//a value differs from what the FPGA held, or the divider changed since the previous call.
int  fpga_decim_write(uint8_t mode, int has_fir, uint8_t fir_en, uint8_t shift);

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
