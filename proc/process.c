

#include <proc/process.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <lib/string.h>
#include <drivers/vga.h>
#include <types.h>

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

static process_t *alloc_process(void) {
    uint32_t i;
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED)
            return &process_table[i];
    }
    return 0;
}

process_t *process_create_kernel(void) {
    process_t *p = alloc_process();
    if (!p) return 0;

    memset(p, 0, sizeof(process_t));
    p->pid        = next_pid++;
    p->state      = PROC_RUNNING;
    p->priority   = 0;
    strcpy(p->name, "kernel");
    strcpy(p->cwd,  "/");
    p->page_dir   = vmm_get_current_dir();
    p->ctx.cr3    = (uint64_t)vmm_get_current_dir();
    p->ctx.rflags = 0x202;

    current_process = p;
    return p;
}

process_t *process_create(const char *name, uint64_t entry, uint32_t priority) {
    process_t *p = alloc_process();
    if (!p) return 0;

    memset(p, 0, sizeof(process_t));
    p->pid      = next_pid++;
    p->state    = PROC_READY;
    p->priority = priority;
    strncpy(p->name, name, 63);

    uint64_t kstack = pmm_alloc_page();
    if (!kstack) return 0;
    p->kernel_stack = kstack + KERNEL_STACK_SIZE;

    p->page_dir = vmm_create_address_space();
    if (!p->page_dir) {
        pmm_free_page(kstack);
        return 0;
    }

    p->ctx.rip    = entry;
    p->ctx.rsp    = p->kernel_stack;
    p->ctx.rflags = 0x202;
    p->ctx.cr3    = (uint64_t)p->page_dir;

    strcpy(p->cwd, current_process ? current_process->cwd : "/");
    p->parent = current_process;

    
    if (current_process && current_process->nchildren < 8)
        current_process->children[current_process->nchildren++] = p->pid;

    return p;
}


void process_stdin_push(process_t *p, char c) {
    if (!p || !p->stdin_pipe) return;
    term_pipe_t *pipe = p->stdin_pipe;
    if (pipe->len < TERM_PIPE_SIZE) {
        pipe->buf[pipe->head] = (uint8_t)c;
        pipe->head = (pipe->head + 1) % TERM_PIPE_SIZE;
        pipe->len++;
    }
    if (p->wait_stdin && p->state == PROC_BLOCKED) {
        p->wait_stdin = false;
        p->state      = PROC_READY;
    }
}


int process_stdout_read(process_t *p) {
    if (!p || !p->stdout_pipe || p->stdout_pipe->len == 0) return -1;
    term_pipe_t *pipe = p->stdout_pipe;
    uint8_t c = pipe->buf[pipe->tail];
    pipe->tail = (pipe->tail + 1) % TERM_PIPE_SIZE;
    pipe->len--;
    return (int)c;
}

void process_child_exited(process_t *child) {
    if (!child || !child->parent) return;
    process_t *par = child->parent;
    if (par->waiting_child) {
        par->wait_result    = child->exit_code;
        par->waiting_child  = false;
        par->state          = PROC_READY;
    }
}

void process_exit(int32_t code) {
    if (current_process) {
        current_process->state     = PROC_ZOMBIE;
        current_process->exit_code = code;
        process_child_exited(current_process);
    }
    __asm__ volatile ("int $0x80" : : "a"((uint64_t)1), "D"((uint64_t)code));
}

process_t *process_current(void)          { return current_process; }
void        process_set_current(process_t *p) { current_process = p; }

process_t *process_create_app(const char *name, uint64_t mem_bytes) {
    process_t *p = alloc_process();
    if (!p) return 0;

    memset(p, 0, sizeof(process_t));
    p->pid        = next_pid++;
    p->state      = PROC_RUNNING;
    p->priority   = 1;
    strncpy(p->name, name, 63);
    p->uid        = 0;
    p->page_dir   = vmm_get_current_dir();
    p->ctx.cr3    = (uint64_t)p->page_dir;
    p->ctx.rflags = 0x202;

    p->mem_block = kmalloc((size_t)mem_bytes);
    p->mem_size  = p->mem_block ? mem_bytes : 0;

    return p;
}

void process_kill(uint32_t pid) {
    if (pid == 0) return;
    uint32_t i;
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid && process_table[i].state != PROC_UNUSED) {
            if (process_table[i].mem_block) {
                kfree(process_table[i].mem_block);
                process_table[i].mem_block = 0;
            }
            process_table[i].state = PROC_UNUSED;
            process_table[i].pid   = 0;
            return;
        }
    }
}

void process_iterate(void (*cb)(process_t *p, void *ctx), void *ctx) {
    uint32_t i;
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROC_UNUSED)
            cb(&process_table[i], ctx);
    }
}

process_t *process_get(uint32_t pid) {
    uint32_t i;
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROC_UNUSED &&
            process_table[i].pid == pid)
            return &process_table[i];
    }
    return 0;
}

process_t *process_fork(process_t *parent, uint64_t kstack_entry) {
    if (!parent) return 0;
    process_t *child = alloc_process();
    if (!child) return 0;

    
    *child = *parent;

    child->pid        = next_pid++;
    child->state      = PROC_READY;
    child->parent     = parent;
    child->nchildren  = 0;
    child->next       = 0;
    child->waiting_child = false;
    child->wait_result   = 0;

    
    uint64_t kpage = pmm_alloc_page();
    if (!kpage) { child->state = PROC_UNUSED; return 0; }
    child->kernel_stack = kpage + KERNEL_STACK_SIZE;

    
    child->page_dir = vmm_clone_address_space(parent->page_dir);
    if (!child->page_dir) {
        pmm_free_page(kpage);
        child->state = PROC_UNUSED;
        return 0;
    }

    
    child->ctx.rip    = kstack_entry;
    child->ctx.rsp    = child->kernel_stack;
    child->ctx.cr3    = (uint64_t)child->page_dir;
    child->ctx.rflags = 0x202;

    
    if (parent->nchildren < 8)
        parent->children[parent->nchildren++] = child->pid;

    return child;
}

void __attribute__((noreturn)) fork_child_complete(void) {
    process_t *p = process_current();
    uint64_t rip    = p->fork_user_rip;
    uint64_t rflags = p->fork_user_rflags;
    uint64_t rsp    = p->fork_user_rsp;
    uint64_t tls    = p->fork_tls;

    if (tls) {
        
        uint32_t lo = (uint32_t)(tls & 0xFFFFFFFFULL);
        uint32_t hi = (uint32_t)(tls >> 32);
        __asm__ volatile ("wrmsr" : : "c"(0xC0000100U), "a"(lo), "d"(hi));
    }

    __asm__ volatile (
        "mov %0, %%rcx\n\t"
        "mov %1, %%r11\n\t"
        "mov %2, %%rsp\n\t"
        "xor %%eax, %%eax\n\t"
        "sysretq"
        :
        : "r"(rip), "r"(rflags), "r"(rsp)
        : "rcx", "r11", "rax"
    );
    __builtin_unreachable();
}
