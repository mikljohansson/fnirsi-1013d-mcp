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
//is 2048 byte"). FIFOADD is in 8-byte units, FIFOSZ = 2^(n+3) bytes, DPB doubles it. 1.00o5's CDC layout put EP1 at
//byte 2048 and the EP2 TX FIFO at byte 4096, both outside the RAM (they alias onto the EP2 RX FIFO at 0).
//  EP2 OUT  bytes    0 ..  511  512 B, single buffered (requests are <= 512 B; the ISR empties it at once)
//  EP2 IN   bytes  512 .. 1535  2 x 512 B, double buffered (bulk IN throughput)
//  EP1 IN   bytes 1536 .. 1599  64 B single (8-byte notification endpoint, never loaded)
#define PC_EP2_RXFIFOAD   0
#define PC_EP2_RXFIFOSZ   6
#define PC_EP2_TXFIFOAD   64
#define PC_EP2_TXFIFOSZ   (USBC_BP_TXFIFOSZ_DPB | 6)
#define PC_EP1_TXFIFOAD   192
#define PC_EP1_TXFIFOSZ   3

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
