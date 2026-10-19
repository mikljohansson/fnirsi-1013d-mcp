//----------------------------------------------------------------------------------------------------------------------------------
// bode_cli.c - command-line access to bode_dsp.c for test_bode.py (the host tool's numbers against the device maths).
//
// Reads commands from stdin, one per line, answers one line each (sweeps: the count, then one line per point):
//   P <f_mhz>                                   plan of one point
//   S <start_mhz> <stop_mhz> <points>           plan of a sweep
//   M <f_mhz> <file1> <file2> <cpr1_q24> <cpr2_q24> <zero1_q8> <zero2_q8> <volt_mdb1> <volt_mdb2>
//                                               measurement of two u16 records (little-endian, >= record samples)
//   B <f_mhz> <file>                            one-channel level (raw mdB)
//   K <n> f_mhz mag_mdb ph_mdeg ok ...          markers
//   L <v> | E <y_q32>                           bode_log2_q32 / bode_exp2_round
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bode_dsp.h"

static uint16 rec1[16384], rec2[16384];
static int32  work[16384];

static int load(const char *path, uint16 *dst, int max)
{
  FILE *f = fopen(path, "rb");
  int   n;

  if(!f)
    return -1;

  n = (int)fread(dst, 2, (size_t)max, f);
  fclose(f);
  return n;
}

static void print_plan(const bode_plan *pp)
{
  printf("%u %u %u %u %llu %u %u %u %u %u\n", pp->m, pp->n, bode_divider(pp), pp->phase_inc,
         (unsigned long long)pp->f_mhz, pp->p, pp->q, pp->record, bode_total(pp), pp->hann);
}

int main(void)
{
  static char line[65536];
  static bode_plan plans[BODE_MAX_POINTS];

  while(fgets(line, sizeof(line), stdin))
  {
    char *p = line;
    char  cmd = *p++;

    if(cmd == 'P')
    {
      bode_plan pp;
      unsigned long long f = strtoull(p, &p, 10);

      if(bode_plan_point(f, &pp))
        printf("error\n");
      else
        print_plan(&pp);
    }
    else if(cmd == 'S')
    {
      unsigned long long a = strtoull(p, &p, 10), b = strtoull(p, &p, 10);
      int n = (int)strtol(p, &p, 10), i, k = bode_plan_sweep(a, b, n, plans, BODE_MAX_POINTS);

      printf("%d\n", k);
      for(i = 0; i < k; i++)
        print_plan(&plans[i]);
    }
    else if(cmd == 'M')
    {
      bode_plan  pp;
      bode_dft   d;
      bode_meas  m;
      bode_scale s[2];
      int32      zc[2];
      uint32     uv[2] = { 256 * 1000, 256 * 1000 };
      char       f1[1024], f2[1024];
      unsigned long long f;
      long       c1, c2, z1, z2, v1, v2;

      if(sscanf(p, "%llu %1023s %1023s %ld %ld %ld %ld %ld %ld", &f, f1, f2, &c1, &c2, &z1, &z2, &v1, &v2) != 9 ||
         bode_plan_point(f, &pp) || (load(f1, rec1, 16384) < pp.record) || (load(f2, rec2, 16384) < pp.record))
      {
        printf("error\n");
        fflush(stdout);
        continue;
      }

      memset(s, 0, sizeof(s));
      s[0].cpr_q24 = (uint32)c1;  s[1].cpr_q24 = (uint32)c2;
      s[0].zero_q8 = (int32)z1;   s[1].zero_q8 = (int32)z2;
      s[0].volt_mdb = (int32)v1;  s[1].volt_mdb = (int32)v2;
      zc[0] = zc[1] = 12800;

      bode_dft_run(&pp, rec1, rec2, work, &d);
      bode_measure(&pp, &d, s, zc, uv, &m);

      printf("%d %d %d %d %d %d %d %d %d %d %d %u %d %d %d %d %d %d %d %d %d %d\n", m.h_mdb, m.h_mdeg, m.c[0].lvl_mdb,
             m.c[1].lvl_mdb, m.c[0].noise_mdb, m.c[1].noise_mdb, m.leak_mdb, m.snr2_mdb, m.half_ok, m.half_mdb,
             m.half_mdeg, m.half_rel_ppm, bode_unsettled(&m), m.c[0].ac_c100, m.c[1].ac_c100, m.c[0].centre_c100,
             m.c[1].centre_c100, m.c[0].clipped, m.c[1].clipped, m.c[0].code_mdb, m.c[1].code_mdb, m.c[0].ph_mdeg);
    }
    else if(cmd == 'B')
    {
      bode_plan pp;
      char      f1[1024];
      unsigned long long f;
      int32     lvl;

      if((sscanf(p, "%llu %1023s", &f, f1) != 2) || bode_plan_point(f, &pp) || (load(f1, rec1, 16384) < pp.record))
        printf("error\n");
      else
      {
        bode_dft_level(&pp, rec1, &lvl);
        printf("%d\n", lvl);
      }
    }
    else if(cmd == 'K')
    {
      static uint64 fm[BODE_MAX_POINTS];
      static int32  mg[BODE_MAX_POINTS], ph[BODE_MAX_POINTS];
      static uint8  ok[BODE_MAX_POINTS];
      bode_markers  mk;
      int n = (int)strtol(p, &p, 10), i;

      for(i = 0; i < n && i < BODE_MAX_POINTS; i++)
      {
        fm[i] = strtoull(p, &p, 10);
        mg[i] = (int32)strtol(p, &p, 10);
        ph[i] = (int32)strtol(p, &p, 10);
        ok[i] = (uint8)strtol(p, &p, 10);
      }

      bode_markers_find(fm, mg, ph, ok, n, &mk);
      printf("%u %u %u %llu %llu %d %u %llu %d %u %u", mk.shape, mk.valid, mk.n3db, (unsigned long long)mk.f3db_mhz[0],
             (unsigned long long)mk.f3db_mhz[1], mk.ref_mdb, mk.have_peak, (unsigned long long)mk.peak_mhz, mk.peak_mdb,
             mk.have_q, mk.q_x100);
      for(i = 0; i < BODE_PH_TARGETS; i++)
        printf(" %llu", (unsigned long long)mk.ph_mhz[i]);
      printf("\n");
    }
    else if(cmd == 'L')
      printf("%lld\n", (long long)bode_log2_q32(strtoull(p, &p, 10)));
    else if(cmd == 'E')
      printf("%llu\n", (unsigned long long)bode_exp2_round(strtoll(p, &p, 10)));
    else if(cmd == 'Q')
      break;

    fflush(stdout);
  }

  return 0;
}
