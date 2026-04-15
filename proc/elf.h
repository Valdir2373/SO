/*
 * proc/elf.h — ELF32 Loader
 * Carrega executáveis ELF i386 no espaço de endereçamento de um processo.
 * Suporta binários estáticos (sem PT_INTERP / dynamic linker).
 */
#ifndef _PROC_ELF_H
#define _PROC_ELF_H

#include <types.h>
#include <proc/process.h>

/* ================================================================
 * Estruturas ELF32 (conforme especificação System V ABI i386)
 * ================================================================ */

typedef struct {
    uint8_t  e_ident[16];   /* Magic, classe, dados, versão, OSABI... */
    uint16_t e_type;        /* ET_EXEC=2, ET_DYN=3 */
    uint16_t e_machine;     /* EM_386=3 */
    uint32_t e_version;
    uint32_t e_entry;       /* Endereço virtual de entrada (_start) */
    uint32_t e_phoff;       /* Offset da tabela de Program Headers */
    uint32_t e_shoff;       /* Offset da tabela de Section Headers */
    uint32_t e_flags;
    uint16_t e_ehsize;      /* Tamanho do ELF header */
    uint16_t e_phentsize;   /* Tamanho de cada Program Header */
    uint16_t e_phnum;       /* Número de Program Headers */
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf32_hdr_t;

typedef struct {
    uint32_t p_type;    /* PT_LOAD=1, PT_DYNAMIC=2, PT_INTERP=3 */
    uint32_t p_offset;  /* Offset do segmento no arquivo */
    uint32_t p_vaddr;   /* Endereço virtual destino */
    uint32_t p_paddr;   /* Endereço físico (ignorado em userspace) */
    uint32_t p_filesz;  /* Bytes a copiar do arquivo */
    uint32_t p_memsz;   /* Bytes em memória (>= p_filesz; diferença = BSS) */
    uint32_t p_flags;   /* PF_X=1, PF_W=2, PF_R=4 */
    uint32_t p_align;   /* Alinhamento (geralmente 0x1000) */
} __attribute__((packed)) elf32_phdr_t;

/* Tipos de Program Header */
#define PT_NULL      0
#define PT_LOAD      1
#define PT_DYNAMIC   2
#define PT_INTERP    3
#define PT_PHDR      6
#define PT_GNU_STACK 0x6474E551U

/* Tipos ELF */
#define ET_EXEC  2
#define ET_DYN   3

/* Arquitetura */
#define EM_386   3

/* Índices no e_ident */
#define EI_CLASS   4    /* 1=32bit, 2=64bit */
#define EI_DATA    5    /* 1=little-endian */
#define EI_OSABI   7

#define ELFCLASS32   1
#define ELFDATA2LSB  1
#define ELFOSABI_SYSV   0x00
#define ELFOSABI_LINUX  0x03
#define ELFOSABI_KRYPX  0xFF

/* ================================================================
 * API pública
 * ================================================================ */

typedef struct {
    uint32_t entry_point;     /* Endereço de entrada do ELF (e_entry) */
    uint32_t user_stack_top;  /* ESP inicial (aponta para argc na stack) */
    uint32_t heap_base;       /* Início do heap (logo após o último segmento) */
    bool     is_linux_compat; /* true se OSABI indica Linux */
    bool     is_dynamic;      /* true se tem PT_INTERP (dynamic linker) */
} elf_load_result_t;

/*
 * Valida magic bytes, classe 32-bit e arquitetura i386.
 * Retorna true se o ELF é carregável.
 */
bool elf_validate(const uint8_t *data, size_t size);

/*
 * Carrega o ELF no espaço de endereçamento do processo:
 *   1. Mapeia segmentos PT_LOAD com as flags corretas.
 *   2. Copia dados do arquivo e zera BSS.
 *   3. Cria stack Linux-style com argc/argv/envp mínimos.
 * Retorna 0 em sucesso, -1 em falha.
 */
int elf_load(process_t *proc, const uint8_t *data, size_t size,
             elf_load_result_t *result);

#endif /* _PROC_ELF_H */
