
#ifndef _DRIVERS_RTL8188EU_H
#define _DRIVERS_RTL8188EU_H

#include <types.h>

bool rtl8188eu_init(void);


bool rtl8188eu_present(void);


void rtl8188eu_get_mac(uint8_t buf[6]);


void rtl8188eu_scan(void);


void rtl8188eu_send_frame(const uint8_t *frame, uint16_t len);


void rtl8188eu_set_channel(uint8_t ch);


void rtl8188eu_poll(void);

#endif
