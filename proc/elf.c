/*
 * proc/elf.c — ELF32 Loader
 *
 * Carrega binários ELF i386 estáticos no espaço de endereçamento de um
 * processo Krypx. O algoritmo:
 *
 *   1. Valida header ELF (magic, 32-bit, i386).
 *   2. Detecta se é Linux (OSABI 0/3) ou nativo Krypx (OSABI 0xFF).
 *   3. Para cada Program Header PT_LOAD:
 *        a. Aloca páginas físicas via PMM.
 *        b. Mapeia no page directory do processo.
 *        c. Zera a região (BSS também fica zerado).
 *        d. Copia os dados do arquivo para a memória virtual.
 *   4. Monta a stack inicial Linux-style (argc/argv/envp/auxv mínimos).
 *   5. Retorna entry_point e stack_top para o processo começar a executar.
 *
 * Todo o trabalho de escrita em memória virtual é feito DENTRO do espaço
 * de endereçamento do processo (vmm_switch_address_space), o que evita
 * precisar de truques de acesso via endereço físico.
 */

#include "elf.h"
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/string.h>
#include <drivers/vga.h>
#include <types.h>

/* Endereço virtual da stack do processo Linux (1 página = 4 KB) */
#define USER_STACK_PAGE   0xBFFFF000U
#define USER_STACK_SIZE   0x1000U      /* 4 KB inicial */

/* ================================================================
 * elf_validate — Verifica header ELF mínimo
 * ================================================================ */
bool elf_validate(const uint8_t *data, size_t size) {
    if (!data || size < sizeof(elf32_hdr_t)) return false;

    elf32_hdr_t *h = (elf32_hdr_t *)data;

    /* Magic 0x7F 'E' 'L' 'F' */
    if (h->e_ident[0] != 0x7F || h->e_ident[1] != 'E' ||
        h->e_ident[2] != 'L'  || h->e_ident[3] != 'F')
        return false;

    /* Deve ser 32-bit little-endian */
    if (h->e_ident[EI_CLASS] != ELFCLASS32)  return false;
    if (h->e_ident[EI_DATA]  != ELFDATA2LSB) return false;

    /* Arquitetura i386 */
    if (h->e_machine != EM_386) return false;

    /* Executável ou shared object */
    if (h->e_type != ET_EXEC && h->e_type != ET_DYN) return false;

    /* Tabela de program headers deve existir */
    if (h->e_phnum == 0 || h->e_phoff == 0) return false;

    return true;
}

/* ================================================================
 * load_pt_load — Mapeia e copia um segmento PT_LOAD
 *
 * Deve ser chamado com o CR3 do processo já ativo
 * (vmm_switch_address_space já chamado).
 * ================================================================ */
static int load_pt_load(process_t *proc, const uint8_t *data,
                         elf32_phdr_t *ph) {
    if (ph->p_memsz == 0) return 0;

    /* Intervalo de páginas necessárias */
    uint32_t virt_start = ph->p_vaddr & ~0xFFFU;
    uint32_t virt_end   = (ph->p_vaddr + ph->p_memsz + 0xFFFU) & ~0xFFFU;

    /* Aloca e mapeia as páginas físicas */
    uint32_t virt;
    for (virt = virt_start; virt < virt_end; virt += 0x1000U) {
        uint32_t phys = pmm_alloc_page();
        if (!phys) return -1;

        uint32_t flags = PAGE_PRESENT | PAGE_USER;
        /* Segmentos graváveis (dados, BSS) → PAGE_WRITABLE */
        if (ph->p_flags & 0x2) flags |= PAGE_WRITABLE;
        /* Código puro poderia ser read-only, mas precisamos escrever agora */
        flags |= PAGE_WRITABLE;

        vmm_map_page(proc->page_dir, virt, phys, flags);
    }

    /*
     * Agora estamos no espaço de endereçamento do processo,
     * então podemos escrever diretamente nos endereços virtuais.
     */

    /* Zera toda a região mapeada (inclui BSS) */
    memset((void *)virt_start, 0, virt_end - virt_start);

    /* Copia os bytes do arquivo (p_filesz ≤ p_memsz) */
    if (ph->p_filesz > 0) {
        memcpy((void *)ph->p_vaddr, data + ph->p_offset, ph->p_filesz);
    }

    return 0;
}

/* ================================================================
 * elf_load — Carrega ELF no processo e configura stack
 * ================================================================ */
int elf_load(process_t *proc, const uint8_t *data, size_t size,
             elf_load_result_t *result) {

    if (!elf_validate(data, size)) return -1;

    elf32_hdr_t *hdr = (elf32_hdr_t *)data;

    result->is_linux_compat = (hdr->e_ident[EI_OSABI] == ELFOSABI_SYSV  ||
                                hdr->e_ident[EI_OSABI] == ELFOSABI_LINUX);
    result->is_dynamic      = false;

    /* Verifica se tem dynamic linker */
    uint16_t i;
    for (i = 0; i < hdr->e_phnum; i++) {
        elf32_phdr_t *ph = (elf32_phdr_t *)
            (data + hdr->e_phoff + (uint32_t)i * hdr->e_phentsize);
        if (ph->p_type == PT_INTERP) {
            result->is_dynamic = true;
        }
    }

    /*
     * Salva o page directory atual (kernel) e troca para o do processo.
     * A partir daqui, vmm_map_page e os memcpy/memset operam no espaço
     * virtual do processo. O kernel continua acessível porque
     * vmm_create_address_space copia os entries de identidade do kernel.
     */
    uint32_t *saved_dir = vmm_get_current_dir();
    vmm_switch_address_space(proc->page_dir);

    /* Rastreia o fim do último segmento para calcular a base do heap */
    uint32_t segment_end = 0;

    /* Carrega todos os segmentos PT_LOAD */
    for (i = 0; i < hdr->e_phnum; i++) {
        elf32_phdr_t *ph = (elf32_phdr_t *)
            (data + hdr->e_phoff + (uint32_t)i * hdr->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;

        if (load_pt_load(proc, data, ph) != 0) {
            vmm_switch_address_space(saved_dir);
            return -1;
        }

        uint32_t seg_end = ph->p_vaddr + ph->p_memsz;
        if (seg_end > segment_end) segment_end = seg_end;
    }

    /* ============================================================
     * Configura a stack Linux-style (1 página em USER_STACK_PAGE)
     * ============================================================ */
    uint32_t stack_phys = pmm_alloc_page();
    if (!stack_phys) {
        vmm_switch_address_space(saved_dir);
        return -1;
    }
    vmm_map_page(proc->page_dir, USER_STACK_PAGE, stack_phys,
                 PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);

    /* Zera toda a página de stack */
    memset((void *)USER_STACK_PAGE, 0, USER_STACK_SIZE);

    /*
     * Layout da stack inicial (ESP aponta para argc):
     *
     *   ESP → [USER_STACK_PAGE + 0x000]  argc  = 1
     *          [USER_STACK_PAGE + 0x004]  argv[0] = ptr para nome
     *          [USER_STACK_PAGE + 0x008]  NULL    (fim do argv)
     *          [USER_STACK_PAGE + 0x00C]  NULL    (fim do envp)
     *          [USER_STACK_PAGE + 0x010]  AT_NULL (fim do auxv)
     *          [USER_STACK_PAGE + 0x014]  0
     *          ...
     *          [USER_STACK_PAGE + 0xFF0]  "nome_do_programa\0"
     */
    uint32_t name_virt = USER_STACK_PAGE + 0xFF0U;

    /* Copia nome do processo para o topo da stack page */
    size_t name_len = strlen(proc->name);
    if (name_len > 15) name_len = 15;   /* Limita tamanho */
    memcpy((void *)name_virt, proc->name, name_len);
    ((char *)name_virt)[name_len] = '\0';

    /* Escreve argc / argv / envp / auxv */
    uint32_t *sp = (uint32_t *)USER_STACK_PAGE;
    sp[0] = 1;          /* argc = 1 */
    sp[1] = name_virt;  /* argv[0] → nome do programa (endereço virtual) */
    sp[2] = 0;          /* argv[1] = NULL */
    sp[3] = 0;          /* envp[0] = NULL */
    sp[4] = 0;          /* AT_NULL type */
    sp[5] = 0;          /* AT_NULL value */

    /* Restaura o page directory do kernel */
    vmm_switch_address_space(saved_dir);

    /* Preenche resultado */
    result->entry_point    = hdr->e_entry;
    result->user_stack_top = USER_STACK_PAGE;  /* ESP = &argc */
    result->heap_base      = (segment_end + 0xFFFU) & ~0xFFFU;

    return 0;
}
