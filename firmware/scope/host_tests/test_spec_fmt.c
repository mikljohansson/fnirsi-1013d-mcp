// test_spec_fmt.c - the spectrum view's number formatting (spec_fmt.c), run natively.

#include <stdio.h>
#include <string.h>

#include "spec_fmt.h"
#include "test_util.h"

static char b[64];

static const char *f(unsigned long long mhz) { fmt_freq(b, mhz); return b; }
static const char *r(unsigned long long mhz) { fmt_freq_units(b, mhz, 1); return b; }
static const char *c1(int v) { fmt_c1(b, v); return b; }
static const char *si(int v) { fmt_int(b, v); return b; }

int main(void)
{
  char *p;

  T_STR(f(0), "0 Hz");
  T_STR(f(499), "0 Hz");
  T_STR(f(500), "0.500 Hz");
  T_STR(f(1000), "1.000 Hz");
  T_STR(f(12345), "12.35 Hz");
  T_STR(f(732400), "732.4 Hz");
  T_STR(f(999960), "1.000 kHz");
  T_STR(f(1000000), "1.000 kHz");
  T_STR(f(48828125), "48.83 kHz");
  T_STR(f(999999999ull), "1.000 MHz");
  T_STR(f(50000000000ull), "50.00 MHz");
  T_STR(f(100000000000ull), "100.0 MHz");
  T_STR(f(66666666666ull), "66.67 MHz");
  T_STR(f(1234567890123ull), "1235 MHz");
  T_STR(f(12345678901234ull), "12350 MHz");
  T_STR(r(200000000000ull), "200.0 MSa/s");
  T_STR(r(1000000000ull), "1.000 MSa/s");
  T_STR(r(10000000ull), "10.00 kSa/s");
  T_STR(r(0), "0 Sa/s");

  T_STR(c1(0), "0.0");
  T_STR(c1(-1234), "-12.3");
  T_STR(c1(1235), "12.4");
  T_STR(c1(-4), "0.0");
  T_STR(c1(-5), "-0.1");
  T_STR(c1(3907), "39.1");

  T_STR(si(10), "+10");
  T_STR(si(0), "0");
  T_STR(si(-20), "-20");

  p = fmt_uint(b, 4096);
  T_EQ(p - b, 4);
  p = fmt_str(p, " pts");
  T_STR(b, "4096 pts");
  T_EQ(*p, 0);

  return t_report("test_spec_fmt");
}
