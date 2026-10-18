//----------------------------------------------------------------------------------------------------------------------------------
// pc_protocol.c - frame parser / encoder for the FNIRSI 1013D PC control protocol v1.0 (docs/protocol.md).
//
// Pure C: no hardware access, no globals. All state is in the caller's pc_parser.
//----------------------------------------------------------------------------------------------------------------------------------

#include "pc_protocol.h"

//----------------------------------------------------------------------------------------------------------------------------------
// CRC-32/ISO-HDLC table (poly 0x04C11DB7 reflected = 0xEDB88320), generated with Python, same as zlib.crc32

static const uint32_t pc_crc32_table[256] =
{
  0x00000000U, 0x77073096U, 0xEE0E612CU, 0x990951BAU, 0x076DC419U, 0x706AF48FU,
  0xE963A535U, 0x9E6495A3U, 0x0EDB8832U, 0x79DCB8A4U, 0xE0D5E91EU, 0x97D2D988U,
  0x09B64C2BU, 0x7EB17CBDU, 0xE7B82D07U, 0x90BF1D91U, 0x1DB71064U, 0x6AB020F2U,
  0xF3B97148U, 0x84BE41DEU, 0x1ADAD47DU, 0x6DDDE4EBU, 0xF4D4B551U, 0x83D385C7U,
  0x136C9856U, 0x646BA8C0U, 0xFD62F97AU, 0x8A65C9ECU, 0x14015C4FU, 0x63066CD9U,
  0xFA0F3D63U, 0x8D080DF5U, 0x3B6E20C8U, 0x4C69105EU, 0xD56041E4U, 0xA2677172U,
  0x3C03E4D1U, 0x4B04D447U, 0xD20D85FDU, 0xA50AB56BU, 0x35B5A8FAU, 0x42B2986CU,
  0xDBBBC9D6U, 0xACBCF940U, 0x32D86CE3U, 0x45DF5C75U, 0xDCD60DCFU, 0xABD13D59U,
  0x26D930ACU, 0x51DE003AU, 0xC8D75180U, 0xBFD06116U, 0x21B4F4B5U, 0x56B3C423U,
  0xCFBA9599U, 0xB8BDA50FU, 0x2802B89EU, 0x5F058808U, 0xC60CD9B2U, 0xB10BE924U,
  0x2F6F7C87U, 0x58684C11U, 0xC1611DABU, 0xB6662D3DU, 0x76DC4190U, 0x01DB7106U,
  0x98D220BCU, 0xEFD5102AU, 0x71B18589U, 0x06B6B51FU, 0x9FBFE4A5U, 0xE8B8D433U,
  0x7807C9A2U, 0x0F00F934U, 0x9609A88EU, 0xE10E9818U, 0x7F6A0DBBU, 0x086D3D2DU,
  0x91646C97U, 0xE6635C01U, 0x6B6B51F4U, 0x1C6C6162U, 0x856530D8U, 0xF262004EU,
  0x6C0695EDU, 0x1B01A57BU, 0x8208F4C1U, 0xF50FC457U, 0x65B0D9C6U, 0x12B7E950U,
  0x8BBEB8EAU, 0xFCB9887CU, 0x62DD1DDFU, 0x15DA2D49U, 0x8CD37CF3U, 0xFBD44C65U,
  0x4DB26158U, 0x3AB551CEU, 0xA3BC0074U, 0xD4BB30E2U, 0x4ADFA541U, 0x3DD895D7U,
  0xA4D1C46DU, 0xD3D6F4FBU, 0x4369E96AU, 0x346ED9FCU, 0xAD678846U, 0xDA60B8D0U,
  0x44042D73U, 0x33031DE5U, 0xAA0A4C5FU, 0xDD0D7CC9U, 0x5005713CU, 0x270241AAU,
  0xBE0B1010U, 0xC90C2086U, 0x5768B525U, 0x206F85B3U, 0xB966D409U, 0xCE61E49FU,
  0x5EDEF90EU, 0x29D9C998U, 0xB0D09822U, 0xC7D7A8B4U, 0x59B33D17U, 0x2EB40D81U,
  0xB7BD5C3BU, 0xC0BA6CADU, 0xEDB88320U, 0x9ABFB3B6U, 0x03B6E20CU, 0x74B1D29AU,
  0xEAD54739U, 0x9DD277AFU, 0x04DB2615U, 0x73DC1683U, 0xE3630B12U, 0x94643B84U,
  0x0D6D6A3EU, 0x7A6A5AA8U, 0xE40ECF0BU, 0x9309FF9DU, 0x0A00AE27U, 0x7D079EB1U,
  0xF00F9344U, 0x8708A3D2U, 0x1E01F268U, 0x6906C2FEU, 0xF762575DU, 0x806567CBU,
  0x196C3671U, 0x6E6B06E7U, 0xFED41B76U, 0x89D32BE0U, 0x10DA7A5AU, 0x67DD4ACCU,
  0xF9B9DF6FU, 0x8EBEEFF9U, 0x17B7BE43U, 0x60B08ED5U, 0xD6D6A3E8U, 0xA1D1937EU,
  0x38D8C2C4U, 0x4FDFF252U, 0xD1BB67F1U, 0xA6BC5767U, 0x3FB506DDU, 0x48B2364BU,
  0xD80D2BDAU, 0xAF0A1B4CU, 0x36034AF6U, 0x41047A60U, 0xDF60EFC3U, 0xA867DF55U,
  0x316E8EEFU, 0x4669BE79U, 0xCB61B38CU, 0xBC66831AU, 0x256FD2A0U, 0x5268E236U,
  0xCC0C7795U, 0xBB0B4703U, 0x220216B9U, 0x5505262FU, 0xC5BA3BBEU, 0xB2BD0B28U,
  0x2BB45A92U, 0x5CB36A04U, 0xC2D7FFA7U, 0xB5D0CF31U, 0x2CD99E8BU, 0x5BDEAE1DU,
  0x9B64C2B0U, 0xEC63F226U, 0x756AA39CU, 0x026D930AU, 0x9C0906A9U, 0xEB0E363FU,
  0x72076785U, 0x05005713U, 0x95BF4A82U, 0xE2B87A14U, 0x7BB12BAEU, 0x0CB61B38U,
  0x92D28E9BU, 0xE5D5BE0DU, 0x7CDCEFB7U, 0x0BDBDF21U, 0x86D3D2D4U, 0xF1D4E242U,
  0x68DDB3F8U, 0x1FDA836EU, 0x81BE16CDU, 0xF6B9265BU, 0x6FB077E1U, 0x18B74777U,
  0x88085AE6U, 0xFF0F6A70U, 0x66063BCAU, 0x11010B5CU, 0x8F659EFFU, 0xF862AE69U,
  0x616BFFD3U, 0x166CCF45U, 0xA00AE278U, 0xD70DD2EEU, 0x4E048354U, 0x3903B3C2U,
  0xA7672661U, 0xD06016F7U, 0x4969474DU, 0x3E6E77DBU, 0xAED16A4AU, 0xD9D65ADCU,
  0x40DF0B66U, 0x37D83BF0U, 0xA9BCAE53U, 0xDEBB9EC5U, 0x47B2CF7FU, 0x30B5FFE9U,
  0xBDBDF21CU, 0xCABAC28AU, 0x53B39330U, 0x24B4A3A6U, 0xBAD03605U, 0xCDD70693U,
  0x54DE5729U, 0x23D967BFU, 0xB3667A2EU, 0xC4614AB8U, 0x5D681B02U, 0x2A6F2B94U,
  0xB40BBE37U, 0xC30C8EA1U, 0x5A05DF1BU, 0x2D02EF8DU,
};

//----------------------------------------------------------------------------------------------------------------------------------

uint16_t pc_crc16(const uint8_t *data, uint32_t len)
{
  uint32_t crc = 0xFFFF;
  uint32_t i;

  //Only ever run over 14 header bytes, so bitwise is cheap enough and saves a 512 byte table
  while(len--)
  {
    crc ^= (uint32_t)(*data++) << 8;

    for(i=0;i<8;i++)
    {
      if(crc & 0x8000)
        crc = (crc << 1) ^ 0x1021;
      else
        crc <<= 1;
    }
  }

  return (uint16_t)crc;
}

//----------------------------------------------------------------------------------------------------------------------------------

uint32_t pc_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
  crc = ~crc;

  while(len--)
  {
    crc = pc_crc32_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
  }

  return ~crc;
}

//----------------------------------------------------------------------------------------------------------------------------------

uint32_t pc_crc32(const uint8_t *data, uint32_t len)
{
  return pc_crc32_update(0, data, len);
}

//----------------------------------------------------------------------------------------------------------------------------------

void pc_header_decode(const uint8_t *raw, pc_header *h)
{
  h->proto_major = raw[2];
  h->flags       = raw[3];
  h->cmd         = pc_get_u16(raw + 4);
  h->seq         = pc_get_u16(raw + 6);
  h->status      = pc_get_u16(raw + 8);
  h->len         = pc_get_u32(raw + 10);
}

//----------------------------------------------------------------------------------------------------------------------------------

int pc_header_valid(const uint8_t *raw)
{
  if((raw[0] != PC_MAGIC0) || (raw[1] != PC_MAGIC1))
    return 0;

  return (pc_crc16(raw, 14) == pc_get_u16(raw + 14));
}

//----------------------------------------------------------------------------------------------------------------------------------

uint32_t pc_frame_finish(uint8_t *frame, uint16_t cmd, uint16_t seq, uint16_t status, uint8_t flags, uint32_t len)
{
  frame[0] = PC_MAGIC0;
  frame[1] = PC_MAGIC1;
  frame[2] = PC_PROTO_MAJOR;
  frame[3] = flags;
  pc_put_u16(frame + 4, cmd);
  pc_put_u16(frame + 6, seq);
  pc_put_u16(frame + 8, status);
  pc_put_u32(frame + 10, len);
  pc_put_u16(frame + 14, pc_crc16(frame, 14));

  if(len)
  {
    pc_put_u32(frame + PC_HDR_SIZE + len, pc_crc32(frame + PC_HDR_SIZE, len));
  }

  return PC_FRAME_SIZE(len);
}

//----------------------------------------------------------------------------------------------------------------------------------

int pc_frame_needs_zlp(uint32_t frame_len, uint32_t packet_size)
{
  return (frame_len != 0) && ((frame_len % packet_size) == 0);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Parser
//
// Bytes go through a small replay queue: when a header CRC fails, the 14 bytes after the bad magic are pushed back to the
// front of the queue and parsed again (P-3.2 "restarts at the byte after the bad magic"). Only an ASCII line can complete
// inside 14 replayed bytes, so an event can occur while queued bytes remain; pc_parser_service() drains them.

static void pc_queue_prepend(pc_parser_q *q, const uint8_t *data, uint32_t len)
{
  uint32_t i;

  //Bounded by construction (see pc_protocol.h); drop on overflow rather than corrupt memory
  if(q->count + len > sizeof(q->buf))
    len = sizeof(q->buf) - q->count;

  //Move the existing bytes up
  for(i=q->count;i>0;i--)
    q->buf[i - 1 + len] = q->buf[i - 1];

  for(i=0;i<len;i++)
    q->buf[i] = data[i];

  q->count += len;
}

//----------------------------------------------------------------------------------------------------------------------------------

static int pc_step(pc_parser *p, uint8_t b, uint32_t now_ms);

static int pc_step_hunt(pc_parser *p, uint8_t b, uint32_t now_ms)
{
  if(b == PC_MAGIC0)
  {
    p->hdr_raw[0] = b;
    p->pos        = 1;
    p->state      = PC_PS_MAGIC1;
    p->last_ms    = now_ms;
  }
  else if(b == '#')
  {
    p->pos            = 0;
    p->ascii_overflow = 0;
    p->state          = PC_PS_ASCII;
    p->last_ms        = now_ms;
  }
  else
  {
    p->stats.junk_bytes++;
  }

  return PC_EV_NONE;
}

//----------------------------------------------------------------------------------------------------------------------------------

static int pc_step_header_done(pc_parser *p)
{
  //Header CRC first: a corrupt len must never make the parser wait for a bogus amount of data
  if(pc_header_valid(p->hdr_raw) == 0)
  {
    p->stats.hdr_crc_errors++;
    p->state = PC_PS_HUNT;

    //Restart at the byte after the bad magic. Byte 1 is 0x13, which can not start anything, so replay 2..15.
    pc_queue_prepend(&p->q, &p->hdr_raw[2], PC_HDR_SIZE - 2);
    return PC_EV_NONE;
  }

  pc_header_decode(p->hdr_raw, &p->hdr);
  p->state = PC_PS_HUNT;

  //A response towards the device is never valid (echoing host or loop back); drop it silently
  if(p->hdr.flags & PC_FLAG_RESPONSE)
  {
    p->stats.responses_dropped++;
    return PC_EV_NONE;
  }

  if(p->hdr.proto_major != PC_PROTO_MAJOR)
  {
    p->stats.version_errors++;
    return PC_EV_BAD_VERSION;
  }

  if(p->hdr.len > PC_MAX_REQUEST_PAYLOAD)
  {
    //The header CRC vouches for len, so skip exactly the payload and its CRC instead of hunting through it
    //(hunting would turn payload bytes into false '#' lines). A huge len ends with the 100 ms silence timeout.
    p->stats.oversize++;
    p->need  = p->hdr.len + PC_CRC_SIZE;
    p->state = PC_PS_SKIP;
    return PC_EV_BAD_LEN;
  }

  if(p->hdr.len == 0)
  {
    p->stats.frames++;
    return PC_EV_FRAME;
  }

  p->need  = p->hdr.len + PC_CRC_SIZE;
  p->pos   = 0;
  p->state = PC_PS_PAYLOAD;
  return PC_EV_NONE;
}

//----------------------------------------------------------------------------------------------------------------------------------

static int pc_step(pc_parser *p, uint8_t b, uint32_t now_ms)
{
  switch(p->state)
  {
    default:
    case PC_PS_HUNT:
      return pc_step_hunt(p, b, now_ms);

    case PC_PS_MAGIC1:
      if(b == PC_MAGIC1)
      {
        p->hdr_raw[1] = b;
        p->pos        = 2;
        p->state      = PC_PS_HEADER;
        p->last_ms    = now_ms;
        return PC_EV_NONE;
      }

      //Not a frame start; this byte may itself start one (A5 A5 13 ...) or a line
      p->stats.junk_bytes++;
      p->state = PC_PS_HUNT;
      return pc_step_hunt(p, b, now_ms);

    case PC_PS_HEADER:
      p->hdr_raw[p->pos++] = b;
      p->last_ms = now_ms;

      if(p->pos < PC_HDR_SIZE)
        return PC_EV_NONE;

      return pc_step_header_done(p);

    case PC_PS_PAYLOAD:
      p->payload[p->pos++] = b;
      p->last_ms = now_ms;

      if(p->pos < p->need)
        return PC_EV_NONE;

      p->state = PC_PS_HUNT;

      if(pc_crc32(p->payload, p->hdr.len) != pc_get_u32(p->payload + p->hdr.len))
      {
        p->stats.payload_crc_errors++;
        return PC_EV_BAD_CRC;
      }

      p->stats.frames++;
      return PC_EV_FRAME;

    case PC_PS_SKIP:
      p->last_ms = now_ms;

      if(--p->need == 0)
        p->state = PC_PS_HUNT;

      return PC_EV_NONE;

    case PC_PS_ASCII:
      p->last_ms = now_ms;

      if((b == '\r') || (b == '\n'))
      {
        p->state = PC_PS_HUNT;
        p->line[p->pos] = 0;

        if(p->ascii_overflow)
          return PC_EV_ASCII_OVERFLOW;

        return PC_EV_ASCII;
      }

      if(p->pos < PC_ASCII_MAX_LINE)
        p->line[p->pos++] = (char)b;
      else
        p->ascii_overflow = 1;

      return PC_EV_NONE;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------

void pc_parser_init(pc_parser *p)
{
  uint8_t *d = (uint8_t *)p;
  uint32_t i;

  for(i=0;i<sizeof(*p);i++)
    d[i] = 0;

  p->state = PC_PS_HUNT;
}

//----------------------------------------------------------------------------------------------------------------------------------

void pc_parser_reset(pc_parser *p)
{
  //Keep the statistics over link resets (DTR toggles)
  p->state    = PC_PS_HUNT;
  p->pos      = 0;
  p->q.count  = 0;
  p->ascii_overflow = 0;
}

//----------------------------------------------------------------------------------------------------------------------------------

int pc_parser_service(pc_parser *p, uint32_t now_ms)
{
  int ev;
  uint32_t i;

  while(p->q.count)
  {
    uint8_t b = p->q.buf[0];

    for(i=1;i<p->q.count;i++)
      p->q.buf[i - 1] = p->q.buf[i];

    p->q.count--;

    ev = pc_step(p, b, now_ms);

    if(ev != PC_EV_NONE)
      return ev;
  }

  return PC_EV_NONE;
}

//----------------------------------------------------------------------------------------------------------------------------------

int pc_parser_feed(pc_parser *p, uint8_t byte, uint32_t now_ms)
{
  //Fast path, nothing queued: parse directly. A header failure here queues its replay bytes, drained below.
  if(p->q.count == 0)
  {
    int ev = pc_step(p, byte, now_ms);

    if(ev != PC_EV_NONE)
      return ev;

    return pc_parser_service(p, now_ms);
  }

  //Bytes still queued from a replay: this byte goes behind them
  if(p->q.count < sizeof(p->q.buf))
    p->q.buf[p->q.count++] = byte;

  return pc_parser_service(p, now_ms);
}

//----------------------------------------------------------------------------------------------------------------------------------

int pc_parser_tick(pc_parser *p, uint32_t now_ms)
{
  if((p->state != PC_PS_HUNT) && ((uint32_t)(now_ms - p->last_ms) >= PC_RX_TIMEOUT_MS))
  {
    p->stats.timeouts++;
    p->state = PC_PS_HUNT;
    return 1;
  }

  return 0;
}

//----------------------------------------------------------------------------------------------------------------------------------

int pc_parser_busy(const pc_parser *p)
{
  return (p->state != PC_PS_HUNT) || (p->q.count != 0);
}

//----------------------------------------------------------------------------------------------------------------------------------
