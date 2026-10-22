//----------------------------------------------------------------------------------------------------------------------------------
// pc_seq_hw.c - the sequencer on the scope (pc_seq_hw.h): the pc_hal.h sequencer functions and the UI's averaging.
// Design: docs/protocol.md section 14 row 56, firmware/README.md "Averaging".
//
// UI averaging (fw-one: one behaviour, owner decision 2026-10-20 "consistency matters"). With the acquisition menu's
// Average on (the conditions of 1.00o5's software average: auto or normal trigger mode, not the waveform viewer, not roll
// mode) every displayed record is the mean of N records, N from one planner for both paths (pc_seq.h). A legacy record
// (K = 0) that fits the accumulator (3000-sample depth = 1500 words <= 2046) is one hardware averaging run of N records;
// the readout takes the lane sums (S = 0) and feeds fpga_read_adc_data the corrected codes of the mean (pc_seq_avg_code).
// Every other record (16-bit, or deeper than 4092 samples) is averaged by the MCU: the conversion the main loop armed is
// record 1; once it is done and triggered, pc_seq_ui_done arms the same conversion N - 1 times more, reads each record's
// lanes and sums them per sample (32-bit sums, exact mean), then feeds the mean the same way. The MCU pass costs N x
// (record + readout), which the planner keeps within the 40 ms budget (3000 16-bit samples at 1 MSa/s: 3 ms record + 3.6
// ms readout, N = 4; at 200 MSa/s legacy with 6000 samples: 30 us + 3.6 ms, N = 8; 10 kSa/s: N = 1, single records).
// Atlan's running (trace + previous) / 2 average is gone: the same picture whatever the filter and time base.
//
// Deadline, both paths: a run (or the MCU's further records) not done by max(100 ms, two records) + N records is stopped
// (hardware: 0x5D = 1, 0x1A = 0; MCU: 0x1A = 0) once it holds at least one record, and shows the mean of what it has.
// With none (auto mode without triggers) the next UI conversions are single records until one of them triggers (the
// MCU path's first record is the main loop's conversion, which the firmware's auto time-out ends untriggered: shown as
// it is). In normal mode with no record yet the run, or the main loop's first conversion, keeps waiting.
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>

#include "types.h"
#include "variables.h"
#include "fpga_control.h"
#include "pc_hooks.h"
#include "pc_hal.h"
#include "pc_cmds.h"
#include "pc_seq.h"
#include "pc_seq_hw.h"
#include "fpga_bus.h"
#include "adc_cal.h"

static uint8  seq_57 = 0xFF;            //0x57 as last written; 0xFF unknown (an MCU restart leaves the FPGA running)

//UI averaging
#define UI_RING   8192                  //sample memory words (AL3)

static uint8  ui_run;                   //the UI conversion in flight is a hardware averaging run
static uint8  ui_mcu;                   //...or record 1 of an MCU-summed mean (pc_seq_ui_done takes the rest)
static uint8  ui_rearm;                 //fpga_do_conversion called by the MCU loop (pc_seq_ui_conversion keeps the plan)
static uint8  ui_stopped;               //the hardware run's deadline stopped it
static uint8  ui_auto;                  //auto trigger mode: stop at the deadline even without a record
static uint8  ui_fallback;              //hardware path: single records until one triggers (last run had no record)
static uint8  ui_feed;                  //the readout in progress takes the mean
static uint8  ui_wide;                  //16-bit layout (one sample per word)
static uint8  ui_state;                 //GET_STATUS 46: 0 Average off, 1 the MCU's mean (log2n 0: single records), 2 hardware
static uint8  ui_log2n;                 //GET_STATUS 47: log2 N planned
static uint32 ui_n, ui_total, ui_pre;
static uint32 ui_t0, ui_deadline;
static uint32 ui_count;                 //records in the finished run / summed by the MCU
static uint8  ui_lane[4][PC_SEQ_ACC_WORDS];       //the hardware run's lane sums
static uint32 ui_sum[2][UI_RING];                 //the MCU's sums per channel: legacy [2 w] core B, [2 w + 1] core A; 16-bit [w]
static uint16 ui_tmp[UI_RING];                    //one lane / one channel of one record

//----------------------------------------------------------------------------------------------------------------------------------
// FPGA access

static void seq_w8(uint8 cmd, uint8 v)
{
  fpga_write_cmd(cmd);
  fpga_write_byte(v);
}

void pc_seq_hw_pre_reset(const pc_seq_cfg *c)
{
  //The slot mask (0x57 / 0x59) loads only on write-clock edges while 0x01 = 1 on 0x1536 (fpga-gen6): written before the
  //reset, as the geometry is on every bitstream since fw 0.2.72 (fpga_arm_reset, fpga_bus.h)
  if(c && c->mode)
  {
    seq_57 = pc_seq_cmd57(c, 0);
    seq_w8(0x57, seq_57);
    fpga_write_cmd(0x58);
    fpga_write_short(pc_seq_cmd58(c));
    seq_w8(0x59, c->m & 0x0F);
    fpga_write_cmd(0x5A);
    fpga_write_short(pc_seq_cmd5a(c));
    seq_w8(0x5B, 0);
    fpga_write_cmd(0x5C);
    fpga_write_int(pc_seq_holdoff_units(c->holdoff_ns));
  }
  else if(seq_57 != 0)
  {
    seq_57 = 0;
    seq_w8(0x57, 0);
  }
}

void pc_hal_seq_status(uint8_t raw[8])
{
  uint8_t b[8];
  int i, tries;

  //The count can change between its bytes while a run is active: read until two reads agree
  for(tries=0;tries<4;tries++)
  {
    fpga_write_cmd(0x2A);

    for(i=0;i<8;i++)
      b[i] = fpga_read_byte();

    if(tries && (memcmp(b, raw, 8) == 0))
      return;

    for(i=0;i<8;i++)
      raw[i] = b[i];
  }
}

void pc_hal_seq_stop(void)
{
  //Stop first, so the sequencer ends in its waiting state; then auto mode ends the record waiting for a trigger now
  seq_w8(0x5D, 0x01);
  seq_w8(0x1A, 0x00);
}

void pc_hal_seq_view(uint8_t mode57, uint8_t shift)
{
  seq_w8(0x5B, shift);
  seq_w8(0x57, mode57);
  seq_57 = mode57;
}

void pc_hal_seq_read(uint8_t byte, uint32_t first, uint32_t count, uint8_t *dst, uint32_t stride)
{
  //The view word is latched one i_mcu_clk edge after the pointer moves, as 0x20..0x23: the same dummy strobe (fpga_bus.h)
  fpga_read_start((uint8_t)(0x2B + (byte & 7)), (uint16_t)(first & (PC_SEQ_ACC_WORDS - 1)));

  while(count--)
  {
    FPGA_PULSE_CLK();
    *dst = FPGA_GET_DATA();
    dst += stride;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// UI averaging

int pc_seq_ui_conversion(int ui, const pc_decim_cfg *c, uint32 divider)
{
  uint32   n, log2n;
  uint32_t run_us;
  int      hw;

  //The MCU loop's re-arm of record 2..N: the plan stands
  if(ui_rearm)
    return 0;

  ui_run     = 0;
  ui_mcu     = 0;
  ui_stopped = 0;
  ui_feed    = 0;

  if(!ui)
    return 0;

  //C6 peak detect: an averaging run would mix the max and min bytes of the words (docs/plans/fpga-0x153c.md section 4):
  //Average is not applied while Sample is Peak (GET_STATUS 46 = 0; the ACQ menu greys the box, docs/plans/ui-rows.md 3.2).
  //ui-sample: the setting decides, not the record: at 200 MSa/s (and over 8190 samples) Peak falls back to the 8-bit
  //record, and the menu still says "Average not applied", so no average runs there either
  if(!scopesettings.average_mode || scopesettings.waveviewmode || scopesettings.long_mode || c->peak || pc_glue_sample_peak() ||
     ((scopesettings.triggermode != 0) && (scopesettings.triggermode != 2)))
  {
    ui_state = 0;
    ui_log2n = 0;
    return 0;
  }

  ui_wide  = (uint8)pc_decim_wide(c);
  ui_total = pc_glue_decim_words(fpgasettings.totalsamples);
  ui_pre   = pc_glue_decim_pre_words(fpgasettings.settriggerpoint);
  ui_auto  = (uint8)(scopesettings.triggermode == 0);
  ui_count = 0;

  //The path: the FPGA for a legacy record that fits its accumulator, the MCU for every other record; one planner
  hw = !ui_wide && (ui_total <= PC_SEQ_AVG_MAX_TOTAL);
  n  = hw ? pc_seq_ui_plan(ui_total, ui_pre, divider, &run_us) : pc_seq_ui_plan_mcu(ui_total, ui_pre, divider, ui_wide, &run_us);

  if(!n)
  {
    //Two records do not fit the budget: single records (the mean of one), whatever the path would have been
    ui_state = 1;
    ui_log2n = 0;
    return 0;
  }

  for(log2n=0;(1u << log2n)<n;log2n++)
    ;

  ui_n        = n;
  ui_log2n    = (uint8)log2n;
  ui_deadline = pc_auto_deadline_ms(scopesettings.samplecount, divider) + run_us / 1000 + 1;
  ui_state    = (uint8)(hw ? 2 : 1);

  if(!hw)
  {
    ui_mcu = 1;
    return 0;
  }

  if(ui_fallback)
    return 0;

  ui_run = 1;
  ui_t0  = timer0ticks;
  return 1;
}

void pc_seq_ui_pre_reset(void)
{
  pc_seq_cfg c;

  c.mode       = ui_run ? PC_SEQ_AVERAGE : PC_SEQ_OFF;
  c.flags      = 0;
  c.m          = 0;
  c.count      = 1;
  c.n          = ui_n;
  c.holdoff_ns = 0;

  pc_seq_hw_pre_reset(&c);
}

void pc_seq_ui_service(void)
{
  uint8_t raw[8];
  pc_seq_status st;

  if(!ui_run || ui_stopped || !scopesettings.runstate || scopesettings.display_data_done ||
     ((timer0ticks - ui_t0) < ui_deadline))
    return;

  pc_hal_seq_status(raw);
  pc_seq_status_decode(raw, &st);

  if(st.flags & PC_SEQ_ST_DONE)
    return;

  if(st.count || ui_auto)
  {
    pc_hal_seq_stop();
    ui_stopped = 1;
  }
}

//One record into the sums: the enabled channels' lanes from ring word start (the 0x0B words before 0x14)
static void ui_mcu_add(uint32 start)
{
  uint32 ch, w, words = ui_total;

  if(words > UI_RING)
    words = UI_RING;

  for(ch=0;ch<2;ch++)
  {
    PCHANNELSETTINGS s = ch ? &scopesettings.channel2 : &scopesettings.channel1;
    uint32 *sum = ui_sum[ch];

    if(!s->enable)
      continue;

    if(ui_wide)
    {
      fpga_read_decim_data(s->adc1command, s->adc2command, start % UI_RING, words, ui_tmp);

      for(w=0;w<words;w++)
        sum[w] += ui_tmp[w];
    }
    else
    {
      uint8 *b = (uint8 *)ui_tmp;

      //Core A (0x20 / 0x22) to the odd positions, core B (0x21 / 0x23) to the even ones, as fpga_read_sample_data stores them
      pc_hal_read_core((uint8_t)(2 * ch), start, words, b, 1, UI_RING);

      for(w=0;w<words;w++)
        sum[2 * w + 1] += b[w];

      pc_hal_read_core((uint8_t)(2 * ch + 1), start, words, b, 1, UI_RING);

      for(w=0;w<words;w++)
        sum[2 * w] += b[w];
    }
  }
}

//Records 2..N of the MCU path, armed and summed in this pass (the first one, at addr, is the main loop's conversion)
static void ui_mcu_collect(uint32 addr)
{
  uint32 bytes = (ui_wide ? ui_total : 2 * ui_total) * sizeof(uint32);

  if(bytes > sizeof(ui_sum[0]))
    bytes = sizeof(ui_sum[0]);

  memset(ui_sum[0], 0, bytes);
  memset(ui_sum[1], 0, bytes);
  ui_mcu_add(pc_glue_ui_read_start(addr));
  ui_count = 1;
  ui_t0    = timer0ticks;

  while(ui_count < ui_n)
  {
    int done;

    //The same conversion again (the UI's filter setting). ui_rearm keeps the plan through every fpga_do_conversion of
    //this record, including the full-rate check's own re-arm inside fpga_done_conversion (enc_heal_hw.c)
    ui_rearm = 1;
    pc_glue_decim_rearm();
    fpga_do_conversion();

    //Its record, or the deadline: no trigger in time ends the pass with the mean of the records it holds (auto and
    //normal mode alike: this record never waits beyond the deadline)
    while(!(done = fpga_done_conversion()) && ((timer0ticks - ui_t0) < ui_deadline))
      PC_BUSY_POLL();

    if(!done)
    {
      uint32 t1 = timer0ticks;

      //0x1A = 0 ends the capture at once; the partial record is discarded (0x14 read to leave the FPGA as a done one)
      pc_hal_force();

      while(!fpga_done_conversion() && ((timer0ticks - t1) < 50))
        PC_BUSY_POLL();

      (void)fpga_prepare_for_transfer();
      ui_rearm = 0;
      break;
    }

    ui_rearm = 0;
    ui_mcu_add(pc_glue_ui_read_start(fpga_prepare_for_transfer()));
    ui_count++;
  }

  ui_feed = 1;
}

void pc_seq_ui_done(uint8_t *triggered, uint32 addr)
{
  uint8_t raw[8];
  pc_seq_status st;

  if(ui_mcu)
  {
    //A triggered first record starts the rest; an untriggered one (the auto time-out) is shown as it is, and the next
    //conversion tries again
    ui_mcu = 0;

    if(*triggered)
      ui_mcu_collect(addr);

    return;
  }

  if(!ui_run)
  {
    //A single record after an empty hardware run: back to the hardware once one triggers
    if(ui_fallback && *triggered)
      ui_fallback = 0;

    return;
  }

  ui_run = 0;
  pc_hal_seq_status(raw);
  pc_seq_status_decode(raw, &st);
  ui_count = st.count;

  if(ui_count > ui_n)
    ui_count = ui_n;

  //The record shown is the mean of the counted records, triggered by construction; none: the sample memory's record
  //(ended by the stop, untriggered) and single records from now on
  *triggered  = (uint8_t)(ui_count != 0);
  ui_feed     = (uint8)(ui_count != 0);
  ui_fallback = (uint8)(ui_count == 0);
}

int pc_seq_ui_feed(void *settings, int16 *feed, uint32 n)
{
  PCHANNELSETTINGS s = (PCHANNELSETTINGS)settings;
  uint32 ch, w, r, words = ui_total, b;

  if(!ui_feed || !ui_count)
    return 0;

  if(s == &scopesettings.channel1)
    ch = 0;
  else if(s == &scopesettings.channel2)
    ch = 1;
  else
    return 0;

  r = pc_seq_recip(ui_count);

  if(ui_state == 2)
  {
    if(words * 2 > n)
      words = n / 2;

    //Lanes of the channel: core A = L0 / L2 (bytes 0-1 / 4-5), core B = L1 / L3 (bytes 2-3 / 6-7); sums at S = 0
    for(b=0;b<4;b++)
      pc_hal_seq_read((uint8_t)(ch * 4 + b), 0, words, ui_lane[b], 1);

    //B before A in each word, as fpga_read_sample_data stores them: odd = core A (first pass), even = core B
    for(w=0;w<words;w++)
    {
      uint32 sa = ui_lane[0][w] | ((uint32)ui_lane[1][w] << 8);
      uint32 sb = ui_lane[2][w] | ((uint32)ui_lane[3][w] << 8);

      feed[2 * w + 1] = (int16)pc_seq_avg_code(sa, ui_count, r, s->gain_a, s->off_a);
      feed[2 * w]     = (int16)pc_seq_avg_code(sb, ui_count, r, s->gain_b, s->off_b);
    }

    for(w=words*2;w<n;w++)
      feed[w] = feed[words ? words * 2 - 1 : 0];

    return 1;
  }

  if(ui_wide)
  {
    //16-bit samples: the mean (exact, rounded) through the readout's conversion into the 8-bit trace domain, as
    //pc_glue_decim_read does for a single record (scale with gain16 folded in, corr16 added)
    pc_decim_cfg d;
    int    written;
    uint32 scale;

    pc_hal_decim_current(&d, &written);
    scale = acal_scale(d.scale_q24, s->gain16);

    if(words > n)
      words = n;

    for(w=0;w<words;w++)
      feed[w] = (int16)pc_decim_to_code((uint16_t)pc_seq_udiv(ui_sum[ch][w] + ui_count / 2, ui_count, r), scale, s->corr16);

    for(w=words;w<n;w++)
      feed[w] = feed[words ? words - 1 : 0];

    return 1;
  }

  if(words * 2 > n)
    words = n / 2;

  for(w=0;w<words;w++)
  {
    feed[2 * w + 1] = (int16)pc_seq_avg_code(ui_sum[ch][2 * w + 1], ui_count, r, s->gain_a, s->off_a);
    feed[2 * w]     = (int16)pc_seq_avg_code(ui_sum[ch][2 * w], ui_count, r, s->gain_b, s->off_b);
  }

  for(w=words*2;w<n;w++)
    feed[w] = feed[words ? words * 2 - 1 : 0];

  return 1;
}

void pc_seq_ui_read_done(void)
{
  ui_feed = 0;
}

int pc_seq_ui_running(void)
{
  return ui_run;
}

void pc_hal_ui_average(uint8_t *state, uint8_t *log2n)
{
  *state = scopesettings.average_mode ? ui_state : 0;
  *log2n = *state ? ui_log2n : 0;
}
