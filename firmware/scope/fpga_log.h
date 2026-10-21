//----------------------------------------------------------------------------------------------------------------------------------
// fpga_log.h - the FPGA 0x153D streaming logger's bus side (fw-d4, item D4 of docs/plans/fpga-0x153c.md sections 2.1, 2.2,
// 6; protocol 1.27 row 67). Host-buildable against fpga_bus.h's primitives (host_tests/test_log.c drives it against
// fpga_model.c).
//
// Registers (contract 2.1 / 2.2): 0x69 bit 0 stream on (the capture never stops and never triggers; the arm is the normal
// 0x01 = 1 / 0 with 0x0B = 0x0C = 0; decimator, filter, peak and the readout as in any capture), bit 1 clear the lap flag
// (self-clearing); 0x6A u13 reader position; read 0x38 = 8 bytes from one snapshot at the command byte: { streaming, lap,
// 0, W[12:8] }, W[7:0] (the next word to be written), u32 words written since 0x01 = 0, u16 words since the reader
// position.
//
// How the firmware drives them (fpga-d1's RTL as built and checked by tb_stream.v, 2026-10-21):
// - Start: 0x69 = 1, 0x57 = 0 (sequencer off), 0x1A = 0 (auto), the sampling reset (fpga_arm_reset: 0x0B = 0x0C = 0,
//   0x0E = 0 in the window). 0x01 = 1 sets the FPGA's reader, unread count and word counter to 0 (not the lap flag).
//   Then a 0x38 snapshot: base = (W - count) mod 8192 is the ring address of word 0 (0 on the RTL: W == count mod 8192
//   at every snapshot). Bit 7 clear = the bitstream does not stream (0x153C and older read the stale bus byte): refused.
//   Then 0x6A = base, then 0x69 = 3 (stream on, a lap flag of an earlier session cleared).
// - 0x6A holds the NEXT word to read ((base + consumed) mod 8192). The FPGA's unread count (0x38 bytes 6..7) becomes
//   (W - 0x6A) mod 8192 at a 0x6A write and counts every write up to 8192 (full); a write at 8192 sets the lap flag (it
//   overwrote the oldest unread word). It never fires on an empty ring.
// - Drain (fpga_log_drain, one step of the log loop): a snapshot, then up to FPGA_LOG_CHUNK words from (base + consumed)
//   in one pass per lane (0x1F, the read command, the dummy strobe: fpga_read_lane, as READ_RAW) straight into the DDR
//   ring at the word's byte offset (stride = bytes per word), then a second snapshot: the chunk is kept only when the lap
//   flag is clear and the count is at most consumed + FPGA_LOG_SAFE (the writer has not come round to the chunk's first
//   word while the lanes were read; 16 words of margin for the write pipeline), else dropped. Then 0x6A. The second
//   snapshot is the next step's first, so a step costs one 0x38 read (9 strobes), the lanes (5 + n strobes each) and the
//   0x6A write (3 strobes).
// - A gap (lap flag, or the count more than FPGA_LOG_SAFE ahead): the words between consumed and the count are lost;
//   consumed jumps to the count, 0x6A follows, 0x69 = 3 clears the flag, and the next chunk carries PC_LOG_CF_GAP (plus
//   PC_LOG_CF_HOST when the DDR ring was full since the last append: the host did not read in time). The log continues.
// - A full DDR ring: nothing is read (the words wait in the FPGA ring, 16 ms at the fastest tier); the gap follows if the
//   host does not read in time.
// - Duration: the drain never reads past `limit` words and ends there (FPGA_LOG_ENDED, 0x69 = 0).
// - Stop (LOG_STOP): 0x69 = 0; in auto mode with 0x0E = 0 the FPGA stops writing at the next write clock edge (W and the
//   count freeze), and the words written up to there are read like any chunk (the tail).
//
// Wire layout of a word (protocol row 67, agreed with mcp-d6): the enabled channels in order CH1, CH2; per channel
// format 0 (legacy 8-bit, two samples per word) core B then core A (lanes 0x21, 0x20 / 0x23, 0x22: time order,
// PC_INTERLEAVE_ORDER 0), format 1 (filtered, the high byte) lane 0x21 / 0x23, format 2 (filtered 16-bit) u16
// little-endian (0x20 low, 0x21 high / 0x22, 0x23), format 3 (peak pairs) min then max (0x20, 0x21 / 0x22, 0x23).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef FPGA_LOG_H
#define FPGA_LOG_H

#include <stdint.h>

#include "pc_logring.h"

#define FPGA_LOG_RING        8192u        //FPGA ring words (13-bit addresses)
#define FPGA_LOG_CHUNK       2048u        //words per drain step at most (the contract's chunk: 4 ms at 500 kwords/s)
#define FPGA_LOG_SAFE        (FPGA_LOG_RING - 16u)

//Formats (LOG_START byte 1)
#define FPGA_LOG_F_LEGACY    0            //8-bit raw samples, two per word per channel
#define FPGA_LOG_F_HIGH      1            //filtered word, the high byte
#define FPGA_LOG_F_16        2            //filtered word, u16 little-endian
#define FPGA_LOG_F_PEAK      3            //peak-detect pair, min then max
#define FPGA_LOG_F_COUNT     4

//States (the LOG response byte 0)
#define FPGA_LOG_IDLE        0
#define FPGA_LOG_RUNNING     1
#define FPGA_LOG_ENDED       2            //the duration was reached
#define FPGA_LOG_STOPPED     3            //LOG_STOP
#define FPGA_LOG_ERROR       4            //the FPGA stopped streaming, or the host went away (pc_cmds.c)

//Flags (the LOG response byte 3)
#define FPGA_LOG_FL_LAP      0x01         //a gap happened since the start
#define FPGA_LOG_FL_DDR      0x02         //a gap was caused by the DDR ring being full

typedef struct
{
  pc_logring ring;
  uint8_t  ch_mask;             //bit 0 CH1, bit 1 CH2
  uint8_t  format;
  uint8_t  nlanes;              //= bytes per word
  uint8_t  lanes[4];            //read command per byte offset within a word
  uint8_t  state;
  uint8_t  flags;               //FPGA_LOG_FL_*
  uint8_t  pend;                //PC_LOG_CF_* flags for the next append (a gap seen, not yet followed by data)
  uint8_t  ddr_full;            //the DDR ring refused words since the last append
  uint8_t  fresh;               //snap was taken after the last chunk (the next step need not read 0x38 again)
  uint16_t base;                //ring address of word 0
  uint64_t consumed;            //words taken (the counter of the next word to read), 64-bit
  uint64_t count;               //the FPGA's word counter at the last snapshot, extended to 64 bits
  uint64_t limit;               //words to log, 0 = until stopped
  uint64_t lost;                //words lost in gaps
  uint32_t gaps;
  uint32_t steps, chunks;       //drain steps, chunks kept
  uint8_t  snap[8];             //the last 0x38 snapshot
} fpga_log;

//Bytes per word for a channel mask and a format (1, 2 or 4), 0 when invalid
uint8_t  fpga_log_bpw(uint8_t ch_mask, uint8_t format);

//Arm the stream: the rate and decimation registers must be written already. ring / size: the DDR ring. max_polls: the
//bounded 0x05 wait (fpga_arm_cfg). 0 running, -1 the FPGA never became ready, -2 the bitstream does not stream (0x38
//bit 7 clear after the arm): 0x69 = 0 written, state FPGA_LOG_ERROR
int      fpga_log_start(fpga_log *l, uint8_t ch_mask, uint8_t format, uint64_t limit, uint8_t *ring, uint32_t size,
                        uint32_t max_polls);
//One step of the log loop; returns the words appended to the DDR ring
uint32_t fpga_log_drain(fpga_log *l);
//0x69 = 0 and the state (FPGA_LOG_STOPPED / _ERROR); with STOPPED the tail is read too; nothing when not running
void     fpga_log_stop(fpga_log *l, uint8_t state);
//The 0x38 snapshot into l->snap and l->count
void     fpga_log_snapshot(fpga_log *l);

#endif /* FPGA_LOG_H */
