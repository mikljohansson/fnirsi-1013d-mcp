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

//C6 peak detect: the model's 0x56 bit 1 (a register on 0x153C), the 16-bit path and the window
int fm_peak_on(void)
{
  return (fm.version >= 0x153C) && (fm.reg_56 & 0x02);
}

int fm_dec_on(void)
{
  return ((fm.reg_53 & 3) != 0) || fm_peak_on();
}

uint32_t fm_window(void)
{
  return 2u * ((fm.divider >> 1) + 1u);
}

//Synthetic raw 200 MSa/s stream per channel: a sawtooth 64..191 (period 128 samples, channel 2 offset by 50) with a
//255 spike every 1000 samples and a 0 dip every 1777, so a window of N >= 2 samples catches what sample skipping misses
uint8_t fm_raw_sample(uint32_t ch, uint64_t j)
{
  if((j % 1000) == 0)
    return 255;
  if((j % 1777) == 0)
    return 0;

  return (uint8_t)(64 + ((j * 7 + (uint64_t)ch * 50) % 128));
}

void fm_peak_word(uint32_t ch, uint32_t w, uint8_t *mn, uint8_t *mx)
{
  uint32_t n = fm_window(), i;
  uint64_t j = (uint64_t)w * n;
  uint8_t  lo = 255, hi = 0;

  for(i=0;i<n;i++)
  {
    uint8_t s = fm_raw_sample(ch, j + i);

    if(s < lo) lo = s;
    if(s > hi) hi = s;
  }

  *mn = lo;
  *mx = hi;
}

//fw-fullrate (0x153D, legacy K = 0): the ADC encodes and the write clock run at 100 MHz at every rate, a word is stored
//on every (D + 1)-th write clock edge after the release; before 0x153D the write clock itself was divided
static int fm_fullrate(void)
{
  return (fm.version >= 0x153D) && !fm_dec_on();
}

//Write clock edges: 10 (D + 1) ns in the legacy layout up to 0x153C, 10 ns on 0x153D (fm_fullrate), N x 5 ns on the
//16-bit path (filter or peak, one word per edge)
static uint64_t fm_period_ps(void)
{
  if(fm_dec_on())
    return (uint64_t)5000 * fm_window();

  if(fm_fullrate())
    return 10000;

  return (uint64_t)10000 * (fm.divider + 1);
}

//The legacy pair of word w on 0x153D: pair p = w (D + 1) of the full-rate stream, core B the earlier sample (2p),
//core A 5 ns later (2p + 1)
void fm_fullrate_pair(uint32_t ch, uint32_t w, uint8_t *a, uint8_t *b)
{
  uint64_t j = 2ull * w * (fm.divider + 1);

  *b = fm_raw_sample(ch, j);
  *a = fm_raw_sample(ch, j + 1);
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

  //D4 logger: stream mode writes for ever, no trigger, no stop; the lap flag when a word lands on 0x6A
  if((fm.version >= 0x153D) && (fm.reg_69 & 1) && fm.we)
  {
    uint32_t w = fm.n % fm.ring, l;

    if(fm_peak_on())
    {
      fm_peak_word(0, fm.n, &fm.mem[w][0], &fm.mem[w][1]);
      fm_peak_word(1, fm.n, &fm.mem[w][2], &fm.mem[w][3]);
    }
    else
      for(l=0;l<4;l++)
        fm.mem[w][l] = (uint8_t)(fm.n >> (8 * l));

    //fpga-d1's detector: the unread count saturates at 8192; a write at 8192 overwrote the oldest unread word
    if(fm.occ >= 8192)
    {
      if(!fm.lap)
        fm.laps_seen++;
      fm.lap = 1;
    }
    else
      fm.occ++;

    fm.n++;
    fm.e++;
    return;
  }
  //D4 logger end

  if(fm.we)
  {
    uint32_t w = fm.n % fm.ring, l;

    if(fm_peak_on())
    {
      //C6: { max, min } of the window's synthetic raw samples per channel: lanes 0x20 / 0x22 min, 0x21 / 0x23 max
      fm_peak_word(0, fm.n, &fm.mem[w][0], &fm.mem[w][1]);
      fm_peak_word(1, fm.n, &fm.mem[w][2], &fm.mem[w][3]);
    }
    else if(fm.raw_words && fm_fullrate())
    {
      //fw-fullrate: lanes 0x20 / 0x22 core A, 0x21 / 0x23 core B of the stored pair
      fm_fullrate_pair(0, fm.n, &fm.mem[w][0], &fm.mem[w][1]);
      fm_fullrate_pair(1, fm.n, &fm.mem[w][2], &fm.mem[w][3]);
    }
    else
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

//fw-fullrate: 1 when this write clock edge stores a word (every edge but on 0x153D's legacy path at D > 0)
static int fm_store_edge(void)
{
  uint32_t s = fm.wphase;

  if(!fm_fullrate())
    return 1;

  fm.wphase = (s >= fm.divider) ? 0 : s + 1;
  return s == 0;
}

static void fm_edge(void)
{
  if(fm.capture && !fm.in_reset && fm_store_edge())
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

//fw-ctrauto: a sample-level source through the Schmitt trigger, every 5 ns clock of the gate: E, T, W between the
//first and the last rising edge, min / max over the whole gate
static void fm_cnt_publish_wave(uint8_t (*wave)(uint64_t clk), uint64_t g0, uint64_t g1)
{
  uint64_t k, k0 = g0 / 5000, k1 = g1 / 5000, first = 0, last = 0, high = 0, n = 0;
  uint8_t  lvl = fm.reg_62_lvl, lo_thr = (fm.reg_62_lvl > fm.reg_62_hys) ? (uint8_t)(fm.reg_62_lvl - fm.reg_62_hys) : 0;
  uint8_t  x, mn = 255, mx = 0;
  int      st = wave(k0) >= lvl;

  for(k=k0;k<k1;k++)
  {
    x = wave(k);

    if(x < mn) mn = x;
    if(x > mx) mx = x;

    if(!st && (x >= lvl))
    {
      st = 1;

      if(!n)
        first = k;

      last = k;
      n++;
    }
    else if(st && (x < lo_thr))
      st = 0;

    if(st && n)
      high++;                                        //high clocks from the first rising edge on

    if(k == last)
      fm.pub_w = (uint32_t)(high - 1);               //up to (not including) the latest rising edge's clock
  }

  fm.pub_min = mn;
  fm.pub_max = mx;

  if(n >= 2)
  {
    fm.pub_e = (uint32_t)(n - 1);
    fm.pub_t = (uint32_t)(last - first);
  }
  else
    fm.pub_e = fm.pub_t = fm.pub_w = 0;
}

static void fm_cnt_publish(uint64_t g0, uint64_t g1)
{
  int      ext = (fm.cnt_cfg & 0x08) != 0, ch2 = (fm.cnt_cfg & 0x04) != 0, si = ext ? 2 : (ch2 ? 1 : 0);

  uint64_t period = fm.cnt_sig[si].period_ps, high = fm.cnt_sig[si].high_ps, phase = fm.cnt_sig[si].phase_ps;
  uint8_t  lo = fm.cnt_sig[si].lo, hi = fm.cnt_sig[si].hi;
  uint64_t n = 0, k0, k1;
  uint8_t  lo_thr = (fm.reg_62_lvl > fm.reg_62_hys) ? (uint8_t)(fm.reg_62_lvl - fm.reg_62_hys) : 0;
  int      edges = period != 0;

  //fw-ctrauto: a sample-level source
  if(fm.cnt_wave[si])
  {
    fm_cnt_publish_wave(fm.cnt_wave[si], g0, g1);
    fm.pub_cfg = fm.cnt_cfg;
    fm.pub_seq++;
    fm.cnt_publishes++;
    return;
  }

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
  fm.cnt_wave[src]          = 0;
}

void fm_cnt_wave(int src, uint8_t (*wave)(uint64_t clk))
{
  fm.cnt_wave[src] = wave;
}

//----------------------------------------------------------------------------------------------------------------------------------
//The generator's square (fw-d5, the RTL cut to the square by fpga-d3, d60a0ad; fpga_model.h header)

static int fm_gen_has(void)
{
  return fm.version >= 0x153D;
}

uint64_t fm_clk(void)
{
  return fm.t_ps / 5000;
}

int fm_pin(void)
{
  return fm.gen_drive ? fm.gen_level : 2;
}

static void fm_gen_set_level(uint8_t level, uint64_t clk)
{
  if(level != fm.gen_level)
  {
    fm.gen_edge[fm.gen_nedge % FM_GEN_EDGES] = (clk << 1) | level;
    fm.gen_nedge++;
  }

  fm.gen_level = level;
}

static uint8_t fm_gen_idle(void)
{
  return (uint8_t)((fm.gen_ctl >> 2) & 1);
}

//The shadows become the active pair (the commit taken)
static void fm_gen_take(void)
{
  fm.gen_act[0]  = fm.gen_sh[0];
  fm.gen_act[1]  = fm.gen_sh[1];
  fm.gen_pending = 0;
}

//Begin step idx at clock clk (a pending commit is taken at the boundary first): n + the carry of acc + f clocks
static void fm_gen_begin(uint8_t idx, uint64_t clk)
{
  uint32_t sum, dur;

  if(fm.gen_pending)
    fm_gen_take();

  sum = (uint32_t)fm.gen_acc + fm.gen_act[idx].frac;
  dur = fm.gen_act[idx].dur + (sum >> 16);
  fm.gen_acc     = (uint16_t)sum;
  fm.gen_step    = idx;
  fm.gen_end_clk = clk + dur;
  fm_gen_set_level((uint8_t)((fm.gen_act[idx].flags >> 5) & 1), clk);
}

//A start: the pending shadows are taken first, so step 0 is new; the accumulator cleared
static void fm_gen_start(uint64_t clk)
{
  fm.gen_acc = 0;
  fm.gen_run = 1;
  fm.gen_starts++;
  fm_gen_begin(0, clk);
}

static void fm_gen_stop(uint64_t clk)
{
  fm.gen_run = 0;
  fm.gen_step = 0;
  fm_gen_set_level(fm_gen_idle(), clk);
}

//Run the square up to clock clk (every boundary at or before it); the pending bit of a commit taken at once clears
//2 clocks after it
static void fm_gen_run(uint64_t clk)
{
  while(fm.gen_run && (fm.gen_end_clk <= clk))
    fm_gen_begin((uint8_t)(fm.gen_step ^ 1), fm.gen_end_clk);

  if(fm.gen_pending && fm.gen_take_clk && (clk >= fm.gen_take_clk))
  {
    fm_gen_take();
    fm.gen_take_clk = 0;
  }
}

static void fm_time(uint64_t ps)
{
  uint64_t end = fm.t_ps + ps;

  fm_cnt_time(end);

  while(fm.next_edge_ps <= end)
  {
    fm.t_ps = fm.next_edge_ps;
    fm_gen_run(fm.t_ps / 5000);
    fm_edge();
    fm.next_edge_ps += fm_period_ps();
  }

  fm.t_ps = end;
  fm_gen_run(fm.t_ps / 5000);
}

static void fm_gen_write(uint8_t cmd)
{
  uint8_t *b = fm.buf;
  uint64_t now = fm_clk();

  if(!fm_gen_has())
    return;

  fm_gen_run(now);

  switch(cmd)
  {
    case 0x6C:
    {
      uint8_t idx = b[0] & 0x1F;

      //Entries 0 (high) and 1 (low) only, double-buffered; bits 7 / 6 of byte 0 and other indices are ignored
      if(idx < 2)
      {
        fm.gen_sh[idx].flags = b[0] & 0x20;
        fm.gen_sh[idx].dur   = ((uint32_t)b[1] << 24) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 8) | b[4];
        fm.gen_sh[idx].frac  = (uint16_t)((b[5] << 8) | b[6]);
      }
      break;
    }

    case 0x6D:
    {
      uint8_t v = b[0];

      //Mode 3 runs (a write while idle starts it 8 clocks after the strobe; ignored while running), any other mode
      //is off at once at the idle level (bit 2). The commit: taken 2 clocks after the write when the step in progress
      //has at least 3 clocks left (that step completes with its old length, every step from its boundary is new),
      //else at that boundary; the accumulator is not cleared
      fm.gen_mode = v & 3;
      fm.gen_ctl  = v & 0x04;

      if(v & 0x20)
      {
        if(fm.gen_pending && fm.gen_take_clk)
          fm_gen_take();                                       //an earlier commit, still in its 2 clocks, first

        fm.gen_pending  = 1;
        fm.gen_take_clk = (fm.gen_run && (fm.gen_end_clk >= now + 3)) ? now + 2 : 0;
      }

      if(fm.gen_mode != 3)
      {
        if(fm.gen_run)
          fm_gen_stop(now + 1);
        else
          fm_gen_set_level(fm_gen_idle(), now + 1);
      }
      else if(!fm.gen_run)
        fm_gen_start(now + 8);                                //step 0 40..45 ns after the strobe (fpga-d2, tb_pattern)
      break;
    }

    case 0x50: fm.gen_drive = b[3] & 1; break;
  }
}
//The generator's square end

//One bus strobe: 41..298 ns
static void fm_strobe(void)
{
  fm.strobes++;

  //D4 logger: a fixed strobe time for the drain cost test
  if(fm.strobe_ns)
  {
    fm_time((uint64_t)fm.strobe_ns * 1000);
    return;
  }

  fm_time((uint64_t)(41000 + fm_rnd() % 257001));
}

void fm_advance_ns(uint32_t ns)
{
  fm_time((uint64_t)ns * 1000);
}

//D4 logger: idle time in ps (the drain cost test's packet and build times)
void fm_advance_ps(uint64_t ps)
{
  fm_time(ps);
}

void fm_reset(uint16_t version, uint32_t divider, uint32_t seed)
{
  memset(&fm, 0, sizeof(fm));
  memset(&fpga_bus_wr, 0, sizeof(fpga_bus_wr));      //a fresh FPGA holds nothing the firmware wrote
  memset(&fpga_trig, 0, sizeof(fpga_trig));          //and the firmware's copy of the trigger extension is at power-on too
  memset(&fpga_cnt, 0, sizeof(fpga_cnt));            //C7 counter: the firmware's counter state too
  memset(&fpga_gen, 0, sizeof(fpga_gen));            //the generator: the firmware's copy at power-on
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
    case 0x6A:                                                          return 2;   //D4 logger: reader position
    case 0x5F: case 0x60:                                               return 3;   //0x153C: u24 N, H
    case 0x0D: case 0x0E: case 0x1B: case 0x50: case 0x5C:             return 4;   //0x51 / 0x52 retired in 0x153D
    case 0x6C:                                                          return 7;   //0x153D: a square entry
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

  //The generator's registers (0x6E, 0x6F, 0x76..0x7D of the cut pattern generator are ignored, as by the RTL)
  switch(cmd)
  {
    case 0x50: case 0x6C: case 0x6D:
      fm_gen_write(cmd);
      break;
  }

  switch(cmd)
  {
    //fw-relay: the front-end relays
    case 0x33: case 0x36: case 0x34: case 0x37:
    {
      uint8_t i = FPGA_RELAY_INDEX(cmd), nv = (uint8_t)(((cmd == 0x34) || (cmd == 0x37)) ? (v & 1) : v);

      if(((cmd == 0x33) || (cmd == 0x36)) && (v > 5))
        break;

      fm.relay_writes++;
      if(fm.relay[i] != nv)
        fm.relay_switches++;
      fm.relay[i] = nv;
      break;
    }

    case 0x01:
      if(v & 1)
      {
        fm.in_reset        = 1;
        fm.edges_in_reset  = 0;

        //D4 logger (0x153D): the reader position and the unread count go to 0 (the lap flag stays)
        fm.reg_6a = 0;
        fm.occ    = 0;

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
        fm.wphase    = 0;
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

    //D4 logger (0x153D): stream, lap clear, reader position
    case 0x69:
      if(fm.version >= 0x153D)
      {
        fm.reg_69 = (uint8_t)(v & 1);
        if(v & 2)
          fm.lap = 0;
      }
      break;

    case 0x6A:
      if(fm.version >= 0x153D)
      {
        fm.reg_6a = (uint16_t)(v & 0x1FFF);
        fm.occ    = ((fm.released ? fm.n : 0) - fm.reg_6a) & 0x1FFF;
      }
      break;
    //D4 logger end

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

  //D4 logger: the 0x38 snapshot at the command byte
  if((command == 0x38) && (fm.version >= 0x153D))
  {
    uint32_t w = fm.released ? (fm.n % fm.ring) : 0, c = fm.released ? fm.n : 0;
    uint32_t av = fm.occ;
    uint8_t *b = fm.log_snap;

    b[0] = (uint8_t)(((fm.reg_69 & 1) ? 0x80 : 0) | (fm.lap ? 0x40 : 0) | ((w >> 8) & 0x1F));
    b[1] = (uint8_t)w;
    b[2] = (uint8_t)(c >> 24); b[3] = (uint8_t)(c >> 16); b[4] = (uint8_t)(c >> 8); b[5] = (uint8_t)c;
    b[6] = (uint8_t)(av >> 8); b[7] = (uint8_t)av;
  }
}

void fpga_write_byte(uint8_t data)
{
  if(fm.nbytes < sizeof(fm.buf))
    fm.buf[fm.nbytes] = data;

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

    //Decimation state (0x1534+): byte 0 = { dec_on, fir_en, fir_act, 0, 0, swap, K } (0x153C: bits 4..2 read 0, the
    //swap and neg registers are gone), byte 1 = the shift; 0x153C (C6):
    //byte 1 bits 7 / 6 = peak active / peak as written (equal: active whenever written), the FIR off while peak is on
    case 0x29:
      if(fm.version >= 0x1534)
      {
        uint32_t n   = fm_window();
        int      fir = (fm.reg_56 & 1) && (fm.reg_53 & 3) && (n >= 16) && ((n & 3) == 0) && !fm_peak_on();

        if(fm.nbytes++ == 0)
          out = (uint8_t)((fm_dec_on() ? 0x80 : 0) | ((fm.reg_56 & 1) ? 0x40 : 0) | (fir ? 0x20 : 0) |
                        (fm.reg_53 & ((fm.version >= 0x153C) ? 0x03 : 0x07)));
        else
          out = (uint8_t)((fm_peak_on() ? 0xC0 : 0) | (fm.reg_54 & 0x3F));
      }
      break;

    //D4 logger: the 8 bytes of the 0x38 snapshot
    case 0x38:
      if(fm.version >= 0x153D)
      {
        out = fm.log_snap[(fm.nbytes < 8) ? fm.nbytes : 7];
        fm.nbytes++;
      }
      break;

    //The generator: 0x39 = { running, 0, pending, 0, 0, 0, 0, step }, 0, 0, { 0 x 7, mode active (mode 3) }
    case 0x39:
      if(fm_gen_has())
      {
        fm_gen_run(fm_clk());

        switch(fm.nbytes++)
        {
          case 0:  out = (uint8_t)((fm.gen_run ? 0x80 : 0) | (fm.gen_pending ? 0x20 : 0) | (fm.gen_step & 0x01)); break;
          case 3:  out = (uint8_t)(fm.gen_mode == 3); break;
          default: out = 0; break;
        }
      }
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
