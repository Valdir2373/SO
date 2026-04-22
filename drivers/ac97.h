
#ifndef _AC97_H
#define _AC97_H

#include <types.h>


void ac97_init(void);


void ac97_set_volume(int pct);


int  ac97_get_volume(void);


bool ac97_available(void);

#endif
