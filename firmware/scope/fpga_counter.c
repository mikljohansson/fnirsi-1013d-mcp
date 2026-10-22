//----------------------------------------------------------------------------------------------------------------------------------
// fpga_counter.c - the FPGA 0x153C hardware counter: boot configuration, the once-per-pass 0x35 reader, the rate-change
// rule and the derived values (fpga_counter.h). Builds for the scope and on the host (host_tests/test_fpga_bus.c against
// fpga_model.c).
//----------------------------------------------------------------------------------------------------------------------------------

#include "fpga_bus.h"
#include "fpga_counter.h"

fpga_counter_state fpga_cnt;

uint32_t fpga_counter_gate_ms(uint8_t gate)
{
  switch(gate & 3)
  {
    case FPGA_CNT_GATE_10MS:  return 10;
    case FPGA_CNT_GATE_100MS: return 100;
    case FPGA_CNT_GATE_1S:    return 1000;
    default:                  return 0;
  }
}

int fpga_counter_derive(uint32_t e, uint32_t t, uint32_t w, uint64_t *freq_uhz, uint64_t *period_ps, uint64_t *width_ps,
                        uint64_t *low_ps, uint32_t *duty_ppm)
{
  uint64_t num, q, r;

  *freq_uhz = *period_ps = *width_ps = *low_ps = 0;
  *duty_ppm = 0;

  //E = 0: fewer than two rising edges in the gate. T and W are 28-bit, E <= T / 2 (a period is at least two clocks)
  if((e == 0) || (t == 0))
    return 0;

  if(w > t)
    w = t;

  //Frequency = E / (T x 5 ns) = E x 2e8 / T Hz. E x 2e8 fits (5.4e16), E x 2e14 would not: the integer Hz first, then the
  //remainder scaled to uHz; together floor((E x 2e14 + T / 2) / T)
  num = (uint64_t)e * 200000000ull;
  q   = num / t;
  r   = num % t;
  *freq_uhz = q * 1000000ull + (r * 1000000ull + t / 2) / t;

  //Times in ps: one clock is 5000 ps; T x 5000 <= 1.4e12
  *period_ps = ((uint64_t)t * 5000ull + e / 2) / e;
  *width_ps  = ((uint64_t)w * 5000ull + e / 2) / e;
  *low_ps    = ((uint64_t)(t - w) * 5000ull + e / 2) / e;

  //Duty = W / T in ppm; W x 1e6 <= 2.7e14
  *duty_ppm  = (uint32_t)(((uint64_t)w * 1000000ull + t / 2) / t);
  return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// The bus writes

static void cnt_write_61(uint8_t gate, uint8_t source)
{
  fpga_write_cmd(0x61);
  fpga_write_byte((uint8_t)((gate & 3) | ((source & 3) << 2)));
}

static void cnt_write_62(uint8_t level, uint8_t hyst)
{
  fpga_write_cmd(0x62);
  fpga_write_byte(level);
  fpga_write_byte(hyst);
}

void fpga_counter_boot(void)
{
  fpga_counter_state *c = &fpga_cnt;
  uint32_t i;

  for(i=0;i<sizeof(*c);i++)
    ((uint8_t *)c)[i] = 0;

  c->cfg_gate   = FPGA_CNT_GATE_100MS;
  c->cfg_source = FPGA_CNT_SRC_TRIG;
  c->cfg_level  = 0;                                 //auto: the first gate's min / max set the thresholds
  c->cfg_hyst   = 0;

  //Thresholds first, then the gate starts (a FEL load leaves the FPGA as the previous firmware left it: always written)
  cnt_write_62(FPGA_CNT_LEVEL_BOOT, FPGA_CNT_HYST_BOOT);
  cnt_write_61(c->cfg_gate, c->cfg_source);
  c->level  = FPGA_CNT_LEVEL_BOOT;
  c->hyst   = FPGA_CNT_HYST_BOOT;
  c->gate   = c->cfg_gate;
  c->source = c->cfg_source;

  //The initialisation's own 0x0D / 0x53 writes are before the gate: not a rate change
  c->wclk_seen = fpga_bus_wr.wclk;
}

void fpga_counter_config(uint8_t gate, uint8_t source, uint8_t level, uint8_t hyst)
{
  fpga_cnt.cfg_gate   = (uint8_t)(gate & 3);
  fpga_cnt.cfg_source = (uint8_t)(source & 3);
  fpga_cnt.cfg_level  = level;
  fpga_cnt.cfg_hyst   = hyst;
  fpga_cnt.pending    = 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// The pass

static void cnt_take(fpga_counter_state *c, uint32_t now_ms)
{
  const uint8_t *b = c->raw;

  c->held     = 1;
  c->seq      = b[1];
  c->res_src  = (uint8_t)((b[0] & 0x20) ? FPGA_CNT_RES_EXT : ((b[0] & 0x10) ? FPGA_CNT_RES_CH2 : FPGA_CNT_RES_CH1));
  c->res_gate = (uint8_t)(b[0] & 3);
  c->e        = ((uint32_t)b[2] << 24) | ((uint32_t)b[3] << 16) | ((uint32_t)b[4] << 8) | b[5];
  c->t        = ((uint32_t)b[6] << 24) | ((uint32_t)b[7] << 16) | ((uint32_t)b[8] << 8) | b[9];
  c->w        = ((uint32_t)b[10] << 24) | ((uint32_t)b[11] << 16) | ((uint32_t)b[12] << 8) | b[13];
  c->min      = b[14];
  c->max      = b[15];
  c->res_ms   = now_ms;
  c->has_period = (uint8_t)fpga_counter_derive(c->e, c->t, c->w, &c->freq_uhz, &c->period_ps, &c->width_ps, &c->low_ps,
                                               &c->duty_ppm);
  c->results++;
}

//The thresholds the held result's min / max ask for (fpga_counter.h "The thresholds follow the signal"); a pinned level
//or hysteresis is kept. 0: no signal (EXT, or a span below FPGA_CNT_AUTO_MIN_SPAN)
static int cnt_auto_target(const fpga_counter_state *c, uint8_t *level, uint8_t *hyst)
{
  uint32_t span, h, u;

  if((c->res_src == FPGA_CNT_RES_EXT) || (c->max < c->min))
    return 0;

  span = (uint32_t)(c->max - c->min);

  if(span < FPGA_CNT_AUTO_MIN_SPAN)
    return 0;

  if(c->cfg_hyst)
    h = c->cfg_hyst;
  else
  {
    h = (span + 3) / 6;

    if(h < FPGA_CNT_HYST_MIN)
      h = FPGA_CNT_HYST_MIN;

    if(h > span / 2)
      h = span / 2;
  }

  if(c->cfg_level)
  {
    u = c->cfg_level;

    if(h > u)                                        //GET_COUNTER's rule: the low threshold stays at or above 0
      h = u;
  }
  else
  {
    u = ((uint32_t)c->min + c->max + h + 1) / 2;

    if(u > 255)
      u = 255;
  }

  *level = (uint8_t)u;
  *hyst  = (uint8_t)h;
  return 1;
}

static uint32_t cnt_absdiff(uint32_t a, uint32_t b)
{
  return (a > b) ? a - b : b - a;
}

//A result just taken in auto mode: does it fit the thresholds it was counted at? If not, *level / *hyst are the next ones
static void cnt_auto_check(fpga_counter_state *c, uint8_t *level, uint8_t *hyst)
{
  uint8_t  lvl, hys;
  uint32_t band;

  c->misfit = 0;

  if(c->cfg_level && c->cfg_hyst)
    return;

  if(!cnt_auto_target(c, &lvl, &hys))
  {
    //No signal: keep the thresholds; with an auto level the count is noise (EXT has no thresholds)
    c->misfit = (uint8_t)(!c->cfg_level && (c->res_src != FPGA_CNT_RES_EXT));
    return;
  }

  band = hys / 4;

  if(band < 2)
    band = 2;

  if((cnt_absdiff(lvl, c->level) > band) || (cnt_absdiff(hys, c->hyst) > band))
  {
    c->misfit = 1;
    *level    = lvl;
    *hyst     = hys;
  }
}

void fpga_counter_pass(uint32_t now_ms)
{
  fpga_counter_state *c = &fpga_cnt;
  uint32_t i;
  uint8_t  level = c->level, hyst = c->hyst;
  int      drop = 0, zero_written = 0, restart;

  //A write clock change since the previous pass: abort the running gate before anything can be published from it
  if(fpga_bus_wr.wclk != c->wclk_seen)
  {
    c->wclk_seen = fpga_bus_wr.wclk;
    drop = 1;

    if(c->gate != FPGA_CNT_GATE_OFF)
    {
      cnt_write_61(FPGA_CNT_GATE_OFF, c->source);
      zero_written = 1;
    }
  }

  //The snapshot: the command byte, then 16 data strobes
  fpga_write_cmd(0x35);
  fpga_io_read_mode();

  for(i=0;i<16;i++)
    c->raw[i] = fpga_io_strobe_read();

  c->reads++;

  if(c->raw[0] & 0x80)
  {
    if(drop)
      c->dropped++;
    else
    {
      cnt_take(c, now_ms);
      cnt_auto_check(c, &level, &hyst);

      if(c->misfit && ((level != c->level) || (hyst != c->hyst)))
        c->retunes++;
    }
  }

  //Thresholds: the host's pinned values, else the auto ones; a change restarts the gate too
  if(c->cfg_level)
    level = c->cfg_level;

  if(c->cfg_hyst)
    hyst = c->cfg_hyst;
  else if(hyst > level)                              //an auto hysteresis under a newly pinned level
    hyst = level;

  restart = drop || c->pending;

  if((level != c->level) || (hyst != c->hyst))
  {
    cnt_write_62(level, hyst);
    c->level = level;
    c->hyst  = hyst;
    restart  = 1;
  }

  if(restart)
  {
    //0 then the value: a value change restarts the gate, so the next result is one clean full gate
    if(!zero_written)
      cnt_write_61(FPGA_CNT_GATE_OFF, c->source);

    cnt_write_61(c->cfg_gate, c->cfg_source);
    c->gate    = c->cfg_gate;
    c->source  = c->cfg_source;
    c->pending = 0;
    c->restarts++;
  }
}

int fpga_counter_current(uint32_t now_ms)
{
  const fpga_counter_state *c = &fpga_cnt;

  if(!c->held || (c->gate == FPGA_CNT_GATE_OFF))
    return 0;

  return (now_ms - c->res_ms) <= 3 * fpga_counter_gate_ms(c->gate) + 100;
}

int fpga_counter_usable(uint32_t now_ms)
{
  return fpga_counter_current(now_ms) && fpga_cnt.has_period && !fpga_cnt.misfit;
}
