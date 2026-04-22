


#include <drivers/ac97.h>
#include <drivers/pci.h>
#include <io.h>
#include <types.h>


#define NAM_RESET       0x00
#define NAM_MASTER_VOL  0x02   
#define NAM_PCM_VOL     0x18   
#define NAM_MIC_VOL     0x0E   
#define NAM_RECORD_SEL  0x1A


static uint16_t ac97_nam  = 0;   
static uint16_t ac97_nabm = 0;   
static bool     ac97_ok   = false;
static int      ac97_vol  = 80;  


static const uint16_t AC97_DEVS[] = {
    0x2415, 
    0x2425, 
    0x2445, 
    0x2485, 
    0x24C5, 
    0x24D5, 
    0x266E, 
    0x27DE, 
    0
};

bool ac97_available(void) { return ac97_ok; }
int  ac97_get_volume(void) { return ac97_vol; }

void ac97_set_volume(int pct) {
    if (!ac97_ok) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    ac97_vol = pct;

    if (pct == 0) {
        outw(ac97_nam + NAM_MASTER_VOL, 0x8000); 
        outw(ac97_nam + NAM_PCM_VOL,    0x8000);
        return;
    }
    
    uint8_t att = (uint8_t)((100 - pct) * 31 / 100);
    uint16_t vol = (uint16_t)(att | ((uint16_t)att << 8));
    outw(ac97_nam + NAM_MASTER_VOL, vol);
    outw(ac97_nam + NAM_PCM_VOL,    vol);
}

void ac97_init(void) {
    
    uint8_t bus, slot;
    for (bus = 0; bus < 8; bus++) {
        for (slot = 0; slot < 32; slot++) {
            uint16_t vendor = pci_read16(bus, slot, 0, PCI_VENDOR_ID);
            if (vendor != 0x8086) continue;
            uint16_t device = pci_read16(bus, slot, 0, PCI_DEVICE_ID);
            int i;
            for (i = 0; AC97_DEVS[i]; i++) {
                if (device == AC97_DEVS[i]) goto found;
            }
        }
    }
    return; 

found:
    
    ac97_nam  = (uint16_t)(pci_read32(bus, slot, 0, PCI_BAR0) & 0xFFFE);
    ac97_nabm = (uint16_t)(pci_read32(bus, slot, 0, PCI_BAR1) & 0xFFFE);

    
    uint16_t cmd = pci_read16(bus, slot, 0, PCI_COMMAND);
    pci_write16(bus, slot, 0, PCI_COMMAND,
                (uint16_t)(cmd | PCI_CMD_IO_SPACE | PCI_CMD_BUS_MASTER));

    
    outw(ac97_nam + NAM_RESET, 0);

    
    volatile int d = 100000;
    while (d-- > 0) __asm__ volatile ("nop");

    ac97_ok = true;

    
    ac97_set_volume(ac97_vol);
}
