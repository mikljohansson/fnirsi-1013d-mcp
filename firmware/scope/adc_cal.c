//----------------------------------------------------------------------------------------------------------------------------------
// adc_cal.c - per-core ADC calibration maths and the sector 708 block (adc_cal.h, firmware/README.md section 6).
//----------------------------------------------------------------------------------------------------------------------------------

#include "adc_cal.h"

//Round half away from zero, d > 0
static int32_t rdiv(int32_t n, int32_t d)
{
  return (n >= 0) ? (n + d / 2) / d : -((-n + d / 2) / d);
}

static int64_t rdiv64(int64_t n, int64_t d)
{
  return (n >= 0) ? (n + d / 2) / d : -((-n + d / 2) / d);
}

static int32_t floor16(int32_t v)
{
  return (v >= 0) ? v / 16 : -((15 - v) / 16);
}

static int32_t iabs(int32_t v)
{
  return (v < 0) ? -v : v;
}

//----------------------------------------------------------------------------------------------------------------------------------
//Sector block

uint32_t acal_crc32(const uint8_t *data, uint32_t len)
{
  uint32_t crc = 0xFFFFFFFFu;
  uint32_t i;

  while(len--)
  {
    crc ^= *data++;

    for(i=0;i<8;i++)
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }

  return ~crc;
}

static uint8_t *put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); return p + 2; }
static uint8_t *put32(uint8_t *p, uint32_t v) { p = put16(p, v & 0xFFFF); return put16(p, v >> 16); }
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return get16(p) | (get16(p + 2) << 16); }

uint8_t *acal_pack_channel(const acal_channel *c, uint8_t *p)
{
  uint32_t k, r, m;

  for(r=0;r<ACAL_RANGES;r++) p = put16(p, c->pwm[r]);
  for(m=0;m<ACAL_CLASSES;m++) for(k=0;k<2;k++) for(r=0;r<ACAL_RANGES;r++) p = put16(p, (uint16_t)c->zero[m][k][r]);
  for(m=0;m<ACAL_CLASSES;m++) for(k=0;k<2;k++) for(r=0;r<ACAL_RANGES;r++) p = put16(p, (uint16_t)c->slope[m][k][r]);
  for(m=0;m<ACAL_CLASSES;m++) for(k=0;k<2;k++) for(r=0;r<ACAL_RANGES;r++) *p++ = c->noise[m][k][r];
  for(m=0;m<ACAL_CLASSES;m++) for(k=0;k<2;k++) p = put16(p, c->gain[m][k]);
  *p++ = c->gain_range;
  *p++ = c->gain_flags;
  return put16(p, c->gain_lever);
}

void acal_pack(const acal_channel ch[2], uint8_t *block)
{
  uint8_t *p = block;

  p = put32(p, ACAL_MAGIC);
  p = put16(p, ACAL_VERSION);
  p = put16(p, ACAL_BLOCK_BYTES);
  p = put16(p, ACAL_CLASSES);
  p = put16(p, 0);
  p = acal_pack_channel(&ch[0], p);
  p = acal_pack_channel(&ch[1], p);
  put32(p, acal_crc32(block, ACAL_BLOCK_BYTES - 4));
}

int acal_unpack(const uint8_t *block, acal_channel ch[2])
{
  const uint8_t *p = block + ACAL_HDR_BYTES;
  uint32_t c, k, r, m;

  if(get32(block) != ACAL_MAGIC)
    return ACAL_E_MAGIC;

  if((get16(block + 4) != ACAL_VERSION) || (get16(block + 6) != ACAL_BLOCK_BYTES) || (get16(block + 8) != ACAL_CLASSES))
    return ACAL_E_VERSION;

  if(get32(block + ACAL_BLOCK_BYTES - 4) != acal_crc32(block, ACAL_BLOCK_BYTES - 4))
    return ACAL_E_CRC;

  for(c=0;c<2;c++)
  {
    for(r=0;r<ACAL_RANGES;r++, p+=2) ch[c].pwm[r] = (uint16_t)get16(p);
    for(m=0;m<ACAL_CLASSES;m++) for(k=0;k<2;k++) for(r=0;r<ACAL_RANGES;r++, p+=2) ch[c].zero[m][k][r] = (int16_t)get16(p);
    for(m=0;m<ACAL_CLASSES;m++) for(k=0;k<2;k++) for(r=0;r<ACAL_RANGES;r++, p+=2) ch[c].slope[m][k][r] = (int16_t)get16(p);
    for(m=0;m<ACAL_CLASSES;m++) for(k=0;k<2;k++) for(r=0;r<ACAL_RANGES;r++) ch[c].noise[m][k][r] = *p++;
    for(m=0;m<ACAL_CLASSES;m++) for(k=0;k<2;k++, p+=2) ch[c].gain[m][k] = (uint16_t)get16(p);
    ch[c].gain_range = *p++;
    ch[c].gain_flags = *p++;
    ch[c].gain_lever = (uint16_t)get16(p);
    p += 2;
  }

  return ACAL_OK;
}

int acal_bound(const acal_channel *c, const uint16_t *cal_offset)
{
  uint32_t r;

  for(r=0;r<ACAL_RANGES;r++)
    if(c->pwm[r] != cal_offset[r])
      return 0;

  return 1;
}

uint32_t acal_gain16(const acal_channel *c, uint32_t cls, uint32_t k)
{
  return (c->gain_range == ACAL_GAIN_NONE) ? 65536u : 2u * c->gain[cls][k];
}

//----------------------------------------------------------------------------------------------------------------------------------
//Correction

void acal_round_pair(int32_t da16, int32_t db16, int16_t *comp_a, int16_t *comp_b)
{
  //k = the A - B correction in whole codes, s = comp_a + comp_b with the parity of k, nearest to (dA + dB) / 16
  int32_t k = rdiv(da16 - db16, 16);
  int32_t s = k + 2 * rdiv(da16 + db16 - 16 * k, 32);
  int32_t a = (s + k) / 2;
  int32_t b = (s - k) / 2;

  *comp_a = (int16_t)((a > 127) ? 127 : ((a < -127) ? -127 : a));
  *comp_b = (int16_t)((b > 127) ? 127 : ((b < -127) ? -127 : b));
}

//offset term slope (Q16 codes per count) x dcoffset in 1/16 code
static int32_t acal_off16(int32_t slope, int32_t dcoffset)
{
  return (int32_t)rdiv64((int64_t)slope * dcoffset, 4096);
}

void acal_correction(const acal_channel *c, const uint16_t *cal_offset, uint8_t range, uint32_t divider,
                     int16_t dcoffset, int32_t dc_shift_value, int16_t atlan_a, int16_t atlan_b, acal_corr *out)
{
  uint32_t hr  = (range < ACAL_RANGES) ? range : ACAL_RANGES - 1;
  uint32_t cls = divider ? ACAL_GATED : ACAL_FULL;
  int32_t  t16, zc16, zf16[2], d16, k, lo[2], hi[2];
  uint32_t g, gf[2];
  int16_t  comp[2];
  uint8_t  frac[2];
  uint32_t gain[2];
  int32_t  off[2];

  out->shift = dc_shift_value ? ((int32_t)dcoffset * 100) / dc_shift_value : 0;
  out->cls   = (uint8_t)cls;

  if(c && acal_bound(c, cal_offset))
  {
    t16 = (128 - out->shift) * 16;

    for(k=0;k<2;k++)
    {
      //0 V of core k in this clock class and in the full class (16-bit path), 1/16 code
      zc16    = c->zero[cls][k][hr] + acal_off16(c->slope[cls][k][hr], dcoffset);
      zf16[k] = c->zero[ACAL_FULL][k][hr] + acal_off16(c->slope[ACAL_FULL][k][hr], dcoffset);

      //8-bit readout: mean code = T + G (raw - z) = (raw x gain + off) / 65536
      g       = acal_gain16(c, cls, k);
      gf[k]   = acal_gain16(c, ACAL_FULL, k);
      gain[k] = g;
      off[k]  = (int32_t)rdiv64((int64_t)t16 * 65536 - (int64_t)g * zc16, 16);
      lo[k]   = (int32_t)((int64_t)off[k] >> 16);
      hi[k]   = (int32_t)(((int64_t)255 * g + off[k]) >> 16);

      //the correction at the core's 0 V point, whole codes and sixteenths (GET_CAL, diagnostics)
      d16 = t16 - zc16;
      d16 = (d16 > 127 * 16) ? 127 * 16 : ((d16 < -127 * 16) ? -127 * 16 : d16);

      comp[k] = (int16_t)floor16(d16);
      frac[k] = (uint8_t)(d16 - 16 * comp[k]);
    }

    //16-bit path: both cores weigh equally, so the mean gain is g_ref (1 / G_A + 1 / G_B) / 2: correct with the
    //harmonic mean of the two gains
    out->gain16  = (uint32_t)((2ull * gf[0] * gf[1] + (gf[0] + gf[1]) / 2) / (gf[0] + gf[1]));
    out->corr16  = (int32_t)rdiv64((int64_t)t16 * 131072 - (int64_t)out->gain16 * (zf16[0] + zf16[1]), 131072);
    out->zero_q8 = t16 * 16;
    out->version = 3;
  }
  else
  {
    comp[0] = atlan_a;
    comp[1] = atlan_b;
    frac[0] = frac[1] = 0;

    for(k=0;k<2;k++)
    {
      gain[k] = 65536;
      off[k]  = (int32_t)comp[k] * 65536;
      lo[k]   = comp[k];
      hi[k]   = 255 + comp[k];
    }

    out->gain16  = 65536;
    out->zero_q8 = dc_shift_value ? 128 * 256 - ((int32_t)dcoffset * 25600) / dc_shift_value : 128 * 256;
    out->corr16  = ((int32_t)atlan_a + atlan_b) * 8;
    out->version = 1;
  }

  out->comp_a  = comp[0];
  out->comp_b  = comp[1];
  out->frac_a  = frac[0];
  out->frac_b  = frac[1];
  out->gain_a  = gain[0];
  out->gain_b  = gain[1];
  out->off_a   = off[0];
  out->off_b   = off[1];
  out->clip_lo = (lo[0] > lo[1]) ? lo[0] : lo[1];
  out->clip_hi = (hi[0] < hi[1]) ? hi[0] : hi[1];
}

uint32_t acal_scale(uint32_t scale_q24, uint32_t gain16)
{
  return (uint32_t)(((uint64_t)scale_q24 * gain16 + 32768) >> 16);
}

void acal_atlan_comps(const acal_channel *c, int16_t *comp_a, int16_t *comp_b)
{
  int32_t  sa = 0, sb = 0;
  uint32_t r;

  for(r=0;r<ACAL_RANGES;r++)
  {
    sa += c->zero[ACAL_GATED][ACAL_CORE_A][r];
    sb += c->zero[ACAL_GATED][ACAL_CORE_B][r];
  }

  acal_round_pair(2048 - rdiv(sa, ACAL_RANGES), 2048 - rdiv(sb, ACAL_RANGES), comp_a, comp_b);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Measurement

void acal_acc_add(acal_acc *a, const uint8_t *buf, uint32_t count, uint32_t stride)
{
  uint32_t sum = 0, sq = 0, clip = 0, v;

  a->n += count;

  //sq stays below 2^32 for count <= 66,000 (255^2 x count); callers pass at most a few thousand
  while(count--)
  {
    v     = *buf;
    buf  += stride;
    sum  += v;
    sq   += v * v;
    clip += (v == 0) || (v == 255);
  }

  a->sum     += sum;
  a->sumsq   += sq;
  a->clipped += clip;
}

void acal_acc_merge(acal_acc *a, const acal_acc *b)
{
  a->n       += b->n;
  a->sum     += b->sum;
  a->sumsq   += b->sumsq;
  a->clipped += b->clipped;
}

int32_t acal_mean16(const acal_acc *a)
{
  return a->n ? (int32_t)(((uint64_t)a->sum * 16 + a->n / 2) / a->n) : 0;
}

uint32_t acal_rms16(const acal_acc *a)
{
  uint64_t n = a->n, x, r = 0, bit = (uint64_t)1 << 62;
  uint64_t s2;

  if(n < 2)
    return 0;

  //256 x variance = 256 x (n sumsq - sum^2) / n^2, then the integer square root
  s2 = (uint64_t)a->sum * a->sum;
  x  = (n * a->sumsq > s2) ? ((n * a->sumsq - s2) * 256 + n * n / 2) / (n * n) : 0;

  while(bit > x)
    bit >>= 2;

  while(bit)
  {
    if(x >= r + bit)
    {
      x -= r + bit;
      r  = (r >> 1) + bit;
    }
    else
      r >>= 1;

    bit >>= 2;
  }

  return (uint32_t)r;
}

int32_t acal_interp_pwm(int32_t pwm_lo, int32_t mean16_lo, int32_t pwm_hi, int32_t mean16_hi)
{
  int32_t resp = mean16_lo - mean16_hi;

  return (resp > 0) ? pwm_lo + rdiv((mean16_lo - 2048) * (pwm_hi - pwm_lo), resp) : pwm_lo;
}

int32_t acal_centre_pwm(int32_t pwm_lo, int32_t mean16_lo, int32_t pwm_hi, int32_t mean16_hi)
{
  int32_t p;

  if(((mean16_lo - mean16_hi) < ACAL_MIN_RESPONSE) || (pwm_hi <= pwm_lo))
    return -1;

  p = acal_interp_pwm(pwm_lo, mean16_lo, pwm_hi, mean16_hi);

  return ((p < ACAL_PWM_MIN) || (p > ACAL_PWM_MAX)) ? -1 : p;
}

int acal_range_store(const acal_range_meas *m, acal_channel *c, uint8_t r)
{
  int32_t  slope[ACAL_CLASSES][2];
  uint32_t k, cl;

  if(r >= ACAL_RANGES)
    return ACAL_E_ZERO;

  if(m->clipped)
    return ACAL_E_CLIP;

  if(m->pwm_hi <= m->pwm_lo)
    return ACAL_E_SLOPE;

  for(cl=0;cl<ACAL_CLASSES;cl++)
  {
    for(k=0;k<2;k++)
    {
      if(m->noise16[cl][k] > ACAL_MAX_NOISE16)
        return ACAL_E_NOISE;

      if(iabs(m->drift16[cl][k]) > ACAL_MAX_DRIFT16)
        return ACAL_E_DRIFT;

      //(1/16 code) / count -> Q16: x 4096
      slope[cl][k] = rdiv((m->mean16_hi[cl][k] - m->mean16_lo[cl][k]) * 4096, m->pwm_hi - m->pwm_lo);

      if((slope[cl][k] < ACAL_SLOPE_MIN_Q16) || (slope[cl][k] > ACAL_SLOPE_MAX_Q16))
        return ACAL_E_SLOPE;
    }

    if(iabs(slope[cl][0] - slope[cl][1]) > ACAL_MAX_DSLOPE_Q16)
      return ACAL_E_SLOPE;
  }

  for(cl=0;cl<ACAL_CLASSES;cl++)
  {
    for(k=0;k<2;k++)
      if(iabs(m->zero16[cl][k] - 2048) > ACAL_MAX_ZERO_DEV16)
        return ACAL_E_ZERO;

    if(iabs(m->zero16[cl][0] - m->zero16[cl][1]) > ACAL_MAX_SPREAD16)
      return ACAL_E_SPREAD;
  }

  for(k=0;k<2;k++)
    if(iabs(m->zero16[ACAL_FULL][k] - m->zero16[ACAL_GATED][k]) > ACAL_MAX_CLASS16)
      return ACAL_E_CLASS;

  c->pwm[r] = (uint16_t)m->pwm;

  for(cl=0;cl<ACAL_CLASSES;cl++)
  {
    for(k=0;k<2;k++)
    {
      c->slope[cl][k][r] = (int16_t)slope[cl][k];
      c->zero[cl][k][r]  = (int16_t)m->zero16[cl][k];
      c->noise[cl][k][r] = (uint8_t)m->noise16[cl][k];
    }
  }

  return ACAL_OK;
}

//----------------------------------------------------------------------------------------------------------------------------------
//Gain step

//Nominal sensitivity per hardware range (5, 2.5, 1, 0.5, 0.2, 0.1 V/div)
static const int32_t acal_sens[ACAL_RANGES] = { 1, 2, 5, 10, 25, 50 };

uint8_t acal_gain_range(int32_t span16_probe)
{
  int32_t r;

  if(span16_probe <= 0)
    return ACAL_GAIN_PROBE_RANGE;

  for(r=ACAL_RANGES-1;r>0;r--)
    if((int64_t)span16_probe * acal_sens[r] <= (int64_t)ACAL_GAIN_MAX_SPAN16 * acal_sens[ACAL_GAIN_PROBE_RANGE])
      break;

  return (uint8_t)r;
}

int32_t acal_gain_mid_at(int32_t mid16_probe, uint8_t r)
{
  return rdiv(mid16_probe * acal_sens[(r < ACAL_RANGES) ? r : ACAL_RANGES - 1], acal_sens[ACAL_GAIN_PROBE_RANGE]);
}

int32_t acal_gain_pwm(const acal_channel *c, uint8_t r, int32_t off16)
{
  int32_t s, p;

  if(r >= ACAL_RANGES)
    r = ACAL_RANGES - 1;

  //mean gated slope of both cores, Q16 codes per count = 1/4096 sixteenths per count (negative)
  s = ((int32_t)c->slope[ACAL_GATED][0][r] + c->slope[ACAL_GATED][1][r]) / 2;
  p = c->pwm[r];

  if(s < 0)
    p += (int32_t)rdiv64((int64_t)off16 * 4096, -s);

  return (p < ACAL_PWM_MIN) ? ACAL_PWM_MIN : ((p > ACAL_PWM_MAX) ? ACAL_PWM_MAX : p);
}

int acal_gain_store(const acal_gain_meas *m, uint8_t r, acal_channel *c)
{
  int32_t  span2[ACAL_CLASSES][2], ref2, g[ACAL_CLASSES][2], ratio;
  uint32_t k, cl;

  if(m->clipped)
    return ACAL_E_GAIN_CLIP;

  for(cl=0;cl<ACAL_CLASSES;cl++)
    for(k=0;k<2;k++)
    {
      if(iabs(m->mean16[cl][2][k] - m->mean16[cl][0][k]) > ACAL_GAIN_MAX_DRIFT16)
        return ACAL_E_GAIN_DRIFT;

      //high minus the mean of the two lows (linear drift cancels), 1/32 code
      span2[cl][k] = 2 * m->mean16[cl][1][k] - m->mean16[cl][0][k] - m->mean16[cl][2][k];
    }

  //reference: the gated span of the mean of both cores (1/32 code)
  ref2 = (span2[ACAL_GATED][0] + span2[ACAL_GATED][1]) / 2;

  for(cl=0;cl<ACAL_CLASSES;cl++)
    for(k=0;k<2;k++)
      if(span2[cl][k] < 2 * ACAL_GAIN_MIN_LEVER16)
        return ACAL_E_GAIN_LEVER;

  for(cl=0;cl<ACAL_CLASSES;cl++)
  {
    for(k=0;k<2;k++)
      g[cl][k] = (int32_t)rdiv64((int64_t)ref2 * ACAL_GAIN_ONE, span2[cl][k]);

    if(iabs(g[cl][0] - g[cl][1]) > ACAL_GAIN_MAX_SPLIT)
      return ACAL_E_GAIN_SPLIT;
  }

  ratio = (int32_t)rdiv64((int64_t)(g[ACAL_FULL][0] + g[ACAL_FULL][1]) * ACAL_GAIN_ONE, g[ACAL_GATED][0] + g[ACAL_GATED][1]);

  if((ratio < ACAL_GAIN_CLASS_MIN) || (ratio > ACAL_GAIN_CLASS_MAX))
    return ACAL_E_GAIN_CLASS;

  for(cl=0;cl<ACAL_CLASSES;cl++)
    for(k=0;k<2;k++)
      c->gain[cl][k] = (uint16_t)g[cl][k];

  c->gain_range = r;
  c->gain_flags = 0;
  c->gain_lever = (uint16_t)rdiv(ref2, 2);

  return ACAL_OK;
}

void acal_gain_carry(const acal_channel *old, acal_channel *c)
{
  uint32_t k, cl;

  if(old && (old->gain_range != ACAL_GAIN_NONE))
  {
    for(cl=0;cl<ACAL_CLASSES;cl++)
      for(k=0;k<2;k++)
        c->gain[cl][k] = old->gain[cl][k];

    c->gain_range = old->gain_range;
    c->gain_flags = old->gain_flags | ACAL_GAIN_KEPT;
    c->gain_lever = old->gain_lever;
    return;
  }

  for(cl=0;cl<ACAL_CLASSES;cl++)
    for(k=0;k<2;k++)
      c->gain[cl][k] = ACAL_GAIN_ONE;

  c->gain_range = ACAL_GAIN_NONE;
  c->gain_flags = 0;
  c->gain_lever = 0;
}

const char *acal_reason_text(int reason)
{
  switch(reason)
  {
    case ACAL_OK:           return "OK";
    case ACAL_E_RESPONSE:   return "offset not responding";
    case ACAL_E_CLIP:       return "input clips: unplug";
    case ACAL_E_NOISE:      return "noisy: unplug probe";
    case ACAL_E_ZERO:       return "zero off centre";
    case ACAL_E_SPREAD:     return "ADC cores differ > 20";
    case ACAL_E_SLOPE:      return "offset slope wrong";
    case ACAL_E_DRIFT:      return "zero not settled";
    case ACAL_E_CLASS:      return "zero moves w/ clock";
    case ACAL_E_GAIN_CLIP:  return "gain: level clips";
    case ACAL_E_GAIN_DRIFT: return "gain: level drifts";
    case ACAL_E_GAIN_LEVER: return "gain: probe on post?";
    case ACAL_E_GAIN_SPLIT: return "gain: cores differ";
    case ACAL_E_GAIN_CLASS: return "gain: clock classes";
    case ACAL_E_GAIN_NONE:  return "gain: no generator";
    default:                return "no calibration data";
  }
}
