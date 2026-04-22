
#ifndef _JPEG_H
#define _JPEG_H

#include <types.h>

uint32_t *jpeg_decode(const uint8_t *data, uint32_t size, int *out_w, int *out_h);

#endif
