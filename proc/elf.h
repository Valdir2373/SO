
#ifndef _PROC_ELF_H
#define _PROC_ELF_H

#include <types.h>
#include <proc/process.h>


typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf32_hdr_t;


typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) elf32_phdr_t;


typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_hdr_t;


typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;


#define PT_NULL      0
#define PT_LOAD      1
#define PT_DYNAMIC   2
#define PT_INTERP    3
#define PT_PHDR      6
#define PT_GNU_STACK 0x6474E551U


#define ET_EXEC  2
#define ET_DYN   3


#define EM_386    3
#define EM_X86_64 62


#define EI_CLASS   4
#define EI_DATA    5
#define EI_OSABI   7

#define ELFCLASS32   1
#define ELFCLASS64   2
#define ELFDATA2LSB  1
#define ELFOSABI_SYSV   0x00
#define ELFOSABI_LINUX  0x03
#define ELFOSABI_KRYPX  0xFF

typedef struct {
    uint64_t entry_point;
    uint64_t user_stack_top;
    uint64_t heap_base;
    bool     is_linux_compat;
    bool     is_dynamic;
} elf_load_result_t;

bool elf_validate(const uint8_t *data, size_t size);
int  elf_load(process_t *proc, const uint8_t *data, size_t size,
              elf_load_result_t *result);

#endif
