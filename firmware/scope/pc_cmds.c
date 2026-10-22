//----------------------------------------------------------------------------------------------------------------------------------
// pc_cmds.c - PC protocol session: dispatch, handlers, shared control with the UI and the host single capture
// (docs/protocol.md, section 14 for protocol 1.3; docs/plans/collaborative-mode.md).
//
// All hardware access goes through pc_hal.h. All state lives in the caller's pc_session. Builds for the scope and for
// the host tests (firmware/scope/host_tests).
//
// Bench tests that must pass before a [U] assumption here is trusted are marked "BENCH: Tn" (docs/protocol.md section 12).
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>

#include "pc_cmds.h"
#include "pc_hal.h"
#include "pc_report.h"
#include "pc_ident.h"
#include "cap_stage.h"
#include "fpga_log.h"         //D4 logger: the states, formats and flags (no bus access from here)
#include "gen_pat.h"          //D5: the generator's P16 / H16 maths

//----------------------------------------------------------------------------------------------------------------------------------
// Dispatch table

#define F_BUSY      0x02      //accepted from a BUSY or timer poll (no FPGA, no UI access)
#define F_CONTROL   0x04      //accepted while a deferred request is pending
#define F_BASIC     0x10      //works on an unknown FPGA variant
#define F_MODAL     0x20      //fw 0.2.94: read-only, answered inside a modal loop (PC_CTX_MODAL) without closing it

#define PC_DEFERRED 0x10000   //handler result: response comes later
#define PC_HOLD     0x20000   //handler result: not yet, run the request again at the next MAIN poll

typedef int (*pc_handler)(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *resp_len);

typedef struct
{
  uint16_t   cmd;
  uint8_t    flags;
  uint32_t   cap;
  pc_handler handler;           //NULL: no implementation in this build (the capability bit is never set)
} pc_cmd_entry;

//Error detail and text for the response being built (P-4.2)
static int pc_error(pc_session *s, int status, uint16_t detail, const char *text)
{
  s->err_detail = detail;
  s->err_text   = text;
  return status;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Pure helpers

uint32_t pc_ring_mod(int64_t addr, uint32_t ring_words)
{
  int64_t r = addr % (int64_t)ring_words;

  if(r < 0)
    r += ring_words;

  return (uint32_t)r;
}

void pc_test_pattern(uint8_t *dst, uint32_t length, uint8_t pattern)
{
  uint32_t i;
  uint32_t x = length;

  for(i=0;i<length;i++)
  {
    switch(pattern)
    {
      case 0: dst[i] = (uint8_t)i; break;
      case 1: dst[i] = 0x00; break;
      case 2: dst[i] = 0xFF; break;
      default:
        //xorshift32 seeded with length, one state step per 4 bytes, emitted little-endian (protocol.md impl. notes)
        if((i & 3) == 0)
        {
          x ^= x << 13;
          x ^= x >> 17;
          x ^= x << 5;
        }
        dst[i] = (uint8_t)(x >> ((i & 3) * 8));
        break;
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// The bitstream this firmware is built for (P-5.1, PC_FW_FPGA_VERSION). One geometry, one capability word, derived from
// the constant: AL3A10 0x153D has the programmable geometry and the generator (0x1532), the EXT trigger (0x1533), the
// decimation filter with 16-bit samples (0x1534, cmds 0x53-0x55, read 0x29), the CIC compensation FIR (0x1535, 0x56),
// the sequencer (0x1536, cmds 0x57-0x5D, reads 0x2A-0x32), the encode self-check with the guaranteed core order (0x1538,
// read 0x33), the per-core capture stages (0x153A, 0x63 / 0x36), the trigger types with the hold-off (0x153C, cmds
// 0x5E-0x60, 0x66, 0x1B, reads 0x34 / 0x37), peak detect and the counter (0x153C, 0x56 bit 1, 0x61 / 0x62, read 0x35),
// the streaming logger and the generator as a fractional square (0x153D, 0x69 / 0x6A, read 0x38; 0x6C / 0x6D, read
// 0x39). No feature is tested against the version word: the word read at boot decides only the mismatch flag; a
// mismatched scope answers the F_BASIC commands and refuses the rest (pc_dispatch).

void pc_variant_init(uint16_t version, pc_variant *v)
{
  v->version    = version;
  v->mismatch   = (uint8_t)(version != PC_FW_FPGA_VERSION);
  v->variant    = PC_FPGA_VARIANT;
  v->ring_words = PC_FPGA_RING_WORDS;
  v->max_total  = PC_FPGA_MAX_TOTAL;
  v->caps       = PC_CAP_READ_LAG_UNVERIFIED | PC_CAP_INTERLEAVE_UNVERIFIED |          //BENCH: T3, T4
                  PC_CAP_PROG_TRIGGER_POS | PC_CAP_GENERATOR | PC_CAP_EXT_TRIGGER | PC_CAP_DECIM | PC_CAP_FIR |
                  PC_CAP_SEQ | PC_CAP_ENC_CHECK | PC_CAP_CAPTURE_STAGES |
                  PC_CAP_HOLDOFF | PC_CAP_TRIG_TYPES |                                                 //1.26: 0x153C
                  PC_CAP_PEAK_DETECT |                                                                 //C6 peak detect
                  PC_CAP_COUNTER |                                                                     //C7 counter
                  PC_CAP_LOGGER |                                                                      //D4 logger
                  PC_CAP_ENTER_FEL | PC_CAP_SHARED_MODE | PC_CAP_SD_SECTORS | PC_CAP_SCREENSHOT;

#if PC_DEBUG
  //FORCE relies on the Atlan4 stop condition; enabled in bench builds so test T7 can run
  v->caps |= PC_CAP_DEBUG_RAW | PC_CAP_FORCE_TRIGGER;                                  //BENCH: T7
#endif
}

//Core order within a word (P-7.3, rows 58 and 62): the firmware's fixed convention (PC_INTERLEAVE_ORDER 0, core B the
//earlier sample: READ_RAW / READ_TRACE interleave, acal_track's A - B model) is verified by the
//FPGA's own report: read 0x33 at boot with bits 3:2 = 01 (enc_heal_hw.c, before the session starts). A bitstream that
//reports another convention keeps caps bit 12 INTERLEAVE_UNVERIFIED set, is flagged (GET_HEALTH 90 bit 1) and has its
//full-rate captures judged by their data.
void pc_variant_core_order(pc_variant *v)
{
  pc_hw_enc e;

  pc_hal_enc_state(&e);

  if((e.mode == EH_MODE_FLAG) && eh_conv_ok(e.boot33) && !(e.flags & PC_ENCF_CONV_BAD))
    v->caps &= ~PC_CAP_INTERLEAVE_UNVERIFIED;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Shared control

static void pc_complete_deferred(pc_session *s, uint16_t status)
{
  if(s->def_active && !s->def_ready)
  {
    s->def_ready  = 1;
    s->def_status = status;
  }
}

static void pc_drop_deferred(pc_session *s)
{
  s->def_active = 0;
  s->def_ready  = 0;
}

static const uint8_t pc_mode_ui_to_proto[3] = { 0, 2, 1 };    //UI 0 auto, 1 single, 2 normal -> P 0 auto, 1 normal, 2 single
static const uint8_t pc_mode_proto_to_ui[3] = { 0, 2, 1 };

//settings_seq: +1 whenever the capture-relevant settings change, from the UI or the host (protocol section 14). A
//fingerprint compare at every poll, UI arm and UI DONE, so no UI code needs a hook per setting.
static void pc_settings_track(pc_session *s)
{
  uint32_t fp;

  //A raw divider (SET_TIMEBASE mode 1) holds until the UI selects a time base again
  if(s->tb_raw)
  {
    pc_hw_timebase tb;

    pc_hal_timebase_read(&tb);

    if((tb.timeperdiv != s->tb_raw_tpd) || (tb.samplerate != s->tb_raw_sr))
      s->tb_raw = 0;
  }

  fp = pc_hal_settings_fingerprint();

  if(s->tb_raw)
    fp ^= (s->tb_divider * 2654435761u) | 1u;

  if(fp != s->settings_fp)
  {
    s->settings_fp = fp;

    if(++s->settings_seq == PC_SEQ_INVALID)
      s->settings_seq = 0;
  }
}

//A pending host single capture is abandoned (the UI arms, RUN/STOP from either side)
static void pc_host_abort(pc_session *s)
{
  if((s->acq_state == PC_ACQ_ARMED) || (s->acq_state == PC_ACQ_TRIGGERED))
  {
    s->acq_state = PC_ACQ_ABORTED;
    pc_complete_deferred(s, PC_E_ABORTED);
  }
}

//Ring words for a sample count of a capture: two samples per word, one in the 16-bit layout (decimation filter on)
static uint32_t pc_cap_words(const pc_capture *c, uint32_t samples)
{
  return (c->decim & PC_DECIM_WIDE) ? samples : samples / 2;
}

//Decimation filter configuration an arm with the current settings and total samples writes (k = 0 without the cap).
//fw 0.2.87: at the divider the FPGA holds, not the session's raw divider or the UI table: a UI time/div snap writes no
//0x0D and clears tb_raw (pc_settings_track), and an N (shift) for the UI's rate on a raw 4095 saturated every word
//(fpga/al3/AGENTS.md "First-record saturation"). tb_raw only decides what GET_TIMEBASE reports.
static void pc_decim_next(pc_session *s, uint32_t total, pc_decim_cfg *c)
{
  uint8_t set = pc_hal_decim_setting();

  (void)s;
  pc_decim_config(set & 3, set & (PC_DECIM_SET_FIR | PC_DECIM_SET_PEAK), pc_hal_divider_now(), total,
                  pc_decim_has(), c);
}

//What pc_decim_config gets on the built-for bitstream: the CIC filter with the compensation FIR and the capture stages
//(the EXT shifts of 0x153A+ and 0x53 bits 3 / 4)
int pc_decim_has(void)
{
  return PC_DECIM_HAS_FIR | PC_DECIM_HAS_CAPSTAGES;
}

//EXT trigger: samples by which the stored data lag the edge (pc_decim_ext_shift_at: the filter's latency with K >= 1, the
//ADC pipeline's 4 words in the legacy layout since fw 0.2.72), at most the pretrigger (whole words in the legacy layout)
static uint16_t pc_ext_shift(uint8_t source, const pc_decim_cfg *d, uint32_t divider, uint32_t pretrigger)
{
  uint32_t v;

  if(source != 2)
    return 0;

  v = pc_decim_ext_shift_at(d, divider);

  if(v > pretrigger)
    v = d->k ? pretrigger : (pretrigger & ~1u);

  return (uint16_t)v;
}

//Settings a capture is armed with
static void pc_capture_latch(pc_session *s, pc_capture *c, uint8_t origin, uint32_t pretrigger, uint32_t total)
{
  pc_hw_timebase tb;
  pc_hw_trigger  t;

  pc_settings_track(s);
  pc_hal_timebase_read(&tb);
  pc_hal_trigger_read(&t);

  memset(c, 0, sizeof(*c));
  c->origin     = origin;
  c->seq        = s->settings_seq;
  c->timeperdiv = s->tb_raw ? 0xFF : tb.timeperdiv;
  c->divider    = pc_hal_divider_now();       //fw 0.2.87: the one the capture ran at
  c->source     = t.source;
  c->edge       = t.edge;
  c->level      = t.level;
  c->mode       = (t.ui_mode < 3) ? pc_mode_ui_to_proto[t.ui_mode] : 0;
  c->pretrigger = pretrigger;
  c->total      = total;
  pc_hal_channel_read(0, &c->ch[0]);
  pc_hal_channel_read(1, &c->ch[1]);

  //1.26: the trigger extension this capture was armed with
  {
    pc_hw_trig_ext x;

    pc_hal_trig_ext_read(&x);
    c->trig_type    = x.type;
    c->trig_flags   = x.flags;
    c->trig_n       = x.n;
    c->trig_h       = x.h;
    c->trig_holdoff = x.holdoff;
  }

  //1.9: the decimation filter as written for this capture (the arm wrote it just before)
  {
    pc_decim_cfg d;
    int written;

    memset(&d, 0, sizeof(d));
    pc_hal_decim_current(&d, &written);
    c->decim       = pc_decim_status(&d);
    c->decim_shift = pc_decim_shift_byte(&d);          //C6: 0xC0 | S for a peak record
    c->decim_n     = d.n;
    c->ext_shift   = pc_ext_shift(c->source, &d, c->divider, pretrigger);
  }
}

//Capture DONE: it becomes the latest capture; the FPGA ring holds it until the next arm
static void pc_capture_finish(pc_session *s, pc_capture *c, uint16_t trigger_addr, int triggered)
{
  pc_hw_channel ch;
  int i;

  pc_settings_track(s);

  if(s->settings_seq != c->seq)
  {
    c->seq    = PC_SEQ_INVALID;
    c->flags |= 0x10;
  }

  c->flags |= triggered ? 0x01 : 0x02;

  //1.4: an EXT edge was seen during this capture (0x28 bit 2 is cleared by every sampling reset)
  s->ext_flags = pc_hal_ext_status() & 0x07;

  if(s->ext_flags & 0x04)
    c->flags |= 0x20;

  //Overload flags of this readout
  for(i=0;i<2;i++)
  {
    pc_hal_channel_read(i, &ch);
    c->ch[i].overload = ch.overload;

    if(ch.overload)
      c->flags |= (uint8_t)(0x04 << i);
  }

  c->trigger_addr  = trigger_addr;
  c->capture_start = (uint16_t)pc_ring_mod((int64_t)trigger_addr - ((int64_t)pc_cap_words(c, c->pretrigger) - pc_cap_words(c, c->ext_shift)), s->var.ring_words);
  c->capture_id    = ++s->capture_id;

  s->trigger_addr  = c->trigger_addr;
  s->capture_start = c->capture_start;
  s->cap           = *c;
  s->ring_valid    = 1;
}

//UI hooks. They run in main-loop code outside pc_poll; in_poll keeps the timer-tick poll out meanwhile.
void pc_ui_pass_mark(pc_session *s, int n)
{
  if((s->main_polls == 1) && (n >= 0) && (n < 4) && !s->first_mark[n])
    s->first_mark[n] = (pc_hal_ms() - s->first_poll_at) + 1;
}

//fw 0.2.68: the screen's STOP during a host single (the button reads STOP while one runs): abort it
void pc_ui_host_stop(pc_session *s)
{
  pc_host_abort(s);
}

int pc_ui_arm_allowed(pc_session *s)
{
  return !s->ui_hold;
}

void pc_ui_capture_start(pc_session *s)
{
  uint8_t ip = s->in_poll;
  pc_hw_trigger t;

  s->in_poll = 1;

  //The UI resets the sample system: a host single in flight is lost, the ring no longer holds the last capture
  pc_host_abort(s);
  s->ring_valid = 0;
  s->ui_started = 1;

  if(!s->first_arm_ms && s->main_polls)
    s->first_arm_ms = (pc_hal_ms() - s->first_poll_at) + 1;      //+ 1: 0 means "not yet"

  pc_hal_trigger_read(&t);
  pc_capture_latch(s, &s->cap_next, (t.ui_mode == 1) ? PC_ORIGIN_UI_SINGLE : PC_ORIGIN_UI_RUN, t.pretrigger, t.total);
  s->in_poll = ip;
}

void pc_ui_capture_addr(pc_session *s, uint16_t addr)
{
  int done, trig;

  //0x18 (triggered) read as for a host capture                                        BENCH: T6
  pc_hal_acq_poll(&done, &trig);
  s->ui_addr = addr;
  s->ui_trig = (uint8_t)trig;
}

void pc_ui_capture_done(pc_session *s)
{
  uint8_t ip = s->in_poll;

  //Count each conversion once: while the arm gate holds, 1.00o5 processes the same finished conversion again every
  //pass (conversion_done stays set until the next fpga_do_conversion)
  if(!s->ui_started)
    return;

  s->in_poll    = 1;
  s->ui_started = 0;
  pc_capture_finish(s, &s->cap_next, s->ui_addr, s->ui_trig);

  if(!s->first_done_ms && s->main_polls)
    s->first_done_ms = (pc_hal_ms() - s->first_poll_at) + 1;
  s->in_poll    = ip;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Response bodies

static uint8_t pc_run_state(pc_session *s)
{
  pc_hw_ui u;
  pc_hw_trigger t;

  if((s->acq_state == PC_ACQ_ARMED) || (s->acq_state == PC_ACQ_TRIGGERED))
    return PC_RUN_SINGLE;

  pc_hal_ui_read(&u);

  if(!u.run)
    return PC_RUN_STOP;

  pc_hal_trigger_read(&t);
  return (t.ui_mode == 1) ? PC_RUN_SINGLE : PC_RUN_RUN;
}

//1.17: what the built-for bitstream has (GET_STATUS 44, GET_DECIM 32): everything the word names
static uint16_t pc_features(const pc_session *s)
{
  (void)s;
  return PC_FEAT_W_EXT_TRIGGER | PC_FEAT_W_DECIM | PC_FEAT_W_FIR | PC_FEAT_W_SEGMENTS | PC_FEAT_W_HW_AVERAGE |
         PC_FEAT_W_ENC_CHECK | PC_FEAT_W_CAPTURE | PC_FEAT_W_BODE | PC_FEAT_W_TRIG_TYPES |
         PC_FEAT_W_PEAK |                                                                 //C6 peak detect
         PC_FEAT_W_COUNTER |                                                              //C7 counter
         PC_FEAT_W_LOGGER;                                                                 //D4 logger
}

//1.17: features word, the UI's Average state and log2 of its records per hardware run (no FPGA access)
static void pc_features_body(pc_session *s, uint8_t *p)
{
  pc_put_u16(p, pc_features(s));
  pc_hal_ui_average(p + 2, p + 3);
}

static uint32_t pc_status_body(pc_session *s, uint8_t *p)
{
  uint32_t now = pc_hal_ms();
  uint8_t  level, charging, raw;
  uint8_t  flags = 0;
  pc_hw_channel c1, c2;

  pc_settings_track(s);
  pc_hal_battery(&level, &charging, &raw);
  pc_hal_channel_read(0, &c1);
  pc_hal_channel_read(1, &c2);

  //Bits 0-2 describe the host single capture
  if(s->fpga_triggered)   flags |= 0x01;
  if(s->fpga_done)        flags |= 0x02;
  if(s->auto_timeout_hit) flags |= 0x04;
  if(pc_hal_dtr())        flags |= 0x08;
  if(charging)            flags |= 0x10;
  if(s->def_active)       flags |= 0x20;
  if(c1.overload)         flags |= 0x40;
  if(c2.overload)         flags |= 0x80;

  memset(p, 0, PC_STATUS_LEN);
  p[0] = PC_SHARED;
  p[1] = s->acq_state;
  p[2] = flags;
  p[3] = (s->main_polls < 2) ? PC_UI_STARTING : s->ui_busy;
  pc_put_u32(p + 4,  s->capture_id);
  pc_put_u16(p + 8,  s->trigger_addr);
  pc_put_u16(p + 10, s->capture_start);
  pc_put_u32(p + 12, s->armed_once ? (now - s->arm_ms) : 0);
  pc_put_u32(p + 16, now);
  pc_put_u16(p + 20, 0xFFFF);                   //no watchdog in 1.3
  p[22] = level;
  p[23] = 0;                                    //no exit reasons in 1.3
  //Protocol 1.1 appends the main-loop pass time (interval between main-loop polls), last and maximum since the
  //previous GET_STATUS, in ms
  pc_put_u16(p + 28, s->pass_ms);
  pc_put_u16(p + 30, s->pass_max_ms);
  //Protocol 1.3: settings tracking and the shared acquisition state
  pc_put_u32(p + 32, s->settings_seq);
  pc_put_u32(p + 36, s->capture_id ? s->cap.seq : PC_SEQ_INVALID);
  p[40] = pc_run_state(s);
  p[41] = s->capture_id ? s->cap.origin : 0;
  p[42] = s->ext_flags;                         //1.4: EXT pin (bit0 level, bit1 generator on, bit2 edge since arm)
  p[43] = pc_hal_display_mode();                //1.7: 0 scope, 1 spectrum view; 2 / 3 waterfall / density; 1.13: 4 phosphor;
                                                //1.20: 5 Bode plot
  pc_features_body(s, p + 44);                  //1.17: features, UI Average (0 off, 1 software, 2 hardware), log2 N
  //1.26: the trigger extension status read at the last MAIN poll of a host capture in flight: 0x37 (bit 4 hold-off
  //running, bit 3 type bit 2), 0x34 byte 0 ({ 0, 0, counting, pulse began, EXT, longer, type[1:0] }), the edges still
  //to count (Nth edge; the bus gives the high byte first)
  p[48] = s->trig_st[0];
  p[49] = s->trig_st[1];
  pc_put_u16(p + 50, (uint16_t)(((uint16_t)s->trig_st[2] << 8) | s->trig_st[3]));
  return PC_STATUS_LEN;
}

//Correction for the channel's current settings (adc_cal.h): calibration data v3 when loaded and bound, with the v4
//tracking folded in exactly as the readout does it (acal_track_apply; protocol row 54)
static void pc_channel_corr(const pc_hw_channel *c, acal_corr *k)
{
  acal_correction(c->cal2_loaded ? &c->cal2 : 0, c->cal_offset, c->range, c->dcoffset, c->dc_shift_value, c->adc_a_comp,
                  c->adc_b_comp, k);
  acal_track_apply(&c->trk, k);
}

//uV per code x 256 at the BNC with a 1x probe, and the processed code of 0 V x 256 (P-8.5, protocol 1.10+: the v2 / v3
//correction puts it at exactly 128 - dcoffset x 100 / dc_shift_value) [U]   BENCH: T8
static void pc_channel_scaling(const pc_hw_channel *c, uint32_t *uv_q8, int32_t *zero_q8)
{
  acal_corr k;
  uint8_t  r   = (c->range < 7) ? c->range : 6;
  uint64_t mul = pc_hal_mul_factor_1x(r);
  int64_t  ic  = c->input_cal[r];

  if(ic < 0)
    ic = 0;

  //256 * ic / 2^21 * mul * 10 = ic * mul * 10 / 2^13, rounded
  *uv_q8 = (uint32_t)(((uint64_t)ic * mul * 10 + 4096) >> 13);

  pc_channel_corr(c, &k);
  *zero_q8 = k.zero_q8;
}

static uint32_t pc_channel_body(int ch, uint8_t *p)
{
  pc_hw_channel c;
  uint32_t uv;
  int32_t  zero;
  uint8_t  r;

  pc_hal_channel_read(ch, &c);
  pc_channel_scaling(&c, &uv, &zero);
  r = (c.range < 7) ? c.range : 6;

  memset(p, 0, 24);
  p[0] = (uint8_t)ch;
  p[1] = c.enable;
  p[2] = c.range;
  p[3] = (c.range > 5) ? 5 : c.range;
  p[4] = c.coupling;
  p[5] = c.invert;
  p[6] = c.probe;
  pc_put_u16(p + 8,  (uint16_t)c.dcoffset);
  pc_put_u16(p + 10, (uint16_t)(c.cal_offset[r] + c.dcoffset));
  pc_put_u16(p + 12, c.cal_offset[r]);
  pc_put_u32(p + 16, uv);
  pc_put_u32(p + 20, (uint32_t)zero);
  return 24;
}

static uint32_t pc_timebase_body(pc_session *s, uint8_t *p)
{
  pc_hw_timebase t;
  uint64_t ps;

  pc_hal_timebase_read(&t);

  if(s->tb_raw)
  {
    t.timeperdiv = 0xFF;
    t.samplerate = 0xFF;
    t.divider    = s->tb_divider;
  }

  //1.9: with the decimation filter on the output period is N x 5 ns, N = 2 * (floor(D / 2) + 1) (equal for odd D)
  {
    pc_hw_trigger trg;
    pc_decim_cfg  d;

    pc_hal_trigger_read(&trg);
    pc_decim_next(s, trg.total, &d);
    ps = 5000ull * (d.k ? (uint64_t)d.n : ((uint64_t)t.divider + 1));
  }

  memset(p, 0, 16);
  p[0] = t.timeperdiv;
  p[1] = t.samplerate;
  pc_put_u32(p + 4, t.divider);
  pc_put_u32(p + 8, (uint32_t)(1000000000000ull / ps));
  pc_put_u32(p + 12, (ps > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)ps);
  return 16;
}

//Firmware auto mode (fw 0.2.41, pc_hal_scope.c "Firmware auto mode"): the FPGA's auto time-out counts samples and
//expires in microseconds at fast rates, and 0x18 then keeps watching, so a timed-out record could read "triggered".
//Auto captures on Atlan bitstreams arm in normal mode and the firmware gives up after this many ms.
uint32_t pc_auto_deadline_ms(uint32_t samples, uint32_t divider)
{
  uint64_t rec = ((uint64_t)samples * ((uint64_t)divider + 1) + 199999) / 200000;   //record length in ms

  return (2 * rec > PC_AUTO_MIN_MS) ? (uint32_t)(2 * rec) : PC_AUTO_MIN_MS;
}

//The host's auto-mode deadline: max(100 ms, two records) at divider, or an explicit auto_timeout (samples) converted to
//time when it is longer
static uint32_t pc_host_auto_ms(pc_session *s, uint32_t total, uint32_t divider)
{
  uint32_t ms = pc_auto_deadline_ms(total, divider);

  if(s->auto_timeout_cfg)
  {
    uint32_t cfg_ms = pc_auto_deadline_ms(s->auto_timeout_cfg, divider) / 2;

    if(cfg_ms > ms)
      ms = cfg_ms;
  }

  return ms;
}

static uint32_t pc_auto_timeout(pc_session *s, uint32_t total)
{
  if(s->auto_timeout_cfg)
    return s->auto_timeout_cfg;

  return pc_hal_auto_timeout_default(total);
}

static uint32_t pc_trigger_body(pc_session *s, uint8_t *p)
{
  pc_hw_trigger  t;
  pc_hw_trig_ext x;

  pc_hal_trigger_read(&t);
  pc_hal_trig_ext_read(&x);

  memset(p, 0, PC_TRIGGER_LEN);
  p[0] = t.source;
  p[1] = t.edge;
  p[2] = t.level;
  p[3] = (t.ui_mode < 3) ? pc_mode_ui_to_proto[t.ui_mode] : 0;
  pc_put_u32(p + 4,  t.pretrigger);
  pc_put_u32(p + 8,  t.total);
  pc_put_u32(p + 12, x.holdoff);                //1.26: the 0x1B value, 5 ns units (always 0 up to 1.25)
  pc_put_u32(p + 16, pc_auto_timeout(s, t.total));

  //Ring words: one sample per word in the 16-bit layout (1.9)
  {
    pc_decim_cfg d;

    pc_decim_next(s, t.total, &d);
    pc_put_u16(p + 20, d.k ? t.pretrigger : t.pretrigger / 2);
    pc_put_u16(p + 22, d.k ? t.total : t.total / 2);
  }

  //1.26: bytes 28..51 are the SET_TRIGGER2 request that reproduces this trigger (type, polarity = edge, flags, source,
  //N, H, level, level2 0, hold-off, 0)
  p[28] = x.type;
  p[29] = t.edge;
  p[30] = x.flags;
  p[31] = t.source;
  pc_put_u32(p + 32, x.n);
  pc_put_u32(p + 36, x.h);
  pc_put_u16(p + 40, t.level);
  pc_put_u16(p + 42, 0);
  pc_put_u32(p + 44, x.holdoff);
  pc_put_u32(p + 48, 0);
  return PC_TRIGGER_LEN;
}

//1.27 (fw-d5, FPGA 0x153D): the generator is a fractional two-step square. What is in effect: SET_GEN's state until the
//UI's generator settings change, else the UI's (whole Hz / %, through the same P16 / H16 formulas as fpga_control.c). The
//0x39 status is read when the request runs in the main loop (refresh), else the last one read is reported (GET_GEN
//inside a menu).
static void pc_gen_now(pc_session *s, uint8_t *en, uint64_t *p16, uint64_t *h16)
{
  uint32_t hz;
  uint8_t  pct, level;

  pc_hal_gen_read_ui(en, &hz, &pct);

  if(s->gen_valid && ((*en != s->gen_ui_en) || (hz != s->gen_ui_hz) || (pct != s->gen_ui_pct)))
    s->gen_valid = 0;

  if(s->gen_valid)
  {
    *en  = s->gen_enable;
    *p16 = s->gen_p16;
    *h16 = s->gen_h16;
  }
  else if(gp_ui_square(hz, pct, p16, h16, &level))
    *p16 = *h16 = 0;                                       //a static level (frequency 0, duty 0 or 100 %)
}

static uint32_t pc_gen_body(pc_session *s, uint8_t *p, int refresh)
{
  uint8_t  en;
  uint64_t p16, h16;

  pc_gen_now(s, &en, &p16, &h16);

  if(refresh)
    pc_hal_gen_status(s->gen_st);

  //2..11 and 23 reserved (0): the bursts and the source of the 1.27 drafts' patterns
  memset(p, 0, PC_GEN_LEN);
  p[0] = en;
  p[1] = s->gen_st[0];
  p[22] = s->gen_st[1];

  if(p16)
  {
    pc_put_u64(p + 12, gp_mhz_from_p16(p16));
    pc_put_u16(p + 20, gp_bp_from_h16(p16, h16));
    pc_put_u64(p + 24, p16);
    pc_put_u64(p + 32, h16);
  }

  return PC_GEN_LEN;
}

//----------------------------------------------------------------------------------------------------------------------------------
// System handlers

static int h_ping(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)s;
  memcpy(resp, req, len);
  *rl = len;
  return PC_OK;
}

static int h_get_info(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  static const char fw[] = PC_FW_VERSION;
  static const char sn[] = PC_SERIAL;
  uint32_t i;

  (void)req; (void)len;

  memset(resp, 0, PC_INFO_LEN);
  resp[0] = PC_PROTO_MAJOR;
  resp[1] = PC_PROTO_MINOR;
  pc_put_u16(resp + 2,  PC_MAX_REQUEST_FRAME);
  pc_put_u32(resp + 4,  PC_MAX_RESPONSE_PAYLOAD);
  pc_put_u16(resp + 8,  s->var.version);
  resp[10] = s->var.variant;
  resp[11] = 8;
  pc_put_u16(resp + 12, s->var.ring_words);
  resp[14] = 2;
  resp[15] = 2;
  pc_put_u32(resp + 16, 200000000u);
  pc_put_u32(resp + 20, s->var.caps);
  pc_put_u32(resp + 24, GP_CLK_HZ);                                                  //1.27: the generator clock (was the DDS's 50 MHz)
  pc_put_u16(resp + 28, s->var.max_total);
  pc_put_u16(resp + 30, 80);
  resp[32] = 0;                                                                      //read_lag, BENCH: T3
  resp[33] = 7;
  resp[34] = 6;
  resp[35] = PC_INTERLEAVE_ORDER;

  for(i=0;(i<sizeof(fw) - 1) && (i<16);i++)
    resp[36 + i] = (uint8_t)fw[i];

  pc_put_u32(resp + 52, pc_fw_build);

  for(i=0;(i<sizeof(sn) - 1) && (i<16);i++)
    resp[56 + i] = (uint8_t)sn[i];

  pc_put_u16(resp + 72, PC_SCREEN_WIDTH);
  pc_put_u16(resp + 74, PC_SCREEN_HEIGHT);

  //1.25 (fw-one): the bitstream this firmware is built for and whether the scope runs it
  pc_put_u16(resp + 76, PC_FW_FPGA_VERSION);
  resp[78] = s->var.mismatch ? PC_FPGA_F_MISMATCH : 0;
  *rl = PC_INFO_LEN;
  return PC_OK;
}

static int h_get_status(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)req; (void)len;
  *rl = pc_status_body(s, resp);
  s->pass_max_ms = s->pass_ms;                  //max is per GET_STATUS interval
  return PC_OK;
}

//SET_REMOTE (1.3): compatibility no-op. There is no ownership any more; every flag and the watchdog are ignored.
static int h_set_remote(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)req; (void)len;
  *rl = pc_status_body(s, resp);
  return PC_OK;
}

static int h_get_battery(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)s; (void)req; (void)len;
  memset(resp, 0, 4);
  pc_hal_battery(&resp[0], &resp[1], &resp[2]);
  *rl = 4;
  return PC_OK;
}

//GET_LINK_STATS (protocol 1.2): u16 count, u16 reserved, then count u32 counters since boot:
//transport PC_TS_* (11), parser stats (8: frames, hdr_crc_errors, payload_crc_errors, oversize, version_errors,
//responses_dropped, timeouts, junk_bytes), session (8: responses, duplicates, tx_aborts, disp_main, disp_busy,
//disp_isr, held, isr_skipped), startup (fw 0.2.0: ms from session init to the first MAIN poll, duration of the first
//main-loop pass; fw 0.2.2: first MAIN poll -> first UI conversion started, -> first UI capture processed; fw 0.2.4:
//-> first pass after battery check, acquisition, touch handler, RTC. The last six are ms + 1, 0 = not reached),
//USB events (fw 0.2.5: bus resets, suspends, resumes, disconnects, EP0 setups, malformed EP0 setups), RX back-pressure
//(fw 0.2.7: stalls from the ISR, releases, re-stalls inside a release, releases with no packet waiting, largest
//RXCOUNT, RXCOUNT > 512), fw 0.2.12: USB interrupts served by the timer poll (lost IRQs), fw 0.2.17: boot splash ms + 1, fw 0.2.19: restart record (8), fw 0.2.20: USB IRQ races, fw 0.2.48: fault status + address, fw 0.2.49: USB inferred resumes, soft reconnects, fw 0.2.61: full-rate checks bad, recovered,
//fw 0.2.72: FPGA rate writes, filter writes, sampling resets, full-rate captures on 0x1538+ judged by their data;
//fw 0.2.78: requests that asked the modal UI to yield, E_BUSY answers instead (hold reason or time-out)
typedef char pc_link_counters_check[(PC_TSTAT_COUNT + 26 + PC_USBEV_COUNT + PC_RXDBG_COUNT + PC_RESTART_COUNT + 1 +
                                     PC_FAULT_COUNT + PC_USBW_COUNT + PC_ENCH_COUNT + PC_FPGAW_COUNT + 1 + 2 ==
                                     PC_LINK_COUNTERS) ? 1 : -1];

uint32_t pc_link_counters(pc_session *s, uint32_t *v)
{
  const pc_parser_stats *ps = &s->parser.stats;
  uint32_t n = PC_TSTAT_COUNT;

  pc_hal_link_stats(v);
  v[n++] = ps->frames;     v[n++] = ps->hdr_crc_errors; v[n++] = ps->payload_crc_errors; v[n++] = ps->oversize;
  v[n++] = ps->version_errors; v[n++] = ps->responses_dropped; v[n++] = ps->timeouts; v[n++] = ps->junk_bytes;
  v[n++] = s->responses;   v[n++] = s->duplicates;     v[n++] = s->tx_aborts;  v[n++] = s->disp_main;
  v[n++] = s->disp_busy;   v[n++] = s->disp_isr;       v[n++] = s->held;       v[n++] = s->isr_skipped;
  v[n++] = s->first_poll_ms; v[n++] = s->first_pass_ms; v[n++] = s->first_arm_ms; v[n++] = s->first_done_ms;
  v[n++] = s->first_mark[0]; v[n++] = s->first_mark[1]; v[n++] = s->first_mark[2]; v[n++] = s->first_mark[3];
  pc_hal_usb_events(v + n);
  n += PC_USBEV_COUNT;
  pc_hal_rx_debug(v + n);
  n += PC_RXDBG_COUNT;
  v[n++] = 0;              //47 retired (usb_irq_polled, fw 0.2.12-0.2.20 tick fallback)
  v[n++] = pc_hal_boot_splash_ms();
  pc_hal_restart_info(v + n);
  n += PC_RESTART_COUNT;
  v[n++] = 0;              //57 retired (usb_irq_races, fw 0.2.20)
  pc_hal_fault_info(v + n);
  n += PC_FAULT_COUNT;     //58-59 (fw 0.2.48): fault status, fault address of the restart's abort
  pc_hal_usb_watch(v + n);
  n += PC_USBW_COUNT;      //60-61 (fw 0.2.49): USB resumes inferred, soft reconnects
  {
    pc_hw_enc e;

    pc_hal_enc_state(&e);
    v[n++] = e.bad;        //62-63 (fw 0.2.61; every full-rate capture since 0.2.62): captures bad, recovered (enc_heal.h)
    v[n++] = e.recovered;
    pc_hal_fpga_writes(v + n);
    n += PC_FPGAW_COUNT;   //64-66 (fw 0.2.72): 0x0D, 0x53 / 0x56 (0.2.87: + 0x54 / 0x55), 0x01 = 1 writes (fpga_bus.h)
    v[n++] = e.replaced;   //67: full-rate captures on 0x1538+ judged by their data after such a write (eh_capture_mode)
  }
  v[n++] = s->yields;      //68-69 (fw 0.2.78, 1.23): requests that asked the modal UI to yield, E_BUSY answers instead
  v[n++] = s->yield_refused;
  return n;
}

static int h_get_link_stats(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint32_t v[PC_LINK_COUNTERS];
  uint32_t n, i;

  (void)req; (void)len;

  n = pc_link_counters(s, v);
  pc_put_u16(resp, (uint16_t)n);
  pc_put_u16(resp + 2, 0);

  for(i=0;i<n;i++)
    pc_put_u32(resp + 4 + 4 * i, v[i]);

  *rl = 4 + 4 * n;
  return PC_OK;
}

//GET_HEALTH (protocol 1.16, section 14 row 55): USB state now, CP15 control word, unit life, settings save state and
//the stored restart record; 1.19 (row 58): the full-rate capture check. Memory reads only: any poll context.
void pc_health_body(uint8_t *p)
{
  pc_hw_usb  u;
  pc_hw_life l;
  pc_hw_enc  e;
  const pc_life_restart *r = &l.rst;

  pc_hal_usb_state(&u);
  pc_hal_life(&l);
  pc_hal_enc_state(&e);

  memset(p, 0, PC_HEALTH_LEN);
  p[0] = 3;                                //layout 3 since 1.25 (128 bytes); 2 in 1.24 (124)
  pc_put_u16(p + 2, PC_HEALTH_LEN);
  p[4] = u.state;
  p[5] = u.flags;
  p[6] = u.power;
  p[7] = u.faddr;
  pc_put_u32(p + 8,  u.suspended_ms);
  pc_put_u32(p + 12, u.since_config_ms);
  pc_put_u32(p + 16, pc_hal_cp15_control());
  pc_put_u32(p + 20, l.power_on);
  pc_put_u32(p + 24, l.minutes);
  pc_put_u32(p + 28, pc_hal_ms());
  p[32] = l.life_flags;
  p[33] = l.last_end;
  p[34] = l.settings_flags;
  p[35] = l.saves;
  pc_put_u32(p + 36, (uint32_t)l.last_save);
  pc_put_u32(p + 40, l.dirty_age_ms);

  if(r->valid)
  {
    p[44] = (uint8_t)(1 | ((l.power_on && (r->power_on == l.power_on)) ? 2 : 0));
    p[45] = r->type;
    pc_put_u16(p + 46, r->restarts);
    pc_put_u32(p + 48, r->power_on);
    pc_put_u32(p + 52, r->exc_lr);
    pc_put_u32(p + 56, r->spsr);
    pc_put_u32(p + 60, r->exc_sp);
    pc_put_u32(p + 64, r->reset_lr);
    pc_put_u32(p + 68, r->reset_sp);
    pc_put_u32(p + 72, r->reset_cpsr);
    pc_put_u32(p + 76, r->fsr);
    pc_put_u32(p + 80, r->far);
    pc_put_u32(p + 84, r->minutes);
  }

  //1.19: full-rate capture check (enc_heal.h)
  p[88] = e.mode;
  p[89] = e.last33;
  p[90] = e.flags;
  p[91] = e.boot33;
  pc_put_u32(p + 92,  e.bad);
  pc_put_u32(p + 96,  e.recovered);
  pc_put_u32(p + 100, e.checked);

  //1.24: FPGA 0x153A capture stages and the outlier counters after the last full-rate capture (cap_stage.h)
  {
    pc_hw_cap c;
    uint32_t  k;

    pc_hal_cap_state(&c);
    pc_put_u16(p + 104, c.value);
    p[106] = c.flags;
    p[107] = c.flagged;

    for(k=0;k<4;k++)
      pc_put_u16(p + 108 + 2 * k, c.cnt[k]);

    pc_put_u32(p + 116, c.reads);
    pc_put_u32(p + 120, c.flagged_n);
  }

  //1.25 (fw-one): the built-for bitstream and the mismatch flag, as GET_INFO 76..79
  pc_put_u16(p + 124, PC_FW_FPGA_VERSION);
  p[126] = (pc_hal_fpga_version() != PC_FW_FPGA_VERSION) ? PC_FPGA_F_MISMATCH : 0;
}

#if PC_DEBUG
#define PC_HEALTH_FLAGS 0x81    //bench builds: bit 7 = restart from RAM after the answer (tests the stored record)
#else
#define PC_HEALTH_FLAGS 0x01
#endif

static int h_get_health(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint8_t flags = len ? req[0] : 0;

  if(flags & ~PC_HEALTH_FLAGS)
    return pc_error(s, PC_E_BAD_ARG, 0, "flags");

  if(flags & 0x80)
    s->restart_pending = 1;

  pc_health_body(resp);

  if(flags & 0x01)
    pc_hal_life_clear_restart();

  *rl = PC_HEALTH_LEN;
  return PC_OK;
}

//GET_BAD_FRAME (protocol 1.2): u32 bad_count, u32 rx_bytes at completion, u16 len, u16 reserved, then len bytes
static int h_get_bad_frame(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)req; (void)len;

  pc_put_u32(resp, s->bad_count);
  pc_put_u32(resp + 4, s->bad_rx_bytes);
  pc_put_u16(resp + 8, (uint16_t)s->bad_len);
  pc_put_u16(resp + 10, 0);
  memcpy(resp + 12, s->bad, s->bad_len);
  *rl = 12 + s->bad_len;
  return PC_OK;
}

//GET_EVENT_LOG (protocol 1.5): u16 n, u16 0, u32 total events since boot, u32 now (ms), then n entries of 8 bytes
//oldest first: u32 ms, u8 type (PC_EVT_* in pc_transport.h, docs/protocol.md section 14), u8 a, u16 b. fw 0.2.15
//appends the reset snapshot: u16 m, u16 frozen (1 = a bus reset froze it since the previous read), u32 reset ms, then
//m entries (the events right before that reset). fw 0.2.18: request u8 flags, bit 0 = re-arm the snapshot after this
//read (a plain read keeps it frozen)
static int h_get_event_log(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint32_t total, n;

  int32_t  m;
  uint32_t reset_ms;
  uint8_t *snap, *bus;
  uint32_t k, bus_total;

  (void)len;

  //Flags: bit 0 re-arm the reset snapshot; bits 2:1 (1.22) the FPGA write trace (event type 22): 0 unchanged, 1 off,
  //2 on, 3 refused
  if((req[0] & ~0x07) || ((req[0] & 0x06) == 0x06))
    return pc_error(s, PC_E_BAD_ARG, 0, "flags");

  if(req[0] & 0x06)
    pc_hal_fpga_trace((req[0] & 0x04) ? 1 : 0);

  n = pc_hal_event_log(resp + 12, 128, &total);
  pc_put_u16(resp, (uint16_t)n);
  pc_put_u16(resp + 2, pc_hal_fpga_trace(-1));          //1.22: bit 0 the FPGA write trace is on
  pc_put_u32(resp + 4, total);
  pc_put_u32(resp + 8, pc_hal_ms());

  snap = resp + 12 + 8 * n;
  m = pc_hal_event_snapshot(snap + 8, &reset_ms, req[0] & 0x01);
  pc_put_u16(snap, (uint16_t)((m > 0) ? m : 0));
  pc_put_u16(snap + 2, (uint16_t)(m >= 0));
  pc_put_u32(snap + 4, (m >= 0) ? reset_ms : 0);

  //fw 0.2.24: bus-level ring tail (u16 k, u16 0, u32 bus events since boot, k entries oldest first)
  bus = snap + 8 + 8 * ((m > 0) ? m : 0);
  k = pc_hal_event_bus(bus + 8, &bus_total);
  pc_put_u16(bus, (uint16_t)k);
  pc_put_u16(bus + 2, 0);
  pc_put_u32(bus + 4, bus_total);
  *rl = (uint32_t)(bus + 8 + 8 * k - resp);
  return PC_OK;
}

static int h_test_pattern(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint32_t n = pc_get_u32(req);

  (void)s; (void)len;

  if(n > PC_MAX_RESPONSE_PAYLOAD)
    return pc_error(s, PC_E_BAD_ARG, 0, "length");

  if(req[4] > 3)
    return pc_error(s, PC_E_BAD_ARG, 4, "pattern");

  pc_test_pattern(resp, n, req[4]);                                                  //BENCH: T1, T2
  *rl = n;
  return PC_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Configuration handlers

static int h_get_channel(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)s; (void)len;

  if(req[0] > 1)
    return pc_error(s, PC_E_BAD_ARG, 0, "channel");

  *rl = pc_channel_body(req[0], resp);
  return PC_OK;
}

static int h_set_channel(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint8_t  ch   = req[0];
  uint8_t  mask = req[1];
  int32_t  off  = (int32_t)pc_get_u32(req + 8);
  pc_hw_channel cur, nw;
  int32_t  pwm;
  uint8_t  r;

  (void)len;

  if(ch > 1)
    return pc_error(s, PC_E_BAD_ARG, 0xFF, "channel");

  if(mask & ~0x3F)
    return pc_error(s, PC_E_BAD_ARG, 0xFE, "mask");

  pc_hal_channel_read(ch, &cur);
  nw = cur;

  //Validate everything first, then apply (no half-applied requests). detail = mask bit of the field.
  if(mask & PC_CH_ENABLE)
  {
    if(req[2] > 1) return pc_error(s, PC_E_BAD_ARG, 0, "enable");
    nw.enable = req[2];
  }

  if(mask & PC_CH_RANGE)
  {
    if(req[3] > 6) return pc_error(s, PC_E_BAD_ARG, 1, "range > 6");
    nw.range = req[3];
  }

  if(mask & PC_CH_COUPLING)
  {
    if(req[4] > 1) return pc_error(s, PC_E_BAD_ARG, 2, "coupling");
    nw.coupling = req[4];
  }

  if(mask & PC_CH_INVERT)
  {
    if(req[5] > 1) return pc_error(s, PC_E_BAD_ARG, 4, "invert");
    nw.invert = req[5];
  }

  if(mask & PC_CH_PROBE)
  {
    if(req[6] > 6) return pc_error(s, PC_E_BAD_ARG, 5, "probe_index");
    nw.probe = req[6];
  }

  r = (nw.range < 7) ? nw.range : 6;

  if(mask & PC_CH_OFFSET)
  {
    if(req[7] > 1) return pc_error(s, PC_E_BAD_ARG, 3, "offset_mode");

    if(req[7] == 0)
    {
      if((off < -32768) || (off > 32767)) return pc_error(s, PC_E_BAD_ARG, 3, "offset");
      nw.dcoffset = (int16_t)off;
    }
    else
    {
      if((off < 0) || (off > 2000)) return pc_error(s, PC_E_BAD_ARG, 3, "pwm 0..2000");
      nw.dcoffset = (int16_t)(off - (int32_t)nw.cal_offset[r]);
    }
  }

  //The PWM compare value must stay inside the 2000-count period (zaklad.v pwmcounter)
  pwm = (int32_t)nw.cal_offset[r] + nw.dcoffset;
  if((mask & (PC_CH_OFFSET | PC_CH_RANGE)) && ((pwm < 0) || (pwm > 2000)))
    return pc_error(s, PC_E_BAD_ARG, 3, "pwm out of range");

  //A range or coupling change switches relays: ARM waits for them to settle
  if(((mask & PC_CH_RANGE) && (nw.range != cur.range)) || ((mask & PC_CH_COUPLING) && (nw.coupling != cur.coupling)))
  {
    s->settle_until   = pc_hal_ms() + PC_SETTLE_MS;
    s->settle_pending = 1;
  }

  pc_hal_channel_write(ch, &nw, mask);

  *rl = pc_channel_body(ch, resp);
  return PC_OK;
}

static int h_get_timebase(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)req; (void)len;
  *rl = pc_timebase_body(s, resp);
  return PC_OK;
}

static int h_set_timebase(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint8_t  mode  = req[0];
  uint32_t value = pc_get_u32(req + 4);
  pc_hw_timebase t;

  (void)len;

  pc_hal_timebase_read(&t);

  if(mode == 0)
  {
    //Indices 0..10 are long/roll timebases: not in v1
    if(value <= 10)
      return pc_error(s, PC_E_UNSUPPORTED, 4, "roll timebase");

    if((value > 34) || pc_hal_timebase_lookup((uint8_t)value, &t.samplerate, &t.divider))
      return pc_error(s, PC_E_BAD_ARG, 4, "index 11..34");

    t.timeperdiv = (uint8_t)value;
    pc_hal_timebase_write(&t, 0);
    s->tb_raw = 0;
  }
  else if(mode == 1)
  {
    //The FPGA compares 20 bits of 0x0D (pc_decim.h PC_DIVIDER_MAX)
    if(value > PC_DIVIDER_MAX)
      return pc_error(s, PC_E_BAD_ARG, 4, "divider > 1048575");

    t.divider = value;
    pc_hal_timebase_write(&t, 1);
    s->tb_raw     = 1;
    s->tb_raw_tpd = t.timeperdiv;
    s->tb_raw_sr  = t.samplerate;
    s->tb_divider = value;
  }
  else
  {
    return pc_error(s, PC_E_BAD_ARG, 0, "mode");
  }

  *rl = pc_timebase_body(s, resp);
  return PC_OK;
}

static int h_get_trigger(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)req; (void)len;
  *rl = pc_trigger_body(s, resp);
  return PC_OK;
}

static int h_set_trigger(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint16_t mask = pc_get_u16(req);
  uint32_t pre  = pc_get_u32(req + 8);
  uint32_t tot  = pc_get_u32(req + 12);
  uint32_t hold = pc_get_u32(req + 16);
  uint32_t ato  = pc_get_u32(req + 20);
  pc_hw_trigger t;
  uint8_t  hwmask = 0;

  (void)len;

  if(mask & ~0x00FF)
    return pc_error(s, PC_E_BAD_ARG, 0xFE, "mask");

  pc_hal_trigger_read(&t);

  //detail = bit number of the offending field (P-8.3)
  if(mask & 0x01)
  {
    if(req[2] > 2)   return pc_error(s, PC_E_BAD_ARG, 0, "source");
    t.source = req[2];
    hwmask |= PC_TRG_SOURCE;
  }

  if(mask & 0x02)
  {
    if(req[3] > 1)   return pc_error(s, PC_E_BAD_ARG, 1, "edge");
    t.edge = req[3];
    hwmask |= PC_TRG_EDGE;
  }

  if(mask & 0x04)
  {
    t.level = req[4];
    hwmask |= PC_TRG_LEVEL;
  }

  if(mask & 0x08)
  {
    if(req[5] > 2)   return pc_error(s, PC_E_BAD_ARG, 3, "mode");
    t.ui_mode = pc_mode_proto_to_ui[req[5]];
    hwmask |= PC_TRG_MODE;
  }

  if(mask & 0x10)
  {
    if(pre > 0xFFFF) return pc_error(s, PC_E_BAD_ARG, 4, "pretrigger");
    t.pretrigger = (uint16_t)pre;
    hwmask |= PC_TRG_PRE;
  }

  if(mask & 0x20)
  {
    if(tot > 0xFFFF) return pc_error(s, PC_E_BAD_ARG, 5, "total");
    t.total = (uint16_t)tot;
    hwmask |= PC_TRG_TOTAL;
  }

  //Geometry checks on the resulting values
  if(mask & 0x30)
  {
    if(t.total & 1)                                    return pc_error(s, PC_E_BAD_ARG, 5, "total odd");
    if((t.total < 80) || (t.total > s->var.max_total)) return pc_error(s, PC_E_BAD_ARG, 5, "total out of range");
    if(t.pretrigger & 1)                               return pc_error(s, PC_E_BAD_ARG, 4, "pretrigger odd");
    if(t.pretrigger + 2 > t.total)                     return pc_error(s, PC_E_BAD_ARG, 4, "pretrigger > total - 2");

    //1.9: the 16-bit layout holds one sample per word
    if((pc_hal_decim_setting() & (3 | PC_DECIM_SET_PEAK)) && (t.total > PC_DECIM_MAX_TOTAL))   //C6: peak is 16-bit too
      return pc_error(s, PC_E_BAD_ARG, 5, "total > 8190 with the filter on");
  }

  pc_hal_trigger_write(&t, hwmask);

  //1.26: the hold-off (0x1B, 5 ns units; E_UNSUPPORTED up to 1.25) through the trigger extension, the type and its
  //parameters kept; like SET_TRIGGER2 it asks for the hold-off clear at the next arm
  if(mask & 0x40)
  {
    pc_hw_trig_ext x;

    pc_hal_trig_ext_read(&x);
    x.holdoff = hold;
    pc_hal_trig_ext_write(&x);
  }

  if(mask & 0x80)
    s->auto_timeout_cfg = ato;

  *rl = pc_trigger_body(s, resp);
  return PC_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// SET_TRIGGER2 (1.26, fw-c5, FPGA 0x153C; docs/protocol.md row 66, docs/plans/fpga-0x153c.md section 3): the trigger
// definition in one 24-byte request, every field written (no mask): 0 u8 type, 1 u8 polarity, 2 u8 flags, 3 u8 source,
// 4 u32 N, 8 u32 H (N2 on 0x153E), 12 u16 level (0..255: the 8-bit code, as SET_TRIGGER), 14 u16 level2 (0x153E, 0),
// 16 u32 hold-off (5 ns units), 20 u32 0. Nothing is converted: N and H are samples of the trigger stream. The edge
// trigger's registers take source, polarity (0x16) and level as SET_TRIGGER writes them; the extension registers are
// written at once and the hold-off clear is requested for the next arm (pc_hal_trig_ext_write). Response: the
// GET_TRIGGER body. SET_TRIGGER stays for the edge type's fields, the geometry and the mode. Runt / window / range /
// both cores (0x153E) are refused: one bitstream per firmware, no reserved behaviour.

static int h_set_trigger2(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint8_t  type = req[0], pol = req[1], flags = req[2], src = req[3];
  uint32_t n = pc_get_u32(req + 4), h = pc_get_u32(req + 8), hold = pc_get_u32(req + 16);
  uint16_t level = pc_get_u16(req + 12), level2 = pc_get_u16(req + 14);
  pc_hw_trigger  t;
  pc_hw_trig_ext x;

  (void)len;

  //detail = the byte offset of the field (section 14 row 4)
  if((type == PC_TRG_T_RUNT) || (type == PC_TRG_T_WINDOW))
    return pc_error(s, PC_E_BAD_ARG, 0, "runt / window: FPGA 0x153E");

  if(type > PC_TRG_T_TIMEOUT)
    return pc_error(s, PC_E_BAD_ARG, 0, "type 0..3");

  if(pol > 1)
    return pc_error(s, PC_E_BAD_ARG, 1, "polarity 0..1");

  if(flags & (PC_TRG_F_RANGE | PC_TRG_F_BOTH))
    return pc_error(s, PC_E_BAD_ARG, 2, "range / both cores: FPGA 0x153E");

  if(flags & ~PC_TRG_F_LONGER)
    return pc_error(s, PC_E_BAD_ARG, 2, "flags");

  if((flags & PC_TRG_F_LONGER) && (type != PC_TRG_T_PULSE))
    return pc_error(s, PC_E_BAD_ARG, 2, "longer: pulse width only");

  if(src > 2)
    return pc_error(s, PC_E_BAD_ARG, 3, "source 0..2");

  switch(type)
  {
    case PC_TRG_T_PULSE:
    case PC_TRG_T_TIMEOUT:
      if((n == 0) || (n > PC_TRG_N_MAX)) return pc_error(s, PC_E_BAD_ARG, 4, "N 1..16777215 samples");
      if(h)                              return pc_error(s, PC_E_BAD_ARG, 8, "H: Nth edge only");
      break;

    case PC_TRG_T_NTH:
      if((n == 0) || (n > PC_TRG_NTH_MAX)) return pc_error(s, PC_E_BAD_ARG, 4, "N 1..65535 edges");
      if(h > PC_TRG_N_MAX)                 return pc_error(s, PC_E_BAD_ARG, 8, "H 0..16777215 samples");
      break;

    default:
      if(n) return pc_error(s, PC_E_BAD_ARG, 4, "N: 0 for the edge type");
      if(h) return pc_error(s, PC_E_BAD_ARG, 8, "H: 0 for the edge type");
      break;
  }

  if(level > 255)
    return pc_error(s, PC_E_BAD_ARG, 12, "level 0..255");

  if(level2)
    return pc_error(s, PC_E_BAD_ARG, 14, "level2: FPGA 0x153E");

  //The edge trigger's registers as SET_TRIGGER writes them: 0x15, 0x16 (the polarity), 0x17 through the UI's marker
  pc_hal_trigger_read(&t);
  t.source = src;
  t.edge   = pol;
  t.level  = (uint8_t)level;
  pc_hal_trigger_write(&t, PC_TRG_SOURCE | PC_TRG_EDGE | PC_TRG_LEVEL);

  //The extension registers at once, 0x66 = 1 for the next arm
  x.type    = type;
  x.flags   = flags;
  x.n       = n;
  x.h       = h;
  x.holdoff = hold;
  pc_hal_trig_ext_write(&x);

  *rl = pc_trigger_body(s, resp);
  return PC_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
//C7 counter (fw-c7, FPGA 0x153C, protocol 1.26 row 66, cap COUNTER). GET_COUNTER 0x0213: request u8 what (0 read, 1
//configure then read), u8 gate, u8 source, u8 level (0 = auto: the band centred on the signal's midpoint), u8
//hysteresis (0 = auto: round(span / 6), at least 6; fpga_counter.h), 3 x 0. The answer
//is the result the main loop's pass holds (no FPGA access here, so it is answered inside menus too: F_MODAL); a
//configuration is noted and written by the next main-loop pass. Response 24 bytes: 0 flags (bit 0 NEW: the sequence
//number differs from the previous GET_COUNTER's, bit 1 VALID: E >= 1, the result current and its thresholds fitting it (not misfit), bit 2 EXT, bit 3 CH2: the
//result's source), 1 seq, 2 gate and 3 source as configured, 4 u32 E, 8 u32 T, 12 u32 W, 16 min, 17 max, 18 level and
//19 hysteresis in effect, 20 u32 freq_mHz = round(E x 2e11 / T) saturating, 0 when not VALID.

//E x 2e8 / T in mHz, round half up, saturating (E x 2e11 would overflow for the largest E, so two stages)
static uint32_t pc_counter_freq_mhz(uint32_t e, uint32_t t)
{
  uint64_t num = (uint64_t)e * 200000000ull, q = num / t, r = num % t, mhz;

  mhz = q * 1000ull + (r * 1000ull + t / 2) / t;
  return (mhz > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)mhz;
}

static int h_get_counter(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  pc_hw_counter c;
  uint8_t what = len ? req[0] : 0, flags = 0, valid;

  if(what > 1)
    return pc_error(s, PC_E_BAD_ARG, 0, "what 0..1");

  if(what == 1)
  {
    if(len < 5)
      return pc_error(s, PC_E_BAD_ARG, 0, "configuration needs 8 bytes");

    if(req[1] > 3)
      return pc_error(s, PC_E_BAD_ARG, 1, "gate 0..3");

    if(req[2] > 3)
      return pc_error(s, PC_E_BAD_ARG, 2, "source 0..3");

    if(req[3] && (req[4] > req[3]))
      return pc_error(s, PC_E_BAD_ARG, 4, "hysteresis > level");

    pc_hal_counter_config(req[1], req[2], req[3], req[4]);
  }

  pc_hal_counter_read(&c);
  valid = (uint8_t)(c.held && c.current && c.has_period && !c.misfit && (c.t != 0));

  if(c.held && (!s->cnt_seen || (s->cnt_seen_seq != c.seq)))
    flags |= 0x01;

  if(valid)                    flags |= 0x02;
  if(c.res_src == 2)           flags |= 0x04;
  if(c.res_src == 1)           flags |= 0x08;

  s->cnt_seen     = c.held;
  s->cnt_seen_seq = c.seq;

  memset(resp, 0, PC_COUNTER_LEN);
  resp[0] = flags;
  resp[1] = c.seq;
  resp[2] = c.gate;
  resp[3] = c.source;
  pc_put_u32(resp + 4,  c.e);
  pc_put_u32(resp + 8,  c.t);
  pc_put_u32(resp + 12, c.w);
  resp[16] = c.min;
  resp[17] = c.max;
  resp[18] = c.level;
  resp[19] = c.hyst;
  pc_put_u32(resp + 20, valid ? pc_counter_freq_mhz(c.e, c.t) : 0);
  *rl = PC_COUNTER_LEN;
  return PC_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Acquisition handlers

//Geometry the next host ARM writes: the trigger settings, the decimation filter for them, the EXT latency shift, and the
//pretrigger / total words (one sample per word in the 16-bit layout)
static void pc_arm_geometry(pc_session *s, pc_hw_trigger *t, pc_decim_cfg *dc, uint16_t *pre_w, uint16_t *tot_w,
                            uint16_t *ext_shift)
{
  int wide;

  pc_hal_trigger_read(t);

  //1.9: decimation filter for this capture; the 16-bit layout has one sample per ring word
  pc_decim_next(s, t->total, dc);
  wide = dc->k != 0;

  //EXT trigger: the stored data lag the edge, so fewer pretrigger words put the edge back at sample `pretrigger`
  *ext_shift = pc_ext_shift(t->source, dc, pc_hal_divider_now(), t->pretrigger);

  *pre_w = wide ? (uint16_t)(t->pretrigger - *ext_shift) : (uint16_t)((t->pretrigger - *ext_shift) / 2);
  *tot_w = wide ? t->total : t->total / 2;
}

//ARM (1.3): host single capture. The UI acquisition stops (the screen shows STOP), the host's own ARM sequence runs
//as in 1.2, and at DONE the capture is built into the trace buffers, which the UI then shows. 1.17: with a SET_SEQ
//setting it arms a sequencer run (segments or hardware averaging) instead; DONE is the end of the whole run.
//The arm h_arm set up (also the full-rate check's re-arm, pc_enc_rearm)
static int pc_arm_hw(pc_session *s, uint16_t pre_w, uint16_t tot_w)
{
  return pc_hal_arm(pre_w, tot_w, s->arm_auto, s->arm_normal, &s->arm_dc, &s->arm_seq);
}

static int h_arm(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  pc_hw_trigger t;
  pc_hw_ui u;
  pc_decim_cfg dc;
  uint16_t pre_w, tot_w, ext;
  int seq = (s->seq.mode != PC_SEQ_OFF);

  (void)req; (void)len;

  if((s->acq_state == PC_ACQ_ARMED) || (s->acq_state == PC_ACQ_TRIGGERED))
    return pc_error(s, PC_E_STATE, 0, "already armed");

  //fw-roll: in the long time bases too: the UI's roll stops like its conversions do (pc_hal_arm releases the stream)
  pc_hal_ui_read(&u);

  //1.17: the sequencer setting must fit the geometry this arm writes (SET_TRIGGER / SET_DECIM may have changed since)
  if(seq)
  {
    pc_arm_geometry(s, &t, &dc, &pre_w, &tot_w, &ext);

    if(pc_seq_fit_rec(&s->seq, pre_w, tot_w, dc.k != 0, dc.peak) != PC_SEQ_FIT_OK)
      return pc_error(s, PC_E_STATE, 3, "SET_SEQ setting does not fit the trigger geometry (GET_SEQ byte 16)");
  }

  //Stop the UI loop from arming its own conversions; its next RUN starts afresh
  if(u.run)
    pc_hal_ui_set_run(0);

  //1. relay settling
  if(s->settle_pending)
  {
    int32_t left = (int32_t)(s->settle_until - pc_hal_ms());

    if((left > 0) && (left <= PC_SETTLE_MS))
      pc_hal_delay_ms((uint32_t)left);

    s->settle_pending = 0;
  }

  pc_arm_geometry(s, &t, &dc, &pre_w, &tot_w, &ext);
  s->cap_wide      = dc.k != 0;
  s->cap_ext_shift = ext;

  //2.-5. conversion start with a bounded ready wait                                  BENCH: T6
  //A single capture in auto mode arms normal and the firmware's time-out forces it (fw 0.2.41; a forced stop on 0x153B
  //reads the newest total words, never spliced). A sequencer run in auto mode uses the FPGA's own time-out (0x0E):
  //untriggered segments are recorded as such, and averaging skips untriggered records; the firmware auto time-out would
  //end the whole run
  s->ring_valid = 0;
  s->seq_stop   = 0;
  s->arm_dc     = dc;
  s->arm_seq    = s->seq;
  s->arm_auto   = pc_auto_timeout(s, t.total);
  s->arm_normal = (uint8_t)((t.ui_mode != 0) || !seq);
  eh_cap_reset(&s->enc_cap);

  if(pc_arm_hw(s, pre_w, tot_w))
  {
    s->acq_state = PC_ACQ_ABORTED;
    return pc_error(s, PC_E_FPGA, 0, "0x05 not ready");
  }

  pc_capture_latch(s, &s->cap_next, PC_ORIGIN_HOST, t.pretrigger, t.total);
  s->ui_started = 0;

  if(seq)
  {
    pc_capture *c = &s->cap_next;

    c->seq_mode        = s->seq.mode;
    c->seq_flags       = s->seq.flags & PC_SEQ_F_CIRCULAR;
    c->seq_m           = s->seq.m;
    c->seq_fmt16       = (uint8_t)(dc.k != 0);
    c->seq_count       = s->seq.count;
    c->seq_n           = s->seq.n;
    c->seq_holdoff_ns  = s->seq.holdoff_ns;
    c->seq_pre_words   = pre_w;
    c->seq_total_words = tot_w;
  }

  s->acq_state        = PC_ACQ_ARMED;
  s->fpga_done        = 0;
  s->fpga_triggered   = 0;
  s->auto_timeout_hit = 0;
  s->armed_once       = 1;
  memset(s->trig_st, 0, sizeof(s->trig_st));                                        //1.26: read while in flight
  s->arm_ms           = pc_hal_ms();
  s->cap_pre_words    = pre_w;
  s->cap_total_words  = tot_w;
  s->cap_divider      = s->cap_next.divider;

  //Auto mode in firmware (armed normal above): an explicit host time-out (samples) counts too
  s->fw_auto     = (uint8_t)((t.ui_mode == 0) && !seq);
  s->host_forced = 0;
  s->auto_ms     = pc_host_auto_ms(s, t.total, s->cap_divider);

  *rl = pc_status_body(s, resp);
  return PC_OK;
}

static int h_force(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)req; (void)len;

  if(s->acq_state != PC_ACQ_ARMED)
    return pc_error(s, PC_E_STATE, 0, "not armed");

  //fw-left: a sequencer run is ended by STOP (pc_hal_seq_stop: no restarts, then 0x1A = 0 with the arm's 0x0E). The
  //force's 0x0E = 0 would stay for every later segment (each cap_rst restarts time_base_cnt), ending them after about
  //two words
  if(s->cap_next.seq_mode)
    return pc_error(s, PC_E_STATE, 0, "sequencer run: STOP ends it");

  pc_hal_force();                                                                    //BENCH: T7

  *rl = pc_status_body(s, resp);
  return PC_OK;
}

//STOP (1.3): aborts a host single and stops the UI acquisition (run_state STOP)
static int h_stop(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  pc_hw_ui u;

  (void)req; (void)len;

  //1.17: a sequencer run is ended, not abandoned: no further restarts (0x5D), a record waiting for a trigger ends now;
  //the run then completes as DONE with what it holds (a circular run needs this to end at all)
  if(((s->acq_state == PC_ACQ_ARMED) || (s->acq_state == PC_ACQ_TRIGGERED)) && s->cap_next.seq_mode)
  {
    if(!s->seq_stop)
      pc_hal_seq_stop();

    s->seq_stop = 1;
    *rl = pc_status_body(s, resp);
    return PC_OK;
  }

  s->acq_state = PC_ACQ_ABORTED;
  pc_complete_deferred(s, PC_E_ABORTED);

  pc_hal_ui_read(&u);

  if(u.run)
    pc_hal_ui_set_run(0);

  *rl = pc_status_body(s, resp);
  return PC_OK;
}

//SET_ACQ_STATE (1.3): u8 state 0 STOP / 1 RUN, as the RUN/STOP button. Both abort a pending host single.
static int h_set_acq_state(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)len;

  if(req[0] > 1)
    return pc_error(s, PC_E_BAD_ARG, 0, "state 0..1");

  pc_host_abort(s);
  pc_hal_ui_set_run(req[0]);

  *rl = pc_status_body(s, resp);
  return PC_OK;
}

//GET_CAPTURE_INFO (1.3): the settings the latest finished capture (any origin) was taken with
static int h_get_capture_info(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  const pc_capture *c = &s->cap;
  uint32_t i;

  (void)req; (void)len;

  if(s->capture_id == 0)
    return pc_error(s, PC_E_STATE, 0, "no capture");

  memset(resp, 0, PC_CAPTURE_INFO_LEN);
  pc_put_u32(resp,      c->capture_id);
  pc_put_u32(resp + 4,  c->seq);
  resp[8] = c->origin;
  resp[9] = c->flags;
  pc_put_u16(resp + 10, c->trigger_addr);
  pc_put_u16(resp + 12, c->capture_start);
  pc_put_u32(resp + 16, c->divider);
  resp[20] = c->timeperdiv;
  resp[21] = c->source;
  resp[22] = c->edge;
  resp[23] = c->level;
  resp[24] = c->mode;
  pc_put_u32(resp + 28, c->pretrigger);
  pc_put_u32(resp + 32, c->total);

  for(i=0;i<2;i++)
  {
    const pc_hw_channel *ch = &c->ch[i];
    uint8_t *p = resp + 36 + 16 * i;
    uint8_t  r = (ch->range < 7) ? ch->range : 6;
    uint32_t uv;
    int32_t  zero;

    pc_channel_scaling(ch, &uv, &zero);
    p[0] = ch->enable;
    p[1] = ch->range;
    p[2] = ch->coupling;
    p[3] = ch->invert;
    p[4] = ch->probe;
    pc_put_u16(p + 6, (uint16_t)(ch->cal_offset[r] + ch->dcoffset));
    pc_put_u32(p + 8, uv);
    pc_put_u32(p + 12, (uint32_t)zero);
  }

  //1.9: decimation filter of this capture (0x29 byte 0 layout, S, N)
  resp[68] = c->decim;
  resp[69] = c->decim_shift;
  pc_put_u16(resp + 70, c->ext_shift);    //EXT + K >= 1: pretrigger words written = pretrigger - this (rounded latency)
  pc_put_u32(resp + 72, c->decim_n);

  //1.17: sequencer run (0 = a single capture)
  resp[76] = c->seq_mode;
  resp[77] = c->seq_flags;
  resp[78] = c->seq_m;
  //1.26 (C6): bit 0 16-bit records (runs since 1.17, single captures too since 1.26), bit 1 a peak-detect record
  resp[79] = (uint8_t)((((c->decim & PC_DECIM_WIDE) || c->seq_fmt16) ? 0x01 : 0) |
                       ((c->decim_shift & PC_DECIM_PEAK_ACT) ? 0x02 : 0));
  pc_put_u32(resp + 80, c->seq_done);
  pc_put_u16(resp + 84, c->seq_untrig);
  pc_put_u16(resp + 86, c->seq_count);
  pc_put_u32(resp + 88, c->seq_n);
  pc_put_u32(resp + 92, c->seq_holdoff_ns);

  //1.26: the trigger extension this capture was armed with (the polarity is 22, the level 23)
  resp[96] = c->trig_type;
  resp[97] = c->trig_flags;
  pc_put_u16(resp + 98, 0);
  pc_put_u32(resp + 100, c->trig_n);
  pc_put_u32(resp + 104, c->trig_h);
  pc_put_u32(resp + 108, c->trig_holdoff);

  *rl = PC_CAPTURE_INFO_LEN;
  return PC_OK;
}

static int h_wait(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint32_t timeout = pc_get_u32(req);

  (void)len;

  if(timeout > 10000)
    return pc_error(s, PC_E_BAD_ARG, 0, "timeout > 10000");

  //Nothing to wait for: answer at once with the current state
  if((s->acq_state != PC_ACQ_ARMED) && (s->acq_state != PC_ACQ_TRIGGERED))
  {
    *rl = pc_status_body(s, resp);
    return PC_OK;
  }

  s->def_active   = 1;
  s->def_ready    = 0;
  s->def_deadline = pc_hal_ms() + timeout;
  return PC_DEFERRED;
}

//One core of READ_RAW: the ring (slot_len 0), or a segment slot of slot_len words at slot_base, wrapping at its end
static void pc_raw_core(pc_session *s, uint8_t core, uint32_t start, uint32_t count, uint8_t *dst, uint32_t stride,
                        uint32_t slot_base, uint32_t slot_len)
{
  uint32_t n;

  if(!slot_len)
  {
    pc_hal_read_core(core, start, count, dst, stride, s->var.ring_words);
    return;
  }

  n = slot_len - start;
  if(n > count)
    n = count;

  pc_hal_read_core(core, slot_base + start, n, dst, stride, s->var.ring_words);

  if(count > n)
    pc_hal_read_core(core, slot_base, count - n, dst + n * stride, stride, s->var.ring_words);
}

//Segment table entry (accumulator word idx) of the latest segment run
static uint64_t pc_seq_entry_read(uint32_t idx)
{
  uint8_t b[8];
  uint32_t i;

  for(i=0;i<8;i++)
    pc_hal_seq_read((uint8_t)i, idx, 1, b + i, 1);

  return pc_get_u64(b);
}

//The latest capture is a sequencer run of this mode whose data the FPGA still holds (no arm since)
static int pc_seq_held(pc_session *s, uint8_t mode)
{
  return s->capture_id && s->ring_valid && (s->cap.origin == PC_ORIGIN_HOST) && (s->cap.seq_mode == mode);
}

//C6: the record READ_RAW reads is a peak record (its shift byte's peak bit): the capture in flight while armed (ALLOW_NOT_DONE),
//else the latest capture; no capture yet = legacy
static int pc_raw_peak(const pc_session *s)
{
  const pc_capture *c;

  if((s->acq_state == PC_ACQ_ARMED) || (s->acq_state == PC_ACQ_TRIGGERED))
    c = &s->cap_next;
  else if(s->capture_id)
    c = &s->cap;
  else
    return 0;

  return (c->decim_shift & PC_DECIM_PEAK_ACT) != 0;
}

static int h_read_raw(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint8_t  mask   = req[0];
  uint8_t  mode   = req[1];
  uint8_t  layout = req[2];
  uint8_t  flags  = req[3];
  int32_t  addr   = (int32_t)pc_get_u32(req + 4);
  uint32_t count  = pc_get_u32(req + 8);
  uint32_t ring   = s->var.ring_words;
  uint32_t ncores = 0, i, start = 0, total;
  uint32_t slot_base = 0, slot_len = 0;
  int64_t  base;
  uint8_t *d;

  (void)len;

  if((mask == 0) || (mask & 0xF0)) return pc_error(s, PC_E_BAD_ARG, 0, "core_mask");
  if(mode > 3)                     return pc_error(s, PC_E_BAD_ARG, 1, "addr_mode");
  if(layout > 3)                   return pc_error(s, PC_E_BAD_ARG, 2, "layout");
  if(flags & ~0x01)                return pc_error(s, PC_E_BAD_ARG, 3, "flags");
  if((count == 0) || (count > ring)) return pc_error(s, PC_E_BAD_ARG, 8, "count 1..ring_words");

  //C6 peak records (1.26): layout 3 = per channel the minima then the maxima; a 16-bit layout 2 of a peak record would
  //hand a 1.24 host { max, min } pairs as filter samples, and layout 3 of any other record has no pairs to split
  if((layout == 3) && !pc_raw_peak(s)) return pc_error(s, PC_E_BAD_ARG, 2, "layout 3 needs a peak-detect record");
  if((layout == 2) && pc_raw_peak(s))  return pc_error(s, PC_E_BAD_ARG, 2, "peak-detect record: layout 3");

  //1.17 addr_mode 3: segment-relative, address = segment << 16 | word offset from that segment's record start
  if(mode == 3)
  {
    pc_seq_cfg    sc;
    pc_seq_entry  e;
    uint32_t      seg = ((uint32_t)addr >> 16) & 0xFFFF;

    if(!pc_seq_held(s, PC_SEQ_SEGMENTS))
      return pc_error(s, PC_E_STATE, 4, "no segment run held (READ_SEQ / GET_SEQ)");

    sc.mode  = PC_SEQ_SEGMENTS;
    sc.flags = s->cap.seq_flags & PC_SEQ_F_CIRCULAR;
    sc.m     = s->cap.seq_m;
    sc.count = s->cap.seq_count;

    if(seg >= pc_seq_slots(&sc))   return pc_error(s, PC_E_BAD_ARG, 4, "segment");
    slot_len = 1u << sc.m;
    if(count > slot_len)           return pc_error(s, PC_E_BAD_ARG, 8, "count 1..slot words");

    pc_seq_entry_decode(pc_seq_entry_read(seg), &e);
    if(!e.valid)                   return pc_error(s, PC_E_STATE, 5, "segment not recorded");

    slot_base = seg << sc.m;
    start     = (pc_seq_seg_word(sc.m, seg, e.offset, s->cap.seq_pre_words, (uint32_t)addr & 0xFFFF)) - slot_base;
    ring      = slot_len;
  }
  else if(!(flags & 0x01) && (mode != 0) && pc_seq_held(s, PC_SEQ_SEGMENTS))
    return pc_error(s, PC_E_STATE, 4, "segment run: addr_mode 3");

  if((layout >= 1) && ((((mask & 0x03) != 0) && ((mask & 0x03) != 0x03)) || (((mask & 0x0C) != 0) && ((mask & 0x0C) != 0x0C))))
    return pc_error(s, PC_E_BAD_ARG, 2, "layout 1/2 needs both cores");

  //1.3: the latest finished capture of any origin, while the ring still holds it. flags bit 0 (ANY) reads the ring as
  //it is. In RUN the UI has usually armed the next capture already: wait for it (at most PC_HOLD_MS) and keep the UI
  //from re-arming until this request is answered.
  if(!(flags & 0x01) && (mode != 3))
  {
    pc_hw_ui u;

    pc_hal_ui_read(&u);

    //fw-roll: the roll's stream overwrites the ring while it runs (stopped, the ring holds the last capture as in the
    //short time bases)
    if(u.long_mode && u.run)
      return pc_error(s, PC_E_STATE, 2, "UI rolling (long time base)");

    if((s->acq_state == PC_ACQ_ARMED) || (s->acq_state == PC_ACQ_TRIGGERED))
      return pc_error(s, PC_E_STATE, 0, "not done");

    if(!s->ring_valid)
    {
      if(!u.run)
        return pc_error(s, PC_E_STATE, s->capture_id ? 1 : 0, s->capture_id ? "ring overwritten" : "no capture");

      if(!s->hold_active)
      {
        s->hold_active   = 1;
        s->ui_hold       = 1;
        s->hold_deadline = pc_hal_ms() + PC_HOLD_MS;
        return PC_HOLD;
      }

      if((int32_t)(pc_hal_ms() - s->hold_deadline) < 0)
        return PC_HOLD;

      return pc_error(s, PC_E_BUSY, PC_BUSY_CAPTURE, "UI capture running");
    }

    if(s->capture_id == 0)
      return pc_error(s, PC_E_STATE, 0, "no capture");
  }

  for(i=0;i<4;i++)
    if(mask & (1 << i))
      ncores++;

  total = 16 + count * ncores;
  if((total > PC_MAX_RESPONSE_PAYLOAD) || (total > s->tx_cap - PC_HDR_SIZE - PC_CRC_SIZE))
    return pc_error(s, PC_E_OVERFLOW, 0, "response too large");

  //Modular start (P-7.5): never the 1.00o5 "T + 7441" shortcut. Mode 3: relative to the segment's slot (start_word in
  //the header is the absolute word, ring_words the slot length)
  if(mode != 3)
  {
    base  = (mode == 0) ? 0 : ((mode == 1) ? s->capture_start : s->trigger_addr);
    start = pc_ring_mod(base + addr, ring);
  }

  pc_put_u32(resp, s->capture_id);
  pc_put_u16(resp + 4, (uint16_t)(slot_base + start));
  pc_put_u16(resp + 6, (uint16_t)ring);
  pc_put_u32(resp + 8, count);
  resp[12] = mask;
  resp[13] = layout;
  resp[14] = s->acq_state;
  resp[15] = 0;                                                                      //read_lag

  d = resp + 16;

  //BENCH: T3 (read_lag, stale first byte), T4 (interleave order)
  if(layout == 0)
  {
    for(i=0;i<4;i++)
    {
      if(mask & (1 << i))
      {
        pc_raw_core(s, (uint8_t)i, start, count, d, 1, slot_base, slot_len);
        d += count;
      }
    }
  }
  else if(layout == 2)
  {
    //1.9: u16 little-endian per word, low byte core A (0x20/0x22), high byte core B (0x21/0x23): the 16-bit samples
    //of a decimated capture
    for(i=0;i<2;i++)
    {
      if(mask & (3 << (2 * i)))
      {
        pc_raw_core(s, (uint8_t)(2 * i),     start, count, d,     2, slot_base, slot_len);
        pc_raw_core(s, (uint8_t)(2 * i + 1), start, count, d + 1, 2, slot_base, slot_len);
        d += 2 * count;
      }
    }
  }
  else if(layout == 3)
  {
    //1.26 (C6): peak pairs, per selected channel `count` u8 minima (the core A slot, 0x20/0x22) then `count` u8 maxima
    //(the core B slot, 0x21/0x23); both cores of a channel required (as layout 1 / 2)
    for(i=0;i<2;i++)
    {
      if(mask & (3 << (2 * i)))
      {
        pc_raw_core(s, (uint8_t)(2 * i),     start, count, d,         1, slot_base, slot_len);
        pc_raw_core(s, (uint8_t)(2 * i + 1), start, count, d + count, 1, slot_base, slot_len);
        d += 2 * count;
      }
    }
  }
  else
  {
    for(i=0;i<2;i++)
    {
      if(mask & (3 << (2 * i)))
      {
        //core A = 2i (0x20/0x22), core B = 2i + 1 (0x21/0x23); B first (PC_INTERLEAVE_ORDER 0)
        pc_raw_core(s, (uint8_t)(2 * i + 1), start, count, d,     2, slot_base, slot_len);
        pc_raw_core(s, (uint8_t)(2 * i),     start, count, d + 1, 2, slot_base, slot_len);
        d += 2 * count;
      }
    }
  }

  *rl = total;
  return PC_OK;
}

static int h_read_trace(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint8_t  chm   = req[0];
  uint8_t  flags = req[1];
  uint32_t start = pc_get_u32(req + 4);
  uint32_t count = pc_get_u32(req + 8);
  uint32_t samples, pre, nch = 0, total, i, k;
  int32_t  sw;
  uint8_t  origin;
  uint32_t divider;
  uint8_t *meta, *d;
  uint8_t  decim = 0, dshift = 0;
  int      wide;

  (void)len;

  if((chm == 0) || (chm & ~0x03)) return pc_error(s, PC_E_BAD_ARG, 0, "ch_mask");
  if(flags & ~0x07)               return pc_error(s, PC_E_BAD_ARG, 1, "flags");

  //1.17: a segment run has no single record in the ring order the trace readout assumes
  if(s->capture_id && (s->cap.origin == PC_ORIGIN_HOST) && (s->cap.seq_mode == PC_SEQ_SEGMENTS))
    return pc_error(s, PC_E_STATE, 5, "segment run: READ_RAW addr_mode 3");

  if(s->capture_id && (s->cap.origin == PC_ORIGIN_HOST) && (s->trace_capture_id == s->capture_id))
  {
    //Host single: built from the ring at DONE (enabled channels), again on REREAD or for channels not built yet
    samples = s->cap.total;
    pre     = s->cap.pretrigger;

    if(samples > pc_hal_trace_capacity())
      return pc_error(s, PC_E_INTERNAL, 0, "trace buffer");

    if(s->ring_valid && ((flags & 0x01) || ((s->trace_mask & chm) != chm)))
    {
      uint8_t build = (flags & 0x01) ? chm : (uint8_t)(s->trace_mask | chm);

      s->trace_sw_index = pc_hal_trace_build(s->cap.capture_start, samples, pre, build);
      s->trace_mask     = build;
    }

    sw = s->trace_sw_index;
  }
  else
  {
    //UI capture: the UI's own trace buffer and geometry
    pc_hal_trace_ui_info(&samples, &pre, &sw);

    if(samples > pc_hal_trace_capacity())
      samples = pc_hal_trace_capacity();
  }

  if(s->capture_id)
  {
    origin  = s->cap.origin;
    divider = s->cap.divider;
    decim   = s->cap.decim;
    dshift  = s->cap.decim_shift;
  }
  else
  {
    origin  = PC_ORIGIN_UI_RUN;
    divider = pc_hal_divider_now();                     //fw 0.2.87: the one the UI record ran at
  }

  if(start > samples)
    return pc_error(s, PC_E_BAD_ARG, 4, "start");

  //count 0 = to the end; a count past the end is clipped (the header reports the count sent). The buffer length is the
  //capture's, which can differ from the live GET_TRIGGER total (e.g. after SET_TRIGGER while stopped).
  if((count == 0) || (count > samples - start))
    count = samples - start;

  for(i=0;i<2;i++)
    if(chm & (1 << i))
      nch++;

  //1.9: flags bit 2 (WIDE) returns the 16-bit samples of a decimated capture (u16 little-endian, as the FPGA stored
  //them, no compensation, no inversion); otherwise the 8-bit trace as always
  wide = (flags & 0x04) && (decim & PC_DECIM_WIDE);

  if(wide)
  {
    for(i=0;i<2;i++)
    {
      uint32_t n16;

      if((chm & (1 << i)) && ((pc_hal_trace16((int)i, &n16) == 0) || (n16 < start + count)))
        return pc_error(s, PC_E_STATE, 3, "no 16-bit trace");
    }
  }

  total = 32 + 16 * nch + count * nch * (wide ? 2 : 1);
  if((total > PC_MAX_RESPONSE_PAYLOAD) || (total > s->tx_cap - PC_HDR_SIZE - PC_CRC_SIZE))
    return pc_error(s, PC_E_OVERFLOW, 0, "response too large");

  memset(resp, 0, 32);
  pc_put_u32(resp,      s->capture_id);
  pc_put_u32(resp + 4,  samples);
  pc_put_u32(resp + 8,  start);
  pc_put_u32(resp + 12, count);
  pc_put_u32(resp + 16, pre);
  pc_put_u32(resp + 20, (uint32_t)sw);
  pc_put_u32(resp + 24, divider);
  resp[28] = chm;
  resp[29] = origin;
  resp[30] = decim;                       //1.9: 0x29 byte 0 layout; bit 7 with flags bit 2 = 16-bit samples follow
  resp[31] = dshift;

  meta = resp + 32;
  d    = meta + 16 * nch;

  for(i=0;i<2;i++)
  {
    pc_hw_channel c;
    uint32_t uv;
    int32_t  zero;
    const uint8_t *src;
    int flip;

    if(!(chm & (1 << i)))
      continue;

    //Front end state the capture was taken with
    if(s->capture_id)
      c = s->cap.ch[i];
    else
      pc_hal_channel_read((int)i, &c);

    pc_channel_scaling(&c, &uv, &zero);
    flip = c.invert && (flags & 0x02);

    memset(meta, 0, 16);
    meta[0] = (uint8_t)i;
    meta[1] = c.range;
    meta[2] = c.coupling;
    meta[3] = (uint8_t)(((c.invert && !flip) ? 0x01 : 0) | ((c.overload & 0x01) ? 0x02 : 0) | ((c.overload & 0xFE) ? 0x04 : 0));
    pc_put_u32(meta + 4, uv);
    pc_put_u32(meta + 8, (uint32_t)zero);
    pc_put_u16(meta + 12, (uint16_t)c.adc_a_comp);
    pc_put_u16(meta + 14, (uint16_t)c.adc_b_comp);
    meta += 16;

    if(wide)
    {
      uint32_t n16;
      const uint16_t *s16 = pc_hal_trace16((int)i, &n16) + start;

      for(k=0;k<count;k++)
        pc_put_u16(d + 2 * k, s16[k]);

      d += 2 * count;
      continue;
    }

    src = pc_hal_trace((int)i) + start;

    if(flip)
    {
      for(k=0;k<count;k++)
        d[k] = (uint8_t)(255 - src[k]);
    }
    else
    {
      memcpy(d, src, count);
    }

    d += count;
  }

  *rl = total;
  return PC_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Decimation filter (1.9, bitstream 0x1534+, pc_decim.h)

//GET_DECIM / SET_DECIM response, 32 bytes
static uint32_t pc_decim_body(pc_session *s, uint8_t *p)
{
  pc_hw_trigger t;
  pc_decim_cfg next, cur;
  uint8_t  set = pc_hal_decim_setting();
  uint16_t rb  = pc_hal_decim_readback();
  int      written;

  pc_hal_trigger_read(&t);
  pc_decim_next(s, t.total, &next);
  pc_hal_decim_current(&cur, &written);

  memset(p, 0, 36);
  //C6: filter 4 = Sample Peak (the Filter's K / FIR are kept behind it and not applied, so the FIR flag reads 0)
  if(set & PC_DECIM_SET_PEAK)
  {
    p[0] = 4;
    set &= (uint8_t)~PC_DECIM_SET_FIR;
  }
  else
    p[0] = set & 3;                                     //UI Filter: 0 off, 1 boxcar, 2 CIC2, 3 CIC3
  p[1] = (uint8_t)((set & PC_DECIM_SET_FIR) >> 2);      //flags: bit 3 FIR (1.11); bits 0..2 always 0 (the core-order
                                                        //swap retired in 1.27, the falling-edge bits in 1.25)
  p[2] = pc_decim_status(&next);                        //what the next arm writes (0x29 byte 0 layout; peak: 0x81)
  p[3] = pc_decim_shift_byte(&next);                    //S; C6: bits 7 / 6 peak active / written (0xC0 for a peak record)
  pc_put_u32(p + 4, next.n);
  pc_put_u64(p + 8, next.gain);
  pc_put_u32(p + 16, pc_hal_divider_now());            //fw 0.2.87: the divider the FPGA holds
  pc_put_u32(p + 20, pc_decim_rate(&next, pc_hal_divider_now()));
  p[24] = written ? pc_decim_status(&cur) : 0xFF;       //written at the last arm (0xFF: not yet)
  p[25] = pc_decim_shift_byte(&cur);
  p[26] = (uint8_t)(rb >> 8);                           //FPGA 0x29 now
  p[27] = (uint8_t)rb;
  pc_put_u16(p + 28, PC_DECIM_MAX_TOTAL);
  p[30] = next.why;                                     //why the effective K is lower than requested
  p[31] = (uint8_t)pc_ext_shift(t.source, &next, pc_hal_divider_now(), t.pretrigger);   //EXT shift the next arm applies
  pc_features_body(s, p + 32);                          //1.17: as GET_STATUS 44..47
  return 36;
}

static int h_get_decim(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)req; (void)len;
  *rl = pc_decim_body(s, resp);
  return PC_OK;
}

//SET_DECIM: u8 filter (0 off, 1 boxcar, 2 CIC2, 3 CIC3; C6: 4 peak detect), u8 flags (bit 3 CIC compensation FIR, 1.11;
//bits 0..2, once the core-order swap and the falling-edge capture, are accepted and dropped: 0x153C ignores 0x53 bits
//4..2), u16 0. Sets the UI's Filter setting (shared control); the next arm (UI or host) writes it. Filter 4 (the ACQ Sample row,
//cap PEAK_DETECT) sets Sample Peak and keeps the Filter's K / FIR for the way back; the FIR flag is refused with it (the
//FIR is off while peak detect runs); filter 0..3 sets Sample Normal with that filter.
static int h_set_decim(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  pc_hw_trigger t;
  uint8_t       set;

  (void)len;

  if(req[0] > 4)     return pc_error(s, PC_E_BAD_ARG, 0, "filter 0..3, 4 peak");
  if(req[1] & ~0x0F) return pc_error(s, PC_E_BAD_ARG, 1, "flags");

  //C6 peak detect
  if(req[0] == 4)
  {
    if(req[1] & 0x08)                       return pc_error(s, PC_E_BAD_ARG, 1, "FIR with peak detect");
    set = (uint8_t)((pc_hal_decim_setting() & (PC_DECIM_SET_FIR | 3)) | PC_DECIM_SET_PEAK);
  }
  else
    set = (uint8_t)(req[0] | ((req[1] & 0x08) << 2));

  pc_hal_trigger_read(&t);

  if(req[0] && (t.total > PC_DECIM_MAX_TOTAL))
    return pc_error(s, PC_E_BAD_ARG, 0, "total > 8190: SET_TRIGGER total first");

  pc_hal_decim_set(set);
  pc_settings_track(s);

  *rl = pc_decim_body(s, resp);
  return PC_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Sequencer (1.17, bitstream 0x1536+, pc_seq.h): segmented capture and hardware averaging for host ARM

//GET_SEQ / SET_SEQ response, 48 bytes
static uint32_t pc_seq_body(pc_session *s, uint8_t *p)
{
  pc_hw_trigger t;
  pc_decim_cfg  dc;
  uint16_t      pre_w, tot_w, ext;
  uint8_t       raw[8];
  pc_seq_status st;

  pc_arm_geometry(s, &t, &dc, &pre_w, &tot_w, &ext);
  pc_hal_seq_status(raw);
  pc_seq_status_decode(raw, &st);

  memset(p, 0, 48);
  p[0] = s->seq.mode;
  p[1] = s->seq.flags;
  p[2] = s->seq.m;
  pc_put_u16(p + 4, s->seq.count);
  pc_put_u16(p + 6, (uint16_t)(s->seq.n & 0xFFFF));             //0 = 65536
  pc_put_u32(p + 8, s->seq.holdoff_ns);
  pc_put_u32(p + 12, pc_seq_holdoff_units(s->seq.holdoff_ns));   //5 ns units written to 0x5C
  p[16] = pc_seq_fit_rec(&s->seq, pre_w, tot_w, dc.k != 0, dc.peak);   //0 = the next ARM can run it (9: peak record, C6)
  p[17] = (uint8_t)(dc.k != 0);                                  //next ARM records in the 16-bit layout
  pc_put_u16(p + 18, pre_w);
  pc_put_u16(p + 20, tot_w);
  pc_put_u16(p + 22, (uint16_t)pc_seq_max_count(&s->seq, tot_w));
  p[24] = st.flags;                                              //FPGA 0x2A now
  p[25] = st.state;
  pc_put_u16(p + 26, st.index);
  pc_put_u32(p + 28, st.count);
  pc_put_u16(p + 32, st.untriggered);
  p[34] = (uint8_t)(pc_seq_held(s, PC_SEQ_SEGMENTS) || pc_seq_held(s, PC_SEQ_AVERAGE));

  //The latest capture, if it is a sequencer run
  if(s->capture_id && s->cap.seq_mode)
  {
    pc_put_u32(p + 36, s->cap.capture_id);
    pc_put_u32(p + 40, s->cap.seq_done);
    pc_put_u16(p + 44, s->cap.seq_untrig);
    p[46] = s->cap.seq_mode;
    p[47] = s->cap.seq_flags;
  }

  return 48;
}

static int h_get_seq(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)req; (void)len;
  *rl = pc_seq_body(s, resp);
  return PC_OK;
}

//SET_SEQ: u8 mode (0 off, 1 segments, 2 averaging), u8 flags (bit 0 circular segments), u8 m (segments: slot 2^m words,
//5..12), u8 0, u16 count (segments, or averaging positions), u16 N (averaging: records per position, 0 = 65536),
//u32 hold-off ns, u32 0. The next host ARM runs it. Checked against the current trigger geometry here and again at ARM.
static int h_set_seq(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  pc_hw_trigger t;
  pc_decim_cfg  dc;
  pc_seq_cfg    c;
  uint16_t      pre_w, tot_w, ext;

  (void)len;

  c.mode       = req[0];
  c.flags      = req[1];
  c.m          = req[2];
  c.count      = pc_get_u16(req + 4);
  c.n          = pc_get_u16(req + 6) ? pc_get_u16(req + 6) : PC_SEQ_MAX_N;
  c.holdoff_ns = pc_get_u32(req + 8);

  if(c.mode == PC_SEQ_OFF)
    c.flags = c.m = 0, c.count = 0, c.n = 0, c.holdoff_ns = 0;
  if(c.mode == PC_SEQ_AVERAGE)
    c.m = 0;
  if(c.mode == PC_SEQ_SEGMENTS)
    c.n = 0;

  pc_arm_geometry(s, &t, &dc, &pre_w, &tot_w, &ext);

  switch(pc_seq_fit_rec(&c, pre_w, tot_w, dc.k != 0, dc.peak))
  {
    case PC_SEQ_FIT_OK:    break;
    //C6: averaging of peak records would sum max and min bytes (docs/plans/fpga-0x153c.md section 4)
    case PC_SEQ_FIT_PEAK:  return pc_error(s, PC_E_STATE, 7, "peak detect: averaging refused (SET_DECIM filter first)");
    case PC_SEQ_FIT_MODE:  return pc_error(s, PC_E_BAD_ARG, 0, "mode 0..2");
    case PC_SEQ_FIT_FLAGS: return pc_error(s, PC_E_BAD_ARG, 1, "flags");
    case PC_SEQ_FIT_M:     return pc_error(s, PC_E_BAD_ARG, 2, "m 5..12");
    case PC_SEQ_FIT_COUNT: return pc_error(s, PC_E_BAD_ARG, 4, "count");
    case PC_SEQ_FIT_N:     return pc_error(s, PC_E_BAD_ARG, 6, "N (8-bit records: 1..257)");
    case PC_SEQ_FIT_TOTAL: return pc_error(s, PC_E_STATE, 5, "trigger total too long: SET_TRIGGER first");
    default:               return pc_error(s, PC_E_STATE, 6, "pretrigger >= total: SET_TRIGGER first");
  }

  s->seq = c;
  *rl = pc_seq_body(s, resp);
  return PC_OK;
}

//READ_SEQ: u8 what (0 segment table, 1 averaged record), u8 ch_mask (what 1), u8 S (what 1: readout shift 0..31, 0xFF
//the sums), u8 0, u16 first, u16 count (entries or words), u32 0. 16-byte header, then the data (docs/protocol.md
//row 56).
static int h_read_seq(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint8_t  what  = req[0];
  uint8_t  chm   = req[1];
  uint8_t  sh    = req[2];
  uint32_t first = pc_get_u16(req + 4);
  uint32_t count = pc_get_u16(req + 6);
  const pc_capture *c = &s->cap;
  uint32_t limit, total, i, b, bytes = 0, nch = 0;
  int      raw = 0;
  uint8_t *d;

  (void)len;

  if(what > 1)                         return pc_error(s, PC_E_BAD_ARG, 0, "what 0..1");
  if((what == 1) && ((chm == 0) || (chm & ~0x03)))
                                       return pc_error(s, PC_E_BAD_ARG, 1, "ch_mask");
  if((what == 1) && (sh > 31) && (sh != 0xFF))
                                       return pc_error(s, PC_E_BAD_ARG, 2, "S 0..31 or 0xFF");
  if(req[3])                           return pc_error(s, PC_E_BAD_ARG, 3, "flags");

  if(!s->capture_id || !c->seq_mode || (c->origin != PC_ORIGIN_HOST))
    return pc_error(s, PC_E_STATE, 0, "no sequencer run");

  if(!s->ring_valid)
    return pc_error(s, PC_E_STATE, 1, "armed since: run overwritten");

  if(c->seq_mode != (what ? PC_SEQ_AVERAGE : PC_SEQ_SEGMENTS))
    return pc_error(s, PC_E_STATE, 2, "the run is of the other mode");

  if(what == 0)
  {
    pc_seq_cfg sc;

    sc.mode  = PC_SEQ_SEGMENTS;
    sc.flags = c->seq_flags & PC_SEQ_F_CIRCULAR;
    sc.m     = c->seq_m;
    sc.count = c->seq_count;
    limit    = pc_seq_slots(&sc);
    bytes    = 8;
    nch      = 1;
  }
  else
  {
    limit = (uint32_t)c->seq_count * c->seq_total_words;
    raw   = c->seq_fmt16 && (sh == 0xFF);

    for(i=0;i<2;i++)
      if(chm & (1 << i))
        nch++;

    //legacy: two u16 lanes (core A, core B) per word; 16-bit: one u16, or one u32 sum (raw)
    bytes = c->seq_fmt16 ? (raw ? 4 : 2) : 4;
  }

  if(first >= limit)                   return pc_error(s, PC_E_BAD_ARG, 4, "first");
  if((count == 0) || (count > limit - first))
                                       return pc_error(s, PC_E_BAD_ARG, 6, "count");

  total = 16 + count * bytes * nch;
  if((total > PC_MAX_RESPONSE_PAYLOAD) || (total > s->tx_cap - PC_HDR_SIZE - PC_CRC_SIZE))
    return pc_error(s, PC_E_OVERFLOW, 0, "response too large");

  memset(resp, 0, 16);
  pc_put_u32(resp, c->capture_id);
  pc_put_u16(resp + 4, (uint16_t)first);
  pc_put_u16(resp + 6, (uint16_t)count);
  resp[8]  = what;
  resp[9]  = (uint8_t)((c->seq_fmt16 ? 0x01 : 0) | (raw ? 0x02 : 0));
  resp[10] = what ? ((c->seq_fmt16 || (sh != 0xFF)) ? sh : 0) : c->seq_m;
  resp[11] = what ? chm : 0;
  pc_put_u16(resp + 12, c->seq_pre_words);
  pc_put_u16(resp + 14, c->seq_total_words);
  d = resp + 16;

  if(what == 0)
  {
    //Entry i = bytes 0..7 of accumulator word first + i, little-endian
    for(b=0;b<8;b++)
      pc_hal_seq_read((uint8_t)b, first, count, d + b, 8);
  }
  else
  {
    //Readout shift and raw mode act at read time (0x5B, 0x57 bit 3); legacy raw = the 16-bit lane sums = S 0
    pc_hal_seq_view((uint8_t)(PC_SEQ_AVERAGE | (raw ? PC_SEQ_57_RAW : 0)), (sh == 0xFF) ? 0 : sh);

    for(i=0;i<2;i++)
    {
      if(!(chm & (1 << i)))
        continue;

      if(!c->seq_fmt16)
      {
        //Core A lane (L0 / L2), then core B lane (L1 / L3), u16 each
        for(b=0;b<4;b++)
          pc_hal_seq_read((uint8_t)(4 * i + b), first, count, d + (b >> 1) * 2 * count + (b & 1), 2);

        d += 4 * count;
      }
      else
      {
        //L0 / L2 (u16), or the 32-bit sum in bytes 0..3 / 4..7
        for(b=0;b<bytes;b++)
          pc_hal_seq_read((uint8_t)(4 * i + b), first, count, d + b, bytes);

        d += bytes * count;
      }
    }
  }

  *rl = total;
  return PC_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Generator, calibration, debug

static int h_get_gen(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)req; (void)len;

  //F_MODAL: inside a menu no FPGA access (the last 0x39 read is reported); in the main loop the status is read now, so
  //a host stepping SET_GEN can poll the pending bit (byte 1 bit 5) before its next step
  *rl = pc_gen_body(s, resp, s->ctx == PC_CTX_MAIN);
  return PC_OK;
}

//SET_GEN 0x0301 (1.27, fw-d5): the 20-byte layout kept. freq_mode 0 = bytes 4..11 u64 frequency in mHz (1 .. 25 MHz), 1 =
//the raw period P16 (1/65536 of a 5 ns clock); duty_mode 0 = bytes 12..15 u32 duty in basis points (bytes 16..19 0), 1 =
//bytes 12..19 u64 the raw high time H16. Both steps must come out at 2 .. 2^32 - 1 clocks (E_BAD_ARG detail 1 for the
//period, 2 for the duty). A duty in basis points follows a later frequency change; a raw H16 is kept as a time.
static int h_set_gen(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint8_t   mask = req[0], en;
  uint64_t  freq = pc_get_u64(req + 4), h_raw = pc_get_u64(req + 12), p16, h16;
  uint32_t  duty = pc_get_u32(req + 12);
  uint8_t   duty_raw;
  uint16_t  duty_bp;
  gp_square sq;
  int       st;

  (void)len;

  if(mask & ~0x07)  return pc_error(s, PC_E_BAD_ARG, 0xFE, "mask");
  if(req[2] > 1)    return pc_error(s, PC_E_BAD_ARG, 1, "freq_mode");
  if(req[3] > 1)    return pc_error(s, PC_E_BAD_ARG, 2, "duty_mode");

  //Start from what is in effect now; the duty of the UI's state is taken in basis points
  pc_gen_now(s, &en, &p16, &h16);
  duty_raw = (uint8_t)(s->gen_valid && s->gen_duty_raw);
  duty_bp  = s->gen_valid ? s->gen_duty_bp : 0;

  if(!s->gen_valid)
  {
    uint32_t hz;
    uint8_t  pct, e;

    pc_hal_gen_read_ui(&e, &hz, &pct);
    duty_bp = (uint16_t)(pct * 100u);

    if(!p16)
      p16 = gp_p16_from_mhz(hz ? (uint64_t)hz * 1000 : 1000);   //the UI's static level: its frequency, or 1 Hz
  }

  if(mask & 0x01)
  {
    if(req[1] > 1) return pc_error(s, PC_E_BAD_ARG, 0, "enable");
    en = req[1];
  }

  //1.4: the EXT trigger input is the generator's pin
  if((mask & 0x01) && req[1])
  {
    pc_hw_trigger t;

    pc_hal_trigger_read(&t);

    if(t.source == 2)
      return pc_error(s, PC_E_STATE, 0, "EXT trigger uses the pin");
  }

  if(mask & 0x02)
  {
    if(req[2] == 0)
    {
      if((freq == 0) || (freq > GP_FREQ_MAX_MHZ)) return pc_error(s, PC_E_BAD_ARG, 1, "freq 1 mHz .. 25 MHz");
      p16 = gp_p16_from_mhz(freq);
    }
    else
      p16 = freq;
  }

  if((p16 < GP_P16_MIN) || (p16 >= GP_P16_LIMIT))
    return pc_error(s, PC_E_BAD_ARG, 1, "period 8 clocks .. 2^33 clocks (25 MHz .. 23.3 mHz)");

  if(mask & 0x04)
  {
    if(req[3] == 0)
    {
      if(duty > 10000)              return pc_error(s, PC_E_BAD_ARG, 2, "duty > 10000");
      if(pc_get_u32(req + 16))      return pc_error(s, PC_E_BAD_ARG, 2, "reserved bytes 16..19");
      duty_raw = 0;
      duty_bp  = (uint16_t)duty;
    }
    else
    {
      duty_raw = 1;
      h16      = h_raw;
    }
  }

  if(!duty_raw)
    h16 = gp_h16_from_bp(p16, duty_bp);

  st = gp_split(p16, h16, &sq);

  if(st == 1) return pc_error(s, PC_E_BAD_ARG, 1, "period");
  if(st == 2) return pc_error(s, PC_E_BAD_ARG, 2, "both steps 2 .. 2^32 - 1 clocks (duty too close to 0 or 100 %)");

  pc_hal_gen_write(p16, h16, en);
  s->gen_p16      = p16;
  s->gen_h16      = h16;
  s->gen_duty_raw = duty_raw;
  s->gen_duty_bp  = duty_bp;
  s->gen_valid    = 1;
  s->gen_enable   = en;
  pc_hal_gen_read_ui(&s->gen_ui_en, &s->gen_ui_hz, &s->gen_ui_pct);

  *rl = pc_gen_body(s, resp, 1);
  return PC_OK;
}

void pc_cal_body(int ch, uint8_t *resp)
{
  pc_hw_channel c;
  acal_corr k;
  uint32_t i;
  int bound;

  pc_hal_channel_read(ch, &c);

  memset(resp, 0, 72);
  resp[0] = (uint8_t)ch;

  for(i=0;i<7;i++)
  {
    pc_put_u16(resp + 4 + 2 * i,  c.cal_offset[i]);
    pc_put_u32(resp + 20 + 4 * i, (uint32_t)c.input_cal[i]);
  }

  pc_put_u16(resp + 48, (uint16_t)c.adc_a_comp);
  pc_put_u16(resp + 50, (uint16_t)c.adc_b_comp);
  pc_put_u32(resp + 52, c.dc_shift_center);
  pc_put_u32(resp + 56, (uint32_t)c.dc_shift_size);
  pc_put_u32(resp + 60, (uint32_t)c.dc_shift_value);
  pc_put_u32(resp + 64, c.adc_a_rawavg);
  pc_put_u32(resp + 68, c.adc_b_rawavg);

  //1.12: calibration data v3 (protocol section 14 row 51)
  pc_channel_corr(&c, &k);
  bound = c.cal2_loaded && acal_bound(&c.cal2, c.cal_offset);
  memset(resp + 72, 0, PC_CAL_RESP_LEN - 72);
  resp[72] = k.version;
  //bit 2: the gated class in use (fw 0.2.45-0.2.104), reserved since one class (0.2.105)
  resp[73] = (uint8_t)((c.cal2_loaded ? 1 : 0) | (bound ? 2 : 0) |
                       ((c.cal2_loaded && (c.cal2.gain_range != ACAL_GAIN_NONE)) ? 8 : 0) |
                       ((c.cal2_loaded && (c.cal2.gain_flags & ACAL_GAIN_KEPT)) ? 16 : 0));
  pc_put_u16(resp + 74, (uint16_t)k.comp_a);
  pc_put_u16(resp + 76, (uint16_t)k.comp_b);
  resp[78] = k.frac_a;
  resp[79] = k.frac_b;
  pc_put_u32(resp + 80, (uint32_t)k.zero_q8);
  pc_put_u32(resp + 84, (uint32_t)k.corr16);
  pc_put_u32(resp + 88, (uint32_t)k.shift);
  pc_put_u32(resp + 92, k.gain_a);
  pc_put_u32(resp + 96, k.gain_b);
  pc_put_u32(resp + 100, (uint32_t)k.off_a);
  pc_put_u32(resp + 104, (uint32_t)k.off_b);
  pc_put_u32(resp + 108, k.gain16);

  //The stored block (slot 1 reserved: zeros)
  if(c.cal2_loaded)
    acal_pack_channel(&c.cal2, resp + 112);

  //1.15: calibration v4 tracking (row 54). Bytes 84 and 92..107 above already include it
  {
    pc_decim_cfg dc;
    int          written;
    uint32_t     now = pc_hal_ms();
    uint8_t      f   = (uint8_t)(c.trk.flags & (ACAL_TRK_DATA | ACAL_TRK_GAIN | ACAL_TRK_BOW | ACAL_TRK_LAST));

    pc_hal_decim_current(&dc, &written);

    //Applied to the next readout: v3 and the 8-bit path (any divider since one class, fw 0.2.105)
    if((k.version == 3) && (!written || !dc.k))
      f |= 0x10;

    resp[256] = f;
    pc_put_u16(resp + 258, (uint16_t)c.trk.d_q8);
    pc_put_u32(resp + 260, (uint32_t)c.trk.e_q24);
    pc_put_u32(resp + 264, (uint32_t)c.trk.q_q24);
    pc_put_u32(resp + 268, (c.trk.flags & ACAL_TRK_DATA) ? now - c.trk.last_ms : 0xFFFFFFFFu);
    pc_put_u32(resp + 272, c.trk.records);
    pc_put_u16(resp + 276, c.trk.last_pairs);
    pc_put_u16(resp + 278, c.trk.spread16);
    //280..287: the class probe's common mode (fw 0.2.50-0.2.104), 0 since the probe went with the gated class (0.2.105)
  }

  //1.16: provenance (sector 708 bytes 432..487) and the age in power-ons and operating minutes (row 55)
  {
    acal_prov  pv;
    pc_hw_life l;
    int        st = pc_hal_cal_prov(resp + 292);
    uint32_t   ons = 0xFFFFFFFFu, mins = 0xFFFFFFFFu;

    resp[288] = (uint8_t)st;

    if((st == ACAL_PROV_VALID) && (acal_prov_unpack(resp + 292, &pv) == ACAL_PROV_VALID))
    {
      pc_hal_life(&l);

      if(pv.power_on && l.power_on && (l.power_on >= pv.power_on))
        ons = l.power_on - pv.power_on;
      if(pv.power_on && l.power_on && (l.minutes >= pv.minutes))
        mins = l.minutes - pv.minutes;
    }

    pc_put_u32(resp + 348, ons);
    pc_put_u32(resp + 352, mins);
  }

  //1.24: the trained FPGA 0x153A capture stages stored with the calibration (sector 708 bytes 488..503)
  {
    pc_hw_cap cp;

    pc_hal_cap_state(&cp);
    resp[356] = cp.trailer;
    resp[357] = (uint8_t)(cp.flags & (PC_CAPF_HAS | PC_CAPF_TRAINED | PC_CAPF_OTHER));
    pc_put_u16(resp + 358, cp.stored);
    pc_put_u16(resp + 360, cp.stored_fpga);
    pc_put_u16(resp + 362, cp.value);
  }
}

static int h_get_cal(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)len;

  if(req[0] > 1)
    return pc_error(s, PC_E_BAD_ARG, 0, "channel");

  pc_cal_body(req[0], resp);
  *rl = PC_CAL_RESP_LEN;
  return PC_OK;
}

static int h_enter_fel(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)len; (void)resp;

  if(pc_get_u32(req) != PC_FEL_MAGIC)
    return pc_error(s, PC_E_BAD_ARG, 0, "magic");

  //Answer OK first; pc_poll jumps once the response has been handed to the controller (P-8.8)
  s->fel_pending = 1;
  *rl = 0;
  return PC_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// SD card sectors (protocol 1.3, section 14 row 40): persist a FEL-loaded image to sector 750 without removing the card

//First partition start from the MBR (entry 0 at 0x1BE, LBA at +8), 0 when there is no valid table
static uint32_t pc_sd_part_start(uint8_t *scratch)
{
  uint32_t lba;

  if(pc_hal_sd_read(0, 1, scratch) != 0)
    return 0;

  if((scratch[510] != 0x55) || (scratch[511] != 0xAA) || (scratch[0x1BE + 4] == 0))
    return 0;

  lba = pc_get_u32(scratch + 0x1BE + 8);
  return lba;
}

//Writes go to [PC_SD_FIRST, end): below the first partition and below PC_SD_LIMIT. 0 = no writes (no valid MBR).
static uint32_t pc_sd_write_end(uint32_t part_start)
{
  if(part_start == 0)
    return 0;

  return (part_start < PC_SD_LIMIT) ? part_start : PC_SD_LIMIT;
}

//The card image identity (pc_ident.c) checks the same window
typedef char pc_sd_limit_matches_ident[(PC_ID_END == PC_SD_LIMIT) ? 1 : -1];

//Error text for a write past the window's end (fw 0.2.101): names the partition start and the start the write needs,
//so a card partitioned too low says what to change. One static buffer: SD_WRITE runs on the main loop only.
static char pc_sd_err[64];

static char *pc_sd_put_dec(char *p, uint32_t v)
{
  char     t[10];
  uint32_t n = 0;

  do { t[n++] = (char)('0' + v % 10); v /= 10; } while(v);
  while(n) *p++ = t[--n];
  return p;
}

static const char *pc_sd_past_end_text(uint32_t part_start, uint32_t last)
{
  static const char a[] = "partition starts at ", b[] = ": it must start at ", c[] = " or later";
  static const char d[] = "past sector ", e[] = ", the end of the write window";
  char *p = pc_sd_err;

  if(part_start >= PC_SD_LIMIT)
  {
    memcpy(p, d, sizeof(d) - 1); p = pc_sd_put_dec(p + sizeof(d) - 1, PC_SD_LIMIT - 1);
    memcpy(p, e, sizeof(e));
    return pc_sd_err;
  }

  memcpy(p, a, sizeof(a) - 1); p = pc_sd_put_dec(p + sizeof(a) - 1, part_start);
  memcpy(p, b, sizeof(b) - 1); p = pc_sd_put_dec(p + sizeof(b) - 1, last + 1);
  memcpy(p, c, sizeof(c));
  return pc_sd_err;
}

//eGON header at a sector: magic, stored length and checksum; with check, the checksum of `length` bytes on the card
static void pc_sd_egon(uint32_t sector, int check, uint32_t card, uint8_t *scratch, uint8_t *out)
{
  uint32_t length, stored, sum = 0, done = 0, n, i;
  uint8_t  status = 0, result = 0;

  memset(out, 0, 16);
  pc_put_u32(out, sector);

  if(pc_hal_sd_read(sector, 1, scratch) != 0)
    return;

  if(!memcmp(scratch + 4, "eGON.BT0", 8))
    status = 1;
  else if(!memcmp(scratch + 4, "eGON.EXE", 8))
    status = 2;

  stored = pc_get_u32(scratch + 12);
  length = pc_get_u32(scratch + 16);

  if(status && check)
  {
    //Sum of u32 words over length bytes with the checksum field read as the stamp (firmware/tools/mksunxi.py)
    if((length < 512) || (length % 512) || (length / 512 > PC_SD_LIMIT - sector) || (sector + length / 512 > card))
      result = 3;
    else
    {
      while(done < length / 512)
      {
        n = length / 512 - done;

        if(n > PC_SD_MAX_COUNT)
          n = PC_SD_MAX_COUNT;

        if(pc_hal_sd_read(sector + done, n, scratch) != 0)
        {
          result = 3;
          break;
        }

        if(done == 0)
          pc_put_u32(scratch + 12, 0x5F0A6C39u);

        for(i=0;i<n * 512;i+=4)
          sum += pc_get_u32(scratch + i);

        done += n;
      }

      if(result == 0)
        result = (sum == stored) ? 1 : 2;
    }
  }

  out[4] = status;
  out[5] = result;
  pc_put_u32(out + 8,  length);
  pc_put_u32(out + 12, stored);
}

//SD_INFO: u8 flags (bit 0 VERIFY: recompute the eGON checksums), 3 reserved
//SD_INFO body: 112 bytes since 1.16 (row 55: card image identity at 64..111). scratch: 4-byte aligned, a multiple of
//512 bytes, at least 64 KiB with verify (else 1 KiB). Returns 0, or 1 without a card.
int pc_sd_info_body(pc_session *s, int verify, int ident, uint8_t *resp, uint8_t *scratch, uint32_t scratch_bytes)
{
  static const uint32_t images[3] = { 16, 80, 750 };
  uint32_t card = pc_hal_sd_sectors(), part, i;
  const pc_card_id *id;

  if(card == 0)
    return 1;

  part = pc_sd_part_start(scratch);

  memset(resp, 0, PC_SD_INFO_LEN);
  pc_put_u32(resp,      card);
  pc_put_u32(resp + 4,  part);
  pc_put_u32(resp + 8,  pc_sd_write_end(part));
  pc_put_u32(resp + 12, s->sd_stage_cap);

  for(i=0;i<3;i++)
    pc_sd_egon(images[i], verify, card, scratch, resp + 16 + 16 * i);

  id = pc_ident_card(ident, scratch, scratch_bytes);
  resp[64] = id->state;
  resp[65] = id->same;
  resp[66] = id->proto_major;
  resp[67] = id->proto_minor;
  memcpy(resp + 68, id->version, 16);
  pc_put_u32(resp + 84, id->build);
  memcpy(resp + 88, id->time, 6);
  pc_put_u32(resp + 96, id->length);
  pc_put_u32(resp + 100, id->read_ms);
  return 0;
}

//SD_INFO: u8 flags (bit 0 VERIFY: recompute the eGON checksums; 1.16 bit 1 IDENT: identify the sector 750 image), 3
//reserved
static int h_sd_info(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)len;

  if(req[0] & ~0x03)
    return pc_error(s, PC_E_BAD_ARG, 0, "flags");

  //64 KiB scratch inside the TX staging buffer, after the answer
  if(pc_sd_info_body(s, req[0] & 0x01, req[0] & 0x02, resp, resp + PC_SD_INFO_LEN, 65536))
    return pc_error(s, PC_E_IO, 0, "no card");

  *rl = PC_SD_INFO_LEN;
  return PC_OK;
}

//SD_READ_SECTORS: u32 sector, u16 count (1..128), u16 reserved
static int h_sd_read(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint32_t sector = pc_get_u32(req);
  uint32_t count  = pc_get_u16(req + 4);
  uint32_t card   = pc_hal_sd_sectors();
  int      r;

  (void)len;

  if(card == 0)                                        return pc_error(s, PC_E_IO, 0, "no card");
  if((count == 0) || (count > PC_SD_MAX_COUNT))        return pc_error(s, PC_E_BAD_ARG, 4, "count 1..128");
  if((sector >= card) || (count > card - sector))      return pc_error(s, PC_E_BAD_ARG, 0, "beyond the card");
  if(16 + count * 512 > s->tx_cap - PC_HDR_SIZE - PC_CRC_SIZE) return pc_error(s, PC_E_OVERFLOW, 0, "response too large");

  r = pc_hal_sd_read(sector, count, resp + 16);

  if(r != 0)
    return pc_error(s, PC_E_IO, (uint16_t)(-r), "read");

  pc_put_u32(resp,      sector);
  pc_put_u16(resp + 4,  (uint16_t)count);
  pc_put_u16(resp + 6,  0);
  pc_put_u32(resp + 8,  pc_crc32(resp + 16, count * 512));
  pc_put_u32(resp + 12, 0);
  *rl = 16 + count * 512;
  return PC_OK;
}

//SD_STAGE: u32 offset, u16 length, u16 reserved, then length bytes (length = payload - 8). Answers the CRC-32 of them.
static int h_sd_stage(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint32_t off = pc_get_u32(req);
  uint32_t n   = pc_get_u16(req + 4);

  if((len < 8) || (n != len - 8))                       return pc_error(s, PC_E_BAD_LEN, 4, "length != payload - 8");
  if((off > s->sd_stage_cap) || (n > s->sd_stage_cap - off)) return pc_error(s, PC_E_BAD_ARG, 0, "beyond the stage");

  memcpy(s->sd_stage + off, req + 8, n);

  pc_put_u32(resp,     off);
  pc_put_u16(resp + 4, (uint16_t)n);
  pc_put_u16(resp + 6, 0);
  pc_put_u32(resp + 8, pc_crc32(req + 8, n));
  *rl = 12;
  return PC_OK;
}

//SD_WRITE_SECTORS: u32 sector, u16 count (1..128), u8 flags (bit0 CONFIG 708-710, bit1 BOOT 16-79), u8 reserved,
//u32 crc32 of stage[0, count x 512), u32 magic 'SDWR'. Writes, reads back, compares; answers the CRC-32 on the card.
static int h_sd_write(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint32_t sector = pc_get_u32(req);
  uint32_t count  = pc_get_u16(req + 4);
  uint8_t  flags  = req[6];
  uint32_t crc    = pc_get_u32(req + 8);
  uint32_t card   = pc_hal_sd_sectors();
  uint8_t *back   = resp + 16;                         //read-back lands in the TX staging buffer
  uint32_t part, end, last;
  int      r;

  (void)len;

  if(pc_get_u32(req + 12) != PC_SD_WRITE_MAGIC)       return pc_error(s, PC_E_BAD_ARG, 12, "magic");
  if(flags & ~(PC_SD_WF_CONFIG | PC_SD_WF_BOOT))       return pc_error(s, PC_E_BAD_ARG, 6, "flags");
  if((count == 0) || (count > PC_SD_MAX_COUNT) || (count * 512 > s->sd_stage_cap))
                                                       return pc_error(s, PC_E_BAD_ARG, 4, "count 1..128");
  if(card == 0)                                        return pc_error(s, PC_E_IO, 0, "no card");

  part = pc_sd_part_start(back);
  end  = pc_sd_write_end(part);
  last = sector + count - 1;

  if(end == 0)                                         return pc_error(s, PC_E_STATE, 0, "no partition table: writes off");
  if((sector < PC_SD_FIRST) || (last >= card) || (last < sector))
                                                       return pc_error(s, PC_E_BAD_ARG, 0, "outside the write window");
  if(last >= end)                                      return pc_error(s, PC_E_BAD_ARG, 0, pc_sd_past_end_text(part, last));
  if((sector < PC_SD_BOOT_END) && !(flags & PC_SD_WF_BOOT))
                                                       return pc_error(s, PC_E_BAD_ARG, 0, "16..79 bootloader: flag BOOT");
  if((sector <= PC_SD_CFG_LAST) && (last >= PC_SD_CFG_FIRST) && !(flags & PC_SD_WF_CONFIG))
                                                       return pc_error(s, PC_E_BAD_ARG, 0, "708..710 config: flag CONFIG");
  if(pc_crc32(s->sd_stage, count * 512) != crc)        return pc_error(s, PC_E_BAD_ARG, 8, "stage crc");

  r = pc_hal_sd_write(sector, count, s->sd_stage);

  //The card image may have changed: identify it afresh next time (1.16)
  if(last >= PC_ID_SECTOR)
    pc_ident_invalidate();

  if(r != 0)
    return pc_error(s, PC_E_IO, (uint16_t)(-r), "write");

  r = pc_hal_sd_read(sector, count, back);

  if(r != 0)
    return pc_error(s, PC_E_IO, (uint16_t)(-r), "read back");

  if(memcmp(back, s->sd_stage, count * 512))
    return pc_error(s, PC_E_IO, 0xFFFF, "verify");

  pc_put_u32(resp,     sector);
  pc_put_u16(resp + 4, (uint16_t)count);
  pc_put_u16(resp + 6, 0);
  pc_put_u32(resp + 8, pc_crc32(back, count * 512));
  pc_put_u32(resp + 12, 0);
  *rl = 16;
  return PC_OK;
}

//SAVE_REPORT (1.16, row 55): the diagnostics report as a text file on the card. The TX staging buffer after the answer
//is the workspace.
static int h_save_report(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  char     name[48];
  uint32_t written, i;
  int      r;

  (void)req; (void)len;

  if(pc_hal_sd_sectors() == 0)
    return pc_error(s, PC_E_IO, 0, "no card");

  r = pc_save_report(s, resp + 56, PC_MAX_RESPONSE_PAYLOAD - 56, &written, name, sizeof(name));

  if(r)
    return pc_error(s, PC_E_IO, (uint16_t)r, "report file");

  memset(resp, 0, 56);
  pc_put_u32(resp, written);

  for(i=0;(i<sizeof(name)) && name[i];i++)
    resp[8 + i] = (uint8_t)name[i];

  pc_put_u16(resp + 6, (uint16_t)i);
  *rl = 56;
  return PC_OK;
}

#if PC_DEBUG
static int h_debug_fpga(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint32_t i = 0, out = 0, n, k;

  (void)s;

  //Validate the whole list before touching the bus
  while(i < len)
  {
    if(i + 2 > len) return pc_error(s, PC_E_BAD_LEN, (uint16_t)i, "truncated op");
    n = req[i + 1];

    switch(req[i])
    {
      case 1:  if((n != 1) || (i + 3 > len)) return pc_error(s, PC_E_BAD_LEN, (uint16_t)i, "WRITE_CMD"); i += 3; break;
      case 2:  if(i + 2 + n > len)           return pc_error(s, PC_E_BAD_LEN, (uint16_t)i, "WRITE_DATA"); i += 2 + n; break;
      case 3:  out += n ? n : 256; i += 2; break;
      default: return pc_error(s, PC_E_BAD_ARG, (uint16_t)i, "op");
    }
  }

  if(out > PC_MAX_RESPONSE_PAYLOAD)
    return pc_error(s, PC_E_OVERFLOW, 0, "too many reads");

  for(i=0, out=0; i<len; )
  {
    n = req[i + 1];

    if(req[i] == 1)
    {
      pc_hal_fpga_write_cmd(req[i + 2]);
      i += 3;
    }
    else if(req[i] == 2)
    {
      for(k=0;k<n;k++)
        pc_hal_fpga_write_data(req[i + 2 + k]);
      i += 2 + n;
    }
    else
    {
      if(n == 0)
        n = 256;
      for(k=0;k<n;k++)
        resp[out++] = pc_hal_fpga_read_data();
      i += 2;
    }
  }

  *rl = out;
  return PC_OK;
}
#else
#define h_debug_fpga NULL
#endif

//----------------------------------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------------------------------
// FFT_BENCH (1.6, fw 0.2.27): u8 reps (0 = 3, at most 16); main loop only, blocks the UI pass for the run time

static int h_fft_bench(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint32_t reps = req[0];

  (void)len;

  if(reps == 0)
    reps = 3;
  if(reps > 16)
    return pc_error(s, PC_E_BAD_ARG, 0, "reps");

  *rl = pc_hal_fft_bench(resp, reps);
  return PC_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// READ_SPECTRUM (1.9, fw 0.2.28): u8 channel, u8 0, u16 first bin, u16 count (0 = as many as fit), u16 0

static int h_read_spectrum(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint32_t first = pc_get_u16(req + 2);
  uint32_t count = pc_get_u16(req + 4);
  uint32_t max = (PC_MAX_RESPONSE_PAYLOAD - PC_SPECTRUM_HDR) / 2;
  int32_t  n;

  (void)len;

  if(req[0] > 1)
    return pc_error(s, PC_E_BAD_ARG, 0, "channel");

  if((count == 0) || (count > max))
    count = max;

  n = pc_hal_spectrum_read(req[0], first, count, resp, resp + PC_SPECTRUM_HDR);
  if(n < 0)
    return pc_error(s, PC_E_STATE, 0, "no spectrum shown for this channel");

  *rl = PC_SPECTRUM_HDR + 2 * (uint32_t)n;
  return PC_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// CAL_PROBE (1.21 provisional, fw 0.2.67): u8 channel, u8 hardware range 0..5, u8 settle (0..15 extra data-port reads
// per sample), u8 prestrobe (0, 1: a write strobe before each sample strobe); settle and prestrobe are A/B tests of the
// readout, 0 = the wizard's; u8 mode (0 one range, 1 the whole zero step in the wizard's screen context, both channels,
// the response for channel's last measured range). The calibration wizard's zero step for
// that range, exactly as the wizard measures it (cal_wiz_ui.c cal_range), nothing stored; about 1 s, main loop only.
// The UI must be stopped (SET_ACQ_STATE 0) and no host capture pending; the FPGA gets the UI's settings back afterwards

static int h_cal_probe(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  pc_hw_ui u;
  int32_t  n;

  (void)len;

  if(req[0] > 1)
    return pc_error(s, PC_E_BAD_ARG, 0, "channel");

  if(req[1] >= ACAL_RANGES)
    return pc_error(s, PC_E_BAD_ARG, 1, "range 0..5");

  if(req[2] > 15)
    return pc_error(s, PC_E_BAD_ARG, 2, "settle 0..15");

  if(req[3] > 1)
    return pc_error(s, PC_E_BAD_ARG, 3, "prestrobe 0..1");

  if(req[4] > 1)
    return pc_error(s, PC_E_BAD_ARG, 4, "mode 0..1");

  if((s->acq_state == PC_ACQ_ARMED) || (s->acq_state == PC_ACQ_TRIGGERED))
    return pc_error(s, PC_E_STATE, 0, "host capture pending");

  pc_hal_ui_read(&u);

  if(u.run)
    return pc_error(s, PC_E_STATE, 1, "UI running: SET_ACQ_STATE 0 first");

  n = pc_hal_cal_probe(req[0], req[1], req[2], req[3], req[4], resp);

  //It ran for seconds inside this main-loop poll: the modal watch must not count that as a modal screen afterwards
  s->last_main_ms = pc_hal_ms();
  s->ui_busy      = 0;

  if(n < 0)
    return pc_error(s, PC_E_INTERNAL, 0, "cal probe");

  *rl = (uint32_t)n;
  return PC_OK;
}

//BODE_CTRL (1.20): u8 action, u8 flags (bit 0 the settings below apply, bit 1 no background captures), u8 settle 0..3,
//u8 CH2 range mode (0 auto, 1 hold), u32 start cHz, u32 stop cHz, u16 points 1..200, u16 0. Answers the READ_BODE header
//and u32 the estimated duration of a sweep in ms, u32 0 (docs/protocol.md row 60)
static int h_bode_ctrl(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint16_t detail = 0;
  int st;

  (void)len;

  st = pc_hal_bode_ctrl(req, resp, &detail);

  switch(st)
  {
    case PC_OK:
      *rl = PC_BODE_CTRL_RESP;
      return PC_OK;

    case PC_E_BAD_ARG:
      return pc_error(s, st, detail, (detail == 0) ? "action 0..10" : (detail == 1) ? "settle 0..3" :
                      (detail == 2) ? "CH2 range mode 0..1" : (detail == 3) ? "start / stop 1 Hz .. 10 MHz" : "points 1..200");

    default:
      return pc_error(s, st, detail, (detail == 1) ? "Bode not available (a viewer is open)" :
                      (detail == 2) ? "a sweep is running" : (detail == 4) ? "no thru calibration" : "Bode");
  }
}

//READ_BODE (1.20): u16 first row, u16 count (0 or more than fit: all that fit), u32 0. The header, then the rows
static int h_read_bode(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint32_t first = pc_get_u16(req), count = pc_get_u16(req + 2);
  uint32_t max = (PC_MAX_RESPONSE_PAYLOAD - PC_BODE_HDR) / PC_BODE_ROW;

  (void)s; (void)len;

  if((count == 0) || (count > max))
    count = max;

  *rl = pc_hal_bode_read(first, count, resp);
  return PC_OK;
}

//READ_SCREEN (fw 0.2.70): u16 row, u16 nrows. u16 width, u16 row, u16 nrows, u16 format 1 (RGB565 little-endian), then the
//rows from the framebuffer the panel shows. MAIN, or (fw 0.2.94) a modal loop's yield check: never a UI pass half drawn;
//a menu, dialog or screen that is open stays open and is what the screenshot shows (1.23-0.2.93: it closed first)
static int h_read_screen(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint32_t row = pc_get_u16(req), n = pc_get_u16(req + 2);
  uint32_t total = 8 + n * PC_SCREEN_WIDTH * 2;

  (void)len;

  if((row >= PC_SCREEN_HEIGHT) || (n == 0) || (row + n > PC_SCREEN_HEIGHT))
    return pc_error(s, PC_E_BAD_ARG, 0, "rows outside 0..480");

  if((total > PC_MAX_RESPONSE_PAYLOAD) || (total > s->tx_cap - PC_HDR_SIZE - PC_CRC_SIZE))
    return pc_error(s, PC_E_OVERFLOW, 2, "at most 41 rows per request");

  pc_put_u16(resp,     PC_SCREEN_WIDTH);
  pc_put_u16(resp + 2, row);
  pc_put_u16(resp + 4, n);
  pc_put_u16(resp + 6, 1);
  pc_hal_screen_rows(row, n, resp + 8);                                              //BENCH: T9
  *rl = total;
  return PC_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
//D4 logger (fw-d4, item D4 of docs/plans/fpga-0x153c.md section 6.3; FPGA 0x153D 0x69 / 0x6A / 0x38; protocol 1.27 row 67,
//byte-exact with mcp-d6). LOG_START arms the stream and the main loop becomes the log loop (pc_hal_scope.c): drain steps
//into the 8 MiB DDR ring and MAIN polls, the Scope view frozen with a "Logging to the host" line. While it runs, every
//command outside the F_BUSY and F_MODAL sets and LOG_READ / LOG_STOP answers E_STATE detail PC_STATE_LOGGING (pc_dispatch).
//The log ends by LOG_STOP (state 3, the FPGA's tail read), its duration (2), or an error (4: the FPGA left stream mode, the
//link reset or DTR dropped, or no LOG_READ for PC_LOG_HOST_MS); the DDR ring stays readable until the next LOG_START.

//The LOG response (48 bytes): 0 state, 1 channels, 2 format, 3 flags, 4 u32 divider, 8 u32 duration_ms, 12 u32 sample
//clocks, 16 u64 bytes produced, 24 u64 bytes sent, 32 u32 gaps, 36 u32 DDR ring bytes, 40 u32 ms since the start, 44 0
static uint32_t pc_log_body(pc_session *s, uint8_t *p)
{
  pc_hw_log l;

  pc_hal_log_info(&l);
  memset(p, 0, PC_LOG_LEN);
  p[0] = l.state;
  p[1] = l.ch_mask;
  p[2] = l.format;
  p[3] = l.flags;
  pc_put_u32(p + 4,  s->log_divider);
  pc_put_u32(p + 8,  s->log_duration_ms);
  pc_put_u32(p + 12, s->log_clocks);
  pc_put_u64(p + 16, l.produced);
  pc_put_u64(p + 24, l.sent);
  pc_put_u32(p + 32, l.gaps);
  pc_put_u32(p + 36, l.ring_bytes);
  pc_put_u32(p + 40, l.ms);
  return PC_LOG_LEN;
}

//Every poll: acq_state follows the log's end; MAIN stops a log whose host went away
static void pc_log_service(pc_session *s, int ctx)
{
  pc_hw_log l;

  if(s->acq_state != PC_ACQ_LOGGING)
    return;

  if((ctx == PC_CTX_MAIN) && (s->log_link_lost || ((pc_hal_ms() - s->log_read_ms) >= PC_LOG_HOST_MS)))
  {
    pc_hal_log_stop(FPGA_LOG_ERROR);
    s->log_link_lost = 0;
  }

  pc_hal_log_info(&l);

  if(l.state != FPGA_LOG_RUNNING)
    s->acq_state = PC_ACQ_IDLE;
}

//LOG_START: u8 channels (bit 0 CH1, bit 1 CH2), u8 format (0 legacy 8-bit, 1 filtered high byte, 2 filtered 16-bit, 3 peak
//pairs), u16 0, u32 divider (0x0D), u32 duration_ms (0 = until LOG_STOP), u32 0. The filter is SET_DECIM's (format 0 needs
//it off, 1 / 2 a filter 1..3, 3 peak, each effective at this divider); the rate: sample rate x bytes per sample x channels
//<= 500,000 B/s (contract 6.2: 500 kS/s 1 ch 8-bit, 250 kS/s 1 ch 16-bit / peak or 2 ch 8-bit, 125 kS/s 2 ch 16-bit)
static int h_log_start(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint8_t  ch = req[0], fmt = req[1], set, mode, nch, bps;
  uint32_t div = pc_get_u32(req + 4), dur = pc_get_u32(req + 8), clocks, wclocks;
  uint64_t limit = 0;
  pc_decim_cfg dc;
  pc_hw_ui u;
  int r;

  (void)len;

  if((s->acq_state == PC_ACQ_ARMED) || (s->acq_state == PC_ACQ_TRIGGERED))
    return pc_error(s, PC_E_STATE, 0, "host capture armed: STOP first");

  //fw-roll: in the long time bases too: the UI's roll stops below and pc_hal_log_start takes the stream from it
  pc_hal_ui_read(&u);

  mode = pc_hal_display_mode();

  if((mode != 0) && (mode != 4))
    return pc_error(s, PC_E_STATE, PC_STATE_LOG_VIEW, "LOG_START needs the Scope view");

  if((ch == 0) || (ch > 3))
    return pc_error(s, PC_E_BAD_ARG, 0, "channels: bit 0 CH1, bit 1 CH2");

  if(fmt >= FPGA_LOG_F_COUNT)
    return pc_error(s, PC_E_BAD_ARG, 2, "format 0..3");

  if(div > PC_DIVIDER_MAX)
    return pc_error(s, PC_E_BAD_ARG, 4, "divider above 1048575");

  set = pc_hal_decim_setting();
  pc_decim_config(set & 3, set & (PC_DECIM_SET_FIR | PC_DECIM_SET_PEAK), div, 2, pc_decim_has(), &dc);

  if((fmt == FPGA_LOG_F_LEGACY) && pc_decim_wide(&dc))
    return pc_error(s, PC_E_BAD_ARG, 2, "format 0 needs SET_DECIM filter 0");

  if(((fmt == FPGA_LOG_F_HIGH) || (fmt == FPGA_LOG_F_16)) && (!dc.k || dc.peak))
    return pc_error(s, PC_E_BAD_ARG, 2, "formats 1, 2 need SET_DECIM filter 1..3 at divider >= 1");

  if((fmt == FPGA_LOG_F_PEAK) && !dc.peak)
    return pc_error(s, PC_E_BAD_ARG, 2, "format 3 needs SET_DECIM filter 4 (peak) at divider >= 1");

  //Sample clocks of 5 ns: D + 1 in the legacy layout (two samples per word), N on the 16-bit path (one per word)
  clocks  = pc_decim_wide(&dc) ? dc.n : div + 1;
  wclocks = pc_decim_wide(&dc) ? clocks : 2 * clocks;
  nch     = (uint8_t)((ch & 1) + ((ch >> 1) & 1));
  bps     = (uint8_t)(((fmt == FPGA_LOG_F_16) || (fmt == FPGA_LOG_F_PEAK)) ? 2 : 1);

  //200e6 / clocks x bps x nch <= 500,000
  if((uint32_t)400 * bps * nch > clocks)
    return pc_error(s, PC_E_BAD_ARG, 1, "rate > 500 kB/s: 500k 1ch 8b, 250k 16b or 2ch, 125k 2ch 16b");

  //Words for the duration, rounded up (200,000 clocks per ms)
  if(dur)
    limit = ((uint64_t)dur * 200000 + wclocks - 1) / wclocks;

  //The UI's own conversions stop (the screen shows STOP after the log, as after a host ARM)
  if(u.run)
    pc_hal_ui_set_run(0);

  if(s->settle_pending)
  {
    int32_t left = (int32_t)(s->settle_until - pc_hal_ms());

    if((left > 0) && (left <= PC_SETTLE_MS))
      pc_hal_delay_ms((uint32_t)left);

    s->settle_pending = 0;
  }

  s->ring_valid      = 0;
  s->log_used        = 1;
  s->log_link_lost   = 0;
  s->log_divider     = div;
  s->log_duration_ms = dur;
  s->log_clocks      = clocks;
  s->log_seq         = 0;
  s->log_read_ms     = pc_hal_ms();

  r = pc_hal_log_start(ch, fmt, div, &dc, limit);

  if(r)
    return pc_error(s, PC_E_FPGA, 0, "0x05 not ready");

  s->acq_state = PC_ACQ_LOGGING;
  *rl = pc_log_body(s, resp);
  return PC_OK;
}

//LOG_READ: u32 max_bytes (1..65536), u32 0. Returns at once: a 16-byte header (u32 seq: +1 per non-empty chunk, u32 the
//FPGA word counter of the first word, u32 bytes, u8 flags: bit 0 running, 1 gap before this chunk, 2 ended (the log ended
//and nothing is left after this chunk), 3 the gap includes words the full DDR ring refused; u8 format, u16 0) and whole
//words of one segment, oldest first; 0 bytes when nothing is unread
static int h_log_read(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint32_t max = pc_get_u32(req), n;
  uint64_t w;
  uint8_t  f;
  pc_hw_log l;

  (void)len;

  if(!s->log_used)
    return pc_error(s, PC_E_STATE, 0, "no log: LOG_START first");

  if((max == 0) || (max > PC_LOG_READ_MAX))
    return pc_error(s, PC_E_BAD_ARG, 0, "max_bytes 1..65536");

  s->log_read_ms = pc_hal_ms();
  n = pc_hal_log_read(resp + PC_LOG_HDR, max, &w, &f);
  pc_hal_log_info(&l);

  if(l.state == FPGA_LOG_RUNNING)
    f |= PC_LOG_CF_RUNNING;
  else if(l.sent == l.produced)
    f |= PC_LOG_CF_ENDED;

  if(n)
    s->log_seq++;

  pc_put_u32(resp,     s->log_seq);
  pc_put_u32(resp + 4, (uint32_t)w);
  pc_put_u32(resp + 8, n);
  resp[12] = f;
  resp[13] = l.format;
  pc_put_u16(resp + 14, 0);
  *rl = PC_LOG_HDR + n;
  return PC_OK;
}

//LOG_STOP: no request bytes. Stops a running log (state 3; the words the FPGA wrote up to the stop are read into the DDR
//ring), else changes nothing; the LOG response either way
static int h_log_stop(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)req; (void)len;

  if(s->acq_state == PC_ACQ_LOGGING)
  {
    pc_hal_log_stop(FPGA_LOG_STOPPED);
    s->acq_state = PC_ACQ_IDLE;
  }

  *rl = pc_log_body(s, resp);
  return PC_OK;
}
//D4 logger end

static const pc_cmd_entry pc_cmds[] =
{
  { PC_CMD_PING,         F_BUSY | F_CONTROL | F_BASIC, 0, h_ping },
  { PC_CMD_GET_INFO,     F_BUSY | F_BASIC,             0, h_get_info },
  { PC_CMD_GET_STATUS,   F_BUSY | F_CONTROL | F_BASIC, 0, h_get_status },
  { PC_CMD_SET_REMOTE,   F_BUSY | F_CONTROL,           0, h_set_remote },
  { PC_CMD_GET_BATTERY,  F_BUSY | F_BASIC,             0, h_get_battery },
  { PC_CMD_TEST_PATTERN, F_BASIC,                      0, h_test_pattern },
  { PC_CMD_GET_LINK_STATS, F_BUSY | F_CONTROL | F_BASIC, 0, h_get_link_stats },
  { PC_CMD_GET_BAD_FRAME, F_BUSY | F_CONTROL | F_BASIC, 0, h_get_bad_frame },
  { PC_CMD_GET_EVENT_LOG, F_BUSY | F_CONTROL | F_BASIC, 0, h_get_event_log },
  { PC_CMD_GET_HEALTH,   F_BUSY | F_CONTROL | F_BASIC, 0, h_get_health },
  { PC_CMD_GET_CHANNEL,  F_MODAL,                      0, h_get_channel },
  { PC_CMD_SET_CHANNEL,  0,                            0, h_set_channel },
  { PC_CMD_GET_TIMEBASE, F_MODAL,                      0, h_get_timebase },
  { PC_CMD_SET_TIMEBASE, 0,                            0, h_set_timebase },
  { PC_CMD_GET_TRIGGER,  F_MODAL,                      0, h_get_trigger },
  { PC_CMD_SET_TRIGGER,  0,                            0, h_set_trigger },
  { PC_CMD_SET_TRIGGER2, 0,                            PC_CAP_TRIG_TYPES, h_set_trigger2 },   //1.26 (fw-c5)
  { PC_CMD_GET_COUNTER,  F_MODAL,                      PC_CAP_COUNTER, h_get_counter },       //C7 counter (memory only)
  { PC_CMD_LOG_START,    0,                            PC_CAP_LOGGER, h_log_start },          //D4 logger
  { PC_CMD_LOG_READ,     0,                            PC_CAP_LOGGER, h_log_read },           //D4 logger (MAIN: the drain's ring)
  { PC_CMD_LOG_STOP,     F_CONTROL,                    PC_CAP_LOGGER, h_log_stop },           //D4 logger
  { PC_CMD_ARM,          0,                            0, h_arm },
  { PC_CMD_FORCE,        0,                            PC_CAP_FORCE_TRIGGER, h_force },
  { PC_CMD_STOP,         F_CONTROL,                    0, h_stop },
  { PC_CMD_WAIT,         0,                            0, h_wait },
  { PC_CMD_READ_RAW,     0,                            0, h_read_raw },
  { PC_CMD_READ_TRACE,   0,                            0, h_read_trace },
  { PC_CMD_RETIRED_0206, 0,                            0, NULL },     //was READ_LIVE: E_UNSUPPORTED as before
  { PC_CMD_ACQUIRE_AVG,  0,                            PC_CAP_DEVICE_AVERAGE, NULL },
  { PC_CMD_SET_ACQ_STATE, F_CONTROL,                   0, h_set_acq_state },
  { PC_CMD_GET_CAPTURE_INFO, F_BUSY | F_CONTROL,       0, h_get_capture_info },
  { PC_CMD_READ_SPECTRUM, F_BASIC,                     0, h_read_spectrum },
  { PC_CMD_GET_DECIM,    F_MODAL,                      PC_CAP_DECIM, h_get_decim },
  { PC_CMD_SET_DECIM,    0,                            PC_CAP_DECIM, h_set_decim },
  { PC_CMD_SET_SEQ,      0,                            PC_CAP_SEQ, h_set_seq },
  { PC_CMD_GET_SEQ,      F_MODAL,                      PC_CAP_SEQ, h_get_seq },
  { PC_CMD_READ_SEQ,     0,                            PC_CAP_SEQ, h_read_seq },
  { PC_CMD_BODE_CTRL,    0,                            PC_CAP_DECIM, h_bode_ctrl },
  { PC_CMD_READ_BODE,    F_MODAL,                      PC_CAP_DECIM, h_read_bode },
  { PC_CMD_SD_INFO,      F_BASIC,                      PC_CAP_SD_SECTORS, h_sd_info },
  { PC_CMD_SD_READ_SECTORS, F_BASIC,                   PC_CAP_SD_SECTORS, h_sd_read },
  { PC_CMD_SD_STAGE,     F_BUSY | F_BASIC,             PC_CAP_SD_SECTORS, h_sd_stage },   //memory copy only: timer poll
  { PC_CMD_SD_WRITE_SECTORS, F_BASIC,                  PC_CAP_SD_SECTORS, h_sd_write },
  { PC_CMD_SAVE_REPORT,  F_BASIC,                      PC_CAP_SD_SECTORS, h_save_report },
  { PC_CMD_GET_GEN,      F_MODAL,                      0, h_get_gen },
  { PC_CMD_SET_GEN,      0,                            PC_CAP_GENERATOR, h_set_gen },
  { PC_CMD_GET_CAL,      F_MODAL | F_BASIC,            0, h_get_cal },      //sector 708: no FPGA
  { PC_CMD_READ_SCREEN,  F_BASIC | F_MODAL,            PC_CAP_SCREENSHOT, h_read_screen },   //BENCH: T9
  { PC_CMD_CAL_PROBE,    0,                            0, h_cal_probe },
  { PC_CMD_DEBUG_FPGA,   0,                            PC_CAP_DEBUG_RAW, h_debug_fpga },
  { PC_CMD_ENTER_FEL,    F_BASIC,                      PC_CAP_ENTER_FEL, h_enter_fel },
  { PC_CMD_FFT_BENCH,    F_BASIC,                      0, h_fft_bench },
};

static const pc_cmd_entry *pc_find(uint16_t cmd)
{
  uint32_t i;

  for(i=0;i<sizeof(pc_cmds) / sizeof(pc_cmds[0]);i++)
    if(pc_cmds[i].cmd == cmd)
      return &pc_cmds[i];

  return 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Response submission

static void pc_send(pc_session *s, uint16_t cmd, uint16_t seq, int status, uint32_t len)
{
  uint8_t *resp = s->tx + PC_HDR_SIZE;
  uint32_t n;

  if(status != PC_OK)
  {
    //Error payload: u16 detail + up to 64 ASCII bytes without terminator (P-4.2)
    uint32_t t = 0;

    pc_put_u16(resp, s->err_detail);

    if(s->err_text)
      for(t=0; s->err_text[t] && (t < 64); t++)
        resp[2 + t] = (uint8_t)s->err_text[t];

    len = 2 + t;
  }

  n = pc_frame_finish(s->tx, cmd, seq, (uint16_t)status, PC_FLAG_RESPONSE, len);

  //Keep small OK responses for duplicate requests. Errors are not cached: a retry runs again.
  if((status == PC_OK) && (len <= PC_CACHE_PAYLOAD))
  {
    memcpy(s->cache, s->tx, n);
    s->cache_len   = n;
    s->cache_cmd   = cmd;
    s->cache_seq   = seq;
    s->cache_valid = 1;
  }

  s->responses++;
  pc_hal_tx_submit(n);
}

static void pc_send_text(pc_session *s, const char *text)
{
  uint32_t n = 0;

  while(text[n] && n < s->tx_cap)
  {
    s->tx[n] = (uint8_t)text[n];
    n++;
  }

  pc_hal_tx_submit(n);
}

//----------------------------------------------------------------------------------------------------------------------------------
// ASCII probe (P-10)

static const char pc_hex[] = "0123456789abcdef";

#define PC_STR2(x) #x
#define PC_STR(x)  PC_STR2(x)

//"#RESET": bench recovery. Drops the frame in flight, a pending WAIT (no answer), a held READ_RAW, the duplicate cache
//and any partial input; settings and acquisition state stay as they are. Handled even while a response is being sent.
static void pc_link_reset(pc_session *s)
{
  if(pc_hal_tx_busy())
  {
    pc_hal_tx_abort();
    s->tx_aborts++;
  }

  pc_drop_deferred(s);
  pc_parser_reset(&s->parser);
  s->cache_valid = 0;
  s->pending_ev  = PC_EV_NONE;
  s->ui_hold     = 0;
  s->hold_active = 0;
}

//No libc on the scope (-nostdlib): no strcmp
static int pc_streq(const char *a, const char *b)
{
  while(*a && (*a == *b))
  {
    a++;
    b++;
  }

  return *a == *b;
}

static int pc_ascii(pc_session *s, int ev)
{
  if((ev == PC_EV_ASCII) && pc_streq(s->parser.line, "VER"))
  {
    char buf[80];
    const char *a = "#OK,FNIRSI-1013D-AI,fw=" PC_FW_VERSION ",proto=" PC_STR(PC_PROTO_MAJOR) "." PC_STR(PC_PROTO_MINOR) ",fpga=0x";
    uint32_t n = 0, i;

    while(*a) buf[n++] = *a++;
    for(i=0;i<4;i++) buf[n++] = pc_hex[(s->var.version >> (12 - 4 * i)) & 0xF];
    buf[n++] = '\n';
    buf[n]   = 0;

    pc_send_text(s, buf);
    return 1;
  }

  if((ev == PC_EV_ASCII) && pc_streq(s->parser.line, "RESET"))
  {
    pc_link_reset(s);
    pc_send_text(s, "#OK\n");
    return 1;
  }

  //1.2 escape hatch out of REMOTE; 1.3 has no ownership, the answer stays for old hosts
  if((ev == PC_EV_ASCII) && pc_streq(s->parser.line, "LOCAL"))
  {
    pc_send_text(s, "#OK\n");
    return 1;
  }

  pc_send_text(s, "#ERR,UNKNOWN\n");
  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// 1.23 (fw 0.2.78, docs/protocol.md section 14 row 63): a request for the main loop while the UI is modal. Instead of E_BUSY
// it asks the UI to yield (yield_req, read by every modal loop through pc_hal_scope.c) and stays queued; the modal loops
// leave by their cancel paths and the MAIN poll answers it. E_BUSY only while a step that must not be cancelled runs
// (pc_hal_ui_hold: a calibration measurement, a file save, USB drive mode), or when no main-loop pass came within
// PC_YIELD_MS. Session state and a memory read only: safe from the timer interrupt. Returns 0 (keep it queued) or the
// E_BUSY status.

static const char *pc_busy_text(uint8_t why)
{
  switch(why)
  {
    case PC_BUSY_CAL:       return "calibration measurement running";
    case PC_BUSY_SAVE:      return "file save in progress";
    case PC_BUSY_USB_DRIVE: return "USB drive mode";
    default:                return "UI did not return to the main screen";
  }
}

static int pc_ui_yield(pc_session *s)
{
  uint8_t  why = pc_hal_ui_hold();
  uint32_t now = pc_hal_ms();

  if(!why)
  {
    if(!s->yield_req)
    {
      s->yield_ms  = now;
      s->yield_req = 1;
      s->yields++;
      return 0;
    }

    if((now - s->yield_ms) < PC_YIELD_MS)
      return 0;

    why = PC_BUSY_NO_YIELD;
  }

  s->yield_req = 0;
  s->yield_refused++;
  return pc_error(s, PC_E_BUSY, why, pc_busy_text(why));
}

//fw 0.2.94: a read-only request (F_MODAL) while the UI is modal waits for the next yield check of a modal loop, which
//answers it with the screen left open. A hold reason gives E_BUSY with it at once, as for a yield (row 63); no yield
//check within PC_YIELD_MS (a UI pass without one: auto set, a slow capture) gives E_BUSY detail 2 as well
static int pc_ui_modal_wait(pc_session *s)
{
  uint8_t  why = pc_hal_ui_hold();
  uint32_t now = pc_hal_ms();

  if(!why)
  {
    if(!s->modal_req)
    {
      s->modal_ms  = now;
      s->modal_req = 1;
      return 0;
    }

    if((now - s->modal_ms) < PC_YIELD_MS)
      return 0;

    why = PC_BUSY_NO_YIELD;
  }

  s->modal_req = 0;
  s->yield_refused++;
  return pc_error(s, PC_E_BUSY, why, pc_busy_text(why));
}

int pc_ui_modal_pending(const pc_session *s)
{
  //Set from the timer interrupt: read it from memory every time
  return *(volatile const uint8_t *)&s->modal_req;
}

int pc_ui_yield_pending(const pc_session *s)
{
  //Set from the timer interrupt: read it from memory every time
  return *(volatile const uint8_t *)&s->yield_req && !pc_hal_ui_hold();
}

//----------------------------------------------------------------------------------------------------------------------------------
// Dispatch of one parser event. Returns 1 when handled, 0 when it must wait for the MAIN context.

static int pc_dispatch(pc_session *s, int ctx)
{
  const pc_header *h = &s->parser.hdr;
  const pc_cmd_entry *e;
  uint32_t rl = 0;
  int st;

  s->err_detail = 0;
  s->err_text   = 0;

  switch(s->pending_ev)
  {
    case PC_EV_ASCII:
    case PC_EV_ASCII_OVERFLOW:
      s->last_frame_ms = pc_hal_ms();
      return pc_ascii(s, s->pending_ev);

    case PC_EV_BAD_CRC:
    {
      uint32_t ts[PC_TSTAT_COUNT];

      //Keep the damaged request for GET_BAD_FRAME (the host diffs it against what it sent)
      pc_hal_link_stats(ts);
      memcpy(s->bad, s->parser.hdr_raw, PC_HDR_SIZE);
      memcpy(s->bad + PC_HDR_SIZE, s->parser.payload, h->len + PC_CRC_SIZE);
      s->bad_len      = PC_HDR_SIZE + h->len + PC_CRC_SIZE;
      s->bad_rx_bytes = ts[PC_TS_RX_BYTES];
      s->bad_count++;
    }
      pc_send(s, h->cmd, h->seq, pc_error(s, PC_E_BAD_CRC, 0, "payload crc"), 0);
      return 1;

    case PC_EV_BAD_LEN:
      pc_send(s, h->cmd, h->seq, pc_error(s, PC_E_BAD_LEN, 0, "frame > 512 bytes"), 0);
      return 1;

    case PC_EV_BAD_VERSION:
      pc_send(s, h->cmd, h->seq, pc_error(s, PC_E_VERSION, PC_PROTO_MAJOR, "proto_major"), 0);
      return 1;

    case PC_EV_FRAME:
      break;

    default:
      return 1;
  }

  //Link activity (MCP indicator)
  s->last_frame_ms = pc_hal_ms();
  s->frame_seen    = 1;

  //Retransmission of the deferred request still pending: ignore it, its answer follows
  if(s->def_active && (s->def_cmd == h->cmd) && (s->def_seq == h->seq))
    return 1;

  //Duplicate of the last completed request: answer from the cache without executing again (P-4.3)
  if(s->cache_valid && (s->cache_cmd == h->cmd) && (s->cache_seq == h->seq))
  {
    memcpy(s->tx, s->cache, s->cache_len);
    s->duplicates++;
    pc_hal_tx_submit(s->cache_len);
    return 1;
  }

  e = pc_find(h->cmd);

  if(e == 0)
  {
    st = pc_error(s, PC_E_UNKNOWN_CMD, 0, "cmd");
  }
  else if(s->var.mismatch && !(e->flags & F_BASIC))
  {
    //The scope does not run the bitstream this firmware is built for: nothing that would configure or read the
    //acquisition runs (the notice screen says what to do); the F_BASIC commands keep the link, the diagnostics and FEL
    st = pc_error(s, PC_E_UNSUPPORTED, PC_UNSUP_FPGA_MISMATCH, "FPGA bitstream mismatch: this firmware needs " PC_FW_FPGA_VERSION_TEXT);
  }
  else if((e->cap && !(s->var.caps & e->cap)) || (e->handler == 0))
  {
    st = pc_error(s, PC_E_UNSUPPORTED, 0, "capability");
  }
  //D4 logger: while logging only LOG_READ / LOG_STOP and the read-only sets run (the main loop is the log loop)
  else if((s->acq_state == PC_ACQ_LOGGING) && !(e->flags & (F_BUSY | F_MODAL)) && (e->cmd != PC_CMD_LOG_READ) &&
          (e->cmd != PC_CMD_LOG_STOP))
  {
    st = pc_error(s, PC_E_STATE, PC_STATE_LOGGING, "logging: LOG_STOP first");
  }
  else if((ctx != PC_CTX_MAIN) && !(e->flags & F_BUSY) && !((ctx == PC_CTX_MODAL) && (e->flags & F_MODAL)))
  {
    //The timer-tick and BUSY polls leave these for the main loop. While the UI is modal (no main-loop pass for
    //PC_MODAL_MS) they ask it to yield (1.23; before, a BUSY poll answered E_BUSY "UI busy"); fw 0.2.94: a read-only
    //one waits for the modal loop's yield check instead, which answers it with the screen open
    if(!s->ui_busy && (ctx != PC_CTX_MODAL))
      return 0;

    st = (e->flags & F_MODAL) ? pc_ui_modal_wait(s) : pc_ui_yield(s);

    if(st == 0)
      return 0;
  }
  else if(s->def_active && !(e->flags & F_CONTROL))
  {
    st = pc_error(s, PC_E_BUSY, 0, "deferred request pending");
  }
  else
  {
    //fw 0.2.96 (bench 2026-10-20 15:25): Diagnostics is a main-loop screen, so the yield above never reaches it, and a
    //state-changing request drew the view under it (the Spectrum's waterfall, the Frequency response's plot) into the
    //Diagnostics frame. Such a request leaves it by Back first, as it leaves a menu (row 63); the F_BUSY set and the
    //read-only F_MODAL set (READ_SCREEN, GET_*) are answered with it open
    if((ctx == PC_CTX_MAIN) && !(e->flags & (F_BUSY | F_MODAL)) && pc_hal_ui_leave_screen())
      s->yields++;

    //Zero extended copy of the request (missing trailing fields read as 0)
    s->ctx = (uint8_t)ctx;                                 //fw-d5: GET_GEN reads 0x39 in the main loop only
    memset(s->req, 0, sizeof(s->req));
    memcpy(s->req, s->parser.payload, h->len);

    st = e->handler(s, s->req, h->len, s->tx + PC_HDR_SIZE, &rl);

    //READ_RAW waiting for the UI capture in flight: stays queued, runs again at the next MAIN poll
    if(st == PC_HOLD)
      return 0;

    s->ui_hold     = 0;
    s->hold_active = 0;
    s->modal_req   = 0;

    if(st == PC_DEFERRED)
    {
      s->def_cmd = h->cmd;
      s->def_seq = h->seq;
      return 1;
    }
  }

  pc_send(s, h->cmd, h->seq, st, rl);
  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Host single capture polling (MAIN context)

//fw 0.2.62: every full-rate host capture is checked before READ_RAW can read it (enc_heal.h): 0x1538+ by FPGA read 0x33,
//0x1532..0x1537 by the B-against-A residual of the record's first words. A bad one is discarded and re-armed with the same
//setting, at most EH_MAX_TRIES times; then it is accepted (event 21 with bit 7, GET_HEALTH 90 bit 2). fw-fullrate
//(0x153D: the encodes at 200 MSa/s at every rate): the 0x33 check covers every capture, the residual divider-0 legacy
//records only (its core A neighbours are 5 ns away only there).
static uint8_t pc_enc_status(void *ctx)
{
  (void)ctx;
  return pc_hal_enc_status();
}

static void pc_enc_read(void *ctx, uint8_t core, uint32_t start, uint32_t count, uint8_t *dst, uint32_t stride)
{
  pc_session *s = (pc_session *)ctx;

  pc_hal_read_core(core, start, count, dst, stride, s->var.ring_words);
}

//1: the capture was discarded (re-armed, or ABORTED when the re-arm failed)
static int pc_enc_rearm(pc_session *s, uint32_t start)
{
  eh_io    io = { s, pc_enc_status, pc_enc_read };
  int      mode, bad, act;
  uint32_t value;
  eh_note  n;
  pc_hw_enc e;

  //The running bitstream's check (enc_heal_hw.c), by the data when a rate / filter write came after this capture's
  //sampling reset (eh_capture_mode, fw 0.2.72)
  pc_hal_enc_state(&e);
  mode = eh_capture_mode(e.mode, pc_hal_enc_written());

  if((mode == EH_MODE_NONE) || ((mode == EH_MODE_DATA) && ((s->cap_divider != 0) || s->cap_wide)))
    return 0;

  //Sequencer runs: the data check cannot read lane sums or segments (0x1536 / 0x1537), and a run STOP ended is final
  if(s->cap_next.seq_mode && ((mode == EH_MODE_DATA) || s->seq_stop))
    return 0;

  bad = eh_check(&io, mode, start, s->cap_total_words, &value);
  act = eh_cap_step(&s->enc_cap, bad, value, &n);
  pc_hal_enc_note((uint8_t)((mode == EH_MODE_FLAG) ? EH_WHY_FLAG : EH_WHY_DATA), &n);

  if(act != EH_REARM)
  {
    //fw 0.2.79: the outlier counters of the accepted capture (the self-check, cap_stage.h)
    pc_hal_cap_check();
    return 0;
  }

  if(pc_arm_hw(s, s->cap_pre_words, s->cap_total_words))
  {
    s->acq_state = PC_ACQ_ABORTED;
    return 1;
  }

  s->acq_state      = PC_ACQ_ARMED;
  s->fpga_done      = 0;
  s->fpga_triggered = 0;
  s->host_forced    = 0;
  s->arm_ms         = pc_hal_ms();
  return 1;
}

static void pc_acq_service(pc_session *s)
{
  int done, trig;

  pc_hw_ui u;

  if((s->acq_state != PC_ACQ_ARMED) && (s->acq_state != PC_ACQ_TRIGGERED))
    return;

  //RUN pressed on the screen (or anything else that let the UI arm): the host single is lost
  pc_hal_ui_read(&u);

  if(u.run)
  {
    pc_host_abort(s);
    return;
  }

  //Only 0x0A and 0x18: no touch scan, unlike fpga_done_conversion
  pc_hal_acq_poll(&done, &trig);

  //1.26: the trigger extension status (0x37, 0x34) while the capture is in flight, for GET_STATUS 48..51: 6 strobes
  if(!done)
    pc_hal_trig_status(s->trig_st);

  //Firmware auto mode: no trigger in time ends the capture now (0x1A = 0; the FPGA's sample time-out is long past).
  //0x153B latches the newest total words at a stop, so the forced record is contiguous
  if(s->fw_auto && !s->host_forced && !done && !trig && (s->acq_state == PC_ACQ_ARMED) &&
     ((pc_hal_ms() - s->arm_ms) >= s->auto_ms))
  {
    pc_hal_force();
    s->host_forced = 1;
    pc_hal_acq_poll(&done, &trig);
  }

  //A record the time-out ended is untriggered, whatever 0x18 says by now
  if(s->host_forced)
    trig = 0;

  //1.17: a sequencer run is triggered once it holds a triggered segment or record (0x18 is the current record's)
  if(s->cap_next.seq_mode && done)
  {
    uint8_t raw[8];
    pc_seq_status st;

    pc_hal_seq_status(raw);
    pc_seq_status_decode(raw, &st);
    s->cap_next.seq_done   = st.count;
    s->cap_next.seq_untrig = st.untriggered;

    if(s->seq_stop)
      s->cap_next.seq_flags |= 0x02;

    if(st.flags & PC_SEQ_ST_OVERFLOW)
      s->cap_next.seq_flags |= 0x04;

    trig = (s->cap_next.seq_mode == PC_SEQ_SEGMENTS) ? (st.count > st.untriggered) : (st.count != 0);
  }

  s->fpga_triggered = (uint8_t)trig;
  s->fpga_done      = (uint8_t)done;

  if(trig && (s->acq_state == PC_ACQ_ARMED))
    s->acq_state = PC_ACQ_TRIGGERED;

  if(done)
  {
    pc_hw_channel c1, c2;
    uint16_t addr  = pc_hal_acq_finish();                                            //BENCH: T5 (trigger_addr alignment)
    uint32_t start = pc_ring_mod((int64_t)addr - s->cap_pre_words, s->var.ring_words);
    uint32_t samples = (uint32_t)s->cap_total_words * (s->cap_wide ? 1 : 2);
    uint32_t pre_samples = (s->cap_wide ? (uint32_t)s->cap_pre_words : (uint32_t)s->cap_pre_words * 2) + s->cap_ext_shift;
    uint8_t  mask;

    if(pc_enc_rearm(s, start))
      return;

    //Into the trace buffers for the enabled channels, so the UI shows the capture (it redraws every pass). An averaging
    //run leaves its last record in the ring as a normal capture; a segment run has none (the screen keeps its traces)
    pc_hal_channel_read(0, &c1);
    pc_hal_channel_read(1, &c2);
    mask = (uint8_t)((c1.enable ? 0x01 : 0) | (c2.enable ? 0x02 : 0));

    if(s->cap_next.seq_mode == PC_SEQ_SEGMENTS)
      mask = 0;

    if(mask && (samples <= pc_hal_trace_capacity()))
      s->trace_sw_index = pc_hal_trace_build(start, samples, pre_samples, mask);

    s->auto_timeout_hit = !trig;                                                     //BENCH: T6
    s->acq_state        = PC_ACQ_DONE;
    pc_capture_finish(s, &s->cap_next, addr, trig);
    s->trace_capture_id = s->capture_id;
    s->trace_mask       = mask;
  }
}

static void pc_deferred_service(pc_session *s)
{
  if(!s->def_active)
    return;

  if(!s->def_ready)
  {
    if((s->acq_state != PC_ACQ_ARMED) && (s->acq_state != PC_ACQ_TRIGGERED))
      pc_complete_deferred(s, PC_OK);
    else if((int32_t)(pc_hal_ms() - s->def_deadline) >= 0)
      pc_complete_deferred(s, PC_OK);              //timeout: OK either way, the host checks acq_state
  }

  if(s->def_ready && !pc_hal_tx_busy())
  {
    uint32_t rl = 0;

    s->def_active = 0;
    s->def_ready  = 0;
    s->err_detail = 0;
    s->err_text   = (s->def_status == PC_E_ABORTED) ? "aborted" : 0;

    if(s->def_status == PC_OK)
      rl = pc_status_body(s, s->tx + PC_HDR_SIZE);

    pc_send(s, s->def_cmd, s->def_seq, s->def_status, rl);
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

static void pc_link_service(pc_session *s)
{
  uint32_t ev = pc_hal_link_take_events();

  //D4 logger: the host is gone; the next MAIN poll stops the log
  if((ev & (PC_LINK_RESET | PC_LINK_DTR_DOWN)) && (s->acq_state == PC_ACQ_LOGGING))
    s->log_link_lost = 1;

  if(ev & PC_LINK_RESET)
  {
    pc_parser_reset(&s->parser);
    s->pending_ev  = PC_EV_NONE;
    s->rx_len      = s->rx_pos = 0;
    s->cache_valid = 0;
    pc_drop_deferred(s);
  }

  if(ev & PC_LINK_DTR_DOWN)
  {
    s->cache_valid = 0;
    pc_drop_deferred(s);
  }

  //A new connection: drop partial input of the old one, but never a complete request that is waiting for the main
  //loop. The host sends right after opening the port, and the timer poll may parse that request before the DTR event
  //is taken (fw 0.1.1-0.2.10 dropped it unanswered: bench, first TEST_PATTERN after a FEL load timed out)
  if(ev & PC_LINK_DTR_UP)
  {
    if(s->pending_ev == PC_EV_NONE)
      pc_parser_reset(&s->parser);

    s->cache_valid = 0;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void pc_session_init(pc_session *s)
{
  memset(s, 0, sizeof(*s));
  pc_parser_init(&s->parser);
  s->tx = pc_hal_tx_buffer(&s->tx_cap);
  s->sd_stage = pc_hal_sd_stage(&s->sd_stage_cap);
  pc_variant_init(pc_hal_fpga_version(), &s->var);
  pc_variant_core_order(&s->var);
  s->acq_state    = PC_ACQ_IDLE;
  s->last_main_ms = pc_hal_ms();
  s->init_ms      = s->last_main_ms;
  s->settings_fp  = pc_hal_settings_fingerprint();
}

//----------------------------------------------------------------------------------------------------------------------------------

void pc_poll(pc_session *s, int ctx)
{
  uint32_t now;
  int ev;

  //Never nest (UI hooks and pc_ui_service hold in_poll too)
  if(s->in_poll)
  {
    if(ctx == PC_CTX_ISR)
      s->isr_skipped++;
    return;
  }

  s->in_poll = 1;
  now = pc_hal_ms();

  //Keep the TX pump moving even if an EP2 TX interrupt was missed, and never stay blocked on one response: a frame
  //that makes no progress for PC_TX_STALL_MS (host gone, port closed without DTR drop) is dropped
  if(pc_hal_tx_pump() && (pc_hal_tx_idle_ms() >= PC_TX_STALL_MS))
  {
    pc_hal_tx_abort();
    s->tx_aborts++;
  }

  //Link events in every context (session state only), before any new byte is parsed: the DTR edge of a new
  //connection must be seen before its first request
  pc_link_service(s);
  pc_log_service(s, ctx);                         //D4 logger

  if(ctx == PC_CTX_MAIN)
  {
    uint32_t pass = now - s->last_main_ms;

    //The first interval runs from pc_session_init through the rest of boot (logo, USB start): not a loop pass.
    //Both startup intervals are kept for GET_LINK_STATS (bench: first commands waited ~5 s after a FEL load).
    if(s->main_polls == 0)
    {
      s->first_poll_ms = pass;
      s->first_poll_at = now;
    }
    else if(s->main_polls == 1)
      s->first_pass_ms = pass;

    if(s->main_polls++)
    {
      s->pass_ms = (pass > 0xFFFF) ? 0xFFFF : (uint16_t)pass;
      if(s->pass_ms > s->pass_max_ms)
        s->pass_max_ms = s->pass_ms;
    }

    s->last_main_ms = now;
    s->ui_busy      = 0;
    s->yield_req    = 0;        //1.23: the UI is back at the base screen; the request waiting for it is answered below
    s->modal_req    = 0;

    pc_settings_track(s);

    //A held READ_RAW that was dropped (#RESET, link reset) must not keep the UI from arming
    if(s->pending_ev == PC_EV_NONE)
      s->ui_hold = s->hold_active = 0;

    //fw-roll: the roll's stream writes the capture ring while the UI rolls
    {
      pc_hw_ui u;

      pc_hal_ui_read(&u);

      if(u.long_mode && u.run)
        s->ring_valid = 0;
    }

    //EXT pin status for GET_STATUS (FPGA read, MAIN only; not on a bitstream this firmware does not know)
    if(!s->var.mismatch)
      s->ext_flags = pc_hal_ext_status() & 0x07;

    pc_acq_service(s);
    pc_deferred_service(s);
  }
  else if((now - s->last_main_ms) >= PC_MODAL_MS)
  {
    s->ui_busy = 1;
  }

  //One request in flight at a time (P-3.2 window): while the TX staging buffer is in use at most one further event is
  //parsed and held, so a "#RESET" line can still abort a stuck response
  for(;;)
  {
    if(s->pending_ev == PC_EV_NONE)
    {
      ev = pc_parser_service(&s->parser, now);

      while((ev == PC_EV_NONE) && (s->rx_pos < s->rx_len))
        ev = pc_parser_feed(&s->parser, s->rx_buf[s->rx_pos++], now);

      if(ev == PC_EV_NONE)
      {
        s->rx_len = pc_hal_rx_read(s->rx_buf, sizeof(s->rx_buf));
        s->rx_pos = 0;

        if(s->rx_len == 0)
          break;

        continue;
      }

      s->pending_ev = ev;
    }

    if((s->pending_ev == PC_EV_ASCII) && pc_streq(s->parser.line, "RESET"))
    {
      pc_link_reset(s);                            //frees the TX buffer; the "#OK" goes out through pc_dispatch
      s->pending_ev = PC_EV_ASCII;
    }

    if(pc_hal_tx_busy())
      break;

    if(!pc_dispatch(s, ctx))
    {
      s->held++;
      break;
    }

    if(ctx == PC_CTX_MAIN)      s->disp_main++;
    else if(ctx == PC_CTX_ISR)  s->disp_isr++;
    else                        s->disp_busy++;             //BUSY and (fw 0.2.94) MODAL

    s->pending_ev = PC_EV_NONE;

    //A deferred result may now be sendable behind a control response
    if(ctx == PC_CTX_MAIN)
      pc_deferred_service(s);
  }

  pc_parser_tick(&s->parser, now);
  s->in_poll = 0;

  //ENTER_FEL: the OK response has been handed to the controller; the HAL waits until it has left, then jumps
  if((ctx == PC_CTX_MAIN) && s->fel_pending && !pc_hal_tx_busy())
  {
    s->fel_pending = 0;
    pc_hal_enter_fel();
  }

  if((ctx == PC_CTX_MAIN) && s->restart_pending && !pc_hal_tx_busy())
  {
    s->restart_pending = 0;
    pc_hal_restart_test();
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

//Every main-loop pass after the MAIN poll: the MCP indicator in the top bar. Steady while DTR is up or a frame came
//within PC_HOST_ACTIVE_MS, redrawn every PC_CHIP_REDRAW_MS (UI redraws of the top bar paint over it).
void pc_ui_service(pc_session *s)
{
  uint32_t now = pc_hal_ms();
  uint8_t  st;

  if(s->in_poll)
    return;

  s->in_poll = 1;

  st = (pc_hal_dtr() || (s->frame_seen && ((now - s->last_frame_ms) < PC_HOST_ACTIVE_MS))) ? 1 : 0;

  if((st != s->chip_state) || (st && ((now - s->chip_ms) >= PC_CHIP_REDRAW_MS)))
  {
    pc_hal_indicator(st);
    s->chip_state = st;
    s->chip_ms    = now;
  }

  s->in_poll = 0;
}

int pc_ui_chip_shown(const pc_session *s)
{
  return s->chip_state != 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
