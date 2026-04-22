
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <drivers/keyboard.h>
#include <drivers/vga.h>
#include <drivers/framebuffer.h>
#include <kernel/timer.h>
#include <lib/string.h>
#include <types.h>


static uint32_t null_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf)
{ (void)n;(void)off;(void)sz;(void)buf; return 0; }
static uint32_t null_write(vfs_node_t *n, uint32_t off, uint32_t sz, const uint8_t *buf)
{ (void)n;(void)off;(void)buf; return sz; }


static uint32_t zero_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf)
{ (void)n;(void)off; memset(buf,0,sz); return sz; }


static uint64_t g_rng = 0xDEADBEEFCAFEBABEULL;
static uint32_t urandom_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)n;(void)off;
    g_rng ^= timer_get_ticks();
    uint32_t i;
    for (i = 0; i < sz; i++) {
        g_rng ^= g_rng << 13;
        g_rng ^= g_rng >> 7;
        g_rng ^= g_rng << 17;
        buf[i] = (uint8_t)(g_rng & 0xFF);
    }
    return sz;
}


static uint32_t tty_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)n;(void)off;
    uint32_t i = 0;
    for (; i < sz; i++) {
        char c = keyboard_getchar();
        if (!c) break;
        buf[i] = (uint8_t)c;
        if (c == '\n') { i++; break; }
    }
    return i;
}
static uint32_t tty_write(vfs_node_t *n, uint32_t off, uint32_t sz, const uint8_t *buf) {
    (void)n;(void)off;
    uint32_t i;
    for (i = 0; i < sz; i++) vga_putchar((char)buf[i]);
    return sz;
}


static uint32_t fb0_read(vfs_node_t *n, uint32_t off, uint32_t sz, uint8_t *buf) {
    (void)n;
    extern framebuffer_t fb;
    if (!fb.backbuf) return 0;
    uint32_t total = fb.pitch * fb.height;
    if (off >= total) return 0;
    if (off + sz > total) sz = total - off;
    memcpy(buf, (uint8_t*)fb.backbuf + off, sz);
    return sz;
}
static uint32_t fb0_write(vfs_node_t *n, uint32_t off, uint32_t sz, const uint8_t *buf) {
    (void)n;
    extern framebuffer_t fb;
    if (!fb.backbuf) return 0;
    uint32_t total = fb.pitch * fb.height;
    if (off >= total) return 0;
    if (off + sz > total) sz = total - off;
    memcpy((uint8_t*)fb.backbuf + off, buf, sz);
    return sz;
}




#define NDEVS 8

static const char *dev_names[NDEVS] = {
    "null", "zero", "urandom", "random", "tty",
    "fb0", "stdin", "stdout"
};

static vfs_node_t  dev_nodes[NDEVS];
static dirent_t    dev_dirents[NDEVS];
static vfs_node_t  dev_dir;

static dirent_t *devdir_readdir(vfs_node_t *n, uint32_t idx) {
    (void)n;
    if (idx >= NDEVS) return 0;
    return &dev_dirents[idx];
}

static vfs_node_t *devdir_finddir(vfs_node_t *n, const char *name) {
    (void)n;
    uint32_t i;
    for (i = 0; i < NDEVS; i++)
        if (strcmp(name, dev_names[i]) == 0) return &dev_nodes[i];
    return 0;
}

void devfs_init(void) {
    uint32_t i;
    for (i = 0; i < NDEVS; i++) {
        memset(&dev_nodes[i], 0, sizeof(vfs_node_t));
        strncpy(dev_nodes[i].name, dev_names[i], 255);
        dev_nodes[i].flags       = VFS_CHARDEV;
        dev_nodes[i].permissions = 0666;
        dev_nodes[i].inode       = 100 + i;
        dev_nodes[i].read        = null_read;
        dev_nodes[i].write       = null_write;

        strncpy(dev_dirents[i].name, dev_names[i], 255);
        dev_dirents[i].inode = 100 + i;
        dev_dirents[i].type  = VFS_CHARDEV;
    }

    
    dev_nodes[0].read  = null_read;
    dev_nodes[0].write = null_write;
    
    dev_nodes[1].read  = zero_read;
    dev_nodes[1].write = null_write;
    
    dev_nodes[2].read  = urandom_read;
    dev_nodes[2].write = null_write;
    
    dev_nodes[3].read  = urandom_read;
    dev_nodes[3].write = null_write;
    
    dev_nodes[4].read  = tty_read;
    dev_nodes[4].write = tty_write;
    
    dev_nodes[5].read  = fb0_read;
    dev_nodes[5].write = fb0_write;
    dev_nodes[5].impl  = 0xFB0;     
    
    dev_nodes[6].read  = tty_read;
    dev_nodes[6].write = tty_write;
    dev_nodes[7].read  = null_read;
    dev_nodes[7].write = tty_write;

    
    memset(&dev_dir, 0, sizeof(vfs_node_t));
    strncpy(dev_dir.name, "dev", 255);
    dev_dir.flags       = VFS_DIRECTORY;
    dev_dir.permissions = 0755;
    dev_dir.inode       = 99;
    dev_dir.readdir     = devdir_readdir;
    dev_dir.finddir     = devdir_finddir;

    vfs_mount("/dev", &dev_dir);
}
