// fft_cli.c - runs the target's fx_fft.c natively for host_tests/test_fft.py.
//
//   fft_cli LOG2N WINDOW KIND OFFSET [CDB_OFFSET] < samples > result
//
// KIND u8: N bytes in; i32: N little-endian int32 in. Out (little-endian): int32 shift, int32 norm_q16,
// (N/2 + 1) x (int32 re, int32 im), (N/2 + 1) x int16 centi-dB (plus CDB_OFFSET), then the window w[0..N-1] as int32 Q30.
// Mode "log2": reads u64 values, writes int32 fx_log2_q16 and int32 fx_cdb_from_q16 for each.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fx_fft.h"

static int32 win[FX_FFT_WIN_WORDS(FX_FFT_LOG2_MAX)] __attribute__((aligned(8)));
static int32 buf[FX_FFT_BUF_WORDS(FX_FFT_LOG2_MAX)] __attribute__((aligned(8)));
static int32 xi[FX_FFT_N_MAX] __attribute__((aligned(8)));
static uint8 xb[FX_FFT_N_MAX] __attribute__((aligned(8)));
static int16 cdb[FX_FFT_N_MAX / 2 + 1];

int main(int argc, char **argv)
{
  FX_FFT p;
  int    log2n, window, n;
  int32  offset;

  if((argc == 2) && !strcmp(argv[1], "log2"))
  {
    uint64 v;

    while(fread(&v, 8, 1, stdin) == 1)
    {
      int32 out[2];

      out[0] = fx_log2_q16(v);
      out[1] = fx_cdb_from_q16(out[0]);
      fwrite(out, 4, 2, stdout);
    }
    return 0;
  }

  if((argc != 5) && (argc != 6))
  {
    fprintf(stderr, "usage: fft_cli LOG2N WINDOW u8|i32 OFFSET | fft_cli log2\n");
    return 2;
  }

  log2n  = atoi(argv[1]);
  window = atoi(argv[2]);
  offset = atoi(argv[4]);
  n      = 1 << log2n;

  if(fx_fft_init(&p, log2n, window, win, buf))
  {
    fprintf(stderr, "bad plan\n");
    return 2;
  }

  if(!strcmp(argv[3], "u8"))
  {
    if(fread(xb, 1, n, stdin) != (size_t)n) return 3;
    fx_fft_run_u8(&p, xb, offset);
  }
  else
  {
    if(fread(xi, 4, n, stdin) != (size_t)n) return 3;
    fx_fft_run_i32(&p, xi, offset);
  }

  fx_fft_cdb(&p, 0, n / 2 + 1, (argc == 6) ? atoi(argv[5]) : 0, cdb);

  fwrite(&p.shift, 4, 1, stdout);
  fwrite(&p.norm_q16, 4, 1, stdout);
  fwrite(buf, 4, n + 2, stdout);
  fwrite(cdb, 2, n / 2 + 1, stdout);
  fwrite(win, 4, n, stdout);
  return 0;
}
