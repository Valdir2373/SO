
#ifndef _GDT_H
#define _GDT_H

#include <types.h>


#define GDT_NULL         0x00
#define GDT_KERNEL_CODE  0x08   
#define GDT_KERNEL_DATA  0x10   
#define GDT_USER_CODE32  0x18   
#define GDT_USER_DATA    0x20   
#define GDT_USER_CODE64  0x28   
#define GDT_TSS          0x30   

#define GDT_ENTRIES 8           


typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;


typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;


typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;          
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];        
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss64_t;


typedef struct {
    uint16_t length;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  flags1;        
    uint8_t  flags2;        
    uint8_t  base_high;
    uint32_t base_upper;    
    uint32_t reserved;
} __attribute__((packed)) tss64_desc_t;

void gdt_init(void);
void tss_set_kernel_stack(uint64_t rsp0);


void set_fs_base(uint64_t base);
void set_gs_base(uint64_t base);

#endif

