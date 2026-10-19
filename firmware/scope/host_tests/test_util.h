//----------------------------------------------------------------------------------------------------------------------------------
// Minimal assertion helpers for the host tests (no framework dependency)
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int t_checks = 0;
static int t_failures = 0;

#define T_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); t_checks++; \
  if(_a != _b) { t_failures++; printf("%s:%d: FAIL %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while(0)

#define T_TRUE(a) T_EQ(!!(a), 1)
#define T_FALSE(a) T_EQ(!!(a), 0)

#define T_MEM(a, b, n) do { t_checks++; if(memcmp((a), (b), (n))) { t_failures++; \
  printf("%s:%d: FAIL memory %s != %s (%u bytes)\n", __FILE__, __LINE__, #a, #b, (unsigned)(n)); } } while(0)

#define T_STR(a, b) do { t_checks++; if(strcmp((a), (b))) { t_failures++; \
  printf("%s:%d: FAIL \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while(0)

static inline int t_report(const char *name)
{
  printf("%s: %d checks, %d failures\n", name, t_checks, t_failures);
  return t_failures ? 1 : 0;
}

//Hex string to bytes, spaces ignored. Returns the byte count.
static inline int unhex(const char *s, uint8_t *out, int max)
{
  int n = 0, hi = -1;

  for(; *s; s++)
  {
    int v;
    if(*s >= '0' && *s <= '9') v = *s - '0';
    else if(*s >= 'a' && *s <= 'f') v = *s - 'a' + 10;
    else if(*s >= 'A' && *s <= 'F') v = *s - 'A' + 10;
    else continue;

    if(hi < 0) hi = v;
    else { if(n < max) out[n] = (uint8_t)((hi << 4) | v); n++; hi = -1; }
  }

  return n;
}

#endif
