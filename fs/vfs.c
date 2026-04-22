
#include <fs/vfs.h>
#include <lib/string.h>
#include <mm/heap.h>
#include <types.h>

vfs_node_t *vfs_root = 0;


#define VFS_MAX_MOUNTS 16
static struct { char path[64]; uint32_t pathlen; vfs_node_t *root; }
    g_mounts[VFS_MAX_MOUNTS];
static uint32_t g_nmounts = 0;

void vfs_init(void) {
    vfs_root = 0;
    g_nmounts = 0;
}

void vfs_mount_root(vfs_node_t *root) {
    vfs_root = root;
}

void vfs_mount(const char *path, vfs_node_t *root) {
    if (g_nmounts >= VFS_MAX_MOUNTS || !path || !root) return;
    uint32_t i, len = 0;
    while (path[len]) len++;
    for (i = 0; i < len && i < 63; i++) g_mounts[g_nmounts].path[i] = path[i];
    g_mounts[g_nmounts].path[i] = '\0';
    g_mounts[g_nmounts].pathlen = len;
    g_mounts[g_nmounts].root   = root;
    g_nmounts++;
}

uint32_t vfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf) {
    if (!node) return 0;
    
    if (node->mount_point) node = node->mount_point;
    if (node->read) return node->read(node, offset, size, buf);
    return 0;
}

uint32_t vfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buf) {
    if (!node) return 0;
    if (node->mount_point) node = node->mount_point;
    if (node->write) return node->write(node, offset, size, buf);
    return 0;
}

void vfs_open(vfs_node_t *node, uint32_t flags) {
    if (!node) return;
    if (node->mount_point) node = node->mount_point;
    if (node->open) node->open(node, flags);
}

void vfs_close(vfs_node_t *node) {
    if (!node) return;
    if (node->mount_point) node = node->mount_point;
    if (node->close) node->close(node);
}

dirent_t *vfs_readdir(vfs_node_t *node, uint32_t index) {
    if (!node) return 0;
    if (node->mount_point) node = node->mount_point;
    if ((node->flags & 0x7) == VFS_DIRECTORY && node->readdir)
        return node->readdir(node, index);
    return 0;
}

vfs_node_t *vfs_finddir(vfs_node_t *node, const char *name) {
    if (!node) return 0;
    if (node->mount_point) node = node->mount_point;
    if ((node->flags & 0x7) == VFS_DIRECTORY && node->finddir)
        return node->finddir(node, name);
    return 0;
}

int vfs_create(vfs_node_t *dir, const char *name, uint32_t perms) {
    if (!dir || !name) return -1;
    if (dir->mount_point) dir = dir->mount_point;
    if (dir->create) return dir->create(dir, name, perms);
    return -1;
}

int vfs_mkdir(vfs_node_t *dir, const char *name, uint32_t perms) {
    if (!dir || !name) return -1;
    if (dir->mount_point) dir = dir->mount_point;
    if (dir->mkdir) return dir->mkdir(dir, name, perms);
    return -1;
}

int vfs_unlink(vfs_node_t *dir, const char *name) {
    if (!dir || !name) return -1;
    if (dir->mount_point) dir = dir->mount_point;
    if (dir->unlink) return dir->unlink(dir, name);
    return -1;
}

int vfs_rename(vfs_node_t *sdir, const char *sname, vfs_node_t *ddir, const char *dname) {
    (void)sdir; (void)sname; (void)ddir; (void)dname;
    return -1;
}

vfs_node_t *vfs_resolve_parent(const char *path, char *name_out) {
    if (!path || path[0] != '/') return 0;
    if (name_out) name_out[0] = 0;
    int last = 0, i;
    for (i = 0; path[i]; i++) if (path[i] == '/') last = i;
    if (last == 0) {
        if (name_out) { int j=0; while (path[1+j]) { name_out[j]=path[1+j]; j++; } name_out[j]=0; }
        return vfs_root;
    }
    char pp[256];
    for (i = 0; i < last && i < 255; i++) pp[i] = path[i];
    pp[i] = 0;
    if (name_out) { int j=0; while (path[last+1+j]) { name_out[j]=path[last+1+j]; j++; } name_out[j]=0; }
    return vfs_resolve(pp[0] ? pp : "/");
}


vfs_node_t *vfs_resolve(const char *path) {
    if (!path || path[0] != '/') return 0;

    
    uint32_t mi;
    for (mi = 0; mi < g_nmounts; mi++) {
        uint32_t plen = g_mounts[mi].pathlen;
        if (plen == 0) continue;
        
        if (strcmp(path, g_mounts[mi].path) == 0)
            return g_mounts[mi].root;
        
        if (strncmp(path, g_mounts[mi].path, plen) == 0 && path[plen] == '/') {
            
            vfs_node_t *mroot = g_mounts[mi].root;
            const char *rest = path + plen + 1;
            if (*rest == '\0') return mroot;
            char component[256];
            while (*rest) {
                int ci = 0;
                while (*rest && *rest != '/') { if (ci < 255) component[ci++] = *rest; rest++; }
                component[ci] = 0;
                if (*rest == '/') rest++;
                if (ci == 0) continue;
                mroot = vfs_finddir(mroot, component);
                if (!mroot) return 0;
            }
            return mroot;
        }
    }

    if (!vfs_root) return 0;
    vfs_node_t *node = vfs_root;

    
    const char *p = path + 1;   
    char component[256];

    while (*p) {
        
        int i = 0;
        while (*p && *p != '/') {
            if (i < 255) component[i++] = *p;
            p++;
        }
        component[i] = 0;
        if (*p == '/') p++;

        if (i == 0) continue;
        if (strcmp(component, ".") == 0) continue;
        if (strcmp(component, "..") == 0) {
            
            continue;
        }

        node = vfs_finddir(node, component);
        if (!node) return 0;
    }

    return node;
}
