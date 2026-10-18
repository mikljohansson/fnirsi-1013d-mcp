//----------------------------------------------------------------------------------------------------------------------------------
// mock_hal.c - host implementation of pc_hal.h: a small model of the scope, its FPGA ring and the USB transport.
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "mock_hal.h"

mock_state mock;
mock_resp  mock_resps[MOCK_MAX_RESP];
uint32_t   mock_nresps;

static uint8_t mock_tx[PC_MAX_RESPONSE_FRAME + 16] __attribute__((aligned(4)));
static uint8_t mock_sd[MOCK_SD_SECTORS * 512] __attribute__((aligned(4)));
static uint8_t mock_stage[65536] __attribute__((aligned(4)));

//1.00o5 tables (variables.c): time_per_div_sample_rate[35] and sample_rate_settings[0..17]
static const uint8_t tpd_sr[35] = { 28,27,26,25,24,23,22,21,20,19,18, 17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,0,0,0,0,0,0 };
static const uint32_t sr_div[29] = { 0,1,3,9,19,39,99,199,399,999,1999,3999,9999,19999,39999,99999,199999,399999,
                                     399999,399999,399999,399999,399999,399999,399999,399999,399999,399999,399999 };

static uint8_t ring_value(int core, uint32_t addr, uint32_t arms)
{
  return (uint8_t)(addr * 3 + core * 50 + arms * 7 + (addr >> 8));
}

static void fill_ring(void)
{
  uint32_t c, a;

  for(c=0;c<4;c++)
    for(a=0;a<MOCK_RING_MAX;a++)
      mock.ring[c][a] = ring_value((int)c, a, mock.arms);
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

  if(mask & PC_CH_ENABLE)   d->enable = c->enable;
  if(mask & PC_CH_RANGE)    d->range = c->range;
  if(mask & PC_CH_COUPLING) d->coupling = c->coupling;
  if(mask & PC_CH_OFFSET)   d->dcoffset = c->dcoffset;
  if(mask & PC_CH_INVERT)   d->invert = c->invert;
  if(mask & PC_CH_PROBE)    d->probe = c->probe;
  mock.ch_writes++;
}

uint32_t pc_hal_mul_factor_1x(uint8_t range)
{
  static const uint32_t mf[7] = { 10000, 5000, 2000, 1000, 400, 200, 100 };
  return mf[range];
}

void pc_hal_timebase_read(pc_hw_timebase *t) { *t = mock.tb; }

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

  mock.tb_writes++;
}

void pc_hal_trigger_read(pc_hw_trigger *t) { *t = mock.trg; }

void pc_hal_trigger_write(const pc_hw_trigger *t, uint8_t mask)
{
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
  mock.gen_phase = phase_inc; mock.gen_thr = threshold; mock.gen_hw_en = enable; mock.gen_writes++;

  //UI copy, rounded as pc_hal_scope.c does
  mock.gen_en   = enable;
  mock.gen_hz   = (uint32_t)(((uint64_t)phase_inc * 50000000ull + 0x80000000ull) >> 32);
  mock.gen_duty = (uint8_t)(((uint64_t)threshold * 100 + 0x7FFFFFFFull) / 0xFFFFFFFFull);
}

//----------------------------------------------------------------------------------------------------------------------------------

static int mock_polls;

int pc_hal_arm(uint16_t pre_words, uint16_t total_words, uint32_t auto_timeout, int prog, int normal)
{
  if(mock.arm_fail)
    return -1;

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
  *triggered = (mock.polls_to_trigger >= 0) && (mock_polls >= mock.polls_to_trigger);
  *done      = (mock.polls_to_done >= 0) && (mock_polls >= mock.polls_to_done);
}

uint16_t pc_hal_acq_finish(void) { return mock.trigger_addr; }
uint8_t  pc_hal_ext_status(void) { mock.ext_reads++; return mock.ext_status; }
void     pc_hal_force(void)      { mock.forces++; mock_polls = 1000000; mock.polls_to_trigger = -1; mock.polls_to_done = 0; }

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
  if(start > mock.max_read_start)
    mock.max_read_start = start;

  for(i=0;i<count;i++)
    dst[i * stride] = mock.ring[core][(start + i) % ring_words];
}

void    pc_hal_fpga_write_cmd(uint8_t cmd)   { if(mock.ndebug_cmds < 16) mock.debug_cmds[mock.ndebug_cmds++] = 0x100 | cmd; }
void    pc_hal_fpga_write_data(uint8_t data) { if(mock.ndebug_cmds < 16) mock.debug_cmds[mock.ndebug_cmds++] = 0x200 | data; }
uint8_t pc_hal_fpga_read_data(void)          { return 0x5A; }

//----------------------------------------------------------------------------------------------------------------------------------

int32_t pc_hal_trace_build(uint32_t start_word, uint32_t total_samples, uint32_t pretrigger_samples, uint8_t ch_mask)
{
  uint32_t ch, k;

  mock.trace_builds++;

  for(ch=0;ch<2;ch++)
  {
    if(!(ch_mask & (1 << ch)))
      continue;

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
  return pc_crc32(b, n);
}

void pc_hal_indicator(int state) { mock.indicator = state; mock.indicator_draws++; }

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
uint32_t pc_hal_usb_irq_polled(void)   { return 4000; }

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

int32_t pc_hal_event_snapshot(uint8_t *dst, uint32_t *reset_ms)
{
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
