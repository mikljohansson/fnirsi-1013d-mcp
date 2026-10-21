//----------------------------------------------------------------------------------------------------------------------------------
// enc_heal.h - the full-rate capture check with bounded re-arm (fw 0.2.61, per capture since fw 0.2.62; fpga/al3/README.md
// "Findings" item 3 and section 8, firmware/AGENTS.md sections 37, 38 and 48). Pure C, no globals, host-buildable
// (host_tests/test_enc_heal.c, test_session.c); the target glue (UI and calibration conversions, counters, event 21, the
// 0x33 read) is enc_heal_hw.c, the host ARM path is pc_acq_service in pc_cmds.c.
//
// The built-for bitstream (0x153B, PC_FW_FPGA_VERSION) derives the ADC encodes statelessly from the write clock and
// checks them itself: FPGA read 0x33 (since 0x1538). Two ways to check one finished full-rate (divider 0, so legacy
// 8-bit) capture:
//   EH_MODE_FLAG: read 0x33; bit 0 set = an encode check failed since the last 0x01 = 1.
//   EH_MODE_DATA: EH_CHECK_PAIRS words of each channel from the record start, per channel the RMS of core B's samples
//     against the mean of their two core A neighbours (both neighbour pairs, the smaller one: it does not depend on the
//     core order). A signal well under 100 MHz cancels, the cores' offset difference cancels with the mean, the ADC noise
//     leaves about 1.3 codes; a garbled core leaves tens. Above EH_LIMIT16 the capture is bad. Used for a capture with a
//     rate / filter write after its sampling reset (0x33 bit 0 is sticky until the next 0x01 = 1 and such a write can
//     make one prediction miss, fw 0.2.72) and when the boot 0x33 reported an encode convention the firmware does not know.
// A bad capture is discarded and the conversion re-armed, at most EH_MAX_TRIES times per capture; then the last one is
// accepted and flagged.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef ENC_HEAL_H
#define ENC_HEAL_H

#include <stdint.h>

#define EH_LIMIT16      (8 * 16)    //data check: residual RMS per channel, 1/16 code (bench: 1.3 good, 14-34 codes bad)
#define EH_MAX_TRIES    16          //re-arms per capture (0x1537 at 40 % bad: 0.4^17 of captures would stay bad)
#define EH_CHECK_PAIRS  250         //data check: words per channel read from the record start (1000 bus bytes)

//Check modes (pc_hw_enc.mode)
#define EH_MODE_NONE    0           //(never the bitstream's mode since fw-one; a divider >= 1 is not checked)
#define EH_MODE_DATA    1           //residual of the record
#define EH_MODE_FLAG    2           //FPGA read 0x33

//FPGA read 0x33 (the 0x33 write is the CH1 relays, unchanged)
#define EH_33_FAILED    0x01        //sticky: an encode check failed since the last 0x01 = 1
#define EH_33_PREDICT   0x02        //the last write-clock prediction was right
#define EH_33_CONV_MASK 0x0C        //encode convention
#define EH_33_CONV_1    0x04        //convention 1: core order guaranteed, core B the earlier sample (PC_INTERLEAVE_ORDER 0)
#define EH_33_PIN_CH1B  0x10        //last pin check, 1 = right
#define EH_33_PIN_CH1A  0x20
#define EH_33_PIN_CH2B  0x40
#define EH_33_PIN_CH2A  0x80
#define EH_33_GOOD      0xF6        //what a good capture reads

//Event type 21 (protocol 1.19, row 58): a = reason << 5 | re-arms (bit 7: not recovered), b = caller << 12 | value
#define EH_WHY_RATE     0           //0.2.61 check after a 0x0D write (retired in 0.2.62)
#define EH_WHY_DATA     1           //data check of a capture (value = the larger residual, 1/16 code)
#define EH_WHY_FLAG     2           //0x33 bit 0 after a capture (value = the 0x33 byte)
#define EH_WHY_CONV     3           //boot: 0x33 bits 3:2 are not 01 (value = the 0x33 byte)

//0x33 bits 3:2 say convention 1 (order guaranteed, B first)
int      eh_conv_ok(uint8_t s33);

//The check for one capture (fw 0.2.72): mode (enc_heal_hw.c: FLAG, or DATA after a bad boot convention), unless a 0x0D /
//0x53 / 0x56 write came after the capture's sampling reset (written, fpga_bus.h): such a write can make one encode
//prediction miss and 0x33 bit 0 stays set until the next 0x01 = 1, so that capture is judged by its data instead (bench
//0x1538, fw 0.2.64: 112 of 5798 captures re-armed after host rate writes, none garbled)
int      eh_capture_mode(int mode, int written);

//Residual RMS (1/16 code) of one channel's capture in the trace layout: buf[2i] core B, buf[2i + 1] core A, n pairs.
//The smaller of the two neighbour choices (B against A[i-1], A[i] or against A[i], A[i+1])
uint32_t eh_residual16(const uint8_t *buf, uint32_t n);

//What a check needs from the target (or a host mock)
typedef struct
{
  void    *ctx;
  uint8_t (*status)(void *ctx);                                     //FPGA read 0x33
  void    (*read)(void *ctx, uint8_t core, uint32_t start, uint32_t count, uint8_t *dst, uint32_t stride);  //0x20 + core
} eh_io;

//Check one finished capture: 1 = bad. start: the record's first word, words: its length (ring wrap is the reader's),
//*value: the 0x33 byte (FLAG) or the larger residual (DATA)
int      eh_check(const eh_io *io, int mode, uint32_t start, uint32_t words, uint32_t *value);

//Re-arm bookkeeping of one capture stream (UI conversions, host ARM)
typedef struct
{
  uint8_t  tries;           //re-arms made for the capture in progress
  uint32_t first;           //value of its first bad check
} eh_cap;

typedef struct
{
  uint8_t  bad;             //this check failed
  uint8_t  first;           //... and it is the capture's first failure (GET_LINK_STATS 62)
  uint8_t  done;            //the capture was accepted after at least one failure (event 21)
  uint8_t  ok;              //... with its last check passed (recovered, 63)
  uint8_t  tries;           //re-arms made
  uint32_t value;           //the first bad check's value
} eh_note;

#define EH_ACCEPT       0
#define EH_REARM        1

void     eh_cap_reset(eh_cap *c);
//After a check: EH_REARM (discard, re-arm) while bad and tries < EH_MAX_TRIES, else EH_ACCEPT; *n says what to count
int      eh_cap_step(eh_cap *c, int bad, uint32_t value, eh_note *n);

//Event 21 fields
uint8_t  eh_event_a(uint8_t why, uint8_t tries, int ok);
uint16_t eh_event_b(uint8_t who, uint32_t value);

//Target (enc_heal_hw.c)
void     fpga_encode_init(void);                       //pc_glue_init: the 0x33 convention check
int      fpga_encode_after_capture(void);              //fpga_done_conversion on done: 1 = discarded and re-armed
void     fpga_encode_new_conversion(void);             //fpga_do_conversion: a new capture (not a re-arm)
extern uint8_t fpga_enc_garbled;                       //a capture was accepted bad since cleared (calibration wizard)

#endif /* ENC_HEAL_H */
