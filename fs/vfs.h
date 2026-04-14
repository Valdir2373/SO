/*
 * fs/vfs.h — Virtual File System
 * Abstração "tudo é um arquivo". Usa function pointers para polimorfismo.
 */
#ifndef _VFS_H
#define _VFS_H

#include <types.h>

/* Tipos de nó VFS */
#define VFS_FILE        0x01
#define VFS_DIRECTORY   0x02
#define VFS_CHARDEV     0x03
#define VFS_BLOCKDEV    0x04
#define VFS_PIPE        0x05
#define VFS_SYMLINK     0x06
#define VFS_MOUNTPOINT  0x08

/* Flags de abertura */
#define O_RDONLY   0x00
#define O_WRONLY   0x01
#define O_RDWR     0x02
#define O_CREAT    0x04
#define O_TRUNC    0x08
#define O_APPEND   0x10

/* Permissões Unix-like */
#define PERM_OWNER_R  0x100
#define PERM_OWNER_W  0x080
#define PERM_OWNER_X  0x040
#define PERM_GROUP_R  0x020
#define PERM_GROUP_W  0x010
#define PERM_GROUP_X  0x008
#define PERM_OTHER_R  0x004
#define PERM_OTHER_W  0x002
#define PERM_OTHER_X  0x001

struct vfs_node;

/* Entrada de diretório */
typedef struct {
    char     name[256];
    uint32_t inode;
    uint8_t  type;
} dirent_t;

/* Tipos de function pointers */
typedef uint32_t (*vfs_read_fn)(struct vfs_node *, uint32_t offset, uint32_t size, uint8_t *buf);
typedef uint32_t (*vfs_write_fn)(struct vfs_node *, uint32_t offset, uint32_t size, const uint8_t *buf);
typedef void     (*vfs_open_fn)(struct vfs_node *, uint32_t flags);
typedef void     (*vfs_close_fn)(struct vfs_node *);
typedef dirent_t *(*vfs_readdir_fn)(struct vfs_node *, uint32_t index);
typedef struct vfs_node *(*vfs_finddir_fn)(struct vfs_node *, const char *name);
typedef int      (*vfs_mkdir_fn)(struct vfs_node *, const char *name, uint32_t perms);
typedef int      (*vfs_create_fn)(struct vfs_node *, const char *name, uint32_t perms);
typedef int      (*vfs_unlink_fn)(struct vfs_node *, const char *name);

/* Nó VFS */
typedef struct vfs_node {
    char     name[256];
    uint32_t flags;        /* VFS_FILE, VFS_DIRECTORY, etc. */
    uint32_t permissions;
    uint32_t uid, gid;
    uint32_t size;
    uint32_t inode;
    uint32_t impl;         /* Dado privado do driver (ex: cluster FAT32) */

    vfs_read_fn    read;
    vfs_write_fn   write;
    vfs_open_fn    open;
    vfs_close_fn   close;
    vfs_readdir_fn readdir;
    vfs_finddir_fn finddir;
    vfs_mkdir_fn   mkdir;
    vfs_create_fn  create;
    vfs_unlink_fn  unlink;

    struct vfs_node *mount_point;  /* Se for mount point, aponta para raiz do FS */
    struct vfs_node *ptr;          /* Usado para symlinks e mount points */
} vfs_node_t;

/* File descriptor para processos */
typedef struct {
    vfs_node_t *node;
    uint32_t    offset;
    uint32_t    flags;
    bool        used;
} file_descriptor_t;

/* Raiz do VFS */
extern vfs_node_t *vfs_root;

/* Inicializa o VFS */
void vfs_init(void);

/* Monta um filesystem na raiz */
void vfs_mount_root(vfs_node_t *root);

/* Operações de alto nível (resolvem path e delegam para o driver) */
uint32_t    vfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf);
uint32_t    vfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buf);
void        vfs_open(vfs_node_t *node, uint32_t flags);
void        vfs_close(vfs_node_t *node);
dirent_t   *vfs_readdir(vfs_node_t *node, uint32_t index);
vfs_node_t *vfs_finddir(vfs_node_t *node, const char *name);

/* Resolve um path absoluto como "/boot/kernel.bin" */
vfs_node_t *vfs_resolve(const char *path);

#endif /* _VFS_H */
