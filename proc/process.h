
#ifndef _PROCESS_H
#define _PROCESS_H

#include <types.h>
#include <kernel/idt.h>
#include <mm/vmm.h>
#include <fs/vfs.h>

#define MAX_PROCESSES      64
#define MAX_FDS            16
#define KERNEL_STACK_SIZE  8192

/* Terminal I/O pipe — bridges Linux shell stdout/stdin to the GUI terminal */
#define TERM_PIPE_SIZE     8192
typedef struct {
    uint8_t  buf[TERM_PIPE_SIZE];
    uint32_t head, tail, len;
} term_pipe_t;

typedef enum {
    PROC_READY   = 0,
    PROC_RUNNING = 1,
    PROC_BLOCKED = 2,
    PROC_ZOMBIE  = 3,
    PROC_UNUSED  = 4,
} proc_state_t;

#define COMPAT_NONE    0
#define COMPAT_LINUX   1
#define COMPAT_WINDOWS 2

/* Saved register context for context switching (must match switch.asm offsets) */
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8,  r9,  r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip;
    uint64_t rflags;
    uint64_t cr3;
} context_t;

typedef struct process {
    uint32_t      pid;
    char          name[64];
    proc_state_t  state;
    uint32_t      priority;

    context_t     ctx;

    pml4e_t      *page_dir;      /* PML4 for this process */
    uint64_t      kernel_stack;  /* RSP0 (top of kernel stack) */
    uint64_t      user_stack;

    uint64_t      heap_start;
    uint64_t      heap_end;

    vfs_node_t   *fds[MAX_FDS];
    uint64_t      fd_offsets[MAX_FDS];

    uint32_t      uid;
    int32_t       exit_code;
    uint8_t       compat_mode;

    void         *mem_block;
    uint64_t      mem_size;

    char          cwd[256];          /* current working directory */
    uint32_t      children[8];       /* PIDs of child processes */
    uint32_t      nchildren;
    bool          waiting_child;     /* blocked in waitpid */
    int32_t       wait_result;       /* exit code of waited child */

    /* Linux shell I/O bridge (WSL-style) */
    term_pipe_t  *stdin_pipe;        /* GUI writes here; process reads fd 0 */
    term_pipe_t  *stdout_pipe;       /* process writes fd 1/2; GUI reads here */
    bool          wait_stdin;        /* blocked waiting for stdin data */

    /* fork() resumption state — set by lx64_fork before adding child to scheduler */
    uint64_t      fork_user_rip;
    uint64_t      fork_user_rsp;
    uint64_t      fork_user_rflags;
    uint64_t      fork_tls;        /* FS base for CLONE_SETTLS threads */

    struct process *parent;
    struct process *next;
} process_t;

void       process_init(void);
process_t *process_create_kernel(void);
process_t *process_create(const char *name, uint64_t entry, uint32_t priority);
void       process_exit(int32_t code);
process_t *process_current(void);
void       process_set_current(process_t *p);
process_t *process_get(uint32_t pid);
process_t *process_create_app(const char *name, uint64_t mem_bytes);
void       process_kill(uint32_t pid);
void       process_iterate(void (*cb)(process_t *p, void *ctx), void *ctx);
void       process_child_exited(process_t *child);  /* wake waiting parent */
void       process_stdin_push(process_t *p, char c);  /* send char to process stdin */
int        process_stdout_read(process_t *p);          /* read char from process stdout (-1=empty) */
process_t *process_fork(process_t *parent, uint64_t kstack_entry);
void __attribute__((noreturn)) fork_child_complete(void);

#endif
