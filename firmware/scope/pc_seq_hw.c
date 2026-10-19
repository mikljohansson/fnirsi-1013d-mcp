//----------------------------------------------------------------------------------------------------------------------------------
// pc_seq_hw.c - the 0x1536 sequencer on the scope (pc_seq_hw.h): the pc_hal.h sequencer functions and the UI's hardware
// averaging. Design: docs/protocol.md section 14 row 70, firmware/README.md "Segmented capture and hardware averaging".
//
// UI hardware averaging. With the acquisition menu's Average on (the conditions of 1.00o5's software average: auto or
// normal trigger mode, not the waveform viewer, not roll mode) and a 0x1536 bitstream, a UI conversion of a legacy record
// (K = 0: filter off, or 200 MSa/s) that fits the accumulator (3000-sample depth = 1500 words <= 2046) is one averaging
// run of N records (pc_seq_ui_plan: N = 2..64, a power of two, N records within 40 ms). The readout takes the lane sums
// (S = 0) and feeds fpga_read_adc_data the corrected codes of the mean (pc_seq_avg_code), as the 16-bit path does; the
// software average (1.00o5's per-pass (trace + avg) / 2) is then skipped for that record. Everything else (16-bit
// records, deeper memory, slow rates where two records do not fit 40 ms) keeps the software average.
//
// Deadline: a run that is not done by max(100 ms, two records) + N records is stopped (0x5D = 1, 0x1A = 0) once it
// holds at least one record, and shows the mean of what it has. With none (auto mode without triggers) the next UI
// conversions are single records (the software average, as on older bitstreams) until one of them triggers. In normal
// mode with no record yet the run keeps waiting, like a normal-mode capture.
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

static uint8  seq_has;                  //bitstream with the sequencer
static uint8  seq_57 = 0xFF;            //0x57 as last written; 0xFF unknown (an MCU restart leaves the FPGA running)

//UI hardware averaging
static uint8  ui_run;                   //the UI conversion in flight is an averaging run
static uint8  ui_stopped;               //its deadline stopped it
static uint8  ui_auto;                  //auto trigger mode: stop it at the deadline even without a record
static uint8  ui_fallback;              //single records until one triggers (last run had no record)
static uint8  ui_feed;                  //the readout in progress takes the accumulator
static uint8  ui_rec_hw;                //the trace buffers hold a hardware average
static uint8  ui_state;                 //GET_STATUS: 0 Average off, 1 software, 2 hardware
static uint8  ui_log2n;
static uint32 ui_n, ui_total, ui_pre;
static uint32 ui_t0, ui_deadline;
static uint32 ui_count;                 //records in the finished run
static uint8  ui_lane[4][PC_SEQ_ACC_WORDS];

//----------------------------------------------------------------------------------------------------------------------------------
// FPGA access

static void seq_w8(uint8 cmd, uint8 v)
{
  fpga_write_cmd(cmd);
  fpga_write_byte(v);
}

void pc_seq_hw_init(int has)
{
  seq_has = (uint8)(has != 0);
}

void pc_seq_hw_pre_reset(const pc_seq_cfg *c, uint16 pre_words, uint16 total_words)
{
  if(!seq_has)
    return;

  //Geometry before the reset too: 0x0B feeds a write-clock register that loads only on write-clock edges while 0x01 = 1
  //(fpga-gen6, RTL reading); the 0x05 poll then guarantees edges in reset at every rate. Written again in the window.
  fpga_write_cmd(0x0B);
  fpga_write_short(pre_words);
  fpga_write_cmd(0x0C);
  fpga_write_short(total_words);

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
  fpga_write_cmd(0x1F);
  fpga_write_short((uint16)(first & (PC_SEQ_ACC_WORDS - 1)));
  fpga_write_cmd((uint8)(0x2B + (byte & 7)));

  FPGA_BUS_DIR_IN();
  FPGA_DATA_READ();

  //One dummy strobe: the view word is latched one i_mcu_clk edge after the pointer moves (as 0x20..0x23)
  FPGA_PULSE_CLK();

  while(count--)
  {
    FPGA_PULSE_CLK();
    *dst = FPGA_GET_DATA();
    dst += stride;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// UI hardware averaging

int pc_seq_ui_conversion(int ui, const pc_decim_cfg *c, uint32 divider)
{
  uint32 n, run_us, log2n;

  ui_run     = 0;
  ui_stopped = 0;
  ui_feed    = 0;

  if(!ui)
    return 0;

  if(!scopesettings.average_mode || scopesettings.waveviewmode || scopesettings.long_mode ||
     ((scopesettings.triggermode != 0) && (scopesettings.triggermode != 2)))
  {
    ui_state = 0;
    return 0;
  }

  ui_state = 1;

  if(!seq_has || c->k || (fpgasettings.fw_FPGA != 2) || ui_fallback)
    return 0;

  ui_total = fpgasettings.totalsamples / 2;
  ui_pre   = fpgasettings.settriggerpoint / 2;
  n = pc_seq_ui_plan(ui_total, ui_pre, divider, &run_us);

  if(!n)
    return 0;

  for(log2n=0;(1u << log2n)<n;log2n++)
    ;

  ui_run      = 1;
  ui_n        = n;
  ui_log2n    = (uint8)log2n;
  ui_auto     = (scopesettings.triggermode == 0);
  ui_count    = 0;
  ui_t0       = timer0ticks;
  ui_deadline = pc_auto_deadline_ms(scopesettings.samplecount, divider) + run_us / 1000 + 1;
  ui_state    = 2;
  return 1;
}

void pc_seq_ui_pre_reset(void)
{
  pc_seq_cfg c;

  if(!seq_has || (fpgasettings.fw_FPGA < 2))
    return;

  c.mode       = ui_run ? PC_SEQ_AVERAGE : PC_SEQ_OFF;
  c.flags      = 0;
  c.m          = 0;
  c.count      = 1;
  c.n          = ui_n;
  c.holdoff_ns = 0;

  pc_seq_hw_pre_reset(&c, (uint16)pc_glue_decim_pre_words(fpgasettings.settriggerpoint),
                      (uint16)pc_glue_decim_words(fpgasettings.totalsamples));
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

void pc_seq_ui_done(uint8_t *triggered)
{
  uint8_t raw[8];
  pc_seq_status st;

  if(!ui_run)
  {
    //A single record after an empty run: back to the hardware once one triggers
    if(ui_fallback && *triggered)
      ui_fallback = 0;

    ui_rec_hw = 0;
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
  ui_rec_hw   = ui_feed;
  ui_fallback = (uint8)(ui_count == 0);
}

int pc_seq_ui_feed(void *settings, int16 *feed, uint32 n)
{
  PCHANNELSETTINGS s = (PCHANNELSETTINGS)settings;
  uint32 ch, w, r, words = ui_total, b;

  if(!ui_feed)
    return 0;

  if(s == &scopesettings.channel1)
    ch = 0;
  else if(s == &scopesettings.channel2)
    ch = 1;
  else
    return 0;

  if(words * 2 > n)
    words = n / 2;

  //Lanes of the channel: core A = L0 / L2 (bytes 0-1 / 4-5), core B = L1 / L3 (bytes 2-3 / 6-7); sums at S = 0
  for(b=0;b<4;b++)
    pc_hal_seq_read((uint8_t)(ch * 4 + b), 0, words, ui_lane[b], 1);

  r = pc_seq_recip(ui_count);

  //B before A in each word, as fpga_read_sample_data stores them: odd = core A (first pass), even = core B
  for(w=0;w<words;w++)
  {
    uint32 sa = ui_lane[0][w] | ((uint32)ui_lane[1][w] << 8);
    uint32 sb = ui_lane[2][w] | ((uint32)ui_lane[3][w] << 8);

    feed[2 * w + 1] = (int16)pc_seq_avg_code(sa, ui_count, r, s->gain_a, s->off_a);
    feed[2 * w]     = (int16)pc_seq_avg_code(sb, ui_count, r, s->gain_b, s->off_b);
  }

  for(w=words*2;w<n;w++)
    feed[w] = feed[(words * 2) ? words * 2 - 1 : 0];

  return 1;
}

void pc_seq_ui_read_done(void)
{
  ui_feed = 0;
}

void pc_seq_trace_replaced(void)
{
  ui_rec_hw = 0;
}

int pc_seq_sw_average(void)
{
  return !ui_rec_hw;
}

void pc_hal_ui_average(uint8_t *state, uint8_t *log2n)
{
  *state = scopesettings.average_mode ? ui_state : 0;
  *log2n = (*state == 2) ? ui_log2n : 0;
}
