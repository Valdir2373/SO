
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/string.h>
#include <drivers/vga.h>
#include <system.h>
#include <types.h>

/* Kernel PML4 — 4-level page tables, 4KB aligned */
static pml4e_t kernel_pml4[512] __attribute__((aligned(4096)));
static uint64_t kernel_pdpt[512] __attribute__((aligned(4096)));
static uint64_t kernel_pd[512]   __attribute__((aligned(4096)));

static pml4e_t *current_dir = 0;

/* Allocate and zero a 4KB page table (returns pointer = physical address,
 * since we identity-map all RAM) */
static uint64_t *alloc_table(void) {
    uint64_t phys = pmm_alloc_page();
    if (!phys) return 0;
    uint64_t *t = (uint64_t *)phys;
    uint32_t i;
    for (i = 0; i < 512; i++) t[i] = 0;
    return t;
}

void vmm_map_page(pml4e_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t i4 = PML4_INDEX(virt);
    uint64_t i3 = PDPT_INDEX(virt);
    uint64_t i2 = PD_INDEX(virt);
    uint64_t i1 = PT_INDEX(virt);

    /* PML4 → PDPT */
    if (!(pml4[i4] & PAGE_PRESENT)) {
        uint64_t *pdpt = alloc_table();
        if (!pdpt) return;
        pml4[i4] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    uint64_t *pdpt = (uint64_t *)(pml4[i4] & ~0xFFFULL);

    /* PDPT → PD */
    if (!(pdpt[i3] & PAGE_PRESENT)) {
        uint64_t *pd = alloc_table();
        if (!pd) return;
        pdpt[i3] = (uint64_t)pd | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    uint64_t *pd = (uint64_t *)(pdpt[i3] & ~0xFFFULL);

    /* PD: if it's a 2MB page, split into 4KB entries first */
    if (pd[i2] & PAGE_2MB) {
        uint64_t base2mb = pd[i2] & ~((uint64_t)0x1FFFFF);
        uint64_t *pt = alloc_table();
        if (!pt) return;
        uint32_t k;
        for (k = 0; k < 512; k++)
            pt[k] = (base2mb + k * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITABLE;
        pd[i2] = (uint64_t)pt | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }

    /* PD → PT */
    if (!(pd[i2] & PAGE_PRESENT)) {
        uint64_t *pt = alloc_table();
        if (!pt) return;
        pd[i2] = (uint64_t)pt | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    uint64_t *pt = (uint64_t *)(pd[i2] & ~0xFFFULL);

    /* PT → page */
    pt[i1] = (phys & ~0xFFFULL) | PAGE_PRESENT | flags;

    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_unmap_page(pml4e_t *pml4, uint64_t virt) {
    uint64_t i4 = PML4_INDEX(virt);
    uint64_t i3 = PDPT_INDEX(virt);
    uint64_t i2 = PD_INDEX(virt);
    uint64_t i1 = PT_INDEX(virt);

    if (!(pml4[i4] & PAGE_PRESENT)) return;
    uint64_t *pdpt = (uint64_t *)(pml4[i4] & ~0xFFFULL);
    if (!(pdpt[i3] & PAGE_PRESENT)) return;
    uint64_t *pd = (uint64_t *)(pdpt[i3] & ~0xFFFULL);
    if (!(pd[i2] & PAGE_PRESENT)) return;
    uint64_t *pt = (uint64_t *)(pd[i2] & ~0xFFFULL);
    pt[i1] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

uint64_t vmm_get_physical(pml4e_t *pml4, uint64_t virt) {
    uint64_t i4 = PML4_INDEX(virt);
    uint64_t i3 = PDPT_INDEX(virt);
    uint64_t i2 = PD_INDEX(virt);
    uint64_t i1 = PT_INDEX(virt);

    if (!(pml4[i4] & PAGE_PRESENT)) return 0;
    uint64_t *pdpt = (uint64_t *)(pml4[i4] & ~0xFFFULL);
    if (!(pdpt[i3] & PAGE_PRESENT)) return 0;
    uint64_t *pd = (uint64_t *)(pdpt[i3] & ~0xFFFULL);
    if (!(pd[i2] & PAGE_PRESENT)) return 0;
    if (pd[i2] & PAGE_2MB)
        return (pd[i2] & ~(uint64_t)0x1FFFFF) | (virt & 0x1FFFFF);
    uint64_t *pt = (uint64_t *)(pd[i2] & ~0xFFFULL);
    if (!(pt[i1] & PAGE_PRESENT)) return 0;
    return (pt[i1] & ~0xFFFULL) | (virt & 0xFFF);
}

void vmm_map_range(pml4e_t *pml4, uint64_t virt, uint64_t phys,
                   uint64_t size, uint64_t flags) {
    uint64_t offset;
    for (offset = 0; offset < size; offset += PAGE_SIZE)
        vmm_map_page(pml4, virt + offset, phys + offset, flags);
}

/* Create a new address space (copy kernel mappings from kernel_pml4 entry 0) */
pml4e_t *vmm_create_address_space(void) {
    uint64_t phys = pmm_alloc_page();
    if (!phys) return 0;
    pml4e_t *pml4 = (pml4e_t *)phys;
    uint32_t i;
    for (i = 0; i < 512; i++) pml4[i] = 0;
    /* Share the kernel's first PML4 entry (identity map 0–4 GB) */
    pml4[0] = kernel_pml4[0];
    return pml4;
}

/* Deep-copy all user-space pages (PAGE_USER set) from src into a new PML4.
 * Kernel mappings (entry 0, non-user pages) are shared, not copied. */
pml4e_t *vmm_clone_address_space(pml4e_t *src_pml4) {
    if (!src_pml4) return 0;
    pml4e_t *dst = vmm_create_address_space();
    if (!dst) return 0;

    uint32_t i4, i3, i2, i1;
    for (i4 = 0; i4 < 512; i4++) {
        if (!(src_pml4[i4] & PAGE_PRESENT)) continue;
        uint64_t *src_pdpt = (uint64_t *)(src_pml4[i4] & ~0xFFFULL);
        for (i3 = 0; i3 < 512; i3++) {
            if (!(src_pdpt[i3] & PAGE_PRESENT)) continue;
            uint64_t *src_pd = (uint64_t *)(src_pdpt[i3] & ~0xFFFULL);
            for (i2 = 0; i2 < 512; i2++) {
                if (!(src_pd[i2] & PAGE_PRESENT)) continue;
                if (src_pd[i2] & PAGE_2MB) {
                    /* 2MB page — only copy user pages, split into 4KB */
                    if (!(src_pd[i2] & PAGE_USER)) continue;
                    uint64_t src_base = src_pd[i2] & ~(uint64_t)0x1FFFFF;
                    uint64_t virt_base = ((uint64_t)i4 << 39) | ((uint64_t)i3 << 30) | ((uint64_t)i2 << 21);
                    uint64_t flags = (src_pd[i2] & 0xFFF) & ~(uint64_t)PAGE_2MB;
                    for (i1 = 0; i1 < 512; i1++) {
                        uint64_t np = pmm_alloc_page();
                        if (!np) return dst;
                        memcpy((void *)(uintptr_t)np, (void *)(uintptr_t)(src_base + i1 * PAGE_SIZE), (uint32_t)PAGE_SIZE);
                        vmm_map_page(dst, virt_base + i1 * PAGE_SIZE, np, flags);
                    }
                    continue;
                }
                uint64_t *src_pt = (uint64_t *)(src_pd[i2] & ~0xFFFULL);
                for (i1 = 0; i1 < 512; i1++) {
                    if (!(src_pt[i1] & PAGE_PRESENT)) continue;
                    if (!(src_pt[i1] & PAGE_USER)) continue;
                    uint64_t src_phys = src_pt[i1] & ~0xFFFULL;
                    uint64_t np = pmm_alloc_page();
                    if (!np) return dst;
                    memcpy((void *)(uintptr_t)np, (void *)(uintptr_t)src_phys, (uint32_t)PAGE_SIZE);
                    uint64_t virt = ((uint64_t)i4 << 39) | ((uint64_t)i3 << 30) |
                                    ((uint64_t)i2 << 21) | ((uint64_t)i1 << 12);
                    vmm_map_page(dst, virt, np, src_pt[i1] & 0xFFF);
                }
            }
        }
    }
    return dst;
}

/* Free all user-space pages mapped in pml4 (called by execve to replace address space). */
void vmm_free_user_pages(pml4e_t *pml4) {
    if (!pml4) return;
    uint32_t i4, i3, i2, i1;
    for (i4 = 0; i4 < 512; i4++) {
        if (!(pml4[i4] & PAGE_PRESENT)) continue;
        uint64_t *pdpt = (uint64_t *)(pml4[i4] & ~0xFFFULL);
        for (i3 = 0; i3 < 512; i3++) {
            if (!(pdpt[i3] & PAGE_PRESENT)) continue;
            uint64_t *pd = (uint64_t *)(pdpt[i3] & ~0xFFFULL);
            for (i2 = 0; i2 < 512; i2++) {
                if (!(pd[i2] & PAGE_PRESENT)) continue;
                if (pd[i2] & PAGE_2MB) {
                    if (pd[i2] & PAGE_USER) {
                        pmm_free_page(pd[i2] & ~(uint64_t)0x1FFFFF);
                        pd[i2] = 0;
                    }
                    continue;
                }
                uint64_t *pt = (uint64_t *)(pd[i2] & ~0xFFFULL);
                for (i1 = 0; i1 < 512; i1++) {
                    if ((pt[i1] & PAGE_PRESENT) && (pt[i1] & PAGE_USER)) {
                        pmm_free_page(pt[i1] & ~0xFFFULL);
                        pt[i1] = 0;
                    }
                }
            }
        }
    }
}

void vmm_switch_address_space(pml4e_t *pml4) {
    current_dir = pml4;
    __asm__ volatile ("mov %0, %%cr3" : : "r"((uint64_t)pml4) : "memory");
}

pml4e_t *vmm_get_current_dir(void) {
    return current_dir;
}

void vmm_init(void) {
    uint32_t i;
    for (i = 0; i < 512; i++) {
        kernel_pml4[i] = 0;
        kernel_pdpt[i] = 0;
        kernel_pd[i]   = 0;
    }

    /* Identity map 0–4 GB using 2MB pages:
     * kernel_pml4[0] → kernel_pdpt → 4 × kernel_pd (via 4 PDPT entries × 512 × 2MB) */
    /* One PDPT entry covers 1 GB; we need 4 entries for 4 GB */
    /* Use single kernel_pdpt with entries 0-3 each pointing to a separate PD */
    /* For simplicity, use four inline static PDs */
    static uint64_t kpd0[512] __attribute__((aligned(4096)));
    static uint64_t kpd1[512] __attribute__((aligned(4096)));
    static uint64_t kpd2[512] __attribute__((aligned(4096)));
    static uint64_t kpd3[512] __attribute__((aligned(4096)));
    /* Fill each PD with 512 × 2MB identity entries */
    for (i = 0; i < 512; i++) {
        kpd0[i] = ((uint64_t)i << 21) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_2MB;
        kpd1[i] = (((uint64_t)512 + i) << 21) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_2MB;
        kpd2[i] = (((uint64_t)1024 + i) << 21) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_2MB;
        kpd3[i] = (((uint64_t)1536 + i) << 21) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_2MB;
    }
    kernel_pdpt[0] = (uint64_t)kpd0 | PAGE_PRESENT | PAGE_WRITABLE;
    kernel_pdpt[1] = (uint64_t)kpd1 | PAGE_PRESENT | PAGE_WRITABLE;
    kernel_pdpt[2] = (uint64_t)kpd2 | PAGE_PRESENT | PAGE_WRITABLE;
    kernel_pdpt[3] = (uint64_t)kpd3 | PAGE_PRESENT | PAGE_WRITABLE;

    kernel_pml4[0] = (uint64_t)kernel_pdpt | PAGE_PRESENT | PAGE_WRITABLE;

    /* Reload CR3 with the new proper kernel PML4.
     * The boot.asm already set up a minimal identity map; we keep it but
     * switch to our managed kernel_pml4 for consistency. */
    __asm__ volatile ("mov %0, %%cr3" : : "r"((uint64_t)kernel_pml4) : "memory");
    current_dir = kernel_pml4;
}

