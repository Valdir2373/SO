/*
 * compat/detect.h — Detecção de formato de executável
 * Identifica ELF Linux, ELF nativo Krypx e Windows PE (MZ).
 */
#ifndef _COMPAT_DETECT_H
#define _COMPAT_DETECT_H

#include <types.h>

typedef enum {
    BINARY_UNKNOWN    = 0,
    BINARY_KRYPX_ELF  = 1,   /* ELF com OSABI = 0xFF (nativo Krypx) */
    BINARY_LINUX_ELF  = 2,   /* ELF Linux i386 estático */
    BINARY_WINDOWS_PE = 3,   /* Executável Windows PE/PE32 */
} binary_type_t;

/* Inspeciona os primeiros bytes e retorna o tipo de executável */
binary_type_t detect_binary_type(const uint8_t *data, size_t size);

/* Retorna string descritiva do tipo */
const char *binary_type_name(binary_type_t type);

#endif /* _COMPAT_DETECT_H */
