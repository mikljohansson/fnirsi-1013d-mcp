//----------------------------------------------------------------------------------------------------------------------------------
// pc_ident.c - firmware image identity on the card (pc_ident.h, protocol row 55)
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>
#include "pc_ident.h"
#include "pc_hal.h"

static uint32_t rd32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int pc_ident_find_record(const uint8_t *tail, uint32_t n, pc_image_id *out)
{
  int32_t i;

  //The record ends where the image ends (before the zero padding); the last match wins
  for(i=(int32_t)(n & ~3u) - PC_ID_BYTES;i>=0;i-=4)
  {
    const uint8_t *p = tail + i;

    if((rd32(p) == PC_ID_MAGIC0) && (rd32(p + 4) == PC_ID_MAGIC1))
    {
      memset(out, 0, sizeof(*out));
      out->magic0 = PC_ID_MAGIC0;
      out->magic1 = PC_ID_MAGIC1;
      memcpy(out->version, p + 8, 15);
      out->build = rd32(p + 24);
      memcpy(out->time, p + 28, 6);
      out->proto_major = p[34];
      out->proto_minor = p[35];
      return 1;
    }
  }

  return 0;
}

static int valid_time(const uint8_t *t)
{
  return (t[0] >= 20) && (t[0] <= 99) && (t[1] >= 1) && (t[1] <= 12) && (t[2] >= 1) && (t[2] <= 31) &&
         (t[3] < 24) && (t[4] < 60) && (t[5] < 60);
}

int pc_ident_legacy_tail(const uint8_t *tail, uint32_t n, uint32_t *build, uint8_t *time)
{
  int32_t p = (int32_t)n - 1, end;
  const uint8_t *b;

  while((p >= 0) && (tail[p] == 0))
    p--;

  //GCC 10 emits build_id.c's two objects time first: u8 time[6], u8 pad[2], u32 build (the image ends with build)
  end = (p & ~3) + 4;

  if((p < 0) || (end < 12) || (end > (int32_t)n))
    return 0;

  b = tail + end - 12;

  if(!valid_time(b) || b[6] || b[7] || !rd32(b + 8))
    return 0;

  memcpy(time, b, 6);
  *build = rd32(b + 8);
  return 1;
}

static uint32_t parse_num(const char **s)
{
  uint32_t v = 0;

  while((**s >= '0') && (**s <= '9'))
    v = v * 10 + (uint32_t)(*(*s)++ - '0');

  return v;
}

int pc_ident_version_at_least(const char *v, uint32_t major, uint32_t minor, uint32_t patch)
{
  uint32_t a, b, c;

  if((*v < '0') || (*v > '9'))
    return 0;

  a = parse_num(&v);
  b = (*v == '.') ? (v++, parse_num(&v)) : 0;
  c = (*v == '.') ? (v++, parse_num(&v)) : 0;

  if(a != major)
    return a > major;
  if(b != minor)
    return b > minor;
  return c >= patch;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Version-text scan. The needle is built at run time so this file's own constants never match it in an image.

static const char scan_tail[] = "1013D-AI,fw=";   //preceded by "FNIRSI-"

static char needle_at(uint32_t i)
{
  static const char head[] = { 'F', 'N', 'I', 'R', 'S', 'I', '-' };

  return (i < sizeof(head)) ? head[i] : scan_tail[i - sizeof(head)];
}

#define NEEDLE_LEN  (7 + sizeof(scan_tail) - 1)

void pc_ident_scan_init(pc_ident_scan *s)
{
  memset(s, 0, sizeof(*s));
}

static void scan_restart(pc_ident_scan *s)
{
  s->phase = 0;
  s->state = 0;
  s->n     = 0;
  memset(s->version, 0, sizeof(s->version));
  s->major = s->minor = 0;
}

void pc_ident_scan_feed(pc_ident_scan *s, const uint8_t *buf, uint32_t n)
{
  static const char proto[] = "proto=";
  uint32_t i;

  for(i=0;(i<n) && !s->done;i++)
  {
    char c = (char)buf[i];

    switch(s->phase)
    {
      case 0:
        //'F' occurs only at the needle's start, so a mismatch restarts at 0 (or 1 on an 'F')
        if(c == needle_at(s->state))
        {
          if(++s->state == NEEDLE_LEN)
          {
            s->phase = 1;
            s->n     = 0;
          }
        }
        else
          s->state = (c == 'F') ? 1 : 0;
        break;

      case 1:
        if(c == ',')
        {
          if(s->n == 0)
            scan_restart(s);
          else
          {
            s->phase = 2;
            s->state = 0;
          }
        }
        else if((c >= 0x21) && (c <= 0x7E) && (s->n < 15))
          s->version[s->n++] = c;
        else
          scan_restart(s);
        break;

      case 2:
        if(c == proto[s->state])
        {
          if(++s->state == sizeof(proto) - 1)
            s->phase = 3;
        }
        else
          s->done = 1;           //version without a protocol field: keep the version
        break;

      case 3:
        if((c >= '0') && (c <= '9'))
          s->major = (uint8_t)(s->major * 10 + (c - '0'));
        else if(c == '.')
          s->phase = 4;
        else
          s->done = 1;
        break;

      default:
        if((c >= '0') && (c <= '9'))
          s->minor = (uint8_t)(s->minor * 10 + (c - '0'));
        else
          s->done = 1;
        break;
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Card

static pc_card_id pc_id_cache;

void pc_ident_invalidate(void)
{
  memset(&pc_id_cache, 0, sizeof(pc_id_cache));
}

static int pc_id_same(const pc_card_id *c)
{
  const pc_image_id *r = &pc_image_ident;

  return ((c->state == PC_ID_RECORD) || (c->state == PC_ID_TAIL)) && (c->build == r->build) &&
         !memcmp(c->time, r->time, 6) && !memcmp(c->version, r->version, 16);
}

const pc_card_id *pc_ident_card(int read, uint8_t *scratch, uint32_t scratch_bytes)
{
  pc_card_id *c = &pc_id_cache;
  uint32_t t0, sectors, tail_sec, chunk = scratch_bytes / 512, done, k;
  pc_image_id rec;
  pc_ident_scan scan;

  if(!read || (c->state != PC_ID_NOT_READ))
  {
    c->read_ms = 0;
    return c;
  }

  t0 = pc_hal_ms();
  memset(c, 0, sizeof(*c));
  c->state = PC_ID_NO_IMAGE;

  if((pc_hal_sd_sectors() == 0) || (pc_hal_sd_read(PC_ID_SECTOR, 1, scratch) != 0) ||
     memcmp(scratch + 4, "eGON.EXE", 8))
    goto out;

  c->length = rd32(scratch + 16);
  sectors   = c->length / 512;

  if((c->length < 1024) || (c->length % 512) || (sectors > PC_ID_MAX_SECTORS) ||
     (PC_ID_SECTOR + sectors > pc_hal_sd_sectors()))
    goto out;

  //The last two sectors: the identity record, or the 0.2.47-0.2.51 build tail
  tail_sec = PC_ID_SECTOR + sectors - PC_ID_TAIL_SEARCH / 512;

  if(pc_hal_sd_read(tail_sec, PC_ID_TAIL_SEARCH / 512, scratch) != 0)
    goto out;

  if(pc_ident_find_record(scratch, PC_ID_TAIL_SEARCH, &rec))
  {
    c->state = PC_ID_RECORD;
    memcpy(c->version, rec.version, 16);
    c->build       = rec.build;
    memcpy(c->time, rec.time, 6);
    c->proto_major = rec.proto_major;
    c->proto_minor = rec.proto_minor;
    goto out;
  }

  c->state = PC_ID_UNKNOWN;

  if(!pc_ident_legacy_tail(scratch, PC_ID_TAIL_SEARCH, &c->build, c->time))
  {
    c->build = 0;
    memset(c->time, 0, 6);
  }

  //Older images: the #VER text, anywhere in the image
  pc_ident_scan_init(&scan);

  for(done=0;(done<sectors) && !scan.done;done+=k)
  {
    k = sectors - done;

    if(k > chunk)
      k = chunk;

    if(pc_hal_sd_read(PC_ID_SECTOR + done, k, scratch) != 0)
    {
      c->state = PC_ID_NO_IMAGE;
      c->build = 0;
      memset(c->time, 0, 6);
      goto out;
    }

    pc_ident_scan_feed(&scan, scratch, k * 512);
  }

  if(scan.version[0])
  {
    memcpy(c->version, scan.version, 16);
    c->proto_major = scan.major;
    c->proto_minor = scan.minor;

    //The build tail counts only on an image new enough to have one
    if(c->build && pc_ident_version_at_least(c->version, 0, 2, 47))
      c->state = PC_ID_TAIL;
    else
    {
      c->state = PC_ID_VERSION_ONLY;
      c->build = 0;
      memset(c->time, 0, 6);
    }
  }
  else
  {
    c->build = 0;
    memset(c->time, 0, 6);
  }

out:
  c->same    = (uint8_t)pc_id_same(c);
  c->read_ms = pc_hal_ms() - t0;

  if(c->read_ms == 0)
    c->read_ms = 1;

  return c;
}
