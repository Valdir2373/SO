/*
 * fs/vfs.c — Virtual File System
 * Dispatch de operações de arquivo para os drivers registrados.
 */

#include <fs/vfs.h>
#include <lib/string.h>
#include <mm/heap.h>
#include <types.h>

vfs_node_t *vfs_root = 0;

void vfs_init(void) {
    vfs_root = 0;
}

void vfs_mount_root(vfs_node_t *root) {
    vfs_root = root;
}

uint32_t vfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf) {
    if (!node) return 0;
    /* Redireciona para mount point se houver */
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

/* Resolve path como "/dir/subdir/file" */
vfs_node_t *vfs_resolve(const char *path) {
    if (!path || path[0] != '/') return 0;
    if (!vfs_root) return 0;

    vfs_node_t *node = vfs_root;

    /* Percorre cada componente do path */
    const char *p = path + 1;   /* Pula a / inicial */
    char component[256];

    while (*p) {
        /* Extrai próximo componente */
        int i = 0;
        while (*p && *p != '/') {
            if (i < 255) component[i++] = *p;
            p++;
        }
        component[i] = 0;
        if (*p == '/') p++;

        if (i == 0) continue;    /* // → ignora */
        if (strcmp(component, ".") == 0) continue;
        if (strcmp(component, "..") == 0) {
            /* Sobe um nível — não implementado completamente ainda */
            continue;
        }

        node = vfs_finddir(node, component);
        if (!node) return 0;
    }

    return node;
}
