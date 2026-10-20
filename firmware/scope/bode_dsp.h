//----------------------------------------------------------------------------------------------------------------------------------
// bode_dsp.h - the on-device Bode analyser's maths (fw 0.2.66, docs/plans/bode-design.md steps D1-D2): the frequency planner
// shared with the host tool (mcp/src/fnirsi1013d/bode.py plan_point / plan_sweep, docs/bode-plan-vectors.json), the
// single-bin DFT of a coherent CH1 / CH2 record pair, H = X2 / X1 with its quality numbers, and the markers.
//
// Pure C, no globals, no float (soft-float only exists through libgcc and is not used): int64 accumulators, Q30 phasors from
// fx_fft's quarter-wave table, levels in milli-dB through fx_log2_q16, angles through a 32-step CORDIC. Host-buildable:
// host_tests/test_bode.c checks it against the plan vectors and against numpy references of the host tool's numbers.
//
// Units: frequencies in mHz (uint64; 10 MHz = 1e10 mHz does not fit 32 bits), levels in mdB (0.001 dB), angles in mdeg
// (0.001 degree, wrapped to (-180000, 180000]). "Raw" is one sample as the readout gives it: the 16-bit word of the 0x1534+
// decimation filter, or an 8-bit legacy code times 256.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef BODE_DSP_H
#define BODE_DSP_H

#include "types.h"

#define BODE_GEN_HZ           50000000u     //DDS clock: f = 50 MHz / M
#define BODE_MAX_POINTS       200           //planned points per sweep (BODE_CTRL accepts 1..200)
#define BODE_MAX_RECORD       8190          //samples per channel (the 16-bit layout's limit, protocol row 47)
#define BODE_MIN_RECORD       2048
#define BODE_MIN_PERIODS      8
#define BODE_MAX_SPP          512           //samples per period below 24 kHz: 257..512
#define BODE_LEGACY_M_MAX     33            //50 MHz / M > 1.5 MHz (M <= 33): legacy 200 MSa/s, 8-bit
#define BODE_FMIN_MHZ         1000ull       //1 Hz
#define BODE_FMAX_MHZ         10000000000ull //10 MHz

#define BODE_MDB_FLOOR        (-300000)     //level of a zero amplitude

//Point flags (READ_BODE u16; the host tool's flag names in brackets)
#define BODE_F_CH1_CLIPPED    0x0001    //[ch1_clipped]
#define BODE_F_CLIPPED        0x0002    //[clipped] CH2
#define BODE_F_CH2_OVERLOAD   0x0004    //[ch2_overload] CH2's front end saturated by a DC level (a flat record)
#define BODE_F_CH1_LOW        0x0008    //[ch1_low] CH1 fundamental below 20 codes
#define BODE_F_CH2_SMALL      0x0010    //[ch2_small] CH2 below 3 codes: +-1 dB
#define BODE_F_LOW_SNR        0x0020    //[low_snr] CH2 less than 20 dB above its noise
#define BODE_F_NEAR_FLOOR     0x0040    //[near_floor] CH2 less than 20 dB above the generator-off background
#define BODE_F_UNSETTLED      0x0080    //[unsettled]
#define BODE_F_INCOHERENT     0x0100    //[incoherent] CH1 energy beside the fundamental
#define BODE_F_SPLICED        0x0200    //[spliced] CH2 changed range at this point
#define BODE_F_FIR_OFF        0x0400    //[fir_off]
#define BODE_F_NO_CAL         0x0800    //[no_cal] thru applied, no thru point with this (M, N)
#define BODE_F_CAL_RANGE      0x1000    //[cal_range] thru taken on other ranges
#define BODE_F_CAL_FLAGGED    0x2000    //[cal_flagged] the thru point was itself flagged
#define BODE_F_RETRIED        0x4000    //measured again (re-centred, settle retry or range change)
#define BODE_F_BAD            (BODE_F_CH1_CLIPPED | BODE_F_CLIPPED | BODE_F_CH2_OVERLOAD | BODE_F_LOW_SNR | \
                               BODE_F_NEAR_FLOOR | BODE_F_UNSETTLED | BODE_F_INCOHERENT)

//----------------------------------------------------------------------------------------------------------------------------------
// Planner (bode-design 3.1-3.2; the host's plan_point / plan_sweep, identical integers)

typedef struct
{
  uint32 m;           //stimulus period in 50 MHz DDS clocks
  uint32 n;           //decimation factor of the capture (1 = legacy 200 MSa/s, else 16 x 2^k: CIC3 + FIR at divider n - 1)
  uint32 phase_inc;   //SET_GEN raw increment: round(2^32 / m), half up
  uint32 p, q;        //samples per period 4m / n = p / q in lowest terms
  uint16 record;      //samples analysed (j blocks of p): an integer number of periods
  uint16 blocks;      //j
  uint64 f_mhz;       //the DDS frequency phase_inc x 50e9 / 2^32 in mHz, rounded half to even (the host's round())
  uint8  hann;        //the 32-bit increment leaves the record more than 1e-3 periods off whole: Hann window (host)
} bode_plan;

//The plan for one requested frequency in mHz (1 Hz .. 10 MHz): M0 = round(50e9 / f) half to even, as Python's round().
//Returns 0, or -1 outside the range.
int    bode_plan_point(uint64 f_mhz, bode_plan *pp);

//The plan for a given M0 (>= 2): the decimation, the tie of M to N below 24 kHz, the record
void   bode_plan_m0(uint32 m0, bode_plan *pp);

//Log-spaced points from lo to hi (either order; measured low to high), points 1..BODE_MAX_POINTS, snapped to integer M,
//consecutive points with the same M merged (numpy geomspace: the end points exact, the interior points through a 2^-32
//log2 grid, so an interior M0 differs from the host's only within about 1e-9 of a rounding tie). Returns the number of
//plans written to out (<= max), or -1 for a bad argument.
int    bode_plan_sweep(uint64 start_mhz, uint64 stop_mhz, int points, bode_plan *out, int max);

uint32 bode_phase_inc(uint32 m);
uint32 bode_rate_hz(const bode_plan *pp);                   //output sample rate, 200 MHz / n
uint32 bode_divider(const bode_plan *pp);                   //0x0D value: n - 1 (0 for legacy)
uint32 bode_total(const bode_plan *pp);                     //trigger total: record rounded up to even
uint32 bode_record_us(const bode_plan *pp, uint32 total);   //capture time of total samples, rounded up
//Settle time before a capture in ms: max(periods / f, 1 ms), periods in tenths of a period
uint32 bode_settle_ms(const bode_plan *pp, uint32 periods_x10);

//log2 of a positive integer and 2^y, both in Q32 (exact to about 2^-60 inside; helpers of the planner, exposed for the tests)
int64  bode_log2_q32(uint64 v);
uint64 bode_exp2_round(int64 y_q32);                        //round(2^y), y in Q32, 0 <= y < 62

//----------------------------------------------------------------------------------------------------------------------------------
// DFT of a record pair at the plan's frequency (bode-design 1.2): mean removed, a rectangular window over the coherent
// record (Hann when pp->hann), X = 2 / sum(w) sum w (x - mean) e^(-j 2 pi f n / fs), f = phase_inc x 50 MHz / 2^32. The
// phasor runs on a 2^-34 turn grid (phase step phase_inc x n per sample, exact), rounded to the 16384-entry Q30 table. Also
// the two record halves (the settle check), CH1 one bin beside the fundamental (the coherence check), each channel's
// noise (the median of its period differences, robust against a transient as the host's median over FFT bins is) and its
// sample extremes.

typedef struct
{
  int64  si, sq;        //sum w' (x - x0) cos, - sum w' (x - x0) sin over the record, w' = w (Q15) >> 15 applied to x - x0
  int64  hi[2], hq[2];  //the same over each half (when the halves hold whole periods)
  int64  sx, hx[2];     //sum w' (x - x0) over the record and each half
  uint32 mad_q8;        //noise: median |x[n] - 2 x[n + p] + x[n + 2p]| (rk 2) or |x[n] - x[n + p]| (rk 1), Q8
  uint8  rk;
  int32  x0;            //the offset taken off every sample (the first pass's rounded mean)
  uint16 xmin, xmax;    //extremes of the raw samples
  int64  li[2], lq[2];  //CH1 only: the tones one bin below and above the fundamental (leakage)
  int64  su;            //fw 0.2.87: sum (x - x0) and sum (x - x0)^2 over the record, unwindowed (the record's spread)
  uint64 sxx;
} bode_dft_ch;

typedef struct
{
  bode_dft_ch ch[2];
  int64  wsum, wc, ws;           //sum w (Q15), sum w cos, sum w sin (Q15 x Q30)
  int64  hwsum[2], hwc[2], hws[2];
  int64  lwc[2], lws[2];         //sum w cos / sin of the leakage tones
  uint32 len;
  uint8  halves;                 //1: an even number of blocks, the halves hold whole periods
} bode_dft;

//x1, x2: len >= pp->record raw samples per channel (both 2-byte aligned). work: BODE_MAX_RECORD int32 words (scratch).
void   bode_dft_run(const bode_plan *pp, const uint16 *x1, const uint16 *x2, int32 *work, bode_dft *d);
//One channel only (the background capture): the tone at pp's frequency over pp->record samples, level in raw units (mdB)
int32  bode_dft_level(const bode_plan *pp, const uint16 *x, int32 *mdb_raw);

//----------------------------------------------------------------------------------------------------------------------------------
// Measurement: the DFT turned into levels, H and the quality numbers (the host's measure_pair / Measurement)

//Raw -> codes -> volts of one channel for one capture: code = raw x cpr_q24 / 2^24 + zero_q8 / 256 (the readout's
//conversion: the decimation scale with gain16 folded in, corr16; 2^16 and 0 for legacy codes x 256), volts = (code - zero)
//x volts per code. volt_mdb = 20 log10(volts per code at the probe tip) in mdB.
typedef struct
{
  uint32 cpr_q24;
  int32  zero_q8;       //code offset of the conversion (corr16 x 16); only the centre uses it
  int32  volt_mdb;
} bode_scale;

//20 log10 of the volts per code: uV per code x 256 (GET_CHANNEL's uv_q8) and the probe index (0.5x .. 1000x)
int32  bode_volt_mdb(uint32 uv_q8, uint8 probe_index);

typedef struct
{
  int32  lvl_mdb;       //fundamental, volts peak at the tip, dB re 1 V
  int32  code_mdb;      //fundamental in codes peak, dB re 1 code
  int32  ph_mdeg;       //its phase (cosine reference at sample 0)
  int32  noise_mdb;     //noise amplitude in one bin, the same scale as lvl_mdb
  int32  ac_c100;       //largest distance of a sample from the record mean, 1/100 code
  int32  centre_c100;   //record mean, 1/100 code
  int32  mean_uv;       //record mean in uV at the BNC (1x), relative to the channel's 0 V code (128 - offset)
  uint8  clipped;       //fw 0.2.83: a sample of this record at code 0 or 255 (the record's own min / max, nothing else)
  int32  std_mdb;       //fw 0.2.87: the record's standard deviation in codes, dB re 1 code (host Measurement.std1 / std2)
} bode_chm;

typedef struct
{
  bode_chm c[2];
  int32  h_mdb;         //|H| = |X2| / |X1|
  int32  h_mdeg;        //arg H
  int32  leak_mdb;      //CH1 one bin beside the fundamental re the fundamental
  int32  snr2_mdb;      //CH2 fundamental re its noise
  uint8  half_ok;       //the halves were compared
  int32  half_mdb;      //H of the second half re the first
  int32  half_mdeg;
  uint32 half_rel_ppm;  //|Hb / Ha - 1|, ppm
} bode_meas;

//zero_code_c100[c]: the code of 0 V x 100 (128 - offset); uv_q8[c]: uV per code x 256 at the BNC (the DC mean)
void   bode_measure(const bode_plan *pp, const bode_dft *d, const bode_scale s[2], const int32 zero_code_c100[2],
                    const uint32 uv_q8[2], bode_meas *m);

//The half-record settle verdict (host Measurement.unsettled): the halves differ by more than 0.1 dB or 1 deg and by more
//than 4 sqrt(2) x the noise's own spread
int    bode_unsettled(const bode_meas *m);

//fw 0.2.87: the host's Measurement properties and the measurement's own flags (bode.py _result), the same definitions and
//thresholds (bode_dsp.c names each host constant):
//  flat1     CH1's record has a standard deviation under 1 code (FLAT_STD_CODES): a stuck or saturated readout, no stimulus
//  ch1_dead  flat1, or CH1's fundamental under 0.5 code (CH1_MIN_CODES): nothing to divide by
//  suspect   ch1_dead, or CH2 flat (std under 1 code and excursion under 0.05 code): worth one retake
//  flat2     CH2 saturated (coll: its tone collapsed after a range change, or the record flat with a tone under 3
//            codes); never while CH1 is flat (both flat = a stuck readout, not a CH2 overload)
int    bode_flat1(const bode_meas *m);
int    bode_ch1_dead(const bode_meas *m);
int    bode_suspect(const bode_meas *m);
int    bode_flat2(const bode_meas *m, int coll);
//BODE_F_CH1_CLIPPED .. BODE_F_UNSETTLED of a measurement (ch1_clipped, clipped, ch2_overload, ch1_low, ch2_small, low_snr,
//incoherent, unsettled); the engine adds fir_off, near_floor, retried, spliced and the thru's flags
uint16 bode_meas_flags(const bode_meas *m, int coll);

//Helpers exposed for the engine and the tests
int32  bode_mdb_iq(int64 i, int64 q);                     //20 log10 sqrt(i^2 + q^2)
int32  bode_atan2_mdeg(int64 y, int64 x);                 //angle of x + j y, (-180000, 180000]
int32  bode_wrap_mdeg(int32 a);
int32  bode_mdb_u64(uint64 v);                            //20 log10 v

//----------------------------------------------------------------------------------------------------------------------------------
// Markers over the valid points (host markers(); the crossings by linear interpolation in log f between points, where
// the host uses a monotone cubic: the two agree to a fraction of the point spacing)

#define BODE_SHAPE_NONE       0
#define BODE_SHAPE_LOWPASS    1
#define BODE_SHAPE_HIGHPASS   2
#define BODE_SHAPE_FLAT       3
#define BODE_SHAPE_RESONANT   4

#define BODE_PH_TARGETS       6           //+45, -45, +90, -90, -135, -180 deg

typedef struct
{
  uint8  shape;
  uint8  n3db;                //-3 dB crossings found (0..2)
  uint8  valid;               //valid points used
  uint8  have_peak;
  uint8  have_q;
  uint64 f3db_mhz[2];
  int32  ref_mdb;             //the reference of the -3 dB crossings (passband end or peak)
  uint64 peak_mhz;
  int32  peak_mdb;
  uint32 q_x100;              //Q x 100 (resonant with both crossings)
  uint64 ph_mhz[BODE_PH_TARGETS];   //0 = no crossing
} bode_markers;

extern const int32 bode_ph_targets_mdeg[BODE_PH_TARGETS];

//f_mhz, mag_mdb, ph_mdeg: n points (low to high frequency); ok[i] = 0 for flagged points (skipped)
void   bode_markers_find(const uint64 *f_mhz, const int32 *mag_mdb, const int32 *ph_mdeg, const uint8 *ok, int n,
                         bode_markers *mk);

//Interpolate y at frequency f between points (linear in log f), for the cursor; returns 0 outside the points
int    bode_interp(const uint64 *f_mhz, const int32 *y, const uint8 *ok, int n, uint64 f, int32 *out);

#endif /* BODE_DSP_H */
