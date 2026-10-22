//----------------------------------------------------------------------------------------------------------------------------------
// cap_stage.c - FPGA 0x153A capture stage training, self-check and stored value (cap_stage.h)
//----------------------------------------------------------------------------------------------------------------------------------

#include "cap_stage.h"
#include "adc_cal.h"

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

//fw 0.2.105 (cap_stage.h "Pick"): every clean stage scored by the width of its clean run, being 3 or 4, its distance to
//the nearer end of the run, in that order; *tie = the stages with the top score. 0.2.81 broke a tie toward the power-on
//stage (bench 2026-10-22 on 0x153D: CH1 A 2..5 clean on the quiet input, the power-on 4 garbled a real signal)
static uint8_t cs_best(uint8_t clean, uint8_t *tie)
{
  int     s, lo, hi, margin, score, best_score = -1;
  uint8_t best = CS_NONE;

  *tie = 0;

  for(s=CS_STAGE_MIN;s<=CS_STAGE_MAX;s++)
  {
    if(!CLEAN(clean, s))
      continue;

    for(lo=s;(lo > CS_STAGE_MIN) && CLEAN(clean, lo - 1);lo--);
    for(hi=s;(hi < CS_STAGE_MAX) && CLEAN(clean, hi + 1);hi++);

    margin = ((s - lo) < (hi - s)) ? (s - lo) : (hi - s);
    score  = (hi - lo + 1) * 64 + (((s == 3) || (s == 4)) ? 16 : 0) + margin * 4;

    //Equal scores: the lower stage stays (the loop runs upwards)
    if(score > best_score)
    {
      best_score = score;
      best       = (uint8_t)s;
      *tie       = (uint8_t)(1u << s);
    }
    else if(score == best_score)
      *tie |= (uint8_t)(1u << s);
  }

  //One stage at the top: no tie
  if(!(*tie & (*tie - 1)))
    *tie = 0;

  return best;
}

uint8_t cs_pick(uint8_t clean)
{
  uint8_t tie;

  return cs_best(clean, &tie);
}

uint8_t cs_tie(uint8_t clean)
{
  uint8_t tie;

  (void)cs_best(clean, &tie);
  return tie;
}

uint32_t cs_band_code16(uint32_t i)
{
  return (CS_BAND_K0 + i) * 128 - 8;
}

int32_t cs_band_pwm(int32_t pwm_lo, int32_t mean16_lo, int32_t pwm_hi, int32_t mean16_hi, uint32_t code16)
{
  int32_t resp = mean16_lo - mean16_hi, n;

  if((resp <= 0) || (pwm_hi <= pwm_lo))
    return -1;

  //Rounded to the nearest count, either sign
  n = (mean16_lo - (int32_t)code16) * (pwm_hi - pwm_lo);
  return pwm_lo + ((n >= 0) ? (n + resp / 2) / resp : -((-n + resp / 2) / resp));
}

uint32_t cs_resid(const uint8_t *x, uint32_t n)
{
  uint32_t j, e = 0;
  int32_t  a, b, c, m, d;

  for(j=1;j+1<n;j++)
  {
    a = x[j - 1];
    b = x[j];
    c = x[j + 1];

    //Median of three
    if(a > c)
    {
      m = a;
      a = c;
      c = m;
    }

    m = (b < a) ? a : ((b > c) ? c : b);
    d = b - m;
    e += (uint32_t)(d * d);
  }

  return e;
}

uint32_t cs_q_best(const uint32_t sum[CS_STAGES], const uint32_t q[CS_STAGES])
{
  uint32_t i, qmin = 0xFFFFFFFFu;

  for(i=0;i<CS_STAGES;i++)
    if((sum[i] == 0) && (q[i] < qmin))
      qmin = q[i];

  return qmin;
}

uint8_t cs_quiet(const uint32_t sum[CS_STAGES], const uint32_t q[CS_STAGES])
{
  uint32_t i, qmin = cs_q_best(sum, q);
  uint8_t  quiet = 0;

  if(qmin == 0xFFFFFFFFu)
    return 0;

  //Within 25 % of the best outlier-clean stage plus 1/4 code^2 (u32: q stays far below 2^32 / 5, section 18 "Range")
  for(i=0;i<CS_STAGES;i++)
    if((sum[i] == 0) && (q[i] <= qmin * 5 / 4 + 4))
      quiet |= (uint8_t)(1u << (i + CS_STAGE_MIN));

  return quiet;
}

//One pass over the band: at every point, every core in mask at stage s (with ref set as given), s = CS_STAGE_MIN..MAX,
//io->reps captures each; the 0x36 counts and E / CS_RESID_DIV summed per core and stage; clean = the stages with sum 0,
//quiet = cs_quiet
static int cs_pass(const cs_io *io, cs_train *t, uint16_t base, uint8_t mask, uint32_t sum[CS_CORES][CS_STAGES],
                   uint32_t q[CS_CORES][CS_STAGES], uint8_t clean[CS_CORES], uint8_t quiet[CS_CORES])
{
  uint16_t cnt[CS_CORES], v;
  uint32_t e[CS_CORES], k, p, r;
  uint8_t  s;

  for(p=0;p<io->points;p++)
  {
    if(io->park(io->ctx, cs_band_code16(p)))
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

        for(k=0;k<CS_CORES;k++)
          e[k] = 0;

        if(io->measure(io->ctx, v, mask, cnt, e))
          return CS_E_IO;

        for(k=0;k<CS_CORES;k++)
          if(mask & (1u << k))
          {
            sum[k][s - CS_STAGE_MIN] += cnt[k];
            q[k][s - CS_STAGE_MIN]   += e[k] / CS_RESID_DIV;
          }
      }
    }
  }

  for(k=0;k<CS_CORES;k++)
    if(mask & (1u << k))
    {
      for(s=CS_STAGE_MIN;s<=CS_STAGE_MAX;s++)
        if(sum[k][s - CS_STAGE_MIN] == 0)
          clean[k] |= (uint8_t)(1u << s);

      quiet[k] = cs_quiet(sum[k], q[k]);
    }

  return CS_OK;
}

//The lowest and the highest stage of a mask
static uint8_t cs_low(uint8_t m)
{
  uint8_t s = 0;

  while(m && !(m & 1))
  {
    m >>= 1;
    s++;
  }

  return s;
}

static uint8_t cs_high(uint8_t m)
{
  uint8_t s = 0;

  while(m >>= 1)
    s++;

  return s;
}

//Tie pass: the cores with a tie (t->tie) at each of their two stages, the others at their pick (in v), io->tie_reps
//captures per stage at codes CS_TIE_CODE + 2 p (p = CS_TIE_POINTS - 1 .. 0: from the top down), 0x36 counts only; a
//candidate that counts leaves the core's set
static int cs_tie_pass(const cs_io *io, cs_train *t, uint16_t v)
{
  uint16_t cnt[CS_CORES], w;
  uint32_t e[CS_CORES], k, p, r, j;

  for(p=CS_TIE_POINTS;p-- > 0;)
  {
    if(io->park(io->ctx, (CS_TIE_CODE + 2 * p) * 16))
      return CS_E_IO;

    for(j=0;j<2;j++)
    {
      w = v;

      for(k=0;k<CS_CORES;k++)
        if(t->tie[k])
          w = cs_with(w, k, j ? cs_high(t->tie[k]) : cs_low(t->tie[k]));

      for(r=0;r<io->tie_reps;r++)
      {
        t->captures++;

        if(io->measure(io->ctx, w, 0, cnt, e))
          return CS_E_IO;

        for(k=0;k<CS_CORES;k++)
          if(t->tie[k])
            t->tie_sum[k][j] += cnt[k];
      }
    }
  }

  for(k=0;k<CS_CORES;k++)
  {
    if(!t->tie[k])
      continue;

    if(t->tie_sum[k][0])
      t->set[k] &= (uint8_t)~(1u << cs_low(t->tie[k]));

    if(t->tie_sum[k][1])
      t->set[k] &= (uint8_t)~(1u << cs_high(t->tie[k]));
  }

  return CS_OK;
}

int cs_train_run(const cs_io *io, uint16_t start, uint8_t cores, cs_train *t)
{
  uint16_t v, base;
  uint32_t k;
  uint8_t  refmask = 0, ties = 0;
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

  rc = cs_pass(io, t, base, t->cores, t->sum, t->q, t->clean, t->quiet);

  if(rc != CS_OK)
    return rc;

  //fw-capmetric: the pick runs on the quiet set (a core has a quiet stage exactly when it has a clean one)
  for(k=0;k<CS_CORES;k++)
  {
    if(!(t->cores & (1u << k)))
      continue;

    t->set[k] = t->quiet[k];

    if((cs_pick(t->quiet[k]) == CS_NONE) && cs_ref_bit(k))
      refmask |= (uint8_t)(1u << k);
  }

  v = base;

  //B cores with no clean stage: again with their chip's reference bit (data-align mode)
  if(refmask)
  {
    uint16_t rbase = base;

    for(k=0;k<CS_CORES;k++)
      if(refmask & (1u << k))
        rbase = (uint16_t)(rbase | cs_ref_bit(k));

    t->ref = refmask;
    rc = cs_pass(io, t, rbase, refmask, t->sum_ref, t->q_ref, t->clean_ref, t->quiet_ref);

    if(rc != CS_OK)
      return rc;

    for(k=0;k<CS_CORES;k++)
      if((refmask & (1u << k)) && (cs_pick(t->quiet_ref[k]) != CS_NONE))
      {
        t->set[k] = t->quiet_ref[k];
        v = (uint16_t)(v | cs_ref_bit(k));
      }
  }

  for(k=0;k<CS_CORES;k++)
  {
    if(!(t->cores & (1u << k)))
      continue;

    if(cs_pick(t->set[k]) == CS_NONE)
      t->bad |= (uint8_t)(1u << k);
    else
    {
      v = cs_with(v, k, cs_pick(t->set[k]));
      t->tie[k] = cs_tie(t->set[k]);
      ties |= t->tie[k];
    }
  }

  if(t->bad)
    return CS_E_DIRTY;

  //fw-capmetric2: the noise bound (cap_stage.h "Noise bound") on the best outlier-clean stage of the timing in use; the
  //quiet rule is relative and passes data that are wrong alike at every stage
  for(k=0;k<CS_CORES;k++)
    if((t->cores & (1u << k)) &&
       (cs_q_best((v & cs_ref_bit(k)) ? t->sum_ref[k] : t->sum[k], (v & cs_ref_bit(k)) ? t->q_ref[k] : t->q[k]) >
        CS_RESID_MAX_Q16 * io->points * io->reps))
      t->bad |= (uint8_t)(1u << k);

  if(t->bad)
    return CS_E_RESID;

  //Two centres of an even run (or two equal reserves): the tie pass decides, then the pick again on what stayed clean
  if(ties && io->tie_reps)
  {
    rc = cs_tie_pass(io, t, v);

    if(rc != CS_OK)
      return rc;

    for(k=0;k<CS_CORES;k++)
    {
      if(!t->tie[k])
        continue;

      if(cs_pick(t->set[k]) == CS_NONE)
        t->bad |= (uint8_t)(1u << k);
      else
        v = cs_with(v, k, cs_pick(t->set[k]));
    }

    if(t->bad)
      return CS_E_DIRTY;
  }
  else
  {
    for(k=0;k<CS_CORES;k++)
      t->tie[k] = 0;
  }

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

void cs_watch_forget(cs_watch *w, uint8_t cores)
{
  uint32_t k, i;

  for(k=0;k<CS_CORES;k++)
  {
    if(!(cores & (1u << k)))
      continue;

    w->n[k] = 0;
    w->repeat &= (uint8_t)~(1u << k);

    for(i=0;i<CS_REPEAT_N - 1;i++)
      w->t[k][i] = 0;
  }
}

uint16_t cs_sets_pack(const uint8_t set[CS_CORES])
{
  uint16_t v = 0;
  uint32_t k;

  for(k=0;k<CS_CORES;k++)
    v = cs_sets_with(v, k, set[k]);

  return v;
}

uint8_t cs_sets_core(uint16_t sets, uint32_t core)
{
  return (uint8_t)(((sets >> (4 * core)) & 0x0F) << CS_STAGE_MIN);
}

uint16_t cs_sets_with(uint16_t sets, uint32_t core, uint8_t set)
{
  return (uint16_t)((sets & ~(0x0Fu << (4 * core))) | ((uint32_t)((set >> CS_STAGE_MIN) & 0x0F) << (4 * core)));
}

uint8_t cs_guard(uint16_t *value, uint16_t *sets, uint8_t cores)
{
  uint32_t k;
  uint8_t  moved = 0, cur, set, s;

  for(k=0;k<CS_CORES;k++)
  {
    if(!(cores & (1u << k)))
      continue;

    cur = cs_stage(*value, k);
    set = cs_sets_core(*sets, k);

    //A stage outside the stored set (or no set): not the trained value, nothing to move within
    if(!CLEAN(set, cur))
      continue;

    set = (uint8_t)(set & ~(1u << cur));
    s   = cs_pick(set);

    if(s == CS_NONE)
      continue;

    *value = cs_with(*value, k, s);
    *sets  = cs_sets_with(*sets, k, set);
    moved |= (uint8_t)(1u << k);
  }

  return moved;
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

//CRC-32 of the trailer's 12 bytes before its own CRC and the sets' 4 bytes (a CRC over the whole trailer would not bind:
//a CRC-32 over data followed by its own CRC is the same constant for every data)
static uint32_t cs_ext_crc(const uint8_t *sector)
{
  uint8_t  b[16];
  uint32_t i;

  for(i=0;i<12;i++)
    b[i] = sector[CS_TRAILER_OFFSET + i];

  for(i=0;i<4;i++)
    b[12 + i] = sector[CS_EXT_OFFSET + i];

  return acal_crc32(b, 16);
}

void cs_ext_pack(uint8_t *sector, uint16_t sets)
{
  uint8_t *p = sector + CS_EXT_OFFSET;
  uint32_t crc;

  p = cs_put16(p, sets);
  p = cs_put16(p, 0);
  crc = cs_ext_crc(sector);
  p = cs_put16(p, crc & 0xFFFF);
  cs_put16(p, crc >> 16);
}

int cs_ext_unpack(const uint8_t *sector, uint16_t *sets)
{
  const uint8_t *p = sector + CS_EXT_OFFSET;

  *sets = 0;

  if((cs_get16(p + 2) != 0) ||
     ((cs_get16(p + 4) | (cs_get16(p + 6) << 16)) != cs_ext_crc(sector)))
    return 0;

  *sets = (uint16_t)cs_get16(p);
  return 1;
}
