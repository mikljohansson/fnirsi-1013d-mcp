//----------------------------------------------------------------------------------------------------------------------------------
// gen_pat.h - the generator as a fractional two-step square (FPGA 0x153D, fw-d5, protocol 1.27; docs/plans/fpga-0x153c.md 7.2 and
// 7.4): the integer maths from a frequency in mHz and a duty in basis points to the period P16 and the high part H16 in
// 1/65536 of a 5 ns clock, the split into the two steps (n_h + f_h / 65536 high, n_l + f_l / 65536 low) and back. No FPU,
// no globals, host-buildable (host_tests/test_fpga_bus.c test_gen_formulas, host_tests/test_session.c).
//
//   P16 = (2^16 x 2e11 + f / 2) / f          f in mHz; 2e11 = 200e6 clocks/s x 1000 mHz/Hz
//   H16 = (P16 x d + 5000) / 10000           d in basis points
//   L16 = P16 - H16;  n_h = H16 >> 16, f_h = H16 & 0xFFFF;  n_l = L16 >> 16, f_l = L16 & 0xFFFF;  n_h, n_l >= 2
//   back: f = (2^16 x 2e11 + P16 / 2) / P16;  d = (H16 x 10000 + P16 / 2) / P16
//
// The FPGA adds each step's fraction to one 16-bit accumulator at the start of the step and lengthens the step by a clock
// on the carry: the mean period is exactly P16 / 65536 clocks and every edge lands within one clock of its ideal time.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef GEN_PAT_H
#define GEN_PAT_H

#include <stdint.h>

#define GP_CLK_HZ        200000000u                    //the generator clock (clk_200MHz), 5 ns
#define GP_NUM           (65536ull * 200000000000ull)  //2^16 x 2e11: P16 x f_mHz
#define GP_STEP_MIN      2u                            //shortest step, clocks (0x6C duration >= 2)
#define GP_STEP_MAX      0xFFFFFFFFu                   //u32 integer duration
#define GP_P16_MIN       (8ull << 16)                  //SET_GEN's 25 MHz cap: 8 clocks
#define GP_P16_LIMIT     (1ull << 49)                  //P16 below this: H16 x 10000 fits 64 bits (f >= 23.3 mHz)
#define GP_FREQ_MAX_MHZ  25000000000ull                //25 MHz

typedef struct
{
  uint32_t n_h, n_l;          //integer clocks of the high and the low step
  uint16_t f_h, f_l;          //their fractions in 1/65536 clock
} gp_square;

uint64_t gp_p16_from_mhz(uint64_t f_mhz);                 //f_mhz >= 1
uint64_t gp_h16_from_bp(uint64_t p16, uint32_t duty_bp);  //p16 < GP_P16_LIMIT
uint64_t gp_mhz_from_p16(uint64_t p16);                   //p16 >= 1
uint16_t gp_bp_from_h16(uint64_t p16, uint64_t h16);      //h16 <= p16 < GP_P16_LIMIT

//The two steps of P16 / H16. 0 = valid; 1 = the period is outside 8 clocks .. 2^49 / 65536 clocks; 2 = the high or the
//low step is shorter than 2 clocks or longer than 2^32 - 1 (sq is filled either way, saturated)
int      gp_split(uint64_t p16, uint64_t h16, gp_square *sq);

//The UI's generator (whole Hz, whole percent): P16 / H16 with H16 clamped so both steps are at least 2 clocks. 0 = a
//square; 1 = a static level instead (frequency 0 or duty 0 %: low; duty 100 %: high), *level says which
int      gp_ui_square(uint32_t freq_hz, uint8_t duty_pct, uint64_t *p16, uint64_t *h16, uint8_t *level);

//The Bode stimulus: an integer period of P clocks, n_h = ceil(P / 2), n_l = floor(P / 2), no fraction
void     gp_bode_square(uint32_t period, uint64_t *p16, uint64_t *h16);

#endif /* GEN_PAT_H */
