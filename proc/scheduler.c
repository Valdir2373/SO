

#include <proc/scheduler.h>
#include <proc/process.h>
#include <kernel/idt.h>
#include <kernel/timer.h>
#include <kernel/gdt.h>
#include <mm/vmm.h>
#include <include/io.h>
#include <types.h>

static process_t *run_queue  = 0;
static process_t *current    = 0;
static bool       enabled    = false;
static uint32_t   ticks_used = 0;

extern void context_switch(context_t *prev, context_t *next);

void scheduler_init(void) {
    run_queue  = 0;
    current    = 0;
    enabled    = false;
    ticks_used = 0;
}

void scheduler_add(process_t *proc) {
    if (!proc) return;

    if (!run_queue) {
        run_queue  = proc;
        proc->next = proc;
        if (!current) {
            current     = proc;
            proc->state = PROC_RUNNING;
        } else {
            proc->state = PROC_READY;
        }
        return;
    }

    proc->state = PROC_READY;
    process_t *last = run_queue;
    while (last->next != run_queue) last = last->next;
    last->next = proc;
    proc->next = run_queue;
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

void schedule(void) {
    if (!enabled || !run_queue) return;

    ticks_used++;
    if (ticks_used < TIMESLICE_MS) return;
    ticks_used = 0;

    process_t *next = (current && current->next) ? current->next : run_queue;
    process_t *start = next;

    do {
        if (next->state == PROC_READY || next->state == PROC_RUNNING) break;
        next = next->next;
    } while (next != start);

    if (next == current) return;

    
    {
        static uint32_t sw_count = 0;
        if (sw_count < 5) {
            const char pfx[] = "[SW>";
            uint32_t i;
            sw_count++;
            for (i = 0; pfx[i]; i++) {
                while (!(inb(0x3FD) & 0x20)) {}
                outb(0x3F8, pfx[i]);
            }
            while (!(inb(0x3FD) & 0x20)) {}
            outb(0x3F8, (uint8_t)('0' + next->pid / 10));
            while (!(inb(0x3FD) & 0x20)) {}
            outb(0x3F8, (uint8_t)('0' + next->pid % 10));
            while (!(inb(0x3FD) & 0x20)) {}
            outb(0x3F8, ']');
            while (!(inb(0x3FD) & 0x20)) {}
            outb(0x3F8, '\r');
            while (!(inb(0x3FD) & 0x20)) {}
            outb(0x3F8, '\n');
        }
    }

    process_t *prev = current;
    current = next;

    if (prev && prev->state != PROC_ZOMBIE) prev->state = PROC_READY;
    current->state = PROC_RUNNING;

    process_set_current(current);
    tss_set_kernel_stack(current->kernel_stack);

    
    if (current->kernel_stack) {
        extern uint64_t g_syscall_kernel_rsp;
        g_syscall_kernel_rsp = current->kernel_stack;
    }

    uint64_t new_cr3 = current->ctx.cr3;
    uint64_t old_cr3 = prev ? prev->ctx.cr3 : 0;
    if (new_cr3 && new_cr3 != old_cr3)
        vmm_switch_address_space((pml4e_t *)new_cr3);

    if (prev)
        context_switch(&prev->ctx, &current->ctx);
}
