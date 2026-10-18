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
void pc_usb_isr_link(void);                 //bus reset, suspend or disconnect
void pc_usb_isr_ep_cleared(uint8 ep);       //CLEAR_FEATURE(ENDPOINT_HALT) flushed this endpoint's FIFO (0x02 / 0x82)
void pc_usb_dtr(uint8 dtr);                 //SET_CONTROL_LINE_STATE
void pc_usb_isr_event(uint8 ev);            //PC_USB_EV_*: counted for GET_LINK_STATS (fw 0.2.5)
void pc_usb_isr_resume(void);               //RESUME: the configuration is kept, re-enable the EP2 OUT interrupt
void pc_usb_isr_configured(void);           //SET_CONFIGURATION set up EP2

#define PC_USB_EV_BUS_RESET   0
#define PC_USB_EV_SUSPEND     1
#define PC_USB_EV_RESUME      2
#define PC_USB_EV_DISCONNECT  3
#define PC_USB_EV_SETUP       4             //EP0 setup packets
#define PC_USB_EV_BAD_SETUP   5             //EP0 packets that were not 8 bytes (stalled)
#define PC_USB_EV_COUNT       6

//1 when no response byte is left in the staging buffer or the endpoint FIFO (ENTER_FEL waits for this)
int  pc_transport_tx_drained(void);

#endif /* PC_TRANSPORT_H */
