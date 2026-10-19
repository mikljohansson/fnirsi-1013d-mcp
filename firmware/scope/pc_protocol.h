//----------------------------------------------------------------------------------------------------------------------------------
// pc_protocol.h - FNIRSI 1013D PC control protocol v1.0, frame layer (docs/protocol.md sections 3, 4, 9, 10).
//
// Transport independent, no hardware includes, no globals: every bit of state lives in a pc_parser that the
// caller owns. Builds for the scope (arm-none-eabi, -nostdlib) and on the host (firmware/scope/host_tests).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_PROTOCOL_H
#define PC_PROTOCOL_H

#include <stdint.h>

//----------------------------------------------------------------------------------------------------------------------------------
// Frame layout (P-4.1)

#define PC_MAGIC0                 0xA5
#define PC_MAGIC1                 0x13
#define PC_PROTO_MAJOR            1
#define PC_PROTO_MINOR            14    //1.1: GET_STATUS pass times (fw 0.1.1); 1.2: GET_LINK_STATS (fw 0.1.4);
                                        //1.3: shared control, SET_ACQ_STATE, GET_CAPTURE_INFO (fw 0.2.0),
                                        //     SD sector commands (fw 0.2.2); 1.4: EXT trigger (FPGA 0x1533, fw 0.2.9);
                                        //1.5: GET_EVENT_LOG (fw 0.2.12); 1.6: FFT_BENCH (fw 0.2.27);
                                        //1.7: spectrum view, READ_SPECTRUM, GET_STATUS display_mode (fw 0.2.28);
                                        //1.9: decimation filter (FPGA 0x1534, fw 0.2.30);
                                        //1.10: calibration data v2, GET_CAL 248 bytes (fw 0.2.43);
                                        //1.11: CIC compensation FIR (FPGA 0x1535, fw 0.2.44);
                                        //1.12: calibration data v3 (per-core, per-class gain), GET_CAL 256 bytes
                                        //      (fw 0.2.45)
                                        //1.13: phosphor view, GET_STATUS display_mode 4 (fw 0.2.46)
                                        //1.14: USB suspend watch, link counters 60-61, event type 20 (fw 0.2.49)

#define PC_HDR_SIZE               16
#define PC_CRC_SIZE               4

#define PC_FLAG_RESPONSE          0x01
#define PC_FLAG_EVENT             0x02

#define PC_MAX_REQUEST_FRAME      512
#define PC_MAX_REQUEST_PAYLOAD    (PC_MAX_REQUEST_FRAME - PC_HDR_SIZE - PC_CRC_SIZE)   // 492
#define PC_MAX_RESPONSE_PAYLOAD   66560u
#define PC_MAX_RESPONSE_FRAME     (PC_HDR_SIZE + PC_MAX_RESPONSE_PAYLOAD + PC_CRC_SIZE)

#define PC_USB_PACKET             512      // high-speed bulk wMaxPacketSize

#define PC_RX_TIMEOUT_MS          100      // partial frame discarded after this much silence (P-3.2)

#define PC_ASCII_MAX_LINE         63       // P-10

//Frame size for a payload length
#define PC_FRAME_SIZE(len)        (PC_HDR_SIZE + (len) + ((len) ? PC_CRC_SIZE : 0))

//----------------------------------------------------------------------------------------------------------------------------------
// Command ids (P-8.1)

#define PC_CMD_PING               0x0001
#define PC_CMD_GET_INFO           0x0002
#define PC_CMD_GET_STATUS         0x0003
#define PC_CMD_SET_REMOTE         0x0004
#define PC_CMD_GET_BATTERY        0x0005
#define PC_CMD_TEST_PATTERN       0x0006
#define PC_CMD_GET_LINK_STATS     0x0007     //1.2: link counters (firmware 0.1.4)
#define PC_CMD_GET_BAD_FRAME      0x0008     //1.2: last request that failed its payload CRC, as received
#define PC_CMD_GET_EVENT_LOG      0x0009     //1.5: the last 64 USB / link events with timestamps
#define PC_CMD_GET_CHANNEL        0x0100
#define PC_CMD_SET_CHANNEL        0x0101
#define PC_CMD_GET_TIMEBASE       0x0102
#define PC_CMD_SET_TIMEBASE       0x0103
#define PC_CMD_GET_TRIGGER        0x0104
#define PC_CMD_SET_TRIGGER        0x0105
#define PC_CMD_ARM                0x0200
#define PC_CMD_FORCE              0x0201
#define PC_CMD_STOP               0x0202
#define PC_CMD_WAIT               0x0203
#define PC_CMD_READ_RAW           0x0204
#define PC_CMD_READ_TRACE         0x0205
#define PC_CMD_READ_LIVE          0x0206
#define PC_CMD_ACQUIRE_AVG        0x0207
#define PC_CMD_SET_ACQ_STATE      0x0208     //1.3: UI RUN / STOP
#define PC_CMD_GET_CAPTURE_INFO   0x0209     //1.3: settings of the latest finished capture
#define PC_CMD_READ_SPECTRUM      0x020A     //1.7: the spectrum the scope displays (fw 0.2.28)
#define PC_CMD_GET_DECIM          0x020C     //1.9: decimation filter setting and state (FPGA 0x1534+)
#define PC_CMD_SET_DECIM          0x020D     //1.9: decimation filter setting (the UI's acquisition Filter)
#define PC_CMD_SD_INFO            0x0500     //1.3: card size, partition start, write window, eGON headers
#define PC_CMD_SD_READ_SECTORS    0x0501     //1.3: up to 128 sectors
#define PC_CMD_SD_STAGE           0x0502     //1.3: bytes into the 64 KiB write staging buffer
#define PC_CMD_SD_WRITE_SECTORS   0x0503     //1.3: staged sectors to the card, read back and verified
#define PC_CMD_GET_GEN            0x0300
#define PC_CMD_SET_GEN            0x0301
#define PC_CMD_GET_CAL            0x0400
#define PC_CMD_READ_SCREEN        0x0401
#define PC_CMD_DEBUG_FPGA         0x0F00
#define PC_CMD_ENTER_FEL          0x0F01
#define PC_CMD_FFT_BENCH          0x0F02     //1.6: time the on-scope FFT (fw 0.2.27)

//----------------------------------------------------------------------------------------------------------------------------------
// Error codes (P-9)

#define PC_OK                     0
#define PC_E_BAD_CRC              1
#define PC_E_BAD_LEN              2
#define PC_E_UNKNOWN_CMD          3
#define PC_E_BAD_ARG              4
#define PC_E_UNSUPPORTED          5
#define PC_E_NOT_REMOTE           6
#define PC_E_BUSY                 7
#define PC_E_STATE                8
#define PC_E_FPGA                 9
#define PC_E_ABORTED              10
#define PC_E_VERSION              11
#define PC_E_OVERFLOW             12
#define PC_E_IO                   13     //1.3: SD card error; detail = driver error (negated) or 0xFFFF verify mismatch
#define PC_E_INTERNAL             255

//----------------------------------------------------------------------------------------------------------------------------------
// GET_INFO capability bits (P-8.2)

#define PC_CAP_PROG_TRIGGER_POS       (1u << 0)
#define PC_CAP_GENERATOR_DDS          (1u << 1)
#define PC_CAP_HOLDOFF                (1u << 2)
#define PC_CAP_EXT_TRIGGER            (1u << 3)
#define PC_CAP_TRIG_BOTH_CORES        (1u << 4)
#define PC_CAP_FORCE_TRIGGER          (1u << 5)
#define PC_CAP_SCREENSHOT             (1u << 6)
#define PC_CAP_DEVICE_AVERAGE         (1u << 7)
#define PC_CAP_DEBUG_RAW              (1u << 8)
#define PC_CAP_LIVE_READ              (1u << 9)
#define PC_CAP_ENTER_FEL              (1u << 10)
#define PC_CAP_READ_LAG_UNVERIFIED    (1u << 11)
#define PC_CAP_INTERLEAVE_UNVERIFIED  (1u << 12)
#define PC_CAP_SHARED_MODE            (1u << 13)   //1.3: no LOCAL/REMOTE, shared control with the UI
#define PC_CAP_SD_SECTORS             (1u << 14)   //1.3: SD_INFO / SD_READ_SECTORS / SD_STAGE / SD_WRITE_SECTORS
#define PC_CAP_DECIM                  (1u << 15)   //1.9: decimation filter, 16-bit samples (FPGA 0x1534+)
#define PC_CAP_FIR                    (1u << 16)   //1.11: CIC compensation FIR, SET_DECIM flags bit 3 (FPGA 0x1535+)

//----------------------------------------------------------------------------------------------------------------------------------
// Decoded header

typedef struct
{
  uint8_t  proto_major;
  uint8_t  flags;
  uint16_t cmd;
  uint16_t seq;
  uint16_t status;
  uint32_t len;
} pc_header;

//----------------------------------------------------------------------------------------------------------------------------------
// Parser

//Events returned by pc_parser_feed()
#define PC_EV_NONE                0     //byte consumed, nothing complete
#define PC_EV_FRAME               1     //valid request: header + payload CRC ok. p->hdr, p->payload valid
#define PC_EV_BAD_CRC             2     //header ok, payload CRC wrong: answer E_BAD_CRC with hdr.cmd/seq
#define PC_EV_BAD_LEN             3     //header ok, len > PC_MAX_REQUEST_PAYLOAD: answer E_BAD_LEN, payload is skipped
#define PC_EV_BAD_VERSION         4     //header ok, proto_major != 1: answer E_VERSION, parser resyncs
#define PC_EV_ASCII               5     //ASCII probe line complete: p->line (NUL terminated, without '#')
#define PC_EV_ASCII_OVERFLOW      6     //ASCII line longer than 63 bytes: answer "#ERR,UNKNOWN"

//Parser states (internal)
#define PC_PS_HUNT                0
#define PC_PS_MAGIC1              1
#define PC_PS_HEADER              2
#define PC_PS_PAYLOAD             3
#define PC_PS_ASCII               4
#define PC_PS_SKIP                5     //discarding the payload of an oversize frame

typedef struct
{
  uint32_t frames;            //PC_EV_FRAME count
  uint32_t hdr_crc_errors;    //silently dropped headers
  uint32_t payload_crc_errors;
  uint32_t oversize;
  uint32_t version_errors;
  uint32_t responses_dropped; //frames with the RESPONSE flag (never valid towards the device)
  uint32_t timeouts;          //partial frames discarded after PC_RX_TIMEOUT_MS
  uint32_t junk_bytes;        //bytes discarded while hunting for magic
} pc_parser_stats;

//Replay queue: bytes re-parsed after a header CRC failure. At most 2 x 14 bytes can be queued at once.
typedef struct
{
  uint8_t  buf[32];
  uint32_t count;
} pc_parser_q;

typedef struct
{
  uint8_t   state;
  uint8_t   ascii_overflow;
  uint16_t  pos;                                        //bytes collected in the current header / payload+crc / line
  uint32_t  last_ms;                                    //time of the last byte while not hunting
  uint32_t  need;                                       //payload + crc bytes expected (or to skip)
  uint8_t   hdr_raw[PC_HDR_SIZE];
  pc_header hdr;
  uint8_t   payload[PC_MAX_REQUEST_PAYLOAD + PC_CRC_SIZE] __attribute__((aligned(4)));
  char      line[PC_ASCII_MAX_LINE + 1];
  pc_parser_q q;
  pc_parser_stats stats;
} pc_parser;

//----------------------------------------------------------------------------------------------------------------------------------
// API

uint16_t pc_crc16(const uint8_t *data, uint32_t len);                     //CRC-16/CCITT-FALSE, init 0xFFFF
uint32_t pc_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len); //start with 0, raw ISO-HDLC chaining
uint32_t pc_crc32(const uint8_t *data, uint32_t len);                     //CRC-32/ISO-HDLC (= zlib.crc32)

void     pc_parser_init(pc_parser *p);                                     //everything zero
void     pc_parser_reset(pc_parser *p);                                    //drop partial input, keep statistics

//Feed one received byte. now_ms is a free running millisecond counter (wraps).
//After PC_EV_FRAME / PC_EV_BAD_* / PC_EV_ASCII* the decoded data stay valid until the next byte is fed.
int      pc_parser_feed(pc_parser *p, uint8_t byte, uint32_t now_ms);

//Process bytes still queued from a header resync without new input. Call until it returns PC_EV_NONE when the
//input source is empty, otherwise an ASCII line hidden in replayed bytes is only seen with the next byte.
int      pc_parser_service(pc_parser *p, uint32_t now_ms);

//Discard a partial frame or line when no byte arrived for PC_RX_TIMEOUT_MS. Returns 1 when something was discarded.
int      pc_parser_tick(pc_parser *p, uint32_t now_ms);

//1 while a frame or line is partially received
int      pc_parser_busy(const pc_parser *p);

//Decode / encode headers. pc_header_decode does not check the magic or CRC.
void     pc_header_decode(const uint8_t *raw, pc_header *h);
int      pc_header_valid(const uint8_t *raw);                              //magic + header CRC

//Build a frame in place: frame[0..15] = header, payload must already be at frame + 16, len bytes.
//Appends the payload CRC when len > 0. Returns the total frame size.
uint32_t pc_frame_finish(uint8_t *frame, uint16_t cmd, uint16_t seq, uint16_t status, uint8_t flags, uint32_t len);

//1 when a frame of this total size needs a zero length packet after it (P-3.2)
int      pc_frame_needs_zlp(uint32_t frame_len, uint32_t packet_size);

//Little-endian field helpers (unaligned safe)
static inline uint16_t pc_get_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t pc_get_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint64_t pc_get_u64(const uint8_t *p) { return (uint64_t)pc_get_u32(p) | ((uint64_t)pc_get_u32(p + 4) << 32); }
static inline void pc_put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void pc_put_u32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static inline void pc_put_u64(uint8_t *p, uint64_t v) { pc_put_u32(p, (uint32_t)v); pc_put_u32(p + 4, (uint32_t)(v >> 32)); }

#endif /* PC_PROTOCOL_H */
