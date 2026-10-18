//----------------------------------------------------------------------------------------------------------------------------------
// vector_check: feed byte streams through the firmware parser (pc_protocol.c) and print the events.
//
// stdin, one stream per line:   <name> <hex bytes or ->
// stdout per event:             EV <name> frame <cmd> <seq> <status> <flags> <len> <payload hex or ->
//                               EV <name> bad_crc|bad_len|bad_version <cmd> <seq>
//                               EV <name> ascii <line as hex or ->
//                               EV <name> ascii_overflow
//                               END <name> <hdr_crc_errors> <timeouts>
// Each stream starts with a freshly initialised parser and ends with 150 ms of silence.
// Used by check_vectors.py.
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc_protocol.h"

static void print_event(const char *name, const pc_parser *p, int ev)
{
  uint32_t i;

  switch(ev)
  {
    case PC_EV_FRAME:
      printf("EV %s frame %u %u %u %u %u ", name, p->hdr.cmd, p->hdr.seq, p->hdr.status, p->hdr.flags, (unsigned)p->hdr.len);
      if(p->hdr.len == 0)
        printf("-");
      for(i=0;i<p->hdr.len;i++)
        printf("%02x", p->payload[i]);
      printf("\n");
      break;
    case PC_EV_BAD_CRC:     printf("EV %s bad_crc %u %u\n", name, p->hdr.cmd, p->hdr.seq); break;
    case PC_EV_BAD_LEN:     printf("EV %s bad_len %u %u\n", name, p->hdr.cmd, p->hdr.seq); break;
    case PC_EV_BAD_VERSION: printf("EV %s bad_version %u %u\n", name, p->hdr.cmd, p->hdr.seq); break;
    case PC_EV_ASCII:
      printf("EV %s ascii ", name);
      if(p->line[0] == 0)
        printf("-");
      for(i=0;p->line[i];i++)
        printf("%02x", (uint8_t)p->line[i]);
      printf("\n");
      break;
    case PC_EV_ASCII_OVERFLOW: printf("EV %s ascii_overflow\n", name); break;
    default: break;
  }
}

static int hexval(int c)
{
  if(c >= '0' && c <= '9') return c - '0';
  if(c >= 'a' && c <= 'f') return c - 'a' + 10;
  if(c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

int main(void)
{
  static char line[1 << 20];
  static pc_parser p;
  char name[256];

  while(fgets(line, sizeof(line), stdin))
  {
    char *hex;
    uint32_t now = 1000;
    size_t n;
    int ev;

    if(sscanf(line, "%255s", name) != 1)
      continue;

    hex = strchr(line, ' ');
    hex = hex ? hex + 1 : line + strlen(line);

    pc_parser_init(&p);

    for(n=0; hexval(hex[n]) >= 0 && hexval(hex[n + 1]) >= 0; n += 2)
    {
      uint8_t b = (uint8_t)((hexval(hex[n]) << 4) | hexval(hex[n + 1]));

      ev = pc_parser_feed(&p, b, now);
      if(ev != PC_EV_NONE)
        print_event(name, &p, ev);

      //Drain bytes queued by a resync before the next input byte (as the firmware poll loop does)
      while((ev = pc_parser_service(&p, now)) != PC_EV_NONE)
        print_event(name, &p, ev);
    }

    now += 150;
    pc_parser_tick(&p, now);

    printf("END %s %u %u\n", name, (unsigned)p.stats.hdr_crc_errors, (unsigned)p.stats.timeouts);
  }

  return 0;
}
