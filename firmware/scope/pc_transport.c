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
#include "pc_usbw.h"

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

//USB suspend state (fw 0.2.44). A suspend is a state, not a link reset: the host keeps the configuration and the line
//state over a suspend / resume, and a port reset (which the MUSB reports as suspend first: the SE0 looks idle) still
//resets the link when its RESET interrupt arrives. fw 0.2.49: the state lives in the suspend watch (pc_usbw.h), which
//also infers resumes the core does not report and reconnects a device the host abandoned. Written only with IRQs off.
static pc_usbw pc_uw;

extern uint32 usb_connect;                  //usb_interface.c (1.00o5): set by RESET / RESUME, cleared by SUSPEND
static void pc_usb_inferred(uint8 cause);

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

//Bus-level events only (fw 0.2.24, PC_EVBUS_TYPES)
static pc_evlog_entry  pc_evbus[PC_EVBUS_SIZE];
static volatile uint32 pc_evbus_n;

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

  if((type < 32) && (PC_EVBUS_TYPES & (1u << type)))
    pc_evbus[pc_evbus_n++ & (PC_EVBUS_SIZE - 1)] = *en;
}
static volatile uint8  pc_ep2_configured;    //SET_CONFIGURATION set up EP2 since the last bus reset
static volatile uint32 pc_cfg_ms;            //fw 0.2.52: time of the last SET_CONFIGURATION (GET_HEALTH 12)
static volatile uint8  pc_cfg_seen;

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
  //fw 0.2.49: an OUT packet while marked suspended: the resume was not reported
  if(pc_usbw_activity(&pc_uw, PC_USBW_CAUSE_EP2))
    pc_usb_inferred(PC_USBW_CAUSE_EP2);

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
  uint8  epind = *USBC_REG_EPIND;
  uint16 txcsr;

  //fw 0.2.22: EP2 IN state when the session gives up (2 s without progress). UnderRun is cleared at every packet
  //load, so TxPktRdy set = a packet waits and the host takes no IN data; TxPktRdy clear + UnderRun = the host polls
  //and the device has nothing loaded. Device-mode TXCSR does not expose the data toggle. INTTX is not read (that
  //would clear it).
  *USBC_REG_EPIND = 2;
  txcsr = *USBC_REG_TXCSR;
  *USBC_REG_EPIND = epind;

  pc_evlog_add(PC_EVT_TX_ABORT, 0, (uint16)pc_tx_left);
  pc_evlog_add(PC_EVT_TX_ABORT_CSR, *USBC_REG_PCTL, txcsr);
  pc_evlog_add(PC_EVT_TX_ABORT_FRM, (uint8)(((*USBC_REG_INTTXE & USBC_INTTX_FLAG_EP2) ? 0x01 : 0) |
                                            (pc_tx_zlp ? 0x02 : 0)), (uint16)(*USBC_REG_FRNUM & 0x07FF));
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

//ms since the last suspend, at most 65535
static uint16 pc_usb_susp_age(void)
{
  uint32 d = pc_hal_ms() - pc_uw.susp_ms;

  return (uint16)((d > 65535) ? 65535 : d);
}

//Types 1 and 4 (fw 0.2.44): a = 1 when the device was suspended, b = ms since that suspend (65535 = longer). A port
//reset shows a few ms (the SE0 detected as idle first); a reset after a real host suspend shows the suspend's length.
void pc_usb_isr_event(uint8 ev)
{
  static const uint8 type[PC_USB_EV_COUNT] = { PC_EVT_BUS_RESET, PC_EVT_SUSPEND, PC_EVT_RESUME, PC_EVT_DISCONNECT,
                                               PC_EVT_SETUP, PC_EVT_BAD_SETUP };

  if(ev < PC_USB_EV_COUNT)
  {
    pc_usb_ev[ev]++;

    if((ev == PC_USB_EV_BUS_RESET) || (ev == PC_USB_EV_DISCONNECT))
    {
      pc_evlog_add(type[ev], pc_uw.susp, pc_uw.susp ? pc_usb_susp_age() : 0);
      pc_usbw_reset(&pc_uw);
    }
  }

  if(ev == PC_USB_EV_BUS_RESET)
    pc_ep2_configured = 0;
}

//Type 2 (fw 0.2.44): a = POWER, b = frame number at the suspend
void pc_usb_isr_suspend(void)
{
  uint16 frame = (uint16)(*USBC_REG_FRNUM & 0x07FF);

  pc_usb_ev[PC_USB_EV_SUSPEND]++;
  pc_evlog_add(PC_EVT_SUSPEND, (uint8)*USBC_REG_PCTL, frame);
  pc_usbw_suspend(&pc_uw, pc_hal_ms(), frame);
}

//The configuration is kept over a suspend: no SET_CONFIGURATION follows to unmask EP2 OUT
static void pc_usb_resumed(void)
{
  if(pc_ep2_configured)
    *USBC_REG_INTRXE |= USBC_INTRX_FLAG_EP2;
}

//Type 3 (fw 0.2.44): a = POWER, b = ms suspended. Counter 37 counts RESUME interrupts only.
void pc_usb_isr_resume(void)
{
  pc_usb_ev[PC_USB_EV_RESUME]++;
  pc_evlog_add(PC_EVT_RESUME, (uint8)(*USBC_REG_PCTL & 0x7F), pc_uw.susp ? pc_usb_susp_age() : 0);
  pc_usbw_resume(&pc_uw);
  pc_usb_resumed();
}

//Inferred resume (fw 0.2.49, counter 60): type 3 with a = 0x80 | cause << 5 | POWER bits 4-0 (bit 4 high-speed mode,
//bit 1 the core's suspend mode), b = ms suspended. Acts like the RESUME interrupt: the session is live again.
static void pc_usb_inferred(uint8 cause)
{
  pc_evlog_add(PC_EVT_RESUME, (uint8)(0x80 | (cause << 5) | (*USBC_REG_PCTL & 0x1F)), pc_usb_susp_age());
  usb_connect = 1;
  pc_usb_resumed();
}

//Timer interrupt, every ms (fw 0.2.49; 0.2.44 checked from the main loop every 20 ms and only logged). Runs the suspend
//watch (pc_usbw.h): an inferred resume when the frame number moves while suspended, a soft disconnect (SoftConn off,
//link reset, type 20) when the host seems to have abandoned the device, and the reconnect PC_USBW_OFF_MS later. IRQs
//are off here (no nesting); pc_usb_isr_link saves and restores EPIND itself.
void pc_usb_watch_tick(void)
{
  uint16 frame;
  uint32 age, lvl;
  int    act;

  if(!pc_uw.susp && !pc_uw.off)
    return;

  //USB switched off by other code (MSC switch, ENTER_FEL): leave it alone
  if(!pc_uw.off && !(*USBC_REG_PCTL & USBC_BP_POWER_D_SOFT_CONNECT))
    return;

  frame = (uint16)(*USBC_REG_FRNUM & 0x07FF);
  act   = pc_usbw_tick(&pc_uw, pc_hal_ms(), frame);

  if(act == PC_USBW_RESUMED)
    pc_usb_inferred(PC_USBW_CAUSE_FRAME);
  else if(act == PC_USBW_DISCONNECT)
  {
    //Type 20: a = reason | back-off level of that rule after this reconnect (max 3) << 2 | strikes (max 15) << 4,
    //b = s suspended
    age = (pc_hal_ms() - pc_uw.susp_ms) / 1000;
    lvl = (pc_uw.why == PC_USBW_WHY_STORM) ? pc_uw.level : pc_uw.long_level;
    pc_evlog_add(PC_EVT_SOFT_DISCONNECT,
                 (uint8)(pc_uw.why | (((lvl > 3) ? 3 : lvl) << 2) | (((pc_uw.strikes > 15) ? 15 : pc_uw.strikes) << 4)),
                 (uint16)((age > 65535) ? 65535 : age));
    *USBC_REG_PCTL &= ~USBC_BP_POWER_D_SOFT_CONNECT;
    usb_connect = 0;
    pc_usb_isr_link();
  }
  else if(act == PC_USBW_CONNECT)
    *USBC_REG_PCTL |= USBC_BP_POWER_D_SOFT_CONNECT;
}

//USB link state now (fw 0.2.52, GET_HEALTH 4..15, protocol row 55). VBUS is forced valid (ISCR), so a pulled cable is
//not visible: after a host enumerated the device it reads as a suspend.
void pc_hal_usb_state(pc_hw_usb *u)
{
  uint32 cpsr = pc_irq_save();
  uint8  power = *USBC_REG_PCTL;

  u->power = power;
  u->faddr = *USBC_REG_FADDR;
  u->flags = (uint8)(((power & USBC_BP_POWER_D_HIGH_SPEED_FLAG) ? PC_USBF_HIGH_SPEED : 0) |
                     (pc_ep2_configured ? PC_USBF_CONFIGURED : 0) | (pc_uw.susp ? PC_USBF_SUSPENDED : 0) |
                     (pc_dtr ? PC_USBF_DTR : 0) | ((power & USBC_BP_POWER_D_SOFT_CONNECT) ? PC_USBF_SOFTCONN : 0));
  u->suspended_ms    = pc_uw.susp ? (pc_hal_ms() - pc_uw.susp_ms) : 0;
  u->since_config_ms = pc_cfg_seen ? (pc_hal_ms() - pc_cfg_ms) : 0xFFFFFFFFu;

  if(pc_uw.off)
    u->state = PC_USB_RECONNECTING;
  else if(!(power & USBC_BP_POWER_D_SOFT_CONNECT))
    u->state = PC_USB_OFF;
  else if(!pc_ep2_configured)
    u->state = PC_USB_UNCONFIGURED;
  else
    u->state = pc_uw.susp ? PC_USB_SUSPENDED : PC_USB_CONFIGURED;

  pc_irq_restore(cpsr);
}

void pc_hal_usb_watch(uint32_t *v)
{
  v[0] = pc_uw.inferred;
  v[1] = pc_uw.reconnects;
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

void pc_usb_log_fpga_heal(uint8 a, uint16 b)
{
  uint32 cpsr = pc_irq_save();

  pc_evlog_add(PC_EVT_FPGA_HEAL, a, b);
  pc_irq_restore(cpsr);
}

void pc_usb_log_fpga_capture(uint8 a, uint16 b)
{
  uint32 cpsr = pc_irq_save();

  pc_evlog_add(PC_EVT_FPGA_CAPTURE, a, b);
  pc_irq_restore(cpsr);
}

void pc_usb_log_fpga_write(uint8 a, uint16 b)
{
  uint32 cpsr = pc_irq_save();

  pc_evlog_add(PC_EVT_FPGA_WRITE, a, b);
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
  pc_cfg_ms   = pc_hal_ms();
  pc_cfg_seen = 1;
  pc_evlog_add(PC_EVT_CONFIGURED, 0, 0);
  pc_usbw_configured(&pc_uw);
}

void pc_usb_isr_setup(uint32 d0, uint32 d1)
{
  static uint32 last_key, last_ms;
  uint32 key = d0 & 0x00FFFFFF, now = pc_hal_ms();

  (void)d1;
  pc_usb_ev[PC_USB_EV_SETUP]++;

  //fw 0.2.49: a setup while marked suspended is bus activity: the resume was not reported
  if(pc_usbw_activity(&pc_uw, PC_USBW_CAUSE_EP0))
    pc_usb_inferred(PC_USBW_CAUSE_EP0);

  //fw 0.2.24: pyserial repeats GET_LINE_CODING about five times per open; counter 39 still counts every setup
  if(pc_evlog_n && (key == last_key) && ((now - last_ms) < PC_SETUP_REPEAT_MS))
    return;

  last_key = key;
  last_ms  = now;
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
//PHY register read through PHYCTL (0x01C13404): address in bits 8-15, the addressed bit appears at bit 16 (+ PHY
//index). From Allwinner BSP USBC_Phy_Read; verified on the bench 2026-10-18 (0x68 defaults, 0xE9 after the writes). Bits
//addr .. addr + len - 1, addr in bit 0 of the result. Clock and reset of the OTG block must be up.
static uint32 pc_usb_phy_read(uint32 addr, uint32 len)
{
  uint32 i, r = 0;
  volatile uint32 d;

  for(i=0;i<len;i++)
  {
    *USBC_REG_PHYCTL = (*USBC_REG_PHYCTL & ~0x0000FF00u) | ((addr + i) << 8);

    for(d=0;d<8;d++)
      ;

    r |= ((*USBC_REG_PHYCTL >> 16) & 1u) << i;
  }

  return r;
}

//Packed tuning bits: bit 0 RES45 calibration (0x0C), bits 1-5 TX amplitude (0x20, 5 bits), bits 6-7 disconnect
//threshold (0x2A, 2 bits). Linux and 1.00o5 write 1, 0x14, 3, i.e. 0x00E9 when read back as written.
static uint16 pc_usb_phy_tuning(void)
{
  return (uint16)(pc_usb_phy_read(0x0C, 1) | (pc_usb_phy_read(0x20, 5) << 1) | (pc_usb_phy_read(0x2A, 2) << 6));
}

static uint16 pc_usb_phy_pre_bits;

void pc_usb_phy_pre(void)
{
  pc_usb_phy_pre_bits = pc_usb_phy_tuning();
}

//End of usb_device_init (fw 0.2.25): seven type 19 entries, a = field.
//  0: POWER << 8 | FADDR          1: INTUSBE << 8 | CCU flags (as type 16)    2: ISCR bits 4-19
//  3: USBPHY_CFG bits 0-15        4: PHY tuning bits now (packed, pc_usb_phy_tuning)
//  5: PHY tuning bits before the writes   6: PHYCTL bits 0-15 now
void pc_usb_log_post_init(void)
{
  uint32 cpsr = pc_irq_save();
  uint32 phy  = *CCU_USBPHY_CFG;
  uint8  flags = (uint8)(((*CCU_BUS_SOFT_RST0 & CCU_BSRR1_USBOTG_EN) ? 0x01 : 0) |
                         ((*CCU_BUS_CLK_GATE0 & CCU_BCGR1_USBOTG_EN) ? 0x02 : 0) |
                         ((phy & CCU_USBPHY_CFG_RST) ? 0x04 : 0) | ((phy & CCU_USBPHY_CFG_GAT) ? 0x08 : 0));

  pc_evlog_add(PC_EVT_USB_POST_INIT, 0, (uint16)((*USBC_REG_PCTL << 8) | (*USBC_REG_FADDR & 0x7F)));
  pc_evlog_add(PC_EVT_USB_POST_INIT, 1, (uint16)((*USBC_REG_INTUSBE << 8) | flags));
  pc_evlog_add(PC_EVT_USB_POST_INIT, 2, (uint16)((*USBC_REG_ISCR >> 4) & 0xFFFF));
  pc_evlog_add(PC_EVT_USB_POST_INIT, 3, (uint16)(phy & 0xFFFF));
  pc_evlog_add(PC_EVT_USB_POST_INIT, 4, pc_usb_phy_tuning());
  pc_evlog_add(PC_EVT_USB_POST_INIT, 5, pc_usb_phy_pre_bits);
  pc_evlog_add(PC_EVT_USB_POST_INIT, 6, (uint16)(*USBC_REG_PHYCTL & 0xFFFF));
  pc_irq_restore(cpsr);
}

uint32_t pc_hal_event_bus(uint8_t *dst, uint32_t *total)
{
  uint32 cpsr = pc_irq_save();
  uint32 n = pc_evbus_n, k = (n < PC_EVBUS_SIZE) ? n : PC_EVBUS_SIZE, i;

  for(i=0;i<k;i++)
  {
    const pc_evlog_entry *en = &pc_evbus[(n - k + i) & (PC_EVBUS_SIZE - 1)];
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
    //fw 0.2.49: an application uses the port: the suspend watch's long rule starts from 15 min again
    pc_usbw_dtr_up(&pc_uw);

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
  pc_usbw_init(&pc_uw);              //fw 0.2.49: CDC (re)start or MSC switch: no suspend, no pending reconnect

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

//The line state the host set; a suspended host (or a pulled cable, which also reads as suspend with VBUS forced
//valid) is not an active connection, but its DTR comes back with the resume
int pc_hal_dtr(void)
{
  return pc_dtr && !pc_uw.susp && !pc_uw.off;
}

//----------------------------------------------------------------------------------------------------------------------------------

void pc_hal_link_stats(uint32_t *v)
{
  uint32 i;

  for(i=0;i<PC_TSTAT_COUNT;i++)
    v[i] = pc_ts[i];
}

//----------------------------------------------------------------------------------------------------------------------------------
