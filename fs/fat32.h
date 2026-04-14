/*
 * fs/fat32.h — Driver FAT32
 * Leitura e escrita de arquivos/diretórios em partições FAT32.
 */
#ifndef _FAT32_H
#define _FAT32_H

#include <types.h>
#include <fs/vfs.h>

/* Boot Sector / BPB (BIOS Parameter Block) */
typedef struct {
    uint8_t  jmp[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;     /* 0 em FAT32 */
    uint16_t total_sectors_16;     /* 0 se > 65535 */
    uint8_t  media_type;
    uint16_t fat_size_16;          /* 0 em FAT32 */
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    /* Extensão FAT32 */
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];           /* "FAT32   " */
} __attribute__((packed)) fat32_bpb_t;

/* Entrada de diretório FAT32 (32 bytes) */
typedef struct {
    uint8_t  name[8];
    uint8_t  ext[3];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  create_time_ms;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_hi;   /* High 16 bits do primeiro cluster */
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_lo;   /* Low 16 bits do primeiro cluster */
    uint32_t file_size;
} __attribute__((packed)) fat32_dirent_t;

/* Atributos de entrada de diretório */
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F   /* Long File Name entry */

/* Valores especiais da FAT */
#define FAT32_EOC   0x0FFFFFF8   /* End of Chain */
#define FAT32_FREE  0x00000000
#define FAT32_BAD   0x0FFFFFF7

/* Inicializa o driver FAT32 num disco IDE */
bool fat32_init(uint32_t lba_start);

/* Retorna o nó raiz do FAT32 como vfs_node_t */
vfs_node_t *fat32_get_root(void);

#endif /* _FAT32_H */
