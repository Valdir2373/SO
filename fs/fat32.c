

#include <fs/fat32.h>
#include <fs/vfs.h>
#include <drivers/ide.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <types.h>


static struct {
    fat32_bpb_t bpb;
    uint32_t    lba_start;         
    uint32_t    fat_lba;           
    uint32_t    data_lba;          
    uint32_t    root_cluster;      
    uint32_t    sectors_per_clus;
    uint32_t    bytes_per_clus;
    bool        mounted;
} fat32;

static vfs_node_t fat32_root_node;




typedef struct {
    uint8_t  seq;         
    uint16_t name1[5];    
    uint8_t  attr;        
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];    
    uint16_t cluster;     
    uint16_t name3[2];    
} __attribute__((packed)) fat32_lfn_t;


static int fat32_stricmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (int)(uint8_t)ca - (int)(uint8_t)cb;
        a++; b++;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}


static void fat32_extract_lfn(fat32_lfn_t *lfn, char *lfn_buf) {
    uint8_t seq = lfn->seq & 0x1F;
    if (seq == 0 || seq > 20) return;

    int base = (seq - 1) * 13;
    uint16_t chars[13];
    int j;

    for (j = 0; j < 5;  j++) chars[j]    = lfn->name1[j];
    for (j = 0; j < 6;  j++) chars[5+j]  = lfn->name2[j];
    for (j = 0; j < 2;  j++) chars[11+j] = lfn->name3[j];

    for (j = 0; j < 13; j++) {
        uint16_t c = chars[j];
        if (c == 0x0000 || c == 0xFFFF) {
            if (base + j < 255) lfn_buf[base + j] = 0;
            return;
        }
        if (base + j < 255) lfn_buf[base + j] = (char)(c & 0x7F);
    }
}


static uint32_t fat32_next_cluster(uint32_t cluster) {
    uint32_t fat_offset  = cluster * 4;
    uint32_t fat_sector  = fat32.fat_lba + (fat_offset / 512);
    uint32_t fat_index   = (fat_offset % 512) / 4;
    uint8_t  sector[512];

    if (!ide_read_sectors(fat_sector, 1, sector)) return 0x0FFFFFFF;

    uint32_t *fat = (uint32_t *)sector;
    return fat[fat_index] & 0x0FFFFFFF;
}


static bool fat32_read_cluster(uint32_t cluster, void *buffer) {
    uint32_t lba = fat32.data_lba + (cluster - 2) * fat32.sectors_per_clus;
    return ide_read_sectors(lba, (uint8_t)fat32.sectors_per_clus, buffer);
}


static void fat32_83_to_name(const uint8_t *fat_name, const uint8_t *fat_ext,
                              char *out) {
    int i = 0, j = 0;

    
    while (i < 8 && fat_name[i] != ' ') {
        out[j++] = (char)fat_name[i++];
    }

    
    if (fat_ext[0] != ' ') {
        out[j++] = '.';
        int k = 0;
        while (k < 3 && fat_ext[k] != ' ') {
            out[j++] = (char)fat_ext[k++];
        }
    }
    out[j] = 0;
}




static void fat32_name_to_83(const char *name, uint8_t *fname, uint8_t *fext) {
    memset(fname, ' ', 8);
    memset(fext,  ' ', 3);
    int len = 0;
    const char *dot = 0;
    while (name[len]) { if (name[len] == '.') dot = name + len; len++; }
    int base_end = dot ? (int)(dot - name) : len;
    if (base_end > 8) base_end = 8;
    int i;
    for (i = 0; i < base_end; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        fname[i] = (uint8_t)c;
    }
    if (dot) {
        int el = len - (int)(dot - name) - 1;
        if (el > 3) el = 3;
        for (i = 0; i < el; i++) {
            char c = dot[1 + i];
            if (c >= 'a' && c <= 'z') c -= 32;
            fext[i] = (uint8_t)c;
        }
    }
}

static bool fat32_write_fat(uint32_t cluster, uint32_t value) {
    uint32_t fat_off = cluster * 4;
    uint32_t fat_sec = fat32.fat_lba + fat_off / 512;
    uint32_t fat_idx = (fat_off % 512) / 4;
    uint8_t  sec[512];
    if (!ide_read_sectors(fat_sec, 1, sec)) return false;
    uint32_t *fp = (uint32_t *)sec;
    fp[fat_idx] = (fp[fat_idx] & 0xF0000000) | (value & 0x0FFFFFFF);
    if (!ide_write_sectors(fat_sec, 1, sec)) return false;
    if (fat32.bpb.num_fats > 1) {
        uint32_t f2 = fat32.fat_lba + fat32.bpb.fat_size_32 + fat_off / 512;
        if (!ide_read_sectors(f2, 1, sec)) return true;
        fp = (uint32_t *)sec;
        fp[fat_idx] = (fp[fat_idx] & 0xF0000000) | (value & 0x0FFFFFFF);
        ide_write_sectors(f2, 1, sec);
    }
    return true;
}

static uint32_t fat32_alloc_cluster(void) {
    uint32_t total = fat32.bpb.total_sectors_32 / fat32.sectors_per_clus + 2;
    uint32_t c;
    for (c = 2; c < total; c++) {
        if ((fat32_next_cluster(c) & 0x0FFFFFFF) == 0) {
            if (!fat32_write_fat(c, FAT32_EOC)) return 0;
            uint8_t *z = (uint8_t *)kmalloc(fat32.bytes_per_clus);
            if (z) {
                memset(z, 0, fat32.bytes_per_clus);
                uint32_t lba = fat32.data_lba + (c - 2) * fat32.sectors_per_clus;
                ide_write_sectors(lba, (uint8_t)fat32.sectors_per_clus, z);
                kfree(z);
            }
            return c;
        }
    }
    return 0;
}

static bool fat32_write_cluster(uint32_t cluster, const void *buf) {
    uint32_t lba = fat32.data_lba + (cluster - 2) * fat32.sectors_per_clus;
    return ide_write_sectors(lba, (uint8_t)fat32.sectors_per_clus, buf);
}

static void fat32_free_chain(uint32_t cluster) {
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t nxt = fat32_next_cluster(cluster);
        fat32_write_fat(cluster, FAT32_FREE);
        cluster = nxt;
    }
}

static bool fat32_update_dirent_size(uint32_t parent_clus, uint32_t file_clus,
                                      uint32_t new_size) {
    uint32_t cluster = parent_clus;
    uint8_t *cb = (uint8_t *)kmalloc(fat32.bytes_per_clus);
    if (!cb) return false;
    bool ok = false;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        if (!fat32_read_cluster(cluster, cb)) break;
        fat32_dirent_t *es = (fat32_dirent_t *)cb;
        uint32_t n = fat32.bytes_per_clus / sizeof(fat32_dirent_t);
        uint32_t i;
        for (i = 0; i < n; i++) {
            if (es[i].name[0] == 0x00) goto upd_done;
            if (es[i].name[0] == 0xE5) continue;
            if (es[i].attr == FAT_ATTR_LFN) continue;
            if (es[i].attr & FAT_ATTR_VOLUME_ID) continue;
            uint32_t ec = ((uint32_t)es[i].cluster_hi << 16) | es[i].cluster_lo;
            if (ec == file_clus) {
                es[i].file_size = new_size;
                fat32_write_cluster(cluster, cb);
                ok = true;
                goto upd_done;
            }
        }
        cluster = fat32_next_cluster(cluster);
    }
upd_done:
    kfree(cb);
    return ok;
}

static uint32_t  fat32_vfs_read   (vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf);
static dirent_t *fat32_vfs_readdir(vfs_node_t *node, uint32_t index);
static vfs_node_t *fat32_vfs_finddir(vfs_node_t *node, const char *name);
static uint32_t fat32_vfs_write(vfs_node_t *node, uint32_t offset,
                                 uint32_t size, const uint8_t *buf);
static int fat32_vfs_create(vfs_node_t *dir, const char *name, uint32_t perms);
static int fat32_vfs_mkdir(vfs_node_t *dir, const char *name, uint32_t perms);
static int fat32_vfs_unlink(vfs_node_t *dir, const char *name);

static void fat32_set_node_ops(vfs_node_t *node) {
    node->read    = fat32_vfs_read;
    node->write   = fat32_vfs_write;
    node->readdir = fat32_vfs_readdir;
    node->finddir = fat32_vfs_finddir;
    node->create  = fat32_vfs_create;
    node->mkdir   = fat32_vfs_mkdir;
    node->unlink  = fat32_vfs_unlink;
}

static uint32_t fat32_vfs_write(vfs_node_t *node, uint32_t offset,
                                 uint32_t size, const uint8_t *buf) {
    if (!fat32.mounted || !node || !buf || size == 0) return 0;
    uint32_t cluster = node->impl;
    uint32_t bc = fat32.bytes_per_clus;
    if (cluster == 0) {
        cluster = fat32_alloc_cluster();
        if (!cluster) return 0;
        node->impl = cluster;
        fat32_update_dirent_size(node->inode, cluster, 0);
    }
    uint8_t *cb = (uint8_t *)kmalloc(bc);
    if (!cb) return 0;
    uint32_t written = 0;
    uint32_t cur = cluster;
    uint32_t ci = offset / bc;
    uint32_t bo = offset % bc;
    uint32_t i;
    for (i = 0; i < ci; i++) {
        uint32_t nxt = fat32_next_cluster(cur);
        if (nxt >= 0x0FFFFFF8) {
            uint32_t nc = fat32_alloc_cluster();
            if (!nc) { kfree(cb); return written; }
            fat32_write_fat(cur, nc);
            cur = nc;
        } else { cur = nxt; }
    }
    while (written < size && cur >= 2 && cur < 0x0FFFFFF8) {
        fat32_read_cluster(cur, cb);
        uint32_t sp = bc - bo;
        uint32_t tw = size - written;
        if (tw > sp) tw = sp;
        memcpy(cb + bo, buf + written, tw);
        fat32_write_cluster(cur, cb);
        written += tw;
        bo = 0;
        if (written < size) {
            uint32_t nxt = fat32_next_cluster(cur);
            if (nxt >= 0x0FFFFFF8) {
                uint32_t nc = fat32_alloc_cluster();
                if (!nc) break;
                fat32_write_fat(cur, nc);
                cur = nc;
            } else { cur = nxt; }
        }
    }
    kfree(cb);
    uint32_t new_end = offset + written;
    if (new_end > node->size) node->size = new_end;
    if (node->inode && node->impl)
        fat32_update_dirent_size(node->inode, node->impl, node->size);
    return written;
}

static int fat32_write_dirent(uint32_t dir_clus, const char *name,
                               uint8_t attr, uint32_t first_clus) {
    uint8_t fname[8], fext[3];
    fat32_name_to_83(name, fname, fext);
    uint8_t *cb = (uint8_t *)kmalloc(fat32.bytes_per_clus);
    if (!cb) return -1;
    bool done = false;
    uint32_t cluster = dir_clus;
    uint32_t prev = 0;
    while (cluster >= 2 && cluster < 0x0FFFFFF8 && !done) {
        if (!fat32_read_cluster(cluster, cb)) break;
        fat32_dirent_t *es = (fat32_dirent_t *)cb;
        uint32_t n = fat32.bytes_per_clus / sizeof(fat32_dirent_t);
        uint32_t i;
        for (i = 0; i < n; i++) {
            if (es[i].name[0] == 0x00 || es[i].name[0] == 0xE5) {
                memcpy(es[i].name, fname, 8);
                memcpy(es[i].ext,  fext,  3);
                es[i].attr        = attr;
                es[i].reserved    = 0;
                es[i].create_time_ms = 0;
                es[i].create_time = 0;
                es[i].create_date = 0x5521;
                es[i].access_date = 0x5521;
                es[i].cluster_hi  = (uint16_t)(first_clus >> 16);
                es[i].write_time  = 0;
                es[i].write_date  = 0x5521;
                es[i].cluster_lo  = (uint16_t)(first_clus & 0xFFFF);
                es[i].file_size   = 0;
                fat32_write_cluster(cluster, cb);
                done = true;
                break;
            }
        }
        if (!done) {
            prev = cluster;
            cluster = fat32_next_cluster(cluster);
            if (cluster >= 0x0FFFFFF8 && prev) {
                uint32_t nc = fat32_alloc_cluster();
                if (!nc) break;
                fat32_write_fat(prev, nc);
                cluster = nc;
            }
        }
    }
    kfree(cb);
    return done ? 0 : -1;
}

static int fat32_vfs_create(vfs_node_t *dir, const char *name, uint32_t perms) {
    (void)perms;
    if (!fat32.mounted || !dir) return -1;
    uint32_t fc = fat32_alloc_cluster();
    if (!fc) return -1;
    if (fat32_write_dirent(dir->impl, name, FAT_ATTR_ARCHIVE, fc) != 0) {
        fat32_write_fat(fc, FAT32_FREE);
        return -1;
    }
    return 0;
}

static int fat32_vfs_mkdir(vfs_node_t *dir, const char *name, uint32_t perms) {
    (void)perms;
    if (!fat32.mounted || !dir) return -1;
    uint32_t nc = fat32_alloc_cluster();
    if (!nc) return -1;
    uint8_t *cb = (uint8_t *)kmalloc(fat32.bytes_per_clus);
    if (!cb) { fat32_write_fat(nc, FAT32_FREE); return -1; }
    memset(cb, 0, fat32.bytes_per_clus);
    fat32_dirent_t *es = (fat32_dirent_t *)cb;
    memset(es[0].name, ' ', 8); es[0].name[0] = '.';
    memset(es[0].ext,  ' ', 3);
    es[0].attr = FAT_ATTR_DIRECTORY;
    es[0].cluster_hi = (uint16_t)(nc >> 16);
    es[0].cluster_lo = (uint16_t)(nc & 0xFFFF);
    memset(es[1].name, ' ', 8); es[1].name[0] = '.'; es[1].name[1] = '.';
    memset(es[1].ext,  ' ', 3);
    es[1].attr = FAT_ATTR_DIRECTORY;
    uint32_t pc = dir->impl;
    if (pc == fat32.root_cluster) pc = 0;
    es[1].cluster_hi = (uint16_t)(pc >> 16);
    es[1].cluster_lo = (uint16_t)(pc & 0xFFFF);
    fat32_write_cluster(nc, cb);
    kfree(cb);
    if (fat32_write_dirent(dir->impl, name, FAT_ATTR_DIRECTORY, nc) != 0) {
        fat32_free_chain(nc);
        return -1;
    }
    return 0;
}

static int fat32_vfs_unlink(vfs_node_t *dir, const char *name) {
    if (!fat32.mounted || !dir) return -1;
    uint32_t cluster = dir->impl;
    uint8_t *cb = (uint8_t *)kmalloc(fat32.bytes_per_clus);
    if (!cb) return -1;
    char lfn[256];
    memset(lfn, 0, sizeof(lfn));
    bool del = false;
    while (cluster >= 2 && cluster < 0x0FFFFFF8 && !del) {
        if (!fat32_read_cluster(cluster, cb)) break;
        fat32_dirent_t *es = (fat32_dirent_t *)cb;
        uint32_t n = fat32.bytes_per_clus / sizeof(fat32_dirent_t);
        uint32_t i;
        for (i = 0; i < n; i++) {
            if (es[i].name[0] == 0x00) goto unl_done;
            if (es[i].name[0] == 0xE5) { memset(lfn, 0, sizeof(lfn)); continue; }
            if (es[i].attr == FAT_ATTR_LFN) { fat32_extract_lfn((fat32_lfn_t*)&es[i], lfn); continue; }
            if (es[i].attr & FAT_ATTR_VOLUME_ID) { memset(lfn, 0, sizeof(lfn)); continue; }
            char fn[16]; fat32_83_to_name(es[i].name, es[i].ext, fn);
            bool m = (lfn[0] && fat32_stricmp(lfn, name) == 0) || fat32_stricmp(fn, name) == 0;
            memset(lfn, 0, sizeof(lfn));
            if (m) {
                uint32_t fc = ((uint32_t)es[i].cluster_hi << 16) | es[i].cluster_lo;
                fat32_free_chain(fc);
                es[i].name[0] = 0xE5;
                fat32_write_cluster(cluster, cb);
                del = true;
                break;
            }
        }
        if (!del) cluster = fat32_next_cluster(cluster);
    }
unl_done:
    kfree(cb);
    return del ? 0 : -1;
}



static uint32_t fat32_vfs_read(vfs_node_t *node, uint32_t offset,
                                uint32_t size, uint8_t *buf) {
    uint32_t cluster  = node->impl;
    uint32_t file_size = node->size;
    uint32_t bytes_clus = fat32.bytes_per_clus;
    uint8_t *clus_buf;

    if (offset >= file_size) return 0;
    if (offset + size > file_size) size = file_size - offset;

    clus_buf = (uint8_t *)kmalloc(bytes_clus);
    if (!clus_buf) return 0;

    uint32_t bytes_read    = 0;
    uint32_t clus_offset   = offset / bytes_clus;  
    uint32_t byte_in_clus  = offset % bytes_clus;  

    
    uint32_t i;
    for (i = 0; i < clus_offset && cluster < 0x0FFFFFF8; i++) {
        cluster = fat32_next_cluster(cluster);
    }

    while (bytes_read < size && cluster < 0x0FFFFFF8) {
        if (!fat32_read_cluster(cluster, clus_buf)) break;

        uint32_t available = bytes_clus - byte_in_clus;
        uint32_t to_copy   = size - bytes_read;
        if (to_copy > available) to_copy = available;

        memcpy(buf + bytes_read, clus_buf + byte_in_clus, to_copy);
        bytes_read   += to_copy;
        byte_in_clus  = 0;

        cluster = fat32_next_cluster(cluster);
    }

    kfree(clus_buf);
    return bytes_read;
}

static dirent_t *fat32_vfs_readdir(vfs_node_t *node, uint32_t index) {
    uint32_t  cluster   = node->impl;
    uint32_t  bytes_clus = fat32.bytes_per_clus;
    uint8_t  *clus_buf   = (uint8_t *)kmalloc(bytes_clus);
    if (!clus_buf) return 0;

    uint32_t  entry_count = 0;
    dirent_t *result      = 0;

    while (cluster < 0x0FFFFFF8) {
        if (!fat32_read_cluster(cluster, clus_buf)) break;

        fat32_dirent_t *entries = (fat32_dirent_t *)clus_buf;
        uint32_t n_entries = bytes_clus / sizeof(fat32_dirent_t);
        uint32_t i;

        for (i = 0; i < n_entries; i++) {
            fat32_dirent_t *e = &entries[i];

            if (e->name[0] == 0x00) goto done;   
            if (e->name[0] == 0xE5) continue;     
            if (e->attr == FAT_ATTR_LFN) continue; 
            if (e->attr & FAT_ATTR_VOLUME_ID) continue;

            if (entry_count == index) {
                result = (dirent_t *)kmalloc(sizeof(dirent_t));
                if (result) {
                    fat32_83_to_name(e->name, e->ext, result->name);
                    result->inode = ((uint32_t)e->cluster_hi << 16) | e->cluster_lo;
                    result->type  = (e->attr & FAT_ATTR_DIRECTORY) ?
                                    VFS_DIRECTORY : VFS_FILE;
                }
                goto done;
            }
            entry_count++;
        }
        cluster = fat32_next_cluster(cluster);
    }

done:
    kfree(clus_buf);
    return result;
}

static vfs_node_t *fat32_vfs_finddir(vfs_node_t *node, const char *name) {
    uint32_t  cluster    = node->impl;
    uint32_t  bytes_clus = fat32.bytes_per_clus;
    uint8_t  *clus_buf   = (uint8_t *)kmalloc(bytes_clus);
    if (!clus_buf) return 0;

    vfs_node_t *result = 0;

    
    char lfn_buf[256];
    memset(lfn_buf, 0, sizeof(lfn_buf));

    while (cluster < 0x0FFFFFF8) {
        if (!fat32_read_cluster(cluster, clus_buf)) break;

        fat32_dirent_t *entries = (fat32_dirent_t *)clus_buf;
        uint32_t n_entries = bytes_clus / sizeof(fat32_dirent_t);
        uint32_t i;

        for (i = 0; i < n_entries; i++) {
            fat32_dirent_t *e = &entries[i];

            if (e->name[0] == 0x00) goto done;          
            if (e->name[0] == 0xE5) {                    
                memset(lfn_buf, 0, sizeof(lfn_buf));
                continue;
            }

            if (e->attr == FAT_ATTR_LFN) {
                
                fat32_extract_lfn((fat32_lfn_t *)e, lfn_buf);
                continue;
            }

            if (e->attr & FAT_ATTR_VOLUME_ID) {
                memset(lfn_buf, 0, sizeof(lfn_buf));
                continue;
            }

            
            char fname[16];
            fat32_83_to_name(e->name, e->ext, fname);

            bool match = false;
            if (lfn_buf[0] != 0 && fat32_stricmp(lfn_buf, name) == 0) match = true;
            if (!match && fat32_stricmp(fname, name) == 0)             match = true;

            
            char display_name[256];
            if (lfn_buf[0] != 0) {
                int dn = 0;
                while (lfn_buf[dn] && dn < 255) { display_name[dn] = lfn_buf[dn]; dn++; }
                display_name[dn] = 0;
            } else {
                int dn = 0;
                while (fname[dn] && dn < 15) { display_name[dn] = fname[dn]; dn++; }
                display_name[dn] = 0;
            }
            memset(lfn_buf, 0, sizeof(lfn_buf));   

            if (match) {
                result = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
                if (result) {
                    memset(result, 0, sizeof(vfs_node_t));
                    strcpy(result->name, display_name);
                    result->size    = e->file_size;
                    result->impl    = ((uint32_t)e->cluster_hi << 16) | e->cluster_lo;
                    result->inode   = node->impl;  
                    result->flags   = (e->attr & FAT_ATTR_DIRECTORY) ?
                                      VFS_DIRECTORY : VFS_FILE;
                    fat32_set_node_ops(result);
                }
                goto done;
            }
        }
        cluster = fat32_next_cluster(cluster);
    }

done:
    kfree(clus_buf);
    return result;
}



bool fat32_init(uint32_t lba_start) {
    uint8_t sector[512];

    fat32.lba_start = lba_start;
    fat32.mounted   = false;

    
    if (!ide_read_sectors(lba_start, 1, sector)) return false;

    memcpy(&fat32.bpb, sector, sizeof(fat32_bpb_t));

    
    if (memcmp(fat32.bpb.fs_type, "FAT32   ", 8) != 0) return false;

    fat32.sectors_per_clus = fat32.bpb.sectors_per_cluster;
    fat32.bytes_per_clus   = fat32.sectors_per_clus * fat32.bpb.bytes_per_sector;
    fat32.fat_lba          = lba_start + fat32.bpb.reserved_sectors;
    fat32.data_lba         = fat32.fat_lba +
                             (fat32.bpb.num_fats * fat32.bpb.fat_size_32);
    fat32.root_cluster     = fat32.bpb.root_cluster;
    fat32.mounted          = true;

    return true;
}

vfs_node_t *fat32_get_root(void) {
    if (!fat32.mounted) return 0;

    memset(&fat32_root_node, 0, sizeof(vfs_node_t));
    strcpy(fat32_root_node.name, "/");
    fat32_root_node.flags = VFS_DIRECTORY;
    fat32_root_node.impl  = fat32.root_cluster;
    fat32_root_node.inode = fat32.root_cluster;
    fat32_set_node_ops(&fat32_root_node);
    return &fat32_root_node;
}
