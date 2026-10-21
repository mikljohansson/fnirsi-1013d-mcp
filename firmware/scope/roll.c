//----------------------------------------------------------------------------------------------------------------------------------
// roll.c - the roll mode's trace from the logger stream (roll.h)
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>

#include "roll.h"

//Per roll time base (timeperdiv 0 = 50 s/div .. 8 = 100 ms/div): column period = time/div / 50 = W x 2 (D + 1) x 5 ns.
//D + 1 at most 1,000,000 (the 0x0D compare is 20 bits, PC_DIVIDER_MAX 1,048,575): 10 ms words, 200 S/s per channel, for
//2 s/div and slower; four words per column (8 samples per channel) from 1 s/div to 100 ms/div (4 kS/s per channel there)
static const uint32_t roll_div_tab[ROLL_TB_COUNT] = { 999999, 999999, 999999, 999999, 999999, 499999, 249999, 99999, 49999 };
static const uint16_t roll_wpc_tab[ROLL_TB_COUNT] = { 100,    40,     20,     10,     4,      4,      4,      4,     4 };

int roll_plan(uint8_t timeperdiv, uint32_t *divider, uint32_t *wpc)
{
  if(timeperdiv >= ROLL_TB_COUNT)
    return -1;

  *divider = roll_div_tab[timeperdiv];
  *wpc     = roll_wpc_tab[timeperdiv];
  return 0;
}

int32_t roll_code(uint32_t sum_a, uint32_t sum_b, uint32_t n, const roll_corr *c)
{
  int64_t  num = (int64_t)sum_a * c->gain_a + (int64_t)n * c->off_a + (int64_t)sum_b * c->gain_b + (int64_t)n * c->off_b;
  uint64_t den = (uint64_t)n << 17;           //2 n samples, Q16
  int32_t  code;

  if(num <= 0)
    code = 0;
  else
  {
    uint64_t q = ((uint64_t)num + (den >> 1)) / den;

    code = (q > 255) ? 255 : (int32_t)q;
  }

  return c->invert ? 255 - code : code;
}

void roll_reset(roll_model *m, uint8_t *buf1, uint8_t *buf2, uint32_t wpc, uint8_t mode, uint8_t trig_ch, uint8_t edge)
{
  int32_t level = m->level;

  memset(m, 0, sizeof(*m));
  m->buf[0]  = buf1;
  m->buf[1]  = buf2;
  m->wpc     = wpc ? wpc : 1;
  m->trig_ch = (trig_ch > 1) ? ROLL_NO_TRIGGER : trig_ch;
  m->mode    = (m->trig_ch == ROLL_NO_TRIGGER) ? ROLL_AUTO : mode;
  m->edge    = edge ? 1 : 0;
  m->level   = level;
  m->prev    = -1;
  m->state   = (m->mode == ROLL_AUTO) ? ROLL_RUN : ROLL_WAIT;
}

static int roll_crossed(const roll_model *m, int32_t c)
{
  if(m->prev < 0)
    return 0;

  if(m->edge)
    return (m->prev >= m->level) && (c < m->level);

  return (m->prev < m->level) && (c >= m->level);
}

uint32_t roll_feed(roll_model *m, const uint8_t *words, uint32_t n, int gap, const roll_corr corr[2], uint32_t *added)
{
  uint32_t ev = 0, i, ch;
  int32_t  code[2];

  *added = 0;

  if(gap)
  {
    m->nw = 0;
    memset(m->sum, 0, sizeof(m->sum));
    m->prev = -1;
    m->gaps++;
  }

  if(m->state == ROLL_DONE)
    return 0;

  for(i=0;i<n;i++, words += ROLL_WORD_BYTES)
  {
    m->sum[0][1] += words[0];
    m->sum[0][0] += words[1];
    m->sum[1][1] += words[2];
    m->sum[1][0] += words[3];

    if(++m->nw < m->wpc)
      continue;

    for(ch=0;ch<2;ch++)
      code[ch] = roll_code(m->sum[ch][0], m->sum[ch][1], m->wpc, &corr[ch]);

    m->nw = 0;
    memset(m->sum, 0, sizeof(m->sum));
    m->columns++;

    if(m->state == ROLL_WAIT)
    {
      int crossed = roll_crossed(m, code[m->trig_ch]);

      m->prev = code[m->trig_ch];

      if(!crossed)
        continue;

      m->state = ROLL_RUN;
      ev |= ROLL_EV_TRIG;
    }
    else if(m->trig_ch != ROLL_NO_TRIGGER)
      m->prev = code[m->trig_ch];

    //Auto: the oldest column makes room
    if(m->count == ROLL_LEN)
    {
      memmove(m->buf[0], m->buf[0] + 1, ROLL_LEN - 1);
      memmove(m->buf[1], m->buf[1] + 1, ROLL_LEN - 1);
      m->count--;
    }

    m->buf[0][m->count] = (uint8_t)code[0];
    m->buf[1][m->count] = (uint8_t)code[1];
    m->count++;
    (*added)++;

    if((m->mode == ROLL_AUTO) || (m->count < ROLL_LEN))
      continue;

    if(m->mode == ROLL_SINGLE)
    {
      m->state = ROLL_DONE;
      return ev | ROLL_EV_DONE;
    }

    //Normal: the record starts over and waits for the next crossing (from this column on)
    m->count = 0;
    m->state = ROLL_WAIT;
    ev |= ROLL_EV_RESTART;
  }

  return ev;
}
