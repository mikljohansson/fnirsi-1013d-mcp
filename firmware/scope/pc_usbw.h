//----------------------------------------------------------------------------------------------------------------------------------
// USB suspend watch (fw 0.2.49, protocol row 53). Pure C, no registers: pc_transport.c feeds it the CDC interrupt events
// and runs pc_usbw_tick from the 1 ms timer interrupt with the frame number; it returns what to do. Host-tested in
// host_tests/test_usbw.c.
//
// Why: the device has never seen a RESUME interrupt on any build (GET_LINK_STATS 37 = 0 on every bench, 17 suspends and
// no resume before the 2026-10-19 loss). Two things follow:
//  - a resume the core does not report must still end the suspend state: the frame number moving again (SOFs), an EP0
//    setup or an EP2 OUT packet while marked suspended is an inferred resume (counter 60, type 3 with a bit 7);
//  - a host that gave up on the device leaves the bus idle forever, which looks exactly like a suspend. The only way
//    back is a new connection: soft disconnect (POWER SoftConn off for PC_USBW_OFF_MS, D+ pull-up released), then on
//    again, so the hub reports a connect change and the host enumerates a fresh device (counter 61, type 20).
//
// When a soft reconnect fires (both need the bus idle, no SOF, since the last suspend):
//  - storm: PC_USBW_STORM_SUSPENDS or more suspends since the last proof of life (SET_CONFIGURATION, a resume seen or
//    inferred) and then PC_USBW_STORM_QUIET_MS of silence. A host retrying a port reset produces a suspend per attempt
//    (at high speed the reset's SE0 is first seen as 3 ms of idle); when it gives up, the bus goes quiet. The wait
//    doubles with every storm reconnect that is not followed by a successful enumeration (at most 2^PC_USBW_LEVEL_MAX).
//  - long: suspended for PC_USBW_LONG_MS after a host enumerated the device. Once per enumeration: a reconnect clears
//    that, so a host that is asleep (or a pulled cable, which also reads as suspend with VBUS forced valid) costs one
//    reconnect, not one per period.
// A normal suspend never meets the storm rule (one suspend, then resume or reset). The long rule cannot tell a long
// legitimate suspend (PC asleep, port closed) from an abandoned device; it costs one re-enumeration then, which the
// host sees as the device unplugged and plugged in while nothing had the port open.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_USBW_H
#define PC_USBW_H

#include <stdint.h>

#define PC_USBW_OFF_MS           1000u        //pull-up released this long (hub debounce 100 ms, Windows polls the hub)
#define PC_USBW_STORM_SUSPENDS   3u
#define PC_USBW_STORM_QUIET_MS   10000u       //x 2^level
#define PC_USBW_LEVEL_MAX        6u           //storm wait at most 640 s
#define PC_USBW_LONG_MS          900000u      //15 min

//Inferred resume causes (type 3 a bits 6-5). 3 is what fw 0.2.44-0.2.48 logged (a = 0x80 | POWER bits 6-0, SoftConn and
//HS enable always set): the frame number moved
#define PC_USBW_CAUSE_FRAME      0u           //frame number moved: SOFs again
#define PC_USBW_CAUSE_EP0        1u           //EP0 setup packet
#define PC_USBW_CAUSE_EP2        2u           //EP2 OUT packet

//Soft reconnect reasons (type 20 a bits 1-0)
#define PC_USBW_WHY_STORM        1u
#define PC_USBW_WHY_LONG         2u

//pc_usbw_tick results
#define PC_USBW_NONE             0
#define PC_USBW_RESUMED          1            //inferred resume (frame moved): end the suspend state
#define PC_USBW_DISCONNECT       2            //clear SoftConn now, log type 20
#define PC_USBW_CONNECT          3            //set SoftConn again

typedef struct
{
  uint8_t  susp;          //SUSPEND seen and nothing since that ended it
  uint8_t  off;           //soft disconnect in progress
  uint8_t  enumerated;    //SET_CONFIGURATION since boot / the last soft reconnect (arms the long rule)
  uint8_t  strikes;       //suspends since the last proof of life (saturates at 255)
  uint8_t  level;         //storm back-off level
  uint8_t  why;           //reason of the last DISCONNECT (for the log)
  uint16_t susp_frame;    //frame number at the suspend
  uint32_t susp_ms;       //time of the suspend (or of the reconnect)
  uint32_t off_ms;        //time of the soft disconnect
  uint32_t inferred;      //GET_LINK_STATS 60
  uint32_t reconnects;    //GET_LINK_STATS 61
} pc_usbw;

void     pc_usbw_init(pc_usbw *w);                                  //boot, usb_device_init (CDC <-> MSC switch)
void     pc_usbw_suspend(pc_usbw *w, uint32_t now, uint16_t frame);  //SUSPEND interrupt
void     pc_usbw_resume(pc_usbw *w);                                //RESUME interrupt
void     pc_usbw_reset(pc_usbw *w);                                 //bus reset or disconnect interrupt
void     pc_usbw_configured(pc_usbw *w);                            //SET_CONFIGURATION
int      pc_usbw_activity(pc_usbw *w, uint8_t cause);               //EP0 setup / EP2 OUT: 1 = this ended a suspend
int      pc_usbw_tick(pc_usbw *w, uint32_t now, uint16_t frame);    //every ms: PC_USBW_*
uint32_t pc_usbw_storm_wait(const pc_usbw *w);                      //current storm quiet time, ms

#endif /* PC_USBW_H */
