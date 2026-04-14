/*
 * multiboot.h — Estrutura Multiboot passada pelo GRUB ao kernel
 * Conforme especificação Multiboot 1 (0x1BADB002)
 */
#ifndef _MULTIBOOT_H
#define _MULTIBOOT_H

#include <types.h>

/* Flags do campo flags da multiboot_info_t */
#define MULTIBOOT_INFO_MEMORY       0x00000001  /* mem_lower e mem_upper válidos */
#define MULTIBOOT_INFO_BOOTDEV      0x00000002  /* boot_device válido */
#define MULTIBOOT_INFO_CMDLINE      0x00000004  /* cmdline válido */
#define MULTIBOOT_INFO_MODS         0x00000008  /* mods_count e mods_addr válidos */
#define MULTIBOOT_INFO_MEM_MAP      0x00000040  /* mmap_length e mmap_addr válidos */
#define MULTIBOOT_INFO_FRAMEBUFFER  0x00001000  /* framebuffer válido */

/* Tipos de entrada do memory map */
#define MULTIBOOT_MEMORY_AVAILABLE  1
#define MULTIBOOT_MEMORY_RESERVED   2

/* Magic number que o GRUB passa em EAX */
#define MULTIBOOT_BOOTLOADER_MAGIC  0x2BADB002

typedef struct {
    uint32_t flags;

    /* Disponível se MULTIBOOT_INFO_MEMORY */
    uint32_t mem_lower;     /* KB abaixo de 1 MB */
    uint32_t mem_upper;     /* KB acima de 1 MB */

    /* Disponível se MULTIBOOT_INFO_BOOTDEV */
    uint32_t boot_device;

    /* Disponível se MULTIBOOT_INFO_CMDLINE */
    uint32_t cmdline;

    /* Disponível se MULTIBOOT_INFO_MODS */
    uint32_t mods_count;
    uint32_t mods_addr;

    /* Símbolos (não usado) */
    uint32_t syms[4];

    /* Disponível se MULTIBOOT_INFO_MEM_MAP */
    uint32_t mmap_length;
    uint32_t mmap_addr;

    uint32_t drives_length;
    uint32_t drives_addr;

    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;

    /* Campos VBE (offset 72-87) — sempre presentes no struct */
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;

    /* Disponível se MULTIBOOT_INFO_FRAMEBUFFER (offset 88+) */
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  color_info[6];
} __attribute__((packed)) multiboot_info_t;

/* Entrada do memory map do GRUB */
typedef struct {
    uint32_t size;
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
} __attribute__((packed)) multiboot_mmap_entry_t;

/* Módulo carregado pelo GRUB */
typedef struct {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t reserved;
} __attribute__((packed)) multiboot_module_t;

#endif /* _MULTIBOOT_H */
