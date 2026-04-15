/*
 * compat/detect.c — Detecção de formato de executável
 *
 * Inspeciona magic bytes para distinguir:
 *   - ELF  (0x7F 'E' 'L' 'F') → Linux i386 ou nativo Krypx
 *   - PE   ('M' 'Z')           → Windows PE/EXE
 *
 * O campo EI_OSABI do e_ident ELF diferencia Krypx (0xFF) de Linux (0/3).
 */

#include "detect.h"

/* Índice do byte OSABI dentro do e_ident[16] */
#define EI_OSABI        7

#define ELFOSABI_SYSV   0x00   /* System V — usado por Linux */
#define ELFOSABI_LINUX  0x03   /* Explicitamente Linux */
#define ELFOSABI_KRYPX  0xFF   /* Marcador nativo do Krypx */

binary_type_t detect_binary_type(const uint8_t *data, size_t size) {
    if (!data || size < 4) return BINARY_UNKNOWN;

    /* Verifica magic ELF: 0x7F 'E' 'L' 'F' */
    if (data[0] == 0x7F && data[1] == 'E' &&
        data[2] == 'L'  && data[3] == 'F') {

        if (size < 8) return BINARY_UNKNOWN;

        uint8_t osabi = data[EI_OSABI];
        if (osabi == ELFOSABI_KRYPX) return BINARY_KRYPX_ELF;

        /* System V (0), Linux (3) ou desconhecido → trata como Linux */
        return BINARY_LINUX_ELF;
    }

    /* Verifica magic Windows PE: 'M' 'Z' */
    if (data[0] == 'M' && data[1] == 'Z') return BINARY_WINDOWS_PE;

    return BINARY_UNKNOWN;
}

const char *binary_type_name(binary_type_t type) {
    switch (type) {
        case BINARY_KRYPX_ELF:  return "Krypx ELF (nativo)";
        case BINARY_LINUX_ELF:  return "Linux ELF i386";
        case BINARY_WINDOWS_PE: return "Windows PE/EXE";
        default:                return "Desconhecido";
    }
}
