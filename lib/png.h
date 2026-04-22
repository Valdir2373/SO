
#ifndef _PNG_H
#define _PNG_H

#include <types.h>

uint32_t *png_decode(const uint8_t *data, uint32_t size, int *out_w, int *out_h);

#endif
