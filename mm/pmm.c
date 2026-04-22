

#include <mm/pmm.h>
#include <drivers/vga.h>
#include <types.h>

extern uint64_t kernel_end;


#define MAX_PAGES    (1024ULL * 1024ULL)
#define BITMAP_SIZE  (MAX_PAGES / 32ULL)

static uint32_t bitmap[BITMAP_SIZE];
static uint64_t total_pages = 0;
static uint64_t free_pages  = 0;

static inline void bitmap_set(uint64_t page) {
    bitmap[page / 32] |= (1u << (page % 32));
}

static inline void bitmap_clear(uint64_t page) {
    bitmap[page / 32] &= ~(1u << (page % 32));
}

static inline int bitmap_test(uint64_t page) {
    return (bitmap[page / 32] >> (page % 32)) & 1;
}

void pmm_mark_used(uint64_t addr, uint64_t size) {
    uint64_t page_start = addr / PAGE_SIZE;
    uint64_t page_end   = (addr + size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t i;
    for (i = page_start; i < page_end && i < MAX_PAGES; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            if (free_pages > 0) free_pages--;
        }
    }
}

void pmm_mark_free(uint64_t addr, uint64_t size) {
    uint64_t page_start = addr / PAGE_SIZE;
    uint64_t page_end   = (addr + size) / PAGE_SIZE;
    uint64_t i;
    for (i = page_start; i < page_end && i < MAX_PAGES; i++) {
        if (bitmap_test(i)) {
            bitmap_clear(i);
            free_pages++;
        }
    }
}

void pmm_init(multiboot_info_t *mbi) {
    uint64_t i;

    
    for (i = 0; i < BITMAP_SIZE; i++) bitmap[i] = 0xFFFFFFFF;

    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
        multiboot_mmap_entry_t *entry = (multiboot_mmap_entry_t *)(uint64_t)mbi->mmap_addr;
        uint64_t end = (uint64_t)mbi->mmap_addr + mbi->mmap_length;

        while ((uint64_t)entry < end) {
            if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                uint64_t base = entry->base_addr;
                uint64_t len  = entry->length;
                
                if (base < 0x100000000ULL) {
                    if (base + len > 0x100000000ULL)
                        len = 0x100000000ULL - base;
                    total_pages += len / PAGE_SIZE;
                    free_pages  += len / PAGE_SIZE;
                    pmm_mark_free(base, len);
                }
            }
            entry = (multiboot_mmap_entry_t *)((uint64_t)entry + entry->size + 4);
        }
    } else if (mbi->flags & MULTIBOOT_INFO_MEMORY) {
        uint64_t upper = (uint64_t)mbi->mem_upper * 1024ULL;
        total_pages = upper / PAGE_SIZE;
        free_pages  = total_pages;
        pmm_mark_free(0x100000, upper);
    }

    
    pmm_mark_used(0x000000, 0x100000);
    uint64_t kend = (uint64_t)&kernel_end;
    if (kend > 0x100000)
        pmm_mark_used(0x100000, kend - 0x100000);
}

uint64_t pmm_alloc_page(void) {
    uint64_t i, bit;
    if (free_pages == 0) return 0;

    for (i = 0; i < BITMAP_SIZE; i++) {
        if (bitmap[i] == 0xFFFFFFFF) continue;
        for (bit = 0; bit < 32; bit++) {
            if (!((bitmap[i] >> bit) & 1)) {
                uint64_t page = i * 32 + bit;
                bitmap_set(page);
                free_pages--;
                return page * PAGE_SIZE;
            }
        }
    }
    return 0;
}

void pmm_free_page(uint64_t addr) {
    uint64_t page = addr / PAGE_SIZE;
    if (page >= MAX_PAGES) return;
    if (!bitmap_test(page)) return;
    bitmap_clear(page);
    free_pages++;
}

uint64_t pmm_get_free_pages(void)  { return free_pages; }
uint64_t pmm_get_total_pages(void) { return total_pages; }

void pmm_print_info(void) {
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts("[PMM] Total: ");
    vga_put_dec((uint32_t)(total_pages * 4));
    vga_puts(" KB  Livre: ");
    vga_put_dec((uint32_t)(free_pages * 4));
    vga_puts(" KB  Usado: ");
    vga_put_dec((uint32_t)((total_pages - free_pages) * 4));
    vga_puts(" KB\n");
}
