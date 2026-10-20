//----------------------------------------------------------------------------------------------------------------------------------
// cap_stage.c - FPGA 0x153A capture stage training, self-check and stored value (cap_stage.h)
//----------------------------------------------------------------------------------------------------------------------------------

#include "cap_stage.h"
#include "adc_cal.h"

int cs_has(uint16_t fpga_version)
{
  return (fpga_version >= 0x153A) && (fpga_version <= 0x153F);
}

uint8_t cs_stage(uint16_t v, uint32_t core)
{
  return (uint8_t)((v >> (3 * core)) & 7);
}

uint16_t cs_with(uint16_t v, uint32_t core, uint8_t stage)
{
  return (uint16_t)((v & ~(7u << (3 * core))) | ((uint32_t)(stage & 7) << (3 * core)));
}

uint16_t cs_ref_bit(uint32_t core)
{
  return (core == 1) ? CS_REF_CH1B : ((core == 3) ? CS_REF_CH2B : 0);
}

#define CLEAN(c, s)  (((c) >> (s)) & 1)

//fw 0.2.81 (bench 2026-10-20: CH1 A clean at 2, 3, 5, bad at 4 picked 3): every clean stage scored by the width of its
//clean run, being 3 or 4, its distance to the nearer end of the run, being the power-on stage or next to it, in that
//order. 0.2.79 kept the power-on stage whenever 3 and 4 were both clean
uint8_t cs_pick(uint8_t clean, uint8_t po)
{
  int     s, lo, hi, margin, score, best_score = -1;
  uint8_t best = CS_NONE;

  if((po != 3) && (po != 4))
    po = 4;

  for(s=CS_STAGE_MIN;s<=CS_STAGE_MAX;s++)
  {
    if(!CLEAN(clean, s))
      continue;

    for(lo=s;(lo > CS_STAGE_MIN) && CLEAN(clean, lo - 1);lo--);
    for(hi=s;(hi < CS_STAGE_MAX) && CLEAN(clean, hi + 1);hi++);

    margin = ((s - lo) < (hi - s)) ? (s - lo) : (hi - s);
    score  = (hi - lo + 1) * 64 + (((s == 3) || (s == 4)) ? 16 : 0) + margin * 4 +
             ((s == po) ? 2 : (((s == po - 1) || (s == po + 1)) ? 1 : 0));

    if(score > best_score)
    {
      best_score = score;
      best       = (uint8_t)s;
    }
  }

  return best;
}

uint32_t cs_band_code(uint32_t i)
{
  return CS_BAND_FIRST + i * CS_BAND_STEP;
}

int32_t cs_band_pwm(int32_t pwm_lo, int32_t mean16_lo, int32_t pwm_hi, int32_t mean16_hi, uint32_t code)
{
  int32_t resp = mean16_lo - mean16_hi, n;

  if((resp <= 0) || (pwm_hi <= pwm_lo))
    return -1;

  //Rounded to the nearest count, either sign
  n = (mean16_lo - (int32_t)(code * 16)) * (pwm_hi - pwm_lo);
  return pwm_lo + ((n >= 0) ? (n + resp / 2) / resp : -((-n + resp / 2) / resp));
}

//One pass over the band: at every point, every core in mask at stage s (with ref set as given), s = CS_STAGE_MIN..MAX,
//io->reps captures each; the counts summed per core and stage, a stage clean when its sum is 0
static int cs_pass(const cs_io *io, cs_train *t, uint16_t base, uint8_t mask, uint32_t sum[CS_CORES][CS_STAGES],
                   uint8_t clean[CS_CORES])
{
  uint16_t cnt[CS_CORES], v;
  uint32_t k, p, r;
  uint8_t  s;

  for(p=0;p<io->points;p++)
  {
    if(io->park(io->ctx, p))
      return CS_E_IO;

    for(s=CS_STAGE_MIN;s<=CS_STAGE_MAX;s++)
    {
      v = base;

      for(k=0;k<CS_CORES;k++)
        if(mask & (1u << k))
          v = cs_with(v, k, s);

      for(r=0;r<io->reps;r++)
      {
        t->captures++;

        if(io->measure(io->ctx, v, cnt))
          return CS_E_IO;

        for(k=0;k<CS_CORES;k++)
          if(mask & (1u << k))
            sum[k][s - CS_STAGE_MIN] += cnt[k];
      }
    }
  }

  for(k=0;k<CS_CORES;k++)
    if(mask & (1u << k))
      for(s=CS_STAGE_MIN;s<=CS_STAGE_MAX;s++)
        if(sum[k][s - CS_STAGE_MIN] == 0)
          clean[k] |= (uint8_t)(1u << s);

  return CS_OK;
}

int cs_train_run(const cs_io *io, uint16_t start, uint8_t cores, cs_train *t)
{
  uint16_t v, base;
  uint32_t k;
  uint8_t  pick[CS_CORES], refmask = 0, s;
  int      rc;

  for(k=0;k<sizeof(*t);k++)
    ((uint8_t *)t)[k] = 0;

  start      = (uint16_t)(start & CS_MASK);
  t->start   = start;
  t->value   = start;
  t->cores   = (uint8_t)(cores & 0x0F);

  //No band, no captures: nothing would be measured (fail fast, not "all clean")
  if(!io->points || !io->reps)
    return CS_E_IO;

  //Own-encode timing for the trained B cores first
  base = start;

  for(k=0;k<CS_CORES;k++)
    if(t->cores & (1u << k))
      base = (uint16_t)(base & ~cs_ref_bit(k));

  rc = cs_pass(io, t, base, t->cores, t->sum, t->clean);

  if(rc != CS_OK)
    return rc;

  v = base;

  for(k=0;k<CS_CORES;k++)
  {
    pick[k] = CS_NONE;

    if(!(t->cores & (1u << k)))
      continue;

    pick[k] = cs_pick(t->clean[k], cs_stage(CS_PO, k));

    if((pick[k] == CS_NONE) && cs_ref_bit(k))
      refmask |= (uint8_t)(1u << k);
  }

  //B cores with no clean stage: again with their chip's reference bit (data-align mode)
  if(refmask)
  {
    uint16_t rbase = base;

    for(k=0;k<CS_CORES;k++)
      if(refmask & (1u << k))
        rbase = (uint16_t)(rbase | cs_ref_bit(k));

    rc = cs_pass(io, t, rbase, refmask, t->sum_ref, t->clean_ref);

    if(rc != CS_OK)
      return rc;

    for(k=0;k<CS_CORES;k++)
    {
      if(!(refmask & (1u << k)))
        continue;

      s = cs_pick(t->clean_ref[k], cs_stage(CS_PO, k));

      if(s != CS_NONE)
      {
        pick[k] = s;
        v = (uint16_t)(v | cs_ref_bit(k));
      }
    }
  }

  for(k=0;k<CS_CORES;k++)
  {
    if(!(t->cores & (1u << k)))
      continue;

    if(pick[k] == CS_NONE)
      t->bad |= (uint8_t)(1u << k);
    else
      v = cs_with(v, k, pick[k]);
  }

  if(t->bad)
    return CS_E_DIRTY;

  t->value = v;
  return CS_OK;
}

void cs_excess(const uint16_t cnt[CS_CORES], uint16_t ex[CS_CORES])
{
  uint32_t k;

  for(k=0;k<CS_CORES;k++)
    ex[k] = (cnt[k] > cnt[k ^ 1]) ? (uint16_t)(cnt[k] - cnt[k ^ 1]) : 0;
}

uint8_t cs_flagged(const uint16_t cnt[CS_CORES])
{
  uint16_t ex[CS_CORES];
  uint32_t k;
  uint8_t  m = 0;

  cs_excess(cnt, ex);

  for(k=0;k<CS_CORES;k++)
    if(ex[k] > CS_EXCESS_LIMIT)
      m |= (uint8_t)(1u << k);

  return m;
}

void cs_watch_clear(cs_watch *w)
{
  uint32_t i;

  for(i=0;i<sizeof(*w);i++)
    ((uint8_t *)w)[i] = 0;
}

uint8_t cs_watch_add(cs_watch *w, uint8_t flagged, uint32_t now_ms)
{
  uint32_t k, i;
  uint8_t  bit, reached = 0;

  for(k=0;k<CS_CORES;k++)
  {
    bit = (uint8_t)(1u << k);

    if(!(flagged & bit))
      continue;

    if(w->n[k] < CS_REPEAT_N - 1)
    {
      w->t[k][w->n[k]++] = now_ms;
      continue;
    }

    //The oldest of the last CS_REPEAT_N - 1 flags within the window (unsigned: the tick wraps)
    if(((uint32_t)(now_ms - w->t[k][0]) <= CS_REPEAT_MS) && !(w->repeat & bit))
    {
      w->repeat |= bit;
      reached   |= bit;
    }

    for(i=0;i+1<CS_REPEAT_N - 1;i++)
      w->t[k][i] = w->t[k][i + 1];

    w->t[k][CS_REPEAT_N - 2] = now_ms;
  }

  return reached;
}

static char *cs_str(char *p, const char *s)
{
  while(*s)
    *p++ = *s++;

  *p = 0;
  return p;
}

char *cs_core_name(char *p, uint32_t core)
{
  p = cs_str(p, (core & 2) ? "CH2 " : "CH1 ");
  return cs_str(p, (core & 1) ? "B" : "A");
}

char *cs_text_ch(char *p, uint16_t v, uint32_t ch)
{
  p = cs_str(p, ch ? "CH2 A" : "CH1 A");
  *p++ = (char)('0' + cs_stage(v, 2 * ch));
  p = cs_str(p, " B");
  *p++ = (char)('0' + cs_stage(v, 2 * ch + 1));
  *p = 0;

  if(v & cs_ref_bit(2 * ch + 1))
    p = cs_str(p, " A-clk");

  return p;
}

char *cs_text(char *p, uint16_t v)
{
  p = cs_text_ch(p, v, 0);
  p = cs_str(p, ", ");
  return cs_text_ch(p, v, 1);
}

static uint8_t *cs_put16(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  return p + 2;
}

static uint32_t cs_get16(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

void cs_trailer_pack(uint16_t v, uint16_t fpga, uint8_t *out)
{
  uint8_t *p = out;
  uint32_t crc;

  p = cs_put16(p, CS_TRAILER_MAGIC & 0xFFFF);
  p = cs_put16(p, CS_TRAILER_MAGIC >> 16);
  p = cs_put16(p, 1);
  p = cs_put16(p, CS_TRAILER_BYTES);
  p = cs_put16(p, v & CS_MASK);
  p = cs_put16(p, fpga);
  crc = acal_crc32(out, 12);
  p = cs_put16(p, crc & 0xFFFF);
  cs_put16(p, crc >> 16);
}

int cs_trailer_unpack(const uint8_t *in, uint16_t *v, uint16_t *fpga)
{
  *v    = 0;
  *fpga = 0;

  if((cs_get16(in) | (cs_get16(in + 2) << 16)) != CS_TRAILER_MAGIC)
    return CS_T_NONE;

  if((cs_get16(in + 4) != 1) || (cs_get16(in + 6) != CS_TRAILER_BYTES) ||
     ((cs_get16(in + 12) | (cs_get16(in + 14) << 16)) != acal_crc32(in, 12)) || (cs_get16(in + 8) & ~CS_MASK))
    return CS_T_BAD;

  *v    = (uint16_t)cs_get16(in + 8);
  *fpga = (uint16_t)cs_get16(in + 10);
  return CS_T_VALID;
}
