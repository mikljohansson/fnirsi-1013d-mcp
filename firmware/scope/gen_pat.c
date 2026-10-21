//----------------------------------------------------------------------------------------------------------------------------------
// gen_pat.c - the generator's two-step square maths (gen_pat.h)
//----------------------------------------------------------------------------------------------------------------------------------

#include "gen_pat.h"

uint64_t gp_p16_from_mhz(uint64_t f_mhz)
{
  return (GP_NUM + f_mhz / 2) / f_mhz;
}

uint64_t gp_h16_from_bp(uint64_t p16, uint32_t duty_bp)
{
  return (p16 * duty_bp + 5000) / 10000;
}

uint64_t gp_mhz_from_p16(uint64_t p16)
{
  return (GP_NUM + p16 / 2) / p16;
}

uint16_t gp_bp_from_h16(uint64_t p16, uint64_t h16)
{
  return (uint16_t)((h16 * 10000 + p16 / 2) / p16);
}

int gp_split(uint64_t p16, uint64_t h16, gp_square *sq)
{
  uint64_t l16 = (h16 <= p16) ? p16 - h16 : 0;
  uint64_t nh = h16 >> 16, nl = l16 >> 16;

  sq->n_h = (nh > GP_STEP_MAX) ? GP_STEP_MAX : (uint32_t)nh;
  sq->n_l = (nl > GP_STEP_MAX) ? GP_STEP_MAX : (uint32_t)nl;
  sq->f_h = (uint16_t)h16;
  sq->f_l = (uint16_t)l16;

  if((p16 < GP_P16_MIN) || (p16 >= GP_P16_LIMIT))
    return 1;

  if((h16 > p16) || (nh < GP_STEP_MIN) || (nl < GP_STEP_MIN) || (nh > GP_STEP_MAX) || (nl > GP_STEP_MAX))
    return 2;

  return 0;
}

int gp_ui_square(uint32_t freq_hz, uint8_t duty_pct, uint64_t *p16, uint64_t *h16, uint8_t *level)
{
  uint64_t p, h, lo = (uint64_t)GP_STEP_MIN << 16;

  *p16 = *h16 = 0;
  *level = (uint8_t)(duty_pct >= 100);

  if(!freq_hz || !duty_pct || (duty_pct >= 100))
    return 1;

  p = gp_p16_from_mhz((uint64_t)freq_hz * 1000);

  //The UI's 10 MHz limit keeps P >= 20 clocks
  if(p < GP_P16_MIN)
    p = GP_P16_MIN;

  h = gp_h16_from_bp(p, (uint32_t)duty_pct * 100);

  if(h < lo)
    h = lo;

  if(h > p - lo)
    h = p - lo;

  *p16 = p;
  *h16 = h;
  return 0;
}

void gp_bode_square(uint32_t period, uint64_t *p16, uint64_t *h16)
{
  *p16 = (uint64_t)period << 16;
  *h16 = (uint64_t)((period + 1) / 2) << 16;
}
