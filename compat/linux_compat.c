

#include "linux_compat.h"
#include <io.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <fs/vfs.h>
#include <drivers/vga.h>
#include <drivers/framebuffer.h>
#include <kernel/timer.h>
#include <kernel/gdt.h>
#include <net/socket.h>
#include <lib/string.h>
#include <types.h>



typedef struct {
    uint64_t st_dev;
    uint8_t  __pad0[4];
    uint32_t __st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint8_t  __pad3[4];
    int64_t  st_size;
    uint32_t st_blksize;
    uint64_t st_blocks;
    uint32_t st_atime;
    uint32_t st_atime_nsec;
    uint32_t st_mtime;
    uint32_t st_mtime_nsec;
    uint32_t st_ctime;
    uint32_t st_ctime_nsec;
    uint64_t st_ino;
} __attribute__((packed)) linux_stat64_t;

typedef struct {
    uint32_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint32_t st_rdev;
    uint32_t st_size;
    uint32_t st_blksize;
    uint32_t st_blocks;
    uint32_t st_atime;
    uint32_t st_atime_nsec;
    uint32_t st_mtime;
    uint32_t st_mtime_nsec;
    uint32_t st_ctime;
    uint32_t st_ctime_nsec;
} linux_stat_t;

typedef struct {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[256];
} __attribute__((packed)) linux_dirent64_t;

typedef struct {
    uint32_t d_ino;
    uint32_t d_off;
    uint16_t d_reclen;
    char     d_name[256];
} linux_dirent_t;

typedef struct { long tv_sec; long tv_usec; } linux_timeval_t;
typedef struct { long tv_sec; long tv_nsec; } linux_timespec_t;
typedef struct {
    uint32_t uptime;
    uint32_t loads[3];
    uint32_t totalram;
    uint32_t freeram;
    uint32_t sharedram;
    uint32_t bufferram;
    uint32_t totalswap;
    uint32_t freeswap;
    uint16_t procs;
    uint32_t totalhigh;
    uint32_t freehigh;
    uint32_t mem_unit;
    char _f[8];
} linux_sysinfo_t;

typedef struct {
    uint32_t rlim_cur;
    uint32_t rlim_max;
} linux_rlimit_t;


typedef struct { void *iov_base; uint32_t iov_len; } linux_iovec_t;


#define LINUX_HEAP_BASE  0x08800000U


#undef O_RDONLY
#undef O_WRONLY
#undef O_RDWR
#undef O_CREAT
#undef O_TRUNC
#undef O_APPEND
#define O_RDONLY   0
#define O_WRONLY   1
#define O_RDWR     2
#define O_CREAT    0100
#define O_TRUNC    01000
#define O_APPEND   02000
#define O_NONBLOCK 04000


#define VFS_O_RDONLY   0x00
#define VFS_O_WRONLY   0x01


#ifndef offsetof
#define offsetof(type, member) ((uint32_t)&((type *)0)->member)
#endif


static char g_cwd[256] = "/";
#define SEEK_SET   0
#define SEEK_CUR   1
#define SEEK_END   2


#define S_IFMT   0xF000
#define S_IFREG  0x8000
#define S_IFDIR  0x4000
#define S_IFCHR  0x2000
#define S_IFLNK  0xA000


#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK    10
#define DT_SOCK   12


static linux_output_fn g_output_fn = 0;
void linux_compat_set_output(linux_output_fn fn) { g_output_fn = fn; }


static void fill_stat64(linux_stat64_t *s, vfs_node_t *node) {
    memset(s, 0, sizeof(*s));
    bool is_dir = (node->flags & VFS_DIRECTORY) != 0;
    s->st_mode    = is_dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    s->st_nlink   = 1;
    s->st_size    = (int64_t)node->size;
    s->st_blksize = 4096;
    s->st_blocks  = ((uint64_t)node->size + 511) / 512;
    s->st_ino     = node->inode;
    s->st_atime   = s->st_mtime = s->st_ctime = timer_get_ticks() / 1000;
}

static void fill_stat(linux_stat_t *s, vfs_node_t *node) {
    memset(s, 0, sizeof(*s));
    bool is_dir = (node->flags & VFS_DIRECTORY) != 0;
    s->st_mode  = is_dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    s->st_nlink = 1;
    s->st_size  = node->size;
    s->st_ino   = node->inode;
    s->st_blksize = 4096;
    s->st_blocks  = (node->size + 511) / 512;
    s->st_atime = s->st_mtime = s->st_ctime = timer_get_ticks() / 1000;
}


#define DEV_NULL    1
#define DEV_ZERO    2
#define DEV_URANDOM 3
#define DEV_FB0     4
#define DEV_TTY     5


#define IS_DEV_FD(p) (((uintptr_t)(p)) < 16 && ((uintptr_t)(p)) > 0)
#define DEV_FD(id)   ((vfs_node_t *)((uintptr_t)(id)))
#define FD_DEV_ID(p) ((uint8_t)((uintptr_t)(p)))

static uint32_t g_fb_mmap_base = 0xE0000000U;


static int32_t lx_open(const char *path, uint32_t flags, uint32_t mode) {
    (void)mode;
    if (!path) return -LINUX_EINVAL;

    
    if (strcmp(path, "/dev/null") == 0 || strcmp(path, "/dev/zero") == 0 ||
        strcmp(path, "/dev/urandom") == 0 || strcmp(path, "/dev/random") == 0 ||
        strcmp(path, "/dev/tty") == 0 || strcmp(path, "/dev/console") == 0 ||
        strcmp(path, "/dev/stdin") == 0 || strcmp(path, "/dev/stdout") == 0 ||
        strcmp(path, "/dev/stderr") == 0) {
        process_t *p = process_current();
        if (!p) return -LINUX_EINVAL;
        uint8_t devid = DEV_NULL;
        if (strcmp(path, "/dev/zero") == 0)    devid = DEV_ZERO;
        if (strcmp(path, "/dev/urandom") == 0 ||
            strcmp(path, "/dev/random") == 0)  devid = DEV_URANDOM;
        if (strcmp(path, "/dev/tty") == 0 ||
            strcmp(path, "/dev/console") == 0 ||
            strcmp(path, "/dev/stdout") == 0 ||
            strcmp(path, "/dev/stderr") == 0)  devid = DEV_TTY;
        if (strcmp(path, "/dev/stdin") == 0)   devid = DEV_NULL;
        uint32_t fd;
        for (fd = 3; fd < MAX_FDS; fd++) {
            if (!p->fds[fd]) {
                p->fds[fd] = DEV_FD(devid);
                p->fd_offsets[fd] = 0;
                return (int32_t)fd;
            }
        }
        return -LINUX_ENOMEM;
    }

    if (strcmp(path, "/dev/fb0") == 0) {
        process_t *p = process_current();
        if (!p) return -LINUX_EINVAL;
        uint32_t fd;
        for (fd = 3; fd < MAX_FDS; fd++) {
            if (!p->fds[fd]) {
                p->fds[fd] = DEV_FD(DEV_FB0);
                p->fd_offsets[fd] = 0;
                return (int32_t)fd;
            }
        }
        return -LINUX_ENOMEM;
    }

    
    if (strncmp(path, "/proc/", 6) == 0 ||
        strncmp(path, "/sys/", 5) == 0) {
        return -LINUX_ENOENT;
    }

    vfs_node_t *node = vfs_resolve(path);
    if (!node) {
        
        if (flags & O_CREAT) {
            
            const char *slash = path + strlen(path) - 1;
            while (slash > path && *slash != '/') slash--;
            if (slash == path) return -LINUX_ENOENT;
            char parent_path[256];
            uint32_t plen = (uint32_t)(slash - path);
            if (plen == 0) plen = 1;
            memcpy(parent_path, path, plen);
            parent_path[plen] = '\0';
            vfs_node_t *parent = vfs_resolve(parent_path);
            if (!parent) return -LINUX_ENOENT;
            if (vfs_create(parent, slash + 1, 0644) < 0) return -LINUX_ENOMEM;
            node = vfs_finddir(parent, slash + 1);
            if (!node) return -LINUX_ENOMEM;
        } else {
            return -LINUX_ENOENT;
        }
    }
    if ((node->flags & VFS_DIRECTORY) && !(flags & 0200000)) {
        
    }
    process_t *p = process_current();
    if (!p) return -LINUX_EINVAL;
    uint32_t fd;
    for (fd = 3; fd < MAX_FDS; fd++) {
        if (!p->fds[fd]) {
            vfs_open(node, (flags & O_WRONLY) ? VFS_O_WRONLY : VFS_O_RDONLY);
            p->fds[fd]        = node;
            p->fd_offsets[fd] = (flags & O_APPEND) ? node->size : 0;
            if (flags & O_TRUNC) {
                p->fd_offsets[fd] = 0; 
            }
            return (int32_t)fd;
        }
    }
    return -LINUX_ENOMEM;
}


static int32_t lx_read(uint32_t fd, char *buf, uint32_t count) {
    if (!buf || count == 0) return -LINUX_EINVAL;
    process_t *p = process_current();
    if (fd == 0) return 0; 
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return -LINUX_EBADF;
    if (IS_DEV_FD(p->fds[fd])) {
        uint8_t id = FD_DEV_ID(p->fds[fd]);
        if (id == DEV_NULL) return 0;
        if (id == DEV_ZERO) { memset(buf, 0, count); return (int32_t)count; }
        if (id == DEV_URANDOM) {
            uint32_t i; for(i=0;i<count;i++) buf[i]=(char)(timer_get_ticks()^(i*0x6B));
            return (int32_t)count;
        }
        if (id == DEV_TTY) return 0;
        return -LINUX_EINVAL;
    }
    int32_t n = (int32_t)vfs_read(p->fds[fd], p->fd_offsets[fd], count, (uint8_t *)buf);
    if (n > 0) p->fd_offsets[fd] += (uint32_t)n;
    return n;
}


static void lx_ser_putc(char c) {
    
    while (!(inb(0x3FD) & 0x20));
    outb(0x3F8, (uint8_t)c);
}
static void lx_ser_write(const char *buf, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (buf[i] == '\n') lx_ser_putc('\r');
        lx_ser_putc(buf[i]);
    }
}


static int32_t lx_write(uint32_t fd, const char *buf, uint32_t count) {
    if (!buf || count == 0) return -LINUX_EINVAL;
    if (fd == 1 || fd == 2) {
        uint32_t i;
        if (g_output_fn) { for(i=0;i<count;i++) g_output_fn(buf[i]); }
        else { for(i=0;i<count;i++) vga_putchar(buf[i]); }
        lx_ser_write(buf, count);   
        return (int32_t)count;
    }
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return -LINUX_EBADF;
    if (IS_DEV_FD(p->fds[fd])) {
        uint8_t id = FD_DEV_ID(p->fds[fd]);
        if (id == DEV_NULL || id == DEV_TTY) return (int32_t)count;
        return -LINUX_EINVAL;
    }
    uint32_t written = vfs_write(p->fds[fd], p->fd_offsets[fd], count, (const uint8_t *)buf);
    p->fd_offsets[fd] += written;
    return (int32_t)written;
}


static int32_t lx_close(uint32_t fd) {
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return -LINUX_EBADF;
    if (!IS_DEV_FD(p->fds[fd])) vfs_close(p->fds[fd]);
    p->fds[fd] = 0; p->fd_offsets[fd] = 0;
    return 0;
}


static int32_t lx_lseek(uint32_t fd, int32_t offset, uint32_t whence) {
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return -LINUX_EBADF;
    if (IS_DEV_FD(p->fds[fd])) return (int32_t)p->fd_offsets[fd];
    switch (whence) {
        case SEEK_SET: p->fd_offsets[fd]  = (uint32_t)offset; break;
        case SEEK_CUR: p->fd_offsets[fd] += (uint32_t)offset; break;
        case SEEK_END: p->fd_offsets[fd]  = p->fds[fd]->size + (uint32_t)offset; break;
        default: return -LINUX_EINVAL;
    }
    return (int32_t)p->fd_offsets[fd];
}


static int32_t lx_llseek(uint32_t fd, uint32_t offhi, uint32_t offlo,
                           int64_t *result, uint32_t whence) {
    int64_t off = ((int64_t)offhi << 32) | offlo;
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return -LINUX_EBADF;
    uint64_t pos;
    switch (whence) {
        case SEEK_SET: pos = (uint64_t)off; break;
        case SEEK_CUR: pos = p->fd_offsets[fd] + (uint64_t)off; break;
        case SEEK_END: pos = p->fds[fd]->size  + (uint64_t)off; break;
        default: return -LINUX_EINVAL;
    }
    p->fd_offsets[fd] = (uint32_t)pos;
    if (result) *result = (int64_t)pos;
    return 0;
}


static int32_t lx_dup(uint32_t fd) {
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return -LINUX_EBADF;
    uint32_t nfd;
    for (nfd = 0; nfd < MAX_FDS; nfd++) {
        if (!p->fds[nfd]) {
            p->fds[nfd]        = p->fds[fd];
            p->fd_offsets[nfd] = p->fd_offsets[fd];
            return (int32_t)nfd;
        }
    }
    return -LINUX_ENOMEM;
}

static int32_t lx_dup2(uint32_t oldfd, uint32_t newfd) {
    if (newfd >= MAX_FDS) return -LINUX_EBADF;
    process_t *p = process_current();
    if (!p || oldfd >= MAX_FDS || !p->fds[oldfd]) return -LINUX_EBADF;
    if (p->fds[newfd] && !IS_DEV_FD(p->fds[newfd])) vfs_close(p->fds[newfd]);
    p->fds[newfd]        = p->fds[oldfd];
    p->fd_offsets[newfd] = p->fd_offsets[oldfd];
    return (int32_t)newfd;
}


static int32_t lx_pipe(int32_t *fds) {
    if (!fds) return -LINUX_EINVAL;
    
    process_t *p = process_current();
    if (!p) return -LINUX_EINVAL;
    int32_t r = -1, w = -1;
    uint32_t fd;
    for (fd = 3; fd < MAX_FDS && (r<0||w<0); fd++) {
        if (!p->fds[fd]) {
            if (r < 0) { p->fds[fd]=DEV_FD(DEV_NULL); r=(int32_t)fd; }
            else       { p->fds[fd]=DEV_FD(DEV_NULL); w=(int32_t)fd; }
        }
    }
    if (r < 0 || w < 0) return -LINUX_ENOMEM;
    fds[0] = r; fds[1] = w;
    return 0;
}


static int32_t lx_stat64(const char *path, linux_stat64_t *buf) {
    if (!path || !buf) return -LINUX_EINVAL;
    if (strcmp(path, "/dev/fb0") == 0) {
        memset(buf, 0, sizeof(*buf));
        buf->st_mode = S_IFCHR | 0666;
        buf->st_rdev = (64ULL<<8)|0;
        return 0;
    }
    if (strcmp(path, "/dev/null") == 0 || strcmp(path, "/dev/zero") == 0 ||
        strcmp(path, "/dev/urandom") == 0 || strcmp(path, "/dev/tty") == 0) {
        memset(buf, 0, sizeof(*buf));
        buf->st_mode = S_IFCHR | 0666;
        return 0;
    }
    vfs_node_t *node = vfs_resolve(path);
    if (!node) return -LINUX_ENOENT;
    fill_stat64(buf, node);
    return 0;
}

static int32_t lx_lstat64(const char *path, linux_stat64_t *buf) {
    return lx_stat64(path, buf);
}

static int32_t lx_fstat64(uint32_t fd, linux_stat64_t *buf) {
    if (!buf) return -LINUX_EINVAL;
    if (fd == 0 || fd == 1 || fd == 2) {
        memset(buf, 0, sizeof(*buf));
        buf->st_mode = S_IFCHR | 0666;
        return 0;
    }
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return -LINUX_EBADF;
    if (IS_DEV_FD(p->fds[fd])) {
        memset(buf, 0, sizeof(*buf));
        uint8_t id = FD_DEV_ID(p->fds[fd]);
        if (id == DEV_FB0) {
            buf->st_mode = S_IFCHR | 0666;
            buf->st_size = (int64_t)(fb.width * fb.height * 4);
        } else {
            buf->st_mode = S_IFCHR | 0666;
        }
        return 0;
    }
    fill_stat64(buf, p->fds[fd]);
    return 0;
}

static int32_t lx_stat(const char *path, linux_stat_t *buf) {
    if (!path || !buf) return -LINUX_EINVAL;
    vfs_node_t *node = vfs_resolve(path);
    if (!node) return -LINUX_ENOENT;
    fill_stat(buf, node);
    return 0;
}

static int32_t lx_fstat(uint32_t fd, linux_stat_t *buf) {
    if (!buf) return -LINUX_EINVAL;
    if (fd <= 2) { memset(buf,0,sizeof(*buf)); buf->st_mode=S_IFCHR|0666; return 0; }
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return -LINUX_EBADF;
    if (IS_DEV_FD(p->fds[fd])) { memset(buf,0,sizeof(*buf)); buf->st_mode=S_IFCHR|0666; return 0; }
    fill_stat(buf, p->fds[fd]);
    return 0;
}


static int32_t lx_getdents64(uint32_t fd, linux_dirent64_t *dirp, uint32_t count) {
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return -LINUX_EBADF;
    if (IS_DEV_FD(p->fds[fd])) return -LINUX_ENOTDIR;
    vfs_node_t *dir = p->fds[fd];
    if (!(dir->flags & VFS_DIRECTORY)) return -LINUX_ENOTDIR;

    uint32_t written = 0;
    uint32_t idx = p->fd_offsets[fd];
    dirent_t *child = vfs_readdir(dir, idx);
    uint8_t *buf = (uint8_t *)dirp;
    uint32_t hdr = offsetof(linux_dirent64_t, d_name);

    while (child && written + sizeof(linux_dirent64_t) <= count) {
        linux_dirent64_t *ent = (linux_dirent64_t *)(buf + written);
        uint16_t namelen = (uint16_t)strlen(child->name);
        uint16_t reclen  = (uint16_t)((hdr + namelen + 1 + 7) & ~7U);
        memset(ent, 0, reclen);
        ent->d_ino    = child->inode;
        ent->d_off    = (int64_t)(idx + 1);
        ent->d_reclen = reclen;
        ent->d_type   = (child->type == VFS_DIRECTORY) ? DT_DIR : DT_REG;
        memcpy(ent->d_name, child->name, namelen);
        ent->d_name[namelen] = '\0';
        written += reclen;
        idx++;
        child = vfs_readdir(dir, idx);
    }
    p->fd_offsets[fd] = idx;
    return (int32_t)written;
}


static int32_t lx_readlink(const char *path, char *buf, uint32_t bufsz) {
    
    if (!path || !buf) return -LINUX_EINVAL;
    if (strcmp(path, "/proc/self/exe") == 0) {
        process_t *p = process_current();
        if (!p) return -LINUX_EINVAL;
        uint32_t len = (uint32_t)strlen(p->name);
        if (len > bufsz) len = bufsz;
        memcpy(buf, p->name, len);
        return (int32_t)len;
    }
    return -LINUX_EINVAL;
}


static int32_t lx_getcwd(char *buf, uint32_t size) {
    if (!buf || size == 0) return -LINUX_EINVAL;
    uint32_t len = (uint32_t)strlen(g_cwd);
    if (len + 1 > size) return -LINUX_ERANGE;
    memcpy(buf, g_cwd, len + 1);
    return (int32_t)(uintptr_t)buf;
}


static int32_t lx_chdir(const char *path) {
    if (!path) return -LINUX_EINVAL;
    vfs_node_t *node = vfs_resolve(path);
    if (!node || !(node->flags & VFS_DIRECTORY)) return -LINUX_ENOENT;
    uint32_t len = (uint32_t)strlen(path);
    if (len >= sizeof(g_cwd)) len = sizeof(g_cwd) - 1;
    memcpy(g_cwd, path, len); g_cwd[len] = '\0';
    return 0;
}


static int32_t lx_mkdir(const char *path, uint32_t mode) {
    (void)mode;
    if (!path) return -LINUX_EINVAL;
    const char *name = path;
    const char *p2 = path + strlen(path) - 1;
    while (p2 > path && *p2 != '/') p2--;
    if (*p2 == '/') name = p2 + 1;
    char parent[256]; uint32_t plen = (uint32_t)(p2 - path);
    if (plen == 0) { parent[0]='/'; parent[1]='\0'; plen=1; }
    else { memcpy(parent, path, plen); parent[plen]='\0'; }
    vfs_node_t *dir = vfs_resolve(parent);
    if (!dir) return -LINUX_ENOENT;
    int r = vfs_mkdir(dir, name, mode ? mode : 0755);
    return (r == 0) ? 0 : -LINUX_EEXIST;
}

static int32_t lx_unlink(const char *path) {
    if (!path) return -LINUX_EINVAL;
    char fname[256];
    vfs_node_t *parent = vfs_resolve_parent(path, fname);
    if (!parent) return -LINUX_ENOENT;
    return (vfs_unlink(parent, fname) == 0) ? 0 : -LINUX_EPERM;
}

static int32_t lx_rename(const char *old, const char *newp) {
    (void)old; (void)newp;
    return 0; 
}

static int32_t lx_access(const char *path, uint32_t mode) {
    (void)mode;
    if (!path) return -LINUX_EINVAL;
    if (strcmp(path, "/dev/fb0") == 0 || strcmp(path, "/dev/null") == 0)
        return 0;
    vfs_node_t *node = vfs_resolve(path);
    return node ? 0 : -LINUX_ENOENT;
}


static int32_t lx_brk(uint32_t addr) {
    process_t *p = process_current();
    if (!p) return -LINUX_ENOMEM;
    if (p->heap_start == 0) { p->heap_start = LINUX_HEAP_BASE; p->heap_end = LINUX_HEAP_BASE; }
    if (addr == 0) return (int32_t)p->heap_end;
    if (addr < p->heap_start) return -LINUX_ENOMEM;
    if (addr > p->heap_end) {
        uint32_t page = (p->heap_end + 0xFFF) & ~0xFFFU;
        uint32_t target = (addr + 0xFFF) & ~0xFFFU;
        while (page < target) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) return -LINUX_ENOMEM;
            vmm_map_page(p->page_dir, page, phys, PAGE_PRESENT|PAGE_WRITABLE|PAGE_USER);
            page += 0x1000;
        }
    }
    p->heap_end = addr;
    return (int32_t)addr;
}

static int32_t lx_mmap2(uint32_t addr, uint32_t length, uint32_t prot,
                          uint32_t mflags, int32_t fd, uint32_t pgoff) {
    if (length == 0) return -LINUX_EINVAL;
    uint32_t pages = (length + 0xFFFU) >> 12;
    uint32_t base  = addr ? (addr & ~0xFFFU) : 0x40000000U;

    
    process_t *p = process_current();
    if (fd >= 3 && p && fd < (int32_t)MAX_FDS && IS_DEV_FD(p->fds[fd])) {
        if (FD_DEV_ID(p->fds[fd]) == DEV_FB0) {
            base = g_fb_mmap_base;
            uint32_t fb_phys = (uint32_t)(uintptr_t)fb.backbuf;
            uint32_t i;
            for (i = 0; i < pages; i++) {
                vmm_map_page(p->page_dir, base + i*0x1000,
                             fb_phys + i*0x1000,
                             PAGE_PRESENT|PAGE_WRITABLE|PAGE_USER);
            }
            return (int32_t)base;
        }
    }

    
    if (fd >= 3 && p && fd < (int32_t)MAX_FDS && p->fds[fd] && !IS_DEV_FD(p->fds[fd])) {
        if (p) {
            uint32_t i;
            for (i = 0; i < pages; i++) {
                uint64_t phys = pmm_alloc_page();
                if (!phys) return -LINUX_ENOMEM;
                vmm_map_page(p->page_dir, base+i*0x1000, phys,
                             PAGE_PRESENT|PAGE_WRITABLE|PAGE_USER);
            }
            memset((void *)(uintptr_t)base, 0, pages*0x1000);
            uint32_t foff = pgoff * 0x1000;
            uint32_t flen = p->fds[fd]->size;
            uint32_t copy = flen > length ? length : flen;
            vfs_read(p->fds[fd], foff, copy, (uint8_t *)(uintptr_t)base);
            return (int32_t)base;
        }
    }

    (void)prot; (void)mflags;
    if (!p) return -LINUX_ENOMEM;
    uint32_t i;
    for (i = 0; i < pages; i++) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) return -LINUX_ENOMEM;
        vmm_map_page(p->page_dir, base+i*0x1000, phys,
                     PAGE_PRESENT|PAGE_WRITABLE|PAGE_USER);
    }
    memset((void *)(uintptr_t)base, 0, pages*0x1000);
    return (int32_t)base;
}

static int32_t lx_munmap(uint32_t addr, uint32_t length) {
    (void)addr; (void)length;
    return 0;
}

static int32_t lx_mprotect(uint32_t addr, uint32_t len, uint32_t prot) {
    (void)addr; (void)len; (void)prot;
    return 0; 
}

static int32_t lx_mremap(uint32_t old_addr, uint32_t old_size,
                           uint32_t new_size, uint32_t flags) {
    (void)flags;
    if (new_size <= old_size) return (int32_t)old_addr;
    uint32_t extra = new_size - old_size;
    uint32_t pages = (extra + 0xFFFU) >> 12;
    process_t *p = process_current();
    if (!p) return -LINUX_ENOMEM;
    uint32_t i;
    for (i = 0; i < pages; i++) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) return -LINUX_ENOMEM;
        vmm_map_page(p->page_dir, old_addr + old_size + i*0x1000, phys,
                     PAGE_PRESENT|PAGE_WRITABLE|PAGE_USER);
    }
    return (int32_t)old_addr;
}


static int32_t lx_exit(int32_t code) {
    process_t *p = process_current();
    if (p) { p->state = PROC_ZOMBIE; p->exit_code = code; }
    schedule();
    return 0;
}

static int32_t lx_getpid(void) {
    process_t *p = process_current();
    return p ? (int32_t)p->pid : 1;
}

static int32_t lx_getppid(void) { return 1; }
static int32_t lx_gettid(void)  { return lx_getpid(); }


static int32_t lx_getuid(void)  { return 0; }
static int32_t lx_getgid(void)  { return 0; }
static int32_t lx_geteuid(void) { return 0; }
static int32_t lx_getegid(void) { return 0; }


static int32_t lx_clone(uint32_t flags, uint32_t stack, uint32_t ptid,
                          uint32_t tls, uint32_t ctid) {
    (void)flags; (void)stack; (void)ptid; (void)tls; (void)ctid;
    
    return 0;
}

static int32_t lx_fork(void) {
    return 0; 
}

static int32_t lx_wait4(int32_t pid, int32_t *status, int32_t options, void *rusage) {
    (void)pid; (void)options; (void)rusage;
    if (status) *status = 0;
    return -LINUX_ECHILD;
}


static int32_t lx_gettimeofday(linux_timeval_t *tv, void *tz) {
    (void)tz;
    if (tv) {
        uint32_t ticks = timer_get_ticks();
        tv->tv_sec  = (long)(ticks / 1000);
        tv->tv_usec = (long)((ticks % 1000) * 1000);
    }
    return 0;
}

static int32_t lx_clock_gettime(uint32_t clk, linux_timespec_t *tp) {
    (void)clk;
    if (tp) {
        uint32_t ticks = timer_get_ticks();
        tp->tv_sec  = (long)(ticks / 1000);
        tp->tv_nsec = (long)((ticks % 1000) * 1000000L);
    }
    return 0;
}

static int32_t lx_time(uint32_t *tloc) {
    uint32_t t = timer_get_ticks() / 1000;
    if (tloc) *tloc = t;
    return (int32_t)t;
}

static int32_t lx_nanosleep(linux_timespec_t *req, linux_timespec_t *rem) {
    (void)rem;
    if (!req) return -LINUX_EINVAL;
    
    uint32_t ms = (uint32_t)(req->tv_sec * 1000 + req->tv_nsec / 1000000);
    if (ms > 10) ms = 10;
    uint32_t t0 = timer_get_ticks();
    while (timer_get_ticks() - t0 < ms) __asm__ volatile("pause");
    return 0;
}


typedef struct {
    char sysname[65]; char nodename[65]; char release[65];
    char version[65]; char machine[65];  char domainname[65];
} linux_utsname_t;

static int32_t lx_uname(linux_utsname_t *buf) {
    if (!buf) return -LINUX_EINVAL;
    memset(buf, 0, sizeof(*buf));
    memcpy(buf->sysname,    "Linux",   6);
    memcpy(buf->nodename,   "krypx",   6);
    memcpy(buf->release,    "5.15.0",  7);
    memcpy(buf->version,    "#1 SMP",  7);
    memcpy(buf->machine,    "i686",    5);
    memcpy(buf->domainname, "(none)",  7);
    return 0;
}

static int32_t lx_sysinfo(linux_sysinfo_t *info) {
    if (!info) return -LINUX_EINVAL;
    memset(info, 0, sizeof(*info));
    info->uptime    = timer_get_ticks() / 1000;
    info->totalram  = pmm_get_total_pages() * 4096;
    info->freeram   = pmm_get_free_pages()  * 4096;
    info->mem_unit  = 1;
    info->procs     = 1;
    return 0;
}



typedef struct {
    char  id[16]; uint32_t smem_start; uint32_t smem_len;
    uint32_t type; uint32_t type_aux; uint32_t visual;
    uint16_t xpanstep; uint16_t ypanstep; uint16_t ywrapstep;
    uint32_t line_length; uint32_t mmio_start; uint32_t mmio_len;
    uint32_t accel; uint16_t capabilities; uint16_t reserved[2];
} fb_fix_screeninfo_t;

typedef struct {
    uint32_t xres, yres, xres_virt, yres_virt;
    uint32_t xoffset, yoffset;
    uint32_t bits_per_pixel, grayscale;
    struct { uint32_t offset,length,msb_right; } red,green,blue,transp;
    uint32_t nonstd, activate;
    uint32_t height, width;
    uint32_t accel_flags;
    uint32_t pixclock, left_margin, right_margin, upper_margin, lower_margin;
    uint32_t hsync_len, vsync_len, sync, vmode, rotate, colorspace;
    uint32_t reserved[4];
} fb_var_screeninfo_t;

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

static int32_t lx_ioctl(uint32_t fd, uint32_t req, uint32_t arg) {
    process_t *p = process_current();
    bool is_fb = false;
    if (fd >= 3 && p && fd < MAX_FDS && IS_DEV_FD(p->fds[fd]))
        is_fb = (FD_DEV_ID(p->fds[fd]) == DEV_FB0);

    if (is_fb) {
        if (req == FBIOGET_VSCREENINFO) {
            fb_var_screeninfo_t *v = (fb_var_screeninfo_t *)arg;
            memset(v, 0, sizeof(*v));
            v->xres = v->xres_virt = fb.width;
            v->yres = v->yres_virt = fb.height;
            v->bits_per_pixel = 32;
            v->red.offset=16;   v->red.length=8;
            v->green.offset=8;  v->green.length=8;
            v->blue.offset=0;   v->blue.length=8;
            v->transp.offset=24;v->transp.length=8;
            return 0;
        }
        if (req == FBIOGET_FSCREENINFO) {
            fb_fix_screeninfo_t *f = (fb_fix_screeninfo_t *)arg;
            memset(f, 0, sizeof(*f));
            memcpy(f->id, "Krypx FB", 9);
            f->smem_start  = (uint32_t)(uintptr_t)fb.backbuf;
            f->smem_len    = fb.pitch * fb.height;
            f->type        = 0; 
            f->visual      = 2; 
            f->line_length = fb.pitch;
            return 0;
        }
        return 0;
    }
    
    if (req == 0x5401) return (fd<=2) ? 0 : -LINUX_ENOTTY;
    return 0;
}


static int32_t lx_fcntl64(uint32_t fd, uint32_t cmd, uint32_t arg) {
    (void)fd; (void)arg;
    if (cmd == 1) return 0; 
    if (cmd == 2) return 0; 
    if (cmd == 3) return 0; 
    if (cmd == 4) return 0; 
    return 0;
}


static int32_t lx_readv(uint32_t fd, linux_iovec_t *iov, uint32_t iovcnt) {
    int32_t total = 0;
    uint32_t i;
    for (i = 0; i < iovcnt; i++) {
        int32_t n = lx_read(fd, (char *)iov[i].iov_base, iov[i].iov_len);
        if (n < 0) return n;
        total += n;
    }
    return total;
}

static int32_t lx_writev(uint32_t fd, linux_iovec_t *iov, uint32_t iovcnt) {
    int32_t total = 0;
    uint32_t i;
    for (i = 0; i < iovcnt; i++) {
        int32_t n = lx_write(fd, (const char *)iov[i].iov_base, iov[i].iov_len);
        if (n < 0) return n;
        total += n;
    }
    return total;
}


static int32_t lx_select(uint32_t nfds, uint32_t *readfds, uint32_t *writefds,
                           uint32_t *exceptfds, linux_timeval_t *timeout) {
    (void)nfds; (void)readfds; (void)writefds; (void)exceptfds; (void)timeout;
    return 0;
}

typedef struct { int32_t fd; int16_t events; int16_t revents; } linux_pollfd_t;
static int32_t lx_poll(linux_pollfd_t *fds, uint32_t nfds, int32_t timeout) {
    (void)fds; (void)nfds; (void)timeout;
    return 0;
}


static int32_t lx_getrlimit(uint32_t resource, linux_rlimit_t *rlim) {
    if (!rlim) return -LINUX_EINVAL;
    rlim->rlim_cur = 0xFFFFFFFFU;
    rlim->rlim_max = 0xFFFFFFFFU;
    if (resource == 7) { 
        rlim->rlim_cur = MAX_FDS;
        rlim->rlim_max = MAX_FDS;
    }
    return 0;
}


static int32_t lx_futex(uint32_t *uaddr, int32_t op, uint32_t val,
                          linux_timespec_t *timeout, uint32_t *uaddr2, uint32_t val3) {
    (void)timeout; (void)uaddr2; (void)val3;
    int32_t cmd = op & 0x7F;
    if (cmd == 0) { 
        if (uaddr && *uaddr == val) {
            uint32_t t0 = timer_get_ticks();
            while (*uaddr == val && timer_get_ticks()-t0 < 10)
                __asm__ volatile("pause");
        }
        return 0;
    }
    if (cmd == 1) { 
        return 0;
    }
    return 0;
}


typedef struct {
    int32_t  entry_number;
    uint32_t base_addr;
    uint32_t limit;
    uint32_t flags;
} linux_user_desc_t;

static int32_t lx_set_thread_area(linux_user_desc_t *u) {
    if (!u) return -LINUX_EINVAL;
    if (u->entry_number == (int32_t)-1) u->entry_number = 6;
    set_fs_base((uint64_t)u->base_addr);
    return 0;
}

static int32_t lx_set_tid_addr(uint32_t *tidptr) {
    (void)tidptr;
    return lx_getpid();
}


static int32_t lx_socketcall(uint32_t call, uint32_t *args) {
    if (!args) return -LINUX_EINVAL;
    switch (call) {
        case 1: { 
            return socket_create((int)args[0], (int)args[1], (int)args[2]);
        }
        case 2: { 
            return 0;
        }
        case 3: { 
            sockaddr_in_t *sa = (sockaddr_in_t *)args[1];
            if (!sa) return -LINUX_EINVAL;
            
            uint16_t port = (uint16_t)(((sa->port & 0xFF) << 8) | (sa->port >> 8));
            return socket_connect((int)args[0], sa->addr, port);
        }
        case 9: { 
            return socket_send((int)args[0], (void*)args[1], (uint16_t)args[2]);
        }
        case 10: { 
            return socket_recv((int)args[0], (void*)args[1], (uint16_t)args[2]);
        }
        case 6: { 
            return socket_close((int)args[0]);
        }
        default: return -LINUX_ENOSYS;
    }
}


typedef struct {
    uint32_t addr, len, prot, flags;
    int32_t  fd;
    uint32_t offset;
} linux_mmap_args_t;

static int32_t lx_old_mmap(linux_mmap_args_t *a) {
    if (!a) return -LINUX_EINVAL;
    return lx_mmap2(a->addr, a->len, a->prot, a->flags, a->fd, a->offset >> 12);
}


static int32_t lx_ftruncate(uint32_t fd, uint32_t length) {
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return -LINUX_EBADF;
    if (IS_DEV_FD(p->fds[fd])) return -LINUX_EINVAL;
    (void)length; 
    return 0;
}


static uint32_t g_umask = 022;
static int32_t lx_umask(uint32_t mask) { uint32_t old=g_umask; g_umask=mask; return (int32_t)old; }


static int32_t lx_chmod(const char *path, uint32_t mode) { (void)path;(void)mode; return 0; }
static int32_t lx_chown(const char *path, uint32_t uid, uint32_t gid) { (void)path;(void)uid;(void)gid; return 0; }
static int32_t lx_fchmod(uint32_t fd, uint32_t mode) { (void)fd;(void)mode; return 0; }
static int32_t lx_fchown(uint32_t fd, uint32_t uid, uint32_t gid) { (void)fd;(void)uid;(void)gid; return 0; }


static int32_t lx_rt_sigaction(int32_t sig, void *act, void *oact, uint32_t sz) {
    (void)sig;(void)act;(void)oact;(void)sz;
    return 0;
}
static int32_t lx_rt_sigprocmask(int32_t how, void *set, void *oset, uint32_t sz) {
    (void)how;(void)set;(void)oset;(void)sz;
    return 0;
}
static int32_t lx_sigaltstack(void *ss, void *oss) { (void)ss;(void)oss; return 0; }
static int32_t lx_rt_sigreturn(void) { return 0; }


static int32_t lx_getdents(uint32_t fd, linux_dirent_t *dirp, uint32_t count) {
    
    process_t *p = process_current();
    if (!p || fd >= MAX_FDS || !p->fds[fd]) return -LINUX_EBADF;
    if (IS_DEV_FD(p->fds[fd])) return -LINUX_ENOTDIR;
    vfs_node_t *dir = p->fds[fd];
    if (!(dir->flags & VFS_DIRECTORY)) return -LINUX_ENOTDIR;
    uint32_t written = 0;
    uint32_t idx = p->fd_offsets[fd];
    dirent_t *child = vfs_readdir(dir, idx);
    uint8_t *buf = (uint8_t *)dirp;
    while (child && written + 280 <= count) {
        linux_dirent_t *ent = (linux_dirent_t *)(buf + written);
        uint16_t namelen = (uint16_t)strlen(child->name);
        uint16_t reclen  = (uint16_t)((8 + namelen + 2 + 7) & ~7U);
        ent->d_ino    = child->inode;
        ent->d_off    = idx + 1;
        ent->d_reclen = reclen;
        memcpy(ent->d_name, child->name, namelen);
        ent->d_name[namelen]   = '\0';
        ent->d_name[namelen+1] = (char)((child->type == VFS_DIRECTORY) ? DT_DIR : DT_REG);
        written += reclen;
        idx++;
        child = vfs_readdir(dir, idx);
    }
    p->fd_offsets[fd] = idx;
    return (int32_t)written;
}


static int32_t lx_fsync(uint32_t fd) { (void)fd; return 0; }


static int32_t lx_sched_yield(void) { schedule(); return 0; }


static int32_t lx_prctl(uint32_t op, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    (void)op;(void)a;(void)b;(void)c;(void)d;
    return 0;
}

void linux_syscall_handler(registers_t *regs) {
    int32_t ret = -LINUX_ENOSYS;

    
    {
        static uint32_t dbg_count = 0;
        if (dbg_count < 20) {
            dbg_count++;
            lx_ser_write("[LX] syscall eax=", 18);
            char tmp[12]; int ti = 11; tmp[ti] = '\0';
            uint32_t v = regs->rax;
            do { tmp[--ti] = (char)('0' + v % 10); v /= 10; } while (v && ti > 0);
            lx_ser_write(tmp + ti, 11 - ti);
            lx_ser_write("\r\n", 2);
        }
    }

    switch (regs->rax) {
    
    case 1:   ret = lx_exit((int32_t)regs->rbx); break;
    case 252: ret = lx_exit((int32_t)regs->rbx); break;

    
    case 2:   ret = lx_fork(); break;
    case 120: ret = lx_clone(regs->rbx,regs->rcx,regs->rdx,regs->rsi,regs->rdi); break;
    case 190: ret = lx_fork(); break;

    
    case 3:   ret = lx_read(regs->rbx, (char*)regs->rcx, regs->rdx); break;
    case 4:   ret = lx_write(regs->rbx, (const char*)regs->rcx, regs->rdx); break;
    case 5:   ret = lx_open((const char*)regs->rbx, regs->rcx, regs->rdx); break;
    case 6:   ret = lx_close(regs->rbx); break;

    
    case 7:   ret = lx_wait4((int32_t)regs->rbx,(int32_t*)regs->rcx,regs->rdx,0); break;
    case 114: ret = lx_wait4((int32_t)regs->rbx,(int32_t*)regs->rcx,(int32_t)regs->rdx,(void*)regs->rsi); break;

    
    case 19:  ret = lx_lseek(regs->rbx, (int32_t)regs->rcx, regs->rdx); break;
    case 140: ret = lx_llseek(regs->rbx, regs->rcx, regs->rdx, (int64_t*)regs->rsi, regs->rdi); break;

    
    case 41:  ret = lx_dup(regs->rbx); break;
    case 63:  ret = lx_dup2(regs->rbx, regs->rcx); break;

    
    case 42:  ret = lx_pipe((int32_t*)regs->rbx); break;

    
    case 20:  ret = lx_getpid(); break;
    case 64:  ret = lx_getppid(); break;
    case 224: ret = lx_gettid(); break;

    
    case 24: case 199: ret = lx_getuid(); break;
    case 47: case 200: ret = lx_getgid(); break;
    case 49: case 201: ret = lx_geteuid(); break;
    case 50: case 202: ret = lx_getegid(); break;

    
    case 45:  ret = lx_brk(regs->rbx); break;
    case 90:  ret = lx_old_mmap((linux_mmap_args_t*)regs->rbx); break;
    case 91:  ret = lx_munmap(regs->rbx, regs->rcx); break;
    case 125: ret = lx_mprotect(regs->rbx, regs->rcx, regs->rdx); break;
    case 163: ret = lx_mremap(regs->rbx, regs->rcx, regs->rdx, regs->rsi); break;
    case 192: ret = lx_mmap2(regs->rbx, regs->rcx, regs->rdx, regs->rsi,
                              (int32_t)regs->rdi, regs->rbp); break;

    
    case 33:  ret = lx_access((const char*)regs->rbx, regs->rcx); break;
    case 85:  ret = lx_readlink((const char*)regs->rbx,(char*)regs->rcx,regs->rdx); break;
    case 106: ret = lx_stat((const char*)regs->rbx,(linux_stat_t*)regs->rcx); break;
    case 107: ret = lx_stat((const char*)regs->rbx,(linux_stat_t*)regs->rcx); break;
    case 108: ret = lx_fstat(regs->rbx,(linux_stat_t*)regs->rcx); break;
    case 195: ret = lx_stat64((const char*)regs->rbx,(linux_stat64_t*)regs->rcx); break;
    case 196: ret = lx_lstat64((const char*)regs->rbx,(linux_stat64_t*)regs->rcx); break;
    case 197: ret = lx_fstat64(regs->rbx,(linux_stat64_t*)regs->rcx); break;

    
    case 141: ret = lx_getdents(regs->rbx,(linux_dirent_t*)regs->rcx,regs->rdx); break;
    case 220: ret = lx_getdents64(regs->rbx,(linux_dirent64_t*)regs->rcx,regs->rdx); break;
    case 12:  ret = lx_chdir((const char*)regs->rbx); break;
    case 183: ret = lx_getcwd((char*)regs->rbx, regs->rcx); break;
    case 39:  ret = lx_mkdir((const char*)regs->rbx, regs->rcx); break;
    case 10:  ret = lx_unlink((const char*)regs->rbx); break;
    case 40:  ret = 0; break; 
    case 38:  ret = lx_rename((const char*)regs->rbx,(const char*)regs->rcx); break;

    
    case 54:  ret = lx_ioctl(regs->rbx, regs->rcx, regs->rdx); break;
    case 55:  ret = lx_fcntl64(regs->rbx, regs->rcx, regs->rdx); break;
    case 221: ret = lx_fcntl64(regs->rbx, regs->rcx, regs->rdx); break;
    case 92:  ret = lx_ftruncate(regs->rbx, regs->rcx); break;
    case 194: ret = lx_ftruncate(regs->rbx, regs->rcx); break;
    case 118: ret = lx_fsync(regs->rbx); break;
    case 148: ret = lx_fsync(regs->rbx); break;
    case 60:  ret = lx_umask(regs->rbx); break;
    case 15:  ret = lx_chmod((const char*)regs->rbx, regs->rcx); break;
    case 94:  ret = lx_fchmod(regs->rbx, regs->rcx); break;
    case 182: ret = lx_chown((const char*)regs->rbx, regs->rcx, regs->rdx); break;
    case 207: ret = lx_fchown(regs->rbx, regs->rcx, regs->rdx); break;

    
    case 145: ret = lx_readv(regs->rbx,(linux_iovec_t*)regs->rcx,regs->rdx); break;
    case 146: ret = lx_writev(regs->rbx,(linux_iovec_t*)regs->rcx,regs->rdx); break;

    
    case 82: case 142: ret = lx_select(regs->rbx,(uint32_t*)regs->rcx,(uint32_t*)regs->rdx,(uint32_t*)regs->rsi,(linux_timeval_t*)regs->rdi); break;
    case 168: ret = lx_poll((linux_pollfd_t*)regs->rbx,regs->rcx,(int32_t)regs->rdx); break;

    
    case 13:  ret = lx_time((uint32_t*)regs->rbx); break;
    case 78:  ret = lx_gettimeofday((linux_timeval_t*)regs->rbx,(void*)regs->rcx); break;
    case 162: ret = lx_nanosleep((linux_timespec_t*)regs->rbx,(linux_timespec_t*)regs->rcx); break;
    case 265: ret = lx_clock_gettime(regs->rbx,(linux_timespec_t*)regs->rcx); break;
    case 263: ret = lx_clock_gettime(regs->rbx,(linux_timespec_t*)regs->rcx); break;

    
    case 116: ret = lx_sysinfo((linux_sysinfo_t*)regs->rbx); break;
    case 122: ret = lx_uname((linux_utsname_t*)regs->rbx); break;

    
    case 240: ret = lx_futex((uint32_t*)regs->rbx,(int32_t)regs->rcx,regs->rdx,(linux_timespec_t*)regs->rsi,(uint32_t*)regs->rdi,regs->rbp); break;
    case 243: ret = lx_set_thread_area((linux_user_desc_t*)regs->rbx); break;
    case 258: ret = lx_set_tid_addr((uint32_t*)regs->rbx); break;
    case 158: ret = lx_sched_yield(); break;
    case 172: ret = lx_prctl(regs->rbx,regs->rcx,regs->rdx,regs->rsi,regs->rdi); break;

    
    case 75: case 76: case 191: ret = lx_getrlimit(regs->rbx,(linux_rlimit_t*)regs->rcx); break;

    
    case 48:  ret = 0; break;
    case 67:  ret = 0; break;
    case 119: ret = lx_rt_sigreturn(); break;
    case 173: ret = lx_rt_sigreturn(); break;
    case 174: ret = lx_rt_sigaction((int32_t)regs->rbx,(void*)regs->rcx,(void*)regs->rdx,regs->rsi); break;
    case 175: ret = lx_rt_sigprocmask((int32_t)regs->rbx,(void*)regs->rcx,(void*)regs->rdx,regs->rsi); break;
    case 186: ret = lx_sigaltstack((void*)regs->rbx,(void*)regs->rcx); break;

    
    case 102: ret = lx_socketcall(regs->rbx,(uint32_t*)regs->rcx); break;

    
    case 37:  ret = 0; break; 
    case 57:  ret = 0; break; 
    case 65:  ret = (int32_t)lx_getpid(); break; 
    case 66:  ret = (int32_t)lx_getpid(); break; 
    case 126: ret = 0; break; 
    case 136: ret = 0; break; 
    case 150: case 151: case 152: case 153: ret = 0; break; 
    case 154: case 155: case 156: case 157: ret = 0; break; 
    case 159: ret = 99; break; 
    case 160: ret = 0;  break; 
    case 209: case 165: ret = 0; break; 
    case 170: case 171: ret = 0; break; 
    case 203: case 204: ret = 0; break; 
    case 213: ret = 0; break; 
    case 214: ret = 0; break; 
    case 218: ret = 0; break; 
    case 219: ret = 0; break; 
    case 225: ret = 0; break; 
    case 226: case 227: case 228: ret = 0; break; 
    case 270: case 271: case 272: ret = 0; break; 

    default:
        ret = -LINUX_ENOSYS;
        break;
    }

    regs->rax = (uint32_t)ret;
}

void linux_compat_init(void) {  }
