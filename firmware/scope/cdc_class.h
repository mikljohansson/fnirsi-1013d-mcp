#ifndef CDC_CLASS_H
#define CDC_CLASS_H

#include "types.h"
#include "usb_interface.h"

/* Configuration descriptor total length.
   Adjust if you add/remove descriptors. */
#define CONFIG_CDC_DESCRIPTOR_LEN (sizeof(USB_ConfigDescriptor) + \
                                     sizeof(USB_InterfaceDescriptor) + \
                                     sizeof(USB_CDC_HeaderDescriptor) + \
                                     sizeof(USB_CDC_CallMgmtDescriptor) + \
                                     sizeof(USB_CDC_AcmDescriptor) + \
                                     sizeof(USB_CDC_UnionDescriptor) + \
                                     sizeof(USB_EndPointDescriptor) + \
                                     sizeof(USB_InterfaceDescriptor) + \
                                     (sizeof(USB_EndPointDescriptor) * 2))

// Endpoint packet sizes
// typical full-speed bulk packet size 64 and 8 
// typical High-Speed bulk packet size 512 and 8
#define CDC_BULK_MAX_PACKET   512   
#define CDC_INTR_MAX_PACKET   8

/* prototypes / externs */
extern const USB_DeviceDescriptor CDC_DevDesc;
extern const CDC_Descriptor CDC_ConfDesc;

//extern const uint8 StringLangID[];
//usb_interface.c sends sizeof() of these, so the sizes must match cdc_class.c
extern const uint8 str_vendor[32];
extern const uint8 str_ret[26];
extern const uint8 str_isernum[22];
extern const uint8 str_config[24];
extern const uint8 str_interface1[42];
extern const uint8 str_interface2[36];

extern const uint8 vendorVersion[2];
extern const uint8 vendorAttach[2];
extern const uint8 vendorStatus[2];


/* API */
int16 CDCReceive(void);
void  CDCSend(uint8 c);

void usb_CDC_send_text(const char *text);

//void usb_ch340_out_ep_callback(void *fifo, int length);
void usb_CDC_out_ep_callback(uint8 *data, int length);

void usb_CDC_in_ep_callback(void);



#endif /* CDC_CLASS_H */
