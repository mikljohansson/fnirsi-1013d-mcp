//----------------------------------------------------------------------------------------------------------------------------------
// fpga_model.c - bus-level model of zaklad.v's MCU interface (fpga_model.h)
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>

#include "fpga_bus.h"
#include "fpga_counter.h"
#include "fpga_model.h"

fpga_model fm;

static uint32_t fm_rnd(void)
{
  fm.seed = fm.seed * 1664525u + 1013904223u;
  return fm.seed >> 8;
}

static uint64_t fm_period_ps(void)
{
  return (uint64_t)10000 * (fm.divider + 1);
}

//fw-c5 (0x153C): the hold-off of the last trigger still runs (0x37 bit 4); 0x1B = 0 releases it at once
int fm_holdoff_running(void)
{
  return (fm.version >= 0x153C) && fm.reg_1b && (fm.t_ps < fm.ho_until_ps);
}

//fw 0.2.97: one write clock edge of the capture logic (fpga_model.h)
static void fm_capture_edge(void)
{
  int newest = fm.version >= 0x153B;
  uint32_t pre = fm.pre_applied, total = fm.reg_0c;

  if(!fm.released)
    return;

  if(fm.we)
  {
    uint32_t w = fm.n % fm.ring, l;

    for(l=0;l<4;l++)
      fm.mem[w][l] = (uint8_t)(fm.n >> (8 * l));

    //The trigger compares the word written on this edge (after the pretrigger words, as sampling_half_way)
    if(!fm.triggered && (fm.trig_at >= 0) && ((int64_t)fm.e == fm.trig_at) && (fm.n >= pre))
    {
      if(fm_holdoff_running())
        fm.ho_blocked++;                                 //0x153C: trigger_check_enable masked by the hold-off
      else
      {
        fm.triggered = 1;
        fm.trig_word = fm.n;
        fm.taddr     = (uint16_t)(fm.n % fm.ring);

        if((fm.version >= 0x153C) && fm.reg_1b)
          fm.ho_until_ps = fm.t_ps + (uint64_t)fm.reg_1b * 5000;
      }
    }

    fm.n++;

    if(fm.triggered && (fm.n >= fm.trig_word - pre + total))
      fm.we = 0;                                       //post-trigger region complete
    else if(!fm.reg_1a && (fm.n - 1 > fm.reg_0e) && (!newest || !fm.triggered))
    {
      fm.we = 0;                                       //auto time-out (or the forced stop)

      if(!fm.triggered)
        fm.taddr = (uint16_t)(newest ? ((fm.n + 2 * fm.ring - total + pre) % fm.ring) : pre);
    }

    if(!fm.we)
      fm.stop_n = fm.n;
  }
  else if(!newest && !fm.triggered && (fm.trig_at >= 0) && ((int64_t)fm.e == fm.trig_at))
    fm.triggered = 1;                                  //0x1532..0x153A: an edge after the stop still sets 0x18

  fm.e++;
}

static void fm_edge(void)
{
  if(fm.capture && !fm.in_reset)
    fm_capture_edge();

  if(fm.in_reset)
  {
    fm.edges_in_reset++;
    fm.pre_applied = fm.reg_0b;
  }
  else if(fm.load_after_release)
  {
    fm.pre_applied = fm.reg_0b;
    fm.load_after_release = 0;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// C7 counter (fpga_model.h): the gate on the synthetic stream

static uint64_t fm_cnt_gate_ps(uint8_t cfg)
{
  switch(cfg & 3)
  {
    case 1:  return 2000000ull * 5000;
    case 2:  return 20000000ull * 5000;
    case 3:  return 200000000ull * 5000;
    default: return 0;
  }
}

//k_cfg_now: { ext, ch2, gate } from 0x61 and the 0x15 copy
static uint8_t fm_cnt_cfg_now(void)
{
  uint8_t gate = (uint8_t)(fm.reg_61 & 3), src = (uint8_t)((fm.reg_61 >> 2) & 3);
  int ext = (src == 3) || ((src == 0) && (fm.reg_15 == 2));
  int ch2 = (src == 2) || ((src == 0) && !ext && (fm.reg_15 == 1));

  if(gate == 0)
    return 0;

  return (uint8_t)((ext ? 0x08 : 0) | (ch2 ? 0x04 : 0) | gate);
}

//After a 0x61 or 0x15 write: a changed effective configuration restarts the gate (k_on drops for a clock)
static void fm_cnt_check(void)
{
  uint8_t now = fm_cnt_cfg_now();

  if(now == fm.cnt_cfg)
    return;

  fm.cnt_cfg = now;

  if(now)
  {
    fm.cnt_start_ps = fm.t_ps;
    fm.cnt_end_ps   = fm.t_ps + fm_cnt_gate_ps(now);
    fm.cnt_restarts++;
  }
}

static void fm_cnt_publish(uint64_t g0, uint64_t g1)
{
  int      ext = (fm.cnt_cfg & 0x08) != 0, ch2 = (fm.cnt_cfg & 0x04) != 0, si = ext ? 2 : (ch2 ? 1 : 0);
  uint64_t period = fm.cnt_sig[si].period_ps, high = fm.cnt_sig[si].high_ps, phase = fm.cnt_sig[si].phase_ps;
  uint8_t  lo = fm.cnt_sig[si].lo, hi = fm.cnt_sig[si].hi;
  uint64_t n = 0, k0, k1;
  uint8_t  lo_thr = (fm.reg_62_lvl > fm.reg_62_hys) ? (uint8_t)(fm.reg_62_lvl - fm.reg_62_hys) : 0;
  int      edges = period != 0;

  //CH1 / CH2: the Schmitt trigger must see both levels
  if(!ext && edges && !((lo < lo_thr) && (hi >= fm.reg_62_lvl)))
    edges = 0;

  if(edges && (g1 > phase))
  {
    //Rising edges at phase + k x period inside [g0, g1)
    k0 = (g0 > phase) ? (g0 - phase + period - 1) / period : 0;
    k1 = (g1 - 1 - phase) / period;

    if(k1 >= k0)
      n = k1 - k0 + 1;
  }

  if(n >= 2)
  {
    fm.pub_e = (uint32_t)(n - 1);
    fm.pub_t = (uint32_t)((n - 1) * (period / 5000));
    fm.pub_w = (uint32_t)((n - 1) * (high / 5000));
  }
  else
    fm.pub_e = fm.pub_t = fm.pub_w = 0;

  if(ext)
  {
    fm.pub_min = 255;
    fm.pub_max = 0;
  }
  else
  {
    fm.pub_min = lo;
    fm.pub_max = period ? hi : lo;
  }

  fm.pub_cfg = fm.cnt_cfg;
  fm.pub_seq++;
  fm.cnt_publishes++;
}

static void fm_cnt_time(uint64_t end)
{
  while(fm.cnt_cfg && (fm.cnt_end_ps <= end))
  {
    fm_cnt_publish(fm.cnt_start_ps, fm.cnt_end_ps);
    fm.cnt_start_ps = fm.cnt_end_ps;
    fm.cnt_end_ps  += fm_cnt_gate_ps(fm.cnt_cfg);
  }
}

//The 0x35 command byte: one consistent copy of the published result; new = a publish since the previous snapshot
static void fm_cnt_snapshot(void)
{
  uint8_t *b = fm.snap;

  b[0]  = (uint8_t)(((fm.pub_seq != fm.snap_seq) ? 0x80 : 0) | ((fm.pub_cfg & 0x0C) << 2) | (fm.pub_cfg & 3));
  b[1]  = fm.pub_seq;
  b[2]  = (uint8_t)(fm.pub_e >> 24); b[3]  = (uint8_t)(fm.pub_e >> 16); b[4]  = (uint8_t)(fm.pub_e >> 8); b[5]  = (uint8_t)fm.pub_e;
  b[6]  = (uint8_t)(fm.pub_t >> 24); b[7]  = (uint8_t)(fm.pub_t >> 16); b[8]  = (uint8_t)(fm.pub_t >> 8); b[9]  = (uint8_t)fm.pub_t;
  b[10] = (uint8_t)(fm.pub_w >> 24); b[11] = (uint8_t)(fm.pub_w >> 16); b[12] = (uint8_t)(fm.pub_w >> 8); b[13] = (uint8_t)fm.pub_w;
  b[14] = fm.pub_min;
  b[15] = fm.pub_max;
  fm.snap_seq = fm.pub_seq;
}

void fm_cnt_signal(int src, uint64_t period_ns, uint64_t high_ns, uint64_t phase_ns, uint8_t lo, uint8_t hi)
{
  fm.cnt_sig[src].period_ps = (period_ns / 5) * 5000;
  fm.cnt_sig[src].high_ps   = (high_ns / 5) * 5000;
  fm.cnt_sig[src].phase_ps  = (phase_ns / 5) * 5000;
  fm.cnt_sig[src].lo        = lo;
  fm.cnt_sig[src].hi        = hi;
}

//----------------------------------------------------------------------------------------------------------------------------------

static void fm_time(uint64_t ps)
{
  uint64_t end = fm.t_ps + ps;

  fm_cnt_time(end);

  while(fm.next_edge_ps <= end)
  {
    fm.t_ps = fm.next_edge_ps;
    fm_edge();
    fm.next_edge_ps += fm_period_ps();
  }

  fm.t_ps = end;
}

//One bus strobe: 41..298 ns
static void fm_strobe(void)
{
  fm.strobes++;
  fm_time((uint64_t)(41000 + fm_rnd() % 257001));
}

void fm_advance_ns(uint32_t ns)
{
  fm_time((uint64_t)ns * 1000);
}

void fm_reset(uint16_t version, uint32_t divider, uint32_t seed)
{
  memset(&fm, 0, sizeof(fm));
  memset(&fpga_bus_wr, 0, sizeof(fpga_bus_wr));      //a fresh FPGA holds nothing the firmware wrote
  memset(&fpga_trig, 0, sizeof(fpga_trig));          //and the firmware's copy of the trigger extension is at power-on too
  memset(&fpga_cnt, 0, sizeof(fpga_cnt));            //C7 counter: the firmware's counter state too
  fm.pub_min = 255;                                  //C7: the published min reads 255 before the first gate
  fm.version = version;
  fm.divider = divider;
  fm.seed    = seed;
  fm.ring    = (version == 0x1432) ? 4096 : ((version == 0x1632) ? 6144 : 8192);
  fm.next_edge_ps = fm_rnd() % fm_period_ps();
}

void fm_fill(uint32_t salt)
{
  uint32_t w, l;

  for(w=0;w<FM_RING_MAX;w++)
    for(l=0;l<4;l++)
      fm.mem[w][l] = (uint8_t)((w * 7 + l * 61 + (w >> 8) * 13 + salt) & 0xFF);
}

int fm_trace_find(uint8_t cmd, uint32_t value, uint32_t from)
{
  uint32_t i;

  for(i=from;i<fm.ntrace;i++)
    if((fm.trace[i].cmd == cmd) && (fm.trace[i].value == value))
      return (int)i;

  return -1;
}

static uint32_t fm_len(uint8_t cmd)
{
  switch(cmd)
  {
    case 0x0B: case 0x0C: case 0x1F: case 0x38: case 0x58: case 0x5A:  return 2;
    case 0x62:                                                          return 2;   //C7 counter: level, hysteresis
    case 0x5F: case 0x60:                                               return 3;   //0x153C: u24 N, H
    case 0x0D: case 0x0E: case 0x1B: case 0x50: case 0x51: case 0x52: case 0x5C:  return 4;
    default:                                                            return 1;
  }
}

//A completed write
static void fm_apply(uint8_t cmd, uint32_t v)
{
  (void)fpga_bus_note_write(cmd, v);

  if(fm.ntrace < FM_TRACE_MAX)
  {
    fm.trace[fm.ntrace].cmd   = cmd;
    fm.trace[fm.ntrace].value = v;
    fm.ntrace++;
  }

  switch(cmd)
  {
    case 0x01:
      if(v & 1)
      {
        fm.in_reset       = 1;
        fm.edges_in_reset = 0;

        //0x153C: a pending 0x66 request makes this reset clear the hold-off timer (one-shot)
        if(fm.ho_clr_req)
        {
          fm.ho_until_ps = 0;
          fm.ho_clr_req  = 0;
        }
      }
      else if(fm.in_reset)
      {
        fm.in_reset = 0;
        fm.load_after_release = (uint8_t)(fm.version >= 0x1537 && fm.version <= 0x153F);
        fm.released  = 1;
        fm.we        = 1;
        fm.n         = 0;
        fm.e         = 0;
        fm.triggered = 0;
        fm.taddr     = 0;
        fm.stop_n    = 0;
      }
      break;

    case 0x0E: fm.reg_0e  = v;           break;
    case 0x1A: fm.reg_1a  = (uint8_t)v;  break;

    case 0x0B: fm.reg_0b  = (uint16_t)v; break;
    case 0x0C: fm.reg_0c  = (uint16_t)v; break;
    case 0x0D: fm.divider = v;           break;
    case 0x53: fm.reg_53  = (uint8_t)v;  break;
    case 0x54: fm.reg_54  = (uint8_t)v;  break;
    case 0x56: fm.reg_56  = (uint8_t)v;  break;

    case 0x1F:
      fm.rd_ptr = v % fm.ring;
      fm.rd_q   = fm.rd_ptr;             //the memory's address register samples the pointer on the next strobes
      break;

    //0x153C trigger extension (fw-c5); on older bitstreams the latches exist (0x1B: Atlan4's unused hold-off) or
    //the writes are ignored, and nothing below acts on them (fm_holdoff_running, the reads check the version)
    case 0x5E: fm.reg_5e = (uint8_t)v;           break;
    case 0x5F: fm.reg_5f = v & 0xFFFFFF;         break;
    case 0x60: fm.reg_60 = v & 0xFFFFFF;         break;
    case 0x1B: fm.reg_1b = v;                    break;
    case 0x66: fm.ho_clr_req = (uint8_t)(v & 1); break;

    //C7 counter (0x153C): the latches, and the gate restart on a configuration change
    case 0x15:
      fm.reg_15 = (uint8_t)(v & 3);
      if(fm.version >= 0x153C)
        fm_cnt_check();
      break;

    case 0x61:
      if(fm.version >= 0x153C)
      {
        fm.reg_61 = (uint8_t)(v & 0x0F);
        fm_cnt_check();
      }
      break;

    case 0x62:
      if(fm.version >= 0x153C)
      {
        fm.reg_62_lvl = (uint8_t)(v >> 8);
        fm.reg_62_hys = (uint8_t)v;
      }
      break;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// The primitives fpga_bus.c is built against

void fpga_write_cmd(uint8_t command)
{
  fm.cmd    = command;
  fm.nbytes = 0;
  fm.acc    = 0;
  fm_strobe();

  //C7 counter: the snapshot is taken at the 0x35 command byte
  if((command == 0x35) && (fm.version >= 0x153C))
    fm_cnt_snapshot();
}

void fpga_write_byte(uint8_t data)
{
  fm.acc = (fm.acc << 8) | data;
  fm.nbytes++;
  fm_strobe();

  if(fm.nbytes == fm_len(fm.cmd))
    fm_apply(fm.cmd, fm.acc);
}

void fpga_write_short(uint16_t data)
{
  fpga_write_byte((uint8_t)(data >> 8));
  fpga_write_byte((uint8_t)data);
}

void fpga_write_int(uint32_t data)
{
  fpga_write_byte((uint8_t)(data >> 24));
  fpga_write_byte((uint8_t)(data >> 16));
  fpga_write_byte((uint8_t)(data >> 8));
  fpga_write_byte((uint8_t)data);
}

void fpga_io_read_mode(void)
{
}

uint8_t fpga_io_strobe_read(void)
{
  uint8_t out = 0;

  fm_strobe();

  switch(fm.cmd)
  {
    case 0x20: case 0x21: case 0x22: case 0x23:
      //The byte of the word the previous strobe latched; this strobe latches the pointer's word and advances it
      out = fm.mem[fm.rd_q][fm.cmd - 0x20];
      fm.rd_q   = fm.rd_ptr;
      fm.rd_ptr = (fm.rd_ptr + 1) % fm.ring;
      break;

    case 0x05:
      fm.ready_polls++;
      out = (uint8_t)(!fm.ready_never && fm.in_reset && fm.edges_in_reset);
      break;

    case 0x06:
      out = (uint8_t)((fm.nbytes++ == 0) ? (fm.version >> 8) : fm.version);
      break;

    case 0x0A: out = (uint8_t)(fm.released && !fm.we); break;
    case 0x18: out = fm.triggered;                     break;

    case 0x14:
      out = (uint8_t)((fm.nbytes++ == 0) ? (fm.taddr >> 8) : fm.taddr);
      break;

    //0x153C: the qualifier status at its reset state (the state machines are not modelled, fpga_model.h) and the
    //extension status. Older bitstreams have no such read and return the stale bus byte (0 here)
    case 0x34:
      if(fm.version >= 0x153C)
      {
        uint32_t n16 = fm.reg_5f & 0xFFFF, rem = ((fm.reg_5e & 3) == 2) ? ((n16 ? n16 : 1) - 1) : 0;
        uint8_t  counting = (uint8_t)(((fm.reg_5e & 3) == 2) && (fm.reg_60 == 0));

        switch(fm.nbytes++)
        {
          case 0:  out = (uint8_t)((counting << 5) | ((fm.reg_5e & 0x08) >> 1) | (fm.reg_5e & 0x03)); break;
          case 1:  out = (uint8_t)(rem >> 8); break;
          default: out = (uint8_t)rem;        break;
        }
      }
      break;

    case 0x37:
      if(fm.version >= 0x153C)
        out = (uint8_t)((fm_holdoff_running() ? 0x10 : 0) | ((fm.reg_5e & 0x04) << 1));
      break;

    //C7 counter: the 16 bytes of the snapshot; further strobes repeat the last one
    case 0x35:
      if(fm.version >= 0x153C)
      {
        out = fm.snap[(fm.nbytes < 16) ? fm.nbytes : 15];
        fm.nbytes++;
      }
      break;
  }

  return out;
}

uint8_t fpga_read_byte(void)
{
  return fpga_io_strobe_read();
}
