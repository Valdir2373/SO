
#ifndef _USB_HID_H
#define _USB_HID_H

#include <types.h>

void usb_hid_init(void);
bool usb_kbd_available(void);
bool usb_mouse_available(void);
void usb_hid_poll(void);


bool usb_ctrl_req(uint8_t addr, uint8_t bmRequestType, uint8_t bRequest,
                  uint16_t wValue, uint16_t wIndex,
                  void *data, uint16_t len, bool in);
bool usb_bulk_out(uint8_t addr, uint8_t ep, const void *data, uint16_t len);
bool usb_bulk_in (uint8_t addr, uint8_t ep, void *data, uint16_t max, uint16_t *got);

#endif
