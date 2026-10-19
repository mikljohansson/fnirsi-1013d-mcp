//----------------------------------------------------------------------------------------------------------------------------------
// seq_check: the 0x1536 sequencer maths of pc_seq.c for check_vectors.py (docs/protocol-vectors.json "seq").
//
// stdin, one case per line, stdout one line per case:
//   fit <mode> <flags> <m> <count> <n> <pre> <total> <wide>  ->  <fit> <max_count> <slots> <cmd57> <cmd58> <cmd5a>
//   hold <ns>                                                ->  <units>
//   entry <u64 hex>                                          ->  <timestamp> <offset> <triggered> <valid>
//   segw <m> <s> <offset> <pre> <j>                          ->  <word>
//   plan <total> <pre> <divider>                             ->  <n> <run_us>
//   avg <sum> <c> <gain> <off>                               ->  <code>
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "pc_seq.h"

int main(void)
{
  char op[16];

  while(scanf("%15s", op) == 1)
  {
    if(!strcmp(op, "fit"))
    {
      unsigned mode, flags, m, count, n, pre, total, wide;
      pc_seq_cfg c;

      if(scanf("%u %u %u %u %u %u %u %u", &mode, &flags, &m, &count, &n, &pre, &total, &wide) != 8)
        return 1;

      c.mode = (uint8_t)mode; c.flags = (uint8_t)flags; c.m = (uint8_t)m; c.count = (uint16_t)count; c.n = n;
      c.holdoff_ns = 0;
      printf("%u %u %u %u %u %u\n", pc_seq_fit(&c, pre, total, (int)wide), (unsigned)pc_seq_max_count(&c, total),
             (unsigned)pc_seq_slots(&c), pc_seq_cmd57(&c, 0), pc_seq_cmd58(&c), pc_seq_cmd5a(&c));
    }
    else if(!strcmp(op, "hold"))
    {
      unsigned ns;

      if(scanf("%u", &ns) != 1)
        return 1;

      printf("%u\n", (unsigned)pc_seq_holdoff_units(ns));
    }
    else if(!strcmp(op, "entry"))
    {
      unsigned long long w;
      pc_seq_entry e;

      if(scanf("%llx", &w) != 1)
        return 1;

      pc_seq_entry_decode(w, &e);
      printf("%llu %u %u %u\n", (unsigned long long)e.timestamp, e.offset, e.triggered, e.valid);
    }
    else if(!strcmp(op, "segw"))
    {
      unsigned m, s, off, pre, j;

      if(scanf("%u %u %u %u %u", &m, &s, &off, &pre, &j) != 5)
        return 1;

      printf("%u\n", (unsigned)pc_seq_seg_word((uint8_t)m, s, (uint16_t)off, pre, j));
    }
    else if(!strcmp(op, "plan"))
    {
      unsigned total, pre, d;
      uint32_t us;
      uint32_t n;

      if(scanf("%u %u %u", &total, &pre, &d) != 3)
        return 1;

      n = pc_seq_ui_plan(total, pre, d, &us);
      printf("%u %u\n", (unsigned)n, (unsigned)us);
    }
    else if(!strcmp(op, "avg"))
    {
      unsigned sum, c;
      int gain, off;

      if(scanf("%u %u %d %d", &sum, &c, &gain, &off) != 4)
        return 1;

      printf("%d\n", (int)pc_seq_avg_code(sum, c, pc_seq_recip(c), gain, off));
    }
    else
      return 1;
  }

  return 0;
}
