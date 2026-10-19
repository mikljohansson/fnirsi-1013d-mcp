//----------------------------------------------------------------------------------------------------------------------------------
// enc_heal.c - the full-rate capture check and its re-arm bookkeeping (enc_heal.h)
//----------------------------------------------------------------------------------------------------------------------------------

#include "enc_heal.h"

int eh_mode(uint16_t fpga_version)
{
  if((fpga_version >= 0x1532) && (fpga_version <= 0x1537))
    return EH_MODE_DATA;

  if((fpga_version >= 0x1538) && (fpga_version <= 0x153F))
    return EH_MODE_FLAG;

  return EH_MODE_NONE;
}

int eh_affected(uint16_t fpga_version)
{
  return (fpga_version >= 0x1532) && (fpga_version <= 0x1536);
}

int eh_order_differs(uint16_t fpga_version)
{
  return fpga_version == 0x1537;
}

int eh_conv_ok(uint8_t s33)
{
  return (s33 & EH_33_CONV_MASK) == EH_33_CONV_1;
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

int eh_check(const eh_io *io, int mode, uint32_t start, uint32_t words, uint32_t *value)
{
  uint8_t  buf[2 * EH_CHECK_PAIRS];
  uint32_t ch, r, worst = 0;

  if(mode == EH_MODE_FLAG)
  {
    uint8_t s = io->status(io->ctx);

    *value = s;
    return (s & EH_33_FAILED) != 0;
  }

  if(words > EH_CHECK_PAIRS)
    words = EH_CHECK_PAIRS;

  //Trace layout: core B (0x21 / 0x23) even, core A (0x20 / 0x22) odd
  for(ch=0;ch<2;ch++)
  {
    io->read(io->ctx, (uint8_t)(2 * ch + 1), start, words, buf, 2);
    io->read(io->ctx, (uint8_t)(2 * ch), start, words, buf + 1, 2);
    r = eh_residual16(buf, words);

    if(r > worst)
      worst = r;
  }

  *value = worst;
  return worst > EH_LIMIT16;
}

void eh_cap_reset(eh_cap *c)
{
  c->tries = 0;
  c->first = 0;
}

int eh_cap_step(eh_cap *c, int bad, uint32_t value, eh_note *n)
{
  n->bad   = (uint8_t)(bad != 0);
  n->first = (uint8_t)(bad && (c->tries == 0));
  n->done  = 0;
  n->ok    = 0;
  n->tries = c->tries;
  n->value = c->tries ? c->first : value;

  if(bad && (c->tries == 0))
    c->first = value;

  if(bad && (c->tries < EH_MAX_TRIES))
  {
    c->tries++;
    return EH_REARM;
  }

  //Accepted: after failures, an episode for the event log
  if(bad || c->tries)
  {
    n->done = 1;
    n->ok   = (uint8_t)!bad;
  }

  eh_cap_reset(c);
  return EH_ACCEPT;
}

uint8_t eh_event_a(uint8_t why, uint8_t tries, int ok)
{
  return (uint8_t)(((why & 3) << 5) | ((tries > 31) ? 31 : tries) | (ok ? 0 : 0x80));
}

uint16_t eh_event_b(uint8_t who, uint32_t value)
{
  return (uint16_t)(((uint32_t)(who & 15) << 12) | ((value > 0xFFF) ? 0xFFF : value));
}
