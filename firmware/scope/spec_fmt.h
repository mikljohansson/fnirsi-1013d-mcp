//----------------------------------------------------------------------------------------------------------------------------------
// spec_fmt.h - number formatting for the spectrum view (no libc). Each function writes a NUL-terminated string at p
// and returns a pointer to the NUL, so calls chain.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef SPEC_FMT_H
#define SPEC_FMT_H

#include "types.h"

char *fmt_uint(char *p, uint32 v);                       //"4096"
char *fmt_str(char *p, const char *s);
char *fmt_c1(char *p, int32 c);                          //centi-units with one decimal: -1234 -> "-12.3"
char *fmt_int(char *p, int32 v);                         //signed with "+" for positive: "+10", "0", "-20"
char *fmt_freq_units(char *p, uint64 mhz, int rate);     //mHz, 4 significant digits; rate 1 = "Sa/s" units
char *fmt_freq(char *p, uint64 mhz);                     //"0 Hz", "732.4 Hz", "1.000 kHz", "50.00 MHz"

#endif
