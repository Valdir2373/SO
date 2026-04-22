
#include <kernel/syscall.h>
#include <kernel/idt.h>
#include <kernel/timer.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <drivers/vga.h>
#include <drivers/keyboard.h>
#include <fs/vfs.h>
#include <compat/linux_compat.h>
#include <compat/linux_compat64.h>
#include <lib/string.h>
#include <types.h>


extern void syscall_entry(void);
extern uint64_t g_syscall_kernel_rsp;

static inline void wrmsr64(uint32_t msr, uint64_t val) {
    __asm__ volatile ("wrmsr" : :
        "c"(msr),
        "a"((uint32_t)(val & 0xFFFFFFFFULL)),
        "d"((uint32_t)(val >> 32))
        : "memory");
}
static inline uint64_t rdmsr64(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static int64_t sys_exit(int64_t code) {
    process_t *p = process_current();
    if (p) { p->state = PROC_ZOMBIE; p->exit_code = (int32_t)code; }
    schedule();
    return 0;
}

static int64_t sys_open(const char *path, int64_t flags) {
    if (!path) return -1;
    process_t *p = process_current();
    if (!p) return -1;
    int fd = -1, i;
    for (i = 3; i < MAX_FDS; i++) {
        if (!p->fds[i]) { fd = i; break; }
    }
    if (fd < 0) return -1;

    vfs_node_t *node = vfs_resolve(path);
    if (!node) {
        if (flags & O_CREAT) {
            char name[256];
            vfs_node_t *dir = vfs_resolve_parent(path, name);
            if (!dir || !name[0]) return -1;
            if (vfs_create(dir, name, 0644) != 0) return -1;
            node = vfs_resolve(path);
        }
        if (!node) return -1;
    }

    if ((flags & O_TRUNC) && node->write) {
        node->size = 0;
        vfs_write(node, 0, 0, 0);
    }

    p->fds[fd]        = node;
    p->fd_offsets[fd] = (flags & O_APPEND) ? node->size : 0;
    vfs_open(node, (uint32_t)flags);
    return fd;
}

static int64_t sys_close(int64_t fd) {
    if (fd < 0 || fd >= MAX_FDS) return -1;
    process_t *p = process_current();
    if (!p || !p->fds[fd]) return -1;
    vfs_close(p->fds[fd]);
    p->fds[fd]        = 0;
    p->fd_offsets[fd] = 0;
    return 0;
}

static int64_t sys_read(uint64_t fd, char *buf, uint64_t count) {
    if (fd == 0) {
        uint64_t i;
        for (i = 0; i < count; i++) {
            char c = keyboard_read();
            buf[i] = c;
            if (c == '\n') { i++; break; }
        }
        return (int64_t)i;
    }
    if (fd == 1 || fd == 2) return -1;
    process_t *p = process_current();
    if (!p || fd >= (uint64_t)MAX_FDS || !p->fds[fd]) return -1;
    uint32_t n = vfs_read(p->fds[fd], (uint32_t)p->fd_offsets[fd], (uint32_t)count, (uint8_t*)buf);
    p->fd_offsets[fd] += n;
    return (int64_t)n;
}

static int64_t sys_write(uint64_t fd, const char *buf, uint64_t count) {
    if (fd == 1 || fd == 2) {
        uint64_t i;
        for (i = 0; i < count; i++) vga_putchar(buf[i]);
        return (int64_t)count;
    }
    process_t *p = process_current();
    if (!p || fd >= (uint64_t)MAX_FDS || !p->fds[fd]) return -1;
    uint32_t n = vfs_write(p->fds[fd], (uint32_t)p->fd_offsets[fd], (uint32_t)count, (const uint8_t*)buf);
    p->fd_offsets[fd] += n;
    return (int64_t)n;
}

static int64_t sys_seek(uint64_t fd, int64_t offset, uint64_t whence) {
    process_t *p = process_current();
    if (!p || fd >= (uint64_t)MAX_FDS || !p->fds[fd]) return -1;
    vfs_node_t *node = p->fds[fd];
    uint64_t pos = p->fd_offsets[fd];
    if (whence == 0)      pos = (uint64_t)offset;
    else if (whence == 1) pos = (uint64_t)((int64_t)pos + offset);
    else if (whence == 2) pos = (uint64_t)((int64_t)node->size + offset);
    p->fd_offsets[fd] = pos;
    return (int64_t)pos;
}

static int64_t sys_getpid(void) {
    process_t *p = process_current();
    return p ? (int64_t)p->pid : 0;
}

static int64_t sys_yield(void) { schedule(); return 0; }

static int64_t sys_sbrk(int64_t inc) {
    process_t *p = process_current();
    if (!p) return -1;
    uint64_t old = p->heap_end;
    p->heap_end += (uint64_t)inc;
    return (int64_t)old;
}

static int64_t sys_gettime(void) { return (int64_t)timer_get_ticks(); }

static int64_t sys_mkdir(const char *path, uint64_t mode) {
    (void)mode;
    if (!path) return -1;
    char name[256];
    vfs_node_t *dir = vfs_resolve_parent(path, name);
    if (!dir || !name[0]) return -1;
    return vfs_mkdir(dir, name, 0755);
}

static int64_t sys_unlink(const char *path) {
    if (!path) return -1;
    char name[256];
    vfs_node_t *dir = vfs_resolve_parent(path, name);
    if (!dir || !name[0]) return -1;
    return vfs_unlink(dir, name);
}

static int64_t sys_chdir(const char *path) {
    if (!path) return -1;
    vfs_node_t *node = vfs_resolve(path);
    if (!node || (node->flags & 0x7) != VFS_DIRECTORY) return -1;
    process_t *p = process_current();
    if (p) strncpy(p->cwd, path, 255);
    return 0;
}

static int64_t sys_getcwd(char *buf, uint64_t size) {
    if (!buf || size == 0) return -1;
    process_t *p = process_current();
    const char *cwd = p ? p->cwd : "/";
    uint64_t len = strlen(cwd);
    if (len + 1 > size) return -1;
    memcpy(buf, cwd, len + 1);
    return (int64_t)len;
}

static int64_t sys_waitpid(int64_t pid, int64_t *status) {
    (void)pid;
    process_t *p = process_current();
    if (!p) return -1;
    p->waiting_child = true;
    p->state = PROC_BLOCKED;
    schedule();
    
    if (status) *status = p->wait_result;
    return 0;
}

#include <proc/elf.h>
#include <compat/detect.h>
#include <mm/heap.h>

static int64_t sys_exec(const char *path, const char **argv) {
    (void)argv;
    if (!path) return -1;
    vfs_node_t *node = vfs_resolve(path);
    if (!node || node->size == 0) return -1;

    uint8_t *data = (uint8_t *)kmalloc(node->size);
    if (!data) return -1;
    vfs_read(node, 0, node->size, data);

    
    const char *pname = path;
    const char *t = path;
    while (*t) { if (*t == '/') pname = t + 1; t++; }

    process_t *proc = process_create(pname, 0, 2);
    if (!proc) { kfree(data); return -1; }

    elf_load_result_t res;
    if (elf_load(proc, data, node->size, &res) != 0) {
        kfree(data); return -1;
    }
    kfree(data);

    proc->ctx.rip    = res.entry_point;
    proc->ctx.rsp    = res.user_stack_top;
    proc->ctx.rflags = 0x202;
    proc->heap_start = res.heap_base;
    proc->heap_end   = res.heap_base;

    proc->compat_mode = COMPAT_LINUX;  

    scheduler_add(proc);
    return (int64_t)proc->pid;
}

static int64_t sys_rename(const char *src, const char *dst) {
    if (!src || !dst) return -1;
    char sname[256], dname[256];
    vfs_node_t *sdir = vfs_resolve_parent(src, sname);
    vfs_node_t *ddir = vfs_resolve_parent(dst, dname);
    if (!sdir || !ddir) return -1;
    (void)dname;
    return vfs_rename(sdir, sname, ddir, dname);
}

void syscall_handler(registers_t *regs) {
    process_t *cur = process_current();
    if (cur && cur->compat_mode == COMPAT_LINUX) {
        linux_syscall_handler(regs);
        return;
    }
    int64_t ret = -1;
    switch (regs->rax) {
        case SYS_EXIT:    ret = sys_exit((int64_t)regs->rdi); break;
        case SYS_READ:    ret = sys_read(regs->rdi, (char*)regs->rsi, regs->rdx); break;
        case SYS_WRITE:   ret = sys_write(regs->rdi, (const char*)regs->rsi, regs->rdx); break;
        case SYS_OPEN:    ret = sys_open((const char*)regs->rdi, (int64_t)regs->rsi); break;
        case SYS_CLOSE:   ret = sys_close((int64_t)regs->rdi); break;
        case SYS_GETPID:  ret = sys_getpid(); break;
        case SYS_SBRK:    ret = sys_sbrk((int64_t)regs->rdi); break;
        case SYS_YIELD:   ret = sys_yield(); break;
        case SYS_SEEK:    ret = sys_seek(regs->rdi, (int64_t)regs->rsi, regs->rdx); break;
        case SYS_GETTIME: ret = sys_gettime(); break;
        case SYS_MKDIR:   ret = sys_mkdir((const char*)regs->rdi, regs->rsi); break;
        case SYS_UNLINK:  ret = sys_unlink((const char*)regs->rdi); break;
        case SYS_CHDIR:   ret = sys_chdir((const char*)regs->rdi); break;
        case SYS_GETCWD:  ret = sys_getcwd((char*)regs->rdi, regs->rsi); break;
        case SYS_WAITPID: ret = sys_waitpid((int64_t)regs->rdi, (int64_t*)regs->rsi); break;
        case SYS_EXEC:    ret = sys_exec((const char*)regs->rdi, (const char**)regs->rsi); break;
        case SYS_RENAME:  ret = sys_rename((const char*)regs->rdi, (const char*)regs->rsi); break;
        default:          ret = -1; break;
    }
    regs->rax = (uint64_t)ret;
}

void syscall_init(void) {
    
    idt_register_handler(IRQ_SYSCALL, syscall_handler);

    

    
    uint64_t efer = rdmsr64(0xC0000080UL);
    efer |= 1ULL;   
    wrmsr64(0xC0000080UL, efer);

    
    uint64_t star = (0x0018ULL << 48) | (0x0008ULL << 32);
    wrmsr64(0xC0000081UL, star);

    
    wrmsr64(0xC0000082UL, (uint64_t)(uintptr_t)syscall_entry);

    
    wrmsr64(0xC0000084UL, 0x200ULL);

    
    linux_syscall64_init();

    
    
    {
        extern uint64_t g_syscall_kernel_rsp;
        
        static uint8_t _emergency_kstack[4096] __attribute__((aligned(16)));
        g_syscall_kernel_rsp = (uint64_t)(uintptr_t)(_emergency_kstack + 4096);
    }
}
