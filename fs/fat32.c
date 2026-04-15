/*
 * fs/fat32.c — Driver FAT32
 * Suporta leitura de arquivos e diretórios numa partição FAT32.
 * Usa o driver IDE PIO para acesso ao disco.
 */

#include <fs/fat32.h>
#include <fs/vfs.h>
#include <drivers/ide.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <types.h>

/* Estado do filesystem montado */
static struct {
    fat32_bpb_t bpb;
    uint32_t    lba_start;         /* LBA da partição */
    uint32_t    fat_lba;           /* LBA da FAT */
    uint32_t    data_lba;          /* LBA da área de dados */
    uint32_t    root_cluster;      /* Cluster do diretório raiz */
    uint32_t    sectors_per_clus;
    uint32_t    bytes_per_clus;
    bool        mounted;
} fat32;

static vfs_node_t fat32_root_node;

/* ---- Helpers ---- */

/*
 * Entrada LFN (Long File Name) do FAT32 — atributo 0x0F.
 * Armazena até 13 caracteres UTF-16LE por entrada.
 * As entradas aparecem em ordem reversa antes da entrada 8.3 correspondente.
 */
typedef struct {
    uint8_t  seq;         /* Seq | 0x40 se última parte; seq 1-indexed */
    uint16_t name1[5];    /* Chars 1-5 em UTF-16LE */
    uint8_t  attr;        /* 0x0F */
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];    /* Chars 6-11 em UTF-16LE */
    uint16_t cluster;     /* Sempre 0 */
    uint16_t name3[2];    /* Chars 12-13 em UTF-16LE */
} __attribute__((packed)) fat32_lfn_t;

/* Comparação de strings ignorando maiúsculas/minúsculas (ASCII) */
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

/*
 * Extrai até 13 chars da entrada LFN para lfn_buf na posição (seq-1)*13.
 * Para quando encontra 0x0000 (fim) ou 0xFFFF (preenchimento).
 * lfn_buf deve ter pelo menos 256 bytes e estar zerado.
 */
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

/* Lê o próximo cluster da FAT */
static uint32_t fat32_next_cluster(uint32_t cluster) {
    uint32_t fat_offset  = cluster * 4;
    uint32_t fat_sector  = fat32.fat_lba + (fat_offset / 512);
    uint32_t fat_index   = (fat_offset % 512) / 4;
    uint8_t  sector[512];

    if (!ide_read_sectors(fat_sector, 1, sector)) return 0x0FFFFFFF;

    uint32_t *fat = (uint32_t *)sector;
    return fat[fat_index] & 0x0FFFFFFF;
}

/* Lê setores de um cluster para buffer */
static bool fat32_read_cluster(uint32_t cluster, void *buffer) {
    uint32_t lba = fat32.data_lba + (cluster - 2) * fat32.sectors_per_clus;
    return ide_read_sectors(lba, (uint8_t)fat32.sectors_per_clus, buffer);
}

/* Converte nome FAT 8.3 para string legível "FILENAME.EXT" */
static void fat32_83_to_name(const uint8_t *fat_name, const uint8_t *fat_ext,
                              char *out) {
    int i = 0, j = 0;

    /* Nome (sem espaços trailing) */
    while (i < 8 && fat_name[i] != ' ') {
        out[j++] = (char)fat_name[i++];
    }

    /* Extensão */
    if (fat_ext[0] != ' ') {
        out[j++] = '.';
        int k = 0;
        while (k < 3 && fat_ext[k] != ' ') {
            out[j++] = (char)fat_ext[k++];
        }
    }
    out[j] = 0;
}

/* ---- Callbacks VFS ---- */

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
    uint32_t clus_offset   = offset / bytes_clus;  /* Qual cluster no chain */
    uint32_t byte_in_clus  = offset % bytes_clus;  /* Offset dentro do cluster */

    /* Avança ao cluster correto */
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

            if (e->name[0] == 0x00) goto done;   /* Fim do diretório */
            if (e->name[0] == 0xE5) continue;     /* Entrada deletada */
            if (e->attr == FAT_ATTR_LFN) continue; /* LFN — ignora por ora */
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

    /*
     * lfn_buf acumula o nome longo (LFN) da entrada atual.
     * Entradas LFN aparecem em ordem REVERSA antes da entrada 8.3.
     * Cada entrada LFN carrega 13 chars na posição (seq-1)*13.
     */
    char lfn_buf[256];
    memset(lfn_buf, 0, sizeof(lfn_buf));

    while (cluster < 0x0FFFFFF8) {
        if (!fat32_read_cluster(cluster, clus_buf)) break;

        fat32_dirent_t *entries = (fat32_dirent_t *)clus_buf;
        uint32_t n_entries = bytes_clus / sizeof(fat32_dirent_t);
        uint32_t i;

        for (i = 0; i < n_entries; i++) {
            fat32_dirent_t *e = &entries[i];

            if (e->name[0] == 0x00) goto done;          /* Fim do diretório */
            if (e->name[0] == 0xE5) {                    /* Entrada deletada */
                memset(lfn_buf, 0, sizeof(lfn_buf));
                continue;
            }

            if (e->attr == FAT_ATTR_LFN) {
                /* Acumula fragmento LFN */
                fat32_extract_lfn((fat32_lfn_t *)e, lfn_buf);
                continue;
            }

            if (e->attr & FAT_ATTR_VOLUME_ID) {
                memset(lfn_buf, 0, sizeof(lfn_buf));
                continue;
            }

            /* Entrada 8.3 — tenta bater contra LFN (se houver) ou nome curto */
            char fname[16];
            fat32_83_to_name(e->name, e->ext, fname);

            bool match = false;
            if (lfn_buf[0] != 0 && fat32_stricmp(lfn_buf, name) == 0) match = true;
            if (!match && fat32_stricmp(fname, name) == 0)             match = true;

            /* Guarda nome para o nó antes de resetar o buffer */
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
            memset(lfn_buf, 0, sizeof(lfn_buf));   /* Reset para próxima entrada */

            if (match) {
                result = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
                if (result) {
                    memset(result, 0, sizeof(vfs_node_t));
                    strcpy(result->name, display_name);
                    result->size    = e->file_size;
                    result->impl    = ((uint32_t)e->cluster_hi << 16) | e->cluster_lo;
                    result->flags   = (e->attr & FAT_ATTR_DIRECTORY) ?
                                      VFS_DIRECTORY : VFS_FILE;
                    result->read    = fat32_vfs_read;
                    result->readdir = fat32_vfs_readdir;
                    result->finddir = fat32_vfs_finddir;
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

/* ---- Inicialização ---- */

bool fat32_init(uint32_t lba_start) {
    uint8_t sector[512];

    fat32.lba_start = lba_start;
    fat32.mounted   = false;

    /* Lê o boot sector */
    if (!ide_read_sectors(lba_start, 1, sector)) return false;

    memcpy(&fat32.bpb, sector, sizeof(fat32_bpb_t));

    /* Valida "FAT32   " */
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
    fat32_root_node.flags   = VFS_DIRECTORY;
    fat32_root_node.impl    = fat32.root_cluster;
    fat32_root_node.read    = fat32_vfs_read;
    fat32_root_node.readdir = fat32_vfs_readdir;
    fat32_root_node.finddir = fat32_vfs_finddir;
    return &fat32_root_node;
}
