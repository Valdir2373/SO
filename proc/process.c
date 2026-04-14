/*
 * proc/process.c — Criação e gerenciamento de processos
 */

#include <proc/process.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <lib/string.h>
#include <drivers/vga.h>
#include <types.h>

/* Tabela de processos */
static process_t process_table[MAX_PROCESSES];
static process_t *current_process = 0;
static uint32_t  next_pid = 0;

void process_init(void) {
    uint32_t i;
    for (i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].state = PROC_UNUSED;
        process_table[i].pid   = 0;
    }
    current_process = 0;
    next_pid = 0;
}

/* Aloca uma entrada livre na tabela */
static process_t *alloc_process(void) {
    uint32_t i;
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED) {
            return &process_table[i];
        }
    }
    return 0;
}

process_t *process_create_kernel(void) {
    process_t *p = alloc_process();
    if (!p) return 0;

    memset(p, 0, sizeof(process_t));
    p->pid      = next_pid++;
    p->state    = PROC_RUNNING;
    p->priority = 0;
    strcpy(p->name, "kernel");
    p->page_dir = vmm_get_current_dir();

    current_process = p;
    return p;
}

process_t *process_create(const char *name, uint32_t entry, uint32_t priority) {
    process_t *p = alloc_process();
    if (!p) return 0;

    memset(p, 0, sizeof(process_t));
    p->pid      = next_pid++;
    p->state    = PROC_READY;
    p->priority = priority;
    strncpy(p->name, name, 63);

    /* Aloca kernel stack */
    uint32_t kstack = pmm_alloc_page();
    if (!kstack) return 0;
    p->kernel_stack = kstack + KERNEL_STACK_SIZE;

    /* Cria espaço de endereçamento */
    p->page_dir = vmm_create_address_space();
    if (!p->page_dir) {
        pmm_free_page(kstack);
        return 0;
    }

    /*
     * Configura o contexto inicial:
     * Quando o scheduler fizer o primeiro switch para este processo,
     * vai restaurar esses registradores e pular para entry.
     */
    p->ctx.eip    = entry;
    p->ctx.esp    = p->kernel_stack;
    p->ctx.eflags = 0x202;  /* IF=1 (interrupções habilitadas) */
    p->ctx.cr3    = (uint32_t)p->page_dir;

    p->parent = current_process;
    return p;
}

void process_exit(int32_t code) {
    if (current_process) {
        current_process->state     = PROC_ZOMBIE;
        current_process->exit_code = code;
    }
    /* O scheduler vai limpar e escolher o próximo */
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(code));
}

process_t *process_current(void) { return current_process; }

process_t *process_get(uint32_t pid) {
    uint32_t i;
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROC_UNUSED &&
            process_table[i].pid == pid) {
            return &process_table[i];
        }
    }
    return 0;
}
