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
#include <string.h>
#include "usb_interface.h"
#include "pc_transport.h"
#include "pc_rx_ring.h"
#include "pc_protocol.h"
#include "pc_hal.h"
#include "interrupt.h"
#include "ccu_control.h"

//----------------------------------------------------------------------------------------------------------------------------------

#define PC_TX_STAGE_SIZE  ((PC_MAX_RESPONSE_FRAME + 3) & ~3u)

static uint8  pc_rx_ring[PC_RX_RING_SIZE] __attribute__((aligned(4)));
static uint8  pc_rx_bounce[PC_USB_PACKET] __attribute__((aligned(4)));   //one OUT packet, read in one go
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

//Link counters for GET_LINK_STATS (order = pc_hal_link_stats, docs/protocol.md section 14)
static volatile uint32 pc_ts[PC_TSTAT_COUNT];
static volatile uint32 pc_usb_ev[PC_USB_EV_COUNT];

//RX back-pressure diagnosis (bench 0.2.3: 307,837 stalls, 660 header CRC errors during an SD persist)
#define PC_RXD_ISR_STALL    0
#define PC_RXD_RELEASE      1
#define PC_RXD_RESTALL      2
#define PC_RXD_EMPTY        3
#define PC_RXD_COUNT_MAX    4
#define PC_RXD_OVERSIZE     5
static volatile uint32 pc_rxd[PC_RXDBG_COUNT];
static volatile uint8  pc_rx_in_release;       //drain called from the main-loop release

//Event log ring (IRQ context writers; the reader masks IRQs)
typedef struct
{
  uint32 ms;
  uint8  type;
  uint8  a;
  uint16 b;
} pc_evlog_entry;

static pc_evlog_entry  pc_evlog[PC_EVLOG_SIZE];
static volatile uint32 pc_evlog_n;              //events logged since boot

//The events before the first bus reset since the host last read the log, frozen there (fw 0.2.15): Windows retries
//its port reset, and anything may flood the ring afterwards, but the lead-up to the original fault survives
static pc_evlog_entry  pc_evsnap[PC_EVSNAP_SIZE];
static volatile uint32 pc_evsnap_n;
static volatile uint32 pc_evsnap_ms;
static volatile uint8  pc_evsnap_frozen;

static void pc_evlog_add(uint8 type, uint8 a, uint16 b)
{
  pc_evlog_entry *en = &pc_evlog[pc_evlog_n & (PC_EVLOG_SIZE - 1)];

  if((type == PC_EVT_BUS_RESET) && !pc_evsnap_frozen)
  {
    uint32 k = (pc_evlog_n < PC_EVSNAP_SIZE) ? pc_evlog_n : PC_EVSNAP_SIZE;
    uint32 i;

    for(i=0;i<k;i++)
      pc_evsnap[i] = pc_evlog[(pc_evlog_n - k + i) & (PC_EVLOG_SIZE - 1)];

    pc_evsnap_n      = k;
    pc_evsnap_ms     = pc_hal_ms();
    pc_evsnap_frozen = 1;
  }

  en->ms   = pc_hal_ms();
  en->type = type;
  en->a    = a;
  en->b    = b;
  pc_evlog_n++;
}
static volatile uint8  pc_ep2_configured;    //SET_CONFIGURATION set up EP2 since the last bus reset

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

static void pc_rx_fifo2_read(void *fifo, void *buffer, uint32_t length)
{
  usb_read_from_fifo(fifo, buffer, length);
}

static uint32 pc_rx_drain_fifo(void)
{
  uint32 packets = 0;

  //A further packet can become ready as soon as one is released
  while(*USBC_REG_RXCSR & USBC_BP_RXCSR_D_RX_PKT_READY)
  {
    uint32 count = *USBC_REG_RXCOUNT & 0x1FFF;
    int32  stored;

    if(count > pc_rxd[PC_RXD_COUNT_MAX])
      pc_rxd[PC_RXD_COUNT_MAX] = count;

    if(count > sizeof(pc_rx_bounce))
    {
      pc_rxd[PC_RXD_OVERSIZE]++;
      count = sizeof(pc_rx_bounce);
    }

    //One FIFO read per packet into the aligned bounce buffer, then a two-segment copy into the ring (pc_rx_ring.h:
    //the 0.1.2-0.1.5 split FIFO read at the ring end corrupted every request crossing a 2 KiB stream boundary)
    stored = pc_rx_ring_store_packet(pc_rx_ring, pc_rx_in, pc_rx_out, count, pc_rx_bounce, pc_rx_fifo2_read,
                                     (void *)USBC_REG_EPFIFO2);

    if(stored < 0)
    {
      //No room: leave RxPktRdy set so the controller NAKs, stop interrupts until the main loop made room
      *USBC_REG_INTRXE &= ~USBC_INTRX_FLAG_EP2;
      pc_rx_stalled = 1;
      pc_ts[PC_TS_RX_STALLS]++;
      pc_evlog_add(PC_EVT_RX_STALL, 0, (uint16)count);
      pc_rxd[pc_rx_in_release ? PC_RXD_RESTALL : PC_RXD_ISR_STALL]++;
      return packets;
    }

    pc_rx_in += count;
    pc_ts[PC_TS_RX_PACKETS]++;
    pc_ts[PC_TS_RX_BYTES] += count;
    packets++;

    //Release the packet and clear a possible overrun (same as 1.00o5)
    *USBC_REG_RXCSR &= ~(USBC_BP_RXCSR_D_RX_PKT_READY | USBC_BP_RXCSR_D_OVERRUN);
  }

  return packets;
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
  uint32 in, used, n;

  //Sweep: take a packet the EP2 RX interrupt did not deliver. 1.00o5's irq_handler clears the INTC pending register
  //by writing 0 after reading it, so a USB interrupt raised in between can be lost; the packet would then sit in
  //the FIFO (host request unanswered) until some other USB event. Every poll (1 ms tick included) checks the FIFO;
  //a packet still waiting 2 ms after it was first seen has missed its interrupt and is taken here (and counted).
  if(!pc_rx_stalled)
  {
    static uint8  seen;
    static uint32 seen_ms;
    static uint32 seen_pkts;                       //packet count when seen: the ISR may have taken it meanwhile
    uint32 cpsr  = pc_irq_save();
    uint8  epind = *USBC_REG_EPIND;

    *USBC_REG_EPIND = 2;

    if(*USBC_REG_RXCSR & USBC_BP_RXCSR_D_RX_PKT_READY)
    {
      if(!seen || (seen_pkts != pc_ts[PC_TS_RX_PACKETS]))
      {
        seen      = 1;
        seen_ms   = pc_hal_ms();
        seen_pkts = pc_ts[PC_TS_RX_PACKETS];
      }
      else if((pc_hal_ms() - seen_ms) >= 2)
      {
        pc_ts[PC_TS_RX_SWEPT] += pc_rx_drain_fifo();
        seen = 0;
      }
    }
    else
    {
      seen = 0;
    }

    *USBC_REG_EPIND = epind;
    pc_irq_restore(cpsr);
  }

  in   = pc_rx_in;
  used = in - pc_rx_out;
  n    = (used < max) ? used : max;

  pc_rx_ring_read(pc_rx_ring, pc_rx_out, dst, n);

  pc_rx_out += n;

  //Room for a full packet again: take the waiting packet(s) and unmask the endpoint
  if(pc_rx_stalled && ((PC_RX_RING_SIZE - (pc_rx_in - pc_rx_out)) >= PC_USB_PACKET))
  {
    uint32 cpsr  = pc_irq_save();
    uint8  epind = *USBC_REG_EPIND;

    *USBC_REG_EPIND = 2;
    pc_rx_stalled   = 0;
    pc_rxd[PC_RXD_RELEASE]++;
    pc_evlog_add(PC_EVT_RX_RELEASE, 0, 0);

    if(!(*USBC_REG_RXCSR & USBC_BP_RXCSR_D_RX_PKT_READY))
      pc_rxd[PC_RXD_EMPTY]++;

    pc_rx_in_release = 1;
    pc_rx_drain_fifo();
    pc_rx_in_release = 0;

    if(!pc_rx_stalled)
      *USBC_REG_INTRXE |= USBC_INTRX_FLAG_EP2;

    *USBC_REG_EPIND = epind;
    pc_irq_restore(cpsr);
  }

  return n;
}

//----------------------------------------------------------------------------------------------------------------------------------
// TX. EPIND == 2, IRQs off (ISR or masked).

static uint32 pc_tx_fill(void)
{
  uint32 k, loaded = 0;

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
      pc_ts[PC_TS_TX_ZLPS]++;
    }

    loaded++;
    pc_ts[PC_TS_TX_PACKETS]++;

    //Clear a possible under run first, then hand the packet to the controller (same order as 1.00o5)
    *USBC_REG_TXCSR &= ~USBC_BP_TXCSR_D_UNDER_RUN;
    *USBC_REG_TXCSR |= USBC_BP_TXCSR_D_TX_READY;

    //Everything is in the FIFO: the staging buffer is free for the next response
    if((pc_tx_left == 0) && !pc_tx_zlp)
      pc_tx_active = 0;
  }

  return loaded;
}

static void pc_tx_flush(void)
{
  uint8 epind = *USBC_REG_EPIND;
  uint32 k;

  if(pc_tx_active)
  {
    pc_ts[PC_TS_TX_FLUSHES]++;
    pc_evlog_add(PC_EVT_TX_FLUSH, 0, (uint16)pc_tx_left);
  }

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
    pc_ts[PC_TS_TX_KICKED] += pc_tx_fill();
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

  pc_evlog_add(PC_EVT_TX_ABORT, 0, (uint16)pc_tx_left);
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
  uint8 epind = *USBC_REG_EPIND;

  //Drop what we hold. The 1.00o5 reset handler flushes with EPIND = 0, i.e. not EP2: flush EP2 here, so a frame left
  //over from the previous host session can never be sent to the next one.
  pc_tx_flush();

  *USBC_REG_EPIND = 2;
  if(*USBC_REG_RXCSR & USBC_BP_RXCSR_D_RX_PKT_READY)
    *USBC_REG_RXCSR |= USBC_BP_RXCSR_D_FLUSH_FIFO;
  *USBC_REG_EPIND = epind;                         //SET_CONFIGURATION re-enables the EP2 RX interrupt

  //A packet held back by a full ring was just flushed: the EP2 OUT interrupt must not stay masked. A RESUME keeps the
  //configuration (no SET_CONFIGURATION follows), so nothing else would unmask it (0.1.x-0.2.4 relied on the poll sweep)
  if(pc_rx_stalled)
    *USBC_REG_INTRXE |= USBC_INTRX_FLAG_EP2;

  pc_rx_out    = pc_rx_in;
  pc_rx_stalled = 0;
  pc_dtr       = 0;
  pc_ts[PC_TS_LINK_RESETS]++;
  pc_link_events |= PC_LINK_RESET;
}

void pc_usb_isr_event(uint8 ev)
{
  static const uint8 type[PC_USB_EV_COUNT] = { PC_EVT_BUS_RESET, PC_EVT_SUSPEND, PC_EVT_RESUME, PC_EVT_DISCONNECT,
                                               PC_EVT_SETUP, PC_EVT_BAD_SETUP };

  if(ev < PC_USB_EV_COUNT)
  {
    pc_usb_ev[ev]++;

    //Setups and bad setups are logged with their details by their own hooks
    if(ev < PC_USB_EV_SETUP)
      pc_evlog_add(type[ev], 0, 0);
  }

  if(ev == PC_USB_EV_BUS_RESET)
    pc_ep2_configured = 0;
}

void pc_usb_isr_resume(void)
{
  pc_usb_isr_event(PC_USB_EV_RESUME);

  if(pc_ep2_configured)
    *USBC_REG_INTRXE |= USBC_INTRX_FLAG_EP2;
}

void pc_hal_usb_events(uint32_t *v)
{
  uint32 i;

  for(i=0;i<PC_USB_EV_COUNT;i++)
    v[i] = pc_usb_ev[i];
}

void pc_hal_rx_debug(uint32_t *v)
{
  uint32 i;

  for(i=0;i<PC_RXDBG_COUNT;i++)
    v[i] = pc_rxd[i];
}

void pc_usb_log_restart(uint8 type, uint16 pc)
{
  uint32 cpsr = pc_irq_save();

  pc_evlog_add(PC_EVT_RESTART, type, pc);
  pc_irq_restore(cpsr);
}

//USB controller state as usb_device_init finds it (fw 0.2.22): five type 16 entries, a = field.
//  0: b = POWER << 8 | FADDR            1: b = INTUSBE << 8 | CCU flags (bit 0 OTG reset released, bit 1 OTG bus clock
//  2: b = EP2 TXCSR                        on, bit 2 PHY reset released, bit 3 PHY clock on)
//  3: b = EP2 RXCSR                     4: b = ISCR bits 4-19 (change detects, VBUS source, force ID/VBUS, pull-ups)
//Controller registers read as 0 unless the OTG clock is on and its reset released.
void pc_usb_log_init_state(void)
{
  uint32 cpsr = pc_irq_save();
  uint32 phy  = *CCU_USBPHY_CFG;
  uint8  flags = (uint8)(((*CCU_BUS_SOFT_RST0 & CCU_BSRR1_USBOTG_EN) ? 0x01 : 0) |
                         ((*CCU_BUS_CLK_GATE0 & CCU_BCGR1_USBOTG_EN) ? 0x02 : 0) |
                         ((phy & CCU_USBPHY_CFG_RST) ? 0x04 : 0) | ((phy & CCU_USBPHY_CFG_GAT) ? 0x08 : 0));
  uint16 power = 0, intusbe = 0, txcsr = 0, rxcsr = 0, iscr = 0;
  uint8  epind;

  if((flags & 0x03) == 0x03)
  {
    power   = (uint16)((*USBC_REG_PCTL << 8) | (*USBC_REG_FADDR & 0x7F));
    intusbe = *USBC_REG_INTUSBE;
    iscr    = (uint16)((*USBC_REG_ISCR >> 4) & 0xFFFF);
    epind   = *USBC_REG_EPIND;
    *USBC_REG_EPIND = 2;
    txcsr   = *USBC_REG_TXCSR;
    rxcsr   = *USBC_REG_RXCSR;
    *USBC_REG_EPIND = epind;
  }

  pc_evlog_add(PC_EVT_USB_INIT, 0, power);
  pc_evlog_add(PC_EVT_USB_INIT, 1, (uint16)((intusbe << 8) | flags));
  pc_evlog_add(PC_EVT_USB_INIT, 2, txcsr);
  pc_evlog_add(PC_EVT_USB_INIT, 3, rxcsr);
  pc_evlog_add(PC_EVT_USB_INIT, 4, iscr);
  pc_irq_restore(cpsr);
}

void pc_usb_isr_configured(void)
{
  pc_ep2_configured = 1;
  pc_evlog_add(PC_EVT_CONFIGURED, 0, 0);
}

void pc_usb_isr_setup(uint32 d0, uint32 d1)
{
  (void)d1;
  pc_usb_ev[PC_USB_EV_SETUP]++;
  pc_evlog_add(PC_EVT_SETUP, (uint8)(d0 >> 8), (uint16)(((d0 & 0xFF) << 8) | ((d0 >> 16) & 0xFF)));
}

void pc_usb_isr_bad_setup(uint32 count)
{
  pc_usb_ev[PC_USB_EV_BAD_SETUP]++;
  pc_evlog_add(PC_EVT_BAD_SETUP, 0, (uint16)count);
}

uint32_t pc_hal_event_log(uint8_t *dst, uint32_t max, uint32_t *total)
{
  uint32 cpsr = pc_irq_save();
  uint32 n = pc_evlog_n, k, i, first;

  k = (n < PC_EVLOG_SIZE) ? n : PC_EVLOG_SIZE;

  if(k > max)
    k = max;

  first = n - k;

  for(i=0;i<k;i++)
  {
    const pc_evlog_entry *en = &pc_evlog[(first + i) & (PC_EVLOG_SIZE - 1)];
    uint8_t *p = dst + 8 * i;

    p[0] = (uint8_t)en->ms; p[1] = (uint8_t)(en->ms >> 8); p[2] = (uint8_t)(en->ms >> 16); p[3] = (uint8_t)(en->ms >> 24);
    p[4] = en->type;
    p[5] = en->a;
    p[6] = (uint8_t)en->b; p[7] = (uint8_t)(en->b >> 8);
  }

  *total = n;
  pc_irq_restore(cpsr);
  return k;
}

//The reset snapshot (entries as above), *reset_ms = when it was taken; returns its size, -1 when none is frozen.
//Only rearm re-arms it (fw 0.2.18: a plain read kept nothing frozen if the host read the log at every session open)
int32_t pc_hal_event_snapshot(uint8_t *dst, uint32_t *reset_ms, int rearm)
{
  uint32 cpsr = pc_irq_save();
  int32  k = pc_evsnap_frozen ? (int32)pc_evsnap_n : -1;
  int32  i;

  for(i=0;i<k;i++)
  {
    const pc_evlog_entry *en = &pc_evsnap[i];
    uint8_t *p = dst + 8 * i;

    p[0] = (uint8_t)en->ms; p[1] = (uint8_t)(en->ms >> 8); p[2] = (uint8_t)(en->ms >> 16); p[3] = (uint8_t)(en->ms >> 24);
    p[4] = en->type;
    p[5] = en->a;
    p[6] = (uint8_t)en->b; p[7] = (uint8_t)(en->b >> 8);
  }

  *reset_ms = pc_evsnap_ms;

  if(rearm)
    pc_evsnap_frozen = 0;

  pc_irq_restore(cpsr);
  return k;
}

void pc_usb_isr_ep_cleared(uint8 ep)
{
  pc_evlog_add(PC_EVT_EP_CLEARED, ep, 0);

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

  if(dtr != pc_dtr)
    pc_evlog_add(PC_EVT_DTR, dtr, 0);

  if(pc_dtr && !dtr)
  {
    //Host closed the port: a pending response is discarded (P-3.2)
    pc_tx_flush();
    pc_link_events |= PC_LINK_DTR_DOWN;
    pc_ts[PC_TS_DTR_DOWN]++;
  }
  else if(!pc_dtr && dtr)
  {
    //A new host connection must not receive the tail of a frame meant for the previous one
    pc_tx_flush();
    pc_link_events |= PC_LINK_DTR_UP;
    pc_ts[PC_TS_DTR_UP]++;
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

void pc_hal_link_stats(uint32_t *v)
{
  uint32 i;

  for(i=0;i<PC_TSTAT_COUNT;i++)
    v[i] = pc_ts[i];
}

//----------------------------------------------------------------------------------------------------------------------------------
