//----------------------------------------------------------------------------------------------------------------------------------
// sinc_view.c - sin(x)/x interpolation for the trace between samples (see sinc_view.h)
//----------------------------------------------------------------------------------------------------------------------------------

#include "sinc_view.h"

int32 sincv_at(const int16 *v, int32 n, uint32 pos)
{
  int32        i = (int32)(pos >> 16), k, j, acc = 1 << (SINCV_Q - 1);
  const int16 *h;

  if(i >= n - 1)
    return v[n - 1];

  h = sincv_lut[(pos >> (16 - 5)) & (SINCV_PHASES - 1)];

  //Samples i - 3 .. i + 4 around the position
  for(k = 0; k < SINCV_TAPS; k++)
  {
    j = i + k - (SINCV_TAPS / 2 - 1);

    if(j < 0)
      j = 0;
    else if(j > n - 1)
      j = n - 1;

    acc += (int32)h[k] * v[j];
  }

  return acc >> SINCV_Q;
}
