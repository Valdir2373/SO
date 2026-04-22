
#include "kpkg.h"
#include <fs/vfs.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <net/socket.h>
#include <net/dns.h>
#include <types.h>



typedef struct {
    char     magic[4];    
    uint32_t version;
    uint32_t n_files;
} __attribute__((packed)) kpkg_hdr_t;

typedef struct {
    char     path[KPKG_PATH_MAX];
    uint32_t mode;
    uint32_t size;
} __attribute__((packed)) kpkg_entry_t;



static void kpkg_puts(kpkg_print_fn fn, const char *s) { if (fn) fn(s); }

static void kpkg_putu(kpkg_print_fn fn, uint32_t v) {
    char buf[12]; int i = 10; buf[11] = 0;
    if (v == 0) { kpkg_puts(fn, "0"); return; }
    while (v && i >= 0) { buf[i--] = '0' + (v % 10); v /= 10; }
    kpkg_puts(fn, buf + i + 1);
}


static void mkdirs(const char *path) {
    char buf[256];
    strncpy(buf, path, 255);
    int len = (int)strlen(buf);
    int i;
    for (i = 1; i < len; i++) {
        if (buf[i] == '/') {
            buf[i] = 0;
            vfs_node_t *d = vfs_resolve(buf);
            if (!d) {
                char name[256];
                vfs_node_t *par = vfs_resolve_parent(buf, name);
                if (par && name[0]) vfs_mkdir(par, name, 0755);
            }
            buf[i] = '/';
        }
    }
}


static int write_file(const char *path, const uint8_t *data, uint32_t size, uint32_t mode) {
    
    mkdirs(path);

    char name[256];
    vfs_node_t *dir = vfs_resolve_parent(path, name);
    if (!dir || !name[0]) return -1;

    
    vfs_node_t *node = vfs_resolve(path);
    if (!node) {
        if (vfs_create(dir, name, (uint16_t)mode) != 0) return -1;
        node = vfs_resolve(path);
    }
    if (!node) return -1;

    if (size > 0) {
        vfs_write(node, 0, size, data);
        node->size = size;
    }
    return 0;
}



int kpkg_install(const char *pkgpath, kpkg_print_fn print) {
    vfs_node_t *node = vfs_resolve(pkgpath);
    if (!node || node->size < sizeof(kpkg_hdr_t)) {
        kpkg_puts(print, "kpkg: arquivo nao encontrado: ");
        kpkg_puts(print, pkgpath);
        kpkg_puts(print, "\n");
        return -1;
    }

    
    uint32_t pkgsz = node->size;
    uint8_t *buf = (uint8_t *)kmalloc(pkgsz);
    if (!buf) { kpkg_puts(print, "kpkg: sem memoria\n"); return -1; }
    vfs_read(node, 0, pkgsz, buf);

    
    kpkg_hdr_t *hdr = (kpkg_hdr_t *)buf;
    if (hdr->magic[0] != 'K' || hdr->magic[1] != 'P' ||
        hdr->magic[2] != 'K' || hdr->magic[3] != 'G') {
        kpkg_puts(print, "kpkg: formato invalido\n");
        kfree(buf);
        return -1;
    }
    if (hdr->version != KPKG_VERSION) {
        kpkg_puts(print, "kpkg: versao incompativel\n");
        kfree(buf);
        return -1;
    }

    kpkg_puts(print, "kpkg: instalando ");
    kpkg_putu(print, hdr->n_files);
    kpkg_puts(print, " arquivos...\n");

    
    uint32_t off = sizeof(kpkg_hdr_t);
    uint32_t n;
    for (n = 0; n < hdr->n_files; n++) {
        if (off + sizeof(kpkg_entry_t) > pkgsz) break;
        kpkg_entry_t *ent = (kpkg_entry_t *)(buf + off);
        off += sizeof(kpkg_entry_t);

        if (off + ent->size > pkgsz) break;
        const uint8_t *data = buf + off;
        off += ent->size;

        
        if (ent->path[strlen(ent->path) - 1] == '/') {
            
            mkdirs(ent->path);
        } else {
            if (write_file(ent->path, data, ent->size, ent->mode) != 0) {
                kpkg_puts(print, "kpkg: falha ao escrever: ");
                kpkg_puts(print, ent->path);
                kpkg_puts(print, "\n");
            }
        }
    }

    kfree(buf);

    
    {
        
        const char *base = pkgpath;
        const char *t = pkgpath;
        while (*t) { if (*t == '/') base = t + 1; t++; }

        char dbpath[256];
        strcpy(dbpath, KPKG_DB_DIR "/");
        strncat(dbpath, base, 200);
        
        int dl = strlen(dbpath);
        if (dl > 5 && strcmp(dbpath + dl - 5, ".kpkg") == 0)
            dbpath[dl - 5] = 0;

        mkdirs(KPKG_DB_DIR "/");
        char dbname[256];
        vfs_node_t *dbdir = vfs_resolve_parent(dbpath, dbname);
        if (dbdir && dbname[0]) {
            vfs_node_t *dbnode = vfs_resolve(dbpath);
            if (!dbnode) vfs_create(dbdir, dbname, 0644);
        }
    }

    kpkg_puts(print, "kpkg: instalacao concluida!\n");
    return 0;
}



void kpkg_list(kpkg_print_fn print) {
    vfs_node_t *db = vfs_resolve(KPKG_DB_DIR);
    if (!db) { kpkg_puts(print, "kpkg: nenhum pacote instalado\n"); return; }

    kpkg_puts(print, "Pacotes instalados:\n");
    uint32_t i = 0;
    dirent_t *e;
    while ((e = vfs_readdir(db, i++)) != 0) {
        kpkg_puts(print, "  ");
        kpkg_puts(print, e->name);
        kpkg_puts(print, "\n");
        kfree(e);
    }
}



void kpkg_search(const char *name, kpkg_print_fn print) {
    vfs_node_t *pkgdir = vfs_resolve(KPKG_PKG_DIR);
    if (!pkgdir) { kpkg_puts(print, "kpkg: /packages/ nao encontrado\n"); return; }

    kpkg_puts(print, "Pacotes disponiveis:\n");
    uint32_t i = 0;
    dirent_t *e;
    while ((e = vfs_readdir(pkgdir, i++)) != 0) {
        if (!name || !name[0] || strstr(e->name, name)) {
            
            char dname[256];
            strncpy(dname, e->name, 255);
            int dl = strlen(dname);
            if (dl > 5 && strcmp(dname + dl - 5, ".kpkg") == 0)
                dname[dl - 5] = 0;
            kpkg_puts(print, "  ");
            kpkg_puts(print, dname);
            kpkg_puts(print, "\n");
        }
        kfree(e);
    }
}


int kpkg_create(const char *srcdir, const char *outpath, kpkg_print_fn print) {
    (void)srcdir; (void)outpath;
    kpkg_puts(print, "kpkg create: use scripts/mkpkg.sh no host\n");
    return -1;
}
