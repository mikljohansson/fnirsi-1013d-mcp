//----------------------------------------------------------------------------------------------------------------------------------
// adc_cal.h - per-core ADC calibration ("calibration data v3", firmware/README.md section 6, protocol row 51).
//
// Each channel is digitised by two interleaved ADC cores, A (FPGA 0x20 / 0x22, odd trace positions) and B (0x21 / 0x23,
// even positions). Model per core k, hardware range r and clock class c:
//
//     raw_k = z_k + V x g_k[c],   z_k = zero[c][k][r] + slope[c][k][r] x dcoffset   (zero 1/16 code, slope Q16 per count)
//
// Clock class: in legacy mode (K = 0) the FPGA gates the ADC encode clock with the sample-rate divider, and the zero
// and the gain move with it (bench 2026-10-18, CH1: core B's zero 3.6 codes lower as soon as the clock is gated; gains
// A/B 1.033 at full rate, 1.004 gated, full-rate class 1.1 % lower). ACAL_FULL = divider 0 and every 0x1534 filtered
// capture (ADCs at full rate), ACAL_GATED = legacy divider >= 1.
//
// Every readout turns the model into a gain G_k = g_ref / g_k[c] (g_ref = mean gain of both cores in the gated class,
// what the 1.00o5 voltage calibration is fitted on) and an offset per core so that
//
//     code = clamp((raw x gain_k + off_k + u) >> 16),   u = 16-bit dither,   mean = T + G_k (raw - z_k)
//
// puts 0 V of both cores at T = 128 - dcoffset x 100 / dc_shift_value (the code the 1.00o5 measurements and the screen
// marker take as 0 V) and gives both cores and both classes the same codes per volt. In the 16-bit filter path every
// output weighs both cores equally (even N), so one gain (gain16, full class) and one constant (corr16) correct it.
//
// The gains are measured once per channel (not per range: the mismatch is inside the ADC, an assumption the bench
// checks) at the calibration post with the generator holding its two static levels (acal_gain_store).
//
// The block lives in sector 708 bytes 128..431 (ACAL_SECTOR_OFFSET), outside Atlan's checksummed bytes 16..119, so an
// older firmware still boots with the card (its own save of 708 clears the block). The block is bound to the
// dc_calibration_offset values it was measured at: when sector 709's values differ (an older firmware re-ran the base
// calibration, RESET, defaults) it is ignored and the 1.00o5 compensations apply. v2 blocks (fw 0.2.43-0.2.44) are not
// read.
//
// Integer only, no globals, no hardware: builds for the scope and on the host (host_tests/test_adc_cal.c).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef ADC_CAL_H
#define ADC_CAL_H

#include <stdint.h>

#define ACAL_RANGES          6            //hardware ranges (relay 0..5); range 6 (software zoom) uses range 5
#define ACAL_CLASSES         2            //clock classes in this version
#define ACAL_FULL            0            //ADCs at full rate: legacy divider 0, every filtered capture
#define ACAL_GATED           1            //legacy divider >= 1 (encode clock gated by the FPGA)

#define ACAL_SECTOR_OFFSET   128          //byte offset of the block in sector 708
#define ACAL_MAGIC           0x4C414341u  //"ACAL" little-endian
#define ACAL_VERSION         3            //"calibration data v3" (Atlan's fields alone are v1; v2 had no gains)
#define ACAL_HDR_BYTES       12           //magic, version, length, classes, 3 reserved
#define ACAL_CH_BYTES        144          //per channel: pwm 12, zero 48, slope 48, noise 24, gain 8, gain meta 4
#define ACAL_BLOCK_BYTES     (ACAL_HDR_BYTES + 2 * ACAL_CH_BYTES + 4)   //304: header, two channels, CRC-32

#define ACAL_CORE_A          0
#define ACAL_CORE_B          1

#define ACAL_GAIN_ONE        32768        //stored gain Q15
#define ACAL_GAIN_NONE       0xFF         //gain_range: never measured (gains are ACAL_GAIN_ONE)
#define ACAL_GAIN_KEPT       0x01         //gain_flags: carried over from an earlier calibration's block

typedef struct
{
  uint16_t pwm[ACAL_RANGES];                        //dc_calibration_offset[r] the zeros were measured at (binding)
  int16_t  zero[ACAL_CLASSES][2][ACAL_RANGES];      //[class][core][range] raw mean code at pwm[r], 1/16 code
  int16_t  slope[ACAL_CLASSES][2][ACAL_RANGES];     //[class][core][range] codes per PWM count, Q16 (negative)
  uint8_t  noise[ACAL_CLASSES][2][ACAL_RANGES];     //[class][core][range] RMS about the mean, 1/16 code
  uint16_t gain[ACAL_CLASSES][2];                   //[class][core] G = g_ref / g_k, Q15
  uint8_t  gain_range;                              //hardware range the gains were measured on, ACAL_GAIN_NONE
  uint8_t  gain_flags;                              //ACAL_GAIN_KEPT
  uint16_t gain_lever;                              //gated span between the post levels, mean of both cores, 1/16 code
} acal_channel;

//Sector block: the two channels <-> ACAL_BLOCK_BYTES bytes (little-endian, CRC-32/ISO-HDLC = zlib.crc32 over all but the
//last 4 bytes)
void     acal_pack(const acal_channel ch[2], uint8_t *block);
uint8_t *acal_pack_channel(const acal_channel *c, uint8_t *p);        //one channel's ACAL_CH_BYTES (also GET_CAL 112..)
int      acal_unpack(const uint8_t *block, acal_channel ch[2]);       //ACAL_OK, ACAL_E_MAGIC, ACAL_E_VERSION, ACAL_E_CRC
uint32_t acal_crc32(const uint8_t *data, uint32_t len);

//Calibration provenance (fw 0.2.52, protocol row 55): sector 708 bytes 432..487, after the v3 block and outside its CRC,
//so the v3 block and its version stay as 0.2.45-0.2.51 read them. Written with the block when a save carries a block that
//differs from the one loaded or last saved (a calibration's Done); kept by saves of an unchanged block.
#define ACAL_PROV_OFFSET     (ACAL_SECTOR_OFFSET + ACAL_BLOCK_BYTES)   //432
#define ACAL_PROV_MAGIC      0x56525041u  //"APRV"
#define ACAL_PROV_VERSION    1
#define ACAL_PROV_BYTES      56
#define ACAL_PROV_CH1_ZERO   0x01         //what: the parts this calibration wrote
#define ACAL_PROV_CH1_GAIN   0x02
#define ACAL_PROV_CH2_ZERO   0x04
#define ACAL_PROV_CH2_GAIN   0x08

typedef struct
{
  uint32_t build;                         //commit hash of the firmware that calibrated
  char     version[16];                   //its version string (NUL-padded)
  uint16_t fpga;                          //FPGA version word (command 0x06)
  uint8_t  what;                          //ACAL_PROV_*
  uint8_t  rtc;                           //1: time[] is from the DS3231 module
  uint32_t uptime_ms;
  uint32_t power_on;                      //unit life record (pc_life.h); 0 = unknown
  uint32_t minutes;                       //operating minutes; 0 with an unknown record
  uint8_t  time[6];                       //yy mm dd hh mm ss, zeros without a module
} acal_prov;

#define ACAL_PROV_NONE       0            //acal_prov_unpack results (GET_CAL byte 288)
#define ACAL_PROV_VALID      1
#define ACAL_PROV_BAD        2            //magic present, version or CRC wrong

void     acal_prov_pack(const acal_prov *p, uint8_t *out);                  //ACAL_PROV_BYTES
int      acal_prov_unpack(const uint8_t *in, acal_prov *p);                 //ACAL_PROV_*; p zeroed unless VALID
//The parts that differ between two blocks' channels (ACAL_PROV_* bits), 0 = same calibration
uint8_t  acal_prov_what(const acal_channel old[2], const acal_channel cur[2]);

//1 when c was measured at exactly these PWM centres (cal_offset[0..5])
int      acal_bound(const acal_channel *c, const uint16_t *cal_offset);

//Gain of core k in class cls as applied, Q16 (65536 when the gains were never measured)
uint32_t acal_gain16(const acal_channel *c, uint32_t cls, uint32_t k);

//Correction for one readout
typedef struct
{
  int16_t  comp_a;                        //correction at core A's 0 V point: whole codes (floor) ...
  int16_t  comp_b;
  uint8_t  frac_a;                        //... and sixteenths (0..15; 0 with v1)
  uint8_t  frac_b;
  uint8_t  version;                       //3: calibration data v3 in use, 1: 1.00o5 compensations
  uint8_t  cls;                           //clock class of the 8-bit correction (ACAL_FULL / ACAL_GATED)
  uint32_t gain_a;                        //8-bit loop: code = (raw x gain + off + u) >> 16, Q16
  uint32_t gain_b;
  int32_t  off_a;                         //Q16 codes
  int32_t  off_b;
  int32_t  clip_lo;                       //rail rule: the larger of the two cores' codes for raw 0
  int32_t  clip_hi;                       //the smaller of the two cores' codes for raw 255
  uint32_t gain16;                        //16-bit path: full-class gain of the mean of both cores, Q16
  int32_t  shift;                         //dcoffset x 100 / dc_shift_value (C division), added for the measurements
  int32_t  zero_q8;                       //trace code of 0 V x 256 (mean of both corrected cores)
  int32_t  corr16;                        //16-bit path (ADCs at full rate): added to every converted sample, 1/16 code
} acal_corr;

//Dither for the 8-bit loop: xorshift32 step (three register instructions on ARM, no constants to load; state never 0).
//Its top 16 bits are uniform, so (raw x gain + off + (state >> 16)) >> 16 has the mean (raw x gain + off) / 65536
#define ACAL_DITHER_STEP(x)  do { (x) ^= (x) << 13; (x) ^= (x) >> 17; (x) ^= (x) << 5; } while(0)

//c = 0 (or not bound to cal_offset): 1.00o5 behaviour with atlan_a / atlan_b (adc1/adc2compensation). divider: the 0x0D
//value of the capture (legacy clock class)
void     acal_correction(const acal_channel *c, const uint16_t *cal_offset, uint8_t range, uint32_t divider,
                         int16_t dcoffset, int32_t dc_shift_value, int16_t atlan_a, int16_t atlan_b, acal_corr *out);

//Rail-rule limits from gain_a/b and off_a/b: clip_lo = the larger of the two cores' codes for raw 0, clip_hi = the
//smaller of their codes for raw 255 (acal_correction, and acal_track_apply after it moves the gains and offsets)
void     acal_clips(acal_corr *c);

//16-bit path: the decimation scale (stored sample -> code, Q24) with the gain16 of the readout folded in
uint32_t acal_scale(uint32_t scale_q24, uint32_t gain16);

//Whole-code compensations for per-core corrections dA, dB (1/16 code): |A - B| after them <= 1/2 code, mean <= 1/2 code
//(for the 1.00o5 fields, which have no fraction)
void     acal_round_pair(int32_t da16, int32_t db16, int16_t *comp_a, int16_t *comp_b);

//The 1.00o5 fields from a v3 measurement: compensations for T = 128 at dcoffset 0 from the gated zeros averaged over
//the ranges (the UI's time bases are gated except 200 MSa/s)
void     acal_atlan_comps(const acal_channel *c, int16_t *comp_a, int16_t *comp_b);

//----------------------------------------------------------------------------------------------------------------------------------
//Measurement

typedef struct
{
  uint32_t n;
  uint32_t sum;
  uint64_t sumsq;
  uint64_t convsq;                        //sum over the acal_acc_add calls (one conversion each) of sum^2 / count
  uint32_t clipped;                       //samples at 0 or 255
  uint8_t  lo, hi;                        //lowest and highest code (fw 0.2.67; valid when n > 0)
} acal_acc;

void     acal_acc_add(acal_acc *a, const uint8_t *buf, uint32_t count, uint32_t stride);   //buf[0], buf[stride], ...
void     acal_acc_merge(acal_acc *a, const acal_acc *b);
int32_t  acal_mean16(const acal_acc *a);  //round(16 x mean)
uint32_t acal_rms16(const acal_acc *a);   //16 x RMS about the mean
//16 x RMS about each conversion's own mean (each acal_acc_add call one conversion): the noise inside a capture, without
//what moves between conversions (offset-PWM ripple phase, mains pickup on an open input, drift). acal_rms16 squared is
//this squared plus the variance of the conversion means (fw 0.2.60: the zero step reports both when a check fails)
uint32_t acal_rms16_within(const acal_acc *a);

//Coarse centre: PWM where the mean of both cores reads 128, from the means (1/16 code) at two PWM values. -1 when the
//response is under ACAL_MIN_RESPONSE or the result is outside ACAL_PWM_MIN..ACAL_PWM_MAX
int32_t  acal_centre_pwm(int32_t pwm_lo, int32_t mean16_lo, int32_t pwm_hi, int32_t mean16_hi);

//The same interpolation without the checks (slope points, already checked by acal_range_store); pwm_lo when flat
int32_t  acal_interp_pwm(int32_t pwm_lo, int32_t mean16_lo, int32_t pwm_hi, int32_t mean16_hi);

//One range: per class and core the means (1/16 code) at pwm_lo / pwm_hi and the zero point pwm
typedef struct
{
  int32_t  pwm_lo, pwm_hi, pwm;
  int32_t  mean16_lo[ACAL_CLASSES][2], mean16_hi[ACAL_CLASSES][2];
  int32_t  zero16[ACAL_CLASSES][2];
  int32_t  drift16[ACAL_CLASSES][2];      //second half minus first half of the zero-point conversions
  uint32_t noise16[ACAL_CLASSES][2];      //acal_rms16 of the zero-point conversions (the checked value)
  uint32_t within16[ACAL_CLASSES][2];     //acal_rms16_within of the same conversions (reported only)
  uint32_t clipped;                       //clipped samples over all points
} acal_range_meas;

//Where a range check failed (fw 0.2.60, the wizard's result line): class and core of the failing value and the value.
//Noise: noise16, with within16; drift: drift16; slope: Q16 (or the A - B difference); zero: zero16 - 2048; spread: A - B;
//class: full - gated (1/16 code). Clip: all zero
typedef struct
{
  uint8_t  cls, core;
  int32_t  value;
  uint32_t within16;
  //fw 0.2.67 (acal_fail_point): the measurement point the failure line reports, its PWM and raw per-core numbers.
  //point ACAL_PT_NONE when none was measured
  uint8_t  point;
  uint16_t pwm;
  int32_t  mean16[2];
  uint8_t  lo[2], hi[2];
  uint32_t clipped[2];
} acal_fail;

//Checks m: ACAL_OK or the first ACAL_E_* that applies, with *f (may be 0) saying where
int      acal_range_check(const acal_range_meas *m, acal_fail *f);

//The zero step's measurement points of one range, in the order the wizard takes them (fw 0.2.67: the failure line and
//CAL_PROBE report them). Coarse points 2 gated conversions, gated slope points 4, gated zero 8, full-rate points 64
#define ACAL_PT_COARSE_HI    0            //PWM 500 (high code), gated
#define ACAL_PT_COARSE_LO    1            //PWM 1200 (low code), gated
#define ACAL_PT_G_LO         2            //centre - 150, gated
#define ACAL_PT_G_HI         3            //centre + 150, gated
#define ACAL_PT_G_ZERO       4            //the refined centre, gated
#define ACAL_PT_F_ZERO       5            //the refined centre, 200 MSa/s
#define ACAL_PT_F_HI         6            //centre + 150, 200 MSa/s
#define ACAL_PT_F_LO         7            //centre - 150, 200 MSa/s
#define ACAL_POINTS          8
#define ACAL_PT_NONE         0xFF

typedef struct
{
  uint16_t pwm;
  uint8_t  cls;                           //ACAL_FULL / ACAL_GATED
  uint8_t  convs;                         //conversions taken; 0 = not measured
  int32_t  mean16[2];                     //per core, 1/16 code
  uint16_t rms16[2];                      //about the mean of all the conversions, 1/16 code
  uint16_t within16[2];                   //inside the conversions, 1/16 code
  uint8_t  lo[2], hi[2];                  //lowest / highest code
  uint32_t clipped[2];                    //samples at 0 or 255
} acal_point;

//A point from its accumulators [half][core] (the halves merged)
void     acal_point_set(acal_point *p, int32_t pwm, uint32_t cls, uint32_t convs, const acal_acc acc[2][2]);

//Fills f's point fields for a failed range (rc, f's class from acal_range_check): ACAL_E_CLIP the first point after the
//coarse ones with a clipped sample, ACAL_E_RESPONSE the coarse high point, otherwise the zero point of f's class
//(ACAL_E_TIMING: the full-rate zero point). ACAL_PT_NONE when that point was not measured
void     acal_fail_point(const acal_point pt[ACAL_POINTS], int rc, acal_fail *f);

//CAL_PROBE response (protocol 1.21 provisional, docs/protocol.md section 14 row 61): ACAL_PROBE_BYTES
#define ACAL_PROBE_BYTES     (24 + 28 * ACAL_POINTS)
#define ACAL_PROBE_GARBLED   0x01         //flags: a full-rate capture was accepted garbled after the re-arms
#define ACAL_PROBE_NOCENTRE  0x02         //flags: the coarse centre failed, no fine points
#define ACAL_PROBE_PRESTROBE 0x04         //flags: read with a write strobe before each sample strobe (A/B test)
#define ACAL_PROBE_WIZARD    0x08         //flags: mode 1, the whole zero step in the wizard's screen context
                                          //flags bits 7:4: the request's settle reads (0 = the wizard's readout)

uint32_t acal_probe_pack(uint8_t *dst, uint8_t ch, uint8_t range, int rc, uint8_t flags, int32_t centre, int32_t pwm,
                         uint32_t enc_bad, uint32_t enc_recovered, const acal_fail *f, const acal_point pt[ACAL_POINTS]);

//Checks m (acal_range_check) and stores it as range r of c: ACAL_OK or the first ACAL_E_* (c unchanged then)
int      acal_range_store(const acal_range_meas *m, acal_channel *c, uint8_t r);

//----------------------------------------------------------------------------------------------------------------------------------
//Gain step (calibration post, generator output low / high, both static)

#define ACAL_GAIN_PROBE_RANGE  2          //first look at the two levels (x1 probe: about 88 codes apart)

//The range to measure on, from the gated span (1/16 code, mean of both cores) at ACAL_GAIN_PROBE_RANGE: the most
//sensitive one whose predicted span is at most ACAL_GAIN_MAX_SPAN16 (nominal 1 : 2 : 5 : 10 : 25 : 50 per range)
uint8_t  acal_gain_range(int32_t span16_probe);

//Offset the levels' midpoint has at range r (1/16 code from 0 V), predicted from mid16_probe (same unit, probe range)
int32_t  acal_gain_mid_at(int32_t mid16_probe, uint8_t r);

//PWM for range r that moves the codes by -off16 from c's centre pwm[r] (gated slopes of both cores), so the two levels
//sit symmetric about the 0 V code; clamped to ACAL_PWM_MIN..ACAL_PWM_MAX
int32_t  acal_gain_pwm(const acal_channel *c, uint8_t r, int32_t off16);

//The measurement: means per class, level (0 low, 1 high, 2 low again) and core, 1/16 code
typedef struct
{
  int32_t  mean16[ACAL_CLASSES][3][2];
  uint32_t clipped;
} acal_gain_meas;

//Checks m (taken on range r) and stores the gains in c: ACAL_OK or the first ACAL_E_GAIN_* (c unchanged then)
int      acal_gain_store(const acal_gain_meas *m, uint8_t r, acal_channel *c);

//Gains for a new zero calibration: those of old (flag ACAL_GAIN_KEPT) when it has measured ones, else unity
void     acal_gain_carry(const acal_channel *old, acal_channel *c);

const char *acal_reason_text(int reason);                     //failure box text, at most 21 characters

//Limits (codes in 1/16, slope Q16, gains Q15)
#define ACAL_MIN_RESPONSE    (100 * 16)   //coarse points: 700 PWM counts must move the mean by >= 100 codes (~185)
#define ACAL_PWM_MIN         200
#define ACAL_PWM_MAX         1800
#define ACAL_MAX_NOISE16     (3 * 16)     //RMS per core at the zero point (bench 0.6..1.3 codes)
#define ACAL_MAX_ZERO_DEV16  (16 * 16)    //|zero - 128| per core and class at the centre PWM
#define ACAL_MAX_SPREAD16    (20 * 16)    //|zero A - zero B| per class (1.00o5 FAIL 2 limit)
#define ACAL_MAX_CLASS16     (12 * 16)    //|zero full - zero gated| per core (bench: 3.6)
#define ACAL_SLOPE_MIN_Q16   (-21627)     //-0.33 codes per count (nominal -0.26)
#define ACAL_SLOPE_MAX_Q16   (-13107)     //-0.20
#define ACAL_MAX_DSLOPE_Q16  1311         //|slope A - slope B| <= 0.02 codes per count, per class
#define ACAL_MAX_DRIFT16     8            //zero point settled: halves agree within 0.5 code
#define ACAL_GAIN_MAX_SPAN16 (120 * 16)   //range choice: the levels at most 120 codes apart (+-60 about 128)
#define ACAL_GAIN_MIN_LEVER16 (40 * 16)   //gated span below 40 codes: no probe on the post (or x10 at range 5 too small)
#define ACAL_GAIN_MAX_DRIFT16 8           //the two low-level measurements agree within 0.5 code
#define ACAL_GAIN_MAX_SPLIT  2621         //|G_A - G_B| <= 0.08 per class (bench: 0.033 full, 0.004 gated)
#define ACAL_GAIN_CLASS_MIN  29491        //(G_A + G_B) full / gated, Q15: 0.9 ..
#define ACAL_GAIN_CLASS_MAX  36045        //.. 1.1 (bench: 1.011)

#define ACAL_OK              0
#define ACAL_E_MAGIC         1            //no block (sector written by an older firmware or blank)
#define ACAL_E_VERSION       2            //another layout (v2 of fw 0.2.43-0.2.44 included)
#define ACAL_E_CRC           3
#define ACAL_E_RESPONSE      4            //offset PWM does not move the codes (coarse points)
#define ACAL_E_CLIP          5            //a measurement point reached code 0 or 255
#define ACAL_E_NOISE         6
#define ACAL_E_ZERO          7
#define ACAL_E_SPREAD        8
#define ACAL_E_SLOPE         9
#define ACAL_E_DRIFT         10
#define ACAL_E_CLASS         11           //full-rate and gated zeros implausibly far apart
#define ACAL_E_GAIN_CLIP     12           //a post level reached code 0 or 255
#define ACAL_E_GAIN_DRIFT    13           //the low level moved between its two measurements
#define ACAL_E_GAIN_LEVER    14           //the two levels too close: no probe on the post
#define ACAL_E_GAIN_SPLIT    15           //core gains differ by more than ACAL_GAIN_MAX_SPLIT
#define ACAL_E_GAIN_CLASS    16           //full-rate / gated gain outside 0.9..1.1
#define ACAL_E_GAIN_NONE     17           //no generator in this bitstream (stock FPGA)
#define ACAL_E_TIMING        18           //full-rate capture still garbled after the retries (enc_heal.h)

#endif /* ADC_CAL_H */
