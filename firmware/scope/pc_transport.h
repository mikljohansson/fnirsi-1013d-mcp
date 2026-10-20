//----------------------------------------------------------------------------------------------------------------------------------
// pc_transport.h - USB CDC byte transport for the PC protocol (docs/plans/firmware-usb-protocol.md section 6).
//
// ISR side (called from usb_device_CDC_irq_handler with EPIND already selected as noted) and the main-loop side
// (the transport part of pc_hal.h: pc_hal_rx_read, pc_hal_tx_submit, ...).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_TRANSPORT_H
#define PC_TRANSPORT_H

#include "types.h"

#include "pc_rx_ring.h"           //PC_RX_RING_SIZE

//Endpoint FIFO layout. The F1C100s USB FIFO RAM is 2048 bytes (F1C100s datasheet: "Configurable Data FIFO. The size
//is 2048 byte"), and the Endpoint 0 FIFO has a fixed size (64 bytes) and a fixed location (start address 0)
//(MUSBMHDRC programmer's guide, dynamic FIFO note). FIFOADD is in 8-byte units, FIFOSZ = 2^(n+3) bytes, DPB doubles
//it. 1.00o5's CDC layout put EP2 OUT on top of EP0 at 0, EP1 at byte 2048 and the EP2 TX FIFO at byte 4096, both
//outside the RAM (aliasing onto 0): control requests overwrote bulk data and IN packets overwrote waiting requests.
//  map 0 (default):  EP0 0-63 fixed | EP1 IN 64-127 | EP2 OUT 512-1023 single | EP2 IN 1024-2047 2 x 512 DPB
//  map 1:            EP2 OUT 64-575 | EP2 IN 576-1599 DPB | EP1 1600-1663 (fw 0.1.3-0.1.5 default)
//PC_FIFO_MAP selects the layout (make PC_FIFO_MAP=n). The 0.1.3-0.1.5 request corruption was the RX ring-wrap FIFO
//read in pc_transport.c, not the layout.
#ifndef PC_FIFO_MAP
#define PC_FIFO_MAP 0
#endif

#if PC_FIFO_MAP == 1
//  map 1 = the fw 0.1.3-0.1.5 default, kept for comparison: EP2 OUT 64-575, EP2 IN 576-1599 (DPB), EP1 1600-1663
#define PC_EP2_RXFIFOAD   8
#define PC_EP2_TXFIFOAD   72
#define PC_EP1_TXFIFOAD   200
#else
//  map 0 (fw 0.1.6): size-aligned like 1.00o5 MSC and pcprogrammer's ch340 code. EP0 0-63 | EP1 IN 64-127 |
//  EP2 OUT 512-1023 single | EP2 IN 1024-2047 (2 x 512, DPB). The guide only requires 8-byte granularity.
#define PC_EP2_RXFIFOAD   64
#define PC_EP2_TXFIFOAD   128
#define PC_EP1_TXFIFOAD   8
#endif
#define PC_EP2_RXFIFOSZ   6
#define PC_EP2_TXFIFOSZ   (USBC_BP_TXFIFOSZ_DPB | 6)
#define PC_EP1_TXFIFOSZ   3

//Compile-time layout check (byte ranges, EP0 = 0-63): pairwise disjoint and inside 2048
#define PC_FB(u)          ((u) * 8)
#define PC_DISJ(a, la, b, lb) ((PC_FB(a) + (la) <= PC_FB(b)) || (PC_FB(b) + (lb) <= PC_FB(a)))
typedef char pc_fifo_layout_check[(PC_DISJ(0, 64, PC_EP2_RXFIFOAD, 512) && PC_DISJ(0, 64, PC_EP2_TXFIFOAD, 1024) &&
                                   PC_DISJ(0, 64, PC_EP1_TXFIFOAD, 64) && PC_DISJ(PC_EP2_RXFIFOAD, 512, PC_EP2_TXFIFOAD, 1024) &&
                                   PC_DISJ(PC_EP2_RXFIFOAD, 512, PC_EP1_TXFIFOAD, 64) && PC_DISJ(PC_EP2_TXFIFOAD, 1024, PC_EP1_TXFIFOAD, 64) &&
                                   (PC_FB(PC_EP2_RXFIFOAD) + 512 <= 2048) && (PC_FB(PC_EP2_TXFIFOAD) + 1024 <= 2048) &&
                                   (PC_FB(PC_EP1_TXFIFOAD) + 64 <= 2048)) ? 1 : -1];

//Called from usb_device_init(): CDC (re)start or MSC switch. Drops all buffered data, flags a link reset.
void pc_transport_reset(void);

//ISR side. EPIND must be 2 for the two endpoint handlers.
void pc_usb_isr_rx(void);                   //EP2 OUT packet(s) ready
void pc_usb_isr_tx(void);                   //EP2 IN packet sent
void pc_usb_isr_link(void);                 //bus reset or disconnect
void pc_usb_isr_ep_cleared(uint8 ep);       //CLEAR_FEATURE(ENDPOINT_HALT) flushed this endpoint's FIFO (0x02 / 0x82)
void pc_usb_dtr(uint8 dtr);                 //SET_CONTROL_LINE_STATE
void pc_usb_isr_event(uint8 ev);            //PC_USB_EV_*: counted for GET_LINK_STATS (fw 0.2.5)
void pc_usb_isr_resume(void);               //RESUME: the configuration is kept, re-enable the EP2 OUT interrupt
void pc_usb_isr_suspend(void);              //SUSPEND (fw 0.2.44): a state, not a link reset; logged with POWER, frame
void pc_usb_watch_tick(void);               //timer interrupt, every ms: suspend watch (fw 0.2.49, pc_usbw.h)
void pc_usb_isr_configured(void);           //SET_CONFIGURATION set up EP2

#define PC_USB_EV_BUS_RESET   0
#define PC_USB_EV_SUSPEND     1
#define PC_USB_EV_RESUME      2
#define PC_USB_EV_DISCONNECT  3
#define PC_USB_EV_SETUP       4             //EP0 setup packets
#define PC_USB_EV_BAD_SETUP   5             //EP0 packets that were not 8 bytes (stalled)
#define PC_USB_EV_COUNT       6

//Event log (fw 0.2.12, GET_EVENT_LOG): the last PC_EVLOG_SIZE USB / link events with their ms time
#define PC_EVLOG_SIZE         128   //fw 0.2.15 (64 before)
#define PC_EVSNAP_SIZE        16    //events frozen at a bus reset (fw 0.2.15)
#define PC_EVBUS_SIZE         16    //fw 0.2.24: last bus-level events, a ring the EP0 / DTR chatter cannot evict
//Types copied into it: 1-4 (reset, suspend, resume, disconnect), 7, 8, 12, 13, 15-18, 20
#define PC_EVBUS_TYPES        ((1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 7) | (1u << 8) | (1u << 12) |\
                               (1u << 13) | (1u << 15) | (1u << 16) | (1u << 17) | (1u << 18) | (1u << 20))
#define PC_SETUP_REPEAT_MS    1000  //fw 0.2.24: a setup identical to the last logged one is logged at most once per second
#define PC_EVT_BUS_RESET      1     //fw 0.2.44: a = 1 if suspended before, b = ms since that suspend (65535 = longer)
#define PC_EVT_SUSPEND        2     //fw 0.2.44: a = POWER, b = frame number
#define PC_EVT_RESUME         3     //fw 0.2.44: a = POWER (bit 7: inferred, no RESUME interrupt; 0.2.49: bits 6-5 cause,
                                    //bits 4-0 POWER), b = ms suspended
#define PC_EVT_DISCONNECT     4     //fw 0.2.44: as type 1
#define PC_EVT_SETUP          5     //a = bRequest, b = bmRequestType << 8 | wValue low byte
#define PC_EVT_BAD_SETUP      6     //b = COUNT0
#define PC_EVT_CONFIGURED     7
#define PC_EVT_EP_CLEARED     8     //a = endpoint (0x02 / 0x82)
#define PC_EVT_DTR            9     //a = new DTR
#define PC_EVT_RX_STALL       10    //ring full, EP2 OUT held back
#define PC_EVT_RX_RELEASE     11
#define PC_EVT_TX_FLUSH       12    //a frame in flight was dropped (link, DTR)
#define PC_EVT_TX_ABORT       13    //dropped by the session (2 s no progress, #RESET)
//type 14 retired (fw 0.2.12-0.2.20: USB tick fallback served flags; removed in 0.2.21, never emitted)
#define PC_EVT_USB_INIT       16    //fw 0.2.22, usb_device_init before its reset, a = field, b = value (protocol section 14 row 43)
#define PC_EVT_TX_ABORT_CSR   17    //fw 0.2.22, after each type 13: a = POWER, b = EP2 TXCSR
#define PC_EVT_TX_ABORT_FRM   18    //fw 0.2.22, after type 17: a = bit 0 EP2 TX interrupt enabled, bit 1 ZLP pending; b = frame number
#define PC_EVT_USB_POST_INIT  19    //fw 0.2.25, end of usb_device_init, a = field, b = value (protocol section 14 row 43)
#define PC_EVT_SOFT_DISCONNECT 20   //fw 0.2.49, suspend watch reconnect (row 53): a = reason | level << 2 | strikes << 4,
                                    //b = s suspended
#define PC_EVT_FPGA_HEAL      21    //fw 0.2.61, full-rate capture garbled after a rate change (enc_heal.h, row 57): a =
                                    //retries, bit 7 not recovered; b = the larger residual of the first check, 1/16 code
#define PC_EVT_FPGA_WRITE     22    //fw 0.2.61, an FPGA write of 0x0D, 0x53, 0x56 or 0x01 = 1 (fw 0.2.87: 0x54 shift and
                                    //0x55 16-bit level too): a = command, b = caller (FPGA_WHO_*) << 12 | value
                                    //(saturated at 0xFFF)
#define PC_EVT_FPGA_CAPTURE   23    //fw 0.2.79, FPGA 0x153A capture stages (cap_hw.c): a bits 3:0 cores flagged by the
                                    //self-check (bit 0 CH1 A .. 3 CH2 B), b = the largest excess; a bit 4 set: a
                                    //training ended, bits 3:0 the cores without a clean stage, b = the 0x63 value in effect
#define PC_EVT_RESTART        15    //fw 0.2.19, at boot after a restart from RAM: a = exception type, b = low PC bits

void pc_usb_log_restart(uint8 type, uint16 pc);
void pc_usb_log_fpga_heal(uint8 a, uint16 b);              //type 21 (enc_heal_hw.c)
void pc_usb_log_fpga_write(uint8 a, uint16 b);             //type 22 (fpga_control.c)
void pc_usb_log_fpga_capture(uint8 a, uint16 b);           //type 23 (cap_hw.c)
void pc_usb_log_init_state(void);                          //usb_device_init: controller state found (type 16)
void pc_usb_phy_pre(void);                                 //usb_device_init: PHY tuning bits before the writes
void pc_usb_log_post_init(void);                           //usb_device_init: state at the end (type 19)

void pc_usb_isr_setup(uint32 d0, uint32 d1);                //EP0 setup packet (the 8 bytes as two words)
void pc_usb_isr_bad_setup(uint32 count);                    //EP0 packet that was not 8 bytes

//1 when no response byte is left in the staging buffer or the endpoint FIFO (ENTER_FEL waits for this)
int  pc_transport_tx_drained(void);

#endif /* PC_TRANSPORT_H */
