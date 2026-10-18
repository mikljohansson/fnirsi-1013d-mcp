#include "types.h"
#include "cdc_class.h"
#include "usb_interface.h" /* for usb_read_from_fifo / usb_write_ep2_data etc. */
#include <string.h>

#include "pc_cmds.h"      /* PC_SERIAL */

//------------------------------------------------------------------------------
// USB identity. PC protocol builds use the pid.codes test VID/PID (never to be shipped; plan section 10)
// instead of ST's 0x0483:0x5740, a new bcdDevice so Windows does not reuse a cached binding, a serial number
// string and interface strings.
#ifndef PC_USB_VID
#define PC_USB_VID 0x1209
#endif
#ifndef PC_USB_PID
#define PC_USB_PID 0x0001
#endif
#define CDC_VID           PC_USB_VID
#define CDC_PID           PC_USB_PID
#define CDC_BCD_DEVICE    0x0100
#define CDC_ISERIAL       0x03
#define CDC_IIF_CONTROL   0x05
#define CDC_IIF_DATA      0x06

 /* Buffers and indices */
 uint8 usb_rx[1024];
 volatile uint32 usb_rx_in_idx = 0;
 volatile uint32 usb_rx_out_idx = 0;

//PC protocol: the 32 KiB text TX ring is not needed (pc_transport.c stages whole responses). The 1 KiB RX ring stays
//because PC_interface.c (unused OEM-app code, still linked) references it; nothing fills it.

  uint32 circular_buffer_used(const volatile uint32 in, const volatile uint32 out, const uint32 size);
  uint32 circular_buffer_free(const volatile uint32 in, const volatile uint32 out, const uint32 size);
  

//volatile uint32 EP2DisableTX = 0; /* flag to disable EP transmission */
volatile uint32 EP1DisableTX = 0;       /* flag to disable EP transmission */

/* Forward helpers */
 inline uint32 circular_buffer_used(const volatile uint32 in, const volatile uint32 out, const uint32 size) {
    if (in >= out) return in - out;
    return size - (out - in);
}

 inline uint32 circular_buffer_free(const volatile uint32 in, const volatile uint32 out, const uint32 size) {
    uint32 used = circular_buffer_used(in, out, size);
    return size - used - 1; // keep one byte free to distinguish full/empty 
}


//==============================================================================
/* Device descriptor */
 const USB_DeviceDescriptor CDC_DevDesc =
{
  sizeof(USB_DeviceDescriptor),
  DEVICE_DESCRIPTOR,
  0x0200,
  0x02,
  0x00,
  0x00,
  USB_EP0_FIFO_SIZE,
  CDC_VID,
  CDC_PID,
  CDC_BCD_DEVICE,
  0x01,
  0x02,
  CDC_ISERIAL,
  0x01
};

/* Configuration descriptor & related descriptors -- keep structure as before,
   but ensure endpoint packet sizes are consistent with CH340_BULK_MAX_PACKET / CH340_INTR_MAX_PACKET */
const CDC_Descriptor CDC_ConfDesc =
{
  {
    sizeof(USB_ConfigDescriptor),
    CONFIGURATION_DESCRIPTOR,
    CONFIG_CDC_DESCRIPTOR_LEN,
    0x02,
    0x01,
    0x00,
    0xC0,
    0x32
  },
  {
    sizeof(USB_InterfaceDescriptor),
    INTERFACE_DESCRIPTOR,
    0x00,
    0x00,
    0x01,
    0x02,
    0x02,
    0x01,
    CDC_IIF_CONTROL
  },
  {
    sizeof(USB_CDC_HeaderDescriptor),
    USB_DT_CS_INTERFACE,
    0x00,
    0x0110
  },
  {
    sizeof(USB_CDC_CallMgmtDescriptor),
    USB_DT_CS_INTERFACE,
    0x01,
    0x00,
    0x01
  },
  {
    sizeof(USB_CDC_AcmDescriptor),
    USB_DT_CS_INTERFACE,
    0x02,
    0x02
  },
  {
    sizeof(USB_CDC_UnionDescriptor),
    USB_DT_CS_INTERFACE,
    0x06,
    0x00,
    0x01
  },
  /* Interrupt endpoint for notifications (IN) */
  {
    sizeof(USB_EndPointDescriptor),
    ENDPOINT_DESCRIPTOR,
    (1 << 7) | 1,         // 0x81 — IN endpoint 1 (interrupt) 
    3,                    // interrupt 
    CDC_INTR_MAX_PACKET,  // wMaxPacketSize = 8 bytes
    10                    // bInterval = 10 (both FS and HS)
  },
  /* Data interface descriptor */
  {
    sizeof(USB_InterfaceDescriptor),
    INTERFACE_DESCRIPTOR,
    0x01,
    0x00,
    0x02,
    0x0A,
    0x00,
    0x00,
    CDC_IIF_DATA
  },
  /* Bulk IN and OUT endpoints (IN = 0x82, OUT = 0x02) */
  {
    {
      sizeof(USB_EndPointDescriptor),
      ENDPOINT_DESCRIPTOR,
      (1 << 7) | 2, /* 0x82 — IN endpoint 2 (bulk) */
      2,            /* bulk */
      CDC_BULK_MAX_PACKET,
      0
    },
    {
      sizeof(USB_EndPointDescriptor),
      ENDPOINT_DESCRIPTOR,
      (0 << 7) | 2, /* 0x02 — OUT endpoint 2 (bulk) */
      2,            /* bulk */
      CDC_BULK_MAX_PACKET,
      0
    }
  }
};

/* String descriptors, PC protocol build (product string is what host discovery checks, protocol.md 3.1) */
const uint8 str_vendor[32] = {
    0x20, 0x03,   // "fnirsi-1013d-ai"
    'f',0,'n',0,'i',0,'r',0,'s',0,'i',0,'-',0,'1',0,'0',0,'1',0,'3',0,'d',0,'-',0,'a',0,'i',0
};

const uint8 str_ret[26] = {
    0x1A, 0x03,   // "FNIRSI 1013D"
    'F',0,'N',0,'I',0,'R',0,'S',0,'I',0,' ',0,'1',0,'0',0,'1',0,'3',0,'D',0
};

const uint8 str_isernum[22] = {
    0x16, 0x03,   // "1013D-0001" = PC_SERIAL (GET_INFO serial)
    '1',0,'0',0,'1',0,'3',0,'D',0,'-',0,'0',0,'0',0,'0',0,'1',0
};

//The serial string must be PC_SERIAL; this fails to compile when only one of them is changed
typedef char pc_serial_length_check[(sizeof(PC_SERIAL) * 2 == sizeof(str_isernum)) ? 1 : -1];

const uint8 str_config[24] = {
    0x18, 0x03,   // "FNIRSI Conf"
    'F',0,'N',0,'I',0,'R',0,'S',0,'I',0,' ',0,'C',0,'o',0,'n',0,'f',0
};

const uint8 str_interface1[42] = {
    0x2A, 0x03,   // "FNIRSI 1013D control"
    'F',0,'N',0,'I',0,'R',0,'S',0,'I',0,' ',0,'1',0,'0',0,'1',0,'3',0,'D',0,' ',0,'c',0,'o',0,'n',0,'t',0,'r',0,'o',0,'l',0
};

const uint8 str_interface2[36] = {
    0x24, 0x03,   // "FNIRSI 1013D data"
    'F',0,'N',0,'I',0,'R',0,'S',0,'I',0,' ',0,'1',0,'0',0,'1',0,'3',0,'D',0,' ',0,'d',0,'a',0,'t',0,'a',0
};

/* vendor specifics */
const uint8 vendorVersion[2] = { 0x31, 0x00 };
const uint8 vendorAttach[2]  = { 0xC3, 0x00 };
const uint8 vendorStatus[2]  = { 0xFF, 0xEE };

//PC protocol: the CDC data endpoint belongs to pc_transport.c. The old busy-wait pump stays as a no-op for the
//debug helpers in menu.c that still call it.
volatile uint8 usb_tx_busy = 0;

void usb_CDC_in_ep_callback(void)
{
}

//==============================================================================

