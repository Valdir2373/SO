/*
 * Linux x86_64 syscall compatibility layer for Krypx.
 * Handles syscall instruction ABI (not int $0x80).
 * Covers the ~160 syscalls needed by musl/glibc statically-linked binaries
 * and the Firefox ESR startup path.
 */

#include "linux_compat64.h"
#include <io.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <fs/vfs.h>
#include <drivers/vga.h>
#include <drivers/keyboard.h>
#include <drivers/framebuffer.h>
#include <kernel/timer.h>
#include <kernel/gdt.h>
#include <lib/string.h>
#include <types.h>

/* ── errno values ────────────────────────────────────────────────────────── */
#define EPERM     1
#define ENOENT    2
#define ESRCH     3
#define ENOEXEC   8
#define EINTR    4
#define EIO      5
#define EBADF    9
#define ECHILD   10
#define EAGAIN   11
#define ENOMEM   12
#define EACCES   13
#define EFAULT   14
#define EBUSY    16
#define EEXIST   17
#define ENODEV   19
#define ENOTDIR  20
#define EISDIR   21
#define EINVAL   22
#define ENFILE   23
#define EMFILE   24
#define ENOTTY   25
#define ENOSPC   28
#define EPIPE    32
#define ERANGE   34
#define ENOSYS   38
#define ENOTEMPTY 39
#define ELOOP    40
#define ENOTSOCK 88
#define EAFNOSUPPORT 97
#define ECONNREFUSED 111
#define EWOULDBLOCK  11

#define ERR(e) (-(int64_t)(e))

/* ── Linux x86_64 syscall numbers ────────────────────────────────────────── */
#define SYS64_READ          0
#define SYS64_WRITE         1
#define SYS64_OPEN          2
#define SYS64_CLOSE         3
#define SYS64_STAT          4
#define SYS64_FSTAT         5
#define SYS64_LSTAT         6
#define SYS64_POLL          7
#define SYS64_LSEEK         8
#define SYS64_MMAP          9
#define SYS64_MPROTECT     10
#define SYS64_MUNMAP       11
#define SYS64_BRK          12
#define SYS64_RT_SIGACTION 13
#define SYS64_RT_SIGPROCMASK 14
#define SYS64_RT_SIGRETURN 15
#define SYS64_IOCTL        16
#define SYS64_PREAD64      17
#define SYS64_PWRITE64     18
#define SYS64_READV        19
#define SYS64_WRITEV       20
#define SYS64_ACCESS       21
#define SYS64_PIPE         22
#define SYS64_SELECT       23
#define SYS64_SCHED_YIELD  24
#define SYS64_MREMAP       25
#define SYS64_MADVISE      28
#define SYS64_DUP          32
#define SYS64_DUP2         33
#define SYS64_PAUSE        34
#define SYS64_NANOSLEEP    35
#define SYS64_GETPID       39
#define SYS64_SOCKET       41
#define SYS64_CONNECT      42
#define SYS64_ACCEPT       43
#define SYS64_SENDTO       44
#define SYS64_RECVFROM     45
#define SYS64_SENDMSG      46
#define SYS64_RECVMSG      47
#define SYS64_SHUTDOWN     48
#define SYS64_BIND         49
#define SYS64_LISTEN       50
#define SYS64_GETSOCKNAME  51
#define SYS64_GETPEERNAME  52
#define SYS64_SETSOCKOPT   54
#define SYS64_GETSOCKOPT   55
#define SYS64_CLONE        56
#define SYS64_FORK         57
#define SYS64_VFORK        58
#define SYS64_EXECVE       59
#define SYS64_EXIT         60
#define SYS64_WAIT4        61
#define SYS64_KILL         62
#define SYS64_UNAME        63
#define SYS64_FCNTL        72
#define SYS64_FSYNC        74
#define SYS64_TRUNCATE     76
#define SYS64_FTRUNCATE    77
#define SYS64_GETDENTS     78
#define SYS64_GETCWD       79
#define SYS64_CHDIR        80
#define SYS64_FCHDIR       81
#define SYS64_RENAME       82
#define SYS64_MKDIR        83
#define SYS64_RMDIR        84
#define SYS64_CREAT        85
#define SYS64_UNLINK       87
#define SYS64_LINK         86
#define SYS64_SYMLINK      88
#define SYS64_READLINK     89
#define SYS64_CHMOD        90
#define SYS64_FCHMOD       91
#define SYS64_CHOWN        92
#define SYS64_FCHOWN       93
#define SYS64_LCHOWN       94
#define SYS64_UMASK        95
#define SYS64_GETTIMEOFDAY 96
#define SYS64_GETRLIMIT    97
#define SYS64_SYSINFO      99
#define SYS64_GETUID      102
#define SYS64_GETGID      104
#define SYS64_SETUID      105
#define SYS64_SETGID      106
#define SYS64_GETEUID     107
#define SYS64_GETEGID     108
#define SYS64_GETPPID     110
#define SYS64_SETSID      112
#define SYS64_SIGALTSTACK 131
#define SYS64_ARCH_PRCTL  158
#define SYS64_GETTID      186
#define SYS64_FUTEX       202
#define SYS64_GETDENTS64  217
#define SYS64_SET_TID_ADDR 218
#define SYS64_CLOCK_GETTIME 228
#define SYS64_CLOCK_GETRES  229
#define SYS64_EXIT_GROUP  231
#define SYS64_TGKILL      234
#define SYS64_OPENAT      257
#define SYS64_MKDIRAT     258
#define SYS64_NEWFSTATAT  262
#define SYS64_UNLINKAT    263
#define SYS64_FACCESSAT   269
#define SYS64_DUP3        292
#define SYS64_PIPE2       293
#define SYS64_GETRANDOM   318
#define SYS64_MEMFD_CREATE 319

#define SYS64_SETPGID       109
#define SYS64_GETPGID       121
#define SYS64_PRCTL         157
#define SYS64_STATFS        137
#define SYS64_FSTATFS       138
#define SYS64_EPOLL_WAIT    232
#define SYS64_EPOLL_CTL     233
#define SYS64_INOTIFY_INIT  253
#define SYS64_INOTIFY_ADD_WATCH 254
#define SYS64_INOTIFY_RM_WATCH  255
#define SYS64_TIMERFD_CREATE   283
#define SYS64_EVENTFD          284
#define SYS64_TIMERFD_SETTIME  286
#define SYS64_TIMERFD_GETTIME  287
#define SYS64_ACCEPT4          288
#define SYS64_SIGNALFD4        289
#define SYS64_EVENTFD2         290
#define SYS64_EPOLL_CREATE1    291
#define SYS64_INOTIFY_INIT1    294
#define SYS64_EPOLL_CREATE     213
#define SYS64_MEMBARRIER       324

/* ── Linux stat structs ──────────────────────────────────────────────────── */
typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    uint64_t st_atime;
    uint64_t st_atime_nsec;
    uint64_t st_mtime;
    uint64_t st_mtime_nsec;
    uint64_t st_ctime;
    uint64_t st_ctime_nsec;
    int64_t  __unused[3];
} linux64_stat_t;

typedef struct {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[256];
} __attribute__((packed)) linux64_dirent_t;

typedef struct { int64_t tv_sec; int64_t tv_nsec; } linux64_timespec_t;
typedef struct { int64_t tv_sec; int64_t tv_usec; } linux64_timeval_t;

typedef struct {
    int64_t rlim_cur;
    int64_t rlim_inf;
} linux64_rlimit_t;

typedef struct {
    int8_t  sysname[65];
    int8_t  nodename[65];
    int8_t  release[65];
    int8_t  version[65];
    int8_t  machine[65];
    int8_t  domainname[65];
} linux64_utsname_t;

typedef struct {
    int64_t uptime;
    uint64_t loads[3];
    uint64_t totalram;
    uint64_t freeram;
    uint64_t sharedram;
    uint64_t bufferram;
    uint64_t totalswap;
    uint64_t freeswap;
    uint16_t procs;
    uint8_t  _pad[6];
    uint64_t totalhigh;
    uint64_t freehigh;
    uint32_t mem_unit;
} linux64_sysinfo_t;

typedef struct {
    uint64_t base_addr;
    uint32_t limit;
    uint32_t flags;
} linux64_user_desc_t;

typedef struct {
    int64_t  tv_sec;
    int64_t  tv_nsec;
} linux64_itimerspec_t;

typedef struct {
    int32_t  fd;
    int16_t  events;
    int16_t  revents;
} linux64_pollfd_t;

typedef struct {
    uint64_t iov_base;
    uint64_t iov_len;
} linux64_iovec_t;

typedef struct {
    uint64_t msg_name;        /* +0  */
    uint32_t msg_namelen;     /* +8  */
    uint32_t __pad1;          /* +12 */
    uint64_t msg_iov;         /* +16 */
    uint64_t msg_iovlen;      /* +24 */
    uint64_t msg_control;     /* +32 */
    uint64_t msg_controllen;  /* +40 */
    int32_t  msg_flags;       /* +48 */
} linux64_msghdr_t;

/* ── AT_FDCWD ────────────────────────────────────────────────────────────── */
#define AT_FDCWD  ((int64_t)-100)

/* ── MAP flags ───────────────────────────────────────────────────────────── */
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20
#define MAP_ANON        MAP_ANONYMOUS
#define PROT_NONE       0
#define PROT_READ       1
#define PROT_WRITE      2
#define PROT_EXEC       4

/* ── ARCH_PRCTL codes ────────────────────────────────────────────────────── */
#define ARCH_SET_GS  0x1001
#define ARCH_SET_FS  0x1002
#define ARCH_GET_FS  0x1003
#define ARCH_GET_GS  0x1004

/* ── FUTEX ops ───────────────────────────────────────────────────────────── */
#define FUTEX_WAIT        0
#define FUTEX_WAKE        1
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_WAIT_PRIVATE (FUTEX_WAIT | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAKE_PRIVATE (FUTEX_WAKE | FUTEX_PRIVATE_FLAG)

/* ── CLOCK ids ───────────────────────────────────────────────────────────── */
#define CLOCK_REALTIME   0
#define CLOCK_MONOTONIC  1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3

/* ── F_* fcntl commands ──────────────────────────────────────────────────── */
#define F_DUPFD       0
#define F_GETFD       1
#define F_SETFD       2
#define F_GETFL       3
#define F_SETFL       4
#define F_GETLK       5
#define F_SETLK       6
#define FD_CLOEXEC    1
#define O_NONBLOCK  2048

/* ── RLIMIT resource ids ─────────────────────────────────────────────────── */
#define RLIMIT_NOFILE 7
#define RLIMIT_STACK  3
#define RLIMIT_AS     9

/* ── serial debug helper ─────────────────────────────────────────────────── */
static void ser64(const char *s) {
    while (*s) { while (!(inb(0x3FD)&0x20)){} outb(0x3F8,(uint8_t)*s++); }
}

static void ser64_hex(uint64_t v) {
    static const char h[]="0123456789ABCDEF";
    char b[19]; int i;
    b[0]='0'; b[1]='x';
    for (i = 0; i < 16; i++) b[2+i] = h[(v>>(60-i*4))&0xF];
    b[18] = 0;
    ser64(b);
}

/* ── Heap base for Linux processes ──────────────────────────────────────── */
#define LX64_HEAP_BASE  0x10000000ULL

/* ── fill stat ───────────────────────────────────────────────────────────── */
static void fill_stat64(linux64_stat_t *st, vfs_node_t *node) {
    memset(st, 0, sizeof(*st));
    st->st_ino   = node->inode;
    st->st_size  = node->size;
    st->st_nlink = 1;
    st->st_uid   = node->uid;
    st->st_gid   = node->gid;
    st->st_blksize = 512;
    st->st_blocks  = (node->size + 511) / 512;
    if (node->flags == VFS_DIRECTORY) {
        st->st_mode = 0040755;
    } else if (node->flags == VFS_CHARDEV) {
        st->st_mode = 0020666;
    } else {
        st->st_mode = 0100644;
    }
}

/* ── file operations ─────────────────────────────────────────────────────── */

/* Read one byte from a term_pipe (non-blocking: returns 0 if empty) */
static int term_pipe_read_byte(term_pipe_t *p, char *out) {
    if (!p || p->len == 0) return 0;
    *out = (char)p->buf[p->tail];
    p->tail = (p->tail + 1) % TERM_PIPE_SIZE;
    p->len--;
    return 1;
}

static int64_t lx64_read(uint64_t fd, char *buf, uint64_t count) {
    process_t *p = process_current();
    if (fd == 0) {
        if (p && p->stdin_pipe) {
            /* Block until data available */
            while (p->stdin_pipe->len == 0) {
                p->wait_stdin = true;
                p->state = PROC_BLOCKED;
                schedule();
                p->wait_stdin = false;
            }
            /* Read up to count bytes (stop at newline) */
            uint64_t i;
            for (i = 0; i < count; i++) {
                char c;
                if (!term_pipe_read_byte(p->stdin_pipe, &c)) break;
                buf[i] = c;
                if (c == '\n') { i++; break; }
            }
            return (int64_t)i;
        }
        /* Fallback: direct keyboard (non-piped processes) */
        uint64_t i;
        for (i = 0; i < count; i++) {
            char c = keyboard_read();
            if (!c) break;
            buf[i] = c;
            if (c == '\n') { i++; break; }
        }
        return (int64_t)i;
    }
    if (fd == 1 || fd == 2) return ERR(EBADF);
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return ERR(EBADF);
    uint32_t n = vfs_read(p->fds[fd], (uint32_t)p->fd_offsets[fd], (uint32_t)count, (uint8_t*)buf);
    p->fd_offsets[fd] += n;
    return (int64_t)n;
}

static int64_t lx64_write(uint64_t fd, const char *buf, uint64_t count) {
    process_t *p = process_current();
    if (fd == 1 || fd == 2) {
        if (p && p->stdout_pipe) {
            /* Route output to the GUI terminal pipe */
            uint64_t i;
            for (i = 0; i < count; i++) {
                if (p->stdout_pipe->len < TERM_PIPE_SIZE) {
                    p->stdout_pipe->buf[p->stdout_pipe->head] = (uint8_t)buf[i];
                    p->stdout_pipe->head = (p->stdout_pipe->head + 1) % TERM_PIPE_SIZE;
                    p->stdout_pipe->len++;
                }
            }
            return (int64_t)count;
        }
        /* Fallback: VGA text + serial debug */
        uint64_t i;
        for (i = 0; i < count; i++) vga_putchar(buf[i]);
        for (i = 0; i < count && i < 256; i++) {
            while (!(inb(0x3FD)&0x20)) {}
            outb(0x3F8, (uint8_t)buf[i]);
        }
        return (int64_t)count;
    }
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return ERR(EBADF);
    uint32_t n = vfs_write(p->fds[fd], (uint32_t)p->fd_offsets[fd], (uint32_t)count, (const uint8_t*)buf);
    p->fd_offsets[fd] += n;
    return (int64_t)n;
}

static int find_free_fd(process_t *p) {
    int i;
    for (i = 3; i < MAX_FDS; i++) if (!p->fds[i]) return i;
    return -1;
}

static int64_t lx64_open_at(int64_t dirfd, const char *path, int64_t flags, uint64_t mode) {
    (void)dirfd; (void)mode;
    if (!path) return ERR(EFAULT);
    process_t *p = process_current();
    if (!p) return ERR(ENOMEM);
    int fd = find_free_fd(p);
    if (fd < 0) return ERR(EMFILE);

    vfs_node_t *node = vfs_resolve(path);
    if (!node) {
        if (flags & O_CREAT) {
            char name[256];
            vfs_node_t *dir = vfs_resolve_parent(path, name);
            if (!dir || !name[0]) return ERR(ENOENT);
            if (vfs_create(dir, name, (uint32_t)mode) != 0) return ERR(EIO);
            node = vfs_resolve(path);
        }
        if (!node) return ERR(ENOENT);
    }
    if ((flags & O_TRUNC) && node->write) {
        node->size = 0;
        vfs_write(node, 0, 0, 0);
    }
    p->fds[fd]        = node;
    p->fd_offsets[fd] = (flags & O_APPEND) ? node->size : 0;
    vfs_open(node, (uint32_t)flags);
    return (int64_t)fd;
}

static int64_t lx64_close(uint64_t fd) {
    if (fd < 3 || fd >= MAX_FDS) return ERR(EBADF);
    process_t *p = process_current();
    if (!p || !p->fds[fd]) return ERR(EBADF);
    vfs_close(p->fds[fd]);
    p->fds[fd]        = 0;
    p->fd_offsets[fd] = 0;
    return 0;
}

static int64_t lx64_stat(const char *path, linux64_stat_t *st) {
    if (!path || !st) return ERR(EFAULT);
    vfs_node_t *node = vfs_resolve(path);
    if (!node) return ERR(ENOENT);
    fill_stat64(st, node);
    return 0;
}

static int64_t lx64_fstat(uint64_t fd, linux64_stat_t *st) {
    if (!st) return ERR(EFAULT);
    if (fd == 0 || fd == 1 || fd == 2) {
        memset(st, 0, sizeof(*st));
        st->st_mode  = 0020666;
        st->st_nlink = 1;
        return 0;
    }
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return ERR(EBADF);
    fill_stat64(st, p->fds[fd]);
    return 0;
}

static int64_t lx64_lseek(uint64_t fd, int64_t offset, uint64_t whence) {
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return ERR(EBADF);
    vfs_node_t *node = p->fds[fd];
    uint64_t pos = p->fd_offsets[fd];
    if (whence == 0)      pos = (uint64_t)offset;
    else if (whence == 1) pos = (uint64_t)((int64_t)pos + offset);
    else if (whence == 2) pos = (uint64_t)((int64_t)node->size + offset);
    else return ERR(EINVAL);
    p->fd_offsets[fd] = pos;
    return (int64_t)pos;
}

static int64_t lx64_dup(uint64_t oldfd) {
    process_t *p = process_current();
    if (!p || oldfd >= MAX_FDS || !p->fds[oldfd]) return ERR(EBADF);
    int newfd = find_free_fd(p);
    if (newfd < 0) return ERR(EMFILE);
    p->fds[newfd]        = p->fds[oldfd];
    p->fd_offsets[newfd] = p->fd_offsets[oldfd];
    return newfd;
}

static int64_t lx64_dup2(uint64_t oldfd, uint64_t newfd) {
    process_t *p = process_current();
    if (!p || oldfd >= MAX_FDS || !p->fds[oldfd]) return ERR(EBADF);
    if (newfd >= MAX_FDS) return ERR(EBADF);
    if (p->fds[newfd]) vfs_close(p->fds[newfd]);
    p->fds[newfd]        = p->fds[oldfd];
    p->fd_offsets[newfd] = p->fd_offsets[oldfd];
    return (int64_t)newfd;
}

static int64_t lx64_fcntl(uint64_t fd, uint64_t cmd, uint64_t arg) {
    process_t *p = process_current();
    if (!p) return ERR(EBADF);
    if (cmd == F_GETFD) return (fd < MAX_FDS && p->fds[fd]) ? 0 : ERR(EBADF);
    if (cmd == F_SETFD) return 0;
    if (cmd == F_GETFL) return (fd < MAX_FDS && p->fds[fd]) ? O_RDWR : ERR(EBADF);
    if (cmd == F_SETFL) return 0;
    if (cmd == F_DUPFD) {
        if (fd >= MAX_FDS || !p->fds[fd]) return ERR(EBADF);
        int i;
        for (i = (int)arg; i < MAX_FDS; i++) {
            if (!p->fds[i]) {
                p->fds[i] = p->fds[fd];
                p->fd_offsets[i] = p->fd_offsets[fd];
                return i;
            }
        }
        return ERR(EMFILE);
    }
    return 0;
}

static int64_t lx64_getdents64(uint64_t fd, linux64_dirent_t *dirp, uint64_t count) {
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return ERR(EBADF);
    vfs_node_t *dir = p->fds[fd];
    if (!(dir->flags & VFS_DIRECTORY)) return ERR(ENOTDIR);
    uint64_t written = 0;
    uint64_t idx = p->fd_offsets[fd];
    uint8_t *buf = (uint8_t *)dirp;
    dirent_t *child = vfs_readdir(dir, (uint32_t)idx);
    while (child && written + 280 <= count) {
        linux64_dirent_t *ent = (linux64_dirent_t *)(buf + written);
        uint16_t namelen = (uint16_t)strlen(child->name);
        uint16_t reclen  = (uint16_t)((19 + namelen + 1 + 7) & ~7U);
        ent->d_ino    = child->inode;
        ent->d_off    = (int64_t)(idx + 1);
        ent->d_reclen = reclen;
        ent->d_type   = (child->type == VFS_DIRECTORY) ? 4 : 8;
        memcpy(ent->d_name, child->name, namelen);
        ent->d_name[namelen] = '\0';
        written += reclen;
        idx++;
        child = vfs_readdir(dir, (uint32_t)idx);
    }
    p->fd_offsets[fd] = idx;
    return (int64_t)written;
}

static int64_t lx64_readv(uint64_t fd, linux64_iovec_t *iov, uint64_t iovcnt) {
    int64_t total = 0;
    uint64_t i;
    for (i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_base || iov[i].iov_len == 0) continue;
        int64_t n = lx64_read(fd, (char*)(uintptr_t)iov[i].iov_base, iov[i].iov_len);
        if (n < 0) return n;
        total += n;
        if ((uint64_t)n < iov[i].iov_len) break;
    }
    return total;
}

static int64_t lx64_writev(uint64_t fd, linux64_iovec_t *iov, uint64_t iovcnt) {
    int64_t total = 0;
    uint64_t i;
    for (i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_base || iov[i].iov_len == 0) continue;
        int64_t n = lx64_write(fd, (const char*)(uintptr_t)iov[i].iov_base, iov[i].iov_len);
        if (n < 0) return n;
        total += n;
    }
    return total;
}

/* ── memory ──────────────────────────────────────────────────────────────── */

static int64_t lx64_brk(uint64_t addr) {
    process_t *p = process_current();
    if (!p) return ERR(ENOMEM);
    if (p->heap_start == 0) {
        p->heap_start = LX64_HEAP_BASE;
        p->heap_end   = LX64_HEAP_BASE;
    }
    if (addr == 0) return (int64_t)p->heap_end;
    if (addr < p->heap_start) return (int64_t)p->heap_end;
    if (addr > p->heap_end) {
        uint64_t page  = (p->heap_end + 0xFFFULL) & ~0xFFFULL;
        uint64_t target = (addr + 0xFFFULL) & ~0xFFFULL;
        while (page < target) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) return (int64_t)p->heap_end;
            vmm_map_page(p->page_dir, page, phys,
                         PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
            memset((void*)(uintptr_t)page, 0, 0x1000);
            page += 0x1000;
        }
    }
    p->heap_end = addr;
    return (int64_t)addr;
}

static uint64_t g_mmap_next = 0x40000000ULL;

static int64_t lx64_mmap(uint64_t addr, uint64_t length, uint64_t prot,
                          uint64_t flags, int64_t fd, uint64_t pgoff) {
    (void)prot;
    if (length == 0) return ERR(EINVAL);
    uint64_t pages = (length + 0xFFFULL) >> 12;
    uint64_t base;

    if (addr && (flags & MAP_FIXED)) {
        base = addr & ~0xFFFULL;
    } else if (addr) {
        base = addr & ~0xFFFULL;
    } else {
        base = g_mmap_next;
        g_mmap_next += pages * 0x1000ULL + 0x1000ULL; /* guard page */
    }

    process_t *p = process_current();

    /* Map framebuffer device */
    if (fd >= 3 && p && (uint64_t)fd < MAX_FDS && p->fds[fd]) {
        vfs_node_t *node = p->fds[fd];
        if (node->flags == VFS_CHARDEV && node->impl == 0xFB0) {
            extern framebuffer_t fb;
            uint64_t fb_phys = (uint64_t)(uintptr_t)fb.backbuf;
            uint64_t i;
            for (i = 0; i < pages; i++) {
                vmm_map_page(p->page_dir, base + i*0x1000,
                             fb_phys + i*0x1000,
                             PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
            }
            return (int64_t)base;
        }
    }

    /* File-backed mmap */
    if (fd >= 3 && p && (uint64_t)fd < MAX_FDS && p->fds[fd] &&
        !(flags & MAP_ANONYMOUS)) {
        uint64_t i;
        for (i = 0; i < pages; i++) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) return ERR(ENOMEM);
            vmm_map_page(p->page_dir, base + i*0x1000, phys,
                         PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
        memset((void*)(uintptr_t)base, 0, pages * 0x1000);
        uint64_t foff  = pgoff * 0x1000;
        uint64_t fsize = p->fds[fd]->size;
        uint64_t copy  = (fsize > length) ? length : fsize;
        vfs_read(p->fds[fd], (uint32_t)foff, (uint32_t)copy,
                 (uint8_t*)(uintptr_t)base);
        return (int64_t)base;
    }

    /* Anonymous mmap */
    uint64_t i;
    for (i = 0; i < pages; i++) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) return ERR(ENOMEM);
        vmm_map_page(p ? p->page_dir : vmm_get_current_dir(),
                     base + i*0x1000, phys,
                     PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }
    memset((void*)(uintptr_t)base, 0, pages * 0x1000);
    return (int64_t)base;
}

static int64_t lx64_munmap(uint64_t addr, uint64_t length) {
    (void)addr; (void)length;
    return 0;
}

static int64_t lx64_mprotect(uint64_t addr, uint64_t len, uint64_t prot) {
    (void)prot;
    /* Re-map with appropriate flags */
    process_t *p = process_current();
    if (!p) return 0;
    uint64_t base  = addr & ~0xFFFULL;
    uint64_t pages = (len + 0xFFFULL) >> 12;
    uint64_t flags = PAGE_PRESENT | PAGE_USER;
    if (prot & PROT_WRITE) flags |= PAGE_WRITABLE;
    uint64_t i;
    for (i = 0; i < pages; i++) {
        uint64_t phys = vmm_get_physical(p->page_dir, base + i*0x1000);
        if (phys) vmm_map_page(p->page_dir, base + i*0x1000, phys, flags);
    }
    return 0;
}

static int64_t lx64_mremap(uint64_t old_addr, uint64_t old_size,
                             uint64_t new_size, uint64_t flags) {
    (void)flags;
    if (new_size <= old_size) return (int64_t)old_addr;
    /* Allocate new region and copy */
    int64_t new_addr = lx64_mmap(0, new_size, PROT_READ|PROT_WRITE,
                                   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (new_addr < 0) return new_addr;
    memcpy((void*)(uintptr_t)new_addr, (void*)(uintptr_t)old_addr, old_size);
    lx64_munmap(old_addr, old_size);
    return new_addr;
}

/* ── process ─────────────────────────────────────────────────────────────── */

static int64_t lx64_getpid(void) {
    process_t *p = process_current();
    return p ? (int64_t)p->pid : 1;
}

static int64_t lx64_getppid(void) {
    process_t *p = process_current();
    return (p && p->parent) ? (int64_t)p->parent->pid : 0;
}

static int64_t lx64_gettid(void)   { return lx64_getpid(); }
static int64_t lx64_getuid(void)   { return 0; }
static int64_t lx64_getgid(void)   { return 0; }
static int64_t lx64_geteuid(void)  { return 0; }
static int64_t lx64_getegid(void)  { return 0; }
static int64_t lx64_setsid(void)   { return lx64_getpid(); }
static int64_t lx64_umask(uint64_t mask) { (void)mask; return 022; }

static int64_t lx64_exit(int64_t code) {
    process_t *p = process_current();
    if (p) { p->state = PROC_ZOMBIE; p->exit_code = (int32_t)code; }
    ser64("[LX64] exit code=");
    ser64_hex((uint64_t)code);
    ser64("\r\n");
    schedule();
    for (;;) __asm__ volatile ("hlt");
    return 0;
}

/* ── extern globals from syscall_entry.asm and switch.asm ─────────────────── */
extern uint64_t g_syscall_user_rsp;
extern void fork_return_to_user(void);

/* ── CLONE flags ─────────────────────────────────────────────────────────── */
#define CLONE_VM             0x00000100ULL
#define CLONE_FILES          0x00000400ULL
#define CLONE_THREAD         0x00010000ULL
#define CLONE_SETTLS         0x00080000ULL
#define CLONE_PARENT_SETTID  0x00100000ULL
#define CLONE_CHILD_CLEARTID 0x00200000ULL
#define CLONE_CHILD_SETTID   0x01000000ULL

static int64_t lx64_fork(syscall64_frame_t *f) {
    process_t *parent = process_current();
    if (!parent) return ERR(ENOMEM);

    process_t *child = process_fork(parent, (uint64_t)(uintptr_t)fork_return_to_user);
    if (!child) return ERR(ENOMEM);

    child->fork_user_rip    = f->rcx;
    child->fork_user_rflags = f->r11;
    child->fork_user_rsp    = g_syscall_user_rsp;
    child->fork_tls         = 0;
    child->compat_mode      = parent->compat_mode;
    child->stdin_pipe       = parent->stdin_pipe;
    child->stdout_pipe      = parent->stdout_pipe;

    scheduler_add(child);
    ser64("[LX64] fork -> child pid=");
    ser64_hex((uint64_t)child->pid);
    ser64("\r\n");
    return (int64_t)child->pid;
}

static int64_t lx64_clone(syscall64_frame_t *f, uint64_t flags, uint64_t stack,
                           uint64_t ptid, uint64_t ctid, uint64_t tls) {
    (void)ctid;
    process_t *parent = process_current();
    if (!parent) return ERR(ENOMEM);

    process_t *child = process_fork(parent, (uint64_t)(uintptr_t)fork_return_to_user);
    if (!child) return ERR(ENOMEM);

    child->fork_user_rip    = f->rcx;
    child->fork_user_rflags = f->r11;
    child->fork_user_rsp    = stack ? stack : g_syscall_user_rsp;
    child->fork_tls         = (flags & CLONE_SETTLS) ? tls : 0;
    child->compat_mode      = parent->compat_mode;
    child->stdin_pipe       = parent->stdin_pipe;
    child->stdout_pipe      = parent->stdout_pipe;

    if ((flags & CLONE_PARENT_SETTID) && ptid)
        *(uint32_t*)(uintptr_t)ptid = child->pid;

    scheduler_add(child);
    ser64("[LX64] clone -> child pid=");
    ser64_hex((uint64_t)child->pid);
    ser64("\r\n");
    return (int64_t)child->pid;
}

static int64_t lx64_wait4(syscall64_frame_t *f, int64_t pid, int32_t *status,
                           uint64_t options, void *rusage) {
    (void)options; (void)rusage; (void)f;
    process_t *parent = process_current();
    if (!parent) return ERR(ECHILD);

    process_t *child = 0;
    if (pid > 0) {
        child = process_get((uint32_t)pid);
        if (!child || child->parent != parent) return ERR(ECHILD);
        if (child->state == PROC_ZOMBIE) goto reap;
    } else {
        /* pid == -1: look for any zombie child */
        uint32_t i;
        for (i = 0; i < parent->nchildren; i++) {
            process_t *c = process_get(parent->children[i]);
            if (c && c->state == PROC_ZOMBIE) { child = c; goto reap; }
        }
        if (parent->nchildren == 0) return ERR(ECHILD);
    }

    /* Block until a child exits */
    parent->waiting_child = true;
    parent->state = PROC_BLOCKED;
    schedule();

    /* Woken by process_child_exited() — find the zombie */
    if (pid > 0) {
        child = process_get((uint32_t)pid);
    } else {
        uint32_t i;
        for (i = 0; i < parent->nchildren; i++) {
            process_t *c = process_get(parent->children[i]);
            if (c && c->state == PROC_ZOMBIE) { child = c; break; }
        }
    }
    if (!child) return ERR(ECHILD);

reap:
    if (status) *status = (int32_t)(child->exit_code << 8);
    return (int64_t)child->pid;
}

#include <proc/elf.h>

static int64_t lx64_execve(syscall64_frame_t *f, const char *path,
                            uint64_t *argv, uint64_t *envp) {
    (void)argv; (void)envp;
    if (!path) return ERR(EFAULT);

    vfs_node_t *node = vfs_resolve(path);
    if (!node) return ERR(ENOENT);

    uint32_t sz = node->size;
    if (sz < 4) return ERR(ENOEXEC);
    uint8_t *data = (uint8_t*)kmalloc(sz);
    if (!data) return ERR(ENOMEM);
    vfs_read(node, 0, sz, data);

    if (!elf_validate(data, sz)) {
        kfree(data);
        return ERR(ENOEXEC);
    }

    process_t *p = process_current();
    if (!p) { kfree(data); return ERR(ESRCH); }

    /* Tear down old user address space */
    vmm_free_user_pages(p->page_dir);
    p->heap_start = 0;
    p->heap_end   = 0;

    /* Load new image into current process */
    elf_load_result_t res;
    int r = elf_load(p, data, sz, &res);
    kfree(data);
    if (r < 0) return ERR(ENOEXEC);

    p->heap_start  = res.heap_base;
    p->heap_end    = res.heap_base;
    p->compat_mode = res.is_linux_compat ? COMPAT_LINUX : COMPAT_NONE;

    /* Redirect sysretq to new entry point with new stack */
    f->rcx = res.entry_point;
    f->r11 = 0x202;
    g_syscall_user_rsp = res.user_stack_top;

    ser64("[LX64] execve -> entry=");
    ser64_hex(res.entry_point);
    ser64("\r\n");
    return 0;  /* parent's sysretq goes to new entry */
}

static int64_t lx64_kill(int64_t pid, int64_t sig) {
    (void)pid; (void)sig;
    return 0;
}

/* ── time ────────────────────────────────────────────────────────────────── */

static int64_t lx64_clock_gettime(uint64_t clk, linux64_timespec_t *ts) {
    if (!ts) return ERR(EFAULT);
    (void)clk;
    uint64_t ticks = timer_get_ticks();
    ts->tv_sec  = (int64_t)(ticks / 1000);
    ts->tv_nsec = (int64_t)((ticks % 1000) * 1000000LL);
    return 0;
}

static int64_t lx64_gettimeofday(linux64_timeval_t *tv, void *tz) {
    (void)tz;
    if (!tv) return ERR(EFAULT);
    uint64_t ticks = timer_get_ticks();
    tv->tv_sec  = (int64_t)(ticks / 1000);
    tv->tv_usec = (int64_t)((ticks % 1000) * 1000LL);
    return 0;
}

static int64_t lx64_nanosleep(linux64_timespec_t *req, linux64_timespec_t *rem) {
    (void)rem;
    if (!req) return ERR(EFAULT);
    uint64_t ms = (uint64_t)(req->tv_sec * 1000 + req->tv_nsec / 1000000);
    uint64_t t0 = timer_get_ticks();
    while (timer_get_ticks() - t0 < ms) __asm__ volatile ("hlt");
    return 0;
}

/* ── arch_prctl ──────────────────────────────────────────────────────────── */

static int64_t lx64_arch_prctl(uint64_t code, uint64_t addr) {
    switch (code) {
    case ARCH_SET_FS:
        set_fs_base(addr);
        return 0;
    case ARCH_SET_GS:
        set_gs_base(addr);
        return 0;
    case ARCH_GET_FS: {
        uint32_t lo, hi;
        __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000100U));
        *(uint64_t*)(uintptr_t)addr = ((uint64_t)hi << 32) | lo;
        return 0;
    }
    case ARCH_GET_GS: {
        uint32_t lo, hi;
        __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000101U));
        *(uint64_t*)(uintptr_t)addr = ((uint64_t)hi << 32) | lo;
        return 0;
    }
    default: return ERR(EINVAL);
    }
}

/* ── futex ───────────────────────────────────────────────────────────────── */

static int64_t lx64_futex(uint32_t *uaddr, int64_t op, uint32_t val,
                           linux64_timespec_t *timeout, uint32_t *uaddr2, uint32_t val3) {
    (void)timeout; (void)uaddr2; (void)val3;
    int base_op = (int)(op & ~FUTEX_PRIVATE_FLAG);
    if (base_op == FUTEX_WAIT) {
        if (!uaddr) return ERR(EFAULT);
        uint64_t t0 = timer_get_ticks();
        uint64_t deadline = t0 + (timeout ? (uint64_t)(timeout->tv_sec*1000 + timeout->tv_nsec/1000000) : 5000);
        while (*uaddr == val && timer_get_ticks() < deadline)
            __asm__ volatile ("hlt");
        return 0;
    }
    if (base_op == FUTEX_WAKE) {
        return 1;
    }
    return 0;
}

/* ── set_tid_address ─────────────────────────────────────────────────────── */
static int64_t lx64_set_tid_address(uint64_t *tidptr) {
    (void)tidptr;
    return lx64_getpid();
}

/* ── sysinfo / uname / rlimit ────────────────────────────────────────────── */

static int64_t lx64_sysinfo(linux64_sysinfo_t *info) {
    if (!info) return ERR(EFAULT);
    memset(info, 0, sizeof(*info));
    info->uptime    = (int64_t)(timer_get_ticks() / 1000);
    info->totalram  = pmm_get_total_pages() * 4096;
    info->freeram   = pmm_get_free_pages()  * 4096;
    info->procs     = 1;
    info->mem_unit  = 1;
    return 0;
}

static int64_t lx64_uname(linux64_utsname_t *u) {
    if (!u) return ERR(EFAULT);
    memset(u, 0, sizeof(*u));
    memcpy(u->sysname,  "Linux",   6);
    memcpy(u->nodename, "krypx",   6);
    memcpy(u->release,  "5.15.0",  7);
    memcpy(u->version,  "#1 Krypx", 9);
    memcpy(u->machine,  "x86_64",  7);
    return 0;
}

static int64_t lx64_getrlimit(uint64_t resource, linux64_rlimit_t *rl) {
    if (!rl) return ERR(EFAULT);
    switch (resource) {
    case RLIMIT_NOFILE: rl->rlim_cur = MAX_FDS; rl->rlim_inf = MAX_FDS; break;
    case RLIMIT_STACK:  rl->rlim_cur = 8*1024*1024; rl->rlim_inf = (int64_t)-1; break;
    default:            rl->rlim_cur = (int64_t)-1; rl->rlim_inf = (int64_t)-1; break;
    }
    return 0;
}

/* ── directory ops ───────────────────────────────────────────────────────── */

static int64_t lx64_getcwd(char *buf, uint64_t size) {
    if (!buf || size < 2) return ERR(EINVAL);
    buf[0] = '/'; buf[1] = '\0';
    return (int64_t)(uintptr_t)buf;
}

static int64_t lx64_chdir(const char *path) {
    (void)path;
    return 0;
}

static int64_t lx64_mkdir(const char *path, uint64_t mode) {
    if (!path) return ERR(EFAULT);
    char name[256];
    vfs_node_t *dir = vfs_resolve_parent(path, name);
    if (!dir || !name[0]) return ERR(ENOENT);
    return vfs_mkdir(dir, name, (uint32_t)mode) == 0 ? 0 : ERR(EEXIST);
}

static int64_t lx64_unlink(const char *path) {
    if (!path) return ERR(EFAULT);
    char name[256];
    vfs_node_t *dir = vfs_resolve_parent(path, name);
    if (!dir || !name[0]) return ERR(ENOENT);
    return vfs_unlink(dir, name) == 0 ? 0 : ERR(ENOENT);
}

static int64_t lx64_access(const char *path, uint64_t mode) {
    (void)mode;
    if (!path) return ERR(EFAULT);
    vfs_node_t *node = vfs_resolve(path);
    return node ? 0 : ERR(ENOENT);
}

static int64_t lx64_readlink(const char *path, char *buf, uint64_t bufsz) {
    (void)path; (void)buf; (void)bufsz;
    return ERR(ENOENT);
}

static int64_t lx64_rename(const char *old, const char *newp) {
    (void)old; (void)newp;
    return ERR(ENOSYS);
}

/* ── ioctl ───────────────────────────────────────────────────────────────── */

#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414
#define TCGETS      0x5401
#define TCSETS      0x5402
#define FIONREAD    0x541B

typedef struct { uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel; } linux64_winsize_t;

static int64_t lx64_ioctl(uint64_t fd, uint64_t req, uint64_t arg) {
    (void)fd;
    if (req == TIOCGWINSZ) {
        linux64_winsize_t *ws = (linux64_winsize_t*)(uintptr_t)arg;
        if (!ws) return ERR(EFAULT);
        ws->ws_row = 25; ws->ws_col = 80;
        ws->ws_xpixel = 640; ws->ws_ypixel = 480;
        return 0;
    }
    if (req == TCGETS || req == TCSETS) return 0;
    if (req == FIONREAD) {
        if (arg) *(int32_t*)(uintptr_t)arg = 0;
        return 0;
    }
    return ERR(ENOTTY);
}

/* ── poll / select ───────────────────────────────────────────────────────── */

static int64_t lx64_poll(linux64_pollfd_t *fds, uint64_t nfds, int64_t timeout) {
    (void)fds; (void)nfds; (void)timeout;
    return 0;
}

/* ── getrandom ───────────────────────────────────────────────────────────── */

static uint64_t lx64_rand_state = 0xDEADBEEF1234ABCDULL;

static int64_t lx64_getrandom(char *buf, uint64_t count, uint64_t flags) {
    (void)flags;
    if (!buf) return ERR(EFAULT);
    uint64_t i;
    for (i = 0; i < count; i++) {
        lx64_rand_state ^= timer_get_ticks() ^ (lx64_rand_state << 13);
        lx64_rand_state ^= lx64_rand_state >> 7;
        lx64_rand_state ^= lx64_rand_state << 17;
        buf[i] = (char)(lx64_rand_state & 0xFF);
    }
    return (int64_t)count;
}

/* ── socket ──────────────────────────────────────────────────────────────── */

#define AF_UNIX_LX  1
#define AF_INET_LX  2
#define SOCK_STREAM_LX 1
#define SOCK_DGRAM_LX  2
#define SOCK_NONBLOCK  2048

typedef struct {
    uint16_t sa_family;
    char     sa_data[108];
} linux64_sockaddr_t;

typedef struct {
    uint16_t sun_family;
    char     sun_path[108];
} linux64_sockaddr_un_t;

/* Unix socket table (simple in-memory pipe implementation) */
#define MAX_UNIX_SOCKETS 16
#define UNIX_BUF_SIZE    65536

typedef struct {
    bool     used;
    bool     listening;
    bool     connected;
    char     path[108];
    uint8_t  buf[UNIX_BUF_SIZE];
    uint32_t buf_read;
    uint32_t buf_write;
    int      peer;          /* index of connected peer */
} unix_sock_t;

static unix_sock_t unix_socks[MAX_UNIX_SOCKETS];

/* All sockets: we track whether it's Unix or INET in a type table */
#define KRYPX_SOCK_FREE   0
#define KRYPX_SOCK_UNIX   1
#define KRYPX_SOCK_INET   2

typedef struct {
    uint8_t type;
    int     idx;  /* index into unix_socks[] or inet socket fd */
} krypx_sock_t;

#define MAX_KRYPX_SOCKETS 32
static krypx_sock_t ksocks[MAX_KRYPX_SOCKETS];

/* ── kernel service sockets (X11 server, etc.) ───────────────────────────── */
#define MAX_KERNEL_SERVICES 4
#define KSVC_BUF 65536
#define PEER_KSVC_BASE  0x8000

typedef struct {
    bool     active;
    char     path[108];
    uint8_t  rx[KSVC_BUF];   /* client → server */
    uint32_t rx_rd, rx_wr;
    uint8_t  tx[KSVC_BUF];   /* server → client */
    uint32_t tx_rd, tx_wr;
    int      client_uidx;    /* index into unix_socks[] of connected client, or -1 */
} ksvc_t;

static ksvc_t g_ksvcs[MAX_KERNEL_SERVICES];

/* VFS node wrapper for a socket fd */
static uint32_t sock_read_fn(vfs_node_t *node, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)off;
    int kidx = (int)node->impl;
    if (kidx < 0 || kidx >= MAX_KRYPX_SOCKETS) return 0;
    krypx_sock_t *ks = &ksocks[kidx];
    if (ks->type != KRYPX_SOCK_UNIX) return 0;
    unix_sock_t *us = &unix_socks[ks->idx];
    if (!us->connected) return 0;

    /* Reading from a kernel service: trigger X11 processing then drain tx ring */
    if (us->peer >= PEER_KSVC_BASE) {
        int si = us->peer - PEER_KSVC_BASE;
        /* Let X11 server process any pending requests before we return data */
        extern void x11_server_process(int svc_idx);
        x11_server_process(si);
        /* Drain from the unix_sock read buffer (written by lx64_ksvc_write) */
        uint32_t avail = us->buf_write - us->buf_read;
        uint32_t n = (sz < avail) ? sz : avail;
        if (!n) return 0;
        memcpy(buf, us->buf + (us->buf_read % UNIX_BUF_SIZE), n);
        us->buf_read += n;
        return n;
    }

    if (us->buf_read == us->buf_write) return 0;
    uint32_t avail = us->buf_write - us->buf_read;
    uint32_t n = (sz < avail) ? sz : avail;
    memcpy(buf, us->buf + (us->buf_read % UNIX_BUF_SIZE), n);
    us->buf_read += n;
    return n;
}

static uint32_t sock_write_fn(vfs_node_t *node, uint32_t off, uint32_t sz, const uint8_t *buf) {
    (void)off;
    int kidx = (int)node->impl;
    if (kidx < 0 || kidx >= MAX_KRYPX_SOCKETS) return 0;
    krypx_sock_t *ks = &ksocks[kidx];
    if (ks->type != KRYPX_SOCK_UNIX) return 0;
    unix_sock_t *us = &unix_socks[ks->idx];

    /* Writing to a kernel service: put data into service rx ring */
    if (us->peer >= PEER_KSVC_BASE) {
        int si = us->peer - PEER_KSVC_BASE;
        ksvc_t *svc = &g_ksvcs[si];
        uint32_t space = KSVC_BUF - (svc->rx_wr - svc->rx_rd);
        uint32_t n = sz < space ? sz : space;
        memcpy(svc->rx + (svc->rx_wr % KSVC_BUF), buf, n);
        svc->rx_wr += n;
        return n;
    }

    /* Normal unix socket: write to peer's buffer */
    if (us->peer < 0 || (uint32_t)us->peer >= MAX_UNIX_SOCKETS) return 0;
    unix_sock_t *peer = &unix_socks[us->peer];
    uint32_t space = UNIX_BUF_SIZE - (peer->buf_write - peer->buf_read);
    uint32_t n = (sz < space) ? sz : space;
    memcpy(peer->buf + (peer->buf_write % UNIX_BUF_SIZE), buf, n);
    peer->buf_write += n;
    return n;
}

static vfs_node_t sock_nodes[MAX_KRYPX_SOCKETS];

static int alloc_ksock(void) {
    int i;
    for (i = 0; i < MAX_KRYPX_SOCKETS; i++) {
        if (ksocks[i].type == KRYPX_SOCK_FREE) return i;
    }
    return -1;
}

static int64_t lx64_socket(uint64_t domain, uint64_t type, uint64_t proto) {
    (void)proto;
    uint64_t base_type = type & ~(uint64_t)(SOCK_NONBLOCK | 524288 /*SOCK_CLOEXEC*/);

    process_t *p = process_current();
    if (!p) return ERR(ENOMEM);
    int fd = find_free_fd(p);
    if (fd < 0) return ERR(EMFILE);
    int kidx = alloc_ksock();
    if (kidx < 0) return ERR(ENFILE);

    memset(&sock_nodes[kidx], 0, sizeof(vfs_node_t));
    sock_nodes[kidx].flags = VFS_CHARDEV;
    sock_nodes[kidx].impl  = (uint32_t)kidx;
    sock_nodes[kidx].read  = sock_read_fn;
    sock_nodes[kidx].write = sock_write_fn;
    memcpy(sock_nodes[kidx].name, "socket", 7);

    ksocks[kidx].idx = -1;

    if (domain == AF_UNIX_LX && base_type == SOCK_STREAM_LX) {
        ksocks[kidx].type = KRYPX_SOCK_UNIX;
        int uidx = -1;
        int i;
        for (i = 0; i < MAX_UNIX_SOCKETS; i++) {
            if (!unix_socks[i].used) { uidx = i; break; }
        }
        if (uidx < 0) return ERR(ENFILE);
        memset(&unix_socks[uidx], 0, sizeof(unix_sock_t));
        unix_socks[uidx].used = true;
        unix_socks[uidx].peer = -1;
        ksocks[kidx].idx = uidx;
    } else {
        /* Delegate to existing INET socket layer */
        ksocks[kidx].type = KRYPX_SOCK_INET;
        /* stub: return the fd with no real backing */
    }

    p->fds[fd]        = &sock_nodes[kidx];
    p->fd_offsets[fd] = 0;
    return (int64_t)fd;
}

/* Find listening Unix socket by path */
static int find_unix_listener(const char *path) {
    int i;
    for (i = 0; i < MAX_UNIX_SOCKETS; i++) {
        if (unix_socks[i].used && unix_socks[i].listening &&
            strcmp(unix_socks[i].path, path) == 0)
            return i;
    }
    return -1;
}

static int64_t lx64_bind(uint64_t sockfd, linux64_sockaddr_t *addr, uint64_t addrlen) {
    (void)addrlen;
    if (!addr) return ERR(EFAULT);
    process_t *p = process_current();
    if (!p || sockfd >= MAX_FDS || !p->fds[sockfd]) return ERR(EBADF);
    int kidx = (int)p->fds[sockfd]->impl;
    if (ksocks[kidx].type != KRYPX_SOCK_UNIX) return 0;
    linux64_sockaddr_un_t *un = (linux64_sockaddr_un_t*)addr;
    unix_socks[ksocks[kidx].idx].listening = false;
    memcpy(unix_socks[ksocks[kidx].idx].path, un->sun_path, 107);
    return 0;
}

static int64_t lx64_listen(uint64_t sockfd, uint64_t backlog) {
    (void)backlog;
    process_t *p = process_current();
    if (!p || sockfd >= MAX_FDS || !p->fds[sockfd]) return ERR(EBADF);
    int kidx = (int)p->fds[sockfd]->impl;
    if (ksocks[kidx].type != KRYPX_SOCK_UNIX) return 0;
    unix_socks[ksocks[kidx].idx].listening = true;
    return 0;
}

static int64_t lx64_connect(uint64_t sockfd, linux64_sockaddr_t *addr, uint64_t addrlen) {
    (void)addrlen;
    if (!addr) return ERR(EFAULT);
    process_t *p = process_current();
    if (!p || sockfd >= MAX_FDS || !p->fds[sockfd]) return ERR(EBADF);
    int kidx = (int)p->fds[sockfd]->impl;
    if (ksocks[kidx].type != KRYPX_SOCK_UNIX) return ERR(ECONNREFUSED);
    linux64_sockaddr_un_t *un = (linux64_sockaddr_un_t*)addr;

    /* Check kernel service endpoints first */
    {
        int si;
        const char *spath = un->sun_path;
        /* handle abstract namespace (starts with \0) */
        if (spath[0] == '\0') spath++;
        for (si = 0; si < MAX_KERNEL_SERVICES; si++) {
            if (g_ksvcs[si].active && strcmp(g_ksvcs[si].path, spath) == 0) {
                int client_uidx = ksocks[kidx].idx;
                unix_socks[client_uidx].connected = true;
                unix_socks[client_uidx].peer      = PEER_KSVC_BASE + si;
                g_ksvcs[si].client_uidx = client_uidx;
                return 0;
            }
        }
    }

    int server = find_unix_listener(un->sun_path);
    if (server < 0) {
        /* also try abstract namespace */
        const char *apath = un->sun_path;
        if (apath[0] == '\0') server = find_unix_listener(apath + 1);
    }
    if (server < 0) return ERR(ECONNREFUSED);
    /* Connect: set up bidirectional link */
    unix_socks[ksocks[kidx].idx].connected = true;
    unix_socks[ksocks[kidx].idx].peer      = server;
    unix_socks[server].connected = true;
    unix_socks[server].peer      = ksocks[kidx].idx;
    return 0;
}

static int64_t lx64_sendto(uint64_t sockfd, const char *buf, uint64_t len,
                            uint64_t flags, linux64_sockaddr_t *addr, uint64_t addrlen) {
    (void)flags; (void)addr; (void)addrlen;
    return lx64_write(sockfd, buf, len);
}

static int64_t lx64_recvfrom(uint64_t sockfd, char *buf, uint64_t len,
                               uint64_t flags, linux64_sockaddr_t *addr, uint64_t *addrlen) {
    (void)flags; (void)addr; (void)addrlen;
    return lx64_read(sockfd, buf, len);
}

static int64_t lx64_shutdown(uint64_t sockfd, uint64_t how) {
    (void)how;
    return lx64_close(sockfd);
}

static int64_t lx64_setsockopt(uint64_t fd, uint64_t lvl, uint64_t opt,
                                const void *val, uint64_t vlen) {
    (void)fd;(void)lvl;(void)opt;(void)val;(void)vlen;
    return 0;
}

static int64_t lx64_getsockname(uint64_t sockfd, linux64_sockaddr_t *addr, uint64_t *addrlen) {
    (void)sockfd;
    if (addr) memset(addr, 0, sizeof(*addr));
    if (addrlen) *addrlen = 0;
    return 0;
}

/* ── sendmsg / recvmsg ───────────────────────────────────────────────────── */

static int64_t lx64_sendmsg(uint64_t sockfd, linux64_msghdr_t *msg, uint64_t flags) {
    (void)flags;
    if (!msg) return ERR(EFAULT);
    linux64_iovec_t *iov = (linux64_iovec_t *)(uintptr_t)msg->msg_iov;
    uint64_t iovcnt = msg->msg_iovlen;
    if (!iov || !iovcnt) return 0;
    int64_t total = 0;
    uint64_t i;
    for (i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_len) continue;
        int64_t r = lx64_write(sockfd, (const char *)(uintptr_t)iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return (total > 0) ? total : r;
        total += r;
    }
    return total;
}

static int64_t lx64_recvmsg(uint64_t sockfd, linux64_msghdr_t *msg, uint64_t flags) {
    (void)flags;
    if (!msg) return ERR(EFAULT);
    linux64_iovec_t *iov = (linux64_iovec_t *)(uintptr_t)msg->msg_iov;
    uint64_t iovcnt = msg->msg_iovlen;
    /* Zero out control length — we don't support ancillary data */
    if (msg->msg_controllen) msg->msg_controllen = 0;
    if (!iov || !iovcnt) return 0;
    int64_t total = 0;
    uint64_t i;
    for (i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_len) continue;
        int64_t r = lx64_read(sockfd, (char *)(uintptr_t)iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return (total > 0) ? total : r;
        total += r;
        if ((uint64_t)r < iov[i].iov_len) break; /* partial read — stop */
    }
    return total;
}

/* ── socketpair ──────────────────────────────────────────────────────────── */

static int64_t lx64_socketpair(uint64_t domain, uint64_t type, uint64_t proto, int32_t *fds) {
    (void)domain; (void)proto;
    if (!fds) return ERR(EFAULT);
    process_t *p = process_current();
    if (!p) return ERR(ENOMEM);

    uint64_t base_type = type & ~(uint64_t)(SOCK_NONBLOCK | 524288);
    if (base_type != SOCK_STREAM_LX && base_type != SOCK_DGRAM_LX) return ERR(EAFNOSUPPORT);

    /* Allocate two ksocks */
    int k0 = alloc_ksock();
    if (k0 < 0) return ERR(ENFILE);

    /* Temporarily mark k0 used so alloc_ksock won't return it again */
    ksocks[k0].type = KRYPX_SOCK_UNIX;
    int k1 = alloc_ksock();
    if (k1 < 0) { ksocks[k0].type = KRYPX_SOCK_FREE; return ERR(ENFILE); }

    /* Allocate two unix_sock slots */
    int u0 = -1, u1 = -1;
    uint32_t j;
    for (j = 0; j < MAX_UNIX_SOCKETS; j++) {
        if (!unix_socks[j].used) { u0 = (int)j; unix_socks[j].used = true; break; }
    }
    for (j = 0; j < MAX_UNIX_SOCKETS; j++) {
        if (!unix_socks[j].used) { u1 = (int)j; unix_socks[j].used = true; break; }
    }
    if (u0 < 0 || u1 < 0) {
        if (u0 >= 0) unix_socks[u0].used = false;
        if (u1 >= 0) unix_socks[u1].used = false;
        ksocks[k0].type = KRYPX_SOCK_FREE;
        ksocks[k1].type = KRYPX_SOCK_FREE;
        return ERR(ENFILE);
    }

    memset(&unix_socks[u0], 0, sizeof(unix_sock_t));
    memset(&unix_socks[u1], 0, sizeof(unix_sock_t));
    unix_socks[u0].used = true;  unix_socks[u0].connected = true; unix_socks[u0].peer = u1;
    unix_socks[u1].used = true;  unix_socks[u1].connected = true; unix_socks[u1].peer = u0;

    /* Set up ksocks and vfs nodes */
    ksocks[k0].type = KRYPX_SOCK_UNIX; ksocks[k0].idx = u0;
    ksocks[k1].type = KRYPX_SOCK_UNIX; ksocks[k1].idx = u1;

    memset(&sock_nodes[k0], 0, sizeof(vfs_node_t));
    sock_nodes[k0].flags = VFS_CHARDEV; sock_nodes[k0].impl = (uint32_t)k0;
    sock_nodes[k0].read  = sock_read_fn; sock_nodes[k0].write = sock_write_fn;
    memcpy(sock_nodes[k0].name, "socket", 7);

    memset(&sock_nodes[k1], 0, sizeof(vfs_node_t));
    sock_nodes[k1].flags = VFS_CHARDEV; sock_nodes[k1].impl = (uint32_t)k1;
    sock_nodes[k1].read  = sock_read_fn; sock_nodes[k1].write = sock_write_fn;
    memcpy(sock_nodes[k1].name, "socket", 7);

    /* Assign file descriptors */
    int fd0 = -1, fd1 = -1;
    for (j = 3; j < MAX_FDS; j++) { if (!p->fds[j]) { fd0 = (int)j; break; } }
    for (j = fd0 + 1; (uint32_t)j < MAX_FDS; j++) { if (!p->fds[j]) { fd1 = (int)j; break; } }
    if (fd0 < 0 || fd1 < 0) {
        unix_socks[u0].used = false; unix_socks[u1].used = false;
        ksocks[k0].type = KRYPX_SOCK_FREE; ksocks[k1].type = KRYPX_SOCK_FREE;
        return ERR(EMFILE);
    }

    p->fds[fd0] = &sock_nodes[k0];
    p->fds[fd1] = &sock_nodes[k1];
    fds[0] = fd0; fds[1] = fd1;
    return 0;
}

/* ── pipe ────────────────────────────────────────────────────────────────── */

#define PIPE_BUF_SIZE  4096
typedef struct {
    uint8_t  buf[PIPE_BUF_SIZE];
    uint32_t rd, wr;
    bool     used;
} pipe_t;

#define MAX_PIPES 8
static pipe_t pipes[MAX_PIPES];

static uint32_t pipe_read_fn(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)off;
    pipe_t *pp = &pipes[n->impl];
    uint32_t avail = pp->wr - pp->rd;
    uint32_t count = sz < avail ? sz : avail;
    memcpy(buf, pp->buf + (pp->rd % PIPE_BUF_SIZE), count);
    pp->rd += count;
    return count;
}

static uint32_t pipe_write_fn(vfs_node_t *n, uint32_t off, uint32_t sz, const uint8_t *buf) {
    (void)off;
    pipe_t *pp = &pipes[n->impl];
    uint32_t space = PIPE_BUF_SIZE - (pp->wr - pp->rd);
    uint32_t count = sz < space ? sz : space;
    memcpy(pp->buf + (pp->wr % PIPE_BUF_SIZE), buf, count);
    pp->wr += count;
    return count;
}

static vfs_node_t pipe_nodes[MAX_PIPES * 2];

static int64_t lx64_pipe2(int32_t *fds, uint64_t flags) {
    (void)flags;
    if (!fds) return ERR(EFAULT);
    process_t *p = process_current();
    if (!p) return ERR(ENOMEM);
    int pidx = -1, i;
    for (i = 0; i < MAX_PIPES; i++) if (!pipes[i].used) { pidx = i; break; }
    if (pidx < 0) return ERR(ENFILE);
    int rfd = find_free_fd(p);
    if (rfd < 0) return ERR(EMFILE);
    p->fds[rfd] = (vfs_node_t*)1; /* temporarily mark as used */
    int wfd = find_free_fd(p);
    p->fds[rfd] = 0;
    if (wfd < 0) return ERR(EMFILE);

    pipes[pidx].used = true;
    pipes[pidx].rd = pipes[pidx].wr = 0;

    memset(&pipe_nodes[pidx*2], 0, sizeof(vfs_node_t));
    pipe_nodes[pidx*2].impl = (uint32_t)pidx;
    pipe_nodes[pidx*2].flags = VFS_PIPE;
    pipe_nodes[pidx*2].read  = pipe_read_fn;

    memset(&pipe_nodes[pidx*2+1], 0, sizeof(vfs_node_t));
    pipe_nodes[pidx*2+1].impl = (uint32_t)pidx;
    pipe_nodes[pidx*2+1].flags = VFS_PIPE;
    pipe_nodes[pidx*2+1].write = pipe_write_fn;

    p->fds[rfd] = &pipe_nodes[pidx*2];
    p->fds[wfd] = &pipe_nodes[pidx*2+1];
    p->fd_offsets[rfd] = p->fd_offsets[wfd] = 0;
    fds[0] = rfd; fds[1] = wfd;
    return 0;
}

/* ── misc ────────────────────────────────────────────────────────────────── */

static int64_t lx64_sigaltstack(void *ss, void *oss) { (void)ss;(void)oss; return 0; }
static int64_t lx64_rt_sigaction(int64_t sig, void *act, void *oact, uint64_t sz) {
    (void)sig;(void)act;(void)oact;(void)sz; return 0;
}
static int64_t lx64_rt_sigprocmask(int64_t how, void *set, void *oset, uint64_t sz) {
    (void)how;(void)set;(void)oset;(void)sz; return 0;
}

static int64_t lx64_pread64(uint64_t fd, char *buf, uint64_t count, uint64_t offset) {
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return ERR(EBADF);
    uint64_t saved = p->fd_offsets[fd];
    p->fd_offsets[fd] = offset;
    int64_t n = lx64_read(fd, buf, count);
    p->fd_offsets[fd] = saved;
    return n;
}

static int64_t lx64_pwrite64(uint64_t fd, const char *buf, uint64_t count, uint64_t offset) {
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return ERR(EBADF);
    uint64_t saved = p->fd_offsets[fd];
    p->fd_offsets[fd] = offset;
    int64_t n = lx64_write(fd, buf, count);
    p->fd_offsets[fd] = saved;
    return n;
}

static int64_t lx64_fsync(uint64_t fd) { (void)fd; return 0; }
static int64_t lx64_chmod(const char *p, uint64_t m) { (void)p;(void)m; return 0; }
static int64_t lx64_fchmod(uint64_t fd, uint64_t m) { (void)fd;(void)m; return 0; }
static int64_t lx64_chown(const char *p, uint64_t u, uint64_t g) { (void)p;(void)u;(void)g; return 0; }
static int64_t lx64_fchown(uint64_t fd, uint64_t u, uint64_t g) { (void)fd;(void)u;(void)g; return 0; }
static int64_t lx64_ftruncate(uint64_t fd, uint64_t len) { (void)fd;(void)len; return 0; }
static int64_t lx64_symlink(const char *t, const char *l) { (void)t;(void)l; return ERR(ENOSYS); }
static int64_t lx64_link(const char *o, const char *n) { (void)o;(void)n; return ERR(ENOSYS); }

static int64_t lx64_memfd_create(const char *name, uint64_t flags) {
    (void)name;(void)flags;
    /* Create an anonymous in-memory file */
    process_t *p = process_current();
    if (!p) return ERR(ENOMEM);
    int fd = find_free_fd(p);
    if (fd < 0) return ERR(EMFILE);
    vfs_node_t *node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) return ERR(ENOMEM);
    memset(node, 0, sizeof(vfs_node_t));
    memcpy(node->name, "memfd", 6);
    node->flags = VFS_FILE;
    p->fds[fd] = node;
    p->fd_offsets[fd] = 0;
    return fd;
}

/* Register a path as a kernel service endpoint.  Returns service index or -1. */
int lx64_register_kernel_service(const char *path) {
    int i;
    for (i = 0; i < MAX_KERNEL_SERVICES; i++) {
        if (!g_ksvcs[i].active) {
            memset(&g_ksvcs[i], 0, sizeof(ksvc_t));
            g_ksvcs[i].active     = true;
            g_ksvcs[i].client_uidx = -1;
            strncpy(g_ksvcs[i].path, path, 107);
            return i;
        }
    }
    return -1;
}

/* Read data sent by the connected client.  Returns bytes read. */
int lx64_ksvc_read(int svc, void *buf, uint32_t max) {
    if (svc < 0 || svc >= MAX_KERNEL_SERVICES || !g_ksvcs[svc].active) return 0;
    ksvc_t *s = &g_ksvcs[svc];
    uint32_t avail = s->rx_wr - s->rx_rd;
    uint32_t n = avail < max ? avail : max;
    if (!n) return 0;
    memcpy(buf, s->rx + (s->rx_rd % KSVC_BUF), n);
    s->rx_rd += n;
    return (int)n;
}

/* Write data to the connected client.  Returns bytes written. */
int lx64_ksvc_write(int svc, const void *buf, uint32_t len) {
    if (svc < 0 || svc >= MAX_KERNEL_SERVICES || !g_ksvcs[svc].active) return 0;
    ksvc_t *s = &g_ksvcs[svc];

    /* Copy into tx ring buffer */
    uint32_t space = KSVC_BUF - (s->tx_wr - s->tx_rd);
    uint32_t n = len < space ? len : space;
    memcpy(s->tx + (s->tx_wr % KSVC_BUF), buf, n);
    s->tx_wr += n;

    /* Also push directly into the client unix_sock read buffer */
    if (s->client_uidx >= 0 && s->client_uidx < MAX_UNIX_SOCKETS) {
        unix_sock_t *peer = &unix_socks[s->client_uidx];
        uint32_t pspace = UNIX_BUF_SIZE - (peer->buf_write - peer->buf_read);
        uint32_t pn = n < pspace ? n : pspace;
        memcpy(peer->buf + (peer->buf_write % UNIX_BUF_SIZE), buf, pn);
        peer->buf_write += pn;
        s->tx_wr -= n;   /* already delivered directly */
    }
    return (int)n;
}

/* True if a client is currently connected to this service. */
bool lx64_ksvc_has_client(int svc) {
    if (svc < 0 || svc >= MAX_KERNEL_SERVICES) return false;
    return g_ksvcs[svc].active && g_ksvcs[svc].client_uidx >= 0;
}

/* ── eventfd ─────────────────────────────────────────────────────────────── */
#define MAX_EVENTFDS 16
#define EFD_SEMAPHORE 1
#define EFD_NONBLOCK  0x800
#define EFD_CLOEXEC   0x80000

typedef struct { bool used; uint64_t value; uint32_t flags; } eventfd_t;
static eventfd_t eventfds[MAX_EVENTFDS];
static vfs_node_t eventfd_nodes[MAX_EVENTFDS];

static uint32_t eventfd_read_fn(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)off;
    if (sz < 8) return 0;
    int idx = (int)(n->impl & 0xFF);
    if (idx < 0 || idx >= MAX_EVENTFDS) return 0;
    eventfd_t *efd = &eventfds[idx];
    if (efd->value == 0) return 0;
    uint64_t val = (efd->flags & EFD_SEMAPHORE) ? 1 : efd->value;
    if (efd->flags & EFD_SEMAPHORE) efd->value--; else efd->value = 0;
    memcpy(buf, &val, 8);
    return 8;
}

static uint32_t eventfd_write_fn(vfs_node_t *n, uint32_t off, uint32_t sz, const uint8_t *buf) {
    (void)off;
    if (sz < 8) return 0;
    int idx = (int)(n->impl & 0xFF);
    if (idx < 0 || idx >= MAX_EVENTFDS) return 0;
    uint64_t val; memcpy(&val, buf, 8);
    eventfds[idx].value += val;
    return 8;
}

static int64_t lx64_eventfd2(uint64_t initval, int flags) {
    process_t *p = process_current();
    if (!p) return ERR(ENOMEM);
    int fd = find_free_fd(p);
    if (fd < 0) return ERR(EMFILE);
    int i;
    for (i = 0; i < MAX_EVENTFDS; i++) if (!eventfds[i].used) break;
    if (i >= MAX_EVENTFDS) return ERR(ENFILE);
    eventfds[i].used = true; eventfds[i].value = initval; eventfds[i].flags = (uint32_t)flags;
    memset(&eventfd_nodes[i], 0, sizeof(vfs_node_t));
    memcpy(eventfd_nodes[i].name, "eventfd", 8);
    eventfd_nodes[i].flags = VFS_CHARDEV;
    eventfd_nodes[i].impl  = 0xED00 | (uint32_t)i;
    eventfd_nodes[i].read  = eventfd_read_fn;
    eventfd_nodes[i].write = eventfd_write_fn;
    p->fds[fd] = &eventfd_nodes[i];
    return fd;
}

/* ── timerfd ─────────────────────────────────────────────────────────────── */
#define MAX_TIMERFDS 8
typedef struct {
    bool     used;
    bool     armed;
    uint64_t start_tick;
    uint64_t initial_ms;
    uint64_t interval_ms;
} timerfd_t;
static timerfd_t timerfds[MAX_TIMERFDS];
static vfs_node_t timerfd_nodes[MAX_TIMERFDS];

typedef struct { int64_t tv_sec; int64_t tv_nsec; } lx_timespec2_t;
typedef struct { lx_timespec2_t it_interval; lx_timespec2_t it_value; } lx_itimerspec2_t;

static uint32_t timerfd_read_fn(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)off;
    if (sz < 8) return 0;
    int idx = (int)(n->impl & 0xFF);
    if (idx < 0 || idx >= MAX_TIMERFDS) return 0;
    timerfd_t *tfd = &timerfds[idx];
    if (!tfd->armed) return 0;
    uint64_t elapsed = timer_get_ticks() - tfd->start_tick;
    if (elapsed < tfd->initial_ms) return 0;
    uint64_t exp = 1;
    if (tfd->interval_ms > 0) {
        exp = (elapsed - tfd->initial_ms) / tfd->interval_ms + 1;
        tfd->start_tick += tfd->initial_ms + (exp - 1) * tfd->interval_ms;
        tfd->initial_ms  = tfd->interval_ms;
    } else {
        tfd->armed = false;
    }
    memcpy(buf, &exp, 8);
    return 8;
}

static int64_t lx64_timerfd_create(int clockid, int flags) {
    (void)clockid; (void)flags;
    process_t *p = process_current();
    if (!p) return ERR(ENOMEM);
    int fd = find_free_fd(p);
    if (fd < 0) return ERR(EMFILE);
    int i;
    for (i = 0; i < MAX_TIMERFDS; i++) if (!timerfds[i].used) break;
    if (i >= MAX_TIMERFDS) return ERR(ENFILE);
    timerfds[i].used = true; timerfds[i].armed = false;
    memset(&timerfd_nodes[i], 0, sizeof(vfs_node_t));
    memcpy(timerfd_nodes[i].name, "timerfd", 8);
    timerfd_nodes[i].flags = VFS_CHARDEV;
    timerfd_nodes[i].impl  = 0xFD00 | (uint32_t)i;
    timerfd_nodes[i].read  = timerfd_read_fn;
    p->fds[fd] = &timerfd_nodes[i];
    return fd;
}

static int64_t lx64_timerfd_settime(uint64_t fd, int flags, lx_itimerspec2_t *nv,
                                     lx_itimerspec2_t *ov) {
    (void)flags; (void)ov;
    if (!nv) return ERR(EFAULT);
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return ERR(EBADF);
    if ((p->fds[fd]->impl & 0xFF00) != 0xFD00) return ERR(EBADF);
    int idx = (int)(p->fds[fd]->impl & 0xFF);
    timerfd_t *tfd = &timerfds[idx];
    int64_t ims = nv->it_value.tv_sec * 1000 + nv->it_value.tv_nsec / 1000000;
    int64_t pms = nv->it_interval.tv_sec * 1000 + nv->it_interval.tv_nsec / 1000000;
    if (ims <= 0 && pms <= 0) { tfd->armed = false; return 0; }
    tfd->armed      = true;
    tfd->start_tick = timer_get_ticks();
    tfd->initial_ms = (uint64_t)(ims > 0 ? ims : 0);
    tfd->interval_ms = (uint64_t)(pms > 0 ? pms : 0);
    return 0;
}

static int64_t lx64_timerfd_gettime(uint64_t fd, lx_itimerspec2_t *cur) {
    if (!cur) return ERR(EFAULT);
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return ERR(EBADF);
    if ((p->fds[fd]->impl & 0xFF00) != 0xFD00) return ERR(EBADF);
    int idx = (int)(p->fds[fd]->impl & 0xFF);
    timerfd_t *tfd = &timerfds[idx];
    memset(cur, 0, sizeof(*cur));
    if (tfd->armed && tfd->interval_ms > 0) {
        cur->it_interval.tv_sec  = (int64_t)(tfd->interval_ms / 1000);
        cur->it_interval.tv_nsec = (int64_t)((tfd->interval_ms % 1000) * 1000000);
    }
    if (tfd->armed) {
        uint64_t elapsed = timer_get_ticks() - tfd->start_tick;
        int64_t rem = (int64_t)tfd->initial_ms - (int64_t)elapsed;
        if (rem > 0) {
            cur->it_value.tv_sec  = rem / 1000;
            cur->it_value.tv_nsec = (rem % 1000) * 1000000;
        }
    }
    return 0;
}

/* ── epoll ────────────────────────────────────────────────────────────────── */
#define EPOLLIN      0x001
#define EPOLLOUT     0x004
#define EPOLLERR     0x008
#define EPOLLHUP     0x010
#define EPOLLRDHUP   0x2000
#define EPOLLET      (1u<<31)
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

typedef struct { uint32_t events; uint64_t data; } epoll_event_t;
typedef struct { uint32_t fd; uint32_t events; uint64_t data; } ewatch_t;

#define MAX_EPOLL_SETS   8
#define MAX_EPOLL_WATCH  32

typedef struct {
    bool     used;
    ewatch_t watches[MAX_EPOLL_WATCH];
    int      nwatches;
} epoll_set_t;

static epoll_set_t g_epoll[MAX_EPOLL_SETS];
static vfs_node_t  g_epoll_nodes[MAX_EPOLL_SETS];

static int alloc_epoll(void) {
    int i;
    for (i = 0; i < MAX_EPOLL_SETS; i++) if (!g_epoll[i].used) return i;
    return -1;
}

static int epoll_idx_from_fd(process_t *p, uint64_t fd) {
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return -1;
    uint32_t impl = p->fds[fd]->impl;
    if ((impl & 0xFF00) != 0xEF00) return -1;
    return (int)(impl & 0xFF);
}

static int64_t lx64_epoll_create1(int flags) {
    (void)flags;
    process_t *p = process_current();
    if (!p) return ERR(ENOMEM);
    int fd = find_free_fd(p);
    if (fd < 0) return ERR(EMFILE);
    int eidx = alloc_epoll();
    if (eidx < 0) return ERR(ENFILE);
    g_epoll[eidx].used = true;
    g_epoll[eidx].nwatches = 0;
    memset(&g_epoll_nodes[eidx], 0, sizeof(vfs_node_t));
    memcpy(g_epoll_nodes[eidx].name, "epoll", 6);
    g_epoll_nodes[eidx].flags = VFS_CHARDEV;
    g_epoll_nodes[eidx].impl  = 0xEF00 | (uint32_t)eidx;
    p->fds[fd] = &g_epoll_nodes[eidx];
    return fd;
}

static int64_t lx64_epoll_ctl(uint64_t epfd, int op, uint64_t fd, epoll_event_t *ev) {
    process_t *p = process_current();
    int eidx = epoll_idx_from_fd(p, epfd);
    if (eidx < 0) return ERR(EBADF);
    epoll_set_t *es = &g_epoll[eidx];
    int i;
    if (op == EPOLL_CTL_ADD || op == EPOLL_CTL_MOD) {
        if (!ev) return ERR(EFAULT);
        for (i = 0; i < es->nwatches; i++) {
            if (es->watches[i].fd == (uint32_t)fd) {
                es->watches[i].events = ev->events;
                es->watches[i].data   = ev->data;
                return 0;
            }
        }
        if (es->nwatches >= MAX_EPOLL_WATCH) return ERR(ENOSPC);
        es->watches[es->nwatches].fd     = (uint32_t)fd;
        es->watches[es->nwatches].events = ev->events;
        es->watches[es->nwatches].data   = ev->data;
        es->nwatches++;
    } else if (op == EPOLL_CTL_DEL) {
        for (i = 0; i < es->nwatches; i++) {
            if (es->watches[i].fd == (uint32_t)fd) {
                es->watches[i] = es->watches[--es->nwatches];
                return 0;
            }
        }
    }
    return 0;
}

/* Check if an fd has data available for reading */
static bool epoll_fd_readable(process_t *p, uint32_t fd) {
    if (!p) return false;
    if (fd == 0) return p->stdin_pipe && p->stdin_pipe->len > 0;
    if (fd >= MAX_FDS || !p->fds[fd]) return false;
    vfs_node_t *node = p->fds[fd];
    uint32_t impl = node->impl;
    /* pipe */
    if (node->flags == VFS_PIPE && impl < (uint32_t)MAX_PIPES)
        return (pipes[impl].wr - pipes[impl].rd) > 0;
    /* eventfd */
    if ((impl & 0xFF00) == 0xED00) {
        int ei = (int)(impl & 0xFF);
        return ei < MAX_EVENTFDS && eventfds[ei].value > 0;
    }
    /* timerfd */
    if ((impl & 0xFF00) == 0xFD00) {
        int ti = (int)(impl & 0xFF);
        if (ti >= MAX_TIMERFDS || !timerfds[ti].armed) return false;
        return (timer_get_ticks() - timerfds[ti].start_tick) >= timerfds[ti].initial_ms;
    }
    /* unix socket */
    if (node->flags == VFS_CHARDEV && impl < (uint32_t)MAX_KRYPX_SOCKETS) {
        krypx_sock_t *ks = &ksocks[impl];
        if (ks->type == KRYPX_SOCK_UNIX && ks->idx >= 0) {
            unix_sock_t *us = &unix_socks[ks->idx];
            /* For kernel-service sockets (e.g. X11), process pending requests first */
            if (us->connected && us->peer >= (int)PEER_KSVC_BASE) {
                extern void x11_server_process(int svc_idx);
                x11_server_process(us->peer - PEER_KSVC_BASE);
            }
            return (us->buf_write - us->buf_read) > 0;
        }
    }
    return false;
}

static int64_t lx64_epoll_wait(uint64_t epfd, epoll_event_t *evs, int maxevents,
                                int timeout_ms) {
    process_t *p = process_current();
    int eidx = epoll_idx_from_fd(p, epfd);
    if (eidx < 0) return ERR(EBADF);
    if (!evs || maxevents <= 0) return ERR(EINVAL);
    epoll_set_t *es = &g_epoll[eidx];

    uint64_t t0 = timer_get_ticks();
    for (;;) {
        int nready = 0, i;
        for (i = 0; i < es->nwatches && nready < maxevents; i++) {
            bool in  = (es->watches[i].events & EPOLLIN)  && epoll_fd_readable(p, es->watches[i].fd);
            bool out = !!(es->watches[i].events & EPOLLOUT); /* our buffers are always writable */
            if (in || out) {
                evs[nready].events = (in ? (uint32_t)EPOLLIN : 0u) | (out ? (uint32_t)EPOLLOUT : 0u);
                evs[nready].data   = es->watches[i].data;
                nready++;
            }
        }
        if (nready > 0) return nready;
        if (timeout_ms == 0) return 0;
        if (timeout_ms > 0 && (int64_t)(timer_get_ticks() - t0) >= timeout_ms) return 0;
        schedule();
    }
}

/* ── inotify (stub) ──────────────────────────────────────────────────────── */
static vfs_node_t g_inotify_node;
static uint32_t inotify_noop_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf)
{ (void)n;(void)off;(void)sz;(void)buf; return 0; }

static int64_t lx64_inotify_init1(int flags) {
    (void)flags;
    process_t *p = process_current();
    if (!p) return ERR(ENOMEM);
    int fd = find_free_fd(p);
    if (fd < 0) return ERR(EMFILE);
    memset(&g_inotify_node, 0, sizeof(vfs_node_t));
    memcpy(g_inotify_node.name, "inotify", 8);
    g_inotify_node.flags = VFS_CHARDEV;
    g_inotify_node.impl  = 0x1234;
    g_inotify_node.read  = inotify_noop_read;
    p->fds[fd] = &g_inotify_node;
    return fd;
}

/* ── prctl ───────────────────────────────────────────────────────────────── */
#define PR_SET_PDEATHSIG        1
#define PR_SET_DUMPABLE         4
#define PR_GET_DUMPABLE         3
#define PR_SET_NAME            15
#define PR_GET_NAME            16
#define PR_SET_CHILD_SUBREAPER 36
#define PR_SET_SECCOMP         22
#define PR_CAPBSET_READ        23
#define PR_SET_NO_NEW_PRIVS    38

static int64_t lx64_prctl(int opt, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3;(void)a4;(void)a5;
    process_t *p = process_current();
    if (opt == PR_SET_NAME) {
        if (!a2) return ERR(EFAULT);
        if (p) strncpy(p->name, (const char*)(uintptr_t)a2, 15);
        return 0;
    }
    if (opt == PR_GET_NAME) {
        if (!a2) return ERR(EFAULT);
        strncpy((char*)(uintptr_t)a2, p ? p->name : "unknown", 15);
        return 0;
    }
    if (opt == PR_GET_DUMPABLE) return 1;
    /* All others: silently succeed */
    return 0;
}

/* ── setpgid / getpgid ───────────────────────────────────────────────────── */
static int64_t lx64_setpgid(uint64_t pid, uint64_t pgid) { (void)pid;(void)pgid; return 0; }
static int64_t lx64_getpgid(uint64_t pid) {
    (void)pid;
    process_t *p = process_current();
    return p ? (int64_t)p->pid : 1;
}

/* ── statfs ──────────────────────────────────────────────────────────────── */
typedef struct {
    int64_t  f_type;
    int64_t  f_bsize;
    uint64_t f_blocks, f_bfree, f_bavail;
    uint64_t f_files, f_ffree;
    uint64_t f_fsid[2];
    int64_t  f_namelen;
    int64_t  f_frsize;
    int64_t  f_flags;
    int64_t  f_spare[4];
} linux64_statfs_t;

static int64_t lx64_statfs(const char *path, linux64_statfs_t *st) {
    (void)path;
    if (!st) return ERR(EFAULT);
    memset(st, 0, sizeof(*st));
    st->f_type    = 0x4d44;  /* MSDOS_SUPER_MAGIC (FAT32) */
    st->f_bsize   = 4096;
    st->f_blocks  = pmm_get_total_pages();
    st->f_bfree   = pmm_get_free_pages();
    st->f_bavail  = st->f_bfree;
    st->f_namelen = 255;
    return 0;
}

/* ── accept4 ─────────────────────────────────────────────────────────────── */
static int64_t lx64_accept4(uint64_t sockfd, linux64_sockaddr_t *addr,
                              uint64_t *addrlen, int flags) {
    (void)addr; (void)addrlen; (void)flags;
    (void)sockfd;
    return ERR(EAGAIN);
}

/* ── signalfd4 (stub) ────────────────────────────────────────────────────── */
static vfs_node_t g_sigfd_node;
static uint32_t sigfd_noop_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf)
{ (void)n;(void)off;(void)sz;(void)buf; return 0; }

static int64_t lx64_signalfd4(int64_t fd, void *mask, uint64_t sigsz, int flags) {
    (void)mask;(void)sigsz;(void)flags;
    process_t *p = process_current();
    if (!p) return ERR(ENOMEM);
    if (fd >= 0) return fd; /* reuse existing fd */
    int newfd = find_free_fd(p);
    if (newfd < 0) return ERR(EMFILE);
    memset(&g_sigfd_node, 0, sizeof(vfs_node_t));
    memcpy(g_sigfd_node.name, "signalfd", 9);
    g_sigfd_node.flags = VFS_CHARDEV;
    g_sigfd_node.impl  = 0x5601;
    g_sigfd_node.read  = sigfd_noop_read;
    p->fds[newfd] = &g_sigfd_node;
    return newfd;
}

/* ── init ────────────────────────────────────────────────────────────────── */

void linux_syscall64_init(void) {
    memset(ksocks, 0, sizeof(ksocks));
    memset(unix_socks, 0, sizeof(unix_socks));
    memset(pipes, 0, sizeof(pipes));
    memset(g_epoll, 0, sizeof(g_epoll));
    memset(eventfds, 0, sizeof(eventfds));
    memset(timerfds, 0, sizeof(timerfds));
    lx64_rand_state = timer_get_ticks() ^ 0xBEEFCAFE12345678ULL;
    ser64("[LX64] syscall64 layer ready\r\n");
}

/* ── dispatcher ──────────────────────────────────────────────────────────── */

void linux_syscall64_handler(syscall64_frame_t *f) {
    uint64_t nr = f->rax;
    int64_t ret = ERR(ENOSYS);

    /* Trace first few calls */
    {
        static uint32_t dbg = 0;
        if (dbg < 30) {
            dbg++;
            ser64("[LX64] #");
            ser64_hex(nr);
            ser64(" rdi="); ser64_hex(f->rdi);
            ser64("\r\n");
        }
    }

    switch (nr) {
    /* file I/O */
    case SYS64_READ:       ret = lx64_read(f->rdi, (char*)(uintptr_t)f->rsi, f->rdx); break;
    case SYS64_WRITE:      ret = lx64_write(f->rdi, (const char*)(uintptr_t)f->rsi, f->rdx); break;
    case SYS64_OPEN:       ret = lx64_open_at(AT_FDCWD,(const char*)(uintptr_t)f->rdi,f->rsi,f->rdx); break;
    case SYS64_OPENAT:     ret = lx64_open_at((int64_t)f->rdi,(const char*)(uintptr_t)f->rsi,f->rdx,f->r10); break;
    case SYS64_CLOSE:      ret = lx64_close(f->rdi); break;
    case SYS64_STAT:       ret = lx64_stat((const char*)(uintptr_t)f->rdi,(linux64_stat_t*)(uintptr_t)f->rsi); break;
    case SYS64_FSTAT:      ret = lx64_fstat(f->rdi,(linux64_stat_t*)(uintptr_t)f->rsi); break;
    case SYS64_LSTAT:      ret = lx64_stat((const char*)(uintptr_t)f->rdi,(linux64_stat_t*)(uintptr_t)f->rsi); break;
    case SYS64_NEWFSTATAT: ret = lx64_stat((const char*)(uintptr_t)f->rsi,(linux64_stat_t*)(uintptr_t)f->rdx); break;
    case SYS64_LSEEK:      ret = lx64_lseek(f->rdi,(int64_t)f->rsi,f->rdx); break;
    case SYS64_PREAD64:    ret = lx64_pread64(f->rdi,(char*)(uintptr_t)f->rsi,f->rdx,f->r10); break;
    case SYS64_PWRITE64:   ret = lx64_pwrite64(f->rdi,(const char*)(uintptr_t)f->rsi,f->rdx,f->r10); break;
    case SYS64_READV:      ret = lx64_readv(f->rdi,(linux64_iovec_t*)(uintptr_t)f->rsi,f->rdx); break;
    case SYS64_WRITEV:     ret = lx64_writev(f->rdi,(linux64_iovec_t*)(uintptr_t)f->rsi,f->rdx); break;
    case SYS64_DUP:        ret = lx64_dup(f->rdi); break;
    case SYS64_DUP2:       ret = lx64_dup2(f->rdi,f->rsi); break;
    case SYS64_DUP3:       ret = lx64_dup2(f->rdi,f->rsi); break;
    case SYS64_FCNTL:      ret = lx64_fcntl(f->rdi,f->rsi,f->rdx); break;
    case SYS64_FSYNC:      ret = lx64_fsync(f->rdi); break;
    case SYS64_TRUNCATE:   ret = 0; break;
    case SYS64_FTRUNCATE:  ret = lx64_ftruncate(f->rdi,f->rsi); break;
    case SYS64_GETDENTS:   /* fallthrough */
    case SYS64_GETDENTS64: ret = lx64_getdents64(f->rdi,(linux64_dirent_t*)(uintptr_t)f->rsi,f->rdx); break;
    case SYS64_IOCTL:      ret = lx64_ioctl(f->rdi,f->rsi,f->rdx); break;
    case SYS64_POLL:       ret = lx64_poll((linux64_pollfd_t*)(uintptr_t)f->rdi,f->rsi,(int64_t)f->rdx); break;
    case SYS64_SELECT:     ret = 0; break;
    case SYS64_PIPE:       ret = lx64_pipe2((int32_t*)(uintptr_t)f->rdi,0); break;
    case SYS64_PIPE2:      ret = lx64_pipe2((int32_t*)(uintptr_t)f->rdi,f->rsi); break;
    case SYS64_MEMFD_CREATE: ret = lx64_memfd_create((const char*)(uintptr_t)f->rdi,f->rsi); break;

    /* memory */
    case SYS64_MMAP:       ret = lx64_mmap(f->rdi,f->rsi,f->rdx,f->r10,(int64_t)f->r8,f->r9); break;
    case SYS64_MPROTECT:   ret = lx64_mprotect(f->rdi,f->rsi,f->rdx); break;
    case SYS64_MUNMAP:     ret = lx64_munmap(f->rdi,f->rsi); break;
    case SYS64_BRK:        ret = lx64_brk(f->rdi); break;
    case SYS64_MREMAP:     ret = lx64_mremap(f->rdi,f->rsi,f->rdx,f->r10); break;
    case SYS64_MADVISE:    ret = 0; break;

    /* process */
    case SYS64_GETPID:     ret = lx64_getpid(); break;
    case SYS64_GETTID:     ret = lx64_gettid(); break;
    case SYS64_GETPPID:    ret = lx64_getppid(); break;
    case SYS64_GETUID:     ret = lx64_getuid(); break;
    case SYS64_GETGID:     ret = lx64_getgid(); break;
    case SYS64_GETEUID:    ret = lx64_geteuid(); break;
    case SYS64_GETEGID:    ret = lx64_getegid(); break;
    case SYS64_SETUID: case SYS64_SETGID: ret = 0; break;
    case SYS64_SETSID:     ret = lx64_setsid(); break;
    case SYS64_UMASK:      ret = lx64_umask(f->rdi); break;
    case SYS64_EXIT:       /* fallthrough */
    case SYS64_EXIT_GROUP: ret = lx64_exit((int64_t)f->rdi); break;
    case SYS64_FORK:       ret = lx64_fork(f); break;
    case SYS64_VFORK:      ret = lx64_fork(f); break;
    case SYS64_CLONE:      ret = lx64_clone(f,f->rdi,f->rsi,f->rdx,f->r10,f->r8); break;
    case SYS64_WAIT4:      ret = lx64_wait4(f,(int64_t)f->rdi,(int32_t*)(uintptr_t)f->rsi,f->rdx,(void*)(uintptr_t)f->r10); break;
    case SYS64_KILL:       ret = lx64_kill((int64_t)f->rdi,(int64_t)f->rsi); break;
    case SYS64_TGKILL:     ret = 0; break;
    case SYS64_SETPGID:    ret = lx64_setpgid(f->rdi,f->rsi); break;
    case SYS64_GETPGID:    ret = lx64_getpgid(f->rdi); break;
    case SYS64_PRCTL:      ret = lx64_prctl((int)f->rdi,f->rsi,f->rdx,f->r10,f->r8); break;
    case SYS64_EXECVE:     ret = lx64_execve(f,(const char*)(uintptr_t)f->rdi,(uint64_t*)(uintptr_t)f->rsi,(uint64_t*)(uintptr_t)f->rdx); break;
    case SYS64_SCHED_YIELD: ret = 0; schedule(); break;
    case SYS64_SET_TID_ADDR: ret = lx64_set_tid_address((uint64_t*)(uintptr_t)f->rdi); break;

    /* time */
    case SYS64_CLOCK_GETTIME: ret = lx64_clock_gettime(f->rdi,(linux64_timespec_t*)(uintptr_t)f->rsi); break;
    case SYS64_CLOCK_GETRES:  ret = lx64_clock_gettime(f->rdi,(linux64_timespec_t*)(uintptr_t)f->rsi); break;
    case SYS64_GETTIMEOFDAY:  ret = lx64_gettimeofday((linux64_timeval_t*)(uintptr_t)f->rdi,(void*)(uintptr_t)f->rsi); break;
    case SYS64_NANOSLEEP:     ret = lx64_nanosleep((linux64_timespec_t*)(uintptr_t)f->rdi,(linux64_timespec_t*)(uintptr_t)f->rsi); break;

    /* arch */
    case SYS64_ARCH_PRCTL: ret = lx64_arch_prctl(f->rdi,f->rsi); break;

    /* threading */
    case SYS64_FUTEX:      ret = lx64_futex((uint32_t*)(uintptr_t)f->rdi,(int64_t)f->rsi,(uint32_t)f->rdx,(linux64_timespec_t*)(uintptr_t)f->r10,(uint32_t*)(uintptr_t)f->r8,(uint32_t)f->r9); break;

    /* dir */
    case SYS64_GETCWD:     ret = lx64_getcwd((char*)(uintptr_t)f->rdi,f->rsi); break;
    case SYS64_CHDIR:      ret = lx64_chdir((const char*)(uintptr_t)f->rdi); break;
    case SYS64_FCHDIR:     ret = 0; break;
    case SYS64_MKDIR:      ret = lx64_mkdir((const char*)(uintptr_t)f->rdi,f->rsi); break;
    case SYS64_MKDIRAT:    ret = lx64_mkdir((const char*)(uintptr_t)f->rsi,f->rdx); break;
    case SYS64_RMDIR:      ret = lx64_unlink((const char*)(uintptr_t)f->rdi); break;
    case SYS64_UNLINK:     ret = lx64_unlink((const char*)(uintptr_t)f->rdi); break;
    case SYS64_UNLINKAT:   ret = lx64_unlink((const char*)(uintptr_t)f->rsi); break;
    case SYS64_RENAME:     ret = lx64_rename((const char*)(uintptr_t)f->rdi,(const char*)(uintptr_t)f->rsi); break;
    case SYS64_ACCESS:     ret = lx64_access((const char*)(uintptr_t)f->rdi,f->rsi); break;
    case SYS64_FACCESSAT:  ret = lx64_access((const char*)(uintptr_t)f->rsi,f->rdx); break;
    case SYS64_READLINK:   ret = lx64_readlink((const char*)(uintptr_t)f->rdi,(char*)(uintptr_t)f->rsi,f->rdx); break;
    case SYS64_CHMOD:      ret = lx64_chmod((const char*)(uintptr_t)f->rdi,f->rsi); break;
    case SYS64_FCHMOD:     ret = lx64_fchmod(f->rdi,f->rsi); break;
    case SYS64_CHOWN: case SYS64_LCHOWN:
        ret = lx64_chown((const char*)(uintptr_t)f->rdi,f->rsi,f->rdx); break;
    case SYS64_FCHOWN:     ret = lx64_fchown(f->rdi,f->rsi,f->rdx); break;
    case SYS64_SYMLINK:    ret = lx64_symlink((const char*)(uintptr_t)f->rdi,(const char*)(uintptr_t)f->rsi); break;
    case SYS64_CREAT:      ret = lx64_open_at(AT_FDCWD,(const char*)(uintptr_t)f->rdi,O_CREAT|O_WRONLY|O_TRUNC,f->rsi); break;
    case SYS64_LINK:       ret = lx64_link((const char*)(uintptr_t)f->rdi,(const char*)(uintptr_t)f->rsi); break;

    /* sysinfo */
    case SYS64_UNAME:      ret = lx64_uname((linux64_utsname_t*)(uintptr_t)f->rdi); break;
    case SYS64_SYSINFO:    ret = lx64_sysinfo((linux64_sysinfo_t*)(uintptr_t)f->rdi); break;
    case SYS64_GETRLIMIT:  ret = lx64_getrlimit(f->rdi,(linux64_rlimit_t*)(uintptr_t)f->rsi); break;
    case 98: /*getrusage*/ ret = 0; break;

    /* signals */
    case SYS64_RT_SIGACTION:  ret = lx64_rt_sigaction((int64_t)f->rdi,(void*)(uintptr_t)f->rsi,(void*)(uintptr_t)f->rdx,f->r10); break;
    case SYS64_RT_SIGPROCMASK: ret = lx64_rt_sigprocmask((int64_t)f->rdi,(void*)(uintptr_t)f->rsi,(void*)(uintptr_t)f->rdx,f->r10); break;
    case SYS64_RT_SIGRETURN:  ret = 0; break;
    case SYS64_SIGALTSTACK:   ret = lx64_sigaltstack((void*)(uintptr_t)f->rdi,(void*)(uintptr_t)f->rsi); break;
    case 37: /*alarm*/  ret = 0; break;
    case 34: /*pause*/  ret = 0; break;

    /* sockets */
    case SYS64_SOCKET:     ret = lx64_socket(f->rdi,f->rsi,f->rdx); break;
    case SYS64_CONNECT:    ret = lx64_connect(f->rdi,(linux64_sockaddr_t*)(uintptr_t)f->rsi,f->rdx); break;
    case SYS64_BIND:       ret = lx64_bind(f->rdi,(linux64_sockaddr_t*)(uintptr_t)f->rsi,f->rdx); break;
    case SYS64_LISTEN:     ret = lx64_listen(f->rdi,f->rsi); break;
    case SYS64_ACCEPT:     ret = ERR(ENOSYS); break;
    case SYS64_SENDTO:     ret = lx64_sendto(f->rdi,(const char*)(uintptr_t)f->rsi,f->rdx,f->r10,(linux64_sockaddr_t*)(uintptr_t)f->r8,f->r9); break;
    case SYS64_RECVFROM:   ret = lx64_recvfrom(f->rdi,(char*)(uintptr_t)f->rsi,f->rdx,f->r10,(linux64_sockaddr_t*)(uintptr_t)f->r8,(uint64_t*)(uintptr_t)f->r9); break;
    case SYS64_SHUTDOWN:   ret = lx64_shutdown(f->rdi,f->rsi); break;
    case SYS64_SETSOCKOPT: ret = lx64_setsockopt(f->rdi,f->rsi,f->rdx,(void*)(uintptr_t)f->r10,f->r8); break;
    case SYS64_GETSOCKOPT: ret = 0; break;
    case SYS64_GETSOCKNAME: ret = lx64_getsockname(f->rdi,(linux64_sockaddr_t*)(uintptr_t)f->rsi,(uint64_t*)(uintptr_t)f->rdx); break;
    case SYS64_GETPEERNAME: ret = 0; break;
    case 53: /* socketpair */ ret = lx64_socketpair(f->rdi,f->rsi,f->rdx,(int32_t*)(uintptr_t)f->r10); break;
    case SYS64_SENDMSG:    ret = lx64_sendmsg(f->rdi,(linux64_msghdr_t*)(uintptr_t)f->rsi,f->rdx); break;
    case SYS64_RECVMSG:    ret = lx64_recvmsg(f->rdi,(linux64_msghdr_t*)(uintptr_t)f->rsi,f->rdx); break;

    /* random */
    case SYS64_GETRANDOM:  ret = lx64_getrandom((char*)(uintptr_t)f->rdi,f->rsi,f->rdx); break;

    /* misc stubs */
    case 100: /* times */   ret = 0; break;
    case SYS64_EVENTFD:        ret = lx64_eventfd2(f->rdi,(int)f->rsi); break;
    case SYS64_EVENTFD2:       ret = lx64_eventfd2(f->rdi,(int)f->rsi); break;
    case SYS64_TIMERFD_CREATE: ret = lx64_timerfd_create((int)f->rdi,(int)f->rsi); break;
    case SYS64_TIMERFD_SETTIME: ret = lx64_timerfd_settime(f->rdi,(int)f->rsi,(lx_itimerspec2_t*)(uintptr_t)f->rdx,(lx_itimerspec2_t*)(uintptr_t)f->r10); break;
    case SYS64_TIMERFD_GETTIME: ret = lx64_timerfd_gettime(f->rdi,(lx_itimerspec2_t*)(uintptr_t)f->rsi); break;
    case SYS64_EPOLL_CREATE:   /* fallthrough */
    case SYS64_EPOLL_CREATE1:  ret = lx64_epoll_create1((int)f->rdi); break;
    case SYS64_EPOLL_CTL:      ret = lx64_epoll_ctl(f->rdi,(int)f->rsi,f->rdx,(epoll_event_t*)(uintptr_t)f->r10); break;
    case SYS64_EPOLL_WAIT:     ret = lx64_epoll_wait(f->rdi,(epoll_event_t*)(uintptr_t)f->rsi,(int)f->rdx,(int)f->r10); break;
    case SYS64_INOTIFY_INIT:   /* fallthrough */
    case SYS64_INOTIFY_INIT1:  ret = lx64_inotify_init1((int)f->rdi); break;
    case SYS64_INOTIFY_ADD_WATCH: ret = 1; break; /* stub: return watch descriptor */
    case SYS64_INOTIFY_RM_WATCH:  ret = 0; break;
    case SYS64_ACCEPT4:        ret = lx64_accept4(f->rdi,(linux64_sockaddr_t*)(uintptr_t)f->rsi,(uint64_t*)(uintptr_t)f->rdx,(int)f->r10); break;
    case SYS64_SIGNALFD4:      ret = lx64_signalfd4((int64_t)f->rdi,(void*)(uintptr_t)f->rsi,f->rdx,(int)f->r10); break;
    case SYS64_STATFS:         ret = lx64_statfs((const char*)(uintptr_t)f->rdi,(linux64_statfs_t*)(uintptr_t)f->rsi); break;
    case SYS64_FSTATFS:        ret = lx64_statfs(0,(linux64_statfs_t*)(uintptr_t)f->rsi); break;
    case SYS64_MEMBARRIER:     ret = 0; break;
    case 302: /* prlimit64 */  ret = 0; break;
    case 149: /* mlock */      ret = 0; break;
    case 150: /* munlock */    ret = 0; break;
    case 334: /* rseq */         ret = ERR(ENOSYS); break;
    case 435: /* clone3 */       ret = ERR(ENOSYS); break;
    case 275: /* splice */       ret = ERR(ENOSYS); break;
    case 285: /* fallocate */    ret = 0; break;
    case 316: /* renameat2 */    ret = 0; break;
    case 326: /* copy_file_range */ ret = ERR(ENOSYS); break;
    case 139: /* sched_getparam */     ret = 0; break;
    case 141: /* sched_setaffinity */  ret = 0; break;
    case 142: /* sched_getaffinity */  ret = 0; break;
    case 115: /* getgroups */          ret = 0; break;
    case 116: /* setgroups */          ret = 0; break;
    case 122: /* getpgrp */            ret = (int64_t)process_current()->pid; break;
    case 124: /* getsid */             ret = (int64_t)process_current()->pid; break;
    case 125: /* capget */             ret = 0; break;
    case 126: /* capset */             ret = 0; break;
    case 130: /* mknod */              ret = 0; break;
    case 132: /* utime */              ret = 0; break;
    case 133: /* mknodat */            ret = 0; break;
    case 151: /* mlockall */           ret = 0; break;
    case 152: /* munlockall */         ret = 0; break;
    case 153: /* vhangup */            ret = 0; break;
    case 154: /* modify_ldt */         ret = ERR(ENOSYS); break;
    case 203: /* sched_setaffinity2 */ ret = 0; break;
    case 204: /* sched_getaffinity2 */ ret = 0; break;

    default:
        ser64("[LX64] UNHANDLED syscall #");
        ser64_hex(nr);
        ser64("\r\n");
        ret = ERR(ENOSYS);
        break;
    }

    f->rax = (uint64_t)ret;
}
