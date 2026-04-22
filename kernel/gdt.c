
#include <kernel/gdt.h>
#include <include/io.h>
#include <types.h>


static gdt_entry_t  gdt[GDT_ENTRIES];
static gdt_ptr_t    gdt_ptr;
static tss64_t      tss64 __attribute__((aligned(16)));

extern void gdt_flush(gdt_ptr_t *ptr);
extern void tss_flush(void);

static void gdt_set_entry(int idx, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t gran) {
    gdt[idx].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[idx].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    gdt[idx].base_high   = (uint8_t)((base >> 24) & 0xFF);
    gdt[idx].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[idx].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[idx].access      = access;
}

static void tss64_install(void) {
    uint64_t base = (uint64_t)&tss64;
    uint32_t limit = (uint32_t)sizeof(tss64_t) - 1;

    
    tss64_desc_t *desc = (tss64_desc_t *)&gdt[6];
    desc->length     = (uint16_t)limit;
    desc->base_low   = (uint16_t)(base & 0xFFFF);
    desc->base_mid   = (uint8_t)((base >> 16) & 0xFF);
    desc->flags1     = 0x89;   
    desc->flags2     = 0x00;
    desc->base_high  = (uint8_t)((base >> 24) & 0xFF);
    desc->base_upper = (uint32_t)(base >> 32);
    desc->reserved   = 0;

    
    uint8_t *p = (uint8_t *)&tss64;
    uint32_t i;
    for (i = 0; i < sizeof(tss64_t); i++) p[i] = 0;
    tss64.iomap_base = (uint16_t)sizeof(tss64_t);
}

void gdt_init(void) {
    
    gdt_ptr.limit = (uint16_t)(sizeof(gdt_entry_t) * GDT_ENTRIES - 1);
    gdt_ptr.base  = (uint64_t)&gdt;

    
    gdt_set_entry(0, 0, 0x00000, 0x00, 0x00);   
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xAF);   
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xCF);   
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xCF);   
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xCF);   
    gdt_set_entry(5, 0, 0xFFFFF, 0xFA, 0xAF);   

    tss64_install();

    gdt_flush(&gdt_ptr);
    tss_flush();
}

void tss_set_kernel_stack(uint64_t rsp0) {
    tss64.rsp0 = rsp0;
}

static inline void wrmsr_inline(uint32_t msr, uint64_t val) {
    __asm__ volatile ("wrmsr" : :
        "c"(msr),
        "a"((uint32_t)(val & 0xFFFFFFFFU)),
        "d"((uint32_t)(val >> 32))
        : "memory");
}

void set_fs_base(uint64_t base) {
    wrmsr_inline(0xC0000100, base);   
}

void set_gs_base(uint64_t base) {
    wrmsr_inline(0xC0000101, base);   
}
