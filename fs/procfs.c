
#include <fs/procfs.h>
#include <fs/vfs.h>
#include <mm/pmm.h>
#include <proc/process.h>
#include <kernel/timer.h>
#include <lib/string.h>
#include <types.h>


static int p_puts(char *b, int pos, const char *s) {
    while (*s) b[pos++] = *s++;
    return pos;
}
static int p_putu(char *b, int pos, uint64_t v) {
    char tmp[20]; int i = 0, n;
    if (v == 0) { b[pos++] = '0'; return pos; }
    while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    for (n = 0; n < i; n++) b[pos++] = tmp[i-1-n];
    return pos;
}
static int p_puth(char *b, int pos, uint64_t v, int w) {
    static const char h[] = "0123456789abcdef";
    char tmp[16]; int i = 0, n;
    while (v || i < w) { tmp[i++] = h[v&0xF]; v >>= 4; if (i >= 16) break; }
    for (n = 0; n < i; n++) b[pos++] = tmp[i-1-n];
    return pos;
}


static uint32_t noop_write(vfs_node_t *n, uint32_t off, uint32_t sz, const uint8_t *buf)
{ (void)n;(void)off;(void)buf; return sz; }


static uint32_t cpuinfo_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)n;
    static char content[2048];
    static uint32_t clen = 0;
    if (off == 0 || clen == 0) {
        int p = 0;
        p = p_puts(content, p, "processor\t: 0\n");
        p = p_puts(content, p, "vendor_id\t: GenuineIntel\n");
        p = p_puts(content, p, "cpu family\t: 6\n");
        p = p_puts(content, p, "model\t\t: 142\n");
        p = p_puts(content, p, "model name\t: Krypx Virtual CPU\n");
        p = p_puts(content, p, "stepping\t: 10\ncpu MHz\t\t: 2400.000\n");
        p = p_puts(content, p, "cache size\t: 4096 KB\nphysical id\t: 0\n");
        p = p_puts(content, p, "siblings\t: 1\ncore id\t\t: 0\ncpu cores\t: 1\n");
        p = p_puts(content, p, "flags\t\t: fpu vme de pse tsc msr pae mce cx8 apic sep "
            "mtrr pge mca cmov pat clflush mmx fxsr sse sse2 syscall nx lm\n");
        p = p_puts(content, p, "bogomips\t: 4800.00\n");
        content[p] = '\0';
        clen = (uint32_t)p;
    }
    if (off >= clen) return 0;
    uint32_t nb = clen - off; if (nb > sz) nb = sz;
    memcpy(buf, content + off, nb); return nb;
}


static uint32_t meminfo_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)n;
    char content[1024];
    uint64_t total_kb = pmm_get_total_pages() * 4;
    uint64_t free_kb  = pmm_get_free_pages()  * 4;
    uint64_t used_kb  = total_kb - free_kb;
    int p = 0;
    p = p_puts(content,p,"MemTotal:       "); p = p_putu(content,p,total_kb);
    p = p_puts(content,p," kB\nMemFree:        "); p = p_putu(content,p,free_kb);
    p = p_puts(content,p," kB\nMemAvailable:   "); p = p_putu(content,p,free_kb);
    p = p_puts(content,p," kB\nBuffers:               0 kB\nCached:                0 kB\n");
    p = p_puts(content,p,"SwapTotal:             0 kB\nSwapFree:              0 kB\n");
    p = p_puts(content,p,"AnonPages:      "); p = p_putu(content,p,used_kb);
    p = p_puts(content,p," kB\nSlab:                  0 kB\n");
    p = p_puts(content,p,"CommitLimit:    "); p = p_putu(content,p,total_kb);
    p = p_puts(content,p," kB\nCommitted_AS:   "); p = p_putu(content,p,used_kb);
    p = p_puts(content,p," kB\n");
    uint32_t clen = (uint32_t)p;
    if (off >= clen) return 0;
    uint32_t nb = clen - off; if (nb > sz) nb = sz;
    memcpy(buf, content + off, nb); return nb;
}


static uint32_t version_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)n;
    const char *v = "Linux version 5.15.0-krypx (gcc version 12.0.0) #1 SMP Krypx OS\n";
    uint32_t clen = (uint32_t)strlen(v);
    if (off >= clen) return 0;
    uint32_t nb = clen - off; if (nb > sz) nb = sz;
    memcpy(buf, v + off, nb); return nb;
}


static uint32_t stat_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)n;
    char content[512];
    uint64_t ticks = timer_get_ticks();
    int p = 0;
    p = p_puts(content,p,"cpu  "); p = p_putu(content,p,ticks);
    p = p_puts(content,p," 0 0 0 0 0 0 0 0 0\ncpu0 ");
    p = p_putu(content,p,ticks);
    p = p_puts(content,p," 0 0 0 0 0 0 0 0 0\n");
    p = p_puts(content,p,"intr 0\nctxt 0\nbtime 0\nprocesses 1\nprocs_running 1\n");
    uint32_t clen = (uint32_t)p;
    if (off >= clen) return 0;
    uint32_t nb = clen - off; if (nb > sz) nb = sz;
    memcpy(buf, content + off, nb); return nb;
}


static uint32_t mounts_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)n;
    const char *m =
        "rootfs / rootfs rw 0 0\n"
        "devtmpfs /dev devtmpfs rw 0 0\n"
        "proc /proc proc rw 0 0\n"
        "tmpfs /tmp tmpfs rw 0 0\n";
    uint32_t clen = (uint32_t)strlen(m);
    if (off >= clen) return 0;
    uint32_t nb = clen - off; if (nb > sz) nb = sz;
    memcpy(buf, m + off, nb); return nb;
}


static uint32_t overcommit_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)n;
    const char *v = "1\n";
    uint32_t clen = 2;
    if (off >= clen) return 0;
    uint32_t nb = clen - off; if (nb > sz) nb = sz;
    memcpy(buf, v + off, nb); return nb;
}
static uint32_t hostname_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)n;
    const char *h = "krypx\n";
    uint32_t clen = (uint32_t)strlen(h);
    if (off >= clen) return 0;
    uint32_t nb = clen - off; if (nb > sz) nb = sz;
    memcpy(buf, h + off, nb); return nb;
}


static uint32_t maps_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)n;
    static char content[4096];
    int p = 0;
    process_t *pr = process_current();
    p = p_puth(content,p, 0x400000ULL, 12);
    p = p_puts(content,p, "-");
    p = p_puth(content,p, 0x500000ULL, 12);
    p = p_puts(content,p, " r-xp 00000000 00:00 0          ");
    p = p_puts(content,p, pr ? pr->name : "[process]");
    content[p++] = '\n';
    if (pr && pr->heap_start) {
        uint64_t hs = pr->heap_start;
        uint64_t he = pr->heap_end > hs ? pr->heap_end : hs + 0x1000;
        p = p_puth(content,p,hs,12); p = p_puts(content,p,"-");
        p = p_puth(content,p,he,12); p = p_puts(content,p," rw-p 00000000 00:00 0          [heap]\n");
    }
    p = p_puth(content,p,0x7ffe00000000ULL,12); p = p_puts(content,p,"-");
    p = p_puth(content,p,0x7ffe00100000ULL,12);
    p = p_puts(content,p," rw-p 00000000 00:00 0          [stack]\n");
    p = p_puts(content,p,"7fff00000000-7fff00001000 r-xp 00000000 00:00 0          [vdso]\n");
    content[p] = '\0';
    uint32_t clen = (uint32_t)p;
    if (off >= clen) return 0;
    uint32_t nb = clen - off; if (nb > sz) nb = sz;
    memcpy(buf, content + off, nb); return nb;
}


static uint32_t pstatus_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)n;
    char content[1024];
    process_t *p = process_current();
    int pos = 0;
    pos = p_puts(content,pos,"Name:\t");
    pos = p_puts(content,pos, p ? p->name : "unknown");
    content[pos++] = '\n';
    pos = p_puts(content,pos,"State:\tR (running)\nPid:\t");
    pos = p_putu(content,pos, p ? p->pid : 0);
    pos = p_puts(content,pos,"\nPPid:\t0\nUid:\t0\t0\t0\t0\nGid:\t0\t0\t0\t0\n");
    pos = p_puts(content,pos,"VmPeak:\t   16384 kB\nVmSize:\t   16384 kB\n");
    pos = p_puts(content,pos,"VmRSS:\t    8192 kB\nVmData:\t    4096 kB\n");
    pos = p_puts(content,pos,"VmStk:\t    1024 kB\nVmExe:\t    1024 kB\nThreads:\t1\n");
    uint32_t clen = (uint32_t)pos;
    if (off >= clen) return 0;
    uint32_t nb = clen - off; if (nb > sz) nb = sz;
    memcpy(buf, content + off, nb); return nb;
}


static uint32_t exe_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)n;
    process_t *p = process_current();
    char path[256];
    int pos = 0;
    path[pos++] = '/';
    if (p) { const char *nm = p->name; while (*nm) path[pos++] = *nm++; }
    path[pos++] = '\n'; path[pos] = '\0';
    uint32_t clen = (uint32_t)pos;
    if (off >= clen) return 0;
    uint32_t nb = clen - off; if (nb > sz) nb = sz;
    memcpy(buf, path + off, nb); return nb;
}


static uint32_t cmdline_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)n;
    process_t *p = process_current();
    const char *name = p ? p->name : "krypx";
    uint32_t clen = (uint32_t)(strlen(name) + 1);
    if (off >= clen) return 0;
    uint32_t nb = clen - off; if (nb > sz) nb = sz;
    memcpy(buf, name + off, nb); return nb;
}


static dirent_t  selffd_dirent_tmp;

static dirent_t *selffd_readdir(vfs_node_t *n, uint32_t idx) {
    (void)n;
    process_t *p = process_current();
    if (!p) return 0;
    uint32_t count = 0, i;
    for (i = 0; i < MAX_FDS; i++) {
        if (p->fds[i]) {
            if (count == idx) {
                
                char tmp[8]; int tp=0;
                uint32_t v = i;
                if (v == 0) { tmp[tp++]='0'; }
                else {
                    char tt[8]; int ti=0, tn;
                    while(v){tt[ti++]=(char)('0'+v%10);v/=10;}
                    for(tn=0;tn<ti;tn++) tmp[tp++]=tt[ti-1-tn];
                }
                tmp[tp]='\0';
                strncpy(selffd_dirent_tmp.name, tmp, 255);
                selffd_dirent_tmp.inode = 510 + i;
                selffd_dirent_tmp.type  = VFS_CHARDEV;
                return &selffd_dirent_tmp;
            }
            count++;
        }
    }
    return 0;
}

static vfs_node_t selffd_node_tmp;
static vfs_node_t *selffd_finddir(vfs_node_t *n, const char *name) {
    (void)n;
    uint32_t fd = 0;
    const char *c = name;
    while (*c >= '0' && *c <= '9') { fd = fd*10 + (uint32_t)(*c - '0'); c++; }
    if (*c != '\0' || fd >= MAX_FDS) return 0;
    process_t *p = process_current();
    if (!p || !p->fds[fd]) return 0;
    memcpy(&selffd_node_tmp, p->fds[fd], sizeof(vfs_node_t));
    return &selffd_node_tmp;
}


#define NSELF 5
static const char *self_entry_names[NSELF] = { "maps","status","exe","fd","cmdline" };
static vfs_node_t  self_nodes[NSELF];
static dirent_t    self_dirents[NSELF];
static vfs_node_t  self_dir;
static vfs_node_t  selffd_dir_node;

static dirent_t *selfdir_readdir(vfs_node_t *n, uint32_t idx) {
    (void)n;
    if (idx >= NSELF) return 0;
    return &self_dirents[idx];
}
static vfs_node_t *selfdir_finddir(vfs_node_t *n, const char *name) {
    (void)n;
    uint32_t i;
    for (i = 0; i < NSELF; i++)
        if (strcmp(name, self_entry_names[i]) == 0) return &self_nodes[i];
    return 0;
}


static vfs_node_t syskernel_nodes[2];
static vfs_node_t syskernel_dir;
static vfs_node_t sys_dir;

static dirent_t *syskerneldir_readdir(vfs_node_t *n, uint32_t idx) {
    (void)n;
    static dirent_t d;
    if (idx == 0) { strncpy(d.name,"overcommit_memory",255); d.inode=900; d.type=VFS_FILE; return &d; }
    if (idx == 1) { strncpy(d.name,"hostname",255);          d.inode=901; d.type=VFS_FILE; return &d; }
    return 0;
}
static vfs_node_t *syskerneldir_finddir(vfs_node_t *n, const char *name) {
    (void)n;
    if (strcmp(name,"overcommit_memory")==0) return &syskernel_nodes[0];
    if (strcmp(name,"hostname")==0)          return &syskernel_nodes[1];
    return 0;
}
static dirent_t *sysdir_readdir(vfs_node_t *n, uint32_t idx) {
    (void)n;
    static dirent_t d;
    if (idx == 0) { strncpy(d.name,"kernel",255); d.inode=800; d.type=VFS_DIRECTORY; return &d; }
    return 0;
}
static vfs_node_t *sysdir_finddir(vfs_node_t *n, const char *name) {
    (void)n;
    if (strcmp(name,"kernel")==0) return &syskernel_dir;
    return 0;
}


#define NPROC 7
static const char *proc_entry_names[NPROC] = {
    "cpuinfo","meminfo","version","stat","mounts","self","sys"
};
static vfs_node_t proc_nodes[NPROC];
static dirent_t   proc_dirents[NPROC];
static vfs_node_t proc_dir;

static dirent_t *procdir_readdir(vfs_node_t *n, uint32_t idx) {
    (void)n;
    if (idx >= NPROC) return 0;
    return &proc_dirents[idx];
}
static vfs_node_t *procdir_finddir(vfs_node_t *n, const char *name) {
    (void)n;
    uint32_t i;
    for (i = 0; i < NPROC; i++)
        if (strcmp(name, proc_entry_names[i]) == 0) return &proc_nodes[i];
    return 0;
}


void procfs_init(void) {
    uint32_t i;

    
    memset(&selffd_dir_node, 0, sizeof(vfs_node_t));
    strncpy(selffd_dir_node.name, "fd", 255);
    selffd_dir_node.flags   = VFS_DIRECTORY; selffd_dir_node.inode = 501;
    selffd_dir_node.readdir = selffd_readdir; selffd_dir_node.finddir = selffd_finddir;

    
    for (i = 0; i < NSELF; i++) {
        memset(&self_nodes[i], 0, sizeof(vfs_node_t));
        strncpy(self_nodes[i].name, self_entry_names[i], 255);
        self_nodes[i].flags = VFS_FILE; self_nodes[i].inode = 500+i; self_nodes[i].size = 4096;
        memset(&self_dirents[i], 0, sizeof(dirent_t));
        strncpy(self_dirents[i].name, self_entry_names[i], 255);
        self_dirents[i].inode = 500+i; self_dirents[i].type = VFS_FILE;
    }
    self_nodes[0].read = maps_read;
    self_nodes[1].read = pstatus_read;
    self_nodes[2].read = exe_read;
    
    memcpy(&self_nodes[3], &selffd_dir_node, sizeof(vfs_node_t));
    self_dirents[3].type = VFS_DIRECTORY;
    self_nodes[4].read = cmdline_read;

    
    memset(&self_dir, 0, sizeof(vfs_node_t));
    strncpy(self_dir.name, "self", 255);
    self_dir.flags   = VFS_DIRECTORY; self_dir.inode = 499;
    self_dir.readdir = selfdir_readdir; self_dir.finddir = selfdir_finddir;

    
    memset(syskernel_nodes, 0, sizeof(syskernel_nodes));
    strncpy(syskernel_nodes[0].name, "overcommit_memory", 255);
    syskernel_nodes[0].flags = VFS_FILE; syskernel_nodes[0].inode = 900;
    syskernel_nodes[0].read  = overcommit_read;
    syskernel_nodes[0].write = noop_write;
    strncpy(syskernel_nodes[1].name, "hostname", 255);
    syskernel_nodes[1].flags = VFS_FILE; syskernel_nodes[1].inode = 901;
    syskernel_nodes[1].read  = hostname_read;
    syskernel_nodes[1].write = noop_write;

    memset(&syskernel_dir, 0, sizeof(vfs_node_t));
    strncpy(syskernel_dir.name, "kernel", 255);
    syskernel_dir.flags   = VFS_DIRECTORY; syskernel_dir.inode = 800;
    syskernel_dir.readdir = syskerneldir_readdir;
    syskernel_dir.finddir = syskerneldir_finddir;

    memset(&sys_dir, 0, sizeof(vfs_node_t));
    strncpy(sys_dir.name, "sys", 255);
    sys_dir.flags   = VFS_DIRECTORY; sys_dir.inode = 799;
    sys_dir.readdir = sysdir_readdir;
    sys_dir.finddir = sysdir_finddir;

    
    for (i = 0; i < NPROC; i++) {
        memset(&proc_nodes[i], 0, sizeof(vfs_node_t));
        strncpy(proc_nodes[i].name, proc_entry_names[i], 255);
        proc_nodes[i].flags = VFS_FILE; proc_nodes[i].inode = 200+i; proc_nodes[i].size = 4096;
        memset(&proc_dirents[i], 0, sizeof(dirent_t));
        strncpy(proc_dirents[i].name, proc_entry_names[i], 255);
        proc_dirents[i].inode = 200+i; proc_dirents[i].type = VFS_FILE;
    }
    proc_nodes[0].read = cpuinfo_read;
    proc_nodes[1].read = meminfo_read;
    proc_nodes[2].read = version_read;
    proc_nodes[3].read = stat_read;
    proc_nodes[4].read = mounts_read;
    memcpy(&proc_nodes[5], &self_dir, sizeof(vfs_node_t));
    proc_dirents[5].type = VFS_DIRECTORY;
    memcpy(&proc_nodes[6], &sys_dir, sizeof(vfs_node_t));
    proc_dirents[6].type = VFS_DIRECTORY;

    memset(&proc_dir, 0, sizeof(vfs_node_t));
    strncpy(proc_dir.name, "proc", 255);
    proc_dir.flags   = VFS_DIRECTORY; proc_dir.inode = 199;
    proc_dir.readdir = procdir_readdir;
    proc_dir.finddir = procdir_finddir;

    vfs_mount("/proc", &proc_dir);
}
