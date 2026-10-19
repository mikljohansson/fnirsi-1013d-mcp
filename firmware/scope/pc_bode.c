//----------------------------------------------------------------------------------------------------------------------------------
// pc_bode.c - the scope's hardware behind the Bode sweep engine (bode_sweep.h bode_hw) and the protocol's BODE_CTRL /
// READ_BODE (fw 0.2.66, protocol 1.20 provisional, docs/protocol.md section 14 row 60).
//
// Every capture goes through the paths the host's own captures use (pc_hal_scope.c): the raw divider write
// (pc_hal_timebase_write), the arm with the decimation filter configuration of this capture (pc_hal_arm: CIC3 + FIR at
// N = 16 x 2^k, legacy 200 MSa/s at divider 0; the 0x53 swap / falling-edge bits from the Filter setting), FPGA auto mode
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
#include "pc_seq.h"
#include "adc_cal.h"
#include "types.h"
#include "variables.h"

//pc_hal_scope.c (fw 0.2.66)
uint32 pc_glue_caps(void);
uint32 pc_glue_ring_words(void);
int    pc_glue_host_capture(void);
void   pc_glue_ring_taken(void);

static uint16 bh_legacy[2][BODE_MAX_RECORD + 2] __attribute__((aligned(4)));
static uint32 bh_divider = 0xFFFFFFFF;    //the 0x0D value the engine wrote last (0xFFFFFFFF: write at the next arm)
static uint32 bh_inc;
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

static void bh_gen(void *ctx, uint32 inc, int on)
{
  (void)ctx;

  if(on)
    bh_inc = inc;

  pc_hal_gen_write(bh_inc ? bh_inc : 0x00020000u, 0x80000000u, (uint8)(on != 0));
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

static int bh_arm(void *ctx, const bode_plan *pp, uint32 total)
{
  uint32 caps = pc_glue_caps(), div = bode_divider(pp);
  int    has = (caps & PC_CAP_FIR) ? PC_DECIM_HAS_FIR : (caps & PC_CAP_DECIM) ? PC_DECIM_HAS_CIC : PC_DECIM_HAS_NONE;
  pc_decim_cfg dc;
  pc_seq_cfg   sq;

  (void)ctx;

  if(div != bh_divider)
  {
    pc_hw_timebase tb;

    memset(&tb, 0, sizeof(tb));
    tb.divider = div;
    pc_hal_timebase_write(&tb, 1);
    bh_divider = div;
  }

  //CIC3 + FIR at N = 16 x 2^k (the firmware drops to CIC2 beyond its exactness limit, GET_DECIM's why), legacy at
  //divider 0; the swap and falling-edge bits as the Filter setting has them
  pc_decim_config((pp->n > 1) ? PC_DECIM_CIC3 : PC_DECIM_OFF,
                  (uint8)((pc_hal_decim_setting() & PC_DECIM_FLAGS) | ((pp->n > 1) ? PC_DECIM_SET_FIR : 0)), div, total,
                  has, &dc);

  memset(&sq, 0, sizeof(sq));
  bh_wide = (uint8)(dc.k != 0);
  bh_pre  = 0;

  //The PC's last capture is no longer in the ring
  pc_glue_ring_taken();

  //FPGA auto (time-out = the record): the record starts at once, triggered or not
  return pc_hal_arm(0, (uint16)(bh_wide ? total : total / 2), total, 1, 0, (caps & PC_CAP_DECIM) ? &dc : 0,
                    (caps & PC_CAP_SEQ) ? &sq : 0);
}

static int bh_poll(void *ctx)
{
  int done, trig;

  (void)ctx;

  if(pc_glue_host_capture())
    return -1;

  pc_hal_acq_poll(&done, &trig);
  return done;
}

static void bh_force(void *ctx)
{
  (void)ctx;
  pc_hal_force();
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
    cap->s[c].clipped  = (uint8)(h.overload != 0);
    cap->zero_c100[c]  = 12800 - (int32)(((int64)h.dcoffset * 10000) / dcsv);
  }

  cap->fir = dc.fir;
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
