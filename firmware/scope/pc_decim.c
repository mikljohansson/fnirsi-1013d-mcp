//----------------------------------------------------------------------------------------------------------------------------------
// pc_decim.c - decimation filter maths (pc_decim.h). Integer only; 64-bit divisions only in the per-setting functions,
// never per sample.
//----------------------------------------------------------------------------------------------------------------------------------

#include "pc_decim.h"

uint32_t pc_decim_n(uint32_t divider)
{
  return 2u * ((divider >> 1) + 1u);
}

uint64_t pc_decim_gain(uint32_t n, uint8_t k)
{
  uint64_t g = 1;

  while(k--)
    g *= n;

  return g;
}

uint8_t pc_decim_shift(uint64_t gain)
{
  uint64_t y = gain * 255u;
  uint8_t  bits = 0;

  while(y)
  {
    bits++;
    y >>= 1;
  }

  return (bits > 16) ? (uint8_t)(bits - 16) : 0;
}

uint16_t pc_decim_level16(uint8_t level8, uint64_t gain, uint8_t shift)
{
  uint64_t y = (uint64_t)level8 * gain;

  if(shift)
    y = (y + (1ull << (shift - 1))) >> shift;

  return (y > 65535u) ? 65535u : (uint16_t)y;
}

uint32_t pc_decim_scale_q24(uint64_t gain, uint8_t shift)
{
  uint64_t num;

  if((gain == 0) || (shift > 39))
    return 0;

  num = 1ull << (24 + shift);
  return (uint32_t)((num + (gain >> 1)) / gain);
}

int pc_decim_fir_eligible(uint32_t n)
{
  return (n >= PC_DECIM_FIR_MIN_N) && ((n & 3) == 0);
}

void pc_decim_config(uint8_t k_req, uint8_t flags, uint32_t divider, uint32_t total_samples, int has,
                     pc_decim_cfg *c)
{
  uint8_t  k = k_req & 3;
  uint32_t n = pc_decim_n(divider), m;

  c->why       = PC_DECIM_WHY_NONE;
  c->k         = 0;
  c->shift     = 0;
  c->n         = 0;
  c->gain      = 1;
  c->scale_q24 = 0;
  c->fir_en    = 0;
  c->fir       = 0;
  c->peak      = 0;
  c->cap       = (uint8_t)((has & PC_DECIM_HAS_CAPSTAGES) != 0);
  has         &= ~PC_DECIM_HAS_CAPSTAGES;

  if(has == PC_DECIM_HAS_NONE)
  {
    c->why = PC_DECIM_WHY_NOCAP;
    return;
  }

  //0x56 is written as requested; the FPGA engages the FIR only where it is eligible. C6: the FIR is off while peak
  //detect is on (the FPGA would ignore it; the firmware writes bit 0 = 0 so 0x29 reads what runs)
  c->fir_en = (has >= PC_DECIM_HAS_FIR) && (flags & PC_DECIM_SET_FIR) && !(flags & PC_DECIM_SET_PEAK);

  if((k == 0) && !(flags & PC_DECIM_SET_PEAK))
  {
    c->why = PC_DECIM_WHY_OFF;
    return;
  }

  //D = 0 with K >= 1 would give N = 2 (100 MSa/s): 200 MSa/s exists only on the legacy path. Peak detect at divider 0
  //has nothing to catch (every sample is stored) and would halve the word rate the same way: legacy record
  if(divider == 0)
  {
    c->why = PC_DECIM_WHY_FULLRATE;
    return;
  }

  if(total_samples > PC_DECIM_MAX_TOTAL)
  {
    c->why = PC_DECIM_WHY_TOTAL;
    return;
  }

  //C6 peak detect: { max, min } words of the N-sample window, no filter maths: K = 1 to 0x53 (the contract's register
  //value; the 0x56 bit alone would select the path), shift 0, gain 1, scale 0 (pc_peak_* convert the bytes); the
  //request's K and FIR are kept in the setting for the way back. Not "lower than requested": why 0
  if(flags & PC_DECIM_SET_PEAK)
  {
    c->peak = 1;
    c->k    = 1;
    c->n    = n;
    return;
  }

  //With the FIR the CIC decimates by M = N/2 and the FIR by 2
  c->fir = c->fir_en && pc_decim_fir_eligible(n);
  m      = c->fir ? n / 2 : n;

  //Exactness: 255 * M^K < 2^48
  if((k == 3) && (m > PC_DECIM_CIC3_MAX_N))
  {
    k = 2;
    c->why = PC_DECIM_WHY_CIC3;
  }

  if((k == 2) && (m > PC_DECIM_CIC2_MAX_N))
  {
    k = 1;
    c->why = PC_DECIM_WHY_CIC2;
  }

  c->k         = k;
  c->n         = n;
  c->gain      = pc_decim_gain(m, k);
  c->shift     = (uint8_t)(pc_decim_shift(c->gain) + (c->fir ? 1 : 0));
  c->scale_q24 = pc_decim_scale_q24(c->gain, c->shift);
}

uint8_t pc_decim_mode(const pc_decim_cfg *c)
{
  return (uint8_t)(c->k & 3);                            //0x53 bits 4..2 are written 0 (pc_decim.h)
}

uint8_t pc_decim_status(const pc_decim_cfg *c)
{
  return (uint8_t)(pc_decim_mode(c) | (pc_decim_wide(c) ? PC_DECIM_WIDE : 0) | (c->fir_en ? PC_DECIM_FIR_EN : 0) |
                   (c->fir ? PC_DECIM_FIR_ACT : 0));
}

uint8_t pc_decim_cmd56(const pc_decim_cfg *c)
{
  return (uint8_t)((c->fir_en ? 0x01 : 0x00) | (c->peak ? 0x02 : 0x00));
}

//----------------------------------------------------------------------------------------------------------------------------------
// C6 peak detect: the mean-core 8-bit correction (pc_decim.h)

uint32_t pc_peak_gain(uint32_t gain_a, uint32_t gain_b)
{
  return (uint32_t)(((uint64_t)gain_a + gain_b + 1) / 2);
}

int32_t pc_peak_off(int32_t off_a, int32_t off_b)
{
  return (int32_t)(((int64_t)off_a + off_b) / 2);
}

int32_t pc_peak_code(uint8_t raw, uint32_t gain, int32_t off)
{
  int64_t v = ((int64_t)raw * gain + off + (1 << 15)) >> 16;

  return (v < 0) ? 0 : ((v > 255) ? 255 : (int32_t)v);
}

int32_t pc_peak_mid(uint8_t mn, uint8_t mx, uint32_t gain, int32_t off)
{
  //(min + max) / 2 through the correction, rounded once: (sum x gain + 2 off + 2^16) >> 17
  int64_t v = ((int64_t)((uint32_t)mn + mx) * gain + ((int64_t)off << 1) + (1 << 16)) >> 17;

  return (v < 0) ? 0 : ((v > 255) ? 255 : (int32_t)v);
}

uint8_t pc_peak_level(uint8_t code8, uint32_t gain, int32_t off)
{
  //code = (raw x gain + off) / 2^16, so raw = (code x 2^16 - off) / gain
  int64_t num = ((int64_t)code8 << 16) - off;

  if(!gain || (num <= 0))
    return 0;

  num = (num + gain / 2) / gain;
  return (num > 255) ? 255 : (uint8_t)num;
}

uint8_t pc_peak_overload(uint8_t mn, uint8_t mx)
{
  return (uint8_t)(((mn == 0) ? 0x01 : 0x00) | ((mx == 255) ? 0xFE : 0x00));
}

int32_t pc_decim_to_code(uint16_t out, uint32_t scale_q24, int32_t corr16)
{
  int64_t acc = (int64_t)((uint64_t)out * scale_q24) + ((int64_t)corr16 << 20) + (1 << 23);
  int32_t v   = (int32_t)(acc >> 24);

  return (v < 0) ? 0 : ((v > 255) ? 255 : v);
}

uint16_t pc_decim_level16_corr(uint8_t code8, uint32_t scale_q24, int32_t corr16)
{
  //code = out x scale / 2^24 + corr16 / 16, so out = (16 code - corr16) x 2^20 / scale
  int64_t num = ((int64_t)code8 * 16 - corr16) * (1ll << 20);

  if(!scale_q24 || (num <= 0))
    return 0;

  num = (num + scale_q24 / 2) / scale_q24;

  return (num > 65535) ? 65535u : (uint16_t)num;
}

uint32_t pc_decim_rate(const pc_decim_cfg *c, uint32_t divider)
{
  if(pc_decim_wide(c) && c->n)
    return 200000000u / c->n;

  return (uint32_t)(200000000ull / ((uint64_t)divider + 1));
}

uint32_t pc_decim_ext_shift(const pc_decim_cfg *c)
{
  uint64_t lag, period;
  uint32_t k = c->peak ? 1 : c->k;        //C6: peak detect has the K = 1 boxcar's latency (fpga-c2, tb_peak)

  if(!pc_decim_wide(c) || !c->n)
    return 0;

  if(c->fir)
  {
    //In units of 0.25 ns: 250 ns = 1000, 17.5 N ns = 70 N, 1.25 K (N - 2) ns = 5 K (N - 2), one period N x 5 ns = 20 N
    lag    = 1000 + (uint64_t)c->n * 70 + (uint64_t)k * (c->n - 2) * 5 + (c->cap ? 160 : 0);      //0x153A: + 40 ns
    period = (uint64_t)c->n * 20;
  }
  else
  {
    //In units of 0.5 ns: 120 ns = 240, 2.5 ns = 5, one output period N x 5 ns = 10 N
    lag    = 240 + (uint64_t)k * (c->n - 1) * 5 + (c->cap ? 80 : 0);                                //0x153A: + 40 ns
    period = (uint64_t)c->n * 10;
  }

  return (uint32_t)((lag + period / 2) / period);
}

uint32_t pc_decim_ext_shift_legacy(uint32_t divider, int cap)
{
  if(cap)
    return divider ? 14 : 12;

  return divider ? 8 : 4;
}

uint32_t pc_decim_ext_shift_at(const pc_decim_cfg *c, uint32_t divider)
{
  return pc_decim_wide(c) ? pc_decim_ext_shift(c) : pc_decim_ext_shift_legacy(divider, c->cap);
}

uint32_t pc_decim_settle_us(const pc_decim_cfg *c)
{
  uint64_t t;

  if(!pc_decim_wide(c))
    return 0;

  //(K + 2) output periods of N x 5 ns; with the FIR (K + 10) N x 5 ns + 1 us. C6: after a 0x56 peak write as after a
  //filter write, (K + 10) N x 5 ns + 1 us with K = 0 counting as 1 (fpga-c2, FPGA commands.txt)
  if(c->peak)
    t = ((uint64_t)11 * c->n + 199) / 200 + 1;
  else if(c->fir)
    t = ((uint64_t)(c->k + 10) * c->n + 199) / 200 + 1;
  else
    t = ((uint64_t)(c->k + 2) * c->n + 199) / 200;
  return (t > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)t;
}

//ns per word: 10 (D + 1) in the legacy layout (two samples per word), 5 (D + 1) with K >= 1; at most 2^34
static uint64_t pc_word_ns(uint32_t divider, int wide)
{
  return (uint64_t)5 * ((uint64_t)divider + 1) * (wide ? 1u : 2u);
}

uint32_t pc_ms_to_words(uint32_t ms, uint32_t divider, int wide)
{
  uint64_t w = ((uint64_t)ms * 1000000ull) / pc_word_ns(divider, wide);

  return (w > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)w;
}

uint32_t pc_words_to_ms(uint32_t words, uint32_t divider, int wide)
{
  uint64_t ns = pc_word_ns(divider, wide), ms;

  //words < 2^32, ns <= 2^34: split so the product cannot overflow
  ms = (uint64_t)(words / 1000000u) * ns + ((uint64_t)(words % 1000000u) * ns + 999999ull) / 1000000ull;
  return (ms > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)ms;
}
