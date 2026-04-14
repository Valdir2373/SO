/*
 * kernel/syscall.h — Interface de syscalls (int 0x80)
 * Número do syscall em EAX, argumentos em EBX, ECX, EDX.
 */
#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <types.h>
#include <kernel/idt.h>

/* Números de syscall */
#define SYS_EXIT    1
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_GETPID  9
#define SYS_SBRK   10
#define SYS_YIELD  11

/* Inicializa o handler de syscalls (registra na IDT vetor 0x80) */
void syscall_init(void);

/* Handler principal (chamado pelo ISR 128) */
void syscall_handler(registers_t *regs);

#endif /* _SYSCALL_H */
