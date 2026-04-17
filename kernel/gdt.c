

#include <kernel/gdt.h>
#include <types.h>


static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;
static tss_entry_t tss;


extern void gdt_flush(uint32_t gdt_ptr_addr);
extern void tss_flush(void);


static void gdt_set_entry(int idx, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t gran) {
    gdt[idx].base_low    = (base & 0xFFFF);
    gdt[idx].base_mid    = (base >> 16) & 0xFF;
    gdt[idx].base_high   = (base >> 24) & 0xFF;
    gdt[idx].limit_low   = (limit & 0xFFFF);
    gdt[idx].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[idx].access      = access;
}


static void tss_init(uint32_t kernel_stack) {
    uint32_t base  = (uint32_t)&tss;
    uint32_t limit = base + sizeof(tss_entry_t);

    
    gdt_set_entry(5, base, limit, 0xE9, 0x00);

    
    uint8_t *p = (uint8_t *)&tss;
    uint32_t i;
    for (i = 0; i < sizeof(tss_entry_t); i++) p[i] = 0;

    tss.ss0  = GDT_KERNEL_DATA;   
    tss.esp0 = kernel_stack;       
    tss.cs   = GDT_KERNEL_CODE | 0x3;
    tss.ss = tss.ds = tss.es = tss.fs = tss.gs = GDT_KERNEL_DATA | 0x3;
    tss.iomap_base = sizeof(tss_entry_t);
}

void gdt_init(void) {

    gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;


    gdt_set_entry(0, 0, 0x00000000, 0x00, 0x00);
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);
    /* Entry 6: TLS — initially empty, filled by gdt_set_tls() */
    gdt_set_entry(6, 0, 0xFFFFF, 0xF2, 0xCF);


    tss_init(0x90000);


    gdt_flush((uint32_t)&gdt_ptr);


    tss_flush();
}

void gdt_set_tls(uint32_t base) {
    /* User-accessible data segment for thread-local storage (DPL=3) */
    gdt_set_entry(6, base, 0xFFFFF, 0xF2, 0xCF);
    gdt_flush((uint32_t)&gdt_ptr);
}

void tss_set_kernel_stack(uint32_t stack) {
    tss.esp0 = stack;
}
