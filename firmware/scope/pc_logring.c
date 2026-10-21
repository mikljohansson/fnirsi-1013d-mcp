//----------------------------------------------------------------------------------------------------------------------------------
// pc_logring.c - the streaming logger's DDR ring (pc_logring.h)
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>

#include "pc_logring.h"

void pc_logring_reset(pc_logring *r, uint8_t *buf, uint32_t size, uint8_t bpw)
{
  memset(r, 0, sizeof(*r));
  r->buf  = buf;
  r->size = size;
  r->bpw  = bpw;
}

//An append opens a segment when it is the first, does not continue the counter, or carries gap flags
static int pc_logring_opens(const pc_logring *r, uint64_t first_word, uint8_t flags)
{
  return (r->seg_n == 0) || (first_word != r->next_word) || flags;
}

uint8_t *pc_logring_wptr(const pc_logring *r, uint64_t first_word, uint8_t flags, uint32_t *words)
{
  uint32_t at   = (uint32_t)(r->wr & (r->size - 1));
  uint64_t free = r->size - (r->wr - r->rd);
  uint64_t n    = r->size - at;

  if(free < n)
    n = free;

  //A new segment needs a free slot: the reader's segment and every later one are in use
  if(pc_logring_opens(r, first_word, flags) && ((r->seg_n - r->seg_r) >= PC_LOGRING_SEGS))
    n = 0;

  *words = (uint32_t)(n / r->bpw);
  return r->buf + at;
}

void pc_logring_commit(pc_logring *r, uint32_t words, uint64_t first_word, uint8_t flags)
{
  if(words == 0)
    return;

  if(pc_logring_opens(r, first_word, flags))
  {
    pc_logseg *s = &r->seg[r->seg_n & (PC_LOGRING_SEGS - 1)];

    s->pos   = r->wr;
    s->word  = first_word;
    //A counter jump is a gap whatever the caller says (an append after a dropped chunk)
    s->flags = (uint8_t)(flags | (((r->seg_n != 0) && (first_word != r->next_word)) ? PC_LOG_CF_GAP : 0));

    if(r->seg_n == 0)
      r->rd_first = 1;

    r->seg_n++;
  }

  r->wr       += (uint64_t)words * r->bpw;
  r->next_word = first_word + words;
}

uint32_t pc_logring_read(pc_logring *r, uint8_t *dst, uint32_t max_bytes, uint64_t *word, uint8_t *flags)
{
  const pc_logseg *s;
  uint64_t end, n;
  uint32_t at, k;

  *word  = r->next_word;
  *flags = 0;

  if(r->seg_n == 0)
    return 0;

  //Past the end of the reader's segment: the next one starts there
  for(;;)
  {
    s   = &r->seg[r->seg_r & (PC_LOGRING_SEGS - 1)];
    end = ((r->seg_r + 1) < r->seg_n) ? r->seg[(r->seg_r + 1) & (PC_LOGRING_SEGS - 1)].pos : r->wr;

    if((r->rd < end) || ((r->seg_r + 1) >= r->seg_n))
      break;

    r->seg_r++;
    r->rd_first = 1;
  }

  *word = s->word + (r->rd - s->pos) / r->bpw;

  n = end - r->rd;

  if(n > max_bytes)
    n = max_bytes;

  n -= n % r->bpw;

  if(n == 0)
    return 0;

  if(r->rd_first)
    *flags = s->flags;

  r->rd_first = 0;

  //Two pieces at most: up to the buffer end, then from its start
  at = (uint32_t)(r->rd & (r->size - 1));
  k  = r->size - at;

  if(k > n)
    k = (uint32_t)n;

  memcpy(dst, r->buf + at, k);

  if(n > k)
    memcpy(dst + k, r->buf, (uint32_t)n - k);

  r->rd += n;
  return (uint32_t)n;
}
