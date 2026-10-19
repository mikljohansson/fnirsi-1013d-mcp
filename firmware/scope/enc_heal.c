//----------------------------------------------------------------------------------------------------------------------------------
// enc_heal.c - check and heal of the AL3 0x1532..0x1536 encode-phase race (enc_heal.h)
//----------------------------------------------------------------------------------------------------------------------------------

#include "enc_heal.h"

int eh_affected(uint16_t fpga_version)
{
  return (fpga_version >= 0x1532) && (fpga_version <= 0x1536);
}

static uint32_t eh_isqrt(uint64_t x)
{
  uint64_t r = 0, bit = (uint64_t)1 << 62;

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

//d = 2 B - A1 - A2 in half codes; 16 x RMS(d / 2) about its mean = 8 x sqrt(var(d))
static uint32_t eh_rms(const uint8_t *buf, uint32_t n, int later)
{
  int64_t  s = 0;
  uint64_t q = 0;
  uint32_t i, m = 0;
  int32_t  d;

  for(i=1;i+1<n;i++)
  {
    d  = 2 * (int32_t)buf[2 * i] - buf[2 * i + 1] - (later ? buf[2 * i + 3] : buf[2 * i - 1]);
    s += d;
    q += (uint64_t)((int64_t)d * d);
    m++;
  }

  if(m < 2)
    return 0;

  //64 x var(d) = 64 x (m q - s^2) / m^2
  return eh_isqrt((((uint64_t)m * q - (uint64_t)(s * s)) * 64 + (uint64_t)m * m / 2) / ((uint64_t)m * m));
}

uint32_t eh_residual16(const uint8_t *buf, uint32_t n)
{
  uint32_t e = eh_rms(buf, n, 0), l = eh_rms(buf, n, 1);

  return (e < l) ? e : l;
}

static int eh_pass(const uint32_t res16[2])
{
  return (res16[0] <= EH_LIMIT16) && (res16[1] <= EH_LIMIT16);
}

int eh_run(const eh_ops *o, eh_result *r)
{
  r->tries = 0;
  o->capture(o->ctx, r->res16);
  r->first16 = (r->res16[0] > r->res16[1]) ? r->res16[0] : r->res16[1];
  r->bad = !eh_pass(r->res16);
  r->ok  = !r->bad;

  while(!r->ok && (r->tries < EH_MAX_TRIES))
  {
    o->retry(o->ctx);
    r->tries++;
    o->capture(o->ctx, r->res16);
    r->ok = eh_pass(r->res16);
  }

  return r->ok;
}
