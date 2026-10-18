//----------------------------------------------------------------------------------------------------------------------------------
// spectrum.h - on-scope spectrum analyser (protocol builds only; docs/plans/dpos350p-features.md section 2)
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef SPECTRUM_H
#define SPECTRUM_H

#include "types.h"

//Microseconds since boot from timer0 (1 ms ticks + the 24 MHz down-counter), wraps after 71 minutes
uint32 spectrum_us(void);

//FFT_BENCH (fw 0.2.27): time the FFT on the CH1 trace buffer, layout in docs/protocol.md section 14 row 44.
//Returns the response length (<= SPECTRUM_BENCH_MAX).
#define SPECTRUM_BENCH_MAX    (8 + 8 * 24)
uint32 spectrum_bench(uint8 *out, uint32 reps);

#endif
