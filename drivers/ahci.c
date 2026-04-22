


#include <drivers/ahci.h>
#include <drivers/pci.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <io.h>
#include <types.h>


#define HBA_CAP   0x00
#define HBA_GHC   0x04
#define HBA_IS    0x08
#define HBA_PI    0x0C
#define HBA_VS    0x10
#define HBA_GHC_AE   (1u<<31)  
#define HBA_GHC_HR   (1u<<0)   

#define PORT_CLB  0x00
#define PORT_CLBU 0x04
#define PORT_FB   0x08
#define PORT_FBU  0x0C
#define PORT_IS   0x10
#define PORT_IE   0x14
#define PORT_CMD  0x18
#define PORT_TFD  0x20
#define PORT_SIG  0x24
#define PORT_SSTS 0x28
#define PORT_SCTL 0x2C
#define PORT_SERR 0x30
#define PORT_SACT 0x34
#define PORT_CI   0x38

#define CMD_ST    0x0001u
#define CMD_FRE   0x0010u
#define CMD_FR    0x4000u
#define CMD_CR    0x8000u

#define FIS_TYPE_REG_H2D 0x27u

#define ATA_CMD_READ_DMA_EX  0x25u
#define ATA_CMD_WRITE_DMA_EX 0x35u
#define ATA_CMD_IDENTIFY     0xECu
#define ATA_DEV_LBA          0x40u

#define SATA_SIG_ATA  0x00000101u
#define SATA_SIG_ATAPI 0xEB140101u


typedef struct __attribute__((packed)) {
    uint8_t  fis_type;
    uint8_t  pmport_c;
    uint8_t  command;
    uint8_t  featurel;
    uint8_t  lba0, lba1, lba2;
    uint8_t  device;
    uint8_t  lba3, lba4, lba5;
    uint8_t  featureh;
    uint8_t  countl, counth;
    uint8_t  icc;
    uint8_t  control;
    uint8_t  rsv[4];
} fis_h2d_t;

typedef struct __attribute__((packed)) {
    uint32_t dba;
    uint32_t dbau;
    uint32_t _res;
    uint32_t dbc;   
} hba_prd_t;

typedef struct __attribute__((packed)) {
    uint8_t   cfis[64];
    uint8_t   acmd[16];
    uint8_t   _res[48];
    hba_prd_t prdt[8];
} hba_cmd_tbl_t;

typedef struct __attribute__((packed)) {
    uint16_t desc;    
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t _res[4];
} hba_cmd_hdr_t;


static uint32_t      g_hba   = 0;    
static int           g_port  = -1;   
static hba_cmd_hdr_t *g_cl   = 0;    
static uint8_t       *g_fis  = 0;    
static hba_cmd_tbl_t *g_ct   = 0;    
static uint32_t      g_sects = 0;    
static bool          g_ok    = false;


static inline volatile uint32_t *hba_r(uint32_t off) {
    return (volatile uint32_t *)(g_hba + off);
}
static inline volatile uint32_t *port_r(uint32_t off) {
    return (volatile uint32_t *)(g_hba + 0x100u + (uint32_t)g_port * 0x80u + off);
}

static void port_stop(void) {
    volatile uint32_t *cmd = port_r(PORT_CMD);
    *cmd &= ~(uint32_t)CMD_ST;
    uint32_t t = 0;
    while ((*cmd & CMD_CR) && ++t < 500000);
    *cmd &= ~(uint32_t)CMD_FRE;
    while ((*cmd & CMD_FR)  && ++t < 500000);
}

static void port_start(void) {
    volatile uint32_t *cmd = port_r(PORT_CMD);
    uint32_t t = 0;
    while ((*cmd & CMD_CR) && ++t < 500000);
    *cmd |= CMD_FRE | CMD_ST;
}

static bool port_wait(void) {
    volatile uint32_t *ci  = port_r(PORT_CI);
    volatile uint32_t *tfd = port_r(PORT_TFD);
    uint32_t t = 0;
    while ((*ci & 1u) && ++t < 2000000) {
        if (*tfd & 0x01u) { 
            *port_r(PORT_IS) = 0xFFFFFFFFu;
            return false;
        }
    }
    return (t < 2000000);
}


static bool ahci_issue(uint8_t cmd, uint64_t lba, uint16_t count,
                       void *buf, bool write) {
    if (!g_ok || !buf) return false;

    *port_r(PORT_IS) = 0xFFFFFFFFu;

    
    fis_h2d_t *fis = (fis_h2d_t *)g_ct->cfis;
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80u;  
    fis->command  = cmd;
    fis->device   = ATA_DEV_LBA;
    fis->lba0 = (uint8_t)lba;
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);
    fis->countl = (uint8_t)count;
    fis->counth = (uint8_t)(count >> 8);

    
    uint32_t byte_count = (uint32_t)count * 512u;
    g_ct->prdt[0].dba  = (uint32_t)(uintptr_t)buf;
    g_ct->prdt[0].dbau = 0;
    g_ct->prdt[0]._res = 0;
    g_ct->prdt[0].dbc  = (byte_count - 1u) & 0x3FFFFFu;

    
    g_cl[0].desc  = (uint16_t)((sizeof(fis_h2d_t) / 4u) | (write ? (1u<<6) : 0u));
    g_cl[0].prdtl = 1;
    g_cl[0].prdbc = 0;
    g_cl[0].ctba  = (uint32_t)(uintptr_t)g_ct;
    g_cl[0].ctbau = 0;

    *port_r(PORT_CI) = 1u;   
    return port_wait();
}


bool ahci_init(void) {
    
    uint8_t bus, slot, func;
    bool found = false;
    for (bus = 0; bus < 8 && !found; bus++) {
        for (slot = 0; slot < 32 && !found; slot++) {
            uint16_t vid = pci_read16(bus, slot, 0, PCI_VENDOR_ID);
            if (vid == 0xFFFF) continue;
            uint8_t cls = (uint8_t)(pci_read32(bus, slot, 0, 0x08) >> 24);
            uint8_t sub = (uint8_t)(pci_read32(bus, slot, 0, 0x08) >> 16);
            if (cls == 0x01 && sub == 0x06) {
                
                uint16_t pcmd = pci_read16(bus, slot, 0, PCI_COMMAND);
                pci_write16(bus, slot, 0, PCI_COMMAND,
                            (uint16_t)(pcmd | PCI_CMD_IO_SPACE | PCI_CMD_BUS_MASTER | 0x02));
                
                g_hba = pci_read32(bus, slot, 0, 0x24) & 0xFFFFFFF0u;
                found = true;
            }
        }
    }
    if (!found || !g_hba) return false;

    
    *hba_r(HBA_GHC) |= HBA_GHC_AE;

    
    uint32_t pi = *hba_r(HBA_PI);
    int i;
    for (i = 0; i < 32; i++) {
        if (!(pi & (1u << i))) continue;
        uint32_t pb = g_hba + 0x100u + (uint32_t)i * 0x80u;
        uint32_t det = (*(volatile uint32_t *)(pb + PORT_SSTS)) & 0xFu;
        uint32_t sig = *(volatile uint32_t *)(pb + PORT_SIG);
        if (det == 3u && (sig == SATA_SIG_ATA || sig == 0x00000101u)) {
            g_port = i; break;
        }
    }
    if (g_port < 0) return false;

    
    g_cl  = (hba_cmd_hdr_t *)kmalloc(1024 + 1024); 
    if (!g_cl) return false;
    
    uint32_t cl_addr = ((uint32_t)(uintptr_t)g_cl + 1023u) & ~1023u;
    g_cl = (hba_cmd_hdr_t *)(uintptr_t)cl_addr;

    g_fis = (uint8_t *)kmalloc(512);  
    if (!g_fis) return false;
    uint32_t fis_addr = ((uint32_t)(uintptr_t)g_fis + 255u) & ~255u;
    g_fis = (uint8_t *)(uintptr_t)fis_addr;

    g_ct = (hba_cmd_tbl_t *)kmalloc(sizeof(hba_cmd_tbl_t) + 128u);
    if (!g_ct) return false;
    uint32_t ct_addr = ((uint32_t)(uintptr_t)g_ct + 127u) & ~127u;
    g_ct = (hba_cmd_tbl_t *)(uintptr_t)ct_addr;

    memset((void *)(uintptr_t)cl_addr,  0, 1024);
    memset((void *)(uintptr_t)fis_addr, 0, 256);
    memset(g_ct, 0, sizeof(*g_ct));

    port_stop();

    *port_r(PORT_CLB)  = cl_addr;
    *port_r(PORT_CLBU) = 0;
    *port_r(PORT_FB)   = fis_addr;
    *port_r(PORT_FBU)  = 0;
    *port_r(PORT_SERR) = 0xFFFFFFFFu;
    *port_r(PORT_IS)   = 0xFFFFFFFFu;
    *port_r(PORT_IE)   = 0;

    port_start();
    g_ok = true;

    
    uint16_t id[256];
    memset(id, 0, sizeof(id));
    if (ahci_issue(ATA_CMD_IDENTIFY, 0, 1, id, false)) {
        
        g_sects = id[100] | ((uint32_t)id[101] << 16);
        if (!g_sects)
            g_sects = id[60] | ((uint32_t)id[61] << 16); 
    }

    return true;
}

bool     ahci_present(void)                               { return g_ok; }
uint32_t ahci_sector_count(void)                          { return g_sects; }

int ahci_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    if (!g_ok) return -1;
    return ahci_issue(ATA_CMD_READ_DMA_EX, lba, count, buf, false) ? 0 : -1;
}

int ahci_write_sectors(uint32_t lba, uint8_t count, const void *buf) {
    if (!g_ok) return -1;
    return ahci_issue(ATA_CMD_WRITE_DMA_EX, lba, count, (void *)buf, true) ? 0 : -1;
}
