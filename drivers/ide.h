/*
 * drivers/ide.h — Driver IDE/ATA em modo PIO
 * Suporta leitura e escrita de setores de 512 bytes via LBA28.
 */
#ifndef _IDE_H
#define _IDE_H

#include <types.h>

#define IDE_SECTOR_SIZE  512

/* Inicializa o controlador IDE primário e detecta disco */
void ide_init(void);

/* Retorna true se disco foi detectado */
bool ide_disk_present(void);

/* Lê 'count' setores a partir de LBA para buffer */
bool ide_read_sectors(uint32_t lba, uint8_t count, void *buffer);

/* Escreve 'count' setores em LBA a partir de buffer */
bool ide_write_sectors(uint32_t lba, uint8_t count, const void *buffer);

/* Retorna tamanho total do disco em setores */
uint32_t ide_get_sector_count(void);

#endif /* _IDE_H */
