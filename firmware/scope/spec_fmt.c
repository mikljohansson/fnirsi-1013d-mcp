//----------------------------------------------------------------------------------------------------------------------------------
// spec_fmt.c - number formatting for the spectrum view (no libc). Host-tested: host_tests/test_spec_fmt.c.
//----------------------------------------------------------------------------------------------------------------------------------

#include "spec_fmt.h"


char *fmt_uint(char *p, uint32 v)
{
  char   t[11];
  int    n = 0;

  do { t[n++] = '0' + (v % 10); v /= 10; } while(v);
  while(n) *p++ = t[--n];
  *p = 0;
  return p;
}

char *fmt_str(char *p, const char *s)
{
  while(*s) *p++ = *s++;
  *p = 0;
  return p;
}

//Signed value with one decimal from centi-units: -1234 -> "-12.3"
char *fmt_c1(char *p, int32 c)
{
  uint32 a = ((uint32)((c < 0) ? -c : c) + 5) / 10;

  //No sign on a value that rounds to zero
  if((c < 0) && a)
    *p++ = '-';
  p = fmt_uint(p, a / 10);
  *p++ = '.';
  *p++ = '0' + (a % 10);
  *p = 0;
  return p;
}

char *fmt_int(char *p, int32 v)
{
  if(v < 0) { *p++ = '-'; v = -v; }
  else if(v > 0) *p++ = '+';
  return fmt_uint(p, (uint32)v);
}

//Frequency in mHz with 4 significant digits: "0 Hz", "732.4 Hz", "1.000 kHz", "50.00 MHz"; rate = 1 for "S/s" units.
//Rounds first, then picks the unit, so 999.96 Hz reads "1.000 kHz".
char *fmt_freq_units(char *p, uint64 mhz, int rate)
{
  static const char *units[2][3] = { { "Hz", "kHz", "MHz" }, { "S/s", "kS/s", "MS/s" } };
  uint64 p10 = 1, value;
  uint32 v, u = 0, e_p = 0, e_u = 3, len, i;
  int32  dec;
  char   t[12];

  if(mhz < 500)
  {
    p = fmt_str(p, "0 ");
    return fmt_str(p, units[rate][0]);
  }

  //v = mhz / p10 rounded, at most 4 digits
  while(mhz >= 10000 * p10)
  {
    p10 *= 10;
    e_p++;
  }

  v = (uint32)((mhz + p10 / 2) / p10);
  if(v >= 10000)
  {
    v /= 10;
    p10 *= 10;
    e_p++;
  }

  value = (uint64)v * p10;
  if(value >= 1000000000ull)  { u = 2; e_u = 9; }
  else if(value >= 1000000ull) { u = 1; e_u = 6; }

  fmt_uint(t, v);
  for(len = 0; t[len]; len++) {}
  dec = (int32)e_u - (int32)e_p;

  if(dec <= 0)
  {
    p = fmt_str(p, t);
    for(i = 0; i < (uint32)(-dec); i++)
      *p++ = '0';
  }
  else
  {
    //Integer part (at least "0"), point, decimals (zero padded below 1 unit)
    if((int32)len <= dec)
    {
      *p++ = '0';
      *p++ = '.';
      for(i = 0; i < (uint32)dec - len; i++)
        *p++ = '0';
      p = fmt_str(p, t);
    }
    else
    {
      for(i = 0; i < len; i++)
      {
        if(i == len - (uint32)dec)
          *p++ = '.';
        *p++ = t[i];
      }
    }
  }

  *p++ = ' ';
  return fmt_str(p, units[rate][u]);
}

char *fmt_freq(char *p, uint64 mhz) { return fmt_freq_units(p, mhz, 0); }

