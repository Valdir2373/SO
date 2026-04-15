/*
 * compat/linux_compat.h — Camada de compatibilidade Linux para o Krypx
 *
 * Traduz syscalls Linux i386 (int 0x80) para chamadas internas do Krypx.
 * Quando process_t.compat_mode == COMPAT_LINUX, o dispatcher de syscall
 * encaminha para linux_syscall_handler() em vez do handler nativo.
 */
#ifndef _COMPAT_LINUX_H
#define _COMPAT_LINUX_H

#include <types.h>
#include <kernel/idt.h>

/*
 * Números de syscall Linux i386
 * Fonte: linux/arch/x86/entry/syscalls/syscall_32.tbl
 */
#define LINUX_SYS_EXIT            1
#define LINUX_SYS_FORK            2
#define LINUX_SYS_READ            3
#define LINUX_SYS_WRITE           4
#define LINUX_SYS_OPEN            5
#define LINUX_SYS_CLOSE           6
#define LINUX_SYS_GETPID         20
#define LINUX_SYS_ACCESS         33
#define LINUX_SYS_BRK            45
#define LINUX_SYS_IOCTL          54
#define LINUX_SYS_MUNMAP         91
#define LINUX_SYS_STAT          106
#define LINUX_SYS_LSTAT         107
#define LINUX_SYS_FSTAT         108
#define LINUX_SYS_UNAME         122
#define LINUX_SYS_MMAP2         192
#define LINUX_SYS_FSTAT64       197
#define LINUX_SYS_SET_THREAD_AREA 243
#define LINUX_SYS_EXIT_GROUP    252
#define LINUX_SYS_SET_TID_ADDR  258
#define LINUX_SYS_CLOCK_GETTIME 265

/* Erros Linux */
#define LINUX_EPERM    1
#define LINUX_ENOENT   2
#define LINUX_EBADF    9
#define LINUX_ENOMEM  12
#define LINUX_EINVAL  22
#define LINUX_ENOSYS  38

/* Callback para redirecionar saída do processo Linux (stdout/stderr).
 * Se NULL, saída vai para vga_putchar. */
typedef void (*linux_output_fn)(char c);
void linux_compat_set_output(linux_output_fn fn);

/* Inicializa o subsistema de compatibilidade Linux */
void linux_compat_init(void);

/* Handler de syscall Linux — chamado quando process->compat_mode == COMPAT_LINUX */
void linux_syscall_handler(registers_t *regs);

#endif /* _COMPAT_LINUX_H */
