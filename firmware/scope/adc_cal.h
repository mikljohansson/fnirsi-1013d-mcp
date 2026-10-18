//----------------------------------------------------------------------------------------------------------------------------------
// adc_cal.h - per-core ADC zero calibration ("calibration data v2", firmware/README.md section 6).
//
// Each channel is digitised by two interleaved ADC cores, A (FPGA 0x20 / 0x22, odd trace positions) and B (0x21 / 0x23,
// even positions). Model per core k, hardware range r and clock class c:
//
//     raw_k(0 V) = zero[c][k][r] + slope[k][r] x dcoffset       (zero 1/16 code, slope Q16 codes per PWM count)
//
// Clock class: in legacy mode (K = 0) the FPGA gates the ADC encode clock with the sample-rate divider, and the zero
// moves with it (bench 2026-10-18, CH1: core B 3.6 codes lower as soon as the clock is gated, flat from 20 MSa/s down
// to 1 kSa/s; core A 0.8). ACAL_FULL = divider 0 and every 0x1534 filtered capture (ADCs at full rate), ACAL_GATED =
// legacy divider >= 1.
//
// Every readout turns the model into a correction per core of comp + frac / 16 codes that puts 0 V of both cores at
// T = 128 - dcoffset x 100 / dc_shift_value, the code the 1.00o5 measurements and the screen marker take as 0 V. The
// 8-bit loop adds comp and, with probability frac / 16, one more code (dither), so the mean is exact to 1/16 code
// without a wider buffer. In the 16-bit filter path every output weighs both cores equally (even N), so one constant
// corrects it: corr16 = T16 - (zero_A + zero_B) / 2 at full rate (acal_corr).
//
// The block lives in sector 708 bytes 128..455 (ACAL_SECTOR_OFFSET), outside Atlan's checksummed bytes 16..119, so an
// older firmware still boots with the card (its own save of 708 clears the block). The block is bound to the
// dc_calibration_offset values it was measured at: when sector 709's values differ (an older firmware re-ran the base
// calibration, RESET, defaults) it is ignored and the 1.00o5 compensations apply.
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
#define ACAL_VERSION         2            //"calibration data v2" (Atlan's fields alone are v1)
#define ACAL_HDR_BYTES       12           //magic, version, length, classes, 3 reserved
#define ACAL_CH_BYTES        156          //per channel: pwm 12, zero 48, slope 48, noise 48
#define ACAL_BLOCK_BYTES     (ACAL_HDR_BYTES + 2 * ACAL_CH_BYTES + 4)   //328: header, two channels, CRC-32

#define ACAL_CORE_A          0
#define ACAL_CORE_B          1

typedef struct
{
  uint16_t pwm[ACAL_RANGES];                        //dc_calibration_offset[r] the zeros were measured at (binding)
  int16_t  zero[ACAL_CLASSES][2][ACAL_RANGES];      //[class][core][range] raw mean code at pwm[r], 1/16 code
  int32_t  slope[2][ACAL_RANGES];                   //[core][range] codes per PWM count, Q16 (negative), gated clock
  uint16_t noise[ACAL_CLASSES][2][ACAL_RANGES];     //[class][core][range] RMS about the mean, 1/16 code
} acal_channel;

//Sector block: the two channels <-> ACAL_BLOCK_BYTES bytes (little-endian, CRC-32/ISO-HDLC = zlib.crc32 over all but the
//last 4 bytes)
void     acal_pack(const acal_channel ch[2], uint8_t *block);
int      acal_unpack(const uint8_t *block, acal_channel ch[2]);       //ACAL_OK, ACAL_E_MAGIC, ACAL_E_VERSION, ACAL_E_CRC
uint32_t acal_crc32(const uint8_t *data, uint32_t len);

//1 when c was measured at exactly these PWM centres (cal_offset[0..5])
int      acal_bound(const acal_channel *c, const uint16_t *cal_offset);

//Correction for one readout
typedef struct
{
  int16_t  comp_a;                        //added to every core A code
  int16_t  comp_b;                        //added to every core B code
  uint8_t  frac_a;                        //0..15: one more code for core A with probability frac_a / 16 (v2 only)
  uint8_t  frac_b;
  uint8_t  v2;                            //1: per-core model in use, 0: 1.00o5 compensations
  uint8_t  cls;                           //clock class of the 8-bit correction (ACAL_FULL / ACAL_GATED)
  int32_t  shift;                         //dcoffset x 100 / dc_shift_value (C division), added for the measurements
  int32_t  zero_q8;                       //trace code of 0 V x 256 (mean of both corrected cores)
  int32_t  corr16;                        //16-bit path (ADCs at full rate): added to every converted sample, 1/16 code
} acal_corr;

//Dither for the 8-bit loop: next LCG state (Numerical Recipes constants); its top 4 bits are uniform 0..15, so
//"(state >> 28) < frac" adds one code with probability frac / 16
#define ACAL_DITHER_NEXT(x)  ((x) * 1664525u + 1013904223u)

//c = 0 (or not bound to cal_offset): 1.00o5 behaviour with atlan_a / atlan_b (adc1/adc2compensation). divider: the 0x0D
//value of the capture (legacy clock class)
void     acal_correction(const acal_channel *c, const uint16_t *cal_offset, uint8_t range, uint32_t divider,
                         int16_t dcoffset, int32_t dc_shift_value, int16_t atlan_a, int16_t atlan_b, acal_corr *out);

//Whole-code compensations for per-core corrections dA, dB (1/16 code): |A - B| after them <= 1/2 code, mean <= 1/2 code
//(for the 1.00o5 fields, which have no fraction)
void     acal_round_pair(int32_t da16, int32_t db16, int16_t *comp_a, int16_t *comp_b);

//The 1.00o5 fields from a v2 measurement: compensations for T = 128 at dcoffset 0 from the gated zeros averaged over
//the ranges (the UI's time bases are gated except 200 MSa/s)
void     acal_atlan_comps(const acal_channel *c, int16_t *comp_a, int16_t *comp_b);

//----------------------------------------------------------------------------------------------------------------------------------
//Measurement

typedef struct
{
  uint32_t n;
  uint32_t sum;
  uint64_t sumsq;
  uint32_t clipped;                       //samples at 0 or 255
} acal_acc;

void     acal_acc_add(acal_acc *a, const uint8_t *buf, uint32_t count, uint32_t stride);   //buf[0], buf[stride], ...
void     acal_acc_merge(acal_acc *a, const acal_acc *b);
int32_t  acal_mean16(const acal_acc *a);  //round(16 x mean)
uint32_t acal_rms16(const acal_acc *a);   //16 x RMS about the mean

//Coarse centre: PWM where the mean of both cores reads 128, from the means (1/16 code) at two PWM values. -1 when the
//response is under ACAL_MIN_RESPONSE or the result is outside ACAL_PWM_MIN..ACAL_PWM_MAX
int32_t  acal_centre_pwm(int32_t pwm_lo, int32_t mean16_lo, int32_t pwm_hi, int32_t mean16_hi);

//One range: per core means (1/16 code) at pwm_lo / pwm_hi (gated clock) and the zero point pwm in both classes
typedef struct
{
  int32_t  pwm_lo, pwm_hi, pwm;
  int32_t  mean16_lo[2], mean16_hi[2];
  int32_t  zero16[ACAL_CLASSES][2];
  int32_t  drift16[ACAL_CLASSES][2];      //second half minus first half of the zero-point conversions
  uint32_t noise16[ACAL_CLASSES][2];
  uint32_t clipped;                       //clipped samples over all points
} acal_range_meas;

//Checks m and stores it as range r of c: ACAL_OK or the first ACAL_E_* that applies (c unchanged then)
int      acal_range_store(const acal_range_meas *m, acal_channel *c, uint8_t r);

const char *acal_reason_text(int reason);                     //short text for the failure box

//Limits (codes in 1/16, slope Q16)
#define ACAL_MIN_RESPONSE    (100 * 16)   //coarse points: 700 PWM counts must move the mean by >= 100 codes (~185)
#define ACAL_PWM_MIN         200
#define ACAL_PWM_MAX         1800
#define ACAL_MAX_NOISE16     (3 * 16)     //RMS per core at the zero point (bench 0.6..1.3 codes)
#define ACAL_MAX_ZERO_DEV16  (16 * 16)    //|zero - 128| per core and class at the centre PWM
#define ACAL_MAX_SPREAD16    (20 * 16)    //|zero A - zero B| per class (1.00o5 FAIL 2 limit)
#define ACAL_MAX_CLASS16     (12 * 16)    //|zero full - zero gated| per core (bench: 3.6)
#define ACAL_SLOPE_MIN_Q16   (-21627)     //-0.33 codes per count (nominal -0.26)
#define ACAL_SLOPE_MAX_Q16   (-13107)     //-0.20
#define ACAL_MAX_DSLOPE_Q16  1311         //|slope A - slope B| <= 0.02 codes per count
#define ACAL_MAX_DRIFT16     8            //zero point settled: halves agree within 0.5 code

#define ACAL_OK              0
#define ACAL_E_MAGIC         1            //no block (sector written by an older firmware or blank)
#define ACAL_E_VERSION       2
#define ACAL_E_CRC           3
#define ACAL_E_RESPONSE      4            //offset PWM does not move the codes (coarse points)
#define ACAL_E_CLIP          5            //a measurement point reached code 0 or 255
#define ACAL_E_NOISE         6
#define ACAL_E_ZERO          7
#define ACAL_E_SPREAD        8
#define ACAL_E_SLOPE         9
#define ACAL_E_DRIFT         10
#define ACAL_E_CLASS         11           //full-rate and gated zeros implausibly far apart

#endif /* ADC_CAL_H */
