//----------------------------------------------------------------------------------------------------------------------------------
// fpga_bus.c - readout start and sampling reset shared by every reader and arm (fpga_bus.h)
//----------------------------------------------------------------------------------------------------------------------------------

#include "fpga_bus.h"

void fpga_read_start(uint8_t cmd, uint16_t start_word)
{
  uint32_t i;

  fpga_write_cmd(0x1F);
  fpga_write_short(start_word);
  fpga_write_cmd(cmd);

  fpga_io_read_mode();

  for(i=0;i<FPGA_READ_DUMMY;i++)
    (void)fpga_io_strobe_read();
}

void fpga_read_lane(uint8_t cmd, uint32_t start, uint32_t count, uint8_t *dst, uint32_t stride, uint32_t ring_words)
{
  while(count)
  {
    uint32_t n = ring_words - start;

    if(n > count)
      n = count;

    fpga_read_start(cmd, (uint16_t)start);
    count -= n;

    while(n--)
    {
      *dst = fpga_io_strobe_read();
      dst += stride;
    }

    start = 0;
  }
}

uint32_t fpga_ring_start(uint32_t addr, uint32_t pre_words, uint32_t ring_words)
{
  return (addr % ring_words + ring_words - pre_words % ring_words) % ring_words;
}

void fpga_read_decim_data(uint8_t cmdlo, uint8_t cmdhi, uint32_t start, uint32_t count, uint16_t *dst)
{
  uint32_t i;

  //16-bit samples exist on AL3 only, whose 13-bit read pointer wraps at the 8192-word ring end by itself
  fpga_read_start(cmdlo, (uint16_t)start);

  for(i=0;i<count;i++)
    dst[i] = fpga_io_strobe_read();

  fpga_read_start(cmdhi, (uint16_t)start);

  for(i=0;i<count;i++)
    dst[i] |= (uint16_t)(fpga_io_strobe_read() << 8);
}

static void fpga_arm_geometry(const fpga_arm_cfg *c)
{
  fpga_write_cmd(0x0B);
  fpga_write_short(c->pre_words);
  fpga_write_cmd(0x0C);
  fpga_write_short(c->total_words);
}

int fpga_arm_reset(const fpga_arm_cfg *c)
{
  uint32_t n = 0;

  //Before the reset: the write clock edges the 0x05 poll waits for load 0x0B into the trigger read address
  fpga_arm_geometry(c);

  //Reset the sample system and wait for ready
  fpga_write_cmd(0x01);
  fpga_write_byte(0x01);
  fpga_write_cmd(0x05);

  while((fpga_read_byte() & 1) == 0)
  {
    n++;

    if(c->max_polls)
    {
      if(n >= c->max_polls)
      {
        fpga_write_cmd(0x01);
        fpga_write_byte(0x00);
        return -1;
      }
    }
    else if(((n & 0xFFFF) == 0) && c->idle)
      c->idle();
  }

  //Inside the window as 1.00o5 ("send when the system is in reset")
  fpga_arm_geometry(c);

  //0x153C: the trigger type registers, applied by the qualifier at the first write clock edge after the release
  fpga_trig_arm();

  if(c->write_0e)
  {
    fpga_write_cmd(0x0E);
    fpga_write_int(c->auto_timeout);
  }

  //Release
  fpga_write_cmd(0x01);
  fpga_write_byte(0x00);
  return 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Trigger types and hold-off (0x153C, fw-c5): fpga_bus.h

fpga_trig_ext fpga_trig;

static void fpga_write_u24(uint8_t cmd, uint32_t v)
{
  fpga_write_cmd(cmd);
  fpga_write_byte((uint8_t)(v >> 16));
  fpga_write_byte((uint8_t)(v >> 8));
  fpga_write_byte((uint8_t)v);
}

void fpga_trig_arm(void)
{
  fpga_write_cmd(0x5E);
  fpga_write_byte((uint8_t)((fpga_trig.type & 0x07) | (fpga_trig.longer ? 0x08 : 0x00)));

  //The edge type looks at neither; the qualified types take N and H at the first write clock edge after the release
  if(fpga_trig.type != FPGA_TRIG_EDGE)
  {
    fpga_write_u24(0x5F, fpga_trig.n & FPGA_TRIG_N_MAX);
    fpga_write_u24(0x60, fpga_trig.h & FPGA_TRIG_N_MAX);
  }
}

void fpga_trig_write(const fpga_trig_ext *t, int clear)
{
  fpga_trig = *t;
  fpga_trig_arm();

  fpga_write_cmd(0x1B);
  fpga_write_int(fpga_trig.holdoff);

  if(clear)
  {
    fpga_write_cmd(0x66);
    fpga_write_byte(0x01);
  }
}

void fpga_trig_status(uint8_t out[4])
{
  fpga_write_cmd(0x37);
  out[0] = fpga_read_byte();
  fpga_write_cmd(0x34);
  out[1] = fpga_read_byte();
  out[2] = fpga_read_byte();
  out[3] = fpga_read_byte();
}

//----------------------------------------------------------------------------------------------------------------------------------

fpga_bus_writes fpga_bus_wr;

int fpga_bus_note_write(uint8_t cmd, uint32_t value)
{
  switch(cmd)
  {
    case 0x0D:
      fpga_bus_wr.rate++;
      fpga_bus_wr.divider = value;
      fpga_bus_wr.held   |= FPGA_HELD_DIVIDER;
      break;

    case 0x53:
      fpga_bus_wr.filter++;
      fpga_bus_wr.mode  = (uint8_t)value;
      fpga_bus_wr.held |= FPGA_HELD_MODE;
      break;

    case 0x56:
      fpga_bus_wr.filter++;
      fpga_bus_wr.fir   = (uint8_t)value;
      fpga_bus_wr.held |= FPGA_HELD_FIR;
      break;

    //Shift and level: no effect on the encode clocks (since_reset unchanged)
    case 0x54:
      fpga_bus_wr.filter++;
      fpga_bus_wr.shift = (uint8_t)value;
      fpga_bus_wr.held |= FPGA_HELD_SHIFT;
      return 1;

    case 0x55:
      fpga_bus_wr.filter++;
      return 1;

    case 0x01:
      if(value != 1)
        return 0;

      fpga_bus_wr.resets++;
      fpga_bus_wr.since_reset = 0;
      return 1;

    default:
      return 0;
  }

  fpga_bus_wr.since_reset = 1;
  return 1;
}

//The divider at the previous fpga_decim_write (0xFFFFFFFF: none yet, or no 0x0D written then)
static uint32_t fpga_decim_div = 0xFFFFFFFF;

int fpga_decim_write(uint8_t mode, int has_fir, uint8_t cmd56, uint8_t shift)
{
  uint32_t div = (fpga_bus_wr.held & FPGA_HELD_DIVIDER) ? fpga_bus_wr.divider : 0xFFFFFFFF;
  int      settle = (div != fpga_decim_div);

  fpga_decim_div = div;
  mode &= 0x1F;
  cmd56 &= 0x03;                        //bit 0 FIR enable, bit 1 peak detect (0x153C, C6); bits 7..2 write 0
  shift &= 0x3F;

  if(!(fpga_bus_wr.held & FPGA_HELD_MODE) || (fpga_bus_wr.mode != mode))
  {
    fpga_write_cmd(0x53);
    fpga_write_byte(mode);
    settle = 1;
  }

  //0x1535+: FIR enable (and the 0x153C peak detect bit); older bitstreams never get the command
  if(has_fir && (!(fpga_bus_wr.held & FPGA_HELD_FIR) || (fpga_bus_wr.fir != cmd56)))
  {
    fpga_write_cmd(0x56);
    fpga_write_byte(cmd56);
    settle = 1;
  }

  //The shift at every arm with the filter on: one byte, 0.6 us
  if(mode & 3)
  {
    if(!(fpga_bus_wr.held & FPGA_HELD_SHIFT) || (fpga_bus_wr.shift != shift))
      settle = 1;

    fpga_write_cmd(0x54);
    fpga_write_byte(shift);
  }

  return settle;
}
