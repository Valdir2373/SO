

#include <drivers/usb_hid.h>
#include <drivers/pci.h>
#include <drivers/keyboard.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <io.h>
#include <types.h>


#define USBCMD      0x00   
#define USBSTS      0x02   
#define USBINTR     0x04   
#define FRNUM       0x06   
#define FLBASEADD   0x08   
#define SOFMOT      0x0C   
#define PORTSC1     0x10   
#define PORTSC2     0x12   


#define CMD_RS      0x0001  
#define CMD_HCRESET 0x0002  
#define CMD_GRESET  0x0004  
#define CMD_EGSM    0x0008  
#define CMD_FGR     0x0010  
#define CMD_SWDBG   0x0020  
#define CMD_CF      0x0040  
#define CMD_MAXP    0x0080  


#define PORTSC_CCS    0x0001  
#define PORTSC_CSC    0x0002  
#define PORTSC_PED    0x0004  
#define PORTSC_PEDC   0x0008  
#define PORTSC_LS     0x0030  
#define PORTSC_RD     0x0040  
#define PORTSC_LSDA   0x0100  
#define PORTSC_RESET  0x0200  
#define PORTSC_SUSP   0x1000  


typedef struct __attribute__((packed)) {
    uint32_t link;      
    uint32_t status;    
    uint32_t token;     
    uint32_t buf;       
    
    uint32_t _sw[4];
} uhci_td_t;


typedef struct __attribute__((packed)) {
    uint32_t qhlp;  
    uint32_t qelp;  
    uint32_t _sw[2];
} uhci_qh_t;


typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
} __attribute__((packed)) hid_kbd_report_t;


typedef struct __attribute__((packed)) {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_t;


#define REQ_GET_DESCRIPTOR   0x06
#define REQ_SET_ADDRESS      0x05
#define REQ_SET_CONFIGURATION 0x09
#define REQ_SET_PROTOCOL     0x0B   
#define REQ_SET_IDLE         0x0A   


#define HID_BOOT_PROTOCOL    0


#define DESC_DEVICE          0x01
#define DESC_CONFIG          0x02
#define DESC_STRING          0x03
#define DESC_INTERFACE       0x04
#define DESC_ENDPOINT        0x05
#define DESC_HID             0x21
#define DESC_REPORT          0x22


#define TD_LINK_TERMINATE    0x01
#define TD_LINK_QH           0x02
#define TD_LINK_DEPTH        0x04
#define TD_STATUS_ACTIVE     (1u << 23)
#define TD_STATUS_STALL      (1u << 22)
#define TD_STATUS_BABBLE     (1u << 20)
#define TD_STATUS_IOC        (1u << 24)
#define TD_STATUS_ISO        (1u << 25)
#define TD_STATUS_LS         (1u << 26)
#define TD_STATUS_SPD        (1u << 29)
#define TD_C_ERR(n)          ((uint32_t)(n) << 27)

#define TD_PID_SETUP  0x2Du
#define TD_PID_IN     0x69u
#define TD_PID_OUT    0xE1u

#define TD_TOKEN(pid,addr,endp,toggle,maxlen) \
    ((uint32_t)(pid) | ((uint32_t)(addr)<<8) | ((uint32_t)(endp)<<15) | \
     ((uint32_t)(toggle)<<19) | ((uint32_t)((maxlen)-1)<<21))


#define QH_LP_TERMINATE 0x01
#define QH_LP_QH        0x02


static uint16_t g_ubase = 0;     
static bool     g_ok    = false;
static bool     g_kbd   = false;
static bool     g_mouse = false;
static uint8_t  g_kbd_addr  = 0; 
static uint8_t  g_kbd_endp  = 1; 
static uint8_t  g_kbd_toggle = 0;
static uint8_t  g_kbd_maxpkt = 8;


static uint32_t *g_fl = 0;


#define TD_POOL  64
#define QH_POOL  16
static uhci_td_t g_td_pool[TD_POOL] __attribute__((aligned(16)));
static uhci_qh_t g_qh_pool[QH_POOL] __attribute__((aligned(16)));
static int g_td_idx = 0, g_qh_idx = 0;

static hid_kbd_report_t g_kbd_report;
static hid_kbd_report_t g_kbd_prev;

static uhci_td_t *alloc_td(void) {
    if (g_td_idx >= TD_POOL) return 0;
    uhci_td_t *td = &g_td_pool[g_td_idx++];
    memset(td, 0, sizeof(*td));
    return td;
}
static uhci_qh_t *alloc_qh(void) {
    if (g_qh_idx >= QH_POOL) return 0;
    uhci_qh_t *qh = &g_qh_pool[g_qh_idx++];
    memset(qh, 0, sizeof(*qh));
    return qh;
}


static bool td_wait(uhci_td_t *td, uint32_t timeout_ms) {
    uint32_t t = 0;
    while ((td->status & TD_STATUS_ACTIVE) && t++ < timeout_ms * 1000)
        __asm__ volatile("pause");
    return !(td->status & TD_STATUS_ACTIVE) && !(td->status & 0x7E0000u);
}




static bool uhci_control(uint8_t addr, usb_setup_t *setup, void *data, bool in) {
    if (!g_ok) return false;

    uhci_qh_t *qh = alloc_qh();
    if (!qh) return false;
    uhci_td_t *td_setup = alloc_td();
    uhci_td_t *td_data  = data ? alloc_td() : 0;
    uhci_td_t *td_status = alloc_td();
    if (!td_setup || !td_status) return false;

    
    td_setup->link   = data ? ((uint32_t)(uintptr_t)td_data  | TD_LINK_DEPTH) :
                               ((uint32_t)(uintptr_t)td_status | TD_LINK_DEPTH);
    td_setup->status = TD_STATUS_ACTIVE | TD_C_ERR(3);
    td_setup->token  = TD_TOKEN(TD_PID_SETUP, addr, 0, 0, 8);
    td_setup->buf    = (uint32_t)(uintptr_t)setup;

    
    if (data && td_data) {
        td_data->link   = (uint32_t)(uintptr_t)td_status | TD_LINK_DEPTH;
        td_data->status = TD_STATUS_ACTIVE | TD_C_ERR(3) | (in ? TD_STATUS_SPD : 0);
        td_data->token  = TD_TOKEN(in ? TD_PID_IN : TD_PID_OUT, addr, 0, 1,
                                   setup->wLength ? setup->wLength : 8);
        td_data->buf    = (uint32_t)(uintptr_t)data;
    }

    
    td_status->link   = TD_LINK_TERMINATE;
    td_status->status = TD_STATUS_ACTIVE | TD_C_ERR(3) | TD_STATUS_IOC;
    td_status->token  = TD_TOKEN(in ? TD_PID_OUT : TD_PID_IN, addr, 0, 1, 0);
    td_status->buf    = 0;

    
    qh->qhlp = QH_LP_TERMINATE;
    qh->qelp = (uint32_t)(uintptr_t)td_setup;

    
    uint32_t old = g_fl[0];
    g_fl[0] = (uint32_t)(uintptr_t)qh | QH_LP_QH;

    bool ok = td_wait(td_status, 200);

    g_fl[0] = old;
    return ok;
}




static bool port_reset(uint16_t port_reg_off) {
    
    outw(g_ubase + port_reg_off, PORTSC_RESET);
    uint32_t t;
    for (t = 0; t < 50000; t++) __asm__ volatile("pause");
    outw(g_ubase + port_reg_off, 0);
    for (t = 0; t < 10000; t++) __asm__ volatile("pause");

    
    outw(g_ubase + port_reg_off, PORTSC_PED);
    for (t = 0; t < 20000; t++) __asm__ volatile("pause");

    uint16_t ps = inw(g_ubase + port_reg_off);
    
    outw(g_ubase + port_reg_off, (uint16_t)(ps | PORTSC_CSC | PORTSC_PEDC));
    return (ps & PORTSC_CCS) && (ps & PORTSC_PED);
}

static bool enumerate_hid(uint8_t port_num) {
    uint16_t preg = (port_num == 0) ? PORTSC1 : PORTSC2;

    if (!port_reset(preg)) return false;

    
    uint8_t dev_desc[18];
    memset(dev_desc, 0, sizeof(dev_desc));
    usb_setup_t s = {0x80, REQ_GET_DESCRIPTOR, 0x0100, 0, 8};
    if (!uhci_control(0, &s, dev_desc, true)) return false;

    
    uint8_t new_addr = port_num + 1;
    usb_setup_t s2 = {0x00, REQ_SET_ADDRESS, new_addr, 0, 0};
    if (!uhci_control(0, &s2, 0, true)) return false;

    
    uint32_t t;
    for (t = 0; t < 5000; t++) __asm__ volatile("pause");

    
    usb_setup_t s3 = {0x80, REQ_GET_DESCRIPTOR, 0x0100, 0, 18};
    if (!uhci_control(new_addr, &s3, dev_desc, true)) return false;

    
    uint8_t cfg[64];
    memset(cfg, 0, sizeof(cfg));
    usb_setup_t s4 = {0x80, REQ_GET_DESCRIPTOR, 0x0200, 0, 9};
    if (!uhci_control(new_addr, &s4, cfg, true)) return false;

    uint16_t total_len = cfg[2] | ((uint16_t)cfg[3] << 8);
    if (total_len > 64) total_len = 64;
    usb_setup_t s4b = {0x80, REQ_GET_DESCRIPTOR, 0x0200, 0, total_len};
    if (!uhci_control(new_addr, &s4b, cfg, true)) return false;

    
    bool is_kbd = false;
    uint8_t ep_addr = 1;
    uint8_t ep_maxpkt = 8;
    uint32_t i;
    for (i = 0; i < total_len;) {
        uint8_t dlen  = cfg[i];
        uint8_t dtype = cfg[i+1];
        if (dlen == 0) break;
        if (dtype == DESC_INTERFACE) {
            if (cfg[i+5]==0x03 && cfg[i+6]==0x01 && cfg[i+7]==0x01) is_kbd = true;
        }
        if (dtype == DESC_ENDPOINT && is_kbd) {
            ep_addr   = cfg[i+2] & 0x0F;
            ep_maxpkt = cfg[i+4];
            break;
        }
        i += dlen;
    }
    if (!is_kbd) return false;

    
    usb_setup_t s5 = {0x00, REQ_SET_CONFIGURATION, 1, 0, 0};
    if (!uhci_control(new_addr, &s5, 0, true)) return false;

    
    usb_setup_t s6 = {0x21, REQ_SET_PROTOCOL, HID_BOOT_PROTOCOL, 0, 0};
    uhci_control(new_addr, &s6, 0, true);

    
    usb_setup_t s7 = {0x21, REQ_SET_IDLE, 0, 0, 0};
    uhci_control(new_addr, &s7, 0, true);

    g_kbd_addr  = new_addr;
    g_kbd_endp  = ep_addr;
    g_kbd_maxpkt = ep_maxpkt > 8 ? 8 : ep_maxpkt;
    g_kbd_toggle = 0;
    g_kbd = true;
    return true;
}


static const char hid_to_ascii[256] = {
    0,0,0,0,
    'a','b','c','d','e','f','g','h','i','j','k','l','m',
    'n','o','p','q','r','s','t','u','v','w','x','y','z',
    '1','2','3','4','5','6','7','8','9','0',
    '\n',27,'\b','\t',' ','-','=','[',']','\\',0,';','\'','`',
    ',','.','/',0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,  
    0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
static const char hid_to_ascii_shift[256] = {
    0,0,0,0,
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    '!','@','#','$','%','^','&','*','(',')','_','+','{','}','|',
    '\n',27,'\b','\t',' ','_','+','{','}','|',0,':','"','~',
    '<','>','?',0,
};



bool usb_ctrl_req(uint8_t addr, uint8_t bmRT, uint8_t req,
                  uint16_t wVal, uint16_t wIdx,
                  void *data, uint16_t dlen, bool in) {
    usb_setup_t s;
    s.bmRequestType = bmRT;
    s.bRequest      = req;
    s.wValue        = wVal;
    s.wIndex        = wIdx;
    s.wLength       = dlen;
    return uhci_control(addr, &s, dlen ? data : 0, in);
}

bool usb_bulk_out(uint8_t addr, uint8_t ep, const void *data, uint16_t len) {
    if (!g_ok || !data || len == 0) return false;
    
    g_td_idx = 0; g_qh_idx = 0;
    uhci_td_t *td = alloc_td();
    if (!td) return false;

    td->link   = TD_LINK_TERMINATE;
    td->status = TD_STATUS_ACTIVE | TD_C_ERR(3);
    td->token  = TD_TOKEN(TD_PID_OUT, addr, ep, 0, len > 64 ? 64 : len);
    td->buf    = (uint32_t)(uintptr_t)data;

    uint16_t fn = inw(g_ubase + FRNUM) & 0x3FF;
    uint32_t old = g_fl[fn];
    g_fl[fn] = (uint32_t)(uintptr_t)td;
    bool ok = td_wait(td, 100);
    g_fl[fn] = old;
    return ok;
}

bool usb_bulk_in(uint8_t addr, uint8_t ep, void *data, uint16_t max, uint16_t *got) {
    if (!g_ok || !data) return false;
    g_td_idx = 0; g_qh_idx = 0;
    uhci_td_t *td = alloc_td();
    if (!td) return false;

    memset(data, 0, max > 64 ? 64 : max);
    td->link   = TD_LINK_TERMINATE;
    td->status = TD_STATUS_ACTIVE | TD_C_ERR(3) | TD_STATUS_SPD;
    td->token  = TD_TOKEN(TD_PID_IN, addr, ep, 0, max > 64 ? 64 : max);
    td->buf    = (uint32_t)(uintptr_t)data;

    uint16_t fn = inw(g_ubase + FRNUM) & 0x3FF;
    uint32_t old = g_fl[fn];
    g_fl[fn] = (uint32_t)(uintptr_t)td;
    bool ok = td_wait(td, 50);
    g_fl[fn] = old;
    if (ok && got) *got = (uint16_t)((td->status & 0x7FF) + 1);
    return ok;
}

void usb_hid_poll(void) {
    if (!g_ok || !g_kbd) return;

    uhci_td_t *td = alloc_td();
    if (!td) { g_td_idx = 0; td = alloc_td(); }  

    memset(&g_kbd_report, 0, sizeof(g_kbd_report));
    td->link   = TD_LINK_TERMINATE;
    td->status = TD_STATUS_ACTIVE | TD_C_ERR(3) | TD_STATUS_SPD;
    td->token  = TD_TOKEN(TD_PID_IN, g_kbd_addr, g_kbd_endp,
                          g_kbd_toggle, g_kbd_maxpkt);
    td->buf    = (uint32_t)(uintptr_t)&g_kbd_report;

    
    uint16_t fn = inw(g_ubase + FRNUM) & 0x3FF;
    uint32_t old = g_fl[fn];
    g_fl[fn] = (uint32_t)(uintptr_t)td;

    if (!td_wait(td, 5)) {
        g_fl[fn] = old;
        return;
    }
    g_fl[fn] = old;
    g_kbd_toggle ^= 1;

    
    uint8_t mod = g_kbd_report.modifiers;
    bool shift = (mod & 0x22) != 0;  
    bool ctrl  = (mod & 0x11) != 0;

    int k;
    for (k = 0; k < 6; k++) {
        uint8_t key = g_kbd_report.keys[k];
        if (key == 0 || key == 1) continue;
        
        bool already = false;
        int j;
        for (j = 0; j < 6; j++)
            if (g_kbd_prev.keys[j] == key) { already = true; break; }
        if (already) continue;

        char c = shift ? hid_to_ascii_shift[key] : hid_to_ascii[key];
        if (!c) continue;
        if (ctrl && c >= 'a' && c <= 'z') c = c - 'a' + 1;
        keyboard_inject(c);
    }
    memcpy(&g_kbd_prev, &g_kbd_report, sizeof(g_kbd_report));
}


void usb_hid_init(void) {
    
    uint8_t bus, slot;
    for (bus = 0; bus < 8; bus++) {
        for (slot = 0; slot < 32; slot++) {
            uint16_t vid = pci_read16(bus, slot, 0, PCI_VENDOR_ID);
            if (vid == 0xFFFF) continue;
            uint32_t cc = pci_read32(bus, slot, 0, 0x08);
            uint8_t cls = (uint8_t)(cc >> 24);
            uint8_t sub = (uint8_t)(cc >> 16);
            uint8_t prg = (uint8_t)(cc >> 8);
            if (cls != 0x0C || sub != 0x03 || prg != 0x00) continue;

            
            uint16_t cmd = pci_read16(bus, slot, 0, PCI_COMMAND);
            pci_write16(bus, slot, 0, PCI_COMMAND,
                        (uint16_t)(cmd | PCI_CMD_IO_SPACE | PCI_CMD_BUS_MASTER));

            
            g_ubase = (uint16_t)(pci_read32(bus, slot, 0, 0x20) & 0xFFFC);
            if (!g_ubase) continue;

            
            pci_write16(bus, slot, 0, 0xC0, 0x2000);

            
            outw(g_ubase + USBCMD, CMD_HCRESET);
            uint32_t t;
            for (t = 0; t < 50000 && (inw(g_ubase+USBCMD) & CMD_HCRESET); t++)
                __asm__ volatile("pause");

            
            g_fl = (uint32_t *)kmalloc(4096 + 4096);
            if (!g_fl) return;
            uint32_t fl_phys = ((uint32_t)(uintptr_t)g_fl + 4095u) & ~4095u;
            g_fl = (uint32_t *)(uintptr_t)fl_phys;
            int i;
            for (i = 0; i < 1024; i++) g_fl[i] = TD_LINK_TERMINATE;

            outl(g_ubase + FLBASEADD, fl_phys);
            outw(g_ubase + FRNUM, 0);
            outw(g_ubase + USBINTR, 0);
            outb(g_ubase + SOFMOT, 0x40); 
            outw(g_ubase + USBCMD, CMD_RS | CMD_CF | CMD_MAXP);

            g_ok = true;

            
            enumerate_hid(0);
            if (!g_kbd) enumerate_hid(1);

            return;
        }
    }
}

bool usb_kbd_available(void)   { return g_kbd; }
bool usb_mouse_available(void) { return g_mouse; }
