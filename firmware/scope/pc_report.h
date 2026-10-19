//----------------------------------------------------------------------------------------------------------------------------------
// pc_report.h - the diagnostics report text ("Save report", SAVE_REPORT, fw 0.2.52, protocol row 55): identity, unit life,
// USB, restarts, link counters, SD boot sectors, calibration and the event log, all in words. Built from the same bodies the
// protocol answers (GET_HEALTH, GET_CAL, SD_INFO, GET_LINK_STATS, GET_EVENT_LOG), so the file and the MCP agree.
// Host-buildable (pc_hal.h only); host_tests/test_session.c checks it.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_REPORT_H
#define PC_REPORT_H

#include <stdint.h>
#include "pc_cmds.h"

#define PC_REPORT_SCRATCH    32768            //SD scratch at the workspace's start while the card is read
#define PC_REPORT_WORKSPACE  49152            //minimum workspace: the scratch first, then the text (and 1280 bytes of events)

//Text builder (no libc): appends, truncates at max - 1, always NUL-terminated
typedef struct
{
  char     *buf;
  uint32_t  max;
  uint32_t  len;
  uint8_t   full;              //something was cut off
} pc_text;

void pc_text_init(pc_text *t, char *buf, uint32_t max);
void pc_text_str(pc_text *t, const char *s);
void pc_text_dec(pc_text *t, int32_t v);
void pc_text_udec(pc_text *t, uint32_t v);
void pc_text_hex(pc_text *t, uint32_t v, uint32_t digits);     //"0x" + digits
void pc_text_fix(pc_text *t, int32_t v, uint32_t denom, uint32_t decimals);   //v / denom, rounded, sign kept
void pc_text_nl(pc_text *t);                                                  //CR LF

//Builds the report into workspace (>= PC_REPORT_WORKSPACE bytes, 4-byte aligned; its first PC_REPORT_SCRATCH bytes are
//SD scratch while the card is read). Returns the text length; the text starts at workspace (MAIN context: reads the card).
uint32_t pc_report_build(pc_session *s, uint8_t *workspace, uint32_t size);

//Builds the report and writes it to the card (pc_hal_report_write): 0 or the FatFs FRESULT; *written = bytes, name = the
//file name (NUL-terminated). The Diagnostics "Save report" button and SAVE_REPORT both end here.
int      pc_save_report(pc_session *s, uint8_t *workspace, uint32_t size, uint32_t *written, char *name, uint32_t name_max);

#endif /* PC_REPORT_H */
