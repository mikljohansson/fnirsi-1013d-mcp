//----------------------------------------------------------------------------------------------------------------------------------
// test_rx_ring.c - RX ring wrap test: pc_rx_ring.h (the code pc_transport.c runs) + pc_protocol.c parser, at every ring
// phase. Host build: make -C firmware/scope/host_tests test
//
// For every start position of the ring write pointer (all 2048 byte phases, which covers every phase of a 512-byte
// packet against the ring end), and with the free running counters also placed just below the 2^32 wrap, a stream of
// request frames is sent as USB packets through a model of the EP2 OUT FIFO, stored with pc_rx_ring_store_packet,
// drained with pc_rx_ring_read in uneven chunks (as pc_cmds.c's 64-byte rx_buf does) and parsed. Every frame must come
// out as PC_EV_FRAME with the bytes that were sent and the parser must see no CRC error and no junk.
//
// FIFO model (inferred from the fw 0.1.5 GET_BAD_FRAME diffs, not from a datasheet): within one packet, a read wider
// than the previous read returns the previous value replicated (8-bit 0xA2 -> 32-bit 0xA2A2A2A2, 16-bit 0xD284 ->
// 0xD284D284) and does not advance. The test checks that this model reproduces the bench signature with the 0.1.5
// split read (tail replaced from exactly the 2 KiB stream boundary on) and that the current code is immune to it.
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pc_protocol.h"
#include "pc_rx_ring.h"
#include "test_util.h"

//----------------------------------------------------------------------------------------------------------------------------------
// EP2 OUT FIFO model: one packet at a time

typedef struct
{
  uint8_t  data[PC_USB_PACKET];
  uint32_t len;
  uint32_t rd;
  uint32_t last_w;        //width of the previous access in this packet, 0 = none yet
  uint32_t last_val;
  uint32_t width_ups;     //accesses wider than the previous one (the forbidden case)
} fifo_model;

static uint32_t fifo_access(fifo_model *f, uint32_t w)
{
  uint32_t v = 0, i;

  if(f->last_w && (w > f->last_w))
  {
    f->width_ups++;
    v = (f->last_w == 1) ? (f->last_val * 0x01010101u) : (f->last_val | (f->last_val << 16));
    return (w == 2) ? (v & 0xFFFF) : v;
  }

  for(i=0;i<w;i++)
    v |= (uint32_t)((f->rd < f->len) ? f->data[f->rd++] : 0) << (8 * i);

  f->last_w   = w;
  f->last_val = v;
  return v;
}

//Same branch structure as usb_read_from_fifo in usb_interface.c (32-bit when the buffer is aligned, then 16/8-bit tail)
static void fifo_read(void *fifo, void *buffer, uint32_t length)
{
  fifo_model *f = (fifo_model *)fifo;
  uint8_t *b = (uint8_t *)buffer;
  uint32_t alignment = ((uintptr_t)b) & 3;
  uint32_t v;

  if(alignment == 0)
  {
    uint32_t count = length / 4;
    length -= count * 4;
    while(count--) { v = fifo_access(f, 4); memcpy(b, &v, 4); b += 4; }
    if(length > 1) { v = fifo_access(f, 2); b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b += 2; length -= 2; }
    if(length) *b = (uint8_t)fifo_access(f, 1);
  }
  else if(alignment == 2)
  {
    uint32_t count = length / 2;
    while(count--) { v = fifo_access(f, 2); b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b += 2; }
    if(length & 1) *b = (uint8_t)fifo_access(f, 1);
  }
  else
  {
    while(length--) *b++ = (uint8_t)fifo_access(f, 1);
  }
}

static void fifo_load(fifo_model *f, const uint8_t *d, uint32_t n)
{
  memcpy(f->data, d, n);
  f->len = n;
  f->rd = 0;
  f->last_w = 0;
  f->last_val = 0;
}

//fw 0.1.2-0.1.5 store: two FIFO reads straight into the ring when the packet straddles the end (the bug)
static int32_t store_split_015(uint8_t *ring, uint32_t in, uint32_t out, uint32_t count, fifo_model *f)
{
  uint32_t pos = in & PC_RX_RING_MASK, first = PC_RX_RING_SIZE - pos;

  if(PC_RX_RING_SIZE - (in - out) < count)
    return -1;

  if(first > count)
    first = count;

  fifo_read(f, ring + pos, first);
  if(count > first)
    fifo_read(f, ring, count - first);

  return (int32_t)count;
}

//----------------------------------------------------------------------------------------------------------------------------------
// One run: a stream of frames through FIFO -> ring -> chunked reads -> parser

static uint8_t  ring[PC_RX_RING_SIZE] __attribute__((aligned(4)));
static uint8_t  bounce[PC_USB_PACKET] __attribute__((aligned(4)));

#define MAX_FRAMES 24

typedef struct
{
  uint32_t frames_ok;
  uint32_t frames_bad;          //missing, wrong event, or wrong bytes
  uint32_t parser_errors;       //header/payload CRC errors, junk, timeouts
  uint32_t wrap_aligned_bad;    //bad frames whose first wrong byte is exactly at a 2 KiB stream boundary
  uint32_t stalls;              //packets that had to wait for room (back-pressure path)
} run_result;

static uint32_t rng_state = 1;
static uint32_t rng(void)
{
  rng_state = rng_state * 1103515245u + 12345u;
  return (rng_state >> 8) & 0xFFFFFF;
}

static void run(uint32_t start, int use_split_015, uint32_t seed, run_result *r)
{
  static uint8_t frames[MAX_FRAMES][PC_MAX_REQUEST_FRAME];
  static uint32_t flen[MAX_FRAMES], fstream[MAX_FRAMES];
  static const uint32_t fixed_len[] = { 0, 1, 2, 3, 4, 5, 7, 200, 491, 492 };
  fifo_model fifo;
  pc_parser parser;
  uint32_t in = start, out = start, stream = 0;
  uint32_t nframes, k, sent_frame = 0, sent_off = 0, got = 0;
  int pending = 0;                          //a packet is waiting in the FIFO
  int zlp_due = 0;
  uint8_t pkt[PC_USB_PACKET];
  uint32_t pkt_len = 0, guard = 0;

  rng_state = seed;
  pc_parser_init(&parser);
  nframes = 6 + (rng() % (MAX_FRAMES - 6));

  for(k=0;k<nframes;k++)
  {
    uint32_t len = (k < 10 && (seed & 1)) ? fixed_len[k] : 1 + (rng() % 200);
    uint32_t i;

    if(rng() % 7 == 0)
      len = PC_MAX_REQUEST_PAYLOAD;

    for(i=0;i<len;i++)
      frames[k][PC_HDR_SIZE + i] = (uint8_t)rng();

    flen[k] = pc_frame_finish(frames[k], PC_CMD_PING, (uint16_t)(k + seed), 0, 0, len);
    fstream[k] = 0;
  }

  while(got < nframes)
  {
    if(++guard > 100000)
      break;

    //Host side: the next USB packet of the current frame (each request is one host write, split at 512 bytes; a frame
    //of exactly 512 bytes is followed by a zero length packet)
    if(!pending && zlp_due)
    {
      fifo_load(&fifo, pkt, 0);
      pkt_len = 0;
      zlp_due = 0;
      pending = 1;
    }
    else if(!pending && (sent_frame < nframes))
    {
      uint32_t left = flen[sent_frame] - sent_off;

      if(sent_off == 0)
        fstream[sent_frame] = stream;

      pkt_len = (left > PC_USB_PACKET) ? PC_USB_PACKET : left;
      memcpy(pkt, frames[sent_frame] + sent_off, pkt_len);
      sent_off += pkt_len;

      if(sent_off == flen[sent_frame])
      {
        zlp_due = (flen[sent_frame] % PC_USB_PACKET) == 0;
        sent_frame++;
        sent_off = 0;
      }

      fifo_load(&fifo, pkt, pkt_len);
      pending = 1;
    }

    //Device ISR: store the waiting packet
    if(pending)
    {
      int32_t st = use_split_015 ? store_split_015(ring, in, out, pkt_len, &fifo)
                                 : pc_rx_ring_store_packet(ring, in, out, pkt_len, bounce, fifo_read, &fifo);

      if(st >= 0)
      {
        in += pkt_len;
        stream += pkt_len;
        pending = 0;
      }
      else
        r->stalls++;
    }

    //Main loop: drain an uneven chunk (sometimes nothing, so the ring fills and back-pressure is exercised)
    if((rng() % 4) != 0)
    {
      uint8_t chunk[64];
      uint32_t used = in - out, n = 1 + (rng() % 64), i;

      if(n > used)
        n = used;

      pc_rx_ring_read(ring, out, chunk, n);
      out += n;

      for(i=0;i<n;i++)
      {
        int ev = pc_parser_feed(&parser, chunk[i], 0);

        while(ev != PC_EV_NONE)
        {
          if(got < nframes)
          {
            uint32_t plen = flen[got] - PC_HDR_SIZE - ((flen[got] > PC_HDR_SIZE) ? PC_CRC_SIZE : 0);
            int ok = (ev == PC_EV_FRAME) && (parser.hdr.cmd == PC_CMD_PING) && (parser.hdr.seq == (uint16_t)(got + seed)) &&
                     (parser.hdr.len == plen) && !memcmp(parser.hdr_raw, frames[got], PC_HDR_SIZE) &&
                     !memcmp(parser.payload, frames[got] + PC_HDR_SIZE, plen + (plen ? PC_CRC_SIZE : 0));

            if(ok)
              r->frames_ok++;
            else
            {
              uint32_t j, total = flen[got];
              uint8_t rx[PC_MAX_REQUEST_FRAME];

              r->frames_bad++;

              //Where does the received copy first differ? (only meaningful for BAD_CRC, where hdr + payload are kept)
              if(ev == PC_EV_BAD_CRC)
              {
                memcpy(rx, parser.hdr_raw, PC_HDR_SIZE);
                memcpy(rx + PC_HDR_SIZE, parser.payload, total - PC_HDR_SIZE);

                for(j=0;(j<total) && (rx[j] == frames[got][j]);j++)
                  ;

                if((j < total) && (((fstream[got] + j + (start & PC_RX_RING_MASK)) & PC_RX_RING_MASK) == 0))
                  r->wrap_aligned_bad++;
              }
            }
          }

          got++;
          ev = pc_parser_service(&parser, 0);
        }
      }
    }

    if((sent_frame >= nframes) && !pending && !zlp_due && (in == out) && (got < nframes))
      break;                                    //everything delivered, some frames never completed
  }

  if(got < nframes)
    r->frames_bad += nframes - got;

  r->parser_errors += parser.stats.hdr_crc_errors + parser.stats.payload_crc_errors + parser.stats.junk_bytes +
                      parser.stats.timeouts + parser.stats.oversize + parser.stats.version_errors;
}

//----------------------------------------------------------------------------------------------------------------------------------

static void test_ring_copies(void)
{
  uint8_t src[PC_RX_RING_SIZE], dst[PC_RX_RING_SIZE];
  uint32_t pos, n, i, bad = 0;

  for(i=0;i<sizeof(src);i++)
    src[i] = (uint8_t)(i * 7 + 3);

  //Every position x a set of lengths up to the full ring, also across the 2^32 counter wrap
  for(pos=0;pos<PC_RX_RING_SIZE;pos++)
  {
    static const uint32_t lens[] = { 1, 2, 3, 4, 63, 64, 511, 512, 513, 2047, 2048 };
    uint32_t base;

    for(base=0;base<2;base++)
      for(n=0;n<sizeof(lens)/sizeof(lens[0]);n++)
      {
        uint32_t at = (base ? 0xFFFFF800u : 0) + pos;

        memset(ring, 0xEE, sizeof(ring));
        pc_rx_ring_write(ring, at, src, lens[n]);

        for(i=0;i<lens[n];i++)
          if(ring[(at + i) & PC_RX_RING_MASK] != src[i])
            bad++;

        memset(dst, 0, sizeof(dst));
        pc_rx_ring_read(ring, at, dst, lens[n]);
        if(memcmp(dst, src, lens[n]))
          bad++;
      }
  }

  T_EQ(bad, 0);
}

static void test_all_phases(int use_split_015, run_result *total)
{
  uint32_t phase, base;

  memset(total, 0, sizeof(*total));

  for(base=0;base<2;base++)
    for(phase=0;phase<PC_RX_RING_SIZE;phase++)
    {
      uint32_t start = (base ? 0xFFFFF800u : 0) + phase;
      uint32_t seed;

      for(seed=1;seed<=4;seed++)
        run(start, use_split_015, seed * 7919u + phase * 2u + base, total);
    }
}

int main(void)
{
  run_result now, old;

  test_ring_copies();

  //Current code: clean at every phase
  test_all_phases(0, &now);
  printf("pc_rx_ring_store_packet: %u frames ok, %u bad, %u parser errors, %u stalled stores\n",
         now.frames_ok, now.frames_bad, now.parser_errors, now.stalls);
  T_TRUE(now.frames_ok > 100000);
  T_EQ(now.frames_bad, 0);
  T_EQ(now.parser_errors, 0);
  T_TRUE(now.stalls > 0);                      //the back-pressure path was exercised

  //Model check: the 0.1.5 split read reproduces the bench signature (corruption only from a 2 KiB stream boundary on)
  test_all_phases(1, &old);
  printf("0.1.5 split read (model): %u frames ok, %u bad, %u starting at a 2 KiB boundary\n",
         old.frames_ok, old.frames_bad, old.wrap_aligned_bad);
  T_TRUE(old.frames_bad > 0);
  T_TRUE(old.wrap_aligned_bad > 0);

  return t_report("test_rx_ring");
}
