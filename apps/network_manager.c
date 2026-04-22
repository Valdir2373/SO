


#include <apps/network_manager.h>
#include <gui/window.h>
#include <gui/canvas.h>
#include <proc/process.h>
#include <drivers/framebuffer.h>
#include <drivers/e1000.h>
#include <net/net.h>
#include <net/netif.h>
#include <net/dhcp.h>
#include <net/wifi.h>
#include <drivers/rtl8188eu.h>
#include <kernel/timer.h>
#include <lib/string.h>
#include <types.h>

static window_t *nm_win    = NULL;


static char     nm_status[80]  = "";
static bool     nm_connecting  = false;
static uint32_t nm_connect_t   = 0;


static bool     nm_pw_active   = false;    
static char     nm_pw_buf[64]  = "";
static int      nm_pw_len      = 0;
static int      nm_sel_idx     = -1;       



static void ip_to_str(uint32_t ip, char *out) {
    uint8_t *b = (uint8_t *)&ip;
    int pos = 0, i;
    for (i = 0; i < 4; i++) {
        uint8_t v = b[i];
        if (v >= 100) out[pos++] = (char)('0' + v/100);
        if (v >= 10)  out[pos++] = (char)('0' + (v/10)%10);
        out[pos++] = (char)('0' + v%10);
        if (i < 3) out[pos++] = '.';
    }
    out[pos] = '\0';
}

static void mac_to_str(const uint8_t *mac, char *out) {
    const char *hex = "0123456789ABCDEF";
    int i;
    for (i = 0; i < 6; i++) {
        out[i*3+0] = hex[(mac[i]>>4)&0xF];
        out[i*3+1] = hex[mac[i]&0xF];
        out[i*3+2] = (i < 5) ? ':' : '\0';
    }
    out[17] = '\0';
}

static void draw_row(int lx, int *cy, const char *label, const char *value,
                     uint32_t lbl_col, uint32_t val_col, int vx) {
    canvas_draw_string(lx, *cy, label, lbl_col, COLOR_TRANSPARENT);
    canvas_draw_string(vx, *cy, value, val_col, COLOR_TRANSPARENT);
    *cy += 18;
}



static void nm_on_paint(window_t *win) {
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);

    int bx = win->content_x, by = win->content_y;
    int w  = win->content_w,  h  = win->content_h;

    canvas_fill_rect(bx, by, w, h, 0x001A2332);

    
    canvas_fill_gradient(bx, by, w, 36, 0x00003D7A, 0x001A2332);
    canvas_draw_string(bx + 12, by + 10, "Gerenciador de Rede", 0x0074B9FF, COLOR_TRANSPARENT);

    
    bool eth_up = netif_is_up();
    bool wif_up = wifi_is_connected();
    uint32_t badge_col = (eth_up||wif_up) ? 0x0000B894 : 0x00C0392B;
    canvas_fill_rounded_rect(bx+w-130, by+6, 120, 22, 5, badge_col);
    canvas_draw_string(bx+w-124, by+11,
                       wif_up ? "WiFi OK" : (eth_up ? "Eth OK" : "Offline"),
                       0x00FFFFFF, COLOR_TRANSPARENT);

    int cy = by + 44;
    int lx = bx + 8;
    int vx = lx + 90;
    uint32_t lbl = 0x0074B9FF, val = 0x00DFE6E9;

    
    canvas_draw_string(lx, cy, "-- Ethernet --", 0x00636E72, COLOR_TRANSPARENT);
    cy += 16;

    char mac_str[20];
    if (e1000_ready()) mac_to_str(net_mac, mac_str);
    else memcpy(mac_str, "N/A", 4);
    draw_row(lx, &cy, "MAC:", mac_str, lbl, val, vx);

    char ip_str[20];
    ip_to_str(net_ip ? net_ip : 0, ip_str);
    if (!net_ip) memcpy(ip_str, "0.0.0.0", 8);
    draw_row(lx, &cy, "IP:", ip_str, lbl, val, vx);

    char gw_str[20];
    if (net_gateway) ip_to_str(net_gateway, gw_str);
    else memcpy(gw_str, "0.0.0.0", 8);
    draw_row(lx, &cy, "Gateway:", gw_str, lbl, val, vx);

    
    int btn_w=180, btn_h=22, btn_x=bx+(w-btn_w)/2;
    uint32_t btn_col = nm_connecting ? 0x00555555 : 0x000984E3;
    canvas_fill_rounded_rect(btn_x, cy, btn_w, btn_h, 5, btn_col);
    const char *bl = nm_connecting ? "Conectando..." : "Reconectar DHCP";
    canvas_draw_string(btn_x + (btn_w-(int)strlen(bl)*8)/2, cy+6,
                       bl, 0x00FFFFFF, COLOR_TRANSPARENT);
    cy += btn_h + 6;

    if (nm_status[0]) {
        bool is_err = (nm_status[0]=='E')||(nm_status[0]=='T');
        canvas_draw_string(lx, cy, nm_status,
                           is_err ? 0x00E74C3C : 0x0000B894, COLOR_TRANSPARENT);
        cy += 16;
    }

    
    if (nm_connecting && timer_get_ticks()-nm_connect_t > 4000) {
        nm_connecting = false;
        if (net_is_configured()) memcpy(nm_status, "IP obtido!", 11);
        else memcpy(nm_status, "Timeout DHCP", 13);
    }

    
    canvas_draw_line(bx+4, cy, bx+w-4, cy, 0x00334455); cy += 10;
    canvas_draw_string(lx, cy, "-- WiFi --", 0x00636E72, COLOR_TRANSPARENT);

    bool wifi_hw = rtl8188eu_present();
    if (!wifi_hw) {
        canvas_draw_string(lx+80, cy, "(sem adaptador WiFi)", 0x00636E72, COLOR_TRANSPARENT);
        cy += 16;
        canvas_draw_string(lx, cy, "Coloque rtl8188eu.bin no pendrive", 0x00FDCB6E, COLOR_TRANSPARENT);
        return;
    }
    cy += 16;

    
    if (g_wifi_status_msg[0]) {
        canvas_draw_string(lx, cy, g_wifi_status_msg, 0x0074B9FF, COLOR_TRANSPARENT);
        cy += 16;
    }

    
    bool scanning = (g_wifi_state == WIFI_STATE_SCANNING);
    uint32_t scan_col = scanning ? 0x00555555 : 0x006C3483;
    canvas_fill_rounded_rect(lx, cy, 80, 20, 4, scan_col);
    canvas_draw_string(lx+8, cy+6, scanning ? "Scanning" : "Scan WiFi",
                       0x00FFFFFF, COLOR_TRANSPARENT);
    cy += 26;

    
    int i;
    for (i = 0; i < g_wifi_network_count && i < 8; i++) {
        wifi_network_t *n = &g_wifi_networks[i];
        bool sel = (i == nm_sel_idx);
        uint32_t row_bg = sel ? 0x002C3E50 : 0x00172330;
        canvas_fill_rect(bx+4, cy, w-8, 20, row_bg);
        if (sel) canvas_draw_rect(bx+4, cy, w-8, 20, 0x000984E3);

        
        int sig = (n->rssi + 100);  
        if (sig < 0) sig = 0; if (sig > 60) sig = 60;
        uint32_t sig_col = sig>40 ? 0x0000B894 : (sig>20 ? 0x00FDCB6E : 0x00E74C3C);
        canvas_fill_rect(bx+w-30, cy+6, (sig*24)/60, 8, sig_col);
        canvas_draw_rect(bx+w-30, cy+6, 24, 8, 0x00334455);

        
        const char *sec = "";
        if (n->security==WIFI_SEC_WPA2) sec="WPA2";
        else if (n->security==WIFI_SEC_WPA) sec="WPA";
        else if (n->security==WIFI_SEC_WEP) sec="WEP";
        canvas_draw_string(bx+w-70, cy+6, sec, 0x00FDCB6E, COLOR_TRANSPARENT);

        canvas_draw_string(lx+4, cy+6, n->ssid, sel?0x00FFFFFF:0x00DFE6E9, COLOR_TRANSPARENT);
        cy += 22;
    }

    
    if (nm_sel_idx >= 0 &&
        g_wifi_networks[nm_sel_idx].security != WIFI_SEC_OPEN) {
        canvas_draw_string(lx, cy, "Senha:", lbl, COLOR_TRANSPARENT); cy+=16;
        canvas_fill_rect(lx, cy, w-16, 22, nm_pw_active ? 0x00243850 : 0x001E2D3E);
        canvas_draw_rect(lx, cy, w-16, 22, nm_pw_active ? 0x000984E3 : 0x00334455);
        
        char stars[64]; int si;
        for(si=0;si<nm_pw_len&&si<32;si++) stars[si]='*';
        stars[si]='\0';
        canvas_draw_string(lx+4, cy+6, stars, val, COLOR_TRANSPARENT);
        if (nm_pw_active) {
            
            int cx2 = lx+4+nm_pw_len*8;
            canvas_fill_rect(cx2, cy+4, 2, 14, 0x00FFFFFF);
        }
        cy += 26;
    }

    
    if (nm_sel_idx >= 0) {
        bool connected_to = (g_wifi_connected_idx == nm_sel_idx &&
                             g_wifi_state == WIFI_STATE_CONNECTED);
        uint32_t cc = connected_to ? 0x00C0392B : 0x000984E3;
        canvas_fill_rounded_rect(lx, cy, 120, 22, 5, cc);
        canvas_draw_string(lx+8, cy+6, connected_to?"Desconectar":"Conectar",
                           0x00FFFFFF, COLOR_TRANSPARENT);
        cy += 28;
    }

    (void)h;
}



static void nm_on_click(window_t *win, int mx, int my) {
    int bx = win->content_x, by = win->content_y;
    int w  = win->content_w;

    
    int dhcp_btn_y = by + 114;
    int btn_w = 180, btn_h = 22;
    int btn_x = bx + (w - btn_w)/2;
    if (!nm_connecting &&
        mx>=btn_x && mx<=btn_x+btn_w &&
        my>=dhcp_btn_y && my<=dhcp_btn_y+btn_h) {
        if (!e1000_ready()) { memcpy(nm_status,"Sem NIC",8); return; }
        nm_connecting = true;
        nm_connect_t  = timer_get_ticks();
        nm_status[0]  = '\0';
        dhcp_request();
        return;
    }

    if (!rtl8188eu_present()) return;

    
    int scan_y = by + 158;
    if (mx >= bx+8 && mx <= bx+88 && my >= scan_y && my <= scan_y+20) {
        wifi_scan();
        rtl8188eu_scan();
        return;
    }

    
    int list_y = scan_y + 26;
    int i;
    for (i = 0; i < g_wifi_network_count && i < 8; i++) {
        int ry = list_y + i*22;
        if (mx >= bx+4 && mx <= bx+w-4 && my >= ry && my <= ry+20) {
            nm_sel_idx  = i;
            nm_pw_active= (g_wifi_networks[i].security != WIFI_SEC_OPEN);
            nm_pw_len   = 0;
            nm_pw_buf[0]= '\0';
            return;
        }
    }

    
    if (nm_sel_idx >= 0) {
        int pw_y   = list_y + g_wifi_network_count*22 + 16;
        int conn_y = pw_y + 22 + 6 + 16 + 26;
        if (g_wifi_networks[nm_sel_idx].security == WIFI_SEC_OPEN)
            conn_y = list_y + g_wifi_network_count*22;

        
        if (my >= pw_y && my <= pw_y+22) {
            nm_pw_active = true;
            return;
        }

        
        if (mx >= bx+8 && mx <= bx+128 && my >= conn_y && my <= conn_y+22) {
            bool connected_to = (g_wifi_connected_idx == nm_sel_idx &&
                                  g_wifi_state == WIFI_STATE_CONNECTED);
            if (connected_to) {
                wifi_disconnect();
            } else {
                const char *pw = (g_wifi_networks[nm_sel_idx].security==WIFI_SEC_OPEN)
                                  ? NULL : nm_pw_buf;
                
                extern uint8_t net_mac[6];
                uint8_t auth[30];
                memset(auth,0,30);
                auth[0]=0xB0; auth[1]=0x00;
                memset(auth+4,0xFF,6);
                memcpy(auth+10,net_mac,6);
                memcpy(auth+16,g_wifi_networks[nm_sel_idx].bssid,6);
                auth[22]=0; auth[23]=0;
                auth[24]=0; auth[25]=0; auth[26]=1; auth[27]=0; auth[28]=0; auth[29]=0;
                rtl8188eu_set_channel(g_wifi_networks[nm_sel_idx].channel);
                wifi_connect(nm_sel_idx, pw);
                rtl8188eu_send_frame(auth, 30);
            }
        }
    }
}



static void nm_on_key(window_t *win, char key) {
    (void)win;
    if (!nm_pw_active) return;
    if (key == '\b') {
        if (nm_pw_len > 0) nm_pw_buf[--nm_pw_len] = '\0';
    } else if (key == '\n' || key == '\r') {
        nm_pw_active = false;
    } else if (nm_pw_len < 63) {
        nm_pw_buf[nm_pw_len++] = key;
        nm_pw_buf[nm_pw_len]   = '\0';
    }
}



void network_manager_open(void) {
    if (nm_win && nm_win->used) { wm_focus(nm_win); return; }

    nm_win = wm_create("Gerenciador de Rede",
                        (int)fb.width/2  - 230,
                        (int)fb.height/2 - 280,
                        460, 560, WIN_RESIZABLE);
    if (!nm_win) return;
    nm_win->bg_color  = 0x001A2332;
    nm_win->on_paint  = nm_on_paint;
    nm_win->on_click  = nm_on_click;
    nm_win->on_keydown= nm_on_key;
    nm_status[0]      = '\0';
    nm_connecting     = false;
    nm_sel_idx        = -1;
    nm_pw_active      = false;
    nm_pw_len         = 0;
    { process_t *p = process_create_app("Rede", 48*1024);
      if (p) nm_win->proc_pid = p->pid; }
}
