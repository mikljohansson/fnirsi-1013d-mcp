//----------------------------------------------------------------------------------------------------------------------------------
// fpga_log.c - the FPGA 0x153D streaming logger's bus side (fpga_log.h)
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>

#include "fpga_bus.h"
#include "fpga_log.h"

//Lanes of one channel per format, in wire order (fpga_log.h): CH1 uses 0x20 / 0x21, CH2 the same + 2
static const uint8_t fpga_log_lane_tab[FPGA_LOG_F_COUNT][2] =
{
  { 0x21, 0x20 },     //legacy: core B (the earlier sample), core A
  { 0x21, 0    },     //high byte
  { 0x20, 0x21 },     //u16 little-endian
  { 0x20, 0x21 },     //min, max
};

uint8_t fpga_log_bpw(uint8_t ch_mask, uint8_t format)
{
  uint8_t nch = (uint8_t)(((ch_mask & 1) ? 1 : 0) + ((ch_mask & 2) ? 1 : 0));

  if((nch == 0) || (ch_mask > 3) || (format >= FPGA_LOG_F_COUNT))
    return 0;

  return (uint8_t)(nch * ((format == FPGA_LOG_F_HIGH) ? 1 : 2));
}

void fpga_log_snapshot(fpga_log *l)
{
  uint32_t c, i;

  fpga_write_cmd(0x38);

  for(i=0;i<8;i++)
    l->snap[i] = fpga_read_byte();

  c = ((uint32_t)l->snap[2] << 24) | ((uint32_t)l->snap[3] << 16) | ((uint32_t)l->snap[4] << 8) | l->snap[5];

  //The FPGA's counter is 32 bits: extend it (a step never sees 2^32 words go by)
  l->count += (uint32_t)(c - (uint32_t)l->count);
}

static uint16_t fpga_log_waddr(const fpga_log *l)
{
  return (uint16_t)(((l->snap[0] & 0x1F) << 8) | l->snap[1]);
}

//0x6A = the next word to read (fpga_log.h)
static void fpga_log_reader(fpga_log *l)
{
  uint16_t r = (uint16_t)((l->base + l->consumed) & (FPGA_LOG_RING - 1));

  fpga_write_cmd(0x6A);
  fpga_write_short(r);
}

static void fpga_log_end(fpga_log *l, uint8_t state)
{
  fpga_write_cmd(0x69);
  fpga_write_byte(0x00);
  l->state = state;
}

int fpga_log_start(fpga_log *l, uint8_t ch_mask, uint8_t format, uint64_t limit, uint8_t *ring, uint32_t size,
                   uint32_t max_polls)
{
  fpga_arm_cfg arm;
  uint8_t bpw = fpga_log_bpw(ch_mask, format), k = 0, ch;

  memset(l, 0, sizeof(*l));
  pc_logring_reset(&l->ring, ring, size, bpw);
  l->ch_mask = ch_mask;
  l->format  = format;
  l->limit   = limit;

  for(ch=0;ch<2;ch++)
  {
    if(!(ch_mask & (1u << ch)))
      continue;

    l->lanes[k++] = (uint8_t)(fpga_log_lane_tab[format][0] + 2 * ch);

    if(format != FPGA_LOG_F_HIGH)
      l->lanes[k++] = (uint8_t)(fpga_log_lane_tab[format][1] + 2 * ch);
  }

  l->nlanes = k;

  //Stream on before the reset, so the capture logic never stops or triggers from the release on; the sequencer off; auto
  //mode with 0x0E = 0 (in the window), so that 0x69 = 0 stops the writes at the next write clock edge and the tail can be
  //read
  fpga_write_cmd(0x69);
  fpga_write_byte(0x01);
  fpga_write_cmd(0x57);
  fpga_write_byte(0x00);
  fpga_write_cmd(0x1A);
  fpga_write_byte(0x00);

  memset(&arm, 0, sizeof(arm));
  arm.write_0e  = 1;
  arm.max_polls = max_polls;

  if(fpga_arm_reset(&arm))
  {
    fpga_log_end(l, FPGA_LOG_ERROR);
    return -1;
  }

  fpga_log_snapshot(l);

  //0x01 = 1 put the FPGA's reader and counters at 0, so word 0 is at address 0 (W == count mod 8192); the formula keeps
  //the firmware independent of that
  l->base     = (uint16_t)((fpga_log_waddr(l) - (uint32_t)l->count) & (FPGA_LOG_RING - 1));
  l->consumed = 0;
  fpga_log_reader(l);

  //Clear a lap flag left from an earlier session (0x01 = 1 does not clear it); stream stays on
  fpga_write_cmd(0x69);
  fpga_write_byte(0x03);

  l->state = FPGA_LOG_RUNNING;
  return 0;
}

//The FPGA ring lapped the reader (or nearly): skip to the newest word, the log continues
static void fpga_log_gap(fpga_log *l)
{
  l->lost    += l->count - l->consumed;
  l->consumed = l->count;
  l->gaps++;
  l->flags   |= FPGA_LOG_FL_LAP;
  l->pend    |= PC_LOG_CF_GAP;

  if(l->ddr_full)
  {
    l->flags |= FPGA_LOG_FL_DDR;
    l->pend  |= PC_LOG_CF_HOST;
  }

  l->ddr_full = 0;
  fpga_log_reader(l);
  fpga_write_cmd(0x69);
  fpga_write_byte((uint8_t)((l->state == FPGA_LOG_RUNNING) ? 0x03 : 0x02));

  //The FPGA's flag is clear now; the snapshot the next step may reuse must say so too
  l->snap[0] &= (uint8_t)~0x40;
}

static int fpga_log_lapped(const fpga_log *l)
{
  return (l->snap[0] & 0x40) || ((l->count - l->consumed) > FPGA_LOG_SAFE);
}

//One chunk from consumed (l->snap is current): the lanes into the DDR ring, a second snapshot, the chunk kept or dropped
//as a gap. Returns the words kept
static uint32_t fpga_log_chunk(fpga_log *l)
{
  uint64_t n = l->count - l->consumed;
  uint32_t room, k, start;
  uint8_t *dst;

  if(n > FPGA_LOG_CHUNK)
    n = FPGA_LOG_CHUNK;

  if(l->limit && (n > l->limit - l->consumed))
    n = l->limit - l->consumed;

  if(n == 0)
    return 0;

  dst = pc_logring_wptr(&l->ring, l->consumed, l->pend, &room);

  if(room == 0)
  {
    l->ddr_full = 1;
    return 0;
  }

  if(n > room)
    n = room;

  start = (uint32_t)((l->base + l->consumed) & (FPGA_LOG_RING - 1));

  for(k=0;k<l->nlanes;k++)
    fpga_read_lane(l->lanes[k], start, (uint32_t)n, dst + k, l->nlanes, FPGA_LOG_RING);

  //Were the words still there when the last lane read them?
  fpga_log_snapshot(l);
  l->fresh = 1;

  if(fpga_log_lapped(l))
  {
    fpga_log_gap(l);
    return 0;
  }

  pc_logring_commit(&l->ring, (uint32_t)n, l->consumed, l->pend);
  l->pend      = 0;
  l->ddr_full  = 0;
  l->consumed += n;
  l->chunks++;
  fpga_log_reader(l);
  return (uint32_t)n;
}

void fpga_log_stop(fpga_log *l, uint8_t state)
{
  uint32_t k;

  if(l->state != FPGA_LOG_RUNNING)
    return;

  fpga_log_end(l, state);

  if(state != FPGA_LOG_STOPPED)
    return;

  //The writes stop at the next write clock edge (auto mode, 0x0E = 0): the words written up to there are read too, as far
  //as the DDR ring takes them (at most the FPGA ring: 4 chunks)
  for(k=0;k<8;k++)
  {
    fpga_log_snapshot(l);

    if(fpga_log_lapped(l))
      fpga_log_gap(l);

    if(!fpga_log_chunk(l))
      break;
  }
}

uint32_t fpga_log_drain(fpga_log *l)
{
  uint32_t n;

  if(l->state != FPGA_LOG_RUNNING)
    return 0;

  l->steps++;

  if(!l->fresh)
    fpga_log_snapshot(l);

  l->fresh = 0;

  //The capture logic left stream mode (a reset bitstream, a bus fault): fail loudly
  if(!(l->snap[0] & 0x80))
  {
    fpga_log_end(l, FPGA_LOG_ERROR);
    return 0;
  }

  if(fpga_log_lapped(l))
    fpga_log_gap(l);

  if(l->limit && (l->consumed >= l->limit))
  {
    fpga_log_end(l, FPGA_LOG_ENDED);
    return 0;
  }

  n = fpga_log_chunk(l);

  if(l->limit && (l->consumed >= l->limit))
    fpga_log_end(l, FPGA_LOG_ENDED);

  return n;
}
