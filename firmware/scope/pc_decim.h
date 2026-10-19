//----------------------------------------------------------------------------------------------------------------------------------
// pc_decim.h - maths for the AL3 decimation filter: bitstream 0x1534 (FPGA cmds 0x53/0x54/0x55, read 0x29) and the
// CIC compensation FIR of 0x1535 (cmd 0x56).
//
// Integer only (no FPU on the F1C100s), no globals, no hardware: builds for the scope and on the host (host_tests).
//
// FPGA interface (fpga-gen3, fpga/al3/src/FPGA commands.txt):
//   0x53 mode  bits 1:0 K (0 legacy 8-bit sample skipping, 1 boxcar = sum of N, 2 CIC2, 3 CIC3), bit 2 core-order swap,
//              bit 3 / bit 4 capture the A / B cores on the falling write-clock edge (5 ns later; default rising)
//   0x54 S     stored sample = min(65535, y >> S), y = filter output, gain N^K (y = 255 * N^K at full scale)
//   0x55 L16   16-bit trigger level (K >= 1), compared with the stored 16-bit sample of the 0x15 channel
//   0x29       read 2 bytes: {bit 7 16-bit layout active, bit 4 neg B, bit 3 neg A, bit 2 swap, bits 1:0 K}, S
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

#define PC_DECIM_SWAP         0x04      //0x53 / 0x29 bit 2: core A is the earlier sample of each pair
#define PC_DECIM_NEG_A        0x08      //0x53 / 0x29 bit 3: A cores captured on the falling write-clock edge
#define PC_DECIM_NEG_B        0x10      //0x53 / 0x29 bit 4: B cores captured on the falling write-clock edge
#define PC_DECIM_FLAGS        (PC_DECIM_SWAP | PC_DECIM_NEG_A | PC_DECIM_NEG_B)
#define PC_DECIM_WIDE         0x80      //0x29 bit 7: 16-bit layout active (K >= 1)
#define PC_DECIM_FIR_EN       0x40      //0x29 bit 6: 0x56 FIR enable as written (0x1535+)
#define PC_DECIM_FIR_ACT      0x20      //0x29 bit 5: FIR active now (enable, K >= 1, N >= 16, N % 4 == 0)

//Filter setting byte (pc_hal_decim_setting, SET_DECIM): bits 1:0 K, bits 2-4 PC_DECIM_FLAGS, bit 5 FIR requested
#define PC_DECIM_SET_FIR      0x20

//Capability levels for pc_decim_config
#define PC_DECIM_HAS_NONE     0         //bitstream without the filter (0x1533 and older)
#define PC_DECIM_HAS_CIC      1         //0x1534: boxcar / CIC
#define PC_DECIM_HAS_FIR      2         //0x1535+: plus the compensation FIR

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
  uint8_t  k;                 //effective order: 0 legacy, 1..3
  uint8_t  flags;             //0x53 bits 2-4: PC_DECIM_SWAP, PC_DECIM_NEG_A, PC_DECIM_NEG_B (written for every K)
  uint8_t  shift;             //S (0 when k = 0)
  uint8_t  why;               //PC_DECIM_WHY_*
  uint8_t  fir_en;            //0x56 bit 0 as written: FIR requested on a 0x1535+ bitstream (any K)
  uint8_t  fir;               //FIR active: fir_en, k >= 1, n >= 16, n % 4 == 0
  uint32_t n;                 //decimation factor of the whole chain (0 when k = 0); output rate 200 MSa/s / n
  uint64_t gain;              //M^K, M = n (n / 2 with the FIR); 1 when k = 0
  uint32_t scale_q24;         //round(2^(24 + S) / gain): stored sample -> 8-bit ADC code, Q24 (0 when k = 0)
} pc_decim_cfg;

uint32_t pc_decim_n(uint32_t divider);                                   //2 * (floor(D / 2) + 1)
uint64_t pc_decim_gain(uint32_t n, uint8_t k);                           //N^K
uint8_t  pc_decim_shift(uint64_t gain);                                  //max(0, bitlen(255 * gain) - 16)
uint16_t pc_decim_level16(uint8_t level8, uint64_t gain, uint8_t shift); //round(v * N^K / 2^S), at most 65535
uint32_t pc_decim_scale_q24(uint64_t gain, uint8_t shift);               //round(2^(24 + S) / N^K)

//The effective configuration for a request (k_req 0..3, flags PC_DECIM_FLAGS bits plus PC_DECIM_SET_FIR), the 0x0D
//divider and the capture length in samples. has = PC_DECIM_HAS_*: NONE always gives k = 0 and no flags, CIC ignores
//PC_DECIM_SET_FIR.
void     pc_decim_config(uint8_t k_req, uint8_t flags, uint32_t divider, uint32_t total_samples, int has,
                         pc_decim_cfg *c);

//The FIR engages at decimation n (with K >= 1): n >= 16 and n % 4 == 0
int      pc_decim_fir_eligible(uint32_t n);

//EXT trigger with K >= 1: output samples by which the stored data lags the EXT edge, rounded to the nearest sample
//(halves up); 0 for K = 0. Lag 120 ns + K (N - 1) 2.5 ns, with the FIR 250 ns + 17.5 N ns + 1.25 K (N - 2) ns; one
//output sample is N x 5 ns
uint32_t pc_decim_ext_shift(const pc_decim_cfg *c);

//0x53 byte for c (bits 1:0 K, bits 2-4 flags) and the 0x29 byte 0 the FPGA reports for it (plus bit 7 when K >= 1,
//bit 6 FIR enable, bit 5 FIR active)
uint8_t  pc_decim_mode(const pc_decim_cfg *c);
uint8_t  pc_decim_status(const pc_decim_cfg *c);

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

#endif /* PC_DECIM_H */
