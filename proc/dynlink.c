
#include "dynlink.h"
#include "elf.h"
#include <fs/vfs.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <types.h>


static const char *interp_search[] = {
    "",       
    "/lib",
    "/usr/lib",
    "/usr/lib/x86_64-linux-musl",
    0
};

static vfs_node_t *find_file(const char *path) {
    if (path[0] == '/') return vfs_resolve(path);
    char buf[256];
    int i;
    for (i = 0; interp_search[i]; i++) {
        buf[0] = 0;
        if (interp_search[i][0]) {
            strncpy(buf, interp_search[i], 200);
            strncat(buf, "/", 255 - strlen(buf));
            strncat(buf, path, 255 - strlen(buf));
        } else {
            strncpy(buf, path, 255);
        }
        vfs_node_t *n = vfs_resolve(buf);
        if (n) return n;
    }
    return 0;
}

static int load_elf64_segments(process_t *proc, const uint8_t *data, size_t size,
                                uint64_t base, uint64_t *entry_out) {
    if (!data || size < sizeof(elf64_hdr_t)) return -1;
    const elf64_hdr_t *hdr = (const elf64_hdr_t *)data;
    if (hdr->e_ident[0] != 0x7F || hdr->e_ident[1] != 'E' ||
        hdr->e_ident[2] != 'L'  || hdr->e_ident[3] != 'F') return -1;
    if (hdr->e_ident[EI_CLASS] != ELFCLASS64) return -1;

    uint16_t i;
    for (i = 0; i < hdr->e_phnum; i++) {
        const elf64_phdr_t *ph = (const elf64_phdr_t *)
            (data + hdr->e_phoff + (uint64_t)i * hdr->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0) continue;

        uint64_t vstart = (base + ph->p_vaddr) & ~0xFFFULL;
        uint64_t vend   = (base + ph->p_vaddr + ph->p_memsz + 0xFFFULL) & ~0xFFFULL;
        uint64_t v;
        for (v = vstart; v < vend; v += PAGE_SIZE) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) return -1;
            vmm_map_page(proc->page_dir, v, phys,
                         PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
        memset((void *)(uintptr_t)vstart, 0, (size_t)(vend - vstart));
        if (ph->p_filesz > 0)
            memcpy((void *)(uintptr_t)(base + ph->p_vaddr),
                   data + ph->p_offset, (size_t)ph->p_filesz);
    }

    *entry_out = base + hdr->e_entry;
    return 0;
}


static inline void push64(uint64_t **sp, uint64_t val) {
    *sp -= 1;
    **sp = val;
}

int dynlink_load(process_t *proc,
                 const char *interp_path,
                 uint64_t prog_entry,
                 uint64_t phdr_vaddr,
                 uint16_t phentsize,
                 uint16_t phnum,
                 uint64_t stack_top,
                 const char *argv0,
                 dynlink_result_t *result) {

    
    vfs_node_t *node = find_file(interp_path);
    if (!node || node->size == 0) return -1;

    uint8_t *idata = (uint8_t *)kmalloc(node->size);
    if (!idata) return -1;
    vfs_read(node, 0, node->size, idata);

    uint64_t interp_entry = 0;
    int r = load_elf64_segments(proc, idata, node->size,
                                 DYNLINK_INTERP_BASE, &interp_entry);
    kfree(idata);
    if (r != 0) return -1;

    
    

    
    uint64_t sp = stack_top & ~0xFULL;

    
    sp -= 16;
    uint64_t at_random_ptr = sp;
    {
        uint8_t *r_bytes = (uint8_t *)(uintptr_t)sp;
        uint8_t seed = 0xAB;
        int k;
        for (k = 0; k < 16; k++) { seed ^= (seed << 3) | (seed >> 5); r_bytes[k] = seed; }
    }

    
    const char *plat = "x86_64";
    sp -= (uint64_t)(strlen(plat) + 1);
    uint64_t plat_ptr = sp;
    memcpy((void *)(uintptr_t)sp, plat, strlen(plat) + 1);

    
    const char *a0 = argv0 ? argv0 : "prog";
    uint64_t a0len = strlen(a0) + 1;
    sp -= a0len;
    uint64_t argv0_ptr = sp;
    memcpy((void *)(uintptr_t)sp, a0, (size_t)a0len);

    
    sp &= ~0xFULL;

    
    
    uint64_t auxv_buf[64];
    int ai = 0;
#define AUX(t,v) do { auxv_buf[ai++]=(t); auxv_buf[ai++]=(v); } while(0)
    AUX(AT_PHDR,   phdr_vaddr);
    AUX(AT_PHENT,  (uint64_t)phentsize);
    AUX(AT_PHNUM,  (uint64_t)phnum);
    AUX(AT_PAGESZ, 4096ULL);
    AUX(AT_BASE,   DYNLINK_INTERP_BASE);
    AUX(AT_FLAGS,  0ULL);
    AUX(AT_ENTRY,  prog_entry);
    AUX(AT_UID,    0ULL);
    AUX(AT_EUID,   0ULL);
    AUX(AT_GID,    0ULL);
    AUX(AT_EGID,   0ULL);
    AUX(AT_HWCAP,  0x1F8BFBFFULL);  
    AUX(AT_CLKTCK, 100ULL);
    AUX(AT_RANDOM, at_random_ptr);
    AUX(AT_SECURE, 0ULL);
    AUX(AT_EXECFN, argv0_ptr);
    AUX(AT_HWCAP2, 0ULL);
    AUX(AT_NULL,   0ULL);
#undef AUX

    
    int nwords = 1 + 1 + 1 + 1 + ai;
    
    if (nwords % 2 == 0) nwords++;  

    sp -= (uint64_t)nwords * 8;
    sp &= ~0xFULL;
    
    
    if (sp % 16 == 0) sp -= 8;

    uint64_t *p64 = (uint64_t *)(uintptr_t)sp;
    int idx = 0;
    p64[idx++] = 1;            
    p64[idx++] = argv0_ptr;    
    p64[idx++] = 0;            
    p64[idx++] = 0;            
    int k;
    for (k = 0; k < ai; k++) p64[idx++] = auxv_buf[k];

    result->interp_base  = DYNLINK_INTERP_BASE;
    result->interp_entry = interp_entry;
    result->stack_pointer = sp;
    return 0;
}
