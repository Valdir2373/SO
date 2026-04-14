/*
 * kernel/syscall.c — Handler de syscalls via int 0x80
 * EAX = número, EBX = arg1, ECX = arg2, EDX = arg3
 * Retorno em EAX.
 */

#include <kernel/syscall.h>
#include <kernel/idt.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <drivers/vga.h>
#include <fs/vfs.h>
#include <types.h>

/* ---- Implementações das syscalls ---- */

static int32_t sys_exit(int32_t code) {
    process_t *p = process_current();
    if (p) {
        p->state     = PROC_ZOMBIE;
        p->exit_code = code;
    }
    schedule();
    return 0;
}

static int32_t sys_write(uint32_t fd, const char *buf, uint32_t count) {
    if (fd == 1 || fd == 2) {
        /* stdout / stderr → VGA */
        uint32_t i;
        for (i = 0; i < count; i++) vga_putchar(buf[i]);
        return (int32_t)count;
    }
    /* fd real → VFS (futuramente) */
    return -1;
}

static int32_t sys_read(uint32_t fd, char *buf, uint32_t count) {
    (void)fd; (void)buf; (void)count;
    return 0;
}

static int32_t sys_getpid(void) {
    process_t *p = process_current();
    return p ? (int32_t)p->pid : 0;
}

static int32_t sys_yield(void) {
    schedule();
    return 0;
}

static int32_t sys_sbrk(int32_t increment) {
    process_t *p = process_current();
    if (!p) return -1;
    uint32_t old_end = p->heap_end;
    p->heap_end += increment;
    return (int32_t)old_end;
}

/* ---- Dispatcher ---- */

void syscall_handler(registers_t *regs) {
    int32_t ret = -1;

    switch (regs->eax) {
        case SYS_EXIT:   ret = sys_exit((int32_t)regs->ebx); break;
        case SYS_WRITE:  ret = sys_write(regs->ebx, (const char *)regs->ecx,
                                          regs->edx); break;
        case SYS_READ:   ret = sys_read(regs->ebx, (char *)regs->ecx,
                                         regs->edx); break;
        case SYS_GETPID: ret = sys_getpid(); break;
        case SYS_YIELD:  ret = sys_yield(); break;
        case SYS_SBRK:   ret = sys_sbrk((int32_t)regs->ebx); break;
        default:
            ret = -1;
            break;
    }

    regs->eax = (uint32_t)ret;
}

void syscall_init(void) {
    idt_register_handler(IRQ_SYSCALL, syscall_handler);
}
