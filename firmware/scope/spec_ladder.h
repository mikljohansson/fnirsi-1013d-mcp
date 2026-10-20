//----------------------------------------------------------------------------------------------------------------------------------
// spec_ladder.h - the Spectrum's zoom ladder (fw 0.2.77; docs/ui-guidelines.md section 2, "Span drives the sample rate").
//
// The view is a cell: a span from a 1-2-5 ladder (1 Hz .. 20 MHz, then the whole axis 0 .. 25 MHz) and a centre. The
// sample rate is a function of the cell at zoom steps: the slowest rate whose fs / 2 covers the stop frequency (centre
// + span / 2) and whose capture of N samples takes at most SPL_TMAX_MS. A tap zooms the span one step about the centre
// and the caller then sets spl_rate() of the new cell, so every zoom step ends on the pair the rule gives. A drag pans
// the centre and never changes the rate (fw 0.2.81, owner): the caller passes the current rate's axis to spl_pan, so
// the cell stays inside 0 .. fs / 2; looking higher takes a zoom out and in again. Zooming in stops where the narrower span would need a rate whose capture N / fs exceeds
// SPL_TMAX_MS, or would hold fewer than SPL_MIN_BINS bins; zooming out stops at the whole axis.
//
// Rates are passed as a table of samples per second, fastest first (sample_rate[0..ACQ_RATES - 1]). A table of one rate
// is a record already taken (stopped): the cell is limited to that rate's axis and its capture time does not matter.
// Frequencies in mHz (uint64), as spectrum.c. Pure C, no globals, host-tested (host_tests/test_spec_zoom.c).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef SPEC_LADDER_H
#define SPEC_LADDER_H

#include "types.h"

#define SPL_FULL          23                          //the whole axis; 0 .. 22 are 1 Hz .. 20 MHz in 1-2-5 steps
#define SPL_FMAX_HZ       25000000u                   //= PC_SPECTRUM_FMAX_HZ (spectrum.c checks)
#define SPL_FMAX_MHZ      ((uint64)SPL_FMAX_HZ * 1000)
#define SPL_MIN_BINS      16                          //a span holds at least this many FFT bins
#define SPL_TMAX_MS       2048                        //capture time N / fs at most 2.048 s ("about 2 s", owner)

typedef struct
{
  uint8  span;                                        //ladder index 0 .. SPL_FULL
  uint64 centre;                                      //mHz
} spl_cell;

//Span of ladder index i in mHz (SPL_FULL: SPL_FMAX_MHZ)
uint64 spl_span_mhz(int i);

//Start and stop frequency of a cell (mHz)
uint64 spl_start(const spl_cell *c);
uint64 spl_stop(const spl_cell *c);

//Top of the axis the rates can show: min(SPL_FMAX_MHZ, rates[0] / 2)
uint64 spl_axis_mhz(const uint32 *rates, int32 n);

//The rate index for stop frequency `stop` (mHz) at FFT length 2^log2n: the slowest of rates[0..n-1] with fs / 2 >= stop
//and N / fs <= SPL_TMAX_MS (with n == 1 only the first); -1 when none
int32 spl_rate(uint64 stop, int log2n, const uint32 *rates, int32 n);

//1 when the cell can be zoomed into: its stop inside the axis, the slowest covering rate's capture within
//SPL_TMAX_MS (n > 1) and the span at least SPL_MIN_BINS bins at that rate
int spl_ok(const spl_cell *c, int log2n, const uint32 *rates, int32 n);

//Keep the cell inside 0 .. axis (the centre moved; a span wider than the axis is centred on it)
void spl_fit(spl_cell *c, uint64 axis);

//One zoom step: dir > 0 one span narrower (in), dir < 0 one wider (out), about the centre. Returns 1 when the cell
//changed, 0 at an end (nothing changed)
int spl_zoom(spl_cell *c, int dir, int log2n, const uint32 *rates, int32 n);

//Pan: the centre moved by d mHz, kept inside 0 .. axis (the current rate's: a pan keeps the rate). Returns 1 when the
//cell changed
int spl_pan(spl_cell *c, int64 d, uint64 axis);

//The cell that shows a whole axis 0 .. axis: the widest span that fits, from 0 (the whole axis at SPL_FMAX_MHZ)
void spl_from_axis(spl_cell *c, uint64 axis);

//Auto's cell (fw 0.2.42 rules on the ladder): span >= max(1.25 f_hi, 20 f0) from 0, narrowed while RBW (1.5 fs / N)
//exceeds f0 / 4, and never past the zoom-in end
void spl_auto(spl_cell *c, uint64 f0, uint64 fhi, int log2n, const uint32 *rates, int32 n);

//The bins that show the cell on an axis of `half` bins of `bin` mHz: first = floor(start / bin), the last one at or
//past the stop, at least 8; kept inside 0 .. half (a rate chosen by hand may not cover the cell). Returns 1 when the
//bins show the whole cell, 0 when it was clamped to the axis
int spl_bins(const spl_cell *c, uint64 bin, uint32 half, uint32 *first, uint32 *span);

#endif // SPEC_LADDER_H
