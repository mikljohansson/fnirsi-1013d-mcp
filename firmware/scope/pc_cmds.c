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

//----------------------------------------------------------------------------------------------------------------------------------
// Dispatch table

#define F_BUSY      0x02      //accepted from a BUSY or timer poll (no FPGA, no UI access)
#define F_CONTROL   0x04      //accepted while a deferred request is pending
#define F_BASIC     0x10      //works on an unknown FPGA variant

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

//phase_inc = round(f_mHz * 2^32 / 50e9). 50e9 = 2^10 * 5^11, so f * 2^22 / 5^11 stays inside 64 bits for f <= 25 MHz.
#define PC_5POW11 48828125ull

uint32_t pc_gen_phase_from_mhz(uint64_t freq_mhz)
{
  return (uint32_t)(((freq_mhz << 22) + PC_5POW11 / 2) / PC_5POW11);
}

//f_mHz = phase_inc * 50e9 / 2^32 = phase_inc * 5^11 / 2^22
uint64_t pc_gen_mhz_from_phase(uint32_t phase_inc)
{
  return (((uint64_t)phase_inc * PC_5POW11) + (1ull << 21)) >> 22;
}

uint32_t pc_gen_thr_from_bp(uint32_t duty_bp)
{
  return (uint32_t)(((uint64_t)duty_bp * 0xFFFFFFFFull + 5000) / 10000);
}

uint16_t pc_gen_bp_from_thr(uint32_t threshold)
{
  return (uint16_t)((((uint64_t)threshold * 10000) + 0x7FFFFFFFull) / 0xFFFFFFFFull);
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
// FPGA variants (P-5.1). Capabilities fail closed: only what the bitstream is known to implement.

void pc_variant_from_version(uint16_t version, pc_variant *v)
{
  uint32_t unverified = PC_CAP_READ_LAG_UNVERIFIED | PC_CAP_INTERLEAVE_UNVERIFIED;   //BENCH: T3, T4

  v->version  = version;
  v->read_lag = 0;                                                                   //BENCH: T3

  switch(version)
  {
    case 0x1432:
      v->variant    = 1;
      v->ring_words = 4096;
      v->max_total  = 3000;
      v->caps       = unverified;
      break;

    case 0x1532:
      v->variant    = 2;
      v->ring_words = 8192;
      v->max_total  = 16382;
      v->caps       = unverified | PC_CAP_PROG_TRIGGER_POS | PC_CAP_GENERATOR_DDS;
      break;

    case 0x1533:
    case 0x1534: case 0x1535: case 0x1536: case 0x1537: case 0x1538: case 0x1539:
    case 0x153A: case 0x153B: case 0x153C: case 0x153D: case 0x153E: case 0x153F:
      //AL3 family, later minors: 0x1533 adds the EXT trigger on the calibration pin (fpga/al3/README.md), 0x1534 the
      //decimation filter with 16-bit samples (cmds 0x53-0x55, read 0x29; pc_decim.h), 0x1535 the CIC compensation FIR
      //(cmd 0x56)
      v->variant    = 2;
      v->ring_words = 8192;
      v->max_total  = 16382;
      v->caps       = unverified | PC_CAP_PROG_TRIGGER_POS | PC_CAP_GENERATOR_DDS | PC_CAP_EXT_TRIGGER;   //BENCH: EXT

      if(version >= 0x1534)
        v->caps |= PC_CAP_DECIM;                                                     //BENCH: DECIM

      if(version >= 0x1535)
        v->caps |= PC_CAP_FIR;                                                       //BENCH: FIR
      break;

    case 0x1632:
      v->variant    = 3;
      v->ring_words = 6144;
      v->max_total  = 12286;
      v->caps       = unverified | PC_CAP_PROG_TRIGGER_POS | PC_CAP_GENERATOR_DDS;
      break;

    case 0x1732:
      //EP4: no ring wrap logic in the Verilog, captures above 6143 words may be corrupt [U]
      v->variant    = 4;
      v->ring_words = 6144;
      v->max_total  = 12286;
      v->caps       = unverified | PC_CAP_PROG_TRIGGER_POS | PC_CAP_GENERATOR_DDS;
      break;

    default:
      v->variant    = 0;
      v->ring_words = 4096;
      v->max_total  = 3000;
      v->caps       = 0;
      return;
  }

  //Reboot into the boot ROM's FEL mode: touches no FPGA, works on every known bitstream          BENCH: plan 11.3 step 1
  v->caps |= PC_CAP_ENTER_FEL;

  //Protocol 1.3: shared control with the UI, no LOCAL/REMOTE; SD sector access (no FPGA involved)
  v->caps |= PC_CAP_SHARED_MODE | PC_CAP_SD_SECTORS;

#if PC_DEBUG
  v->caps |= PC_CAP_DEBUG_RAW;

  //FORCE relies on the Atlan4 stop condition; enabled in bench builds so test T7 can run
  if(v->variant >= 2)
    v->caps |= PC_CAP_FORCE_TRIGGER;                                                 //BENCH: T7
#endif
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

//Decimation filter configuration an arm with the current settings and total samples writes (k = 0 without the cap)
static void pc_decim_next(pc_session *s, uint32_t total, pc_decim_cfg *c)
{
  pc_hw_timebase tb;
  uint8_t set = pc_hal_decim_setting();

  pc_hal_timebase_read(&tb);
  pc_decim_config(set & 3, set & (PC_DECIM_FLAGS | PC_DECIM_SET_FIR), s->tb_raw ? s->tb_divider : tb.divider, total,
                  (s->var.caps & PC_CAP_FIR) ? PC_DECIM_HAS_FIR : ((s->var.caps & PC_CAP_DECIM) ? PC_DECIM_HAS_CIC :
                  PC_DECIM_HAS_NONE), c);
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
  c->divider    = s->tb_raw ? s->tb_divider : tb.divider;
  c->source     = t.source;
  c->edge       = t.edge;
  c->level      = t.level;
  c->mode       = (t.ui_mode < 3) ? pc_mode_ui_to_proto[t.ui_mode] : 0;
  c->pretrigger = pretrigger;
  c->total      = total;
  pc_hal_channel_read(0, &c->ch[0]);
  pc_hal_channel_read(1, &c->ch[1]);

  //1.9: the decimation filter as written for this capture (the arm wrote it just before)
  if(s->var.caps & PC_CAP_DECIM)
  {
    pc_decim_cfg d;
    int written;

    pc_hal_decim_current(&d, &written);
    c->decim       = pc_decim_status(&d);
    c->decim_shift = d.shift;
    c->decim_n     = d.n;
    c->ext_shift   = (uint16_t)((c->source == 2) ? pc_decim_ext_shift(&d) : 0);

    if(c->ext_shift > pretrigger)
      c->ext_shift = (uint16_t)pretrigger;
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
  if(s->var.caps & PC_CAP_EXT_TRIGGER)
  {
    s->ext_flags = pc_hal_ext_status() & 0x07;

    if(s->ext_flags & 0x04)
      c->flags |= 0x20;
  }

  //Overload flags of this readout
  for(i=0;i<2;i++)
  {
    pc_hal_channel_read(i, &ch);
    c->ch[i].overload = ch.overload;

    if(ch.overload)
      c->flags |= (uint8_t)(0x04 << i);
  }

  c->trigger_addr  = trigger_addr;
  c->capture_start = (uint16_t)pc_ring_mod((int64_t)trigger_addr - ((int64_t)pc_cap_words(c, c->pretrigger) - c->ext_shift), s->var.ring_words);
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

  memset(p, 0, 44);
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
  p[43] = pc_hal_display_mode();                //1.7: 0 scope, 1 spectrum view; 2 / 3 waterfall / density; 1.13: 4 phosphor
  return 44;
}

//Correction for the channel's current settings (adc_cal.h): calibration data v3 when loaded and bound, with the v4
//tracking folded in exactly as the readout does it (acal_v4_apply; protocol row 53)
static void pc_channel_corr(const pc_hw_channel *c, acal_corr *k)
{
  acal_correction(c->cal2_loaded ? &c->cal2 : 0, c->cal_offset, c->range, c->divider, c->dcoffset, c->dc_shift_value,
                  c->adc_a_comp, c->adc_b_comp, k);
  acal_v4_apply(&c->trk, &c->cm, k);
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

static uint32_t pc_auto_timeout(pc_session *s, uint32_t total)
{
  if(s->auto_timeout_cfg)
    return s->auto_timeout_cfg;

  return pc_hal_auto_timeout_default(total);
}

static uint32_t pc_trigger_body(pc_session *s, uint8_t *p)
{
  pc_hw_trigger t;

  pc_hal_trigger_read(&t);

  memset(p, 0, 28);
  p[0] = t.source;
  p[1] = t.edge;
  p[2] = t.level;
  p[3] = (t.ui_mode < 3) ? pc_mode_ui_to_proto[t.ui_mode] : 0;
  pc_put_u32(p + 4,  t.pretrigger);
  pc_put_u32(p + 8,  t.total);
  pc_put_u32(p + 12, 0);
  pc_put_u32(p + 16, pc_auto_timeout(s, t.total));

  //Ring words: one sample per word in the 16-bit layout (1.9)
  {
    pc_decim_cfg d;

    pc_decim_next(s, t.total, &d);
    pc_put_u16(p + 20, d.k ? t.pretrigger : t.pretrigger / 2);
    pc_put_u16(p + 22, d.k ? t.total : t.total / 2);
  }
  return 28;
}

static uint32_t pc_gen_body(pc_session *s, uint8_t *p)
{
  uint8_t  en;
  uint32_t phase, thr;
  uint64_t mhz;
  uint16_t bp;

  if(!(s->var.caps & PC_CAP_GENERATOR_DDS))
  {
    //Stock bitstream: fixed 1 kHz square wave on the calibration pin, always driven [U duty]
    en = 1; phase = 0; thr = 0; mhz = 1000000; bp = 5000;
  }
  else
  {
    uint32_t hz;
    uint8_t  pct;

    pc_hal_gen_read_ui(&en, &hz, &pct);

    //Raw DDS words from SET_GEN stay in effect until the UI generator settings change
    if(s->gen_valid && ((en != s->gen_ui_en) || (hz != s->gen_ui_hz) || (pct != s->gen_ui_pct)))
      s->gen_valid = 0;

    if(s->gen_valid)
    {
      en = s->gen_enable; phase = s->gen_phase; thr = s->gen_thr;
    }
    else
    {
      //Same integer math as fpga_set_generator_freq / _duty
      phase = (uint32_t)(((uint64_t)hz << 32) / 50000000ull);
      thr   = (uint32_t)(((uint64_t)pct * 0xFFFFFFFFull) / 100);
    }

    mhz = pc_gen_mhz_from_phase(phase);
    bp  = pc_gen_bp_from_thr(thr);
  }

  memset(p, 0, 24);
  p[0] = en;
  pc_put_u32(p + 4, phase);
  pc_put_u32(p + 8, thr);
  pc_put_u64(p + 12, mhz);
  pc_put_u16(p + 20, bp);
  return 24;
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

  memset(resp, 0, 76);
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
  pc_put_u32(resp + 24, (s->var.caps & PC_CAP_GENERATOR_DDS) ? 50000000u : 0);
  pc_put_u16(resp + 28, s->var.max_total);
  //Stock geometry is fixed at 3000, so min = max there
  pc_put_u16(resp + 30, (s->var.caps & PC_CAP_PROG_TRIGGER_POS) ? 80 : s->var.max_total);
  resp[32] = s->var.read_lag;
  resp[33] = 7;
  resp[34] = 6;
  resp[35] = PC_INTERLEAVE_ORDER;

  for(i=0;(i<sizeof(fw) - 1) && (i<16);i++)
    resp[36 + i] = (uint8_t)fw[i];

  pc_put_u32(resp + 52, pc_fw_build);

  for(i=0;(i<sizeof(sn) - 1) && (i<16);i++)
    resp[56 + i] = (uint8_t)sn[i];

  pc_put_u16(resp + 72, 800);
  pc_put_u16(resp + 74, 480);
  *rl = 76;
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
//RXCOUNT, RXCOUNT > 512), fw 0.2.12: USB interrupts served by the timer poll (lost IRQs), fw 0.2.17: boot splash ms + 1, fw 0.2.19: restart record (8), fw 0.2.20: USB IRQ races, fw 0.2.48: fault status + address, fw 0.2.49: USB inferred resumes, soft reconnects
static int h_get_link_stats(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint32_t v[PC_TSTAT_COUNT + 26 + PC_USBEV_COUNT + PC_RXDBG_COUNT + PC_RESTART_COUNT + 1 + PC_FAULT_COUNT + PC_USBW_COUNT];
  const pc_parser_stats *ps = &s->parser.stats;
  uint32_t n = PC_TSTAT_COUNT, i;

  (void)req; (void)len;

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

  pc_put_u16(resp, (uint16_t)n);
  pc_put_u16(resp + 2, 0);

  for(i=0;i<n;i++)
    pc_put_u32(resp + 4 + 4 * i, v[i]);

  *rl = 4 + 4 * n;
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

  if(req[0] & ~0x01)
    return pc_error(s, PC_E_BAD_ARG, 0, "flags");

  n = pc_hal_event_log(resp + 12, 128, &total);
  pc_put_u16(resp, (uint16_t)n);
  pc_put_u16(resp + 2, 0);
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
  int      prog = (s->var.caps & PC_CAP_PROG_TRIGGER_POS) != 0;
  pc_hw_trigger t;
  uint8_t  hwmask = 0;

  (void)len;

  if(mask & ~0x00FF)
    return pc_error(s, PC_E_BAD_ARG, 0xFE, "mask");

  pc_hal_trigger_read(&t);

  //detail = bit number of the offending field (P-8.3)
  if(mask & 0x01)
  {
    if((req[2] == 2) && !(s->var.caps & PC_CAP_EXT_TRIGGER)) return pc_error(s, PC_E_UNSUPPORTED, 0, "EXT trigger");
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

  if((mask & 0x40) && hold)
    return pc_error(s, PC_E_UNSUPPORTED, 6, "holdoff");

  //Geometry checks on the resulting values
  if(mask & 0x30)
  {
    if(!prog && ((t.pretrigger != 1500) || (t.total != 3000)))
      return pc_error(s, PC_E_UNSUPPORTED, (mask & 0x10) ? 4 : 5, "fixed 1500/3000");

    if(t.total & 1)                                    return pc_error(s, PC_E_BAD_ARG, 5, "total odd");
    if((t.total < 80) || (t.total > s->var.max_total)) return pc_error(s, PC_E_BAD_ARG, 5, "total out of range");
    if(t.pretrigger & 1)                               return pc_error(s, PC_E_BAD_ARG, 4, "pretrigger odd");
    if(t.pretrigger + 2 > t.total)                     return pc_error(s, PC_E_BAD_ARG, 4, "pretrigger > total - 2");

    //1.9: the 16-bit layout holds one sample per word
    if((s->var.caps & PC_CAP_DECIM) && (pc_hal_decim_setting() & 3) && (t.total > PC_DECIM_MAX_TOTAL))
      return pc_error(s, PC_E_BAD_ARG, 5, "total > 8190 with the filter on");
  }

  pc_hal_trigger_write(&t, hwmask);

  if(mask & 0x80)
    s->auto_timeout_cfg = ato;

  *rl = pc_trigger_body(s, resp);
  return PC_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Acquisition handlers

//ARM (1.3): host single capture. The UI acquisition stops (the screen shows STOP), the host's own ARM sequence runs
//as in 1.2, and at DONE the capture is built into the trace buffers, which the UI then shows.
static int h_arm(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  pc_hw_trigger t;
  pc_hw_ui u;
  pc_decim_cfg dc;
  uint16_t pre_w, tot_w;
  int prog = (s->var.caps & PC_CAP_PROG_TRIGGER_POS) != 0;

  (void)req; (void)len;

  if((s->acq_state == PC_ACQ_ARMED) || (s->acq_state == PC_ACQ_TRIGGERED))
    return pc_error(s, PC_E_STATE, 0, "already armed");

  pc_hal_ui_read(&u);

  if(u.long_mode)
    return pc_error(s, PC_E_STATE, 2, "UI long time base");

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

  pc_hal_trigger_read(&t);

  if(!prog)
  {
    //Stock: fixed 750 / 1500 word geometry
    t.pretrigger = 1500;
    t.total      = 3000;
  }

  //1.9: decimation filter for this capture; the 16-bit layout has one sample per ring word
  pc_decim_next(s, t.total, &dc);
  s->cap_wide = dc.k != 0;

  //EXT trigger: the stored data lag the edge (pc_decim_ext_shift), so fewer pretrigger words put the edge back at
  //sample `pretrigger` of the capture
  s->cap_ext_shift = (uint16_t)((t.source == 2) ? pc_decim_ext_shift(&dc) : 0);
  if(s->cap_ext_shift > t.pretrigger)
    s->cap_ext_shift = t.pretrigger;

  pre_w = s->cap_wide ? (uint16_t)(t.pretrigger - s->cap_ext_shift) : t.pretrigger / 2;
  tot_w = s->cap_wide ? t.total : t.total / 2;

  //2.-5. conversion start with a bounded ready wait                                  BENCH: T6 (stock 0x0F semantics)
  s->ring_valid = 0;

  if(pc_hal_arm(pre_w, tot_w, pc_auto_timeout(s, t.total), prog, (t.ui_mode != 0) || prog,
                (s->var.caps & PC_CAP_DECIM) ? &dc : NULL))
  {
    s->acq_state = PC_ACQ_ABORTED;
    return pc_error(s, PC_E_FPGA, 0, "0x05 not ready");
  }

  pc_capture_latch(s, &s->cap_next, PC_ORIGIN_HOST, t.pretrigger, t.total);
  s->ui_started = 0;

  s->acq_state        = PC_ACQ_ARMED;
  s->fpga_done        = 0;
  s->fpga_triggered   = 0;
  s->auto_timeout_hit = 0;
  s->armed_once       = 1;
  s->arm_ms           = pc_hal_ms();
  s->cap_pre_words    = pre_w;
  s->cap_total_words  = tot_w;
  s->cap_divider      = s->cap_next.divider;

  //Auto mode in firmware on Atlan bitstreams (armed normal above): an explicit host time-out (samples) counts too
  s->fw_auto     = (uint8_t)(prog && (t.ui_mode == 0));
  s->host_forced = 0;
  s->auto_ms     = pc_auto_deadline_ms(t.total, s->cap_divider);
  if(s->auto_timeout_cfg)
  {
    uint32_t cfg_ms = pc_auto_deadline_ms(s->auto_timeout_cfg, s->cap_divider) / 2;

    if(cfg_ms > s->auto_ms)
      s->auto_ms = cfg_ms;
  }

  *rl = pc_status_body(s, resp);
  return PC_OK;
}

static int h_force(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)req; (void)len;

  if(s->acq_state != PC_ACQ_ARMED)
    return pc_error(s, PC_E_STATE, 0, "not armed");

  pc_hal_force();                                                                    //BENCH: T7
  *rl = pc_status_body(s, resp);
  return PC_OK;
}

//STOP (1.3): aborts a host single and stops the UI acquisition (run_state STOP)
static int h_stop(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  pc_hw_ui u;

  (void)req; (void)len;

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

  memset(resp, 0, 76);
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

  *rl = 76;
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

static int h_read_raw(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint8_t  mask   = req[0];
  uint8_t  mode   = req[1];
  uint8_t  layout = req[2];
  uint8_t  flags  = req[3];
  int32_t  addr   = (int32_t)pc_get_u32(req + 4);
  uint32_t count  = pc_get_u32(req + 8);
  uint32_t ring   = s->var.ring_words;
  uint32_t ncores = 0, i, start, total;
  int64_t  base;
  uint8_t *d;

  (void)len;

  if((mask == 0) || (mask & 0xF0)) return pc_error(s, PC_E_BAD_ARG, 0, "core_mask");
  if(mode > 2)                     return pc_error(s, PC_E_BAD_ARG, 1, "addr_mode");
  if(layout > 2)                   return pc_error(s, PC_E_BAD_ARG, 2, "layout");
  if(flags & ~0x01)                return pc_error(s, PC_E_BAD_ARG, 3, "flags");
  if((count == 0) || (count > ring)) return pc_error(s, PC_E_BAD_ARG, 8, "count 1..ring_words");

  if((layout >= 1) && ((((mask & 0x03) != 0) && ((mask & 0x03) != 0x03)) || (((mask & 0x0C) != 0) && ((mask & 0x0C) != 0x0C))))
    return pc_error(s, PC_E_BAD_ARG, 2, "layout 1/2 needs both cores");

  //1.3: the latest finished capture of any origin, while the ring still holds it. flags bit 0 (ANY) reads the ring as
  //it is. In RUN the UI has usually armed the next capture already: wait for it (at most PC_HOLD_MS) and keep the UI
  //from re-arming until this request is answered.
  if(!(flags & 0x01))
  {
    pc_hw_ui u;

    pc_hal_ui_read(&u);

    if(u.long_mode)
      return pc_error(s, PC_E_STATE, 2, "UI long time base");

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

      return pc_error(s, PC_E_BUSY, 1, "UI capture running");
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

  //Modular start (P-7.5): never the 1.00o5 "T + 7441" shortcut
  base  = (mode == 0) ? 0 : ((mode == 1) ? s->capture_start : s->trigger_addr);
  start = pc_ring_mod(base + addr, ring);

  pc_put_u32(resp, s->capture_id);
  pc_put_u16(resp + 4, (uint16_t)start);
  pc_put_u16(resp + 6, (uint16_t)ring);
  pc_put_u32(resp + 8, count);
  resp[12] = mask;
  resp[13] = layout;
  resp[14] = s->acq_state;
  resp[15] = s->var.read_lag;

  d = resp + 16;

  //BENCH: T3 (read_lag, stale first byte), T4 (interleave order)
  if(layout == 0)
  {
    for(i=0;i<4;i++)
    {
      if(mask & (1 << i))
      {
        pc_hal_read_core((uint8_t)i, start, count, d, 1, ring, s->var.read_lag);
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
        pc_hal_read_core((uint8_t)(2 * i),     start, count, d,     2, ring, s->var.read_lag);
        pc_hal_read_core((uint8_t)(2 * i + 1), start, count, d + 1, 2, ring, s->var.read_lag);
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
        //core A = 2i (0x20/0x22), core B = 2i + 1 (0x21/0x23)
        pc_hal_read_core((uint8_t)(2 * i + 1), start, count, d + (PC_INTERLEAVE_ORDER ? 1 : 0), 2, ring, s->var.read_lag);
        pc_hal_read_core((uint8_t)(2 * i),     start, count, d + (PC_INTERLEAVE_ORDER ? 0 : 1), 2, ring, s->var.read_lag);
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
    pc_hw_timebase tb;

    pc_hal_timebase_read(&tb);
    origin  = PC_ORIGIN_UI_RUN;
    divider = tb.divider;
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
  pc_hw_timebase tb;
  pc_decim_cfg next, cur;
  uint8_t  set = pc_hal_decim_setting();
  uint16_t rb  = pc_hal_decim_readback();
  int      written;

  pc_hal_trigger_read(&t);
  pc_hal_timebase_read(&tb);
  pc_decim_next(s, t.total, &next);
  pc_hal_decim_current(&cur, &written);

  memset(p, 0, 32);
  p[0] = set & 3;                                       //UI Filter: 0 off, 1 boxcar, 2 CIC2, 3 CIC3
  p[1] = (uint8_t)((set & (PC_DECIM_FLAGS | PC_DECIM_SET_FIR)) >> 2);   //flags: bit 0 core-order swap, bits 1 / 2
                                                        //falling-edge A / B, bit 3 FIR (1.11)
  p[2] = pc_decim_status(&next);                        //what the next arm writes (0x29 byte 0 layout)
  p[3] = next.shift;
  pc_put_u32(p + 4, next.n);
  pc_put_u64(p + 8, next.gain);
  pc_put_u32(p + 16, s->tb_raw ? s->tb_divider : tb.divider);
  pc_put_u32(p + 20, pc_decim_rate(&next, s->tb_raw ? s->tb_divider : tb.divider));
  p[24] = written ? pc_decim_status(&cur) : 0xFF;       //written at the last arm (0xFF: not yet)
  p[25] = cur.shift;
  p[26] = (uint8_t)(rb >> 8);                           //FPGA 0x29 now
  p[27] = (uint8_t)rb;
  pc_put_u16(p + 28, PC_DECIM_MAX_TOTAL);
  p[30] = next.why;                                     //why the effective K is lower than requested
  p[31] = (uint8_t)((t.source == 2) ? pc_decim_ext_shift(&next) : 0);   //EXT latency shift the next arm applies
  return 32;
}

static int h_get_decim(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)req; (void)len;
  *rl = pc_decim_body(s, resp);
  return PC_OK;
}

//SET_DECIM: u8 filter (0 off, 1 boxcar, 2 CIC2, 3 CIC3), u8 flags (bit 0 core-order swap, bit 1 / bit 2 capture the
//A / B cores on the falling write-clock edge, bit 3 CIC compensation FIR, 1.11, FPGA 0x1535+ only), u16 0. Sets the UI's
//Filter setting (shared control); the next arm (UI or host) writes it.
static int h_set_decim(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  pc_hw_trigger t;

  (void)len;

  if(req[0] > 3)     return pc_error(s, PC_E_BAD_ARG, 0, "filter 0..3");
  if(req[1] & ~0x0F) return pc_error(s, PC_E_BAD_ARG, 1, "flags");

  if((req[1] & 0x08) && !(s->var.caps & PC_CAP_FIR))
    return pc_error(s, PC_E_BAD_ARG, 1, "FIR needs FPGA 0x1535+");

  pc_hal_trigger_read(&t);

  if(req[0] && (t.total > PC_DECIM_MAX_TOTAL))
    return pc_error(s, PC_E_BAD_ARG, 0, "total > 8190: SET_TRIGGER total first");

  pc_hal_decim_set((uint8_t)(req[0] | ((req[1] & 0x0F) << 2)));
  pc_settings_track(s);

  *rl = pc_decim_body(s, resp);
  return PC_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Generator, calibration, debug

static int h_get_gen(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  (void)req; (void)len;
  *rl = pc_gen_body(s, resp);
  return PC_OK;
}

static int h_set_gen(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  uint8_t  mask  = req[0];
  uint64_t freq  = pc_get_u64(req + 4);
  uint32_t duty  = pc_get_u32(req + 12);
  uint8_t  cur[24];

  (void)len;

  if(mask & ~0x07)  return pc_error(s, PC_E_BAD_ARG, 0xFE, "mask");
  if(req[2] > 1)    return pc_error(s, PC_E_BAD_ARG, 1, "freq_mode");
  if(req[3] > 1)    return pc_error(s, PC_E_BAD_ARG, 2, "duty_mode");

  //Start from what is in effect now
  pc_gen_body(s, cur);
  s->gen_enable = cur[0];
  s->gen_phase  = pc_get_u32(cur + 4);
  s->gen_thr    = pc_get_u32(cur + 8);

  if(mask & 0x01)
  {
    if(req[1] > 1) return pc_error(s, PC_E_BAD_ARG, 0, "enable");
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
      if(freq > 25000000000ull) return pc_error(s, PC_E_BAD_ARG, 1, "freq > 25 MHz");
    }
    else if(freq > 0x80000000ull)
    {
      return pc_error(s, PC_E_BAD_ARG, 1, "phase_inc > 2^31");
    }
  }

  if((mask & 0x04) && (req[3] == 0) && (duty > 10000))
    return pc_error(s, PC_E_BAD_ARG, 2, "duty > 10000");

  if(mask & 0x01) s->gen_enable = req[1];
  if(mask & 0x02) s->gen_phase  = (req[2] == 0) ? pc_gen_phase_from_mhz(freq) : (uint32_t)freq;
  if(mask & 0x04) s->gen_thr    = (req[3] == 0) ? pc_gen_thr_from_bp(duty) : duty;

  s->gen_valid = 1;
  pc_hal_gen_write(s->gen_phase, s->gen_thr, s->gen_enable);
  pc_hal_gen_read_ui(&s->gen_ui_en, &s->gen_ui_hz, &s->gen_ui_pct);

  *rl = pc_gen_body(s, resp);
  return PC_OK;
}

#define PC_CAL_RESP_LEN 288     //72 bytes up to 1.9, 248 in 1.10-1.11 (v2), 256 in 1.12-1.13 (v3), v4 tracking from 1.14

static int h_get_cal(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  pc_hw_channel c;
  acal_corr k;
  uint32_t i;
  int bound;

  (void)s; (void)len;

  if(req[0] > 1)
    return pc_error(s, PC_E_BAD_ARG, 0, "channel");

  pc_hal_channel_read(req[0], &c);

  memset(resp, 0, 72);
  resp[0] = req[0];

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
  resp[73] = (uint8_t)((c.cal2_loaded ? 1 : 0) | (bound ? 2 : 0) | ((k.cls == ACAL_GATED) ? 4 : 0) |
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

  if(c.cal2_loaded)
    acal_pack_channel(&c.cal2, resp + 112);

  //1.14: calibration v4 tracking (row 53). Bytes 84 and 92..107 above already include it
  {
    pc_decim_cfg dc;
    int          written;
    uint32_t     now = pc_hal_ms();
    uint8_t      f   = (uint8_t)(c.trk.flags & (ACAL_TRK_DATA | ACAL_TRK_GAIN | ACAL_TRK_BOW | ACAL_TRK_LAST));

    pc_hal_decim_current(&dc, &written);

    if((k.version == 3) && (k.cls == ACAL_FULL) && (!written || !dc.k))
      f |= 0x10;
    if(c.cm.flags & ACAL_CM_DATA)
      f |= 0x20;
    if(c.cm.flags & ACAL_CM_LAST)
      f |= 0x40;

    resp[256] = f;
    pc_put_u16(resp + 258, (uint16_t)c.trk.d_q8);
    pc_put_u32(resp + 260, (uint32_t)c.trk.e_q24);
    pc_put_u32(resp + 264, (uint32_t)c.trk.q_q24);
    pc_put_u32(resp + 268, (c.trk.flags & ACAL_TRK_DATA) ? now - c.trk.last_ms : 0xFFFFFFFFu);
    pc_put_u32(resp + 272, c.trk.records);
    pc_put_u16(resp + 276, c.trk.last_pairs);
    pc_put_u16(resp + 278, c.trk.spread16);
    pc_put_u16(resp + 280, (uint16_t)c.cm.cm_q8);
    pc_put_u16(resp + 282, c.cm.probes);
    pc_put_u32(resp + 284, (c.cm.flags & ACAL_CM_DATA) ? now - c.cm.last_ms : 0xFFFFFFFFu);
  }

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
    if((length < 512) || (length % 512) || ((length / 512) > PC_SD_VERIFY_MAX) || (sector + length / 512 > card))
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
static int h_sd_info(pc_session *s, const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rl)
{
  static const uint32_t images[3] = { 16, 80, 750 };
  uint8_t *scratch = resp + 64;                        //64 KiB inside the TX staging buffer, after the answer
  uint32_t card = pc_hal_sd_sectors(), part, i;

  (void)len;

  if(req[0] & ~0x01)
    return pc_error(s, PC_E_BAD_ARG, 0, "flags");

  if(card == 0)
    return pc_error(s, PC_E_IO, 0, "no card");

  part = pc_sd_part_start(scratch);

  memset(resp, 0, 64);
  pc_put_u32(resp,      card);
  pc_put_u32(resp + 4,  part);
  pc_put_u32(resp + 8,  pc_sd_write_end(part));
  pc_put_u32(resp + 12, s->sd_stage_cap);

  for(i=0;i<3;i++)
    pc_sd_egon(images[i], req[0] & 0x01, card, scratch, resp + 16 + 16 * i);

  *rl = 64;
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
  uint32_t end, last;
  int      r;

  (void)len;

  if(pc_get_u32(req + 12) != PC_SD_WRITE_MAGIC)        return pc_error(s, PC_E_BAD_ARG, 12, "magic");
  if(flags & ~(PC_SD_WF_CONFIG | PC_SD_WF_BOOT))       return pc_error(s, PC_E_BAD_ARG, 6, "flags");
  if((count == 0) || (count > PC_SD_MAX_COUNT) || (count * 512 > s->sd_stage_cap))
                                                       return pc_error(s, PC_E_BAD_ARG, 4, "count 1..128");
  if(card == 0)                                        return pc_error(s, PC_E_IO, 0, "no card");

  end  = pc_sd_write_end(pc_sd_part_start(back));
  last = sector + count - 1;

  if(end == 0)                                         return pc_error(s, PC_E_STATE, 0, "no partition table: writes off");
  if((sector < PC_SD_FIRST) || (last >= end) || (last >= card) || (last < sector))
                                                       return pc_error(s, PC_E_BAD_ARG, 0, "outside the write window");
  if((sector < PC_SD_BOOT_END) && !(flags & PC_SD_WF_BOOT))
                                                       return pc_error(s, PC_E_BAD_ARG, 0, "16..79 bootloader: flag BOOT");
  if((sector <= PC_SD_CFG_LAST) && (last >= PC_SD_CFG_FIRST) && !(flags & PC_SD_WF_CONFIG))
                                                       return pc_error(s, PC_E_BAD_ARG, 0, "708..710 config: flag CONFIG");
  if(pc_crc32(s->sd_stage, count * 512) != crc)        return pc_error(s, PC_E_BAD_ARG, 8, "stage crc");

  r = pc_hal_sd_write(sector, count, s->sd_stage);

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
  { PC_CMD_GET_CHANNEL,  0,                            0, h_get_channel },
  { PC_CMD_SET_CHANNEL,  0,                            0, h_set_channel },
  { PC_CMD_GET_TIMEBASE, 0,                            0, h_get_timebase },
  { PC_CMD_SET_TIMEBASE, 0,                            0, h_set_timebase },
  { PC_CMD_GET_TRIGGER,  0,                            0, h_get_trigger },
  { PC_CMD_SET_TRIGGER,  0,                            0, h_set_trigger },
  { PC_CMD_ARM,          0,                            0, h_arm },
  { PC_CMD_FORCE,        0,                            PC_CAP_FORCE_TRIGGER, h_force },
  { PC_CMD_STOP,         F_CONTROL,                    0, h_stop },
  { PC_CMD_WAIT,         0,                            0, h_wait },
  { PC_CMD_READ_RAW,     0,                            0, h_read_raw },
  { PC_CMD_READ_TRACE,   0,                            0, h_read_trace },
  { PC_CMD_READ_LIVE,    0,                            PC_CAP_LIVE_READ, NULL },
  { PC_CMD_ACQUIRE_AVG,  0,                            PC_CAP_DEVICE_AVERAGE, NULL },
  { PC_CMD_SET_ACQ_STATE, F_CONTROL,                   0, h_set_acq_state },
  { PC_CMD_GET_CAPTURE_INFO, F_BUSY | F_CONTROL,       0, h_get_capture_info },
  { PC_CMD_READ_SPECTRUM, F_BASIC,                     0, h_read_spectrum },
  { PC_CMD_GET_DECIM,    0,                            PC_CAP_DECIM, h_get_decim },
  { PC_CMD_SET_DECIM,    0,                            PC_CAP_DECIM, h_set_decim },
  { PC_CMD_SD_INFO,      F_BASIC,                      PC_CAP_SD_SECTORS, h_sd_info },
  { PC_CMD_SD_READ_SECTORS, F_BASIC,                   PC_CAP_SD_SECTORS, h_sd_read },
  { PC_CMD_SD_STAGE,     F_BUSY | F_BASIC,             PC_CAP_SD_SECTORS, h_sd_stage },   //memory copy only: timer poll
  { PC_CMD_SD_WRITE_SECTORS, F_BASIC,                  PC_CAP_SD_SECTORS, h_sd_write },
  { PC_CMD_GET_GEN,      0,                            0, h_get_gen },
  { PC_CMD_SET_GEN,      0,                            PC_CAP_GENERATOR_DDS, h_set_gen },
  { PC_CMD_GET_CAL,      0,                            0, h_get_cal },
  { PC_CMD_READ_SCREEN,  0,                            PC_CAP_SCREENSHOT, NULL },   //BENCH: T9 before it gets a handler
  { PC_CMD_DEBUG_FPGA,   0,                            PC_CAP_DEBUG_RAW, h_debug_fpga },
  { PC_CMD_ENTER_FEL,    0,                            PC_CAP_ENTER_FEL, h_enter_fel },
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
  else if((s->var.variant == 0) && !(e->flags & F_BASIC))
  {
    st = pc_error(s, PC_E_UNSUPPORTED, 0, "unknown FPGA");
  }
  else if((e->cap && !(s->var.caps & e->cap)) || (e->handler == 0))
  {
    st = pc_error(s, PC_E_UNSUPPORTED, 0, "capability");
  }
  else if((ctx != PC_CTX_MAIN) && !(e->flags & F_BUSY))
  {
    //The timer-tick poll leaves these for the main loop. A BUSY poll does the same unless the UI is modal (no main-loop
    //pass for PC_MODAL_MS), then it answers E_BUSY.
    if((ctx == PC_CTX_ISR) || !s->ui_busy)
      return 0;

    st = pc_error(s, PC_E_BUSY, 0, "UI busy");
  }
  else if(s->def_active && !(e->flags & F_CONTROL))
  {
    st = pc_error(s, PC_E_BUSY, 0, "deferred request pending");
  }
  else
  {
    //Zero extended copy of the request (missing trailing fields read as 0)
    memset(s->req, 0, sizeof(s->req));
    memcpy(s->req, s->parser.payload, h->len);

    st = e->handler(s, s->req, h->len, s->tx + PC_HDR_SIZE, &rl);

    //READ_RAW waiting for the UI capture in flight: stays queued, runs again at the next MAIN poll
    if(st == PC_HOLD)
      return 0;

    s->ui_hold     = 0;
    s->hold_active = 0;

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

  //Firmware auto mode: no trigger in time ends the capture now (0x1A = 0; the FPGA's sample time-out is long past)
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
    uint32_t pre_samples = s->cap_wide ? (uint32_t)s->cap_pre_words + s->cap_ext_shift : (uint32_t)s->cap_pre_words * 2;
    uint8_t  mask;

    //Into the trace buffers for the enabled channels, so the UI shows the capture (it redraws every pass)
    pc_hal_channel_read(0, &c1);
    pc_hal_channel_read(1, &c2);
    mask = (uint8_t)((c1.enable ? 0x01 : 0) | (c2.enable ? 0x02 : 0));

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
  pc_variant_from_version(pc_hal_fpga_version(), &s->var);
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

    pc_settings_track(s);

    //A held READ_RAW that was dropped (#RESET, link reset) must not keep the UI from arming
    if(s->pending_ev == PC_EV_NONE)
      s->ui_hold = s->hold_active = 0;

    //The long time base acquisition does not use the capture ring
    {
      pc_hw_ui u;

      pc_hal_ui_read(&u);

      if(u.long_mode)
        s->ring_valid = 0;
    }

    //EXT pin status for GET_STATUS (FPGA read, MAIN only)
    if(s->var.caps & PC_CAP_EXT_TRIGGER)
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
    else if(ctx == PC_CTX_BUSY) s->disp_busy++;
    else                        s->disp_isr++;

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
