//----------------------------------------------------------------------------------------------------------------------------------
// test_log.c - the streaming logger (fw-d4, item D4, protocol 1.27 row 67): pc_logring.c (the DDR ring and its segments) and
// fpga_log.c (start, drain, gaps, stop) against fpga_model.c's 0x153D stream (0x69 / 0x6A / 0x38).
//
// The cost test runs the log loop as the scope does (a drain step, then the USB side) in the model's time: every bus
// strobe takes the measured 0.30 or 0.35 us (fm.strobe_ns), a LOG_READ response costs its build (memcpy + CRC-32, 25 ns
// per byte [E]) and then every 512-byte packet costs 0.43 or 1.0 us per byte of CPU time between drain steps (the
// contract's conservative reading of the USB rates, section 6.2: no DMA, the PIO FIFO writes and the host's pace counted
// as CPU). It checks the contract's rates sustain without a gap at the worst case and reports the measured duty.
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdlib.h>

#include "test_util.h"
#include "fpga_model.h"
#include "fpga_bus.h"
#include "fpga_log.h"
#include "pc_logring.h"

#define BUILD_PS_PER_BYTE   25000ull    //response build: memcpy + table CRC-32, 25 ns per byte [E]
#define IDLE_NS             20000u      //a loop pass with nothing to send (pc_poll and the notice check)
#define HOST_RTT_PS         1000000000ull //the host's next LOG_READ arrives 1 ms after the previous response left [E]

static uint8_t ring_mem[1u << 20];      //the DDR ring of the tests (the scope's is 8 MiB)
static uint8_t chunk[65536];

//----------------------------------------------------------------------------------------------------------------------------------
// The ring alone

static void test_ring(void)
{
  pc_logring r;
  uint8_t  buf[64], out[64];
  uint32_t n, i, words;
  uint64_t w;
  uint8_t  f, *p;

  pc_logring_reset(&r, buf, sizeof(buf), 4);

  //Empty: nothing to read, room for the whole buffer
  T_EQ(pc_logring_read(&r, out, 64, &w, &f), 0);
  p = pc_logring_wptr(&r, 100, 0, &words);
  T_TRUE(p == buf);
  T_EQ(words, 16);

  //10 words from counter 100, read 3 + rest: counters exact, first chunk's flags only
  for(i=0;i<40;i++) p[i] = (uint8_t)i;
  pc_logring_commit(&r, 10, 100, PC_LOG_CF_GAP);
  n = pc_logring_read(&r, out, 14, &w, &f);            //rounded down to whole words
  T_EQ(n, 12); T_EQ(w, 100); T_EQ(f, PC_LOG_CF_GAP); T_EQ(out[11], 11);
  n = pc_logring_read(&r, out, 64, &w, &f);
  T_EQ(n, 28); T_EQ(w, 103); T_EQ(f, 0); T_EQ(out[0], 12);

  //Wrap: 6 words fit before the end (40 written of 64), 10 more after the reader moved on
  p = pc_logring_wptr(&r, 110, 0, &words);
  T_EQ(words, 6); T_TRUE(p == buf + 40);
  for(i=0;i<24;i++) p[i] = (uint8_t)(0x80 + i);
  pc_logring_commit(&r, 6, 110, 0);
  p = pc_logring_wptr(&r, 116, 0, &words);
  T_TRUE(p == buf); T_EQ(words, 10);
  for(i=0;i<16;i++) p[i] = (uint8_t)(0xA0 + i);
  pc_logring_commit(&r, 4, 116, 0);
  n = pc_logring_read(&r, out, 64, &w, &f);            //one segment, across the end
  T_EQ(n, 40); T_EQ(w, 110); T_EQ(f, 0); T_EQ(out[23], 0x97); T_EQ(out[24], 0xA0); T_EQ(out[39], 0xAF);

  //A counter jump opens a segment with the gap flag; a chunk stops at the boundary
  p = pc_logring_wptr(&r, 120, 0, &words);
  pc_logring_commit(&r, 2, 120, 0);
  p = pc_logring_wptr(&r, 130, PC_LOG_CF_HOST, &words);
  pc_logring_commit(&r, 3, 130, PC_LOG_CF_HOST);
  n = pc_logring_read(&r, out, 64, &w, &f);
  T_EQ(n, 8); T_EQ(w, 120); T_EQ(f, 0);
  n = pc_logring_read(&r, out, 64, &w, &f);
  T_EQ(n, 12); T_EQ(w, 130); T_EQ(f, PC_LOG_CF_GAP | PC_LOG_CF_HOST);
  T_EQ(pc_logring_read(&r, out, 64, &w, &f), 0);

  //Full: no room until the reader takes something
  p = pc_logring_wptr(&r, 133, 0, &words);
  T_EQ(words, 16 - (uint32_t)((r.wr & 63) / 4));
  pc_logring_commit(&r, words, 133, 0);
  p = pc_logring_wptr(&r, 133 + words, 0, &words);
  if(words) { pc_logring_commit(&r, words, r.next_word, 0); p = pc_logring_wptr(&r, r.next_word, 0, &words); }
  T_EQ(words, 0);
  T_EQ(pc_logring_unread(&r), 64);

  //Segment slots: PC_LOGRING_SEGS unread segments, the next gap is refused until one is read
  pc_logring_reset(&r, ring_mem, 1u << 16, 1);
  for(i=0;i<PC_LOGRING_SEGS;i++)
  {
    p = pc_logring_wptr(&r, 10 * i, PC_LOG_CF_GAP, &words);
    T_TRUE(words > 0);
    pc_logring_commit(&r, 1, 10 * i, PC_LOG_CF_GAP);
  }
  pc_logring_wptr(&r, 10 * i, PC_LOG_CF_GAP, &words);
  T_EQ(words, 0);
  pc_logring_wptr(&r, 10 * (i - 1) + 1, 0, &words);     //continuing the last segment needs no slot
  T_TRUE(words > 0);
  n = pc_logring_read(&r, out, 64, &w, &f);
  T_EQ(n, 1); T_EQ(w, 0);
  n = pc_logring_read(&r, out, 64, &w, &f);              //moves into segment 1: segment 0 is free now
  T_EQ(w, 10);
  pc_logring_wptr(&r, 10 * i, PC_LOG_CF_GAP, &words);
  T_TRUE(words > 0);
}

//----------------------------------------------------------------------------------------------------------------------------------
// The bus side against the model

//The byte a lane holds for word counter n (fpga_model.c: bytes of the index, or the peak pair of the window)
static uint8_t lane_byte(uint8_t lane, uint32_t n)
{
  uint8_t mn, mx;

  if(fm_peak_on())
  {
    fm_peak_word((lane >= 0x22) ? 1 : 0, n, &mn, &mx);
    return (lane & 1) ? mx : mn;
  }

  return (uint8_t)(n >> (8 * (lane - 0x20)));
}

typedef struct
{
  uint64_t next;                //word counter expected next
  uint64_t words, gaps, lost, host_gaps;
  uint32_t bad;                 //bytes that differ from the model
  uint32_t seq_gaps_without_flag;
} log_check;

static void check_chunk(const fpga_log *l, log_check *c, const uint8_t *b, uint32_t n, uint64_t w, uint8_t f)
{
  uint32_t i, k, bpw = l->nlanes;

  if(w != c->next)
  {
    if(!(f & PC_LOG_CF_GAP) || (w < c->next))
      c->seq_gaps_without_flag++;

    c->lost += w - c->next;
  }

  if(f & PC_LOG_CF_GAP)
    c->gaps++;

  if(f & PC_LOG_CF_HOST)
    c->host_gaps++;

  for(i=0;i<n/bpw;i++)
    for(k=0;k<bpw;k++)
      if(b[i * bpw + k] != lane_byte(l->lanes[k], (uint32_t)(w + i)))
        c->bad++;

  c->words += n / bpw;
  c->next   = w + n / bpw;
}

//Configure the model for a format at a divider: K = 2 (CIC2) for 1 / 2, peak for 3, legacy for 0
static void model_rate(uint8_t format, uint32_t divider, uint32_t seed)
{
  fm_reset(0x153D, divider, seed);
  fm.capture = 1;

  if((format == FPGA_LOG_F_HIGH) || (format == FPGA_LOG_F_16))
    fm.reg_53 = 2;
  else if(format == FPGA_LOG_F_PEAK)
  {
    fm.reg_53 = 1;
    fm.reg_56 = 2;
  }
}

typedef struct
{
  uint32_t usb_ps_per_byte;     //0: the host reads at once and costs nothing
  uint32_t max_read;            //LOG_READ max_bytes
  uint64_t busy_ps;             //CPU time spent on drain steps that moved words or handled a gap, builds and packets
  uint64_t elapsed_ps;
  uint32_t reads;
} log_host;

//The log loop for run_ps of model time (or until the log ends): drain, then the USB side
static void run_loop(fpga_log *l, log_check *c, log_host *h, uint64_t run_ps)
{
  uint64_t t0 = fm.t_ps, tx_left = 0, next_read = fm.t_ps;

  while((fm.t_ps - t0) < run_ps)
  {
    uint64_t ts = fm.t_ps;
    uint32_t gaps = l->gaps;

    //A step that only read 0x38 is the loop waiting for data: spare time, not counted
    if(fpga_log_drain(l) || (l->gaps != gaps))
      h->busy_ps += fm.t_ps - ts;

    if(!tx_left && (fm.t_ps >= next_read))
    {
      uint64_t w;
      uint8_t  f;
      uint32_t n = pc_logring_read(&l->ring, chunk, h->max_read, &w, &f);

      if(n)
      {
        check_chunk(l, c, chunk, n, w, f);
        h->reads++;

        if(h->usb_ps_per_byte)
        {
          fm_advance_ps(BUILD_PS_PER_BYTE * (n + 36));
          h->busy_ps += BUILD_PS_PER_BYTE * (n + 36);
          tx_left = n + 36;                        //header 16, frame 20
        }
      }
    }

    if(tx_left)
    {
      uint64_t pkt = (tx_left > 512) ? 512 : tx_left;

      fm_advance_ps(pkt * h->usb_ps_per_byte);
      h->busy_ps += pkt * h->usb_ps_per_byte;
      tx_left -= pkt;

      if(!tx_left)
        next_read = fm.t_ps + (h->usb_ps_per_byte ? HOST_RTT_PS : 0);
    }
    else
      fm_advance_ns(IDLE_NS);

    if(l->state != FPGA_LOG_RUNNING)
      break;
  }

  h->elapsed_ps += fm.t_ps - t0;
}

//Read what is left after the end
static void drain_rest(fpga_log *l, log_check *c)
{
  uint64_t w;
  uint8_t  f;
  uint32_t n;

  while((n = pc_logring_read(&l->ring, chunk, sizeof(chunk), &w, &f)) != 0)
    check_chunk(l, c, chunk, n, w, f);
}

static void test_start(void)
{
  fpga_log l;
  int i;

  //0x153D: 0x69 = 1, 0x57 = 0, 0x1A = 0 before the reset, 0x0E = 0 in it, 0x6A = base = 0 after the release, 0x69 = 3
  model_rate(0, 399, 1);
  T_EQ(fpga_log_start(&l, 1, 0, 0, ring_mem, 1u << 16, 100000), 0);
  T_EQ(l.state, FPGA_LOG_RUNNING);
  T_EQ(l.base, 0);
  T_EQ(fm.trace[0].cmd, 0x69); T_EQ(fm.trace[0].value, 1);
  T_EQ(fm.trace[1].cmd, 0x57); T_EQ(fm.trace[1].value, 0);
  T_EQ(fm.trace[2].cmd, 0x1A); T_EQ(fm.trace[2].value, 0);
  i = fm_trace_find(0x01, 1, 0);
  T_TRUE(i > 2);
  T_TRUE(fm_trace_find(0x0E, 0, (uint32_t)i) > i);
  T_TRUE(fm_trace_find(0x01, 0, (uint32_t)i) > fm_trace_find(0x0E, 0, (uint32_t)i));
  i = fm_trace_find(0x6A, 0, (uint32_t)fm_trace_find(0x01, 0, (uint32_t)i));
  T_TRUE(i > 0);
  T_TRUE(fm_trace_find(0x69, 3, (uint32_t)i) > i);
  T_EQ(fm.reg_69, 1);
  T_EQ(fm.lap, 0);
  T_TRUE(fm.occ < 100);                                //the unread count from 0x6A on

  //A lap flag left over from an earlier session (0x01 = 1 keeps it) is cleared by the start
  fm.lap = 1;
  fpga_log_start(&l, 1, 0, 0, ring_mem, 1u << 16, 100000);
  T_EQ(fm.lap, 0);
  T_EQ(l.gaps, 0);
  T_EQ(l.nlanes, 2); T_EQ(l.lanes[0], 0x21); T_EQ(l.lanes[1], 0x20);

  //The lanes per format and mask
  fpga_log_start(&l, 3, FPGA_LOG_F_LEGACY, 0, ring_mem, 1u << 16, 100000);
  T_EQ(l.nlanes, 4); T_EQ(l.lanes[2], 0x23); T_EQ(l.lanes[3], 0x22);
  fpga_log_start(&l, 2, FPGA_LOG_F_HIGH, 0, ring_mem, 1u << 16, 100000);
  T_EQ(l.nlanes, 1); T_EQ(l.lanes[0], 0x23);
  fpga_log_start(&l, 3, FPGA_LOG_F_16, 0, ring_mem, 1u << 16, 100000);
  T_EQ(l.nlanes, 4); T_EQ(l.lanes[0], 0x20); T_EQ(l.lanes[1], 0x21); T_EQ(l.lanes[2], 0x22); T_EQ(l.lanes[3], 0x23);
  T_EQ(fpga_log_bpw(0, 0), 0); T_EQ(fpga_log_bpw(4, 0), 0); T_EQ(fpga_log_bpw(1, 4), 0);
  T_EQ(fpga_log_bpw(1, 1), 1); T_EQ(fpga_log_bpw(3, 1), 2); T_EQ(fpga_log_bpw(1, 3), 2); T_EQ(fpga_log_bpw(3, 0), 4);

  //Stop: 0x69 = 0 first, the writes stop at the next edge, the tail is read up to the frozen counter; a second stop does
  //nothing
  fm_advance_ns(1000000);
  fm.ntrace = 0;
  fpga_log_stop(&l, FPGA_LOG_STOPPED);
  T_EQ(l.state, FPGA_LOG_STOPPED); T_EQ(fm.reg_69, 0);
  T_EQ(fm.trace[0].cmd, 0x69); T_EQ(fm.trace[0].value, 0);
  T_EQ(l.consumed, fm.n);
  T_TRUE(l.consumed > 100);
  T_EQ(pc_logring_unread(&l.ring), l.consumed * 4);
  fm.ntrace = 0;
  fpga_log_stop(&l, FPGA_LOG_ERROR);
  T_EQ(l.state, FPGA_LOG_STOPPED); T_EQ(fm.ntrace, 0);
  T_EQ(fpga_log_drain(&l), 0);

  //0x153C does not stream: 0x38 reads 0, refused, 0x69 = 0 written
  fm_reset(0x153C, 399, 2);
  T_EQ(fpga_log_start(&l, 1, 0, 0, ring_mem, 1u << 16, 100000), -2);
  T_EQ(l.state, FPGA_LOG_ERROR);
  T_EQ(fm.trace[fm.ntrace - 1].cmd, 0x69); T_EQ(fm.trace[fm.ntrace - 1].value, 0);

  //A hung FPGA: the bounded ready wait
  model_rate(0, 399, 3);
  fm.ready_never = 1;
  T_EQ(fpga_log_start(&l, 1, 0, 0, ring_mem, 1u << 16, 50), -1);
  T_EQ(l.state, FPGA_LOG_ERROR);
}

//Every format and channel mask: the bytes are the model's, the counters continuous, the DDR ring wraps many times
static void test_formats(void)
{
  static const uint32_t dividers[4] = { 399, 399, 799, 799 };    //each format at a rate of its tier with one channel
  uint8_t fmt, mask;

  for(fmt=0;fmt<4;fmt++)
    for(mask=1;mask<=3;mask++)
    {
      fpga_log  l;
      log_check c;
      log_host  h;

      memset(&c, 0, sizeof(c));
      memset(&h, 0, sizeof(h));
      h.max_read = 3000 + 7 * mask;                      //odd sizes: chunks end inside the DDR ring's wrap
      model_rate(fmt, dividers[fmt] * mask, 10 * fmt + mask);
      T_EQ(fpga_log_start(&l, mask, fmt, 0, ring_mem, 1u << 14, 100000), 0);
      run_loop(&l, &c, &h, 200000000000ull);             //200 ms
      fpga_log_stop(&l, FPGA_LOG_STOPPED);
      drain_rest(&l, &c);

      T_EQ(c.bad, 0);
      T_EQ(c.gaps, 0);
      T_EQ(c.seq_gaps_without_flag, 0);
      T_EQ(l.gaps, 0);
      T_TRUE(c.words > 10000);
      T_EQ(c.words, l.consumed);
      T_TRUE(l.ring.wr > 2 * (1u << 14));                //wrapped at least twice
    }
}

//The FPGA ring laps (the MCU stalled 40 ms): one gap, the counter jumps by the words lost, the data after it is right
static void test_lap(void)
{
  fpga_log  l;
  log_check c;
  log_host  h;

  memset(&c, 0, sizeof(c));
  memset(&h, 0, sizeof(h));
  h.max_read = 65536;
  model_rate(FPGA_LOG_F_HIGH, 399, 7);                   //500 kwords/s: the ring holds 16.4 ms
  T_EQ(fpga_log_start(&l, 1, FPGA_LOG_F_HIGH, 0, ring_mem, 1u << 20, 100000), 0);
  run_loop(&l, &c, &h, 20000000000ull);
  T_EQ(c.gaps, 0);

  fm_advance_ns(40000000);                               //a 40 ms stall
  run_loop(&l, &c, &h, 20000000000ull);
  fpga_log_stop(&l, FPGA_LOG_STOPPED);
  drain_rest(&l, &c);

  T_EQ(c.gaps, 1); T_EQ(l.gaps, 1); T_EQ(c.host_gaps, 0);
  T_EQ(c.seq_gaps_without_flag, 0);
  T_EQ(c.bad, 0);
  T_EQ(c.lost, l.lost);
  T_TRUE(l.lost >= 20000 - 8192);                        //40 ms at 500 kwords/s minus what the ring held
  T_TRUE(l.flags & FPGA_LOG_FL_LAP);
  T_FALSE(l.flags & FPGA_LOG_FL_DDR);
  T_TRUE(fm.laps_seen >= 1);
  T_EQ(c.words + c.lost, l.consumed);

  //A lap flag with no words lost (the detector alone): the chunk carries the gap flag, the counter does not jump
  memset(&c, 0, sizeof(c));
  model_rate(0, 399, 8);
  fpga_log_start(&l, 1, 0, 0, ring_mem, 1u << 20, 100000);
  run_loop(&l, &c, &h, 5000000000ull);
  fm.lap = 1;
  run_loop(&l, &c, &h, 5000000000ull);
  fpga_log_stop(&l, FPGA_LOG_STOPPED);
  drain_rest(&l, &c);
  //(the words still waiting in the FPGA ring are skipped with it: the drain resumes at the newest word)
  T_EQ(c.gaps, 1); T_TRUE(c.lost < 2000); T_EQ(c.lost, l.lost); T_EQ(c.bad, 0); T_EQ(l.gaps, 1);
}

//The DDR ring full (the host does not read): the drain stops reading, the FPGA ring laps, the gap carries HOST
static void test_ddr_full(void)
{
  fpga_log  l;
  log_check c;
  log_host  h;
  uint64_t  t0;

  memset(&c, 0, sizeof(c));
  memset(&h, 0, sizeof(h));
  h.max_read = 65536;
  model_rate(0, 399, 9);                                 //1 ch legacy 500 kS/s: 500 kB/s
  T_EQ(fpga_log_start(&l, 1, 0, 0, ring_mem, 1u << 15, 100000), 0);

  //No reads for 200 ms: 32 KiB fill in 65 ms, then the FPGA ring laps
  t0 = fm.t_ps;
  while(fm.t_ps - t0 < 200000000000ull)
  {
    fpga_log_drain(&l);
    fm_advance_ns(IDLE_NS);
  }

  T_EQ(pc_logring_unread(&l.ring), 1u << 15);
  T_TRUE(l.ddr_full || l.gaps);

  run_loop(&l, &c, &h, 50000000000ull);
  fpga_log_stop(&l, FPGA_LOG_STOPPED);
  drain_rest(&l, &c);

  T_TRUE(c.gaps >= 1); T_TRUE(c.host_gaps >= 1); T_EQ(c.seq_gaps_without_flag, 0); T_EQ(c.bad, 0);
  T_TRUE(l.flags & FPGA_LOG_FL_DDR);
  T_EQ(c.words, l.ring.wr / l.nlanes);
  T_TRUE(c.lost > 25000);                                //50k words in 200 ms, 16k held
  T_TRUE(c.lost <= l.lost);
}

//Duration: exactly the limit, then ENDED with 0x69 = 0
static void test_limit(void)
{
  fpga_log  l;
  log_check c;
  log_host  h;

  memset(&c, 0, sizeof(c));
  memset(&h, 0, sizeof(h));
  h.max_read = 4096;
  model_rate(FPGA_LOG_F_16, 799, 11);                    //250 kwords/s
  T_EQ(fpga_log_start(&l, 3, FPGA_LOG_F_16, 25000, ring_mem, 1u << 20, 100000), 0);   //100 ms
  run_loop(&l, &c, &h, 500000000000ull);
  drain_rest(&l, &c);
  T_EQ(l.state, FPGA_LOG_ENDED);
  T_EQ(fm.reg_69, 0);
  T_EQ(c.words, 25000);
  T_EQ(c.bad, 0); T_EQ(c.gaps, 0);
  T_EQ(fpga_log_drain(&l), 0);
}

//The contract's rates (section 6.2): 500 kS/s 1 ch 8-bit, 250 kS/s 1 ch 16-bit / peak or 2 ch 8-bit, 125 kS/s 2 ch
//16-bit, 2 s each at the worst case (0.35 us per bus byte, 1.0 us per USB byte) without a gap; the duty is printed
typedef struct
{
  const char *name;
  uint8_t  format, mask;
  uint32_t divider;
} log_tier;

static const log_tier tiers[] =
{
  { "1 ch 8-bit legacy 500 kS/s",   FPGA_LOG_F_LEGACY, 1, 399 },
  { "1 ch 8-bit filtered 500 kS/s", FPGA_LOG_F_HIGH,   1, 399 },
  { "1 ch 16-bit 250 kS/s",         FPGA_LOG_F_16,     1, 799 },
  { "1 ch peak 250 kS/s",           FPGA_LOG_F_PEAK,   1, 799 },
  { "2 ch 8-bit legacy 250 kS/s",   FPGA_LOG_F_LEGACY, 3, 799 },
  { "2 ch 8-bit filtered 250 kS/s", FPGA_LOG_F_HIGH,   3, 799 },
  { "2 ch 16-bit 125 kS/s",         FPGA_LOG_F_16,     3, 1599 },
};

static uint32_t run_tier(const log_tier *t, uint32_t strobe_ns, uint32_t usb_ps, uint32_t ring_size, uint64_t run_ps,
                         log_check *c, fpga_log *l, uint32_t *duty_pm)
{
  log_host h;

  memset(c, 0, sizeof(*c));
  memset(&h, 0, sizeof(h));
  h.max_read        = 65536;
  h.usb_ps_per_byte = usb_ps;
  model_rate(t->format, t->divider, t->divider + t->mask);
  fm.strobe_ns = strobe_ns;
  fpga_log_start(l, t->mask, t->format, 0, ring_mem, ring_size, 100000);
  run_loop(l, c, &h, run_ps);
  *duty_pm = (uint32_t)(h.busy_ps * 1000 / h.elapsed_ps);
  fpga_log_stop(l, FPGA_LOG_STOPPED);
  drain_rest(l, c);
  return (uint32_t)(c->words * l->nlanes * 1000000000000ull / h.elapsed_ps);
}

static void test_rates(void)
{
  uint32_t i;

  for(i=0;i<sizeof(tiers)/sizeof(tiers[0]);i++)
  {
    const log_tier *t = &tiers[i];
    fpga_log  l;
    log_check c;
    uint32_t  duty_best, duty_worst, bps;

    //Best case: 0.30 us per bus byte, 0.43 us per USB byte
    run_tier(t, 300, 430000, 1u << 20, 1000000000000ull, &c, &l, &duty_best);
    T_EQ(c.gaps, 0); T_EQ(c.bad, 0);

    //Worst case: 0.35 us, 1.0 us; 2 s
    bps = run_tier(t, 350, 1000000, 1u << 20, 2000000000000ull, &c, &l, &duty_worst);
    T_EQ(c.gaps, 0); T_EQ(c.bad, 0); T_EQ(l.gaps, 0);
    T_TRUE(duty_worst < 900);
    T_TRUE(bps >= 499000);                                //500 kB/s through, all tiers
    printf("  %-30s %u B/s, CPU duty %u.%u %% (best) %u.%u %% (worst), %u steps, %u chunks\n", t->name, bps,
           duty_best / 10, duty_best % 10, duty_worst / 10, duty_worst % 10, l.steps, l.chunks);
  }

  //Twice the 1 ch 8-bit tier at the worst case with a 256 KiB DDR ring: the host falls behind, the ring fills, gaps with
  //HOST follow, the log continues and every byte delivered is right
  {
    log_tier  t = { "2x: 1 ch 8-bit 1 MS/s", FPGA_LOG_F_LEGACY, 1, 199 };
    fpga_log  l;
    log_check c;
    uint32_t  duty, bps;

    bps = run_tier(&t, 350, 1000000, 1u << 18, 2000000000000ull, &c, &l, &duty);
    T_TRUE(c.gaps >= 1); T_TRUE(c.host_gaps >= 1); T_EQ(c.bad, 0); T_EQ(c.seq_gaps_without_flag, 0);
    T_EQ(c.words, l.ring.wr / l.nlanes);
    T_TRUE(c.lost <= l.lost);                            //a gap after the last data is not visible in a chunk
    T_TRUE(c.words > 500000);
    printf("  %-30s %u B/s delivered, %u gaps, %llu words lost of %llu\n", t.name, bps, l.gaps,
           (unsigned long long)l.lost, (unsigned long long)l.consumed);
  }
}

int main(void)
{
  test_ring();
  test_start();
  test_formats();
  test_lap();
  test_ddr_full();
  test_limit();
  test_rates();
  return t_report("test_log");
}
