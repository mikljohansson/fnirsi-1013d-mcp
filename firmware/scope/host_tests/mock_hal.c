//----------------------------------------------------------------------------------------------------------------------------------
// mock_hal.c - host implementation of pc_hal.h: a small model of the scope, its FPGA ring and the USB transport.
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "mock_hal.h"
#include "pc_cmds.h"
#include "cap_stage.h"

mock_state mock;
mock_resp  mock_resps[MOCK_MAX_RESP];
uint32_t   mock_nresps;
const pc_image_id pc_image_ident = { PC_ID_MAGIC0, PC_ID_MAGIC1, "0.0.0-host", 0, { 26, 10, 19, 12, 0, 0 }, 1, 16 };   //build_id.c on the target

static uint8_t mock_tx[PC_MAX_RESPONSE_FRAME + 16] __attribute__((aligned(4)));
static uint8_t mock_sd[MOCK_SD_SECTORS * 512] __attribute__((aligned(4)));
static uint8_t mock_stage[65536] __attribute__((aligned(4)));

//1.00o5 tables (variables.c): time_per_div_sample_rate[35] and sample_rate_settings[0..17]
static const uint8_t tpd_sr[35] = { 28,27,26,25,24,23,22,21,20,19,18, 17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,0,0,0,0,0,0 };
static const uint32_t sr_div[29] = { 0,1,3,9,19,39,99,199,399,999,1999,3999,9999,19999,39999,99999,199999,399999,
                                     399999,399999,399999,399999,399999,399999,399999,399999,399999,399999,399999 };

//A triangle (one code per word, adjacent words always differ) per core and arm: smooth enough for the full-rate data
//check (enc_heal.h), which reads clean unless the arm is marked garbled (enc_bad_arms)
static uint8_t ring_value(int core, uint32_t addr, uint32_t arms)
{
  uint32_t t = addr % 400;

  return (uint8_t)(((t < 200) ? t : 400 - t) + (uint32_t)core * 12 + (arms * 7) % 16);
}

static void fill_ring(void)
{
  uint32_t c, a, x = 12345 + mock.arms;

  for(c=0;c<4;c++)
    for(a=0;a<MOCK_RING_MAX;a++)
      mock.ring[c][a] = ring_value((int)c, a, mock.arms);

  //A garbled capture (enc_heal.h): CH2 core A metastable, as on 0x1537
  if(mock.enc_cur_bad)
    for(a=0;a<MOCK_RING_MAX;a++)
    {
      x = x * 1664525u + 1013904223u;
      mock.ring[2][a] = (uint8_t)(x >> 24);
    }
}

void mock_clear_resps(void)
{
  uint32_t i;

  for(i=0;i<mock_nresps;i++)
    free(mock_resps[i].data);

  mock_nresps = 0;
}

void mock_reset(uint16_t fpga_version)
{
  int i;

  mock_clear_resps();
  memset(&mock, 0, sizeof(mock));
  mock.now        = 5000;
  mock.version    = fpga_version;
  mock.ring_words = ((fpga_version & 0xFFF0) == 0x1530) ? 8192 : ((fpga_version == 0x1632 || fpga_version == 0x1732) ? 6144 : 4096);
  mock.polls_to_trigger = 2;
  mock.polls_to_done    = 3;
  mock.trigger_addr     = 100;
  mock.dtr              = 1;

  for(i=0;i<2;i++)
  {
    int r;
    mock.ch[i].enable   = 1;
    mock.ch[i].range    = 5;
    mock.ch[i].probe    = 1;
    mock.ch[i].dcoffset = 0;
    for(r=0;r<7;r++)
    {
      mock.ch[i].cal_offset[r] = (uint16_t)(1000 + 10 * r);
      mock.ch[i].input_cal[r]  = (r == 6) ? 7600000 : 3800000;
    }
    mock.ch[i].adc_a_comp     = 2;
    mock.ch[i].adc_b_comp     = -1;
    mock.ch[i].dc_shift_value = 385;
    mock.ch[i].dc_shift_size  = 220;
  }

  mock.tb.timeperdiv = 24;
  mock.tb.samplerate = tpd_sr[24];
  mock.tb.divider    = sr_div[tpd_sr[24]];
  mock.fpga_divider  = mock.tb.divider;

  mock.trg.source     = 0;
  mock.trg.edge       = 0;
  mock.trg.level      = 128;
  mock.trg.ui_mode    = 0;
  mock.trg.pretrigger = 1500;
  mock.trg.total      = 3000;

  mock.gen_en = 0; mock.gen_hz = 1000; mock.gen_duty = 50;
  mock.batt_level = 17; mock.batt_charging = 1; mock.batt_raw = 50;
  mock.ui_samples = 3000; mock.ui_pre = 1500; mock.ui_sw = 1502;
  mock.ui_run = 1; mock.last_run_set = -1; mock.indicator = -1;
  mock.enc33_bad  = 0x77;           //CH2 A wrong, check failed
  mock.enc.mode   = (uint8_t)eh_mode(fpga_version);
  mock.enc.boot33 = (mock.enc.mode == EH_MODE_FLAG) ? EH_33_GOOD : 0;

  //0x153A+: the power-on stages in effect, nothing stored (cap_hw.c at boot)
  if(cs_has(fpga_version))
  {
    mock.cap.flags = PC_CAPF_HAS;
    mock.cap.value = CS_PO;
  }

  fill_ring();

  //SD: MBR with one partition at 2048, eGON images at 16 (BT0) and 750 (EXE) with valid checksums
  memset(mock_sd, 0, sizeof(mock_sd));
  mock.sd = mock_sd;
  mock.sd_sectors = MOCK_SD_SECTORS;
  mock_sd[510] = 0x55; mock_sd[511] = 0xAA;
  mock_sd[0x1BE + 4] = 0x0C;
  pc_put_u32(mock_sd + 0x1BE + 8, 2048);
  mock_sd_image(16, "eGON.BT0", 8);
  mock_sd_image(750, "eGON.EXE", 750);
  pc_ident_invalidate();

  //Diagnostics readers (1.16)
  mock.usb.state = PC_USB_CONFIGURED;
  mock.usb.flags = PC_USBF_HIGH_SPEED | PC_USBF_CONFIGURED | PC_USBF_DTR | PC_USBF_SOFTCONN;
  mock.usb.power = 0x70;
  mock.usb.faddr = 5;
  mock.usb.since_config_ms = 4000;
  mock.cp15 = 0x0005107Du;
  mock.life.power_on = 12;
  mock.life.minutes  = 2225;
  mock.life.life_flags = PC_LIFEF_FOUND | PC_LIFEF_BOOT_WRITE | PC_LIFEF_VALID;
  mock.life.last_end = PC_LIFE_END_SAVED;
  mock.life.last_save = 1;
}

void mock_sd_image(uint32_t sector, const char *magic, uint32_t sectors)
{
  uint8_t *p = mock_sd + sector * 512;
  uint32_t i, sum = 0;

  for(i=0;i<sectors * 512;i++)
    p[i] = (uint8_t)(i * 13 + sector);

  memcpy(p + 4, magic, 8);
  pc_put_u32(p + 16, sectors * 512);
  pc_put_u32(p + 12, 0x5F0A6C39u);

  for(i=0;i<sectors * 512;i+=4)
    sum += pc_get_u32(p + i);

  pc_put_u32(p + 12, sum);
}

void mock_rx_push(const uint8_t *data, uint32_t len)
{
  while(len--)
    mock.rx[(mock.rx_head++) & 0xFFFF] = *data++;
}

void mock_tx_release(void)
{
  mock.tx_busy = 0;
}

//----------------------------------------------------------------------------------------------------------------------------------

void pc_hal_channel_read(int ch, pc_hw_channel *c) { *c = mock.ch[ch]; }

void pc_hal_channel_write(int ch, const pc_hw_channel *c, uint8_t mask)
{
  pc_hw_channel *d = &mock.ch[ch];

  mock.under_screen += mock.ui_screen;

  if(mask & PC_CH_ENABLE)   d->enable = c->enable;
  if(mask & PC_CH_RANGE)    d->range = c->range;
  if(mask & PC_CH_COUPLING) d->coupling = c->coupling;
  if(mask & PC_CH_OFFSET)   d->dcoffset = c->dcoffset;
  if(mask & PC_CH_INVERT)   d->invert = c->invert;
  if(mask & PC_CH_PROBE)    d->probe = c->probe;
  mock.ch_writes++;
  mock.fpga_ops++;
}

uint32_t pc_hal_mul_factor_1x(uint8_t range)
{
  static const uint32_t mf[7] = { 10000, 5000, 2000, 1000, 400, 200, 100 };
  return mf[range];
}

void pc_hal_timebase_read(pc_hw_timebase *t) { *t = mock.tb; }

uint32_t pc_hal_divider_now(void) { return mock.fpga_divider; }

int pc_hal_timebase_lookup(uint8_t index, uint8_t *samplerate, uint32_t *divider)
{
  if(index >= 35)
    return -1;

  *samplerate = tpd_sr[index];
  *divider    = sr_div[*samplerate];
  return 0;
}

void pc_hal_timebase_write(const pc_hw_timebase *t, int raw)
{
  if(raw)
    mock.tb.divider = t->divider;
  else
    mock.tb = *t;

  mock.fpga_divider = t->divider;

  mock.tb_writes++;
  mock.fpga_ops++;
  mock.under_screen += mock.ui_screen;

  //0x0D: on 0x1538+ a capture in flight may now carry 0x33 bit 0 (fpga_bus.h)
  mock.fpga_rate_writes++;
  mock.enc_written = 1;
}

void pc_hal_trigger_read(pc_hw_trigger *t) { *t = mock.trg; }

void pc_hal_trigger_write(const pc_hw_trigger *t, uint8_t mask)
{
  mock.fpga_ops++;
  mock.under_screen += mock.ui_screen;

  if(mask & PC_TRG_SOURCE)
  {
    mock.trg.source = t->source;

    if(t->source == 2)
      mock.gen_en = mock.gen_hw_en = 0;     //as pc_hal_scope.c: EXT switches the generator off
  }
  if(mask & PC_TRG_EDGE)   mock.trg.edge = t->edge;
  if(mask & PC_TRG_LEVEL)  mock.trg.level = t->level;
  if(mask & PC_TRG_MODE)   mock.trg.ui_mode = t->ui_mode;
  if(mask & PC_TRG_PRE)    mock.trg.pretrigger = t->pretrigger;
  if(mask & PC_TRG_TOTAL)  mock.trg.total = t->total;
}

uint32_t pc_hal_auto_timeout_default(uint32_t total_samples)
{
  return (mock.version == 0x1432) ? 1800 : total_samples;
}

void pc_hal_gen_read_ui(uint8_t *enable, uint32_t *freq_hz, uint8_t *duty_pct)
{
  *enable = mock.gen_en; *freq_hz = mock.gen_hz; *duty_pct = mock.gen_duty;
}

void pc_hal_gen_write(uint32_t phase_inc, uint32_t threshold, uint8_t enable)
{
  mock.under_screen += mock.ui_screen;
  mock.gen_phase = phase_inc; mock.gen_thr = threshold; mock.gen_hw_en = enable; mock.gen_writes++; mock.fpga_ops++;

  //UI copy, rounded as pc_hal_scope.c does
  mock.gen_en   = enable;
  mock.gen_hz   = (uint32_t)(((uint64_t)phase_inc * 50000000ull + 0x80000000ull) >> 32);
  mock.gen_duty = (uint8_t)(((uint64_t)threshold * 100 + 0x7FFFFFFFull) / 0xFFFFFFFFull);
}

//----------------------------------------------------------------------------------------------------------------------------------

static int mock_polls;

int pc_hal_arm(uint16_t pre_words, uint16_t total_words, uint32_t auto_timeout, int prog, int normal,
               const pc_decim_cfg *dc, const pc_seq_cfg *sq)
{
  mock.fpga_ops++;

  if(sq)
  {
    //As pc_seq_hw.c: written before the reset; the clk_200 side latches mode and format at the release
    mock.seq_arms++;
    mock.seq_cfg      = *sq;
    mock.seq_run_mode = sq->mode;
    mock.seq_fmt16    = (uint8_t)(sq->mode && dc && dc->k);
    mock.seq_57       = pc_seq_cmd57(sq, 0);
    mock.seq_shift    = 0;
  }

  if(dc)
  {
    //As pc_hal_scope.c: written (and settled) before the reset
    mock.decim_cur     = *dc;
    mock.decim_written = 1;
    mock.decim_rb      = (uint16_t)((pc_decim_status(dc) << 8) | dc->shift);
    mock.decim_arms++;
  }

  if(mock.arm_fail)
    return -1;

  mock.enc_cur_bad = (uint8_t)(mock.enc_bad_arms != 0);
  if(mock.enc_bad_arms > 0)
    mock.enc_bad_arms--;

  //The sampling reset clears 0x33 bit 0 and the record of rate / filter writes since the last one (fpga_bus.h)
  mock.enc_written = 0;
  mock.fpga_resets++;

  mock.arms++;
  mock.arm_pre = pre_words; mock.arm_total = total_words; mock.arm_auto = auto_timeout;
  mock.arm_prog = prog; mock.arm_normal = normal;
  mock_polls = 0;
  fill_ring();
  return 0;
}

void pc_hal_acq_poll(int *done, int *triggered)
{
  mock_polls++;
  mock.fpga_ops++;
  *triggered = (mock.polls_to_trigger >= 0) && (mock_polls >= mock.polls_to_trigger);
  *done      = (mock.polls_to_done >= 0) && (mock_polls >= mock.polls_to_done);
}

uint16_t pc_hal_acq_finish(void) { mock.fpga_ops++; return mock.trigger_addr; }
uint8_t  pc_hal_ext_status(void) { mock.fpga_ops++; mock.ext_reads++; return mock.ext_status; }
void     pc_hal_force(void)      { mock.fpga_ops++; mock.forces++; mock_polls = 1000000; mock.polls_to_trigger = -1; mock.polls_to_done = 0; }

void pc_hal_read_core(uint8_t core, uint32_t start, uint32_t count, uint8_t *dst, uint32_t stride,
                      uint32_t ring_words, uint8_t read_lag)
{
  uint32_t i;

  assert(core < 4);
  assert(start < ring_words);
  assert(count <= ring_words);
  assert(ring_words == mock.ring_words);
  (void)read_lag;

  mock.read_calls++;
  mock.fpga_ops++;
  if(start > mock.max_read_start)
    mock.max_read_start = start;

  for(i=0;i<count;i++)
    dst[i * stride] = mock.ring[core][(start + i) % ring_words];
}

//Sequencer: the FPGA's view of an accumulator word (zaklad.v seq_view)
static uint64_t mock_seq_view(uint64_t a)
{
  uint8_t s = mock.seq_shift & 31;
  uint64_t v = 0;
  int i;

  if((mock.seq_run_mode == 1) || (mock.seq_57 & PC_SEQ_57_RAW))
    return a;

  if(mock.seq_fmt16)
  {
    for(i=0;i<2;i++)
    {
      uint64_t t = ((a >> (32 * i)) & 0xFFFFFFFFull) >> s;
      v |= (uint64_t)((t > 0xFFFF) ? 0xFFFF : t) << (32 * i);
    }
    return v;
  }

  for(i=0;i<4;i++)
    v |= (uint64_t)((s >= 16) ? 0 : (((a >> (16 * i)) & 0xFFFF) >> s)) << (16 * i);

  return v;
}

void pc_hal_seq_status(uint8_t raw[8])  { mock.fpga_ops++; mock.seq_status_reads++; memcpy(raw, mock.seq_status, 8); }
void pc_hal_seq_stop(void)              { mock.fpga_ops++; mock.seq_stops++; mock.seq_status[0] |= PC_SEQ_ST_DONE; mock_polls = 1000000; mock.polls_to_done = 0; }
void pc_hal_seq_view(uint8_t mode57, uint8_t shift) { mock.fpga_ops++; mock.seq_views++; mock.seq_57 = mode57; mock.seq_shift = shift; }

void pc_hal_seq_read(uint8_t byte, uint32_t first, uint32_t count, uint8_t *dst, uint32_t stride)
{
  uint32_t i;

  assert(byte < 8);
  assert(first + count <= 2048);
  mock.fpga_ops++;
  mock.seq_reads++;

  for(i=0;i<count;i++)
    dst[i * stride] = (uint8_t)(mock_seq_view(mock.acc[first + i]) >> (8 * byte));
}

void pc_hal_ui_average(uint8_t *state, uint8_t *log2n) { *state = mock.ui_avg_state; *log2n = mock.ui_avg_log2n; }

void    pc_hal_fpga_write_cmd(uint8_t cmd)   { mock.fpga_ops++; if(mock.ndebug_cmds < 16) mock.debug_cmds[mock.ndebug_cmds++] = 0x100 | cmd; }
void    pc_hal_fpga_write_data(uint8_t data) { mock.fpga_ops++; if(mock.ndebug_cmds < 16) mock.debug_cmds[mock.ndebug_cmds++] = 0x200 | data; }
uint8_t pc_hal_fpga_read_data(void)          { mock.fpga_ops++; return 0x5A; }

//----------------------------------------------------------------------------------------------------------------------------------

int32_t pc_hal_trace_build(uint32_t start_word, uint32_t total_samples, uint32_t pretrigger_samples, uint8_t ch_mask)
{
  uint32_t ch, k;

  mock.trace_builds++;

  for(ch=0;ch<2;ch++)
  {
    if(!(ch_mask & (1 << ch)))
      continue;

    mock.trace16_n[ch] = 0;

    //16-bit layout (as pc_glue_decim_read): one sample per word, low byte core A, high byte core B
    if(mock.decim_cur.k)
    {
      int32_t corr16 = (mock.ch[ch].adc_a_comp + mock.ch[ch].adc_b_comp) * 8;

      for(k=0;k<total_samples;k++)
      {
        uint32_t a = (start_word + k) % mock.ring_words;
        uint16_t v = (uint16_t)(mock.ring[2 * ch][a] | (mock.ring[2 * ch + 1][a] << 8));

        mock.trace16[ch][k] = v;
        mock.trace[ch][k]   = (uint8_t)pc_decim_to_code(v, mock.decim_cur.scale_q24, corr16);
      }

      mock.trace16_n[ch] = total_samples;
      continue;
    }

    for(k=0;k<total_samples/2;k++)
    {
      uint32_t a = (start_word + k) % mock.ring_words;
      uint8_t  va = mock.ring[2 * ch][a], vb = mock.ring[2 * ch + 1][a];

      if(mock.ch[ch].invert) { va = (uint8_t)(255 - va); vb = (uint8_t)(255 - vb); }
      mock.trace[ch][2 * k]     = vb;
      mock.trace[ch][2 * k + 1] = va;
    }
  }

  return (int32_t)pretrigger_samples + 2;
}

const uint8_t *pc_hal_trace(int ch) { return mock.trace[ch]; }

void pc_hal_trace_ui_info(uint32_t *sample_count, uint32_t *pretrigger, int32_t *sw_trigger_index)
{
  *sample_count = mock.ui_samples; *pretrigger = mock.ui_pre; *sw_trigger_index = mock.ui_sw;
}

uint32_t pc_hal_trace_capacity(void) { return sizeof(mock.trace[0]); }

uint8_t  pc_hal_decim_setting(void)        { return mock.decim_setting; }
void     pc_hal_decim_set(uint8_t setting) { mock.decim_setting = setting & 0x3F; mock.fpga_ops++; }
uint16_t pc_hal_decim_readback(void)       { mock.fpga_ops++; return mock.decim_rb; }

void pc_hal_decim_current(pc_decim_cfg *c, int *written)
{
  *c = mock.decim_cur;
  *written = mock.decim_written;
}

const uint16_t *pc_hal_trace16(int ch, uint32_t *count)
{
  *count = mock.trace16_n[ch & 1];
  return *count ? mock.trace16[ch & 1] : 0;
}

//----------------------------------------------------------------------------------------------------------------------------------

uint32_t pc_hal_ms(void)              { return mock.now; }
void     pc_hal_delay_ms(uint32_t ms) { mock.now += ms; }
uint16_t pc_hal_fpga_version(void)    { return mock.version; }

void pc_hal_battery(uint8_t *level, uint8_t *charging, uint8_t *keyadc_raw)
{
  *level = mock.batt_level; *charging = mock.batt_charging; *keyadc_raw = mock.batt_raw;
}

void pc_hal_ui_read(pc_hw_ui *u)
{
  u->run = mock.ui_run; u->ui_armed = mock.ui_armed; u->long_mode = mock.long_mode;
}

void pc_hal_ui_set_run(int run)
{
  mock.under_screen += mock.ui_screen;
  mock.ui_run = run ? 1 : 0;
  mock.ui_armed = 0;
  mock.run_sets++;
  mock.last_run_set = run;
}

uint32_t pc_hal_settings_fingerprint(void)
{
  uint8_t b[64];
  uint32_t n = 0;
  int i;

  for(i=0;i<2;i++)
  {
    b[n++] = mock.ch[i].enable; b[n++] = mock.ch[i].range; b[n++] = mock.ch[i].coupling;
    b[n++] = mock.ch[i].invert; b[n++] = mock.ch[i].probe;
    b[n++] = (uint8_t)mock.ch[i].dcoffset; b[n++] = (uint8_t)(mock.ch[i].dcoffset >> 8);
  }

  b[n++] = mock.tb.timeperdiv; b[n++] = mock.tb.samplerate;
  b[n++] = mock.trg.source; b[n++] = mock.trg.edge; b[n++] = mock.trg.level; b[n++] = mock.trg.ui_mode;
  b[n++] = (uint8_t)mock.trg.pretrigger; b[n++] = (uint8_t)(mock.trg.pretrigger >> 8);
  b[n++] = (uint8_t)mock.trg.total; b[n++] = (uint8_t)(mock.trg.total >> 8);
  b[n++] = mock.long_mode;
  b[n++] = mock.decim_setting;
  return pc_crc32(b, n);
}

void pc_hal_indicator(int state) { mock.indicator = state; mock.indicator_draws++; }
uint8_t pc_hal_ui_hold(void) { return mock.ui_hold; }

int pc_hal_ui_leave_screen(void)
{
  if(!mock.ui_screen)
    return 0;

  mock.ui_screen = 0;
  mock.ui_screen_leaves++;
  return 1;
}

int mock_ui_modal_pass(void *session)
{
  pc_session *s = (pc_session *)session;

  pc_poll(s, PC_CTX_BUSY);

  //The yield check (pc_glue_ui_yield): fw 0.2.94 answers a read-only request there first
  if(mock.ui_modal && pc_ui_modal_pending(s))
    pc_poll(s, PC_CTX_MODAL);

  if(mock.ui_modal && pc_ui_yield_pending(s))
  {
    mock.ui_modal--;
    mock.ui_cancels++;
    return 1;
  }

  return 0;
}

//----------------------------------------------------------------------------------------------------------------------------------

uint8_t *pc_hal_tx_buffer(uint32_t *capacity)
{
  *capacity = sizeof(mock_tx);
  return mock_tx;
}

int pc_hal_tx_busy(void) { return mock.tx_busy; }

void pc_hal_tx_submit(uint32_t len)
{
  assert(!mock.tx_busy);
  assert(len <= sizeof(mock_tx));

  if(mock_nresps == MOCK_MAX_RESP)
  {
    free(mock_resps[0].data);
    memmove(mock_resps, mock_resps + 1, sizeof(mock_resps[0]) * (MOCK_MAX_RESP - 1));
    mock_nresps--;
  }

  mock_resps[mock_nresps].data = malloc(len ? len : 1);
  memcpy(mock_resps[mock_nresps].data, mock_tx, len);
  mock_resps[mock_nresps].len = len;
  mock_nresps++;
  mock.submits++;

  if(mock.tx_hold)
  {
    mock.tx_busy  = 1;
    mock.tx_since = mock.now;
  }
}

uint32_t pc_hal_rx_read(uint8_t *dst, uint32_t max)
{
  uint32_t n = 0;

  while((n < max) && (mock.rx_tail != mock.rx_head))
    dst[n++] = mock.rx[(mock.rx_tail++) & 0xFFFF];

  return n;
}

uint32_t pc_hal_link_take_events(void)
{
  uint32_t e = mock.link_events;
  mock.link_events = 0;
  return e;
}

int pc_hal_dtr(void) { return mock.dtr; }

int      pc_hal_tx_pump(void)    { return mock.tx_busy; }
uint32_t pc_hal_tx_idle_ms(void) { return mock.tx_busy ? (mock.now - mock.tx_since) : 0; }
void     pc_hal_tx_abort(void)   { mock.tx_busy = 0; mock.tx_aborts++; }
void     pc_hal_enter_fel(void)  { mock.fel_entries++; }
void     pc_hal_link_stats(uint32_t *v) { uint32_t i; for(i=0;i<PC_TSTAT_COUNT;i++) v[i] = 1000 + i; }

void     pc_hal_usb_events(uint32_t *v) { uint32_t i; for(i=0;i<PC_USBEV_COUNT;i++) v[i] = 2000 + i; }
void     pc_hal_rx_debug(uint32_t *v)   { uint32_t i; for(i=0;i<PC_RXDBG_COUNT;i++) v[i] = 3000 + i; }
uint32_t pc_hal_boot_splash_ms(void)   { return 5000; }

//Diagnostics readers (1.16)
static char mock_report_buf[70000];

void     pc_hal_usb_state(pc_hw_usb *u)  { *u = mock.usb; }
uint32_t pc_hal_cp15_control(void)       { return mock.cp15; }
void     pc_hal_life(pc_hw_life *l)      { *l = mock.life; }
void     pc_hal_life_clear_restart(void) { mock.life.rst.valid = 0; mock.life_clears++; }
void     pc_hal_restart_test(void)       { mock.restart_tests++; }

int pc_hal_cal_prov(uint8_t *raw)
{
  memcpy(raw, mock.prov_raw, ACAL_PROV_BYTES);
  return mock.prov_state;
}

int pc_hal_report_write(const char *text, uint32_t len, uint32_t power_on, char *name, uint32_t name_max)
{
  static const char base[] = "reports/report-00000-00.txt";
  uint32_t i, p = power_on;

  mock.report_writes++;

  if(mock.report_fail)
    return mock.report_fail;

  assert(len < sizeof(mock_report_buf));
  memcpy(mock_report_buf, text, len);
  mock_report_buf[len] = 0;
  mock.report     = mock_report_buf;
  mock.report_len = len;

  assert(name_max > sizeof(base));
  memcpy(name, base, sizeof(base));

  for(i=0;i<5;i++, p/=10)
    name[19 - i] = (char)('0' + p % 10);

  return 0;
}

//READ_SCREEN: a framebuffer whose pixels say where they are (mock_screen_pixel), stored little-endian like the panel's
uint32_t mock_screen_calls;

//Every byte 0x80..0xFF: no "#" or frame magic inside the vector made from it (resp_read_screen_last_row)
uint16_t mock_screen_pixel(uint32_t row, uint32_t x) { return (uint16_t)(0x8080 | (x & 0x7F) | (((x >> 7) & 7) << 12) | ((row & 15) << 8)); }

void pc_hal_screen_rows(uint32_t row, uint32_t nrows, uint8_t *dst)
{
  uint32_t r, x;

  mock_screen_calls++;
  mock.screen_read_under = mock.ui_screen;

  for(r=row;r<row + nrows;r++)
    for(x=0;x<PC_SCREEN_WIDTH;x++, dst += 2)
      pc_put_u16(dst, mock_screen_pixel(r, x));
}

uint8_t  mock_display_mode;
int32_t  mock_spec_total = -1;          //bins in the mock spectrum, -1 = none shown
uint32_t mock_spec_first, mock_spec_max;
int      mock_spec_ch;

uint8_t pc_hal_display_mode(void) { return mock_display_mode; }

int32_t pc_hal_spectrum_read(int ch, uint32_t first, uint32_t max_bins, uint8_t *hdr, uint8_t *levels)
{
  uint32_t n, i;

  mock_spec_ch = ch; mock_spec_first = first; mock_spec_max = max_bins;
  if(mock_spec_total < 0)
    return -1;

  n = (first < (uint32_t)mock_spec_total) ? (uint32_t)mock_spec_total - first : 0;
  if(n > max_bins)
    n = max_bins;
  memset(hdr, 0, PC_SPECTRUM_HDR);
  hdr[0] = (uint8_t)ch; hdr[2] = 12;
  pc_put_u16(hdr + 24, (uint16_t)mock_spec_total); pc_put_u16(hdr + 26, (uint16_t)first); pc_put_u16(hdr + 28, (uint16_t)n);
  for(i = 0; i < n; i++)
    pc_put_u16(levels + 2 * i, (uint16_t)(int16_t)(-1000 - (int32_t)(first + i)));
  return (int32_t)n;
}

//CAL_PROBE (fw 0.2.67): the arguments, the call count, and a canned measurement (zero point A 130.0 / B 126.0 at PWM 812)
uint32_t mock_cal_probe_calls;
uint8_t  mock_cal_probe_ch, mock_cal_probe_range, mock_cal_probe_settle, mock_cal_probe_pre, mock_cal_probe_mode;
int32_t pc_hal_cal_probe(uint8_t ch, uint8_t range, uint8_t settle, uint8_t prestrobe, uint8_t mode, uint8_t *dst)
{
  acal_point pt[ACAL_POINTS];
  acal_fail  f;

  mock_cal_probe_calls++;
  mock_cal_probe_ch    = ch;
  mock_cal_probe_range = range;
  mock_cal_probe_settle = settle;
  mock_cal_probe_pre    = prestrobe;
  mock_cal_probe_mode   = mode;
  mock.now += 1500;                                             //the probe takes about 1.5 s of the main loop

  memset(pt, 0, sizeof(pt));
  memset(&f, 0, sizeof(f));
  f.point = ACAL_PT_NONE;
  pt[ACAL_PT_F_ZERO].pwm       = 812;
  pt[ACAL_PT_F_ZERO].cls       = ACAL_FULL;
  pt[ACAL_PT_F_ZERO].convs     = 64;
  pt[ACAL_PT_F_ZERO].mean16[0] = 130 * 16;
  pt[ACAL_PT_F_ZERO].mean16[1] = 126 * 16;
  return (int32_t)acal_probe_pack(dst, ch, range, 0, (uint8_t)((settle << 4) | (prestrobe ? ACAL_PROBE_PRESTROBE : 0) | (mode ? ACAL_PROBE_WIZARD : 0)), 800, 812, 0, 0, &f, pt);
}

//fw 0.2.66 Bode plot: the status BODE_CTRL answers, the request it saw; READ_BODE: a header and mock_bode_rows rows
int      mock_bode_status;
uint16_t mock_bode_detail;
uint8_t  mock_bode_req[16];
uint32_t mock_bode_rows, mock_bode_first, mock_bode_max;

int pc_hal_bode_ctrl(const uint8_t *req, uint8_t *resp, uint16_t *detail)
{
  memcpy(mock_bode_req, req, 16);
  mock.under_screen += mock.ui_screen;      //BODE_CTRL open draws the Frequency response, run sweeps on it
  *detail = mock_bode_detail;
  memset(resp, 0, PC_BODE_CTRL_RESP);
  resp[4] = 1;                                  //state running
  pc_put_u32(resp + 32, 1234);                  //estimate
  return mock_bode_status;
}

uint32_t pc_hal_bode_read(uint32_t first, uint32_t max_rows, uint8_t *resp)
{
  uint32_t n = (first < mock_bode_rows) ? mock_bode_rows - first : 0, i;

  mock_bode_first = first;
  mock_bode_max   = max_rows;
  if(n > max_rows)
    n = max_rows;
  memset(resp, 0, PC_BODE_HDR + n * PC_BODE_ROW);
  pc_put_u16(resp + 8, (uint16_t)mock_bode_rows);
  for(i = 0; i < n; i++)
    pc_put_u32(resp + PC_BODE_HDR + i * PC_BODE_ROW, 1000 + first + i);
  return PC_BODE_HDR + n * PC_BODE_ROW;
}

uint32_t mock_fft_bench_reps;
uint32_t pc_hal_fft_bench(uint8_t *dst, uint32_t reps)
{
  mock_fft_bench_reps = reps;
  memset(dst, 0, 8 + 24);
  pc_put_u16(dst, 1); pc_put_u16(dst + 2, reps); pc_put_u32(dst + 4, 0x00050078);
  pc_put_u16(dst + 8, 4096); dst[10] = 1; pc_put_u32(dst + 16, 6000);
  return 8 + 24;
}
void     pc_hal_restart_info(uint32_t *v) { uint32_t i; for(i=0;i<PC_RESTART_COUNT;i++) v[i] = 6000 + i; }
void     pc_hal_fault_info(uint32_t *v) { v[0] = 0x0805u; v[1] = 0x80123456u; }
void     pc_hal_usb_watch(uint32_t *v) { v[0] = 7000; v[1] = 7001; }

//Full-rate capture check: 0x33 per the last arm, counted as enc_heal_hw.c counts
void     pc_hal_enc_state(pc_hw_enc *e) { *e = mock.enc; }
void     pc_hal_cap_state(pc_hw_cap *c) { *c = mock.cap; }
void     pc_hal_cap_check(void) { mock.cap_checks++; mock.cap.reads++; mock.cap.flags |= PC_CAPF_READ; }
void     pc_hal_enc_retry(void)         { mock.fpga_ops++; mock.enc_retries++; }
int      pc_hal_enc_written(void)       { return mock.enc_written; }

void pc_hal_fpga_writes(uint32_t *v)
{
  v[0] = mock.fpga_rate_writes;
  v[1] = mock.fpga_filter_writes;
  v[2] = mock.fpga_resets;
}

uint8_t pc_hal_fpga_trace(int set)
{
  if(set >= 0)
    mock.fpga_trace = (uint8_t)(set != 0);

  return mock.fpga_trace;
}

uint8_t pc_hal_enc_status(void)
{
  mock.fpga_ops++;
  mock.enc_status_reads++;
  mock.enc.last33 = mock.enc_cur_bad ? mock.enc33_bad : EH_33_GOOD;

  //A rate / filter write since the sampling reset can make one prediction miss: bit 0 sticky (fpga/al3/README.md section 8)
  if(mock.enc_written)
    mock.enc.last33 |= EH_33_FAILED;
  mock.enc.flags |= PC_ENCF_READ;
  return mock.enc.last33;
}

void pc_hal_enc_note(uint8_t why, const eh_note *n)
{
  mock.enc_notes++;
  mock.enc_last_why  = why;
  mock.enc_last_note = *n;
  mock.enc.checked++;
  mock.enc.bad += n->first;

  if((why == EH_WHY_DATA) && (mock.enc.mode == EH_MODE_FLAG))
    mock.enc.replaced++;

  if(!n->bad)
    mock.enc.flags &= (uint8_t)~PC_ENCF_GARBLED;

  if(n->done)
  {
    mock.enc_events++;

    if(n->ok)
      mock.enc.recovered++;
    else
      mock.enc.flags |= PC_ENCF_GARBLED;
  }
}

uint32_t pc_hal_event_log(uint8_t *dst, uint32_t max, uint32_t *total)
{
  uint32_t i, k = (max < 3) ? max : 3;

  for(i=0;i<k;i++)
  {
    memset(dst + 8 * i, 0, 8);
    pc_put_u32(dst + 8 * i, 100 + i);
    dst[8 * i + 4] = (uint8_t)(1 + i);
    dst[8 * i + 5] = 0xA0;
    pc_put_u16(dst + 8 * i + 6, 0x1234);
  }

  *total = 77;
  return k;
}

int mock_snap_rearms;

uint32_t pc_hal_event_bus(uint8_t *dst, uint32_t *total)
{
  memset(dst, 0, 8);
  pc_put_u32(dst, 120);
  dst[4] = 13;
  dst[5] = 0;
  pc_put_u16(dst + 6, 0x1814);
  *total = 9;
  return 1;
}

int32_t pc_hal_event_snapshot(uint8_t *dst, uint32_t *reset_ms, int rearm)
{
  mock_snap_rearms += rearm ? 1 : 0;
  memset(dst, 0, 16);
  pc_put_u32(dst, 90);
  dst[4] = 5;
  pc_put_u32(dst + 8, 95);
  dst[12] = 1;
  *reset_ms = 95;
  return 2;
}

uint32_t pc_hal_sd_sectors(void) { return mock.sd_sectors; }

int pc_hal_sd_read(uint32_t sector, uint32_t count, uint8_t *dst)
{
  assert(((uintptr_t)dst & 3) == 0);

  if(sector + count > MOCK_SD_SECTORS)
    return -4;

  memcpy(dst, mock_sd + sector * 512, count * 512);
  mock.sd_reads++;
  return 0;
}

int pc_hal_sd_write(uint32_t sector, uint32_t count, const uint8_t *src)
{
  if(mock.sd_fail_write)
    return mock.sd_fail_write;

  if(sector + count > MOCK_SD_SECTORS)
    return -4;

  memcpy(mock_sd + sector * 512, src, count * 512);

  if(mock.sd_corrupt)
    mock_sd[sector * 512 + 100] ^= 0x10;

  mock.sd_writes++;
  return 0;
}

uint8_t *pc_hal_sd_stage(uint32_t *capacity)
{
  *capacity = sizeof(mock_stage);
  return mock_stage;
}
