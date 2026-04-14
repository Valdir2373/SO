/*
 * drivers/ide.c — Driver ATA PIO Mode (28-bit LBA)
 * Controlador IDE primário: portas 0x1F0-0x1F7, IRQ14.
 * Usa polling (BSY flag) sem IRQ para simplicidade.
 */

#include <drivers/ide.h>
#include <drivers/vga.h>
#include <types.h>
#include <io.h>

/* Portas do controlador IDE primário */
#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_FEATURES    0x1F1
#define ATA_SECTOR_CNT  0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

/* Bits do registrador de status */
#define ATA_SR_BSY   0x80   /* Busy */
#define ATA_SR_DRDY  0x40   /* Drive Ready */
#define ATA_SR_DRQ   0x08   /* Data Request */
#define ATA_SR_ERR   0x01   /* Error */

/* Comandos ATA */
#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_IDENTIFY   0xEC
#define ATA_CMD_FLUSH      0xE7

static bool disk_present    = false;
static uint32_t sector_count = 0;

/* Aguarda até BSY=0 (com timeout) */
static bool ata_wait_ready(void) {
    uint32_t timeout = 100000;
    while ((inb(ATA_STATUS) & ATA_SR_BSY) && timeout--);
    return timeout > 0;
}

/* Aguarda DRQ=1 (dados prontos) */
static bool ata_wait_drq(void) {
    uint32_t timeout = 100000;
    uint8_t status;
    while (timeout--) {
        status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR) return false;
        if (status & ATA_SR_DRQ) return true;
    }
    return false;
}

void ide_init(void) {
    uint16_t identify[256];
    uint32_t i;

    /* Soft reset: escreve 0x04 no Device Control (0x3F6), depois 0x00 */
    outb(0x3F6, 0x04);
    for (i = 0; i < 1000; i++) io_wait();
    outb(0x3F6, 0x00);
    for (i = 0; i < 1000; i++) io_wait();

    /* Seleciona drive 0 (master) */
    outb(ATA_DRIVE_HEAD, 0xA0);
    for (i = 0; i < 400; i++) io_wait();

    /* Verifica se há drive conectado */
    if (inb(ATA_STATUS) == 0xFF) {
        /* Nenhum drive */
        return;
    }

    if (!ata_wait_ready()) return;

    /* Envia comando IDENTIFY */
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    for (i = 0; i < 400; i++) io_wait();

    /* Verifica se respondeu */
    if (inb(ATA_STATUS) == 0) return;

    if (!ata_wait_drq()) return;

    /* Lê 256 words de identificação */
    for (i = 0; i < 256; i++) {
        identify[i] = inw(ATA_DATA);
    }

    /*
     * Sector count em LBA28: words 60-61 (uint32_t little-endian).
     * Word 83 bit 10 indica suporte a LBA48 (não usamos aqui).
     */
    sector_count = ((uint32_t)identify[61] << 16) | identify[60];
    disk_present  = (sector_count > 0);
}

bool ide_disk_present(void) { return disk_present; }
uint32_t ide_get_sector_count(void) { return sector_count; }

bool ide_read_sectors(uint32_t lba, uint8_t count, void *buffer) {
    if (!disk_present || count == 0) return false;

    if (!ata_wait_ready()) return false;

    /* Configura LBA28 */
    outb(ATA_DRIVE_HEAD,  0xE0 | ((lba >> 24) & 0x0F)); /* LBA mode + bits 24-27 */
    outb(ATA_SECTOR_CNT,  count);
    outb(ATA_LBA_LO,      (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID,     (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI,      (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND,     ATA_CMD_READ_PIO);

    uint16_t *buf = (uint16_t *)buffer;
    uint8_t  sector;

    for (sector = 0; sector < count; sector++) {
        if (!ata_wait_drq()) return false;

        uint16_t i;
        for (i = 0; i < 256; i++) {
            *buf++ = inw(ATA_DATA);
        }
    }
    return true;
}

bool ide_write_sectors(uint32_t lba, uint8_t count, const void *buffer) {
    if (!disk_present || count == 0) return false;

    if (!ata_wait_ready()) return false;

    outb(ATA_DRIVE_HEAD,  0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT,  count);
    outb(ATA_LBA_LO,      (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID,     (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI,      (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND,     ATA_CMD_WRITE_PIO);

    const uint16_t *buf = (const uint16_t *)buffer;
    uint8_t sector;

    for (sector = 0; sector < count; sector++) {
        if (!ata_wait_drq()) return false;

        uint16_t i;
        for (i = 0; i < 256; i++) {
            outw(ATA_DATA, *buf++);
        }
    }

    /* Flush cache */
    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    ata_wait_ready();
    return true;
}
