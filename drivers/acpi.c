


#include <drivers/acpi.h>
#include <io.h>
#include <lib/string.h>
#include <types.h>


typedef struct {
    char     sig[8];
    uint8_t  checksum;
    char     oem[6];
    uint8_t  rev;
    uint32_t rsdt_addr;
} __attribute__((packed)) rsdp_t;

typedef struct {
    char     sig[4];
    uint32_t len;
    uint8_t  rev;
    uint8_t  cksum;
    char     oem[6];
    char     oem_tid[8];
    uint32_t oem_rev;
    uint32_t creator;
    uint32_t creator_rev;
} __attribute__((packed)) acpi_hdr_t;

typedef struct {
    acpi_hdr_t hdr;
    uint32_t   ptrs[1]; 
} __attribute__((packed)) rsdt_t;


typedef struct {
    acpi_hdr_t hdr;
    uint32_t   facs;
    uint32_t   dsdt;
    uint8_t    _pad[8];
    uint16_t   sci_int;
    uint32_t   smi_cmd;
    uint8_t    acpi_en;
    uint8_t    acpi_dis;
    uint8_t    s4bios;
    uint8_t    pst_cnt;
    uint32_t   pm1a_evt;
    uint32_t   pm1b_evt;
    uint32_t   pm1a_cnt;
    uint32_t   pm1b_cnt;
    uint32_t   pm2_cnt;
    uint32_t   pm_tmr;
    uint32_t   gpe0;
    uint32_t   gpe1;
    uint8_t    pm1_evt_len;
    uint8_t    pm1_cnt_len;
    uint8_t    pm2_cnt_len;
    uint8_t    pm_tmr_len;
    uint8_t    gpe0_len;
    uint8_t    gpe1_len;
    uint8_t    gpe1_base;
    uint8_t    cst_cnt;
    uint16_t   p_lvl2_lat;
    uint16_t   p_lvl3_lat;
    uint16_t   flush_size;
    uint16_t   flush_stride;
    uint8_t    duty_offset;
    uint8_t    duty_width;
    uint8_t    day_alrm;
    uint8_t    mon_alrm;
    uint8_t    century;
    uint16_t   iapc_boot_arch;
    uint8_t    _pad2;
    uint32_t   flags;
    uint8_t    reset_reg[12];
    uint8_t    reset_val;
} __attribute__((packed)) fadt_t;

static uint32_t pm1a_cnt_port = 0;
static uint32_t pm1b_cnt_port = 0;
static uint16_t slp_typa5     = 0; 
static uint16_t slp_typb5     = 0;
static bool     acpi_ok       = false;

static uint8_t acpi_cksum(const void *p, uint32_t len) {
    uint8_t s = 0;
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i;
    for (i = 0; i < len; i++) s += b[i];
    return s;
}


static rsdp_t *find_rsdp(void) {
    uint32_t i;
    
    uint16_t ebda_seg = *(uint16_t *)0x40E;
    uint32_t ebda = (uint32_t)ebda_seg << 4;
    if (ebda >= 0x80000 && ebda < 0xA0000) {
        for (i = ebda; i < ebda + 1024; i += 16) {
            if (memcmp((void *)i, "RSD PTR ", 8) == 0 &&
                acpi_cksum((void *)i, 20) == 0)
                return (rsdp_t *)i;
        }
    }
    
    for (i = 0xE0000; i < 0x100000; i += 16) {
        if (memcmp((void *)i, "RSD PTR ", 8) == 0 &&
            acpi_cksum((void *)i, 20) == 0)
            return (rsdp_t *)i;
    }
    return 0;
}


static void parse_s5(const uint8_t *dsdt_start, uint32_t len) {
    uint32_t i;
    for (i = 0; i < len - 6; i++) {
        
        if ((dsdt_start[i]   == '_' || (i > 0 && dsdt_start[i-1] == 0x08)) &&
            dsdt_start[i]   == '_' &&
            dsdt_start[i+1] == 'S' &&
            dsdt_start[i+2] == '5' &&
            dsdt_start[i+3] == '_') {
            
            uint32_t j = i + 4;
            if (dsdt_start[j] == 0x12) j++; 
            j++;                              
            if (dsdt_start[j] == 0x04) j++; 
            
            if (dsdt_start[j] == 0x0A) { 
                slp_typa5 = (uint16_t)(dsdt_start[j+1] & 0x1F) << 10;
                j += 2;
            } else if (dsdt_start[j] == 0x00) {
                slp_typa5 = 0;
                j++;
            }
            
            if (dsdt_start[j] == 0x0A) {
                slp_typb5 = (uint16_t)(dsdt_start[j+1] & 0x1F) << 10;
            }
            return;
        }
    }
    
    slp_typa5 = 7 << 10;
    slp_typb5 = 7 << 10;
}

void acpi_init(void) {
    rsdp_t *rsdp = find_rsdp();
    if (!rsdp) goto qemu_fallback;

    rsdt_t *rsdt = (rsdt_t *)rsdp->rsdt_addr;
    if (!rsdt || memcmp(rsdt->hdr.sig, "RSDT", 4) != 0) goto qemu_fallback;

    uint32_t nptrs = (rsdt->hdr.len - sizeof(acpi_hdr_t)) / 4;
    uint32_t i;
    fadt_t *fadt = 0;
    for (i = 0; i < nptrs; i++) {
        acpi_hdr_t *h = (acpi_hdr_t *)rsdt->ptrs[i];
        if (!h) continue;
        if (memcmp(h->sig, "FACP", 4) == 0) { fadt = (fadt_t *)h; break; }
    }
    if (!fadt) goto qemu_fallback;

    pm1a_cnt_port = fadt->pm1a_cnt;
    pm1b_cnt_port = fadt->pm1b_cnt;

    
    if (fadt->dsdt) {
        acpi_hdr_t *dsdt_hdr = (acpi_hdr_t *)fadt->dsdt;
        if (memcmp(dsdt_hdr->sig, "DSDT", 4) == 0) {
            parse_s5((const uint8_t *)dsdt_hdr + sizeof(acpi_hdr_t),
                     dsdt_hdr->len - sizeof(acpi_hdr_t));
        }
    }

    
    if (fadt->smi_cmd && fadt->acpi_en &&
        !(inw((uint16_t)pm1a_cnt_port) & 1)) {
        outb((uint16_t)fadt->smi_cmd, fadt->acpi_en);
        uint32_t t = 0;
        while (!(inw((uint16_t)pm1a_cnt_port) & 1) && t++ < 300)
            ; 
    }

    acpi_ok = true;
    return;

qemu_fallback:
    
    pm1a_cnt_port = 0x604;
    slp_typa5 = (uint16_t)(5 << 10); 
    acpi_ok = true;
}

void acpi_shutdown(void) {
    if (acpi_ok && pm1a_cnt_port) {
        outw((uint16_t)pm1a_cnt_port, (uint16_t)(0x2000 | slp_typa5));
        if (pm1b_cnt_port)
            outw((uint16_t)pm1b_cnt_port, (uint16_t)(0x2000 | slp_typb5));
    }
    
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    
    outb(0x64, 0xFE);
    __asm__ volatile("cli; hlt");
    for (;;) {}
}

void acpi_reboot(void) {
    
    uint32_t t;
    for (t = 0; t < 0x10000; t++) {
        if (!(inb(0x64) & 0x02)) break;
    }
    outb(0x64, 0xFE);
    __asm__ volatile("cli");
    
    volatile uint64_t null_idt = 0;
    __asm__ volatile("lidt (%0); int $0" : : "r"(&null_idt));
    for (;;) {}
}

bool acpi_available(void) { return acpi_ok; }
