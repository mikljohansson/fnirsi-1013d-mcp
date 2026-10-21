//----------------------------------------------------------------------------------------------------------------------------------
// pc_seq.c - sequencer maths for bitstream 0x1536 (pc_seq.h). Integer only, no per-sample divisions.
//----------------------------------------------------------------------------------------------------------------------------------

#include "pc_seq.h"

uint32_t pc_seq_max_count(const pc_seq_cfg *c, uint32_t total_words)
{
  if(c->mode == PC_SEQ_SEGMENTS)
    return ((c->m >= PC_SEQ_M_MIN) && (c->m <= PC_SEQ_M_MAX)) ? (PC_SEQ_WORDS >> c->m) : 0;

  if(c->mode == PC_SEQ_AVERAGE)
    return ((total_words == 0) || (total_words > PC_SEQ_AVG_MAX_TOTAL)) ? 0 : PC_SEQ_ACC_WORDS / total_words;

  return 0;
}

uint8_t pc_seq_fit(const pc_seq_cfg *c, uint32_t pre_words, uint32_t total_words, int wide)
{
  if(c->mode > PC_SEQ_AVERAGE)
    return PC_SEQ_FIT_MODE;

  if((c->flags & ~PC_SEQ_F_VALID) || ((c->flags & PC_SEQ_F_CIRCULAR) && (c->mode != PC_SEQ_SEGMENTS)))
    return PC_SEQ_FIT_FLAGS;

  if(c->mode == PC_SEQ_OFF)
    return PC_SEQ_FIT_OK;

  if(c->mode == PC_SEQ_SEGMENTS)
  {
    if((c->m < PC_SEQ_M_MIN) || (c->m > PC_SEQ_M_MAX))
      return PC_SEQ_FIT_M;

    if((c->count == 0) || (c->count > (PC_SEQ_WORDS >> c->m)))
      return PC_SEQ_FIT_COUNT;

    if(total_words + PC_SEQ_TAIL > (1u << c->m))
      return PC_SEQ_FIT_TOTAL;
  }
  else
  {
    if((c->n == 0) || (c->n > PC_SEQ_MAX_N) || (!wide && (c->n > PC_SEQ_LEGACY_MAX_N)))
      return PC_SEQ_FIT_N;

    if(total_words > PC_SEQ_AVG_MAX_TOTAL)
      return PC_SEQ_FIT_TOTAL;

    if((c->count == 0) || ((uint32_t)c->count * total_words > PC_SEQ_ACC_WORDS))
      return PC_SEQ_FIT_COUNT;
  }

  if((total_words == 0) || (pre_words >= total_words))
    return PC_SEQ_FIT_PRE;

  return PC_SEQ_FIT_OK;
}

uint32_t pc_seq_holdoff_units(uint32_t ns)
{
  return (uint32_t)(((uint64_t)ns + 4) / 5);
}

uint8_t pc_seq_cmd57(const pc_seq_cfg *c, int raw)
{
  uint8_t v = c->mode & 3;

  if((c->mode == PC_SEQ_SEGMENTS) && (c->flags & PC_SEQ_F_CIRCULAR))
    v |= PC_SEQ_57_CIRCULAR;

  if(raw)
    v |= PC_SEQ_57_RAW;

  return v;
}

uint16_t pc_seq_cmd58(const pc_seq_cfg *c)
{
  return (uint16_t)(c->count & 0x0FFF);
}

uint16_t pc_seq_cmd5a(const pc_seq_cfg *c)
{
  return (uint16_t)(c->n & 0xFFFF);                    //65536 -> 0
}

uint32_t pc_seq_slots(const pc_seq_cfg *c)
{
  if(c->mode != PC_SEQ_SEGMENTS)
    return 0;

  return (c->flags & PC_SEQ_F_CIRCULAR) ? (PC_SEQ_WORDS >> c->m) : c->count;
}

void pc_seq_status_decode(const uint8_t raw[8], pc_seq_status *st)
{
  st->flags       = raw[0];
  st->count       = ((uint32_t)raw[1] << 16) | ((uint32_t)raw[2] << 8) | raw[3];
  st->untriggered = (uint16_t)((raw[4] << 8) | raw[5]);
  st->state       = (uint8_t)(raw[6] >> 5);
  st->index       = (uint16_t)(((raw[6] & 0x0F) << 8) | raw[7]);
}

void pc_seq_entry_decode(uint64_t e, pc_seq_entry *d)
{
  d->timestamp = e & 0xFFFFFFFFFFull;
  d->offset    = (uint16_t)((e >> 40) & 0x1FFF);
  d->triggered = (uint8_t)((e >> 53) & 1);
  d->valid     = (uint8_t)(e >> 63);
}

uint64_t pc_seq_entry_encode(const pc_seq_entry *d)
{
  return (d->timestamp & 0xFFFFFFFFFFull) | ((uint64_t)(d->offset & 0x1FFF) << 40) | ((uint64_t)(d->triggered & 1) << 53) |
         ((uint64_t)(d->valid & 1) << 63);
}

uint32_t pc_seq_seg_word(uint8_t m, uint32_t s, uint16_t offset, uint32_t pre_words, uint32_t j)
{
  uint32_t mask = (1u << m) - 1;

  return (s << m) + (((uint32_t)offset - pre_words + j) & mask);
}

//----------------------------------------------------------------------------------------------------------------------------------

uint32_t pc_seq_dead_ns(uint32_t total_words, uint32_t pre_words, uint32_t word_ns)
{
  return total_words * 5 + 40 + (2 + pre_words) * word_ns + 100;
}

uint32_t pc_seq_ui_plan_n(uint64_t per_record_ns, uint32_t *run_us)
{
  uint32_t n = PC_SEQ_UI_MAX_N;
  uint64_t run;

  *run_us = 0;

  if(per_record_ns == 0)
    return 0;

  while((n >= PC_SEQ_UI_MIN_N) && ((uint64_t)n * per_record_ns > (uint64_t)PC_SEQ_UI_BUDGET_US * 1000))
    n >>= 1;

  if(n < PC_SEQ_UI_MIN_N)
    return 0;

  run     = (uint64_t)n * per_record_ns;
  *run_us = (uint32_t)((run + 999) / 1000);
  return n;
}

uint32_t pc_seq_ui_plan(uint32_t total_words, uint32_t pre_words, uint32_t divider, uint32_t *run_us)
{
  uint64_t word_ns = 10ull * ((uint64_t)divider + 1);

  *run_us = 0;

  if((total_words == 0) || (total_words > PC_SEQ_AVG_MAX_TOTAL) || (pre_words >= total_words) || (word_ns > 1000000))
    return 0;

  return pc_seq_ui_plan_n((uint64_t)total_words * word_ns + pc_seq_dead_ns(total_words, pre_words, (uint32_t)word_ns), run_us);
}

uint32_t pc_seq_ui_plan_mcu(uint32_t total_words, uint32_t pre_words, uint32_t divider, int wide, uint32_t *run_us)
{
  uint64_t word_ns = (wide ? 5ull : 10ull) * ((uint64_t)divider + 1);

  *run_us = 0;

  if((total_words == 0) || (pre_words >= total_words) || (word_ns > 1000000))
    return 0;

  return pc_seq_ui_plan_n((uint64_t)total_words * word_ns + pc_seq_dead_ns(total_words, pre_words, (uint32_t)word_ns) +
                          (uint64_t)total_words * PC_SEQ_UI_READ_BYTES * PC_SEQ_UI_READ_NS, run_us);
}

uint32_t pc_seq_recip(uint32_t c)
{
  if(c <= 1)
    return 0;

  return (uint32_t)(((1ull << 32) + c - 1) / c);
}

int32_t pc_seq_avg_code(uint32_t sum, uint32_t c, uint32_t r, int32_t gain, int32_t off)
{
  uint32_t m8 = pc_seq_udiv(sum * 256u + c / 2, c, r);
  int64_t  v  = ((int64_t)m8 * gain + ((int64_t)off << 8) + (1 << 23)) >> 24;

  return (v < 0) ? 0 : ((v > 255) ? 255 : (int32_t)v);
}
