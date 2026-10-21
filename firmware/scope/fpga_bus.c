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

void fpga_force_stop(void)
{
  //0x0E first: while 0x1A still reads normal its intermediate bytes stop nothing; then 0x1A = 0 meets time_base_cnt > 0
  fpga_write_cmd(0x0E);
  fpga_write_int(0);
  fpga_write_cmd(0x1A);
  fpga_write_byte(0x00);
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

//D5 pattern: the per-engine code tables (contract 7.5): the capture's scattered registers and the pattern trigger
//engine's contiguous block 0x76..0x7C, in the order source, polarity, level 8, level 16, type, N, H
const fpga_trig_codes fpga_trig_cap_codes = { 0x15, 0x16, 0x17, 0x55, 0x5E, 0x5F, 0x60 };
const fpga_trig_codes fpga_trig_pat_codes = { 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C };

void fpga_trig_type_regs(const fpga_trig_codes *c, const fpga_trig_ext *t)
{
  fpga_write_cmd(c->type);
  fpga_write_byte((uint8_t)((t->type & 0x07) | (t->longer ? 0x08 : 0x00)));

  //The edge type looks at neither; the qualified types take N and H at the first write clock edge after the release
  if(t->type != FPGA_TRIG_EDGE)
  {
    fpga_write_u24(c->n, t->n & FPGA_TRIG_N_MAX);
    fpga_write_u24(c->h, t->h & FPGA_TRIG_N_MAX);
  }
}

void fpga_trig_edge_regs(const fpga_trig_codes *c, uint8_t source, uint8_t polarity, uint8_t level, uint16_t level16)
{
  fpga_write_cmd(c->src);
  fpga_write_byte(source);
  fpga_write_cmd(c->pol);
  fpga_write_byte(polarity);
  fpga_write_cmd(c->lvl8);
  fpga_write_byte(level);
  fpga_write_cmd(c->lvl16);
  fpga_write_short(level16);
}
//D5 pattern end

void fpga_trig_arm(void)
{
  fpga_trig_type_regs(&fpga_trig_cap_codes, &fpga_trig);
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
      fpga_bus_wr.wclk++;                            //C7 counter
      fpga_bus_wr.divider = value;
      fpga_bus_wr.held   |= FPGA_HELD_DIVIDER;
      break;

    case 0x53:
      fpga_bus_wr.filter++;
      fpga_bus_wr.wclk++;                            //C7 counter
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
  mode &= 0x03;                         //K; 0x153C ignores bits 4..2 (pc_decim.h)
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

//----------------------------------------------------------------------------------------------------------------------------------
//D5 pattern: the pattern generator (= the generator) and its trigger engine (FPGA 0x153D, fw-d5): fpga_bus.h

fpga_pat_cfg   fpga_pat;
fpga_ptrig_cfg fpga_ptrig;
uint8_t        fpga_trig_marker;

void fpga_pat_step_write(uint8_t index, const fpga_pat_step *st)
{
  fpga_write_cmd(0x6C);
  fpga_write_byte((uint8_t)(((st->flags & FPGA_PAT_S_END) ? 0x80 : 0) | ((st->flags & FPGA_PAT_S_MARK) ? 0x40 : 0) |
                            ((st->flags & FPGA_PAT_S_LEVEL) ? 0x20 : 0) | (index & 0x1F)));
  fpga_write_int(st->dur);
  fpga_write_short(st->frac);
}

void fpga_pat_ctl(uint8_t v)
{
  fpga_write_cmd(0x6D);
  fpga_write_byte(v);
}

void fpga_pat_drive(int on)
{
  //0x50 as 1.00o5 writes it (4 bytes): 1 = the pin is driven with the pattern's level, 0 = hi-Z (the EXT input)
  fpga_write_cmd(0x50);
  fpga_write_int(on ? 1 : 0);
  fpga_pat.drive = (uint8_t)(on != 0);
}

//The control byte the firmware's copy describes, without the self-clearing bits
static uint8_t fpga_pat_ctl_byte(void)
{
  return (uint8_t)((fpga_pat.mode & 3) | (fpga_pat.ctl & FPGA_PAT_CTL_KEEP));
}

void fpga_pat_square(const gp_square *sq)
{
  fpga_pat_step hi = { FPGA_PAT_S_LEVEL, sq->f_h, sq->n_h };
  fpga_pat_step lo = { FPGA_PAT_S_END, sq->f_l, sq->n_l };
  int running = fpga_pat.square && (fpga_pat.mode == FPGA_PAT_CONT);

  if(!running)
  {
    //From a pattern, a level or off: stop, no gap, then the two entries and commit + start in one control write
    fpga_pat.mode = FPGA_PAT_OFF;
    fpga_pat.ctl  = 0;
    fpga_pat_ctl(0);

    if(fpga_pat.gap)
    {
      fpga_write_cmd(0x6F);
      fpga_write_int(0);
      fpga_pat.gap = 0;
    }
  }

  //Entries 0 and 1 go to the shadow; the commit makes them pending, taken at the next step boundary (or by the start):
  //18 strobes for a running square, glitch-free and phase-continuous
  fpga_pat_step_write(0, &hi);
  fpga_pat_step_write(1, &lo);

  fpga_pat.step[0] = hi;
  fpga_pat.step[1] = lo;
  fpga_pat.n       = 2;
  fpga_pat.square  = 1;
  fpga_pat.mode    = FPGA_PAT_CONT;
  fpga_pat_ctl((uint8_t)(fpga_pat_ctl_byte() | FPGA_PAT_CTL_COMMIT | (running ? 0 : FPGA_PAT_CTL_START)));
}

void fpga_pat_level(uint8_t level)
{
  fpga_pat.mode   = FPGA_PAT_OFF;
  fpga_pat.ctl    = level ? FPGA_PAT_CTL_IDLE : 0;
  fpga_pat.square = 0;
  fpga_pat_ctl(fpga_pat_ctl_byte());
}

void fpga_pat_load(const fpga_pat_cfg *c, int commit, int start)
{
  uint8_t i;

  fpga_pat.square = 0;

  for(i=0;i<c->n;i++)
  {
    fpga_pat_step st = c->step[i];

    st.flags = (uint8_t)((st.flags & (FPGA_PAT_S_LEVEL | FPGA_PAT_S_MARK)) | ((i == c->n - 1) ? FPGA_PAT_S_END : 0));
    fpga_pat_step_write(i, &st);
    fpga_pat.step[i] = st;
  }

  fpga_write_cmd(0x6E);
  fpga_write_short(c->r);
  fpga_write_cmd(0x6F);
  fpga_write_int(c->gap);

  fpga_pat.n    = c->n;
  fpga_pat.r    = c->r;
  fpga_pat.gap  = c->gap;
  fpga_pat.mode = (uint8_t)(c->mode & 3);
  fpga_pat.ctl  = (uint8_t)(c->ctl & FPGA_PAT_CTL_KEEP);

  fpga_pat_ctl((uint8_t)(fpga_pat_ctl_byte() | (commit ? FPGA_PAT_CTL_COMMIT : 0) | (start ? FPGA_PAT_CTL_START : 0)));
}

void fpga_pat_status(uint8_t out[4])
{
  fpga_write_cmd(0x39);
  out[0] = fpga_read_byte();
  out[1] = fpga_read_byte();
  out[2] = fpga_read_byte();
  out[3] = fpga_read_byte();
}

void fpga_ptrig_write(const fpga_ptrig_cfg *p)
{
  fpga_ptrig = *p;

  //Disarmed while the configuration changes, then armed (writing 1 restarts the engine's state) with the event flag
  //cleared
  fpga_write_cmd(0x7D);
  fpga_write_byte(0);
  fpga_trig_edge_regs(&fpga_trig_pat_codes, p->source, p->polarity, p->level, p->level16);
  fpga_trig_type_regs(&fpga_trig_pat_codes, &p->x);
  fpga_write_cmd(0x7D);
  fpga_write_byte((uint8_t)((p->armed ? 0x01 : 0) | (p->rearm ? 0x02 : 0) | 0x04 | (p->route ? 0x08 : 0)));
}

void fpga_ptrig_level16(uint16_t level16)
{
  fpga_ptrig.level16 = level16;
  fpga_write_cmd(0x79);
  fpga_write_short(level16);
}

void fpga_ptrig_status(uint8_t out[3])
{
  fpga_write_cmd(0x42);
  out[0] = fpga_read_byte();
  out[1] = fpga_read_byte();
  out[2] = fpga_read_byte();
}
//D5 pattern end
