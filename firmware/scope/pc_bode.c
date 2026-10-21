//----------------------------------------------------------------------------------------------------------------------------------
// pc_bode.c - the scope's hardware behind the Bode sweep engine (bode_sweep.h bode_hw) and the protocol's BODE_CTRL /
// READ_BODE (fw 0.2.66, protocol 1.20 provisional, docs/protocol.md section 14 row 60).
//
// Every capture goes through the paths the host's own captures use (pc_hal_scope.c): the raw divider write
// (pc_hal_timebase_write), the arm with the decimation filter configuration of this capture (pc_hal_arm: CIC3 + FIR at
// N = 16 x 2^k, legacy 200 MSa/s at divider 0), FPGA auto mode
// with pretrigger 0 (no trigger needed: the phase of H cancels the record's start), and the 1.00o5 readout
// (pc_hal_trace_build), which leaves the 16-bit words in pc_hal_trace16 and the corrected 8-bit codes in the trace
// buffers. Scale per channel as the readout converts: the decimation scale with gain16 folded in, corr16, the range's
// volts per code and the probe factor.
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>

#include "bode_sweep.h"
#include "bode_view.h"
#include "pc_hal.h"
#include "pc_protocol.h"
#include "pc_decim.h"
#include "pc_cmds.h"
#include "pc_seq.h"
#include "adc_cal.h"
#include "types.h"
#include "variables.h"
#include "fpga_control.h"
#include "fpga_bus.h"

//pc_hal_scope.c (fw 0.2.66)
uint32 pc_glue_caps(void);
uint32 pc_glue_ring_words(void);
int    pc_glue_host_capture(void);
void   pc_glue_ring_taken(void);

static uint16 bh_legacy[2][BODE_MAX_RECORD + 2] __attribute__((aligned(4)));
static uint32 bh_divider = 0xFFFFFFFF;    //the 0x0D value the engine wrote last (0xFFFFFFFF: write at the next arm)
static uint32 bh_period;
static uint8  bh_wide;
static uint32 bh_pre;

static PCHANNELSETTINGS bh_ch(int c)
{
  return c ? &scopesettings.channel2 : &scopesettings.channel1;
}

static uint32 bh_uv_q8(const pc_hw_channel *h, int r)
{
  uint64 mul = pc_hal_mul_factor_1x((uint8)r);
  int64  ic  = h->input_cal[(r < 7) ? r : 6];

  if(ic < 0)
    ic = 0;

  //uV per code x 256 at the BNC (GET_CHANNEL uv_q8, pc_cmds.c pc_channel_scaling)
  return (uint32)(((uint64)ic * mul * 10 + 4096) >> 13);
}

static uint32 bh_ms(void *ctx)
{
  (void)ctx;
  return pc_hal_ms();
}

//fw-d5 (FPGA 0x153D): the square of P clocks (n_h = ceil(P / 2), n_l = floor(P / 2)); off = the pin hi-Z, the square left
//running behind it (as the DDS was)
static void bh_gen(void *ctx, uint32 period, int on)
{
  uint64 p16, h16;

  (void)ctx;

  if(on)
    bh_period = period;

  if(!on)
  {
    pc_hal_gen_write(0, 0, 0);
    return;
  }

  gp_bode_square(bh_period, &p16, &h16);
  pc_hal_gen_write(p16, h16, 1);
}

//Enable, DC, range r, offset: only what differs is written (every coupling or range write clicks a relay)
static void bh_channel(void *ctx, int c, int r, int32 off)
{
  PCHANNELSETTINGS s = bh_ch(c);
  pc_hw_channel h;
  uint8 mask = 0;

  (void)ctx;
  pc_hal_channel_read(c, &h);

  if(!h.enable)
  {
    h.enable = 1;
    mask |= PC_CH_ENABLE;
  }

  if(h.coupling)
  {
    h.coupling = 0;
    mask |= PC_CH_COUPLING;
  }

  if((h.range != r) || (s->displayvoltperdiv != r))
  {
    h.range = (uint8)r;
    mask |= PC_CH_RANGE;
  }

  if(h.dcoffset != off)
  {
    h.dcoffset = (int16_t)off;
    mask |= PC_CH_OFFSET;
  }

  if(mask)
    pc_hal_channel_write(c, &h, mask);
}

static void bh_info(void *ctx, int c, int r, bode_chinfo *ci)
{
  pc_hw_channel h;

  (void)ctx;
  pc_hal_channel_read(c, &h);
  ci->cal_offset     = h.cal_offset[(r < 7) ? r : 6];
  ci->dc_shift_value = h.dc_shift_value ? h.dc_shift_value : 1000;
  ci->uv_q8          = bh_uv_q8(&h, r);
}

//fw 0.2.89: the trigger the host's captures use: CH1, rising, code 128 (the engine centres CH1's mean there). The engine
//arms FPGA auto; with the UI's trigger level off the signal the record started on the time-out, and such a record is
//joined from two captures at a random point (bench 2026-10-20 cells_1235: 5 of 8 at 21.6 and 31.6 kHz, leak -3 .. -36
//dB, |H| off by up to 0.4 dB; 0 of 16 with the CH1 trigger). The level follows CH1's range and offset (the UI derives
//it from the marker at every arm), so it is checked before each arm and the marker placed again when it moved; the
//view gives the user's source, edge and marker back on leaving (bode_view_close). fw 0.2.91: the level is the engine's
//(CH1's expected mean code, bode_sweep.c trig_code), not 128: on the pre-scan's 2.5 V/div with offset 0 the post's
//signal sat at codes about 128..143, so 128 was its bottom edge (bench 13:14)
static void bh_trigger(uint8 level)
{
  pc_hw_trigger t;

  fpga_set_trigger_level();
  pc_hal_trigger_read(&t);

  if((t.source != 0) || (t.edge != 0) || (t.level + 3 < level) || (t.level > level + 3))
  {
    t.source = 0;
    t.edge   = 0;
    t.level  = level;
    pc_hal_trigger_write(&t, PC_TRG_SOURCE | PC_TRG_EDGE | PC_TRG_LEVEL);
  }
}

static uint8 bh_triggered;

//0x153B latches the newest total words on a time-out and completes a triggered record, so the engine's FPGA auto arm
//(0x0E = 2 x total, fw 0.2.91) reads a contiguous record whether a trigger came or not
static uint32 bh_total;

static int bh_arm(void *ctx, const bode_plan *pp, uint32 total, uint8 level)
{
  uint32 div = bode_divider(pp);
  pc_decim_cfg dc;
  pc_seq_cfg   sq;

  (void)ctx;

  if((div != bh_divider) || (div != pc_hal_divider_now()))       //fw 0.2.87: or someone else wrote 0x0D since
  {
    pc_hw_timebase tb;

    memset(&tb, 0, sizeof(tb));
    tb.divider = div;
    pc_hal_timebase_write(&tb, 1);
    bh_divider = div;
  }

  //CIC3 + FIR at N = 16 x 2^k (the firmware drops to CIC2 beyond its exactness limit, GET_DECIM's why), legacy at
  //divider 0
  pc_decim_config((pp->n > 1) ? PC_DECIM_CIC3 : PC_DECIM_OFF, (uint8)((pp->n > 1) ? PC_DECIM_SET_FIR : 0), div, total,
                  pc_decim_has(), &dc);

  memset(&sq, 0, sizeof(sq));
  bh_wide = (uint8)(dc.k != 0);
  bh_pre  = 0;

  //The PC's last capture is no longer in the ring
  pc_glue_ring_taken();
  bh_trigger(level);
  bh_triggered = 0;

  //FPGA auto: on CH1's trigger, else on the time-out (fw 0.2.91: twice the record, so a trigger has the time of the
  //record's periods to arrive)
  bh_total = bh_wide ? total : total / 2;

  //ui-type: the sweep's captures take the edge type whatever Type the trigger menu holds ("Not used by the sweep"): a
  //qualified type would make every point wait for the time-out. The arm writes the type registers inside its window
  //from fpga_trig, so the copy is swapped for the call and put back (the next UI arm writes the menu's type again)
  {
    fpga_trig_ext keep = fpga_trig;
    int           r;

    fpga_trig.type = FPGA_TRIG_EDGE;
    r = pc_hal_arm(0, (uint16)bh_total, 2 * total, 0, &dc, &sq);
    fpga_trig = keep;
    return r;
  }
}

static int bh_poll(void *ctx)
{
  int done, trig;

  (void)ctx;

  if(pc_glue_host_capture())
    return -1;

  pc_hal_acq_poll(&done, &trig);

  if(trig)
    bh_triggered = 1;

  return done;
}

//The engine's deadline passed without a stop: 0x1A = 0 ends the capture (0x153B reads the newest total words)
static void bh_force(void *ctx)
{
  (void)ctx;
  pc_hal_force();
}

//fw 0.2.83: the capture self-check of this capture (FPGA 0x153A+, cap_hw.c). 0x36 is read after a legacy 200 MSa/s
//capture only, as after the host's (pc_cmds.c pc_enc_rearm: the counters compare a core's successive words, the
//full-rate case fw-cap validated); a decimated capture has the repeat watch only. Never a flag of an earlier capture.
static uint8 bh_check(void)
{
  pc_hw_cap cs;
  int now = 0;

  if(!bh_wide && (bh_divider == 0))
  {
    pc_hal_cap_check();
    now = 1;
  }

  pc_hal_cap_state(&cs);

  if((cs.flags & PC_CAPF_REPEAT) || (now && (cs.flags & PC_CAPF_FLAGGED)))
    return BODE_CHK_FAULT;

  return BODE_CHK_CLEAN;
}

static int bh_read(void *ctx, const bode_plan *pp, uint32 total, bode_cap *cap)
{
  uint32 ring = pc_glue_ring_words(), addr, start, i;
  pc_decim_cfg dc;
  int written, c;

  (void)ctx;
  (void)pp;

  addr  = pc_hal_acq_finish();
  start = (addr + ring - bh_pre) % ring;

  //fw 0.2.98: triggered = 0x18 seen in a poll (as 0.2.91..0.2.96; the 0.2.97 host rules misread the engine's records,
  //bench 2026-10-20: 10..22 time-outs and 8..15 other retakes per sweep). A forced record on 0x153B is the newest total
  //words: never spliced
  pc_hal_trace_build(start, total, 0, 0x03);
  pc_hal_decim_current(&dc, &written);

  for(c = 0; c < 2; c++)
  {
    PCHANNELSETTINGS s = bh_ch(c);
    pc_hw_channel h;
    int32 dcsv;

    pc_hal_channel_read(c, &h);
    dcsv = h.dc_shift_value ? h.dc_shift_value : 1000;

    if(bh_wide)
    {
      uint32_t n;
      const uint16_t *w = pc_hal_trace16(c, &n);

      if(!w || (n < total))
        return -1;

      cap->x[c]         = w;
      cap->s[c].cpr_q24 = acal_scale(dc.scale_q24, s->gain16);
      cap->s[c].zero_q8 = s->corr16 * 16;
    }
    else
    {
      const uint8 *b = (const uint8 *)pc_hal_trace(c);

      for(i = 0; i < total; i++)
        bh_legacy[c][i] = (uint16)(b[i] << 8);

      cap->x[c]         = bh_legacy[c];
      cap->s[c].cpr_q24 = 65536;
      cap->s[c].zero_q8 = 0;
    }

    cap->uv_q8[c]      = bh_uv_q8(&h, h.range);
    cap->s[c].volt_mdb = bode_volt_mdb(cap->uv_q8[c], h.probe);
    cap->zero_c100[c]  = 12800 - (int32)(((int64)h.dcoffset * 10000) / dcsv);
  }

  cap->fir   = dc.fir;
  cap->check = bh_check();
  cap->triggered = bh_triggered;
  return 0;
}

static const bode_hw bh_hw = { 0, bh_ms, bh_gen, bh_channel, bh_info, bh_arm, bh_poll, bh_force, bh_read, 0 };
static bode_hw  bh_live;
static bode_eng bh_eng;
static uint8    bh_init;

bode_eng *bode_engine(void)
{
  if(!bh_init)
  {
    bh_live = bh_hw;
    bh_live.has_fir = (pc_glue_caps() & PC_CAP_FIR) != 0;
    bode_init(&bh_eng, &bh_live);
    bh_init = 1;
  }

  //A new sweep writes the divider again (the UI or the host may have written 0x0D since)
  if(bode_state(&bh_eng) != BODE_ST_RUNNING)
    bh_divider = 0xFFFFFFFF;

  return &bh_eng;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Protocol glue (pc_hal.h)

int pc_hal_bode_ctrl(const uint8_t *req, uint8_t *resp, uint16_t *detail)
{
  return bode_view_ctrl(req, resp, detail);
}

uint32_t pc_hal_bode_read(uint32_t first, uint32_t max_rows, uint8_t *resp)
{
  return bode_view_read(first, max_rows, resp);
}
