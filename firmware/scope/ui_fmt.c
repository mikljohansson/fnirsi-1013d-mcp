//----------------------------------------------------------------------------------------------------------------------------------
// ui_fmt.c - text measuring and value formatting for the screens (see ui_fmt.h). Host-tested: host_tests/test_ui.c.
//----------------------------------------------------------------------------------------------------------------------------------

#include "ui_fmt.h"

static const char *ui_prefix[8] = { "p", "n", "u", "m", "", "k", "M", "G" };

//1.00o5 had "Freg", "Cycle", "Time+", "Time-", "Duty+", "Duty-" (variables.c)
const char *measurement_names[UI_MEAS_COUNT] =
{
  "Vmax", "Vmin", "Vavg", "Vrms", "Vpp", "Vp", "Freq", "Period", "+Width", "-Width", "+Duty", "-Duty"
};

uint32 ui_text_width(const FONTDATA *font, const char *s)
{
  uint32 w = 0;

  if(font->type != VARIABLE_WIDTH_FONT)
    return 0;

  for(; *s; s++)
  {
    const FONTINFORMATION *info = (const FONTINFORMATION *)font->fontinformation;
    uint16 c = (uint8)*s;

    while(info && ((c < info->first_char) || (c > info->last_char)))
      info = info->next_info;

    if(info)
      w += info->fontmetrics[c - info->first_char].width;
  }

  return w;
}

static char *ui_put(char *p, const char *s)
{
  while(*s)
    *p++ = *s++;

  *p = 0;
  return p;
}

static uint32 ui_digits(uint64 v)
{
  uint32 n = 1;

  while(v >= 10)
  {
    v /= 10;
    n++;
  }

  return n;
}

//Floor of a / 3, also for negative a
static int32 ui_floor3(int32 a)
{
  return (a >= 0) ? a / 3 : -((-a + 2) / 3);
}

static char *ui_sig4(char *p, int32 value, uint32 scale, const char *unit, int fixed)
{
  uint64 v = (value < 0) ? (uint64)(-(int64)value) : (uint64)value;
  int32  e, s, q, i, n;
  uint32 d;
  char   t[24];

  if(scale > 7)
    scale = 7;

  if(!v)
  {
    *p++ = '0';
    return ui_put(p, unit);
  }

  //The number is v x 10^e units: hundredths at the prefix of scale
  e = -2 + 3 * ((int32)scale - 4);

  //Round to four significant digits (half up); a carry to 10000 drops one digit
  d = ui_digits(v);
  if(d > 4)
  {
    uint64 p10 = 1;

    for(i = 4; i < (int32)d; i++)
      p10 *= 10;

    v  = (v + p10 / 2) / p10;
    e += (int32)d - 4;

    if(v >= 10000)
    {
      v /= 10;
      e++;
    }
  }

  //The prefix that puts the leading digit one to three places before the point
  s = fixed ? 4 : ui_floor3(e + (int32)ui_digits(v) - 1) + 4;
  if(s < 0) s = 0;
  if(s > 7) s = 7;

  q = e - 3 * (s - 4);

  if(value < 0)
    *p++ = '-';

  //The digits of v, then q zeros (q >= 0) or a point -q places from the right (with leading zeros where needed)
  n = 0;
  do { t[n++] = (char)('0' + (v % 10)); v /= 10; } while(v);

  if(q >= 0)
  {
    while(n)
      *p++ = t[--n];
    for(i = 0; i < q; i++)
      *p++ = '0';
  }
  else
  {
    while(n <= -q)
      t[n++] = '0';
    for(i = n - 1; i >= 0; i--)
    {
      *p++ = t[i];
      if(i == -q)
        *p++ = '.';
    }
  }

  p = ui_put(p, fixed ? "" : ui_prefix[s]);
  return ui_put(p, unit);
}

char *ui_fmt_sig4(char *p, int32 value, uint32 scale, const char *unit)
{
  return ui_sig4(p, value, scale, unit, 0);
}

char *ui_fmt_sig4_fixed(char *p, int32 value, const char *unit)
{
  return ui_sig4(p, value, 4, unit, 1);
}

char *ui_fmt_record_time(char *p, uint32 samples, uint64 fs_mhz)
{
  uint64 v;
  uint32 scale = 1;

  if(!fs_mhz)
    return ui_put(p, "--");

  //Hundredths of a nanosecond: samples x 1e14 / fs (samples < 2^16, so no overflow), coarser while over int32
  v = ((uint64)samples * 100000000000000ull + fs_mhz / 2) / fs_mhz;
  while(v > 0x7FFFFFFFull)
  {
    v = (v + 500) / 1000;
    scale++;
  }

  return ui_sig4(p, (int32)v, scale, "s", 0);
}

char *ui_fmt_mhz(char *p, uint64 mhz)
{
  if((mhz + 5) / 10 <= 0x7FFFFFFFull)
    return ui_sig4(p, (int32)((mhz + 5) / 10), 4, "Hz", 0);

  return ui_sig4(p, (int32)((mhz + 5000) / 10000), 5, "Hz", 0);
}

char *ui_fmt_cdb(char *p, int32 cdb, const char *unit)
{
  uint32 t = (uint32)(((cdb < 0) ? -(int64)cdb : (int64)cdb) + 5) / 10;
  char   d[12];
  int    n = 0;

  if(t && (cdb > 0))
    *p++ = '+';
  else if(t)
    *p++ = '-';

  do { d[n++] = (char)('0' + (t % 10)); t /= 10; } while(t || (n < 2));

  while(n > 1)
    *p++ = d[--n];

  *p++ = '.';
  *p++ = d[0];
  *p   = 0;

  return ui_put(p, unit);
}

uint32 ui_card_chip_w(const FONTDATA *font, const char *name)
{
  return ui_text_width(font, name) + 2 * UI_CARD_PAD;
}

int ui_card_fits(const FONTDATA *font, const char *name, const char *value)
{
  return ui_card_chip_w(font, name) + UI_CARD_GAP + ui_text_width(font, value) + UI_CARD_PAD <= UI_CARD_W;
}
