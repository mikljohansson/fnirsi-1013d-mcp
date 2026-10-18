//----------------------------------------------------------------------------------------------------------------------------------
// pc_rx_ring.h - the USB RX byte ring used by pc_transport.c: wrap-safe copies in and out, and the per-packet store.
//
// Header only, no hardware includes: host_tests/test_rx_ring.c runs this exact code at every ring phase.
//
// The ring is PC_RX_RING_SIZE bytes (power of two) with free running uint32 in/out counters (in - out = bytes held;
// position = counter & (size - 1); both stay valid across the 2^32 wrap because 2^32 is a multiple of the size).
//
// Bench history (fw 0.1.2-0.1.5): an OUT packet that straddled the ring end was read from the FIFO in two calls, the
// first at the unaligned ring position (8/16-bit FIFO reads), the second at ring[0] (32-bit reads). The MUSB returns
// the last narrow value replicated for the wider reads ("all the transfers associated with one packet must be of the
// same width", MUSBMHDRC guide), so every request crossing a 2 KiB boundary of the byte stream came back with its tail
// replaced by a constant byte or a 2-byte alternation (GET_BAD_FRAME diffs, lead's census 2026-10-18). Rule: one
// FIFO read call per packet, into an aligned buffer, then a plain memory copy into the ring.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_RX_RING_H
#define PC_RX_RING_H

#include <stdint.h>
#include <string.h>

#define PC_RX_RING_SIZE   2048              //power of two
#define PC_RX_RING_MASK   (PC_RX_RING_SIZE - 1)

//Copy n bytes (n <= PC_RX_RING_SIZE) into the ring at free running position in. Two segments at the ring end.
static inline void pc_rx_ring_write(uint8_t *ring, uint32_t in, const uint8_t *src, uint32_t n)
{
  uint32_t pos   = in & PC_RX_RING_MASK;
  uint32_t first = PC_RX_RING_SIZE - pos;

  if(first > n)
    first = n;

  memcpy(ring + pos, src, first);

  if(n > first)
    memcpy(ring, src + first, n - first);
}

//Copy n bytes (n <= PC_RX_RING_SIZE) out of the ring from free running position out. Two segments at the ring end.
static inline void pc_rx_ring_read(const uint8_t *ring, uint32_t out, uint8_t *dst, uint32_t n)
{
  uint32_t pos   = out & PC_RX_RING_MASK;
  uint32_t first = PC_RX_RING_SIZE - pos;

  if(first > n)
    first = n;

  memcpy(dst, ring + pos, first);

  if(n > first)
    memcpy(dst + first, ring, n - first);
}

//FIFO reader: usb_read_from_fifo on the scope, a FIFO model in the host test
typedef void (*pc_rx_fifo_read_fn)(void *fifo, void *buffer, uint32_t length);

//Store one OUT packet of count bytes (count <= bounce size) that is waiting in the FIFO. Returns the number of bytes
//stored (count), or -1 when the ring lacks room: the packet is then left untouched in the FIFO. The packet is read
//with exactly ONE FIFO call into the 4-byte aligned bounce buffer, whatever the ring position.
static inline int32_t pc_rx_ring_store_packet(uint8_t *ring, uint32_t in, uint32_t out, uint32_t count,
                                              uint8_t *bounce, pc_rx_fifo_read_fn fifo_read, void *fifo)
{
  if(PC_RX_RING_SIZE - (in - out) < count)
    return -1;

  if(count)
  {
    fifo_read(fifo, bounce, count);
    pc_rx_ring_write(ring, in, bounce, count);
  }

  return (int32_t)count;
}

#endif /* PC_RX_RING_H */
