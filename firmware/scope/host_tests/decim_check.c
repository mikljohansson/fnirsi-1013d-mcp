//----------------------------------------------------------------------------------------------------------------------------------
// decim_check: the decimation filter maths of pc_decim.c for check_vectors.py (docs/protocol-vectors.json "decim" and, since
// C6, "decim_peak").
//
// Default mode (the "decim" section):
//   argv:   the 8-bit levels to convert into 16-bit trigger levels
//   stdin:  one case per line: <divider> <k_req> <total samples> <fir_req> (FIR on a 0x1535 bitstream)
//   stdout: per case: <k> <n> <gain> <shift> <rate> <settle_us> <ext_shift> <fir> [<level16> per level when k >= 1]
//
// --peak (the "decim_peak" cases; the firmware's capability level, FIR and capture stages):
//   stdin:  <divider> <total> <k_req> <fir_req>
//   stdout: <peak> <k> <n> <why> <status> <shift_byte> <cmd56> <rate> <settle_us> <ext_shift>
//
// --peak-corr <raws...> -- <pairs as min,max ...> -- <levels...> (the "decim_peak" corr):
//   stdin:  <gain_a> <gain_b> <off_a> <off_b>
//   stdout: <gain> <off> <codes per raw> <mids per pair> <level per level> <overload per pair>
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc_decim.h"

static int run_peak(void)
{
  unsigned long d, total, k, fir;

  while(scanf("%lu %lu %lu %lu", &d, &total, &k, &fir) == 4)
  {
    pc_decim_cfg c;

    pc_decim_config((uint8_t)k, (uint8_t)(PC_DECIM_SET_PEAK | (fir ? PC_DECIM_SET_FIR : 0)), (uint32_t)d, (uint32_t)total,
                    PC_DECIM_HAS_FIR | PC_DECIM_HAS_CAPSTAGES, &c);
    printf("%u %u %u %u %u %u %u %u %u %u\n", c.peak, c.k, (unsigned)c.n, c.why, pc_decim_status(&c), pc_decim_shift_byte(&c),
           pc_decim_cmd56(&c), (unsigned)pc_decim_rate(&c, (uint32_t)d), (unsigned)pc_decim_settle_us(&c),
           (unsigned)pc_decim_ext_shift(&c));
  }

  return 0;
}

static int run_peak_corr(int argc, char **argv)
{
  long ga, gb, oa, ob;
  int  i, sep1 = 0, sep2 = 0;

  for(i=2;i<argc;i++)
    if(!strcmp(argv[i], "--"))
    {
      if(!sep1) sep1 = i;
      else if(!sep2) sep2 = i;
    }

  if(!sep1 || !sep2)
    return 1;

  while(scanf("%ld %ld %ld %ld", &ga, &gb, &oa, &ob) == 4)
  {
    uint32_t gain = pc_peak_gain((uint32_t)ga, (uint32_t)gb);
    int32_t  off  = pc_peak_off((int32_t)oa, (int32_t)ob);

    printf("%u %d", gain, off);

    for(i=2;i<sep1;i++)
      printf(" %d", pc_peak_code((uint8_t)atoi(argv[i]), gain, off));

    for(i=sep1+1;i<sep2;i++)
    {
      int mn = atoi(argv[i]), mx = atoi(strchr(argv[i], ',') + 1);
      printf(" %d", pc_peak_mid((uint8_t)mn, (uint8_t)mx, gain, off));
    }

    for(i=sep2+1;i<argc;i++)
      printf(" %u", pc_peak_level((uint8_t)atoi(argv[i]), gain, off));

    for(i=sep1+1;i<sep2;i++)
    {
      int mn = atoi(argv[i]), mx = atoi(strchr(argv[i], ',') + 1);
      printf(" %u", pc_peak_overload((uint8_t)mn, (uint8_t)mx));
    }

    printf("\n");
  }

  return 0;
}

int main(int argc, char **argv)
{
  unsigned long d, k, total, fir;
  int i;

  if((argc >= 2) && !strcmp(argv[1], "--peak"))
    return run_peak();
  if((argc >= 2) && !strcmp(argv[1], "--peak-corr"))
    return run_peak_corr(argc, argv);

  while(scanf("%lu %lu %lu %lu", &d, &k, &total, &fir) == 4)
  {
    pc_decim_cfg c;

    pc_decim_config((uint8_t)k, fir ? PC_DECIM_SET_FIR : 0, (uint32_t)d, (uint32_t)total, PC_DECIM_HAS_FIR, &c);
    printf("%u %u %llu %u %u %u %u %u", c.k, (unsigned)c.n, (unsigned long long)c.gain, c.shift,
           (unsigned)pc_decim_rate(&c, (uint32_t)d), (unsigned)pc_decim_settle_us(&c), (unsigned)pc_decim_ext_shift(&c),
           c.fir);

    if(c.k)
      for(i=1;i<argc;i++)
        printf(" %u", pc_decim_level16((uint8_t)atoi(argv[i]), c.gain, c.shift));

    printf("\n");
  }

  return 0;
}
