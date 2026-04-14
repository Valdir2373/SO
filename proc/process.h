/*
 * proc/process.h — Process Control Block (PCB) do Krypx
 * Cada processo tem sua própria stack, page directory e file descriptors.
 */
#ifndef _PROCESS_H
#define _PROCESS_H

#include <types.h>
#include <kernel/idt.h>
#include <mm/vmm.h>
#include <fs/vfs.h>

#define MAX_PROCESSES  64
#define MAX_FDS        16
#define KERNEL_STACK_SIZE  8192   /* 8 KB de stack por processo */

/* Estado do processo */
typedef enum {
    PROC_READY   = 0,
    PROC_RUNNING = 1,
    PROC_BLOCKED = 2,
    PROC_ZOMBIE  = 3,
    PROC_UNUSED  = 4,
} proc_state_t;

/* Contexto de registradores salvo no context switch */
typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip;
    uint32_t eflags;
    uint32_t cr3;
} context_t;

/* Process Control Block */
typedef struct process {
    uint32_t      pid;
    char          name[64];
    proc_state_t  state;
    uint32_t      priority;     /* 0 = maior prioridade */

    context_t     ctx;          /* Contexto salvo no context switch */

    uint32_t     *page_dir;     /* Espaço de endereçamento (CR3) */
    uint32_t      kernel_stack; /* Base da kernel stack */
    uint32_t      user_stack;   /* Topo da user stack */

    uint32_t      heap_start;
    uint32_t      heap_end;

    vfs_node_t   *fds[MAX_FDS]; /* File descriptors */
    uint32_t      fd_offsets[MAX_FDS];

    uint32_t      uid;
    int32_t       exit_code;

    struct process *parent;
    struct process *next;       /* Lista encadeada no scheduler */
} process_t;

/* Inicializa o subsistema de processos */
void process_init(void);

/* Cria o processo kernel (pid=0) */
process_t *process_create_kernel(void);

/* Cria um processo userspace */
process_t *process_create(const char *name, uint32_t entry, uint32_t priority);

/* Termina o processo atual */
void process_exit(int32_t code);

/* Retorna o processo em execução */
process_t *process_current(void);

/* Retorna processo por PID */
process_t *process_get(uint32_t pid);

#endif /* _PROCESS_H */
