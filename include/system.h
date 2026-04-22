
#ifndef _SYSTEM_H
#define _SYSTEM_H

#include <types.h>

#define KRYPX_VERSION_MAJOR  0
#define KRYPX_VERSION_MINOR  1
#define KRYPX_VERSION_PATCH  0
#define KRYPX_VERSION_STR    "0.1.0"
#define KRYPX_NAME           "Krypx"

#define KERNEL_VIRTUAL_BASE  0x00100000ULL
#define KERNEL_HEAP_START    0x800000ULL
#define KERNEL_HEAP_SIZE     (16ULL * 1024ULL * 1024ULL)


#ifndef PAGE_SIZE
#define PAGE_SIZE            4096ULL
#define PAGE_SHIFT           12
#endif

#define ALIGN_UP(x, align)   (((x) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))
#define PAGE_ALIGN(x)        ALIGN_UP(x, PAGE_SIZE)

#define ARRAY_SIZE(arr)      (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b)            ((a) < (b) ? (a) : (b))
#define MAX(a, b)            ((a) > (b) ? (a) : (b))
#define ABS(x)               ((x) < 0 ? -(x) : (x))

static inline void cpu_halt(void) { __asm__ volatile ("cli; hlt"); }
static inline void cli(void)      { __asm__ volatile ("cli"); }
static inline void sti(void)      { __asm__ volatile ("sti"); }

static inline uint64_t read_cr2(void) {
    uint64_t val;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(val));
    return val;
}

static inline uint64_t read_cr3(void) {
    uint64_t val;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline void write_cr3(uint64_t val) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(val));
}

void kernel_main(uint64_t magic, uint64_t mbi_addr);
void kernel_panic(const char *msg);

#endif
