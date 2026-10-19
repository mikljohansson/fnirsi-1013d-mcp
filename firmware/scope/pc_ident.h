//----------------------------------------------------------------------------------------------------------------------------------
// pc_ident.h - firmware image identity (fw 0.2.52, protocol 1.16, docs/protocol.md section 14 row 55).
//
// The running image carries an identity record as its very last object (build_id.c, linked last): magic, version
// string, commit hash, commit time, protocol version. pc_ident reads the same facts back from an image on the SD card
// (sector 750): the record (0.2.52+), the 12-byte build tail of 0.2.47-0.2.51, or only the `#VER` text every protocol
// image carries. Host-buildable: the card is reached through pc_hal_sd_read; host_tests/test_ident.c tests it.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_IDENT_H
#define PC_IDENT_H

#include <stdint.h>

#define PC_ID_MAGIC0         0x44495746u  //"FWID"
#define PC_ID_MAGIC1         0x33313031u  //"1013"
#define PC_ID_BYTES          36           //the record as linked
#define PC_ID_TAIL_SEARCH    1024         //the record is looked for in the image's last bytes

typedef struct
{
  uint32_t magic0, magic1;
  char     version[16];                   //NUL-padded PC_FW_VERSION
  uint32_t build;                         //short git hash
  uint8_t  time[6];                       //commit time (UTC) yy, mm, dd, hh, mm, ss
  uint8_t  proto_major, proto_minor;
} pc_image_id;

extern const pc_image_id pc_image_ident;  //the running image's (build_id.c; host tests: mock_hal.c)

//SD_INFO byte 64
#define PC_ID_NOT_READ       0
#define PC_ID_RECORD         1            //identity record (0.2.52+)
#define PC_ID_TAIL           2            //0.2.47-0.2.51 build tail + version string
#define PC_ID_VERSION_ONLY   3            //older protocol images: version string only
#define PC_ID_UNKNOWN        4
#define PC_ID_NO_IMAGE       5            //no eGON.EXE, length invalid, or a read error

typedef struct
{
  uint8_t  state;                         //PC_ID_*
  uint8_t  same;                          //1: identical to the running image (version, build and time)
  uint8_t  proto_major, proto_minor;      //0 = unknown
  char     version[16];                   //NUL-padded, empty when unknown
  uint32_t build;                         //0 = unknown
  uint8_t  time[6];                       //zeros = unknown
  uint32_t length;                        //eGON header length
  uint32_t read_ms;                       //time the identification took (0 = cached)
} pc_card_id;

#define PC_ID_SECTOR         750          //the open firmware image (firmware/README.md section 3)
#define PC_ID_MAX_SECTORS    2048

//Pure parts (buffers in memory)
int  pc_ident_find_record(const uint8_t *tail, uint32_t n, pc_image_id *out);   //tail = the image's last n bytes; 1 found
int  pc_ident_legacy_tail(const uint8_t *tail, uint32_t n, uint32_t *build, uint8_t *time);   //0.2.47-0.2.51; 1 found
int  pc_ident_version_at_least(const char *v, uint32_t major, uint32_t minor, uint32_t patch);

//Streaming scan for "FNIRSI-1013D-AI,fw=<version>,proto=<major>.<minor>," over consecutive chunks
typedef struct
{
  uint32_t state;                         //0..: matching the needle, then collecting
  uint32_t n;
  uint8_t  done;
  char     version[16];
  uint8_t  major, minor;
  uint8_t  phase;                         //0 needle, 1 version, 2 "proto=", 3 major, 4 minor
} pc_ident_scan;

void pc_ident_scan_init(pc_ident_scan *s);
void pc_ident_scan_feed(pc_ident_scan *s, const uint8_t *buf, uint32_t n);

//Card: identify the image at PC_ID_SECTOR (MAIN context: SD access) into the cache, or return the cache. scratch is
//4-byte aligned, scratch_bytes a multiple of 512 (>= 1024). force = 0: reuse a cached result.
const pc_card_id *pc_ident_card(int read, uint8_t *scratch, uint32_t scratch_bytes);
void pc_ident_invalidate(void);                                   //SD_WRITE_SECTORS reached sector 750 or later

#endif /* PC_IDENT_H */
