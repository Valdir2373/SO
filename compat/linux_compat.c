/*
 * compat/linux_compat.c — Ambiente de execução Linux para o Krypx
 *
 * Traduz syscalls Linux i386 (int 0x80) para equivalentes Krypx.
 * Programas Linux compilados com gcc -m32 -static -nostdlib funcionam
 * diretamente — o programa roda dentro do seu próprio ambiente isolado,
 * sem tocar no SO Krypx diretamente.
 *
 * Convenção de chamada Linux i386 (int 0x80):
 *   EAX = número da syscall
 *   EBX = arg1, ECX = arg2, EDX = arg3
 *   ESI = arg4, EDI = arg5, EBP = arg6
 *   Retorno em EAX (negativo = erro)
 */

#include "linux_compat.h"
#include <proc/process.h>
#include <proc/scheduler.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <fs/vfs.h>
#include <drivers/vga.h>
#include <lib/string.h>
#include <types.h>

/* Base do heap Linux dentro do espaço de endereçamento do processo */
#define LINUX_HEAP_BASE  0x08800000U

/* Callback global para redirecionar stdout/stderr do processo Linux */
static linux_output_fn g_output_fn = 0;

void linux_compat_set_output(linux_output_fn fn) {
    g_output_fn = fn;
}

/* ================================================================
 * Handlers individuais de syscall
 * ================================================================ */

static int32_t lx_write(uint32_t fd, const char *buf, uint32_t count) {
    if (!buf || count == 0) return -LINUX_EINVAL;

    if (fd == 1 || fd == 2) {
        /* stdout / stderr → callback do terminal (ou VGA como fallback) */
        uint32_t i;
        if (g_output_fn) {
            for (i = 0; i < count; i++) g_output_fn(buf[i]);
        } else {
            for (i = 0; i < count; i++) vga_putchar(buf[i]);
        }
        return (int32_t)count;
    }

    /* FD aberto via open() — delega ao VFS */
    process_t *p = process_current();
    if (p && fd < MAX_FDS && p->fds[fd]) {
        uint32_t written = vfs_write(p->fds[fd], p->fd_offsets[fd],
                                      count, (const uint8_t *)buf);
        p->fd_offsets[fd] += written;
        return (int32_t)written;
    }
    return -LINUX_EBADF;
}

static int32_t lx_read(uint32_t fd, char *buf, uint32_t count) {
    if (!buf || count == 0) return -LINUX_EINVAL;

    process_t *p = process_current();
    if (fd == 0) {
        /* stdin → EOF por enquanto */
        return 0;
    }
    if (p && fd < MAX_FDS && p->fds[fd]) {
        int32_t n = (int32_t)vfs_read(p->fds[fd], p->fd_offsets[fd],
                                        count, (uint8_t *)buf);
        if (n > 0) p->fd_offsets[fd] += (uint32_t)n;
        return n;
    }
    return -LINUX_EBADF;
}

static int32_t lx_open(const char *path, uint32_t flags) {
    (void)flags;
    if (!path) return -LINUX_EINVAL;

    vfs_node_t *node = vfs_resolve(path);
    if (!node) return -LINUX_ENOENT;

    process_t *p = process_current();
    if (!p) return -LINUX_EINVAL;

    /* Acha FD livre (começa em 3 para preservar stdin/stdout/stderr) */
    uint32_t fd;
    for (fd = 3; fd < MAX_FDS; fd++) {
        if (!p->fds[fd]) {
            vfs_open(node, O_RDONLY);
            p->fds[fd]        = node;
            p->fd_offsets[fd] = 0;
            return (int32_t)fd;
        }
    }
    return -LINUX_ENOMEM;
}

static int32_t lx_close(uint32_t fd) {
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return -LINUX_EBADF;
    vfs_close(p->fds[fd]);
    p->fds[fd]        = 0;
    p->fd_offsets[fd] = 0;
    return 0;
}

static int32_t lx_getpid(void) {
    process_t *p = process_current();
    return p ? (int32_t)p->pid : 1;
}

static int32_t lx_exit(int32_t code) {
    process_t *p = process_current();
    if (p) {
        p->state     = PROC_ZOMBIE;
        p->exit_code = code;
    }
    schedule();
    return 0;
}

static int32_t lx_brk(uint32_t addr) {
    process_t *p = process_current();
    if (!p) return -LINUX_ENOMEM;

    /* Inicializa heap Linux se ainda não foi feito */
    if (p->heap_start == 0) {
        p->heap_start = LINUX_HEAP_BASE;
        p->heap_end   = LINUX_HEAP_BASE;
    }

    if (addr == 0) return (int32_t)p->heap_end;

    if (addr < p->heap_start) return -LINUX_ENOMEM;

    /* Mapeia páginas novas se expandindo */
    if (addr > p->heap_end) {
        uint32_t page   = (p->heap_end + 0xFFF) & ~0xFFFU;
        uint32_t target = (addr + 0xFFF)         & ~0xFFFU;
        while (page < target) {
            uint32_t phys = pmm_alloc_page();
            if (!phys) return -LINUX_ENOMEM;
            vmm_map_page(p->page_dir, page, phys,
                         PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
            page += 0x1000;
        }
    }
    p->heap_end = addr;
    return (int32_t)addr;
}

static int32_t lx_mmap2(uint32_t addr, uint32_t length, uint32_t prot,
                          uint32_t mmap_flags, int32_t fd, uint32_t pgoffset) {
    (void)prot; (void)mmap_flags; (void)fd; (void)pgoffset;
    if (length == 0) return -LINUX_EINVAL;

    uint32_t pages = (length + 0xFFFU) >> 12;
    uint32_t base  = addr ? (addr & ~0xFFFU) : 0x40000000U;

    process_t *p = process_current();
    if (!p) return -LINUX_ENOMEM;

    uint32_t i;
    for (i = 0; i < pages; i++) {
        uint32_t phys = pmm_alloc_page();
        if (!phys) return -LINUX_ENOMEM;
        vmm_map_page(p->page_dir, base + i * 0x1000, phys,
                     PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }
    return (int32_t)base;
}

/* Estrutura utsname Linux (6 campos de 65 bytes) */
typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
} linux_utsname_t;

static int32_t lx_uname(linux_utsname_t *buf) {
    if (!buf) return -LINUX_EINVAL;
    memset(buf, 0, sizeof(*buf));
    memcpy(buf->sysname,    "Linux",   6);
    memcpy(buf->nodename,   "krypx",   6);
    memcpy(buf->release,    "5.15.0",  7);
    memcpy(buf->version,    "#1 SMP",  7);
    memcpy(buf->machine,    "i686",    5);
    memcpy(buf->domainname, "(none)",  7);
    return 0;
}

/* ================================================================
 * Dispatcher principal
 * ================================================================ */

void linux_syscall_handler(registers_t *regs) {
    int32_t ret = -LINUX_ENOSYS;

    switch (regs->eax) {

        case LINUX_SYS_EXIT:
        case LINUX_SYS_EXIT_GROUP:
            ret = lx_exit((int32_t)regs->ebx);
            break;

        case LINUX_SYS_READ:
            ret = lx_read(regs->ebx, (char *)regs->ecx, regs->edx);
            break;

        case LINUX_SYS_WRITE:
            ret = lx_write(regs->ebx, (const char *)regs->ecx, regs->edx);
            break;

        case LINUX_SYS_OPEN:
            ret = lx_open((const char *)regs->ebx, regs->ecx);
            break;

        case LINUX_SYS_CLOSE:
            ret = lx_close(regs->ebx);
            break;

        case LINUX_SYS_GETPID:
            ret = lx_getpid();
            break;

        case LINUX_SYS_BRK:
            ret = lx_brk(regs->ebx);
            break;

        case LINUX_SYS_MMAP2:
            ret = lx_mmap2(regs->ebx, regs->ecx, regs->edx,
                            regs->esi, (int32_t)regs->edi, regs->ebp);
            break;

        case LINUX_SYS_UNAME:
            ret = lx_uname((linux_utsname_t *)regs->ebx);
            break;

        /*
         * Stubs silenciosos — retornam 0 (sucesso) para não quebrar
         * a inicialização de binários glibc que chamam essas syscalls
         * durante o startup antes de qualquer I/O.
         */
        case LINUX_SYS_IOCTL:
        case LINUX_SYS_ACCESS:
        case LINUX_SYS_STAT:
        case LINUX_SYS_LSTAT:
        case LINUX_SYS_FSTAT:
        case LINUX_SYS_FSTAT64:
        case LINUX_SYS_SET_THREAD_AREA:
        case LINUX_SYS_SET_TID_ADDR:
        case LINUX_SYS_CLOCK_GETTIME:
            ret = 0;
            break;

        default:
            /* Syscall desconhecida — retorna ENOSYS */
            ret = -LINUX_ENOSYS;
            break;
    }

    regs->eax = (uint32_t)ret;
}

void linux_compat_init(void) {
    /* Subsistema ativado por processo via compat_mode — sem estado global */
}
