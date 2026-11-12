//----------------------------------------------------------------------------------------------------------------------------------
// disp_rate.c - the time scale of the record on screen (see disp_rate.h). Host-tested: host_tests/test_ui.c.
//----------------------------------------------------------------------------------------------------------------------------------

#include "disp_rate.h"
#include "ui_fmt.h"

static uint32 disp_rec_div;           //the record's 0x0D divider
static uint32 disp_rec_p5;            //its sample period in 5 ns units, 0 = none noted

void disp_rate_note(uint32 divider, uint32 n)
{
  disp_rec_div = divider;
  disp_rec_p5  = n ? n : divider + 1;
}

void disp_rate_forget(void)
{
  disp_rec_div = 0;
  disp_rec_p5  = 0;
}

int disp_rate_noted(uint32 *divider, uint32 *n)
{
  *divider = disp_rec_div;
  *n       = (disp_rec_p5 != disp_rec_div + 1) ? disp_rec_p5 : 0;
  return disp_rec_p5 != 0;
}

uint32 disp_rate_p5(void)
{
  return disp_rec_p5;
}

uint32 disp_rate_divider(uint32 ui_divider)
{
  return disp_rec_p5 ? disp_rec_div : ui_divider;
}

int disp_rate_foreign(uint32 ui_divider)
{
  return disp_rec_p5 && (disp_rec_p5 != ui_divider + 1);
}

uint64 disp_rate_scale_ps(uint64 ps, uint32 ui_divider)
{
  uint64 den = (uint64)ui_divider + 1;

  if(!disp_rec_p5)
    return ps;

  //ps x p5 within 64 bits for every time the screen shows (800 px of 200 ms/div x a 2^20 divider is 3.4e18)
  if(ps > 0xFFFFFFFFFFFFFFFFull / disp_rec_p5)
    return (ps / den) * disp_rec_p5;

  return (ps * disp_rec_p5 + den / 2) / den;
}

uint64 disp_rate_samples_ps(uint32 samples_q16)
{
  uint64 v = (uint64)samples_q16 * disp_rec_p5;        //< 2^53

  if(v > 0xFFFFFFFFFFFFFFFFull / DISP_RATE_PS_UNIT)
    return (v >> 16) * DISP_RATE_PS_UNIT;

  return (v * DISP_RATE_PS_UNIT + 0x8000) >> 16;
}

uint64 disp_rate_freq_mhz(uint32 period_q16)
{
  //65536 x 200 MHz in mHz over (Q16 samples x the period in 5 ns units)
  uint64 den = (uint64)period_q16 * disp_rec_p5;

  if(!den)
    return 0;

  return (13107200000000000ull + den / 2) / den;
}

//Copy s to p, dropping the trailing zeros of a fraction (and its point when nothing is left of it)
static char *disp_trim(char *p, const char *s)
{
  const char *dot = 0, *end;

  for(end = s; *end && (((*end >= '0') && (*end <= '9')) || (*end == '.') || (*end == '-')); end++)
    if(*end == '.')
      dot = end;

  if(dot)
  {
    const char *last = end;

    while((last > dot + 1) && (last[-1] == '0'))
      last--;
    if(last == dot + 1)
      last = dot;

    while(s < last)
      *p++ = *s++;
    s = end;
  }

  while(*s)
    *p++ = *s++;

  *p = 0;
  return p;
}

//v rounded to three significant digits (half up): the ACQ chip's text column holds "200ms/div" and "66.7MSa/s"
static uint64 disp_sig3(uint64 v)
{
  uint64 p10 = 1;

  while(v >= 1000 * p10)
    p10 *= 10;

  return ((v + p10 / 2) / p10) * p10;
}

static char *disp_put(char *p, const char *s)
{
  while(*s)
    *p++ = *s++;

  *p = 0;
  return p;
}

char *disp_rate_tdiv_text(char *p, uint64 tdiv_ps)
{
  char t[24];

  ui_fmt_ps(t, disp_sig3(tdiv_ps));
  p = disp_trim(p, t);
  return disp_put(p, "/div");
}

char *disp_rate_rate_text(char *p)
{
  char  t[24], u[24];
  const char *s;

  if(!disp_rec_p5)
    return disp_put(p, "--");

  //200 MHz / p5 in mHz, "20.00MHz" -> "20MHz", then the chip's spelling: "Sa/s" for "Hz", "K" for "k"
  ui_fmt_mhz(t, disp_sig3((200000000000ull + disp_rec_p5 / 2) / disp_rec_p5));
  disp_trim(u, t);

  for(s = u; *s && (*s != 'H'); s++)
    *p++ = (*s == 'k') ? 'K' : *s;

  *p = 0;
  return disp_put(p, "Sa/s");
}
