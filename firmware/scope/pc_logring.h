//----------------------------------------------------------------------------------------------------------------------------------
// pc_logring.h - the streaming logger's DDR ring (fw-d4, item D4 of docs/plans/fpga-0x153c.md section 6.3, protocol 1.27
// row 67). Pure C, no hardware, no globals: the scope keeps one in pc_hal_scope.c over the 8 MiB `.logring` section
// (fnirsi_1013d.ld), the host tests over small buffers (host_tests/test_log.c, mock_hal.c).
//
// What it holds: whole FPGA ring words in the logger's wire layout (bytes per word `bpw`: 1, 2 or 4), oldest first, in a
// byte ring of `size` bytes (a power of two, a multiple of 4, so a word never straddles the end). One producer (the drain,
// fpga_log.c) appends words with the FPGA word counter of the first one; one consumer (LOG_READ) takes chunks.
//
// Segments: a run of words with consecutive counters. An append whose first counter is not the one the ring expects, or
// that carries gap flags (a lap of the FPGA ring, the ring having been full), opens a new segment that remembers its byte
// position, its first counter and its flags. A chunk never crosses a segment boundary, so every chunk is contiguous in
// time and its header's counter (fpga_words_at_start) is exact; the first chunk of a segment carries the segment's flags.
// At most PC_LOGRING_SEGS segments are unread at a time; an append that would need one more is refused (room 0), which the
// drain treats like a full ring (the words stay in the FPGA ring and become a gap if the host does not read in time).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_LOGRING_H
#define PC_LOGRING_H

#include <stdint.h>

#define PC_LOGRING_SEGS        64           //unread segments (gaps) held at most; a power of two

//Chunk / segment flags: the LOG_READ header byte 12 bits 1 and 3 (protocol row 67)
#define PC_LOG_CF_RUNNING      0x01         //the log runs (set by the session, not stored here)
#define PC_LOG_CF_GAP          0x02         //words are missing before this chunk: the counter jumps (or a lap was seen)
#define PC_LOG_CF_ENDED        0x04         //the log ended and this chunk holds its last bytes (set by the session)
#define PC_LOG_CF_HOST         0x08         //the gap includes words dropped because this ring was full (host too slow)

typedef struct
{
  uint64_t pos;                 //byte position (free running) of the segment's first word
  uint64_t word;                //FPGA word counter of that word (since the arm, extended to 64 bits)
  uint8_t  flags;               //PC_LOG_CF_GAP / _HOST
} pc_logseg;

typedef struct
{
  uint8_t  *buf;
  uint32_t  size;               //bytes, a power of two
  uint8_t   bpw;                //bytes per FPGA word in the wire layout
  uint64_t  wr, rd;             //free-running byte positions: produced and consumed
  uint64_t  next_word;          //counter the next append continues (valid once seg_n > 0)
  uint32_t  seg_n;              //segments opened since the reset
  uint32_t  seg_r;              //the segment the reader is in (index, free running)
  uint8_t   rd_first;           //the reader has not yet returned a chunk of segment seg_r: its flags are due
  pc_logseg seg[PC_LOGRING_SEGS];
} pc_logring;

void     pc_logring_reset(pc_logring *r, uint8_t *buf, uint32_t size, uint8_t bpw);

//Where the next word goes and how many whole words fit there without wrapping or overwriting unread bytes; 0 when the
//ring is full, or when an append of first_word with flags would open a segment and none is free
uint8_t *pc_logring_wptr(const pc_logring *r, uint64_t first_word, uint8_t flags, uint32_t *words);

//Take words appended at the pointer pc_logring_wptr gave (at most the count it gave)
void     pc_logring_commit(pc_logring *r, uint32_t words, uint64_t first_word, uint8_t flags);

//Up to max_bytes (whole words) from the oldest unread segment into dst; *word = the counter of the chunk's first word,
//*flags = the segment's flags when this is its first chunk, else 0. Returns the bytes copied (0 = nothing unread)
uint32_t pc_logring_read(pc_logring *r, uint8_t *dst, uint32_t max_bytes, uint64_t *word, uint8_t *flags);

static inline uint64_t pc_logring_unread(const pc_logring *r) { return r->wr - r->rd; }

#endif /* PC_LOGRING_H */
