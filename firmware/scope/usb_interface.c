//----------------------------------------------------------------------------------------------------------------------------------

#include "ccu_control.h"
#include "usb_interface.h"
#include "interrupt.h"
#include "timer.h"
#include "display_lib.h"
#include "scope_functions.h"

#include "mass_storage_class.h"
#include "cdc_class.h"
#include "PC_interface.h"
#include "pc_hooks.h"

#include "pc_transport.h"

#include <string.h>

//----------------------------------------------------------------------------------------------------------------------------------

void usb_device_irq_handler(void);
void usb_device_CDC_irq_handler(void);//*

int32 usb_device_write_data_ep_pack(int32 ep, uint8 * databuf, int32 len);

//----------------------------------------------------------------------------------------------------------------------------------

uint8  current_speed = USB_SPEED_UNKNOWN;
uint32 usb_connect = 0;
int32  usb_ep0_state = EP0_IDLE;


extern volatile uint32 msc_state;

volatile uint32 usb_set_faddr = 0;
volatile uint32 usb_faddr = 0;

volatile uint32  ep0_data_length = 0;
volatile uint8  *ep0_data_pointer;

union
{
  uint32           data[2];
  USB_Setup_Packet packet;
} usb_setup_packet;

USB_CDC_LineCoding port_line_coding = {
		.dwDTERate   = 9600,
		.bCharFormat = 0,
		.bParityType = 0,
		.bDataBits   = 8
};

volatile unsigned int port_line_coding_flag;

uint8 dtr_state = 0;
uint8 rts_state = 0;

//----------------------------------------------------------------------------------------------------------------------------------
//In original code the register is written as byte, which might be wrong since some data is shifted 8 bits to the left
//So using 32 bit register here
//fw 0.2.25: the PHY control register of the F1C100s is 0x01C13404 (Linux phy-sun4i-usb.c: suniv_f1c100s_cfg uses
//REG_PHYCTL_A10 = 0x04 from "phy_ctrl", which suniv-f1c100s.dtsi maps at 0x01C13400, length 0x10). 1.00o5 writes
//0x01C13410 (the A33-and-later offset, outside that region), so its three PHY tuning writes presumably never reached
//the PHY (inference, see firmware/AGENTS.md). Same bit protocol as Linux: address in bits 8-15, data bit 7, pulse bit 0.
#define USB_PHY_SETUP_REG USBC_REG_PHYCTL
static void usb_phy_setup(int32 addr, int32 data, int32 len)
{
  int32 j;

  //Clear the clock bit
  *USB_PHY_SETUP_REG &= 0xFFFFFFFE;
  
  for(j=0;j<len;j++)
  {
    //set the bit address to be written
    *USB_PHY_SETUP_REG &= 0xFFFF00FF;
    *USB_PHY_SETUP_REG |= ((addr + j) << 8);
    
    //set or clear data bit
    if(data & 0x1)
    {
      *USB_PHY_SETUP_REG |= 0x00000080;
    }
    else
    {
      *USB_PHY_SETUP_REG &= 0xFFFFFF7F;
    }

    //Create some sort of clock pulse
    *USB_PHY_SETUP_REG |= 0x00000001;
    *USB_PHY_SETUP_REG &= 0xFFFFFFFE;
    
    //Next data bit
    data >>= 1;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
void usb_device_init(void)
{
    // PC protocol: drop buffered data and flag a link reset (boot, CDC <-> MSC switch)
    pc_transport_reset();

    // fw 0.2.22: log what the boot path left in the controller (event type 16), then put the USB controller and
    // PHY into reset. 1.00o5 below only releases the resets (|=), so after an SD boot it configured a controller in
    // whatever state the bootloader or a previous mode left; now every path starts from the power-on state.
    pc_usb_log_init_state();
    *CCU_BUS_SOFT_RST0 &= ~CCU_BSRR1_USBOTG_EN;
    *CCU_USBPHY_CFG    &= ~CCU_USBPHY_CFG_RST;
    timer0_delay(1);

    // Enable and clock the USB PHY
    *CCU_USBPHY_CFG |= (CCU_USBPHY_CFG_RST | CCU_USBPHY_CFG_GAT);

    // Enable the clock to the USB interface
    *CCU_BUS_CLK_GATE0 |= CCU_BCGR1_USBOTG_EN;

    // Lift the reset of the USB interface
    *CCU_BUS_SOFT_RST0 |= CCU_BSRR1_USBOTG_EN;

    // fw 0.2.25: PHY tuning bits as the reset left them (read back before the writes below)
    pc_usb_phy_pre();

    // PHY calibration: 45 Ω regulation
    usb_phy_setup(0x0C, 0x01, 1);

    // Adjust PHY’s magnitude and rate
    usb_phy_setup(0x20, 0x14, 5);

    // Threshold adjustment disconnect
    usb_phy_setup(0x2A, 3, 2);

    // Configure FIFO base
    *SYS_CNTRL_USB_FIFO = (*SYS_CNTRL_USB_FIFO & 0xFFFFFFFC) | 0x00000001;

    // Enable pull-ups, force ID high and VBUS valid
    *USBC_REG_ISCR =
        USBC_BP_ISCR_DPDM_PULLUP_EN |
        USBC_BP_ISCR_ID_PULLUP_EN |
        USBC_BP_ISCR_FORCE_ID |
        USBC_BP_ISCR_FORCE_VBUS_VALID;

    // Use PIO mode and FIFO (disable DMA)
    *USBC_REG_VEND0 = 0x00;

    // Disable USB device and interrupts before reconfiguring
    usb_device_disable();

    // --- CH340 (CDC) mode ---
    //if ((USB_CH340 == 1) && dev_mode)
    if ((USB_CH340 == 1))
    {
        // Small delay to ensure clean disconnect before mode switch
        timer0_delay(200);

        // Attach correct interrupt handler for CH340
        setup_interrupt(USB_IRQ_NUM, usb_device_CDC_irq_handler, 2);

        // Enable USB device
        usb_device_enable();

        // Wait for host detection and stabilize enumeration
        timer0_delay(500);

        // (Optional) pre-init line coding or send test data
         //set_baud_rate();
         //usb_ch340_send_text("CH340 mode ready\r\n");
    }
    else
    {
        // --- MSC (Mass Storage) mode ---
        setup_interrupt(USB_IRQ_NUM, usb_device_irq_handler, 2);
        //usb_device_enable();
    }

    // fw 0.2.25: controller and PHY state at the end of the init (event type 19)
    pc_usb_log_post_init();
}

/*
 void usb_device_init(void)
{
  //Enable and clock the USB PHY
  *CCU_USBPHY_CFG |= (CCU_USBPHY_CFG_RST | CCU_USBPHY_CFG_GAT);
  
  //Enable the clock to the USB interface
  *CCU_BUS_CLK_GATE0 |= CCU_BCGR1_USBOTG_EN;
  
  //Lift the reset of the USB interface
  *CCU_BUS_SOFT_RST0 |= CCU_BSRR1_USBOTG_EN;
  
  //Regulation 45 ohms
  usb_phy_setup(0x0c, 0x01, 1);

  //Adjust PHY's magnitude and rate
  usb_phy_setup(0x20, 0x14, 5);

  //Threshold adjustment disconnect
  usb_phy_setup(0x2a, 3, 2);
  
  //Configurate the FIFO base
  *SYS_CNTRL_USB_FIFO = (*SYS_CNTRL_USB_FIFO & 0xFFFFFFFC) | 0x00000001;
  
  //Enable pull up resistors and force id high and bus valid.
  *USBC_REG_ISCR = USBC_BP_ISCR_DPDM_PULLUP_EN | USBC_BP_ISCR_ID_PULLUP_EN | USBC_BP_ISCR_FORCE_ID | USBC_BP_ISCR_FORCE_VBUS_VALID;  //Original code has 0x0C00 also set (USBC_BP_ISCR_VBUS_VALID_SRC)
  
  //Set interface to pio mode and use FIFO
  *USBC_REG_VEND0 = 0x00;

  //Disable the device and it's interrupts
  usb_device_disable();
  
  //Setup the interrupt handler for the USB interface
  
  if((USB_CH340==1)&&(dev_mode)) 
    {
      setup_interrupt(USB_IRQ_NUM, usb_device_CH340_irq_handler, 2);    //for CH340
      //Wait 200ms to allow the USB interface to disconnect before switching to CH340 mode
      //Is only needed when loaded via FEL and not when booted from SD card
      timer0_delay(200);
      usb_device_enable();                                              //enable serial
            timer0_delay(1000);
      //set_baud_rate();

    }
    else setup_interrupt(USB_IRQ_NUM, usb_device_irq_handler, 2);       //for mass storage
}
 */

//----------------------------------------------------------------------------------------------------------------------------------

void usb_device_enable(void)
{
  //Start with unknown speed
  current_speed = USB_SPEED_UNKNOWN;

  //Setup for bulk by clearing the iso bit
  *USBC_REG_PCTL &= ~USBC_BP_POWER_D_ISO_UPDATE_EN;
  
  //Enable the high speed mode
  *USBC_REG_PCTL |= USBC_BP_POWER_D_HIGH_SPEED_EN;

  //Keep the PHY powered in suspend (SUSPENDM not driven), as Linux musb does for sunxi: the core still detects the
  //host's resume, and nothing has to be restored afterwards
  *USBC_REG_PCTL &= ~USBC_BP_POWER_D_ENABLE_SUSPENDM;

  //Enable the needed interrupts
  *USBC_REG_INTUSBE = USBC_BP_INTUSB_SUSPEND | USBC_BP_INTUSB_RESUME | USBC_BP_INTUSB_RESET | USBC_BP_INTUSB_DISCONNECT;
  
  //Enable the needed interrupts ch340
    //*USBC_REG_INTUSBE = USBC_BP_INTUSB_SOF | USBC_BP_INTUSB_SUSPEND | USBC_BP_INTUSB_RESUME | USBC_BP_INTUSB_RESET | USBC_BP_INTUSB_DISCONNECT;
  //PC protocol: no SOF interrupt (8000/s at high speed); the EP2 TX interrupt drives the transmit pump

  //Enable EP0 interrupt
  *USBC_REG_INTTXE = 1;

  //Switch the interface on
  *USBC_REG_PCTL |= USBC_BP_POWER_D_SOFT_CONNECT;
}

//----------------------------------------------------------------------------------------------------------------------------------

void usb_device_disable(void)
{
  //Disable all miscellaneous interrupts
  *USBC_REG_INTUSBE = 0;
  
  //Disable all receive interrupts
  *USBC_REG_INTRXE = 0;

  //Disable all transmit interrupts
  *USBC_REG_INTTXE = 0;

  //Clear all pending miscellaneous interrupts
  *USBC_REG_INTUSB = 0xFF;
  
  //Clear all pending receive interrupts
  *USBC_REG_INTRX = 0xFFFF;
  
  //Clear all pending transmit interrupts
  *USBC_REG_INTTX = 0xFFFF;
  
  //Switch the interface off
  *USBC_REG_PCTL &= ~USBC_BP_POWER_D_SOFT_CONNECT;
}

//----------------------------------------------------------------------------------------------------------------------------------

/* fifo read/write */
void usb_write_to_fifo(void *FIFO, void *buffer, uint32 length)
{
    uint32 alignment = ((uint32)buffer) & 3;
    if (alignment == 0) {
        uint32 count = length / 4;
        length -= count * 4;
        while (count--) {
            (*(volatile uint32 *)FIFO) = *(uint32 *)buffer;
            buffer = (uint8*)buffer + 4;
        }
        if (length > 1) {
            (*(volatile uint16 *)FIFO) = *(uint16 *)buffer;
            buffer = (uint8*)buffer + 2;
            length -= 2;
        }
        if (length) (*(volatile uint8 *)FIFO) = *(uint8 *)buffer;
    } else if (alignment == 2) {
        uint32 count = length / 2;
        while (count--) {
            (*(volatile uint16 *)FIFO) = *(uint16 *)buffer;
            buffer = (uint8*)buffer + 2;
        }
        if (length & 1) (*(volatile uint8 *)FIFO) = *(uint8 *)buffer;
    } else {
        while (length--) {
            (*(volatile uint8 *)FIFO) = *(uint8 *)buffer;
            buffer = (uint8*)buffer + 1;
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------------------

void usb_read_from_fifo(void *FIFO, void *buffer, uint32 length)
{
    uint32 alignment = ((uint32)buffer) & 3;
    if (alignment == 0) {
        uint32 count = length / 4;
        length -= count * 4;
        while (count--) {
            *(uint32 *)buffer = (*(volatile uint32 *)FIFO);
            buffer = (uint8*)buffer + 4;
        }
        if (length > 1) {
            *(uint16 *)buffer = (*(volatile uint16 *)FIFO);
            buffer = (uint8*)buffer + 2;
            length -= 2;
        }
        if (length) *(uint8 *)buffer = (*(volatile uint8 *)FIFO);
    } else if (alignment == 2) {
        uint32 count = length / 2;
        while (count--) {
            *(uint16 *)buffer = (*(volatile uint16 *)FIFO);
            buffer = (uint8*)buffer + 2;
        }
        if (length & 1) *(uint8 *)buffer = (*(volatile uint8 *)FIFO);
    } else {
        while (length--) {
            *(uint8 *)buffer = (*(volatile uint8 *)FIFO);
            buffer = (uint8*)buffer + 1;
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------------------

void usb_device_stall_tx_ep(void)
{
  *USBC_REG_TXCSR |= USBC_BP_TXCSR_D_SEND_STALL;
}

//----------------------------------------------------------------------------------------------------------------------------------

void usb_device_stall_rx_ep(void)
{
  *USBC_REG_RXCSR |= USBC_BP_RXCSR_D_SEND_STALL;
}

//----------------------------------------------------------------------------------------------------------------------------------

void usb_write_ep1_data(void *buffer, uint32 length)
{
  usb_write_to_fifo((void *)USBC_REG_EPFIFO1, buffer, length);
  
  //Clear possible under run error and signal packet send
  //This bit needs to be cleared first, otherwise it fails for some reason
  *USBC_REG_TXCSR &= ~USBC_BP_TXCSR_D_UNDER_RUN;
  *USBC_REG_TXCSR |= USBC_BP_TXCSR_D_TX_READY;
}
//----------------------------------------------------------------------------------------------------------------------------------
//treba?
void usb_write_ep2_data(void *data, uint32 length)
{
    *USBC_REG_EPIND = 2; // FIX: nastavenie správneho endpointu pred zápisom
    uint8 *src = (uint8*)data;
    for (uint32 i = 0; i < length; i++)
        *USBC_REG_EPFIFO2 = src[i];

    *USBC_REG_TXCSR &= ~USBC_BP_TXCSR_D_UNDER_RUN;
    *USBC_REG_TXCSR |= USBC_BP_TXCSR_D_TX_READY;
}

//----------------------------------------------------------------------------------------------------------------------------------

void usb_device_irq_handler(void)
{
  //Reading these registers clear all the active interrupt bits, but they still need active clearing
  //So using the registers directly in the if statements does not work
  register uint32 usbirq = *USBC_REG_INTUSB;
  register uint32 txirq  = *USBC_REG_INTTX;
  register uint32 rxirq  = *USBC_REG_INTRX;
  
  //Check on a RESET interrupt
  if(usbirq & USBC_BP_INTUSB_RESET)
  {
    //Clear all pending miscellaneous interrupts
    *USBC_REG_INTUSB = 0xFF;
    
    //Clear all pending receive interrupts
    *USBC_REG_INTRX = 0xFFFF;

    //Clear all pending transmit interrupts
    *USBC_REG_INTTX = 0xFFFF;
    
    //Need to check if this needs to be this way
    usb_connect = 1;
    
    usb_ep0_state = EP0_IDLE;
    
    //Set EP0 as active one
    *USBC_REG_EPIND = 0;
    
    //Switch to default device address
    *USBC_REG_FADDR = 0;

    return;
  }

  //Check on a RESUME interrupt
  if(usbirq & USBC_BP_INTUSB_RESUME)
  {
    //Clear the interrupt
    *USBC_REG_INTUSB = USBC_BP_INTUSB_RESUME;
    
    //Back to connected state
    usb_connect = 1;
    usb_ep0_state = EP0_IDLE;
  }

  //Check on a SUSPEND interrupt
  if(usbirq & USBC_BP_INTUSB_SUSPEND)
  {
    //Clear the interrupt
    *USBC_REG_INTUSB = USBC_BP_INTUSB_SUSPEND;
    
    //No longer connected
    usb_connect = 0;
    usb_ep0_state = EP0_IDLE;
  }

  //Check on a DISCONNECT interrupt
  if(usbirq & USBC_BP_INTUSB_DISCONNECT)
  {
    //Clear the interrupt
    *USBC_REG_INTUSB = USBC_BP_INTUSB_DISCONNECT;

    //No longer connected
    usb_connect = 0;
    usb_ep0_state = EP0_IDLE;
    
    //Need to add code here to force the scope to go back to its normal mode???
    //Use a flag that is checked in the touch scan for the usb on/off
  }

  //Check on an EP0 interrupt
  if(txirq & USBC_INTTX_FLAG_EP0)
  {
    //Clear the interrupt
    *USBC_REG_INTTX = USBC_INTTX_FLAG_EP0;

    //When speed not previously set check what is negotiated by the host
    if(current_speed == USB_SPEED_UNKNOWN)
    {
      //Check the speed negotiated by the host
      if(*USBC_REG_PCTL & USBC_BP_POWER_D_HIGH_SPEED_FLAG)
      {
        current_speed = USB_SPEED_HIGH;
      }
      else
      {
        //Things will fail for low speed due to the end point size not being adjusted
        //Think it would also need a different device descriptor or configuration
        //Question is how big is the change it will be connected to a full speed only device
        current_speed = USB_SPEED_FULL;
      }
    }
    
    //Set EP0 as active one
    *USBC_REG_EPIND = 0;
    
    //Check if previous setup package was a set address command
    if(usb_set_faddr)
    {
      //If so set the received device address in the USB device
      *USBC_REG_FADDR = usb_faddr;
      
      //Only do this when requested
      usb_set_faddr = 0;
    }

    //Clear stall status if needed
    if(*USBC_REG_CSR0 & USBC_BP_CSR0_D_SENT_STALL)
    {
      //Clear the stall state. Is the only bit cleared when a 0 is written to it.
      *USBC_REG_CSR0 = 0x00;

      //Make sure system is back in idle state
      usb_ep0_state = EP0_IDLE;
    }
    else
    {
      //Clear setup end when needed
      if(*USBC_REG_CSR0 & USBC_BP_CSR0_D_SETUP_END)
      {
        //Signal setup end is serviced
        *USBC_REG_CSR0 = USBC_BP_CSR0_D_SERVICED_SETUP_END;

        //Make sure system is back in idle state
        usb_ep0_state = EP0_IDLE;
      }

      //Handle the data based on the current state
      switch(usb_ep0_state)
      {
        case EP0_IDLE:
          //Check if a packet has been received
          if(*USBC_REG_CSR0 & USBC_BP_CSR0_D_RX_PKT_READY)
          {
            //Count is only valid when receive packet ready bit is set
            //A setup packet is always 8 bytes in length
            if(*USBC_REG_COUNT0 == 8)
            {
              //Get 8 bytes of data from the FIFO into the setup packet structure
              //Using a union for alignment to 32 bits
              usb_setup_packet.data[0] = *USBC_REG_EPFIFO0;
              usb_setup_packet.data[1] = *USBC_REG_EPFIFO0;
              
              //Signal packet has been serviced
              *USBC_REG_CSR0 = USBC_BP_CSR0_D_SERVICED_RX_PKT_READY;
              
              //Handle the packet based on the type
              switch(usb_setup_packet.packet.bRequestType & USB_TYPE_MASK)
              {
                case USB_TYPE_STANDARD:
                  switch(usb_setup_packet.packet.bRequest)
                  {
                    case USB_REQ_GET_DESCRIPTOR:
                    {
                      //No data to send yet
                      ep0_data_length = 0;
                      
                      //Handle the request based on the type of descriptor
                      switch(usb_setup_packet.packet.wValue >> 8)
                      {
                        case DEVICE_DESCRIPTOR:
                          ep0_data_length  = sizeof(USB_DeviceDescriptor);
                          ep0_data_pointer = (uint8 *)&Mass_Storage_DevDesc;
                          break;

                        case CONFIGURATION_DESCRIPTOR:
                          ep0_data_length  = sizeof(Mass_Storage_ConfDesc);
                          ep0_data_pointer = (uint8 *)&Mass_Storage_ConfDesc;
                          break;

                        case STRING_DESCRIPTOR:
                        {
                          switch(usb_setup_packet.packet.wValue & 0xFF)
                          {
                            case 0:
                              ep0_data_length  = sizeof(StringLangID);
                              ep0_data_pointer = (uint8 *)&StringLangID;
                              break;

                            case 1:
                            case 3:
                            case 0xEE:
                              ep0_data_length  = sizeof(StringVendor);
                              ep0_data_pointer = (uint8 *)&StringVendor;
                              break;

                            case 2:
                              ep0_data_length  = sizeof(StringProduct);
                              ep0_data_pointer = (uint8 *)&StringProduct;
                              break;

                            case 4:
                              ep0_data_length  = sizeof(StringInterface);
                              ep0_data_pointer = (uint8 *)&StringInterface;
                              break;

                            default:
                              ep0_data_length  = sizeof(StringSerial);
                              ep0_data_pointer = (uint8 *)&StringSerial;
                              break;
                          }
                        }
                        break;
                      }
                      
                      //Check if there is data to send
                      if(ep0_data_length)
                      {
                        //Limit the length on what the host allows
                        if(ep0_data_length > usb_setup_packet.packet.wLength)
                        {
                          ep0_data_length = usb_setup_packet.packet.wLength;
                        }
                        
                        //Need a separate variable for checking if the data length is more then the end point FIFO size
                        uint32 length = ep0_data_length;
                        
                        //Check if more data then the FIFO can hold
                        if(length > USB_EP0_FIFO_SIZE)
                        {
                          //Limit it to the FIFO size
                          length = USB_EP0_FIFO_SIZE;
                        }
                        
                        //Write the data to the FIFO
                        usb_write_to_fifo((void *)USBC_REG_EPFIFO0, (void *)ep0_data_pointer, length);

                        //Take of the number of bytes done
                        ep0_data_length -= length;

                        //Signal a package ready to send
                        *USBC_REG_CSR0 = USBC_BP_CSR0_D_TX_PKT_READY;
                        
                        //Check if there is more data to send to the host
                        if(ep0_data_length)
                        {
                          //Point to the next bit of the data that needs to be send in the next packet
                          ep0_data_pointer += length;
                          
                          //Switch to the state to send the remainder of the data
                          usb_ep0_state = EP0_IN_DATA_PHASE;
                        }
                      }
                    }
                    break;

                    case USB_REQ_SET_ADDRESS:
                      //Signal device address needs to be set on the next interrupt
                      usb_set_faddr = 1;
                      
                      //Save the received device address to be set in the USB device
                      usb_faddr = usb_setup_packet.packet.wValue & 0x7F;
                      break;

                    case USB_REQ_SET_CONFIGURATION:
                      //This needs attention to allow for full speed host connection
                      //Need to check if the double FIFO setup is needed. The rest of the code does not make use of it for as far as I can tell
                      //For the mass storage device two endpoints are used
                      //EP1 OUT for receiving data from the host
                      //EP1 IN for sending data to the host
                    
                      //Select the end point registers used for the mass storage
                      *USBC_REG_EPIND = 1;

                      //EP OUT configuration                      
                      //Reset the end point to bulk end point and clear the FIFO
                      *USBC_REG_RXCSR = USBC_BP_RXCSR_D_CLEAR_DATA_TOGGLE | USBC_BP_RXCSR_D_FLUSH_FIFO;
                      
                      //For double buffering clear the FIFO again
                      *USBC_REG_RXCSR = USBC_BP_RXCSR_D_FLUSH_FIFO;
                        
                      //Max 512 bytes per transaction
                      //This setting is dependent on the negotiated device speed
                      //Needs extra code to cope with that
                      *USBC_REG_RXMAXP = 512;
    
                      //The FIFO size is set based on 2^n * 8, so for 512 bytes it is 6
                      //As double buffering is used this bit is also set
                      *USBC_REG_RXFIFOSZ = USBC_BP_RXFIFOSZ_DPB | 6;
    
                      //The base address of the first FIFO is set to zero
                      *USBC_REG_RXFIFOAD = 0;
    
                      //EP IN configuration
                      //Reset the end point to bulk and clear the FIFO
                      *USBC_REG_TXCSR = USBC_BP_TXCSR_D_MODE | USBC_BP_TXCSR_D_CLEAR_DATA_TOGGLE | USBC_BP_TXCSR_D_FLUSH_FIFO;

                      //For double buffering clear the FIFO again
                      *USBC_REG_TXCSR = USBC_BP_TXCSR_D_FLUSH_FIFO;

                      //Max 512 bytes per transaction
                      //This setting is dependent on the negotiated device speed
                      //Needs extra code to cope with that
                      *USBC_REG_TXMAXP = 512;

                      //The FIFO size is set based on 2^n * 8, so for 512 bytes it is 6
                      //As double buffering is used this bit is also set
                      *USBC_REG_TXFIFOSZ = USBC_BP_TXFIFOSZ_DPB | 6;
                      
                      //The base address of the second FIFO is set to the first free address (twice the receive FIFO size / 8)
                      *USBC_REG_TXFIFOAD = 128;

                      //Enable the endpoint interrupts
                      *USBC_REG_INTRXE |= USBC_INTRX_FLAG_EP1;
                      *USBC_REG_INTTXE |= USBC_INTTX_FLAG_EP1;
  
                      //Clear the SCSI state to wait for command
                      msc_state = MSC_WAIT_COMMAND;
  
                      //Switch back to EP0
                      *USBC_REG_EPIND = 0;

                      //Send a null packet to acknowledge configuration
                      *USBC_REG_CSR0 = USBC_BP_CSR0_D_TX_PKT_READY;
                      break;

                    case USB_REQ_CLEAR_FEATURE:
                      //Might need more filtering. Looking at tinyusb project things are done differently
                      //This is extra in the scope. Not sure if it is really needed
                      
                      //Select the intended endpoint
                      *USBC_REG_EPIND = usb_setup_packet.packet.wIndex & 0x03;
                      
                      //Check the type of endpoint to halt
                      if(usb_setup_packet.packet.wIndex & 0x80)
                      {
                        //Transmit endpoint
                        *USBC_REG_TXCSR = USBC_BP_TXCSR_D_MODE | USBC_BP_TXCSR_D_CLEAR_DATA_TOGGLE | USBC_BP_TXCSR_D_FLUSH_FIFO;
                      }
                      else
                      {
                        //Receive endpoint
                        *USBC_REG_RXCSR = USBC_BP_RXCSR_D_CLEAR_DATA_TOGGLE | USBC_BP_RXCSR_D_FLUSH_FIFO;
                      }
                      
                      //Switch back to EP0
                      *USBC_REG_EPIND = 0;
                      break;
                  }
                  break;
                  
                case USB_TYPE_CLASS:
                  //For mass storage check if this is a max LUN request
                  if(usb_setup_packet.packet.bRequest == 0xFE)
                  {
                    //If so check if host allows for the needed data
                    if(usb_setup_packet.packet.wLength >= 1)
                    {
                      //Device has only one logical unit so send a zero
                      *(uint8 *)USBC_REG_EPFIFO0 = 0;
                      
                      //Signal data send and done with this packet
                      *USBC_REG_CSR0 = USBC_BP_CSR0_D_TX_PKT_READY;
                    }
                  }
//                  //Check if mass storage reset
//                  else if(usb_setup_packet.packet.bRequest == 0xFF)
//                  {
//                    //Mass storage reset might also be needed
//                  }
                  break;
              }

              //Check if done with data. This is only the case when not switched to another state
              if(usb_ep0_state == EP0_IDLE)
              {
                //Signal done with this packet
                *USBC_REG_CSR0 = USBC_BP_CSR0_D_DATA_END;
              }
            }
            else
            {
              //Signal packet has been serviced and stall the end point
              *USBC_REG_CSR0 = USBC_BP_CSR0_D_SERVICED_RX_PKT_READY | USBC_BP_CSR0_D_SEND_STALL;
            }
          }
          break;

        case EP0_IN_DATA_PHASE:
          if(ep0_data_length)
          {
            uint32 length = ep0_data_length;

            //Check if more data to send then the end point FIFO can handle
            if(ep0_data_length > USB_EP0_FIFO_SIZE)
            {
              //If so limit it to the FIFO size
              length = USB_EP0_FIFO_SIZE;
            }

            //Load the data into the FIFO
            usb_write_to_fifo((void *)USBC_REG_EPFIFO0, (void *)ep0_data_pointer, length);
            
            //Take of the data length just send
            ep0_data_length -= length;

            //Signal data packet ready to send
            *USBC_REG_CSR0 = USBC_BP_CSR0_D_TX_PKT_READY;
            
            //Check if there is more data that needs to be send
            if(ep0_data_length)
            {
              //Point to the next data to send
              ep0_data_pointer += length;
            }
            else
            {
              //Signal done with sending
              *USBC_REG_CSR0 = USBC_BP_CSR0_D_DATA_END;

              //Switch back to idle state when done
              usb_ep0_state = EP0_IDLE;
            }
          }
          break;
      }
    }
  }    

  //Check on data received for EP1
  if(rxirq & USBC_INTRX_FLAG_EP1)
  {
    //Clear the interrupt
    *USBC_REG_INTRX = USBC_INTTX_FLAG_EP1;

    //Set EP1 as active one
    *USBC_REG_EPIND = 1;

    //Check if the end point is in a stall
    if(*USBC_REG_RXCSR & USBC_BP_RXCSR_D_SENT_STALL)
    {
      //Clear the stall state
      *USBC_REG_RXCSR &= ~(USBC_BP_RXCSR_D_SENT_STALL | USBC_BP_RXCSR_D_SEND_STALL);
    }
  
    //Check if there is data to handle
    if(*USBC_REG_RXCSR & USBC_BP_RXCSR_D_RX_PKT_READY)
    {
      //Handle the received data in the mass storage code
      usb_mass_storage_out_ep_callback((void *)USBC_REG_EPFIFO1, *USBC_REG_RXCOUNT);

      //Signal done with the packet and clear possible errors
      *USBC_REG_RXCSR &= ~(USBC_BP_RXCSR_D_RX_PKT_READY | USBC_BP_RXCSR_D_OVERRUN);
    }
  }
  
  //Check on data transmitted for EP1
  if(txirq & USBC_INTTX_FLAG_EP1)
  {
    //Clear the interrupt
    *USBC_REG_INTTX = USBC_INTTX_FLAG_EP1;
    
    //Set EP1 as active one
    *USBC_REG_EPIND = 1;

    //Check if the end point is stalled
    if(*USBC_REG_TXCSR & USBC_BP_TXCSR_D_SENT_STALL)
    {
      //Clear the stall state if so
      *USBC_REG_TXCSR &= ~(USBC_BP_TXCSR_D_SENT_STALL | USBC_BP_TXCSR_D_SEND_STALL);
    }

    //Check if the FIFO is ready for more data
    if((*USBC_REG_TXCSR & USBC_BP_TXCSR_D_TX_READY) == 0)
    {
      //Have the mass storage code handle the request for data
      usb_mass_storage_in_ep_callback();
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
//**************************** CH340 *****************************
//----------------------------------------------------------------------------------------------------------------------------------

void usb_device_CDC_irq_handler(void)
{
  //Reading these registers clear all the active interrupt bits, but they still need active clearing
  //So using the registers directly in the if statements does not work
  register uint32 usbirq = *USBC_REG_INTUSB;
  register uint32 txirq  = *USBC_REG_INTTX;
  register uint32 rxirq  = *USBC_REG_INTRX;
  
  //Check on a RESET interrupt
  if(usbirq & USBC_BP_INTUSB_RESET)
  {
    //Clear all pending miscellaneous interrupts
    *USBC_REG_INTUSB = 0xFF;
    
    //Clear all pending receive interrupts
    *USBC_REG_INTRX = 0xFFFF;

    //Clear all pending transmit interrupts
    *USBC_REG_INTTX = 0xFFFF;
    
    //Need to check if this needs to be this way
    usb_connect = 1;
    
    usb_ep0_state = EP0_IDLE;
    
    //Set EP0 as active one
    *USBC_REG_EPIND = 0;
    
    //Switch to default device address
    *USBC_REG_FADDR = 0;
    
        *USBC_REG_CSR0  = USBC_BP_CSR0_D_FLUSH_FIFO; // FIX: po resete vyčisti FIFO

        *USBC_REG_TXCSR = 0;
        *USBC_REG_RXCSR = 0;
        
        *USBC_REG_TXCSR |= USBC_BP_TXCSR_D_FLUSH_FIFO;
        *USBC_REG_RXCSR |= USBC_BP_RXCSR_D_FLUSH_FIFO;

    //Bus reset: buffers are dropped, the event is counted (GET_LINK_STATS)
    pc_usb_isr_event(PC_USB_EV_BUS_RESET);
    pc_usb_isr_link();

    
    return;
  }

  //Check on a SUSPEND interrupt. fw 0.2.49: before RESUME, so that both pending (a resume can only follow its suspend)
  //ends in the resumed state; 1.00o5 handled RESUME first and left the device marked suspended
  if(usbirq & USBC_BP_INTUSB_SUSPEND)
  {
    //Clear the interrupt
    *USBC_REG_INTUSB = USBC_BP_INTUSB_SUSPEND;
    
    //No longer connected
    usb_connect = 0;
    usb_ep0_state = EP0_IDLE;

    //fw 0.2.44: a state, not a link reset (pc_transport.c); the following RESUME or bus reset decides
    pc_usb_isr_suspend();
  }

  //Check on a RESUME interrupt
  if(usbirq & USBC_BP_INTUSB_RESUME)
  {
    //Clear the interrupt
    *USBC_REG_INTUSB = USBC_BP_INTUSB_RESUME;
    
    //Back to connected state
    usb_connect = 1;
    usb_ep0_state = EP0_IDLE;

    pc_usb_isr_resume();
  }

  //Check on a DISCONNECT interrupt
  if(usbirq & USBC_BP_INTUSB_DISCONNECT)
  {
    //Clear the interrupt
    *USBC_REG_INTUSB = USBC_BP_INTUSB_DISCONNECT;

    //No longer connected
    usb_connect = 0;
    usb_ep0_state = EP0_IDLE;

    pc_usb_isr_event(PC_USB_EV_DISCONNECT);
    pc_usb_isr_link();
    
    //Need to add code here to force the scope to go back to its normal mode???
    //Use a flag that is checked in the touch scan for the usb on/off
  }

  //Check on start of frame interrupt
  if(usbirq & USBC_BP_INTUSB_SOF)
  {
    //Clear the interrupt
    *USBC_REG_INTUSB = USBC_BP_INTUSB_SOF;
    
    //Go and see if data needs to be send on EP2
    //usb_CDC_in_ep_callback();
  }
  
  
  //Check on an EP0 interrupt
  if(txirq & USBC_INTTX_FLAG_EP0)
  {
    //Clear the interrupt
    *USBC_REG_INTTX = USBC_INTTX_FLAG_EP0;

    //When speed not previously set check what is negotiated by the host
    if(current_speed == USB_SPEED_UNKNOWN)
    {
      //Check the speed negotiated by the host
      if(*USBC_REG_PCTL & USBC_BP_POWER_D_HIGH_SPEED_FLAG)
      {
        current_speed = USB_SPEED_HIGH;
      }
      else
      {
        //Things will fail for low speed due to the end point size not being adjusted
        //Think it would also need a different device descriptor or configuration
        //Question is how big is the change it will be connected to a full speed only device
        current_speed = USB_SPEED_FULL;
      }
    }
    
    // nastavenia závislé od rýchlosti
    //uint32_t max_packet = (current_speed == USB_SPEED_HIGH) ? 512 : 64;
    //uint32_t fifo_n = (max_packet == 512) ? 6 : 3; // 2^n * 8 = FIFO bytes (n=6 -> 512B, n=3 -> 64B)
    //uint32_t rx_fifo_bytes = (1u << fifo_n) * 8u;  // RX FIFO veľkosť v bajtoch
    //uint32_t tx_fifo_base_for_ep2 = 0;
    
    //Set EP0 as active one
    *USBC_REG_EPIND = 0;
    
    //Check if previous setup package was a set address command
    if(usb_set_faddr)
    {
      //If so set the received device address in the USB device
      *USBC_REG_FADDR = usb_faddr;
      
      //Only do this when requested
      usb_set_faddr = 0;
    }

    //Clear stall status if needed
    if(*USBC_REG_CSR0 & USBC_BP_CSR0_D_SENT_STALL)
    {
      //Clear the stall state. Is the only bit cleared when a 0 is written to it.
      *USBC_REG_CSR0 = 0x00;

      //Make sure system is back in idle state
      usb_ep0_state = EP0_IDLE;
    }
    else
    {
      //Clear setup end when needed
      if(*USBC_REG_CSR0 & USBC_BP_CSR0_D_SETUP_END)
      {
        //Signal setup end is serviced
        *USBC_REG_CSR0 = USBC_BP_CSR0_D_SERVICED_SETUP_END;

        //Make sure system is back in idle state
        usb_ep0_state = EP0_IDLE;
      }

      //Handle the data based on the current state
      switch(usb_ep0_state)
      {
        case EP0_IDLE:
          //Check if a packet has been received
          if(*USBC_REG_CSR0 & USBC_BP_CSR0_D_RX_PKT_READY)
          {   
            //Count is only valid when receive packet ready bit is set
            //A setup packet is always 8 bytes in length
            if(*USBC_REG_COUNT0 == 8)
            {
              //Get 8 bytes of data from the FIFO into the setup packet structure
              //Using a union for alignment to 32 bits
              usb_setup_packet.data[0] = *USBC_REG_EPFIFO0;
              usb_setup_packet.data[1] = *USBC_REG_EPFIFO0;
              pc_usb_isr_setup(usb_setup_packet.data[0], usb_setup_packet.data[1]);
              
              //Signal packet has been serviced
              *USBC_REG_CSR0 = USBC_BP_CSR0_D_SERVICED_RX_PKT_READY;
              
              //No data to send yet
              ep0_data_length = 0;
              
              //Handle the packet based on the type
              switch(usb_setup_packet.packet.bRequestType & USB_TYPE_MASK)
              {
                case USB_TYPE_STANDARD:
                  switch(usb_setup_packet.packet.bRequest)
                  {
                    case USB_REQ_GET_DESCRIPTOR:
                    {
                      //Handle the request based on the type of descriptor
                      switch(usb_setup_packet.packet.wValue >> 8)
                      {
                        case DEVICE_DESCRIPTOR:
                          ep0_data_length  = sizeof(USB_DeviceDescriptor);
                          ep0_data_pointer = (uint8 *)&CDC_DevDesc;
                          break;

                        case CONFIGURATION_DESCRIPTOR:
                          ep0_data_length  = sizeof(CDC_ConfDesc);
                          ep0_data_pointer = (uint8 *)&CDC_ConfDesc;
                          break;
                                    
                        case STRING_DESCRIPTOR:
                        {
                          switch(usb_setup_packet.packet.wValue & 0xFF)
                          {
                            case 0:
                              ep0_data_length  = sizeof(StringLangID);
                              ep0_data_pointer = (uint8 *)&StringLangID;
                              break;

                            case 1: // Vendor
                              ep0_data_length  = sizeof(str_vendor);
                              ep0_data_pointer = (uint8 *)&str_vendor;
                              break;
                              
                            case 2: // Product
                              ep0_data_length  = sizeof(str_ret); 
                              ep0_data_pointer = (uint8 *)&str_ret;
                              break;
                              
                            case 3: // Serial
                              ep0_data_length  = sizeof(str_isernum); 
                              ep0_data_pointer = (uint8 *)&str_isernum;
                              break;
                              
                            case 4: // Configuration
                              ep0_data_length  = sizeof(str_config); 
                              ep0_data_pointer = (uint8 *)&str_config;
                              break;
                              
                            case 5: // Interface 1
                              ep0_data_length  = sizeof(str_interface1);
                              ep0_data_pointer = (uint8 *)&str_interface1;
                              break;
                            
                            case 6: // Interface 2
                              ep0_data_length  = sizeof(str_interface2); //n
                              ep0_data_pointer = (uint8 *)&str_interface2;
                              break;  
                              
                            case 0xEE:
                              // if you implement MS OS descriptor, return its index; else return vendor or lang id
                              ep0_data_length  = sizeof(StringLangID);
                              ep0_data_pointer = (uint8 *)&StringLangID;
                              break;
                            
                            default:
                              ep0_data_length  = sizeof(str_isernum); //n
                              ep0_data_pointer = (uint8 *)&str_isernum;  
                              break;
                          }
                        }
                        break;
                      }
                    }
                    break;

                    case USB_REQ_SET_ADDRESS:
                      //Signal device address needs to be set on the next interrupt
                      usb_set_faddr = 1;
                      
                      //Save the received device address to be set in the USB device
                      usb_faddr = usb_setup_packet.packet.wValue & 0x7F;
                      break;

                    case USB_REQ_SET_CONFIGURATION:
                      //This needs attention to allow for full speed host connection
                      //Need to check if the double FIFO setup is needed. The rest of the code does not make use of it for as far as I can tell
                      //For the CDC device three endpoints are used
                      //EP1 IN  for interrupt handling
                      //EP2 OUT for receiving data from the host
                      //EP2 IN  for sending data to the host
                    
                      //Select the interrupt end point registers used for the CH340
                      *USBC_REG_EPIND = 1;
                      
                      //EP IN configuration
                      //Reset the end point to bulk and clear the FIFO
                      *USBC_REG_TXCSR = USBC_BP_TXCSR_D_MODE | USBC_BP_TXCSR_D_CLEAR_DATA_TOGGLE | USBC_BP_TXCSR_D_FLUSH_FIFO;

                      //For double buffering clear the FIFO again
                      *USBC_REG_TXCSR = USBC_BP_TXCSR_D_FLUSH_FIFO;

                      //Max 8 bytes per transaction
                      //This setting is dependent on the negotiated device speed
                      //Needs extra code to cope with that
                      //*USBC_REG_TXMAXP = 8;
                      *USBC_REG_TXMAXP = CDC_INTR_MAX_PACKET;

                      //The FIFO size is set based on 2^n * 8, so for 512 bytes it is 6
                      //As double buffering is used this bit is also set
                      *USBC_REG_TXFIFOSZ = PC_EP1_TXFIFOSZ;  // 2 KiB FIFO RAM: see pc_transport.h
                      
                      //The base address of the third FIFO is set to the first free address
                      *USBC_REG_TXFIFOAD = PC_EP1_TXFIFOAD;

                      //Enable the endpoint interrupts
                      *USBC_REG_INTTXE |= USBC_INTTX_FLAG_EP1;
                      
                      //Select the data end point registers used for the CH340
                      *USBC_REG_EPIND = 2;

                      //EP OUT configuration                      
                      //Reset the end point to bulk end point and clear the FIFO
                      *USBC_REG_RXCSR = USBC_BP_RXCSR_D_CLEAR_DATA_TOGGLE | USBC_BP_RXCSR_D_FLUSH_FIFO;
                      
                      //For double buffering clear the FIFO again
                      *USBC_REG_RXCSR = USBC_BP_RXCSR_D_FLUSH_FIFO;
                        
                      //Max 512 bytes per transaction
                      //This setting is dependent on the negotiated device speed
                      //Needs extra code to cope with that
                      *USBC_REG_RXMAXP = CDC_BULK_MAX_PACKET;
                      //*USBC_REG_RXMAXP = max_packet;
    
                      //The FIFO size is set based on 2^n * 8, so for 512 bytes it is 6
                      //As double buffering is used this bit is also set
                      *USBC_REG_RXFIFOSZ = PC_EP2_RXFIFOSZ;
                      // nastav RX FIFO size podľa vyjednanej veľkosti
                      //*USBC_REG_RXFIFOSZ = USBC_BP_RXFIFOSZ_DPB | fifo_n;
    
                      //The base address of the first FIFO is set to zero
                      *USBC_REG_RXFIFOAD = PC_EP2_RXFIFOAD;  // above the fixed 64-byte EP0 FIFO
                      
                      // vypočítaj základ pre TX FIFO (RX zaberie rx_fifo_bytes)
                      // register očakáva adresu v jednotkách 8B (?) - ak používaš bytes, zachovaj pôvodné konvencie v driveri
                      //tx_fifo_base_for_ep2 = rx_fifo_bytes; // jednoduché rozmiestnenie pamäte za RX

                      //EP IN configuration
                      //Reset the end point to bulk and clear the FIFO
                      *USBC_REG_TXCSR = USBC_BP_TXCSR_D_MODE | USBC_BP_TXCSR_D_CLEAR_DATA_TOGGLE | USBC_BP_TXCSR_D_FLUSH_FIFO;

                      //For double buffering clear the FIFO again
                      *USBC_REG_TXCSR = USBC_BP_TXCSR_D_FLUSH_FIFO;

                      //Max 512 bytes per transaction
                      //This setting is dependent on the negotiated device speed
                      //Needs extra code to cope with that
                      *USBC_REG_TXMAXP = CDC_BULK_MAX_PACKET;
                     //*USBC_REG_TXMAXP = max_packet;

                      //The FIFO size is set based on 2^n * 8, so for 512 bytes it is 6
                      //As double buffering is used this bit is also set
                      *USBC_REG_TXFIFOSZ = PC_EP2_TXFIFOSZ;
                                            
                      // nastaviť TX FIFO velkost rovnakú ako RX (alebo inak, podľa potreby)
                      //*USBC_REG_TXFIFOSZ = USBC_BP_TXFIFOSZ_DPB | fifo_n;
                      //*USBC_REG_TXFIFOAD = tx_fifo_base_for_ep2; // posuň TX za RX blok
                      
                      //The base address of the second FIFO is set to the first free address (twice the receive FIFO size / 8)
                      *USBC_REG_TXFIFOAD = PC_EP2_TXFIFOAD;

                      //Enable the endpoint interrupts
                      *USBC_REG_INTRXE |= USBC_INTRX_FLAG_EP2;
                      *USBC_REG_INTTXE |= USBC_INTTX_FLAG_EP2;
                      pc_usb_isr_configured();
  
                      //Switch back to EP0
                      *USBC_REG_EPIND = 0;

                      //Send a null packet to acknowledge configuration
                      *USBC_REG_CSR0 = USBC_BP_CSR0_D_TX_PKT_READY;
  
                      break;

                    case USB_REQ_CLEAR_FEATURE:
                      //Might need more filtering. Looking at tinyusb project things are done differently
                      //This is extra in the scope. Not sure if it is really needed
                      
                      //Select the intended endpoint
                      *USBC_REG_EPIND = usb_setup_packet.packet.wIndex & 0x03;
                      
                      //Check the type of endpoint to halt
                      if(usb_setup_packet.packet.wIndex & 0x80)
                      {
                        //Transmit endpoint
                        *USBC_REG_TXCSR = USBC_BP_TXCSR_D_MODE | USBC_BP_TXCSR_D_CLEAR_DATA_TOGGLE | USBC_BP_TXCSR_D_FLUSH_FIFO;
                      }
                      else
                      {
                        //Receive endpoint
                        *USBC_REG_RXCSR = USBC_BP_RXCSR_D_CLEAR_DATA_TOGGLE | USBC_BP_RXCSR_D_FLUSH_FIFO;
                      }
                      
                      //Switch back to EP0
                      *USBC_REG_EPIND = 0;

                      //The FIFO of a CDC data endpoint was flushed: tell the transport
                      if((usb_setup_packet.packet.wIndex & 0x0F) == 2)
                        pc_usb_isr_ep_cleared(usb_setup_packet.packet.wIndex & 0x8F);
                      break;
                  } //end switch(usb_setup_packet.packet.bRequest)
                  break;

//********************************************************************************************************
                  case USB_TYPE_CLASS:   
                    // CDC: SET_LINE_CODING
                    if (usb_setup_packet.packet.bRequest == 0x20) 
                      {  
                        usb_ep0_state = EP0_OUT_DATA_PHASE;
                        //usb_cdc_set_line_coding(); 
                        //*USBC_REG_CSR0 = USBC_BP_CSR0_D_SERVICED_RX_PKT_READY;     
                        //*USBC_REG_CSR0 = USBC_BP_CSR0_D_DATA_END;
                        //usb_ep0_state = EP0_IDLE;
                        break;
                      }
                    
                    // CDC: GET_LINE_CODING 
                    if(usb_setup_packet.packet.bRequest == 0x21)
                    {
                      usb_cdc_get_line_coding();
                      break;
                    } 
                    
                    // CDC: SET_CONTROL_LINE_STATE
                    if (usb_setup_packet.packet.bRequest == 0x22) 
                    {  
                      usb_cdc_set_control_line_state(usb_setup_packet.packet.wValue);
                      // Signal status stage (ACK) - host očakáva status stage -> posielame DATA END
                      *USBC_REG_CSR0 = USBC_BP_CSR0_D_DATA_END;
                      usb_ep0_state = EP0_IDLE;
                      break;
                    }  

                break; 
                
//**********************************************************************************************************
                  
                case USB_TYPE_VENDOR:
                  //For mass storage check if this is a max LUN request
                  if(usb_setup_packet.packet.bRequest == 0x5F)
                  {
                    ep0_data_length  = sizeof(vendorVersion);
                    ep0_data_pointer = (uint8 *)&vendorVersion;
                  }
                  else if(usb_setup_packet.packet.bRequest == 0x95)
                  {
                    if(usb_setup_packet.packet.wValue == 0x2518)
                    {
                      ep0_data_length  = sizeof(vendorAttach);
                      ep0_data_pointer = (uint8 *)&vendorAttach;
                    }
                    else if((usb_setup_packet.packet.wValue == 0x0706) || (usb_setup_packet.packet.wValue == 0x0005))
                    {
                      ep0_data_length  = sizeof(vendorStatus);
                      ep0_data_pointer = (uint8 *)&vendorStatus;
                    }
                  }
                  break;
              } //end switch(usb_setup_packet.packet.bRequestType & USB_TYPE_MASK)
            
              //Check if there is data to send
              if(ep0_data_length)
              {
                //Limit the length on what the host allows
                if(ep0_data_length > usb_setup_packet.packet.wLength)
                {
                  ep0_data_length = usb_setup_packet.packet.wLength;
                }

                //Need a separate variable for checking if the data length is more then the end point FIFO size
                uint32 length = ep0_data_length;

                //Check if more data then the FIFO can hold
                if(length > USB_EP0_FIFO_SIZE)
                {
                  //Limit it to the FIFO size
                  length = USB_EP0_FIFO_SIZE;
                }

                //Write the data to the FIFO
                usb_write_to_fifo((void *)USBC_REG_EPFIFO0, (void *)ep0_data_pointer, length);

                //Take of the number of bytes done
                ep0_data_length -= length;

                //Signal a package ready to send
                *USBC_REG_CSR0 = USBC_BP_CSR0_D_TX_PKT_READY;

                //Check if there is more data to send to the host
                if(ep0_data_length)
                {
                  //Point to the next bit of the data that needs to be send in the next packet
                  ep0_data_pointer += length;

                  //Switch to the state to send the remainder of the data
                  usb_ep0_state = EP0_IN_DATA_PHASE;
                }
              }

              //Check if done with data. This is only the case when not switched to another state
              if(usb_ep0_state == EP0_IDLE)
              {
                //Signal done with this packet
                *USBC_REG_CSR0 = USBC_BP_CSR0_D_DATA_END;
              }
            }
            else
            {
              //Signal packet has been serviced and stall the end point
              pc_usb_isr_bad_setup(*USBC_REG_COUNT0);
              *USBC_REG_CSR0 = USBC_BP_CSR0_D_SERVICED_RX_PKT_READY | USBC_BP_CSR0_D_SEND_STALL;
            }
          }
          break;

        case EP0_OUT_DATA_PHASE:
          usb_cdc_set_line_coding();  
          break;
                
        case EP0_IN_DATA_PHASE:
          if(ep0_data_length)
          {
            uint32 length = ep0_data_length;

            //Check if more data to send then the end point FIFO can handle
            if(ep0_data_length > USB_EP0_FIFO_SIZE)
            {
              //If so limit it to the FIFO size
              length = USB_EP0_FIFO_SIZE;
            }

            //Load the data into the FIFO
            usb_write_to_fifo((void *)USBC_REG_EPFIFO0, (void *)ep0_data_pointer, length);
            
            //Take of the data length just send
            ep0_data_length -= length;

            //Signal data packet ready to send
            *USBC_REG_CSR0 = USBC_BP_CSR0_D_TX_PKT_READY;
            
            //Check if there is more data that needs to be send
            if(ep0_data_length)
            {
              //Point to the next data to send
              ep0_data_pointer += length;
            }
            else
            {
              //Signal done with sending
              *USBC_REG_CSR0 = USBC_BP_CSR0_D_DATA_END;

              //Switch back to idle state when done
              usb_ep0_state = EP0_IDLE;
            }
          }
          break;
          

      }
    }
  }    

  //Check on data received for EP2
  if(rxirq & USBC_INTRX_FLAG_EP2)
  {
    //Clear the interrupt
    *USBC_REG_INTRX = USBC_INTRX_FLAG_EP2;//*USBC_REG_INTRX = USBC_INTTX_FLAG_EP2;

    //Set EP2 as active one
    *USBC_REG_EPIND = 2;

    //PC protocol: packet(s) into the RX ring, NAK back-pressure when it is full; parsing happens in the main loop
    pc_usb_isr_rx();
  }
  
  //Check on data transmitted for EP2
  if(txirq & USBC_INTTX_FLAG_EP2)
  {
    //Clear the interrupt
    *USBC_REG_INTTX = USBC_INTTX_FLAG_EP2;
    
    //Set EP2 as active one
    *USBC_REG_EPIND = 2;

    //Check if the end point is stalled
    if(*USBC_REG_TXCSR & USBC_BP_TXCSR_D_SENT_STALL)
    {
      //Clear the stall state if so
      *USBC_REG_TXCSR &= ~(USBC_BP_TXCSR_D_SENT_STALL | USBC_BP_TXCSR_D_SEND_STALL);
    }

    //Check if the FIFO is ready for more data
    if((*USBC_REG_TXCSR & USBC_BP_TXCSR_D_TX_READY) == 0)
    {
      //Have the CDC code handle the request for data
      //usb_CDC_in_ep_callback();

    }

    //PC protocol: next packet (or the ZLP) of the staged response
    pc_usb_isr_tx();
  }
}
//******************************************************************************************


char line[128];

// extern alebo definícia štruktúry (ak nie je v .h)

extern sysparam SysParam;

uint32 a = 310;
uint32 b = 320;
uint32 c = 70;
uint32 d = 50;
uint32 e = 1000;
uint32 f = 3000;

uint32 g = 0;
uint32 h = 70;
//uint32 i = 50;
uint32 j = 182;
uint32 k = 20;



//***************************************************************************
// jednoduchá funkcia na prevod bajtu na hex znaky
char nibble_to_hex(uint8 nibble)
{
    if(nibble < 10) return '0' + nibble;
    return 'A' + (nibble - 10);
}

//***************************************************************************

//PC protocol: the text debug output path (usb_tx ring) is gone; the protocol owns the CDC data endpoint.
//Callers (menu.c debug helpers via usb_send_uint) become no-ops instead of corrupting the binary stream.
void usb_CDC_send_text(const char *text)
{
  (void)text;
}

void usb_CDC_send_data(uint8 *data, uint32 length)
{
  (void)data;
  (void)length;
}




//=====================================================================================================
// CDC: SET_LINE_CODING (0x20)
// prijíma 7 bajtov od hosta (baud rate, stop bits, parity, data bits)

void usb_cdc_set_line_coding(void)
{
    // Make sure that EP0 has RX packet ready
    if (!(*USBC_REG_CSR0 & USBC_BP_CSR0_D_RX_PKT_READY)) {
        // nothing to read, return and wait for the next interrupt
        return;
    }

    // To be safe, only mask the valid part of the count register
    uint32 cnt = *USBC_REG_COUNT0 & 0x7F;
    if (cnt != 7) {
        // unexpected length -> abort and exit
        *USBC_REG_CSR0 = USBC_BP_CSR0_D_SERVICED_RX_PKT_READY | USBC_BP_CSR0_D_SEND_STALL;
        usb_ep0_state = EP0_IDLE;
        return;
    }

    uint8 data[7];

    // Read 7 bytes: 2x 32-bit read from EPFIFO0 (second read has 3 bytes used)
    uint32 v0 = *USBC_REG_EPFIFO0;
    data[0] = (uint8)(v0 & 0xFF);
    data[1] = (uint8)((v0 >> 8) & 0xFF);
    data[2] = (uint8)((v0 >> 16) & 0xFF);
    data[3] = (uint8)((v0 >> 24) & 0xFF);

    uint32 v1 = *USBC_REG_EPFIFO0;
    data[4] = (uint8)(v1 & 0xFF);
    data[5] = (uint8)((v1 >> 8) & 0xFF);
    data[6] = (uint8)((v1 >> 16) & 0xFF);

    // Acknowledge that we've read the OUT packet
    *USBC_REG_CSR0 = USBC_BP_CSR0_D_SERVICED_RX_PKT_READY;

    // Save settings
    port_line_coding.dwDTERate   = (uint32)(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
    port_line_coding.bCharFormat = data[4];
    port_line_coding.bParityType = data[5];
    port_line_coding.bDataBits   = data[6];
    port_line_coding_flag = 1;

    // Signal status stage (ACK) - host expects status stage -> send DATA END
    *USBC_REG_CSR0 = USBC_BP_CSR0_D_DATA_END;

    // Return to IDLE
    usb_ep0_state = EP0_IDLE;
}

//------------------------------------------------------------------------------

// CDC: GET_LINE_CODING (0x21)
// The host queries the current line parameters

void usb_cdc_get_line_coding(void)
{
    ep0_data_length  = sizeof(USB_CDC_LineCoding);
    ep0_data_pointer = (uint8 *)&port_line_coding;
}

//------------------------------------------------------------------------------

// CDC: SET_CONTROL_LINE_STATE (0x22)
// bRequestType = 0x21, wValue = [bit0 = DTR, bit1 = RTS]

void usb_cdc_set_control_line_state(uint16 value) 
{
    dtr_state = value & 1;
    rts_state = (value >> 1) & 1;

    // If the host sends DTR=1, we consider it an "open" port
    if (dtr_state || rts_state) usb_connect = 1; else usb_connect = 0;

    // DTR 1 -> 0 releases REMOTE (exit_reason 4) unless IGNORE_DTR
    pc_usb_dtr(dtr_state);
}

//==============================================================================


