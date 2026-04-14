/*
 * security/aslr.h — Address Space Layout Randomization
 * Randomiza base de stack e heap de cada processo.
 */
#ifndef _ASLR_H
#define _ASLR_H

#include <types.h>

/* Aplica ASLR ao espaço de endereçamento de um processo:
 * randomiza a base do stack e do heap dentro de ranges definidos. */
void aslr_randomize(uint32_t *stack_base, uint32_t *heap_base);

/* Inicializa gerador de números pseudoaleatórios com seed baseada no timer. */
void aslr_init(uint32_t seed);

#endif /* _ASLR_H */
