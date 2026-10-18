//----------------------------------------------------------------------------------------------------------------------------------
// pc_transport.c - USB CDC transport for the PC protocol (plan section 6, protocol section 3.2).
//
// RX: EP2 OUT packets go from the FIFO straight into a 2 KiB ring in the ISR. When a packet does not fit, the ISR leaves
//     RxPktRdy set and masks the EP2 RX interrupt: the controller NAKs further OUT tokens (back-pressure, no byte is
//     ever dropped). The main loop re-reads the FIFO once it has drained the ring.
// TX: a response is staged whole in one buffer; pc_hal_tx_submit loads the first packet(s), the EP2 TX interrupt loads
//     the rest, 512 bytes per packet, and a zero length packet ends frames that are an exact multiple of 512 bytes.
//
// Main-loop code that touches indexed endpoint registers runs with IRQs off and restores EPIND.
//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "usb_interface.h"
#include "pc_transport.h"
#include "pc_protocol.h"
#include "pc_hal.h"

//----------------------------------------------------------------------------------------------------------------------------------

#define PC_TX_STAGE_SIZE  ((PC_MAX_RESPONSE_FRAME + 3) & ~3u)

static uint8  pc_rx_ring[PC_RX_RING_SIZE] __attribute__((aligned(4)));
static volatile uint32 pc_rx_in;            //free running, written by the ISR
static volatile uint32 pc_rx_out;           //free running, written by the main loop
static volatile uint8  pc_rx_stalled;       //a packet is waiting in the FIFO, EP2 RX interrupt masked

static uint8  pc_tx_stage[PC_TX_STAGE_SIZE] __attribute__((aligned(4)));
static const uint8 * volatile pc_tx_ptr;
static volatile uint32 pc_tx_left;
static volatile uint8  pc_tx_zlp;
static volatile uint8  pc_tx_active;
static volatile uint32 pc_tx_last_ms;      //time of the last packet handed to the controller

static volatile uint32 pc_link_events;
static volatile uint8  pc_dtr;

//----------------------------------------------------------------------------------------------------------------------------------
// IRQ masking (CPSR I bit), nests safely

static inline uint32 pc_irq_save(void)
{
  uint32 cpsr;

  __asm__ __volatile__ ("mrs %0, cpsr" : "=r" (cpsr) : : "memory");
  __asm__ __volatile__ ("msr cpsr_c, %0" : : "r" (cpsr | 0x80) : "memory");
  return cpsr;
}

static inline void pc_irq_restore(uint32 cpsr)
{
  __asm__ __volatile__ ("msr cpsr_c, %0" : : "r" (cpsr) : "memory");
}

//----------------------------------------------------------------------------------------------------------------------------------
// RX. EPIND == 2, IRQs off (ISR or masked).

static void pc_rx_drain_fifo(void)
{
  //Double buffered FIFO: a second packet can become ready as soon as the first is released
  while(*USBC_REG_RXCSR & USBC_BP_RXCSR_D_RX_PKT_READY)
  {
    uint32 count = *USBC_REG_RXCOUNT & 0x1FFF;
    uint32 used  = pc_rx_in - pc_rx_out;
    uint32 pos, first;

    if(PC_RX_RING_SIZE - used < count)
    {
      //No room: leave RxPktRdy set so the controller NAKs, stop interrupts until the main loop made room
      *USBC_REG_INTRXE &= ~USBC_INTRX_FLAG_EP2;
      pc_rx_stalled = 1;
      return;
    }

    pos   = pc_rx_in & (PC_RX_RING_SIZE - 1);
    first = PC_RX_RING_SIZE - pos;

    if(first > count)
      first = count;

    usb_read_from_fifo((void *)USBC_REG_EPFIFO2, &pc_rx_ring[pos], first);

    if(count > first)
      usb_read_from_fifo((void *)USBC_REG_EPFIFO2, &pc_rx_ring[0], count - first);

    pc_rx_in += count;

    //Release the packet and clear a possible overrun (same as 1.00o5)
    *USBC_REG_RXCSR &= ~(USBC_BP_RXCSR_D_RX_PKT_READY | USBC_BP_RXCSR_D_OVERRUN);
  }
}

void pc_usb_isr_rx(void)
{
  if(*USBC_REG_RXCSR & USBC_BP_RXCSR_D_SENT_STALL)
    *USBC_REG_RXCSR &= ~(USBC_BP_RXCSR_D_SENT_STALL | USBC_BP_RXCSR_D_SEND_STALL);

  //While stalled the main loop owns the FIFO read
  if(!pc_rx_stalled)
    pc_rx_drain_fifo();
}

uint32_t pc_hal_rx_read(uint8_t *dst, uint32_t max)
{
  uint32 in   = pc_rx_in;
  uint32 used = in - pc_rx_out;
  uint32 n    = (used < max) ? used : max;
  uint32 i;

  for(i=0;i<n;i++)
    dst[i] = pc_rx_ring[(pc_rx_out + i) & (PC_RX_RING_SIZE - 1)];

  pc_rx_out += n;

  //Room for a full packet again: take the waiting packet(s) and unmask the endpoint
  if(pc_rx_stalled && ((PC_RX_RING_SIZE - (pc_rx_in - pc_rx_out)) >= PC_USB_PACKET))
  {
    uint32 cpsr  = pc_irq_save();
    uint8  epind = *USBC_REG_EPIND;

    *USBC_REG_EPIND = 2;
    pc_rx_stalled   = 0;
    pc_rx_drain_fifo();

    if(!pc_rx_stalled)
      *USBC_REG_INTRXE |= USBC_INTRX_FLAG_EP2;

    *USBC_REG_EPIND = epind;
    pc_irq_restore(cpsr);
  }

  return n;
}

//----------------------------------------------------------------------------------------------------------------------------------
// TX. EPIND == 2, IRQs off (ISR or masked).

static void pc_tx_fill(void)
{
  uint32 k;

  //Up to two packets: with the double buffered FIFO TxPktRdy clears at once while the other half is free
  for(k=0;(k<2) && pc_tx_active;k++)
  {
    if(*USBC_REG_TXCSR & USBC_BP_TXCSR_D_TX_READY)
      break;

    if(pc_tx_left)
    {
      uint32 n = (pc_tx_left > PC_USB_PACKET) ? PC_USB_PACKET : pc_tx_left;

      //PIO FIFO writes, 32 bit when aligned (staging buffer and 512-byte steps keep it aligned)   BENCH: T2
      usb_write_to_fifo((void *)USBC_REG_EPFIFO2, (void *)pc_tx_ptr, n);
      pc_tx_ptr  += n;
      pc_tx_left -= n;
      pc_tx_last_ms = pc_hal_ms();
    }
    else
    {
      //Zero length packet after a frame of n x 512 bytes                              BENCH: T1
      pc_tx_zlp = 0;
    }

    //Clear a possible under run first, then hand the packet to the controller (same order as 1.00o5)
    *USBC_REG_TXCSR &= ~USBC_BP_TXCSR_D_UNDER_RUN;
    *USBC_REG_TXCSR |= USBC_BP_TXCSR_D_TX_READY;

    //Everything is in the FIFO: the staging buffer is free for the next response
    if((pc_tx_left == 0) && !pc_tx_zlp)
      pc_tx_active = 0;
  }
}

static void pc_tx_flush(void)
{
  uint8 epind = *USBC_REG_EPIND;
  uint32 k;

  pc_tx_active = 0;
  pc_tx_left   = 0;
  pc_tx_zlp    = 0;

  *USBC_REG_EPIND = 2;

  //Both halves of the double buffered FIFO
  for(k=0;k<2;k++)
    if(*USBC_REG_TXCSR & USBC_BP_TXCSR_D_FIFO_NOT_EMPTY)
      *USBC_REG_TXCSR |= USBC_BP_TXCSR_D_FLUSH_FIFO;

  *USBC_REG_EPIND = epind;
}

void pc_usb_isr_tx(void)
{
  if(*USBC_REG_TXCSR & USBC_BP_TXCSR_D_SENT_STALL)
    *USBC_REG_TXCSR &= ~(USBC_BP_TXCSR_D_SENT_STALL | USBC_BP_TXCSR_D_SEND_STALL);

  if(pc_tx_active)
    pc_tx_fill();
}

uint8_t *pc_hal_tx_buffer(uint32_t *capacity)
{
  *capacity = sizeof(pc_tx_stage);
  return pc_tx_stage;
}

int pc_hal_tx_busy(void)
{
  return pc_tx_active;
}

void pc_hal_tx_submit(uint32_t len)
{
  uint32 cpsr;
  uint8  epind;

  if((len == 0) || (len > sizeof(pc_tx_stage)))
    return;

  cpsr  = pc_irq_save();
  epind = *USBC_REG_EPIND;

  pc_tx_ptr    = pc_tx_stage;
  pc_tx_left   = len;
  pc_tx_zlp    = (uint8)pc_frame_needs_zlp(len, PC_USB_PACKET);
  pc_tx_active = 1;
  pc_tx_last_ms = pc_hal_ms();

  *USBC_REG_EPIND = 2;
  pc_tx_fill();
  *USBC_REG_EPIND = epind;

  pc_irq_restore(cpsr);
}

//Main-loop kick of the pump. The EP2 TX interrupt normally does all the work; this makes progress independent of a
//missed or late interrupt (bench 2026-10-18: frames above ~5 KiB stalled). Safe at any time: fill only loads a packet
//when the controller has room.
int pc_hal_tx_pump(void)
{
  if(pc_tx_active)
  {
    uint32 cpsr  = pc_irq_save();
    uint8  epind = *USBC_REG_EPIND;

    *USBC_REG_EPIND = 2;
    pc_tx_fill();
    *USBC_REG_EPIND = epind;
    pc_irq_restore(cpsr);
  }

  return pc_tx_active;
}

uint32_t pc_hal_tx_idle_ms(void)
{
  return pc_tx_active ? (pc_hal_ms() - pc_tx_last_ms) : 0;
}

void pc_hal_tx_abort(void)
{
  uint32 cpsr = pc_irq_save();

  pc_tx_flush();
  pc_irq_restore(cpsr);
}

int pc_transport_tx_drained(void)
{
  uint32 cpsr  = pc_irq_save();
  uint8  epind = *USBC_REG_EPIND;
  int    empty;

  *USBC_REG_EPIND = 2;
  empty = !pc_tx_active && !(*USBC_REG_TXCSR & USBC_BP_TXCSR_D_FIFO_NOT_EMPTY);
  *USBC_REG_EPIND = epind;
  pc_irq_restore(cpsr);

  return empty;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Link state

void pc_usb_isr_link(void)
{
  //The bus reset handler already flushed the endpoint FIFOs; drop what we hold
  pc_tx_active = 0;
  pc_tx_left   = 0;
  pc_tx_zlp    = 0;
  pc_rx_out    = pc_rx_in;
  pc_rx_stalled = 0;
  pc_dtr       = 0;
  pc_link_events |= PC_LINK_RESET;
}

void pc_usb_isr_ep_cleared(uint8 ep)
{
  if(ep & 0x80)
  {
    //IN FIFO flushed: the frame in flight is incomplete anyway, the host resynchronises
    pc_tx_active = 0;
    pc_tx_left   = 0;
    pc_tx_zlp    = 0;
  }
  else if(pc_rx_stalled)
  {
    //The waiting OUT packet was flushed
    pc_rx_stalled = 0;
    *USBC_REG_INTRXE |= USBC_INTRX_FLAG_EP2;
  }
}

void pc_usb_dtr(uint8 dtr)
{
  dtr = dtr ? 1 : 0;

  if(pc_dtr && !dtr)
  {
    //Host closed the port: a pending response is discarded (P-3.2)
    pc_tx_flush();
    pc_link_events |= PC_LINK_DTR_DOWN;
  }
  else if(!pc_dtr && dtr)
  {
    //A new host connection must not receive the tail of a frame meant for the previous one
    pc_tx_flush();
    pc_link_events |= PC_LINK_DTR_UP;
  }

  pc_dtr = dtr;
}

void pc_transport_reset(void)
{
  uint32 cpsr = pc_irq_save();

  pc_tx_active  = 0;
  pc_tx_left    = 0;
  pc_tx_zlp     = 0;
  pc_rx_in      = 0;
  pc_rx_out     = 0;
  pc_rx_stalled = 0;
  pc_dtr        = 0;
  pc_link_events |= PC_LINK_RESET;

  pc_irq_restore(cpsr);
}

uint32_t pc_hal_link_take_events(void)
{
  uint32 cpsr = pc_irq_save();
  uint32 ev   = pc_link_events;

  pc_link_events = 0;
  pc_irq_restore(cpsr);
  return ev;
}

int pc_hal_dtr(void)
{
  return pc_dtr;
}

//----------------------------------------------------------------------------------------------------------------------------------
