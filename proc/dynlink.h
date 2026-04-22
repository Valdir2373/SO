#ifndef _DYNLINK_H
#define _DYNLINK_H

#include <types.h>
#include <proc/process.h>


#define AT_NULL       0
#define AT_PHDR       3
#define AT_PHENT      4
#define AT_PHNUM      5
#define AT_PAGESZ     6
#define AT_BASE       7
#define AT_FLAGS      8
#define AT_ENTRY      9
#define AT_UID       11
#define AT_EUID      12
#define AT_GID       13
#define AT_EGID      14
#define AT_HWCAP     16
#define AT_CLKTCK    17
#define AT_SECURE    23
#define AT_RANDOM    25
#define AT_HWCAP2    26
#define AT_EXECFN    31


#define DYNLINK_INTERP_BASE  0x7F0000000000ULL

typedef struct {
    uint64_t interp_base;
    uint64_t interp_entry;
    uint64_t stack_pointer; 
} dynlink_result_t;

int dynlink_load(process_t *proc,
                 const char *interp_path,
                 uint64_t prog_entry,
                 uint64_t phdr_vaddr,
                 uint16_t phentsize,
                 uint16_t phnum,
                 uint64_t stack_top,
                 const char *argv0,
                 dynlink_result_t *result);

#endif
