
#ifndef _VMM_H
#define _VMM_H

#include <types.h>

/* Page table entry flags */
#define PAGE_PRESENT   (1ULL << 0)
#define PAGE_WRITABLE  (1ULL << 1)
#define PAGE_USER      (1ULL << 2)
#define PAGE_NOCACHE   (1ULL << 4)
#define PAGE_2MB       (1ULL << 7)   /* 2MB page in PD level */
#define PAGE_4MB       PAGE_2MB      /* alias for old code */
#define PAGE_NX        (1ULL << 63)

#define PAGE_SIZE      4096ULL

/* Virtual address decomposition for 4-level paging */
#define PML4_INDEX(va)  (((uint64_t)(va) >> 39) & 0x1FFULL)
#define PDPT_INDEX(va)  (((uint64_t)(va) >> 30) & 0x1FFULL)
#define PD_INDEX(va)    (((uint64_t)(va) >> 21) & 0x1FFULL)
#define PT_INDEX(va)    (((uint64_t)(va) >> 12) & 0x1FFULL)

typedef uint64_t pml4e_t;
/* Alias for old code that uses page_dir_t */
typedef uint64_t page_dir_t;

void      vmm_init(void);
void      vmm_map_page(pml4e_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
void      vmm_unmap_page(pml4e_t *pml4, uint64_t virt);
uint64_t  vmm_get_physical(pml4e_t *pml4, uint64_t virt);
void      vmm_map_range(pml4e_t *pml4, uint64_t virt, uint64_t phys,
                        uint64_t size, uint64_t flags);
pml4e_t  *vmm_create_address_space(void);
pml4e_t  *vmm_clone_address_space(pml4e_t *src);   /* fork: deep-copy user pages */
void      vmm_free_user_pages(pml4e_t *pml4);       /* execve: free old user pages */
void      vmm_switch_address_space(pml4e_t *pml4);
pml4e_t  *vmm_get_current_dir(void);

#endif

