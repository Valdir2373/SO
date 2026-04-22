

#include <drivers/rtl8188eu.h>
#include <drivers/pci.h>
#include <drivers/usb_hid.h>
#include <net/wifi.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <io.h>
#include <types.h>


#define RTL_VID         0x0BDA
static const uint16_t RTL_PIDS[] = {
    0x8179, 0x0179, 0x817F, 0x8189, 0x8199, 0xA811, 0
};


#define REG_SYS_FUNC_EN      0x0002
#define REG_APS_FSMCO        0x0004
#define REG_SYS_CLKR         0x0008
#define REG_AFE_MISC         0x0010
#define REG_SPS0_CTRL        0x0011
#define REG_SPS_OCP_CFG      0x0018
#define REG_RSV_CTRL         0x001C
#define REG_RF_CTRL          0x001F
#define REG_LDOA15_CTRL      0x0020
#define REG_LDOV12D_CTRL     0x0021
#define REG_LDOHCI12_CTRL    0x0022
#define REG_LPLDO_CTRL       0x0023
#define REG_AFE_XTAL_CTRL    0x0024
#define REG_SYS_ISO_CTRL     0x0001
#define REG_AFE_PLL_CTRL     0x0028
#define REG_EFUSE_CTRL       0x0030
#define REG_EFUSE_TEST       0x0034
#define REG_PWR_DATA         0x0038
#define REG_CAL_TIMER        0x003C
#define REG_ACLK_MON         0x003E
#define REG_GPIO_MUXCFG      0x0040
#define REG_GPIO_IO_SEL      0x0042
#define REG_MAC_PINMUX_CFG   0x0043
#define REG_GPIO_PIN_CTRL    0x0044
#define REG_GPIO_INTM        0x0048
#define REG_LEDCFG0          0x004C
#define REG_LEDCFG1          0x004D
#define REG_LEDCFG2          0x004E
#define REG_LEDCFG3          0x004F
#define REG_FSIMR            0x0050
#define REG_FSISR            0x0054
#define REG_HSIMR            0x0058
#define REG_HSISR            0x005C
#define REG_MCUFWDL          0x0080
#define REG_HMEBOX_EXT       0x01F0
#define REG_HMEBOX_EXT2      0x01F4
#define REG_HMEBOX_EXT3      0x01F8
#define REG_BIST_SCAN        0x0090
#define REG_BIST_RPT         0x0094
#define REG_BIST_ROM_RPT     0x0098
#define REG_USB_SIE_INTF     0x009C
#define REG_PCIE_MIO_INTF    0x00A0
#define REG_PCIE_MIO_INTD    0x00A4
#define REG_HPON_FSM         0x00B4
#define REG_SYS_CFG          0x00F0
#define REG_CR               0x0100
#define REG_PBP              0x0104
#define REG_TRXDMA_CTRL      0x010C
#define REG_TRXFF_BNDY       0x0114
#define REG_TRXFF_STATUS     0x0118
#define REG_RXFF_PTR         0x011C
#define REG_CPWM             0x012F
#define REG_FWIMR            0x0130
#define REG_FWISR            0x0134
#define REG_PKTBUF_DBG_CTRL  0x0140
#define REG_PKTBUF_DBG_DATA0 0x0144
#define REG_PKTBUF_DBG_DATA1 0x0148
#define REG_TC0_CTRL         0x0150
#define REG_TC1_CTRL         0x0154
#define REG_TCUNIT_BASE      0x0158
#define REG_MBIST_START      0x0174
#define REG_MBIST_DONE       0x0178
#define REG_MBIST_FAIL       0x017C
#define REG_32K_CTRL         0x0194
#define REG_C2HEVT_MSG_NORMAL 0x01A0
#define REG_C2HEVT_CLEAR     0x01AF
#define REG_HIMR             0x01B0
#define REG_HISR             0x01B4
#define REG_HIMRE            0x01B8
#define REG_HISRE            0x01BC
#define REG_CPWM2            0x01FC
#define REG_BCNTCFG          0x0510
#define REG_RQPN             0x0200
#define REG_FIFOPAGE         0x0204
#define REG_TDECTRL          0x0208
#define REG_TXDMA_OFFSET_CHK 0x020C
#define REG_TXDMA_STATUS     0x0210
#define REG_RQPN_NPQ         0x0214
#define REG_HI0Q_TXBD_DESA  0x0220
#define REG_HI1Q_TXBD_DESA  0x0228
#define REG_HI2Q_TXBD_DESA  0x0230
#define REG_HI3Q_TXBD_DESA  0x0238
#define REG_HI4Q_TXBD_DESA  0x0240
#define REG_HI5Q_TXBD_DESA  0x0248
#define REG_HI6Q_TXBD_DESA  0x0250
#define REG_HI7Q_TXBD_DESA  0x0258
#define REG_MGQ_TXBD_DESA   0x0260
#define REG_VOQ_TXBD_DESA   0x0268
#define REG_VIQ_TXBD_DESA   0x0270
#define REG_BEQ_TXBD_DESA   0x0278
#define REG_BKQ_TXBD_DESA   0x0280
#define REG_RXQ_RXBD_DESA   0x0288
#define REG_HI0Q_TXBD_NUM   0x02A0
#define REG_RCVDMA_INITRP    0x02A8
#define REG_TXPKT_EMPTY      0x041A
#define REG_INT_MIG          0x0304
#define REG_BCNQ_DESA        0x0308
#define REG_HW_QUEUE_CTRL    0x031C
#define REG_AUTO_LLT         0x0320
#define REG_DWBCN1_CTRL      0x0228
#define REG_TXRPT_CTRL       0x04EC
#define REG_GT_LUT_BASE      0x0800
#define REG_MACID            0x0610
#define REG_BSSID            0x0618
#define REG_MAR              0x0620
#define REG_MBIDCAMCFG       0x0628
#define REG_USTIME_EDCA      0x0638
#define REG_MAC_SPEC_SIFS    0x063A
#define REG_RESP_SIFS_OFDM   0x063E
#define REG_ACKTO            0x0640
#define REG_CTS2SELF_ATIMWND_1 0x0641
#define REG_SYNTH_DELAY      0x0644
#define REG_ACK_TIMEOUT      0x0645
#define REG_BCN_MAX_ERR      0x0647
#define REG_RXERR_RPT        0x0664
#define REG_WMAC_TRXPTCL_CTL 0x0668
#define REG_TX_ANTSEL        0x066A
#define REG_SIFS_CTX_TRX     0x067C
#define REG_TBTT_PROHIBIT    0x0540
#define REG_RD_CTRL          0x0524
#define REG_BCN_CTRL         0x0550
#define REG_MBSSID_BCN_SPACE 0x0554
#define REG_DRVERLYINT       0x0558
#define REG_BCNDMATIM        0x055A
#define REG_ATIMWND          0x055C
#define REG_BCN_MAX_ERR2     0x055E
#define REG_USTIME_TSF       0x0560
#define REG_BCN_INTERVAL     0x0554
#define REG_ATIMWND_2        0x0558
#define REG_TSFTR            0x0564
#define REG_INIT_TSFTR       0x0564
#define REG_PSTIMER          0x0580
#define REG_TIMER0           0x0584
#define REG_TIMER1           0x0588
#define REG_ACMHWCTRL        0x05C0
#define REG_ACMRSTCTRL       0x05C1
#define REG_ACMAVG           0x05C2
#define REG_VO_ADMTIME       0x05C4
#define REG_VI_ADMTIME       0x05C6
#define REG_BE_ADMTIME       0x05C8
#define REG_EDCA_VO_PARAM    0x0500
#define REG_EDCA_VI_PARAM    0x0504
#define REG_EDCA_BE_PARAM    0x0508
#define REG_EDCA_BK_PARAM    0x050C
#define REG_BCNTCFG2         0x0512
#define REG_SLOT             0x05E0
#define REG_FWHW_TXQ_CTRL    0x0422
#define REG_HWSEQ_CTRL       0x04FE
#define REG_TXPAUSE          0x0522
#define REG_DIS_TXREQ_CLR    0x0523
#define REG_RC_DELAY         0x04D0
#define REG_HMEBOX_DBG_SEL   0x01F4
#define REG_OFDM0_TXPSEUDO   0x0C04
#define REG_OFDM0_TRXPATHENA 0x0C04
#define REG_RCR              0x0608


#define MCUFWDL_EN       0x01
#define MCUFWDL_RDY      0x02
#define MCUFWDL_CHKSUM_RPT 0x04
#define WINTINI_RDY      0x04
#define FWDL_CHKSUM_RPT  0x04


#define RTL_REQ_READ   0x05
#define RTL_REQ_WRITE  0x05

extern bool usb_bulk_out(uint8_t addr, uint8_t ep, const void *data, uint16_t len);
extern bool usb_bulk_in (uint8_t addr, uint8_t ep, void *data, uint16_t len, uint16_t *got);
extern bool usb_ctrl_req(uint8_t addr, uint8_t bmRT, uint8_t req,
                         uint16_t wVal, uint16_t wIdx,
                         void *data, uint16_t len, bool in);


static bool    g_present = false;
static uint8_t g_addr    = 0;    
static uint8_t g_rx_ep   = 1;    
static uint8_t g_tx_ep   = 2;    
static uint8_t g_mac[6]  = {0};
static uint8_t g_channel = 1;


#define RX_BUF_SIZE  2048
static uint8_t g_rxbuf[RX_BUF_SIZE];



static uint8_t rtl_r8(uint16_t reg) {
    uint8_t v = 0;
    usb_ctrl_req(g_addr, 0xC0, RTL_REQ_READ, 0, reg, &v, 1, true);
    return v;
}
static uint16_t rtl_r16(uint16_t reg) {
    uint16_t v = 0;
    usb_ctrl_req(g_addr, 0xC0, RTL_REQ_READ, 0, reg, &v, 2, true);
    return v;
}
static uint32_t rtl_r32(uint16_t reg) {
    uint32_t v = 0;
    usb_ctrl_req(g_addr, 0xC0, RTL_REQ_READ, 0, reg, &v, 4, true);
    return v;
}
static void rtl_w8(uint16_t reg, uint8_t v) {
    usb_ctrl_req(g_addr, 0x40, RTL_REQ_WRITE, v, reg, 0, 0, false);
}
static void rtl_w16(uint16_t reg, uint16_t v) {
    usb_ctrl_req(g_addr, 0x40, RTL_REQ_WRITE, v, reg, &v, 2, false);
}
static void rtl_w32(uint16_t reg, uint32_t v) {
    usb_ctrl_req(g_addr, 0x40, RTL_REQ_WRITE, (uint16_t)v, reg, &v, 4, false);
}
static void rtl_set8(uint16_t reg, uint8_t mask) {
    rtl_w8(reg, (uint8_t)(rtl_r8(reg)|mask));
}
static void rtl_clr8(uint16_t reg, uint8_t mask) {
    rtl_w8(reg, (uint8_t)(rtl_r8(reg)&(uint8_t)~mask));
}



#define FW_PAGE_SIZE   256
#define FW_LOAD_ADDR   0x0200

static bool rtl_fw_download(const uint8_t *fw, uint32_t fwlen) {
    
    rtl_set8(REG_MCUFWDL, MCUFWDL_EN);
    rtl_clr8(REG_MCUFWDL, 0x80);

    
    rtl_w8(REG_HMEBOX_EXT+3, 0);
    rtl_clr8(REG_SYS_FUNC_EN, 0x80);
    rtl_set8(REG_SYS_FUNC_EN, 0x80);

    
    uint32_t i;
    for (i = 0; i < fwlen; i += FW_PAGE_SIZE) {
        uint32_t chunk = fwlen - i;
        if (chunk > FW_PAGE_SIZE) chunk = FW_PAGE_SIZE;
        
        rtl_w8(REG_MCUFWDL+2, (uint8_t)(i / FW_PAGE_SIZE));
        
        usb_ctrl_req(g_addr, 0x40, 0x05,
                     0, (uint16_t)(FW_LOAD_ADDR + (i % (FW_PAGE_SIZE * 4))),
                     (void*)(fw + i), (uint16_t)chunk, false);
    }

    
    rtl_clr8(REG_MCUFWDL, MCUFWDL_EN);
    rtl_set8(REG_MCUFWDL, 0x02);  

    
    uint32_t t;
    for (t = 0; t < 1000000; t++) {
        if (rtl_r8(REG_MCUFWDL) & WINTINI_RDY) return true;
        __asm__ volatile("pause");
    }
    return false;
}



static void rtl_power_on(void) {
    
    rtl_w8(REG_LDOA15_CTRL,   0x05);
    rtl_w8(REG_LDOV12D_CTRL,  0x21);
    rtl_w8(REG_AFE_MISC,      0xE4);
    rtl_w8(REG_SPS0_CTRL,     0x2B);

    
    rtl_w8(REG_AFE_XTAL_CTRL+1, 0x80);

    
    rtl_w32(REG_AFE_PLL_CTRL, 0xF140AA35U);

    
    uint32_t t; for(t=0;t<100000;t++) __asm__ volatile("pause");

    
    rtl_w8(REG_SYS_ISO_CTRL, 0x00);

    
    rtl_w16(REG_SYS_CLKR, 0x70A3U);

    
    rtl_w16(REG_SYS_FUNC_EN, 0xFD);

    
    rtl_w8(REG_APS_FSMCO+1, 0x08);
    rtl_w8(REG_HPON_FSM, 0x00);
}



static void rtl_mac_init(void) {
    
    rtl_w32(REG_EDCA_BE_PARAM, 0x005EA42B);
    rtl_w32(REG_EDCA_BK_PARAM, 0x0000A44F);
    rtl_w32(REG_EDCA_VI_PARAM, 0x005EA324);
    rtl_w32(REG_EDCA_VO_PARAM, 0x002FA226);

    
    rtl_w8(REG_CR, 0xFF);
    rtl_w16(REG_TRXDMA_CTRL, 0xF771);

    
    rtl_w32(REG_RCR, 0x7000228FU);

    
    rtl_w32(REG_MACID,   ((uint32_t)g_mac[0])|((uint32_t)g_mac[1]<<8)|
                          ((uint32_t)g_mac[2]<<16)|((uint32_t)g_mac[3]<<24));
    rtl_w16(REG_MACID+4, ((uint16_t)g_mac[4])|((uint16_t)g_mac[5]<<8));

    
    rtl_w32(REG_TRXFF_BNDY, 0x27FF007CU);

    
    rtl_w32(REG_RQPN, 0x808E000DU);

    
    rtl_w8(REG_FWHW_TXQ_CTRL, 0x80);
    rtl_w8(REG_FWHW_TXQ_CTRL+1, 0x1F);
    rtl_w8(REG_FWHW_TXQ_CTRL+2, 0x10);

    
    rtl_w16(REG_HWSEQ_CTRL, 0x01FF);
}



static void rtl_read_mac(void) {
    
    int i;
    for (i = 0; i < 6; i++)
        g_mac[i] = rtl_r8((uint16_t)(REG_MACID + i));
    
    bool valid = false;
    for (i = 0; i < 6; i++) if (g_mac[i] && g_mac[i] != 0xFF) { valid = true; break; }
    if (!valid) {
        g_mac[0]=0x00; g_mac[1]=0x11; g_mac[2]=0x22;
        g_mac[3]=0x33; g_mac[4]=0x44; g_mac[5]=0x55;
    }
}




static const uint32_t rf_ch_val[15] = {
    0, 
    0x23A, 0x23B, 0x23C, 0x23D, 0x23E, 0x23F, 0x240,
    0x241, 0x242, 0x243, 0x244, 0x245, 0x246, 0x247
};

static void rtl_rf_write(uint8_t path, uint8_t addr, uint32_t val) {
    
    uint32_t data = ((uint32_t)addr << 20) | (val & 0xFFFFF);
    rtl_w32(0x082C, 0x80000000U | ((uint32_t)path << 28) | data);
    uint32_t t; for(t=0;t<1000;t++) __asm__ volatile("pause");
}

void rtl8188eu_set_channel(uint8_t ch) {
    if (ch < 1 || ch > 14) ch = 1;
    g_channel = ch;
    
    rtl_rf_write(0, 0x18, rf_ch_val[ch]);
    
    uint32_t t; for(t=0;t<50000;t++) __asm__ volatile("pause");
}




typedef struct __attribute__((packed)) {
    uint16_t pkt_size;
    uint8_t  offset;         
    uint8_t  bmc;            
    uint16_t txpktbuf_offset;
    uint8_t  queue_sel;      
    uint8_t  rate_id;
    uint16_t seq;
    uint16_t rsvd1;
    uint32_t txdw2;
    uint32_t txdw3;
    uint32_t txdw4;
    uint32_t txdw5;
    uint32_t txdw6;
    uint32_t txdw7;
    uint32_t txdw8;
    uint32_t txdw9;
} rtl8188eu_txdesc_t;

void rtl8188eu_send_frame(const uint8_t *frame, uint16_t len) {
    if (!g_present || len == 0) return;
    uint8_t buf[2048];
    if (len + 40 > (uint16_t)sizeof(buf)) return;

    memset(buf, 0, 40);
    rtl8188eu_txdesc_t *tx = (rtl8188eu_txdesc_t *)buf;
    tx->pkt_size   = len;
    tx->offset     = 40;
    tx->queue_sel  = 0x12;  
    tx->txdw4      = 0x00000004U;  
    tx->txdw5      = 0x00000001U;  
    memcpy(buf + 40, frame, len);

    usb_bulk_out(g_addr, g_tx_ep, buf, (uint16_t)(40 + len));
}



void rtl8188eu_poll(void) {
    if (!g_present) return;
    uint16_t got = 0;
    if (!usb_bulk_in(g_addr, g_rx_ep, g_rxbuf, sizeof(g_rxbuf), &got)) return;
    if (got < 24) return;

    
    uint16_t rx_desc_len = 24;
    if (got <= rx_desc_len) return;

    
    const uint8_t *frame = g_rxbuf + rx_desc_len;
    uint16_t flen = (uint16_t)(got - rx_desc_len);

    wifi_rx_frame(frame, flen);
}



void rtl8188eu_scan(void) {
    if (!g_present) return;
    uint8_t ch;
    for (ch = 1; ch <= 13; ch++) {
        rtl8188eu_set_channel(ch);
        
        uint8_t probe[64];
        memset(probe, 0, 64);
        probe[0] = 0x40; probe[1] = 0x00; 
        probe[2] = 0x00; probe[3] = 0x00; 
        memset(probe+4,  0xFF, 6);  
        memcpy(probe+10, g_mac, 6); 
        memset(probe+16, 0xFF, 6);  
        probe[22]=0x00; probe[23]=0x00;
        
        probe[24]=0x00; probe[25]=0x00;
        
        probe[26]=0x01; probe[27]=0x08;
        probe[28]=0x82; probe[29]=0x84; probe[30]=0x8B; probe[31]=0x96;
        probe[32]=0x24; probe[33]=0x30; probe[34]=0x48; probe[35]=0x6C;
        rtl8188eu_send_frame(probe, 36);

        
        uint32_t t; for(t=0;t<100000;t++) {
            rtl8188eu_poll();
            __asm__ volatile("pause");
        }
    }
}



static bool rtl_find_device(void) {
    
    uint8_t addr;
    for (addr = 1; addr <= 4; addr++) {
        uint32_t cfg = 0xFFFFFFFF;
        if (!usb_ctrl_req(addr, 0xC0, RTL_REQ_READ, 0, REG_SYS_CFG, &cfg, 4, true))
            continue;
        if (cfg == 0xFFFFFFFF || cfg == 0) continue;
        
        g_addr = addr;
        return true;
    }
    return false;
}



bool rtl8188eu_init(void) {
    if (!rtl_find_device()) return false;

    
    vfs_node_t *fw_node = vfs_resolve("/rtl8188eu.bin");
    if (!fw_node) {
        
        fw_node = vfs_resolve("/fw/rtl8188eu.bin");
    }

    if (!fw_node) {
        
        g_present = false;
        return false;
    }

    uint8_t *fw = (uint8_t *)kmalloc(fw_node->size);
    if (!fw) return false;
    vfs_read(fw_node, 0, fw_node->size, fw);

    rtl_power_on();

    
    rtl_read_mac();

    
    extern uint8_t net_mac[6];
    memcpy(net_mac, g_mac, 6);

    bool fw_ok = rtl_fw_download(fw, fw_node->size);
    kfree(fw);

    if (!fw_ok) {
        g_present = false;
        return false;
    }

    rtl_mac_init();
    g_present = true;

    
    rtl8188eu_set_channel(6);

    return true;
}

bool rtl8188eu_present(void) { return g_present; }
void rtl8188eu_get_mac(uint8_t buf[6]) { memcpy(buf, g_mac, 6); }


void wifi_driver_send(const uint8_t *frame, uint16_t len) {
    rtl8188eu_send_frame(frame, len);
}
