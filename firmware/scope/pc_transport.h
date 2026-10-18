//----------------------------------------------------------------------------------------------------------------------------------
// pc_transport.h - USB CDC byte transport for the PC protocol (docs/plans/firmware-usb-protocol.md section 6).
//
// ISR side (called from usb_device_CDC_irq_handler with EPIND already selected as noted) and the main-loop side
// (the transport part of pc_hal.h: pc_hal_rx_read, pc_hal_tx_submit, ...).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_TRANSPORT_H
#define PC_TRANSPORT_H

#include "types.h"

#define PC_RX_RING_SIZE   2048              //power of two

//Endpoint FIFO layout. The F1C100s USB FIFO RAM is 2048 bytes (F1C100s datasheet: "Configurable Data FIFO. The size
//is 2048 byte"), and the Endpoint 0 FIFO has a fixed size (64 bytes) and a fixed location (start address 0)
//(MUSBMHDRC programmer's guide, dynamic FIFO note). FIFOADD is in 8-byte units, FIFOSZ = 2^(n+3) bytes, DPB doubles
//it. 1.00o5's CDC layout put EP2 OUT on top of EP0 at 0, EP1 at byte 2048 and the EP2 TX FIFO at byte 4096, both
//outside the RAM (aliasing onto 0): control requests overwrote bulk data and IN packets overwrote waiting requests.
//  EP0      bytes    0 ..   63  fixed
//  EP2 OUT  bytes   64 ..  575  512 B, single buffered (requests are <= 512 B; the ISR empties it at once)
//  EP2 IN   bytes  576 .. 1599  2 x 512 B, double buffered (bulk IN throughput)
//  EP1 IN   bytes 1600 .. 1663  64 B single (8-byte notification endpoint, never loaded)
//PC_FIFO_MAP selects the layout (make PC_FIFO_MAP=n) so the bench can check whether request corruption follows the
//OUT FIFO's position (fw 0.1.3 census: ~4% E_BAD_CRC with map 0, none with OUT at 0 in 0.1.2).
#ifndef PC_FIFO_MAP
#define PC_FIFO_MAP 0
#endif

#if PC_FIFO_MAP == 1
//  EP0 0-63 | EP1 IN 64-127 | EP2 IN 512-1535 (DPB) | EP2 OUT 1536-2047: OUT as far from EP0 as possible
#define PC_EP2_RXFIFOAD   192
#define PC_EP2_TXFIFOAD   64
#define PC_EP1_TXFIFOAD   8
#else
//  map 0: the table above
#define PC_EP2_RXFIFOAD   8
#define PC_EP2_TXFIFOAD   72
#define PC_EP1_TXFIFOAD   200
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

//1 when no response byte is left in the staging buffer or the endpoint FIFO (ENTER_FEL waits for this)
int  pc_transport_tx_drained(void);

#endif /* PC_TRANSPORT_H */
