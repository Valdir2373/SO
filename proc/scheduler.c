/*
 * proc/scheduler.c — Round-Robin preemptivo com time slice de 20 ms
 * Chamado pelo PIT timer (IRQ0) a cada tick.
 * Context switch em Assembly puro (sem setjmp/longjmp).
 */

#include <proc/scheduler.h>
#include <proc/process.h>
#include <kernel/idt.h>
#include <kernel/timer.h>
#include <kernel/gdt.h>
#include <mm/vmm.h>
#include <types.h>

/* Ponteiro da fila circular de processos */
static process_t *run_queue  = 0;   /* Cabeça da fila */
static process_t *current    = 0;   /* Processo em execução */
static bool       enabled    = false;
static uint32_t   ticks_used = 0;   /* Ticks do time slice atual */

/* Context switch em Assembly:
 * Salva contexto de 'prev', carrega contexto de 'next'.
 * Assinatura: void context_switch(context_t *prev, context_t *next)
 */
extern void context_switch(context_t *prev, context_t *next);

void scheduler_init(void) {
    run_queue = 0;
    current   = 0;
    enabled   = false;
    ticks_used = 0;
}

void scheduler_add(process_t *proc) {
    if (!proc) return;
    proc->state = PROC_READY;

    if (!run_queue) {
        run_queue  = proc;
        proc->next = proc;   /* Fila circular */
        return;
    }

    /* Insere após a cabeça */
    process_t *last = run_queue;
    while (last->next != run_queue) last = last->next;
    last->next  = proc;
    proc->next  = run_queue;
}

void scheduler_remove(process_t *proc) {
    if (!proc || !run_queue) return;

    if (run_queue == proc && proc->next == proc) {
        run_queue = 0;
        return;
    }

    process_t *p = run_queue;
    while (p->next != proc && p->next != run_queue) p = p->next;
    if (p->next == proc) {
        p->next = proc->next;
        if (run_queue == proc) run_queue = proc->next;
    }
    proc->next = 0;
}

void scheduler_enable(void)  { enabled = true; }
void scheduler_disable(void) { enabled = false; }

/* Chamado pelo timer handler — verifica se deve trocar processo */
void schedule(void) {
    if (!enabled || !run_queue) return;

    ticks_used++;
    if (ticks_used < TIMESLICE_MS) return;
    ticks_used = 0;

    /* Procura próximo processo READY */
    process_t *next = (current && current->next) ? current->next : run_queue;
    process_t *start = next;

    do {
        if (next->state == PROC_READY || next->state == PROC_RUNNING) break;
        next = next->next;
    } while (next != start);

    if (next == current) return;  /* Só um processo ativo */

    /* Troca de processo */
    process_t *prev = current;
    current = next;

    if (prev) prev->state = PROC_READY;
    current->state = PROC_RUNNING;

    /* Atualiza ESP0 do TSS para a kernel stack do novo processo */
    tss_set_kernel_stack(current->kernel_stack);

    /* Troca page directory */
    if (current->ctx.cr3 != prev->ctx.cr3) {
        vmm_switch_address_space((uint32_t *)current->ctx.cr3);
    }

    /* Context switch: salva prev, carrega next */
    context_switch(&prev->ctx, &current->ctx);
}
