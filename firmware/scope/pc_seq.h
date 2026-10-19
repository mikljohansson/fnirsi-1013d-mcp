//----------------------------------------------------------------------------------------------------------------------------------
// pc_seq.h - maths for the AL3 sequencer of bitstream 0x1536: segmented capture and hardware averaging
// (fpga/al3/README.md "Changes" section 6, docs/plans/segmented-capture.md, docs/protocol.md section 14 row 56).
//
// Integer only, no globals, no hardware: builds for the scope and on the host (host_tests/test_seq.c, seq_check.c).
//
// FPGA interface (fpga-gen5, src/FPGA commands.txt; multi-byte values high byte first):
//   writes, latched while 0x01 = 1 (the clk_200MHz side re-latches on every clock of the reset; the write-clock side
//   only on write-clock edges, so the firmware writes them before 0x01 = 1, see pc_hal_arm):
//   0x57 1 B  bits 1..0 mode (0 off, 1 segments, 2 averaging, 3 = off), bit 2 circular segments, bit 3 raw readout
//   0x58 2 B  segments (1 .. 8192 >> m) or averaging positions (1 .. 2048 / total), 12 bits
//   0x59 1 B  m: segment slot = 2^m words, 5..12
//   0x5A 2 B  N records per position (1..65535, 0 = 65536)
//   0x5B 1 B  S readout shift (live)
//   0x5C 4 B  hold-off in 5 ns units
//   0x5D 1 B  bit 0 stop: no further restarts (cleared by 0x01 = 1)
//   reads: 0x0A bit 0 = the run is done (while a run is armed); 0x2A 8 B status; 0x2B..0x32 byte 0..7 of the 64-bit view
//   word at the 0x1F read pointer, advancing it like 0x20..0x23 (one dummy strobe after 0x1F).
//
// A "word" is one memory word as for 0x0B/0x0C/0x14: one A/B pair per channel in legacy mode (K = 0), one 16-bit sample
// per channel with the decimation filter (K >= 1).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_SEQ_H
#define PC_SEQ_H

#include <stdint.h>

#define PC_SEQ_OFF            0
#define PC_SEQ_SEGMENTS       1
#define PC_SEQ_AVERAGE        2

#define PC_SEQ_F_CIRCULAR     0x01      //SET_SEQ flags bit 0 (segments only)
#define PC_SEQ_F_VALID        0x01      //all SET_SEQ flag bits

#define PC_SEQ_57_CIRCULAR    0x04      //0x57 bit 2
#define PC_SEQ_57_RAW         0x08      //0x57 bit 3

#define PC_SEQ_M_MIN          5
#define PC_SEQ_M_MAX          12
#define PC_SEQ_WORDS          8192      //sample memory
#define PC_SEQ_ACC_WORDS      2048      //accumulator (= segment table)
#define PC_SEQ_TAIL           2         //words written after a record window (measured): total <= 2^m - 2
#define PC_SEQ_AVG_MAX_TOTAL  2046
#define PC_SEQ_LEGACY_MAX_N   257       //legacy 16-bit lanes are exact up to 255 x 257 = 65535, then they clamp
#define PC_SEQ_MAX_N          65536u

//0x2A byte 0
#define PC_SEQ_ST_DONE        0x80
#define PC_SEQ_ST_OVERFLOW    0x40
#define PC_SEQ_ST_RUNNING     0x20
#define PC_SEQ_ST_FMT16       0x10
#define PC_SEQ_ST_RAW         0x08
#define PC_SEQ_ST_CIRCULAR    0x04
#define PC_SEQ_ST_MODE        0x03

//Why a setting does not fit the next arm's geometry (GET_SEQ byte 16; SET_SEQ E_BAD_ARG detail = the request field)
#define PC_SEQ_FIT_OK         0
#define PC_SEQ_FIT_MODE       1         //mode > 2
#define PC_SEQ_FIT_FLAGS      2         //unknown flag bits, or circular outside segment mode
#define PC_SEQ_FIT_M          3         //segments: m outside 5..12
#define PC_SEQ_FIT_COUNT      4         //count 0, or more segments than 8192 >> m, or positions x total > 2048
#define PC_SEQ_FIT_TOTAL      5         //segments: total > 2^m - 2; averaging: total > 2046
#define PC_SEQ_FIT_PRE        6         //pre >= total
#define PC_SEQ_FIT_N          7         //averaging: N 0 or > 65536, or > 257 with legacy (8-bit) records
#define PC_SEQ_FIT_PROG       8         //bitstream without programmable geometry / sequencer

typedef struct
{
  uint8_t  mode;                //PC_SEQ_OFF / SEGMENTS / AVERAGE
  uint8_t  flags;               //PC_SEQ_F_*
  uint8_t  m;                   //segments: slot = 2^m words
  uint16_t count;               //segments, or averaging positions
  uint32_t n;                   //averaging: records per position, 1..65536
  uint32_t holdoff_ns;          //wait after each segment / record before the restart
} pc_seq_cfg;

typedef struct
{
  uint8_t  flags;               //0x2A byte 0 (PC_SEQ_ST_*)
  uint8_t  state;               //sequencer state, bits 15..13 of bytes 6..7
  uint16_t index;               //next slot or position
  uint32_t count;               //segments completed / records accumulated (24 bit)
  uint16_t untriggered;         //saturating
} pc_seq_status;

typedef struct
{
  uint64_t timestamp;           //5 ns units since the release of 0x01 (40 bit)
  uint16_t offset;              //trigger_address mod 2^m (the 0x14 convention)
  uint8_t  triggered;
  uint8_t  valid;
} pc_seq_entry;

//Fit of a setting against the geometry of the next arm (words) and its record format; PC_SEQ_FIT_*
uint8_t  pc_seq_fit(const pc_seq_cfg *c, uint32_t pre_words, uint32_t total_words, int wide);
//Largest count for the mode: 8192 >> m segments, or 2048 / total positions (0 when total is 0 or over the limit)
uint32_t pc_seq_max_count(const pc_seq_cfg *c, uint32_t total_words);
//Hold-off in 5 ns units, rounded up
uint32_t pc_seq_holdoff_units(uint32_t ns);
//The 0x57 byte (mode, circular, raw), 0x58 and 0x5A values
uint8_t  pc_seq_cmd57(const pc_seq_cfg *c, int raw);
uint16_t pc_seq_cmd58(const pc_seq_cfg *c);
uint16_t pc_seq_cmd5a(const pc_seq_cfg *c);
//Table slots that may hold entries: count, or 8192 >> m for a circular run
uint32_t pc_seq_slots(const pc_seq_cfg *c);

void     pc_seq_status_decode(const uint8_t raw[8], pc_seq_status *st);
void     pc_seq_entry_decode(uint64_t e, pc_seq_entry *d);
uint64_t pc_seq_entry_encode(const pc_seq_entry *d);       //host tests and the mock

//Segment s: physical word of record index j (0 .. total - 1), from the table offset and the pretrigger words written
uint32_t pc_seq_seg_word(uint8_t m, uint32_t s, uint16_t offset, uint32_t pre_words, uint32_t j);

//----------------------------------------------------------------------------------------------------------------------------------
// UI hardware averaging (the acquisition menu's Average with a 0x1536 bitstream)

//Records per UI run budget and limits
#define PC_SEQ_UI_BUDGET_US   40000u    //N records plus their dead time fit in this
#define PC_SEQ_UI_MAX_N       64u
#define PC_SEQ_UI_MIN_N       2u

//Records for one UI averaging run of a legacy record (total / pre words, divider D: one word = 10 (D + 1) ns): the largest
//power of two N in 2..64 with N x (record + dead time) <= the budget; 0 = do not use the hardware (record too long for
//the accumulator, or for two records in the budget). *run_us = N x (record + dead time), rounded up.
uint32_t pc_seq_ui_plan(uint32_t total_words, uint32_t pre_words, uint32_t divider, uint32_t *run_us);
//Dead time per averaged record in ns: post-pass (total x 5 ns + 40 ns) + restart (2 words) + pretrigger fill + 100 ns
uint32_t pc_seq_dead_ns(uint32_t total_words, uint32_t pre_words, uint32_t word_ns);

//Exact rounded mean for the readout: r = pc_seq_recip(c) for 1 <= c <= 256; pc_seq_udiv(x, c, r) = floor(x / c) for every
//x < 2^24 (Granlund-Montgomery: x (r c - 2^32) < 2^32)
uint32_t pc_seq_recip(uint32_t c);
static inline uint32_t pc_seq_udiv(uint32_t x, uint32_t c, uint32_t r)
{
  return (c == 1) ? x : (uint32_t)(((uint64_t)x * r) >> 32);
}

//Corrected 8-bit code of an averaged legacy lane: the lane sum of c records (each a raw core code 0..255) through the
//core's correction (adc_cal.h: code = (raw x gain + off) / 65536), from the mean in Q8, rounded, clamped 0..255
int32_t  pc_seq_avg_code(uint32_t sum, uint32_t c, uint32_t r, int32_t gain, int32_t off);

#endif /* PC_SEQ_H */
