
#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <types.h>


#define KB_DATA_PORT    0x60
#define KB_STATUS_PORT  0x64


void keyboard_init(void);


char keyboard_getchar(void);


char keyboard_read(void);


bool keyboard_available(void);


void keyboard_inject(char c);

#endif 
