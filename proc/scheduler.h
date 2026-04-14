/*
 * proc/scheduler.h — Scheduler Round-Robin preemptivo
 * Chamado pelo timer (IRQ0) a cada tick.
 */
#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include <types.h>
#include <proc/process.h>

#define TIMESLICE_MS  20   /* 20 ms por processo (20 ticks a 1000 Hz) */

/* Inicializa o scheduler */
void scheduler_init(void);

/* Adiciona processo à fila */
void scheduler_add(process_t *proc);

/* Remove processo da fila */
void scheduler_remove(process_t *proc);

/* Executa um context switch para o próximo processo pronto */
void schedule(void);

/* Habilita/desabilita preempção */
void scheduler_enable(void);
void scheduler_disable(void);

#endif /* _SCHEDULER_H */
