//----------------------------------------------------------------------------------------------------------------------------------
// acal_track.h - background tracking of the core A / core B mismatch in full-rate 8-bit records (calibration v4,
// docs/plans/interleave-tracking.md).
//
// Per channel, every usable 8-bit full-rate record (after the v3 correction and the tracker's own applied values) gives
// pairs: each interior sample s_n against the mean i_n of its two neighbours of the other core. y_n = +-(s_n - i_n)
// (+ for core A, - for core B) estimates delta(x) = A(x) - B(x) at x_n = (s_n + i_n) / 2 - 128. The record's sums are
// moved back to the open-loop domain (y + applied d + applied e x) and added to moments with forgetting 1/256 per
// accepted record; a 3-term least-squares fit delta(x) = d + e x + q x^2 (orthogonal polynomials, integer only) gives
// the estimate. d and e are applied symmetrically to every 8-bit readout (acal_track_apply; the ADCs run at full rate at
// every rate since FPGA 0x153D, fw 0.2.105; before, only divider 0 had the full-rate class); q is estimated only. The
// records come from divider 0 only (acal_live.c): there the trace is evenly interleaved, 5 ns per sample, which the
// neighbour interpolation assumes; at D > 0 a stored pair's two samples are 5 ns apart but the pairs (D + 1) x 10 ns.
//
// Units of the moments: x4 = 2 s_n + s_{n-1} + s_{n+1} - 512 is x in quarter codes (exact; |x4| <= 496 for usable
// pairs), y256 is y in 1/256 code after the open-loop shift (|y256| <= 16256: clamp 16 codes plus |applied d| <= 32
// and |applied e x| <= 15.5 codes).
//   m[k] = forgotten sum of x4^k         (k = 0..4)
//   t[k] = forgotten sum of x4^k y256    (k = 0..2)
// Bounds with records of at most ACAL_TRK_MAX_COUNT samples, steady state 256 x a record's sum: m[4] <= 496^4 x 16384 x
// 256 = 2.5e17, |t[2]| <= 496^2 x 16256 x 16384 x 256 = 1.7e16: all far below 2^63 (9.2e18).
//
// Integer only, no globals, no hardware: builds for the scope and on the host (host_tests/test_acal_track.c).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef ACAL_TRACK_H
#define ACAL_TRACK_H

#include <stdint.h>

#include "adc_cal.h"

#define ACAL_TRK_DATA      0x01   //at least one record accepted since reset
#define ACAL_TRK_GAIN      0x02   //e identifiable (spread v >= 64 codes^2) at the last solve
#define ACAL_TRK_BOW       0x04   //q identifiable (w >= 65536 codes^4 and w >= v^2 / 4) at the last solve
#define ACAL_TRK_LAST      0x08   //the last offered record was usable

#define ACAL_TRK_TAU_SHIFT 8      //forgetting 1/256 per accepted record
#define ACAL_TRK_MIN_MS    50     //at most one accepted record per 50 ms
#define ACAL_TRK_MAX_COUNT 16384  //samples of a record looked at (the rest is ignored)

typedef struct
{
  int64_t  m[5];          //forgotten sums of x4^k, k = 0..4 (x4 = x in 1/4 code, x = code - 128)
  int64_t  t[3];          //forgotten sums of x4^k y256 (y256 = A - B estimate in 1/256 code, open-loop domain)
  int32_t  d_q8;          //applied offset mismatch A - B at x = 0, 1/256 code
  int32_t  e_q24;         //applied gain mismatch (slope of A - B vs x), Q24
  int32_t  q_q24;         //bow (x^2 coefficient of A - B), Q24 per code^2; estimated, not applied
  uint32_t last_ms;       //time of the last accepted record
  uint32_t records;       //accepted records since reset
  uint16_t last_pairs;    //usable pairs of the last offered record
  uint16_t spread16;      //sqrt(v) of the accumulated x, 1/16 code
  uint8_t  flags;
} acal_track;

void acal_track_reset(acal_track *t);

//One record of `count` corrected 8-bit codes (buf[0] core B, buf[1] core A, alternating), already corrected with the
//tracker's values applied at that readout (applied_d_q8 / applied_e_q24: t->d_q8 / t->e_q24 as they were when the
//readout's correction was prepared). invert: the buffer holds 255 - code. Returns 1 when the record was accepted and
//the estimate updated, 0 when skipped (rate gate: nothing changes; fewer than 256 usable pairs or more than 1/8 of the
//in-range pairs dropped for slope: only last_pairs and ACAL_TRK_LAST change).
int  acal_track_record(acal_track *t, const uint8_t *buf, uint32_t count, int invert, int32_t applied_d_q8,
                       int32_t applied_e_q24, uint32_t now_ms);

//The fit from the moments (called by acal_track_record, exported for the host test): updates d_q8, e_q24 and q_q24
//(e and q only when identifiable, else held), spread16 and the flags GAIN / BOW. t->m[0] must be > 0
void acal_track_solve(acal_track *t);

//Folds the tracked d and e into a readout correction, only when c->version == 3, symmetric
//about the mean of the cores (x = code - 128): A' = A - (d + e x) / 2, B' = B + (d + e x) / 2. gain_a/b, off_a/b and
//clip_lo/hi change; corr16, gain16, zero_q8, comp/frac stay (the mean of the cores is unchanged)
void acal_track_apply(const acal_track *t, acal_corr *c);

#endif /* ACAL_TRACK_H */
