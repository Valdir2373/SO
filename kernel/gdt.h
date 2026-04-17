
#ifndef _GDT_H
#define _GDT_H

#include <types.h>


#define GDT_NULL         0x00    
#define GDT_KERNEL_CODE  0x08    
#define GDT_KERNEL_DATA  0x10    
#define GDT_USER_CODE    0x18    
#define GDT_USER_DATA    0x20    
#define GDT_TSS          0x28    


#define GDT_ENTRIES 7
#define GDT_TLS      0x30   /* entry 6 — thread-local storage */


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
    uint32_t base;    
} __attribute__((packed)) gdt_ptr_t;


typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;    
    uint32_t ss0;     
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;


void gdt_init(void);

void tss_set_kernel_stack(uint32_t stack);

/* Set the TLS GDT entry (index 6) used by Linux binaries via set_thread_area */
void gdt_set_tls(uint32_t base);

#endif 
