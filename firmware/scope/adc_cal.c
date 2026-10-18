//----------------------------------------------------------------------------------------------------------------------------------
// adc_cal.c - per-core ADC zero calibration maths and the sector 708 block (adc_cal.h, firmware/README.md section 6).
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

void acal_pack(const acal_channel ch[2], uint8_t *block)
{
  uint8_t *p = block;
  uint32_t c, k, r, m;

  p = put32(p, ACAL_MAGIC);
  p = put16(p, ACAL_VERSION);
  p = put16(p, ACAL_BLOCK_BYTES);
  p = put16(p, ACAL_CLASSES);
  p = put16(p, 0);

  for(c=0;c<2;c++)
  {
    for(r=0;r<ACAL_RANGES;r++) p = put16(p, ch[c].pwm[r]);
    for(m=0;m<ACAL_CLASSES;m++) for(k=0;k<2;k++) for(r=0;r<ACAL_RANGES;r++) p = put16(p, (uint16_t)ch[c].zero[m][k][r]);
    for(k=0;k<2;k++) for(r=0;r<ACAL_RANGES;r++) p = put32(p, (uint32_t)ch[c].slope[k][r]);
    for(m=0;m<ACAL_CLASSES;m++) for(k=0;k<2;k++) for(r=0;r<ACAL_RANGES;r++) p = put16(p, ch[c].noise[m][k][r]);
  }

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
    for(k=0;k<2;k++) for(r=0;r<ACAL_RANGES;r++, p+=4) ch[c].slope[k][r] = (int32_t)get32(p);
    for(m=0;m<ACAL_CLASSES;m++) for(k=0;k<2;k++) for(r=0;r<ACAL_RANGES;r++, p+=2) ch[c].noise[m][k][r] = (uint16_t)get16(p);
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

void acal_correction(const acal_channel *c, const uint16_t *cal_offset, uint8_t range, uint32_t divider,
                     int16_t dcoffset, int32_t dc_shift_value, int16_t atlan_a, int16_t atlan_b, acal_corr *out)
{
  uint32_t hr  = (range < ACAL_RANGES) ? range : ACAL_RANGES - 1;
  uint32_t cls = divider ? ACAL_GATED : ACAL_FULL;
  int32_t  t16, off16, zf16[2], d16, k;
  int16_t  comp[2];
  uint8_t  frac[2];

  out->shift = dc_shift_value ? ((int32_t)dcoffset * 100) / dc_shift_value : 0;
  out->cls   = (uint8_t)cls;

  if(c && acal_bound(c, cal_offset))
  {
    t16 = (128 - out->shift) * 16;

    for(k=0;k<2;k++)
    {
      //offset term of core k, 1/16 code
      off16    = (int32_t)rdiv64((int64_t)c->slope[k][hr] * dcoffset, 4096);
      zf16[k]  = c->zero[ACAL_FULL][k][hr] + off16;

      //8-bit readout: correction to T for this clock class, split into whole codes and sixteenths
      d16 = t16 - (c->zero[cls][k][hr] + off16);
      d16 = (d16 > 127 * 16) ? 127 * 16 : ((d16 < -127 * 16) ? -127 * 16 : d16);

      comp[k] = (int16_t)floor16(d16);
      frac[k] = (uint8_t)(d16 - 16 * comp[k]);
    }

    out->comp_a  = comp[0];
    out->comp_b  = comp[1];
    out->frac_a  = frac[0];
    out->frac_b  = frac[1];
    out->zero_q8 = t16 * 16;
    out->corr16  = rdiv(2 * t16 - zf16[0] - zf16[1], 2);
    out->v2      = 1;
  }
  else
  {
    out->comp_a  = atlan_a;
    out->comp_b  = atlan_b;
    out->frac_a  = 0;
    out->frac_b  = 0;
    out->zero_q8 = dc_shift_value ? 128 * 256 - ((int32_t)dcoffset * 25600) / dc_shift_value : 128 * 256;
    out->corr16  = ((int32_t)atlan_a + atlan_b) * 8;
    out->v2      = 0;
  }
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
  int32_t  slope[2];
  uint32_t k, cl;

  if(r >= ACAL_RANGES)
    return ACAL_E_ZERO;

  if(m->clipped)
    return ACAL_E_CLIP;

  if(m->pwm_hi <= m->pwm_lo)
    return ACAL_E_SLOPE;

  for(k=0;k<2;k++)
  {
    for(cl=0;cl<ACAL_CLASSES;cl++)
    {
      if(m->noise16[cl][k] > ACAL_MAX_NOISE16)
        return ACAL_E_NOISE;

      if(iabs(m->drift16[cl][k]) > ACAL_MAX_DRIFT16)
        return ACAL_E_DRIFT;
    }

    //(1/16 code) / count -> Q16: x 4096
    slope[k] = rdiv((m->mean16_hi[k] - m->mean16_lo[k]) * 4096, m->pwm_hi - m->pwm_lo);

    if((slope[k] < ACAL_SLOPE_MIN_Q16) || (slope[k] > ACAL_SLOPE_MAX_Q16))
      return ACAL_E_SLOPE;
  }

  if(iabs(slope[0] - slope[1]) > ACAL_MAX_DSLOPE_Q16)
    return ACAL_E_SLOPE;

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

  for(k=0;k<2;k++)
  {
    c->slope[k][r] = slope[k];

    for(cl=0;cl<ACAL_CLASSES;cl++)
    {
      c->zero[cl][k][r]  = (int16_t)m->zero16[cl][k];
      c->noise[cl][k][r] = (uint16_t)m->noise16[cl][k];
    }
  }

  return ACAL_OK;
}

const char *acal_reason_text(int reason)
{
  switch(reason)
  {
    case ACAL_OK:         return "OK";
    case ACAL_E_RESPONSE: return "offset not responding";
    case ACAL_E_CLIP:     return "input clips: unplug";
    case ACAL_E_NOISE:    return "noisy: unplug probe";
    case ACAL_E_ZERO:     return "zero off centre";
    case ACAL_E_SPREAD:   return "ADC cores differ > 20";
    case ACAL_E_SLOPE:    return "offset slope wrong";
    case ACAL_E_DRIFT:    return "zero not settled";
    case ACAL_E_CLASS:    return "zero moves w/ clock";
    default:              return "no calibration data";
  }
}
