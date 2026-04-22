#ifndef _X11_SERVER_H
#define _X11_SERVER_H

#include <types.h>

#define X11_SOCKET_PATH  "/tmp/.X11-unix/X0"
#define X11_DISPLAY_NUM  0

void x11_server_init(void);
void x11_server_poll(void);    

#endif
