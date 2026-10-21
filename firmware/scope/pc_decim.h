//----------------------------------------------------------------------------------------------------------------------------------
// pc_decim.h - maths for the AL3 decimation filter: bitstream 0x1534 (FPGA cmds 0x53/0x54/0x55, read 0x29) and the
// CIC compensation FIR of 0x1535 (cmd 0x56).
//
// Integer only (no FPU on the F1C100s), no globals, no hardware: builds for the scope and on the host (host_tests).
//
// FPGA interface (fpga-gen3, fpga/al3/src/FPGA commands.txt):
//   0x53 mode  bits 1:0 K (0 legacy 8-bit sample skipping, 1 boxcar = sum of N, 2 CIC2, 3 CIC3); bits 4..2 (the
//              core-order swap and the falling-edge capture of older bitstreams) are ignored since 0x153C, written 0
//   0x54 S     stored sample = min(65535, y >> S), y = filter output, gain N^K (y = 255 * N^K at full scale)
//   0x55 L16   16-bit trigger level (K >= 1), compared with the stored 16-bit sample of the 0x15 channel
//   0x29       read 2 bytes: {bit 7 16-bit layout active, bits 4..2 0 (0x153C), bits 1:0 K}, S
//   Latency: 12 write clocks (120 ns) from pin to memory for every K and N; a 16-bit sample is centred a further
//   K * (N - 1) * 2.5 ns earlier. The ADC trigger is self-consistent; an EXT edge needs pc_decim_ext_shift.
//   N = 2 * (floor(D / 2) + 1), D = the 0x0D divider; output rate 200 MSa/s / N (= 200 / (D + 1) for odd D)
//   K >= 1: one 16-bit output sample per channel per 32-bit word (low byte 0x20/0x22, high byte 0x21/0x23); 0x0B,
//   0x0C, 0x14 and 0x0E count output samples (words), at most 8191.
//
// FIR (0x1535, fpga-gen4, fpga/al3/src/FPGA commands.txt): 0x56 bit 0 = FIR enable (power-on 0). Active only when 0x56 = 1,
//   K >= 1, N >= 16 and N % 4 == 0 (D >= 14, D mod 4 in {2, 3}); read 0x29 byte 0 bit 6 = enable as written, bit 5 =
//   active now. Chain: CIC of order K decimating by N/2 (gain (N/2)^K), shift S, saturate to 16 bit, 15-tap FIR
//   decimating by 2 with DC gain 1, so the output rate stays 200 MSa/s / N. S = max(0, bitlen(255 (N/2)^K) - 16) + 1
//   (+1: headroom for the 6-9 % step overshoot; the worst-case FIR gain 1.21 cannot saturate). Exactness 255 (N/2)^K
//   < 2^48 (CIC3 while N/2 <= 10334). Latency 25 write clocks (250 ns) pin to memory; an EXT edge leads the data by
//   250 ns + 17.5 N ns + 1.25 K (N - 2) ns; settle (K + 10) N 5 ns + 1 us after a change of 0x0D, 0x53, 0x54 or 0x56.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_DECIM_H
#define PC_DECIM_H

#include <stdint.h>

#define PC_DECIM_OFF          0
#define PC_DECIM_BOXCAR       1
#define PC_DECIM_CIC2         2
#define PC_DECIM_CIC3         3

//0x53 bits 4..2 (bit 2 the core-order swap, bits 3 / 4 the falling-edge capture up to 0x1539) select nothing on 0x153C,
//which removed them (fpga/al3/README.md section 15): the firmware writes K alone. SET_DECIM flags bits 0..2 are accepted
//and dropped (bits 1 / 2 since fw-one, bit 0 since fw-cruft); GET_DECIM reports them 0

//The largest divider (0x0D) the firmware writes: the FPGA compares 20 bits (0x153C cruft audit, docs/plans/fpga-0x153c.md
//section 13). The UI's table reaches 399,999 (500 Sa/s), a host raw divider above this is E_BAD_ARG
#define PC_DIVIDER_MAX        0xFFFFFu
#define PC_DECIM_WIDE         0x80      //0x29 bit 7: 16-bit layout active (K >= 1, or peak detect)
#define PC_DECIM_FIR_EN       0x40      //0x29 bit 6: 0x56 FIR enable as written (0x1535+)
#define PC_DECIM_FIR_ACT      0x20      //0x29 bit 5: FIR active now (enable, K >= 1, N >= 16, N % 4 == 0)
//C6 peak detect (0x153C, fw-c6): the shift byte (0x29 byte 1; GET_DECIM 3 / 25 / 27, GET_CAPTURE_INFO 69, READ_TRACE 31)
//carries the peak flags above the 6-bit S: a peak record reads mode 0x81 (16-bit path, K 1) and shift byte 0xC0
#define PC_DECIM_PEAK_ACT     0x80      //0x29 byte 1 bit 7: peak detect active (= bit 6 on 0x153C: active whenever written)
#define PC_DECIM_PEAK_EN      0x40      //0x29 byte 1 bit 6: 0x56 bit 1 as written
#define PC_DECIM_SHIFT_MASK   0x3F      //0x29 byte 1 bits 5..0: the shift S

//Filter setting byte (pc_hal_decim_setting, SET_DECIM): bits 1:0 K, bits 4..2 0, bit 5 FIR requested, bit 6
//peak detect requested (C6: SET_DECIM mode 4, the ACQ Sample row; the K and FIR bits are kept for the way back)
#define PC_DECIM_SET_FIR      0x20
#define PC_DECIM_SET_PEAK     0x40
#define PC_DECIM_SET_MASK     (3 | PC_DECIM_SET_FIR | PC_DECIM_SET_PEAK)

//Capability levels for pc_decim_config
#define PC_DECIM_HAS_NONE     0         //bitstream without the filter (0x1533 and older)
#define PC_DECIM_HAS_CIC      1         //0x1534: boxcar / CIC
#define PC_DECIM_HAS_FIR      2         //0x1535+: plus the compensation FIR
#define PC_DECIM_HAS_CAPSTAGES 0x10    //or-ed in: 0x153A+ capture stages (4 / 3 words more ADC latency against EXT, 40 ns
                                        //at K >= 1; 0x53 bits 3 / 4 inert, written 0)

//Filter setting of new settings (no valid settings word, or the DEFAULT button), per mode since fw 0.2.64 (owner,
//2026-10-19): the Scope trace defaults to CIC2 without the FIR (the FIR's 6-9 % step overshoot pulses on a square wave),
//the Spectrum to CIC2 + FIR (flat passband, alias rejection 78-83 dB on 0x1535). On bitstreams without the FIR the bit is
//ignored (pc_dec_mode_eff) and the setting acts as CIC2. Change them here only. PC_DECIM_DEFAULT_OLD is the single
//default of 0.2.44..0.2.63, used to migrate their settings sectors.
#define PC_DECIM_DEFAULT_SCOPE  (PC_DECIM_CIC2)
#define PC_DECIM_DEFAULT_SPEC   (PC_DECIM_CIC2 | PC_DECIM_SET_FIR)
#define PC_DECIM_DEFAULT_OLD    (PC_DECIM_CIC2 | PC_DECIM_SET_FIR)

//FIR eligibility: N >= 16 (the 15-tap FIR needs N/2 write clocks per output) and N a multiple of 4
#define PC_DECIM_FIR_MIN_N    16u

//Largest capture in the 16-bit layout: 13-bit word counters (0x0B/0x0C), even sample counts as everywhere else
#define PC_DECIM_MAX_TOTAL    8190

//Exactness limits: 255 * M^K < 2^48 (the FPGA's 48-bit accumulators), M = the CIC's decimation (N, or N/2 with the FIR)
#define PC_DECIM_CIC2_MAX_N   1050630u
#define PC_DECIM_CIC3_MAX_N   10334u

//Why the effective K is lower than the requested one (GET_DECIM byte 30)
#define PC_DECIM_WHY_NONE     0
#define PC_DECIM_WHY_OFF      1         //filter off (requested K = 0)
#define PC_DECIM_WHY_FULLRATE 2         //divider 0 (200 MSa/s): only the legacy path reaches it
#define PC_DECIM_WHY_TOTAL    3         //capture longer than PC_DECIM_MAX_TOTAL samples
#define PC_DECIM_WHY_CIC3     4         //CIC3 not exact at this N, CIC2 used
#define PC_DECIM_WHY_CIC2     5         //CIC2 not exact at this N, boxcar used
#define PC_DECIM_WHY_NOCAP    6         //bitstream without the filter

typedef struct
{
  uint8_t  k;                 //effective order: 0 legacy, 1..3 (1 with peak detect: 0x53 = 1, the contract's K)
  uint8_t  shift;             //S (0 when k = 0)
  uint8_t  why;               //PC_DECIM_WHY_*
  uint8_t  fir_en;            //0x56 bit 0 as written: FIR requested on a 0x1535+ bitstream (any K; 0 with peak detect)
  uint8_t  fir;               //FIR active: fir_en, k >= 1, n >= 16, n % 4 == 0
  uint32_t n;                 //decimation factor of the whole chain (0 when k = 0); output rate 200 MSa/s / n. Peak
                              //detect: the window, N raw samples per { max, min } word
  uint64_t gain;              //M^K, M = n (n / 2 with the FIR); 1 when k = 0 or peak
  uint32_t scale_q24;         //round(2^(24 + S) / gain): stored sample -> 8-bit ADC code, Q24 (0 when k = 0 or peak)
  uint8_t  cap;               //0x153A+ capture stages (PC_DECIM_HAS_CAPSTAGES): the ADC data reach the memory later
  //C6 peak detect (0x153C, 0x56 bit 1, docs/plans/fpga-0x153c.md section 4): one 16-bit word per sample per channel =
  //{ max (high byte, 0x21 / 0x23), min (low byte, 0x20 / 0x22) } of the window's N raw 8-bit codes, the ADCs at 200 MSa/s,
  //the FIR off, 0x53 = 1 and 0x54 = 0 (the contract's K = 1; the Filter setting is kept for the way back), the K = 1
  //boxcar's latency. k = 1, gain 1, shift 0, scale 0 (no filter maths: pc_peak_* convert the bytes). Refused at divider 0
  //(nothing to catch at 200 MSa/s: why 2) and over 8190 samples (why 3), as the filter is
  uint8_t  peak;
} pc_decim_cfg;

//The 16-bit layout (one sample per ring word): the filter on, or peak detect
static inline int pc_decim_wide(const pc_decim_cfg *c) { return (c->k != 0) || c->peak; }

//The shift byte reported for c (GET_DECIM 3 / 25, GET_CAPTURE_INFO 69, READ_TRACE 31): S, with bits 7 / 6 for a peak
//record, as the FPGA's 0x29 byte 1 reads it
static inline uint8_t pc_decim_shift_byte(const pc_decim_cfg *c)
{
  return (uint8_t)((c->shift & PC_DECIM_SHIFT_MASK) | (c->peak ? (PC_DECIM_PEAK_ACT | PC_DECIM_PEAK_EN) : 0));
}

uint32_t pc_decim_n(uint32_t divider);                                   //2 * (floor(D / 2) + 1)
uint64_t pc_decim_gain(uint32_t n, uint8_t k);                           //N^K
uint8_t  pc_decim_shift(uint64_t gain);                                  //max(0, bitlen(255 * gain) - 16)
uint16_t pc_decim_level16(uint8_t level8, uint64_t gain, uint8_t shift); //round(v * N^K / 2^S), at most 65535
uint32_t pc_decim_scale_q24(uint64_t gain, uint8_t shift);               //round(2^(24 + S) / N^K)

//The effective configuration for a request (k_req 0..3, flags PC_DECIM_SET_FIR and
//PC_DECIM_SET_PEAK), the 0x0D divider and the capture length in samples. has = PC_DECIM_HAS_*: NONE always gives k = 0
//and no flags, CIC ignores PC_DECIM_SET_FIR. PC_DECIM_SET_PEAK (C6) wins over K: the record is peak words (c->peak, k 1,
//n = the window, why 0) under the same divider / total rules as the filter; the request's K and FIR are not applied.
void     pc_decim_config(uint8_t k_req, uint8_t flags, uint32_t divider, uint32_t total_samples, int has,
                         pc_decim_cfg *c);

//C6 peak records: the two raw 8-bit codes of a word come from either core, so one 8-bit correction is applied to both:
//the mean of the two cores' corrections (adc_cal.h acal_corr gain_a / gain_b / off_a / off_b, Q16, GET_CAL
//92..107). The gain is the arithmetic mean rounded, the offset the mean (C division)
uint32_t pc_peak_gain(uint32_t gain_a, uint32_t gain_b);
int32_t  pc_peak_off(int32_t off_a, int32_t off_b);
//One raw code through it: clamp((raw x gain + off + 2^15) >> 16, 0, 255), the 8-bit loop's formula without the dither
int32_t  pc_peak_code(uint8_t raw, uint32_t gain, int32_t off);
//The processed trace's sample of a peak word: the corrected midpoint of min and max, rounded
int32_t  pc_peak_mid(uint8_t mn, uint8_t mx, uint32_t gain, int32_t off);
//The inverse for the trigger: the raw code whose corrected value is code8 (0x55 = this << 8), rounded, clamped 0..255
uint8_t  pc_peak_level(uint8_t code8, uint32_t gain, int32_t off);
//The overload rule of a peak word (protocol row 66): bit 0 when the min byte is 0, bits 1-7 when the max byte is 255
//(ADoverload's layout: 1 / 254)
uint8_t  pc_peak_overload(uint8_t mn, uint8_t mx);

//The FIR engages at decimation n (with K >= 1): n >= 16 and n % 4 == 0
int      pc_decim_fir_eligible(uint32_t n);

//EXT trigger with K >= 1: output samples by which the stored data lags the EXT edge, rounded to the nearest sample
//(halves up); 0 for K = 0. Lag 120 ns + K (N - 1) 2.5 ns, with the FIR 250 ns + 17.5 N ns + 1.25 K (N - 2) ns; one
//output sample is N x 5 ns
uint32_t pc_decim_ext_shift(const pc_decim_cfg *c);

//EXT trigger in the legacy 8-bit layout (K = 0, fw 0.2.72): samples (two per word) by which the stored data lag the EXT
//edge. The AD9288 outputs a sample 4 encode cycles after taking it, one word at every legacy rate, while the EXT trigger
//address is taken at the edge (fpga/al3/README.md section 7 finding 4). tb_trigpos (0x1537 / 0x1538 model): an EXT edge
//lands at word +5..+6 at D 9 and D 199 and at +3.5 at D 0 (the synchroniser's 15-20 ns are most of a 10 ns word there),
//an ADC trigger at word +1..+1.5. Shift: 4 words, 2 at divider 0. Model numbers; bench check in firmware/AGENTS.md
//section 48 (D 1..8 not simulated)
//0x153A+ (cap): the IOB capture and the stage pick take 4 words more at divider 0 and 3 at divided rates (fpga/al3/README.md
//section 10, tb_trigpos: legacy EXT +14..15 samples at D 0, +16..17 at D 9, +17..18 at D 199 against CH1 +2..3): 12 / 14
uint32_t pc_decim_ext_shift_legacy(uint32_t divider, int cap);

//The EXT shift of a capture with configuration c at divider (0x0D): pc_decim_ext_shift for K >= 1, else the legacy one.
//Samples of the capture's layout; the pretrigger words written are (pretrigger - shift) (16-bit) or / 2 (legacy)
uint32_t pc_decim_ext_shift_at(const pc_decim_cfg *c, uint32_t divider);

//0x53 byte for c (bits 1:0 K, bits 2-4 flags) and the mode byte the firmware reports for it: 0x29 byte 0 (plus bit 7
//when the layout is 16-bit, bit 6 FIR enable, bit 5 FIR active); a peak record reads 0x81
uint8_t  pc_decim_mode(const pc_decim_cfg *c);
uint8_t  pc_decim_status(const pc_decim_cfg *c);
//The 0x56 byte for c: bit 0 FIR enable, bit 1 peak detect (C6)
uint8_t  pc_decim_cmd56(const pc_decim_cfg *c);

//Stored 16-bit sample -> 8-bit ADC code in Q24 (out * 2^S / N^K) plus corr16 / 16 codes (the zero correction,
//adc_cal.h acal_corr.corr16), rounded and clamped to 0..255
int32_t  pc_decim_to_code(uint16_t out, uint32_t scale_q24, int32_t corr16);

//The inverse for the trigger: the 16-bit level (0x54) for a level in the corrected 8-bit trace domain, i.e. the stored
//sample value that pc_decim_to_code maps onto code8 with the same scale (scale_q24 with the readout's gain16 folded in,
//adc_cal.h acal_scale) and corr16. Rounded, clamped to 0..65535. With corr16 0 and scale = cfg.scale_q24 it equals
//pc_decim_level16 to within one 16-bit step (scale_q24 is rounded).
uint16_t pc_decim_level16_corr(uint8_t code8, uint32_t scale_q24, int32_t corr16);

//Output sample rate in Sa/s (200 MSa/s / N for k >= 1, 200 MSa/s / (D + 1) for k = 0)
uint32_t pc_decim_rate(const pc_decim_cfg *c, uint32_t divider);

//Filter settling after a change of 0x0D, 0x53, 0x54 or 0x56, in microseconds, rounded up: (K + 2) output periods,
//with the FIR (K + 10) N x 5 ns + 1 us
uint32_t pc_decim_settle_us(const pc_decim_cfg *c);

//Words written in ms at divider D (a word is 10 (D + 1) ns in the legacy layout, 5 (D + 1) ns with K >= 1; clamped to
//0xFFFFFFFF), and the ms that words take (rounded up)
uint32_t pc_ms_to_words(uint32_t ms, uint32_t divider, int wide);
uint32_t pc_words_to_ms(uint32_t words, uint32_t divider, int wide);

#endif /* PC_DECIM_H */
