//----------------------------------------------------------------------------------------------------------------------------------
// decim_check: the decimation filter maths of pc_decim.c for check_vectors.py (docs/protocol-vectors.json "decim").
//
// argv:   the 8-bit levels to convert into 16-bit trigger levels
// stdin:  one case per line: <divider> <k_req> <total samples> <fir_req> (FIR on a 0x1535 bitstream)
// stdout: per case: <k> <n> <gain> <shift> <rate> <settle_us> <ext_shift> <fir> [<level16> per level when k >= 1]
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>

#include "pc_decim.h"

int main(int argc, char **argv)
{
  unsigned long d, k, total, fir;
  int i;

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
