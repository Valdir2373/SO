/*
 * apps/network_manager.c — Gerenciador de rede
 * Mostra IP, MAC, gateway, DNS e status da conexao.
 */

#include <apps/network_manager.h>
#include <gui/window.h>
#include <gui/canvas.h>
#include <drivers/framebuffer.h>
#include <drivers/e1000.h>
#include <net/net.h>
#include <net/netif.h>
#include <lib/string.h>
#include <types.h>

static window_t *nm_win = NULL;

static void ip_to_str(uint32_t ip, char *out) {
    /* IP está em ordem de rede (little-endian no campo = network byte order) */
    uint8_t *b = (uint8_t *)&ip;
    /* Para IP4(a,b,c,d) = a | b<<8 | c<<16 | d<<24, b[0]=a, b[1]=b, ... */
    char tmp[4];
    int pos = 0;
    int i;
    for (i = 0; i < 4; i++) {
        uint8_t v = b[i];
        if (v >= 100) { out[pos++] = (char)('0' + v/100); }
        if (v >= 10)  { out[pos++] = (char)('0' + (v/10)%10); }
        out[pos++] = (char)('0' + v%10);
        if (i < 3) out[pos++] = '.';
        (void)tmp;
    }
    out[pos] = '\0';
}

static void mac_to_str(const uint8_t *mac, char *out) {
    const char *hex = "0123456789ABCDEF";
    int i;
    for (i = 0; i < 6; i++) {
        out[i*3+0] = hex[(mac[i] >> 4) & 0xF];
        out[i*3+1] = hex[mac[i] & 0xF];
        out[i*3+2] = (i < 5) ? ':' : '\0';
    }
    out[17] = '\0';
}

static void nm_on_paint(window_t *win) {
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);
    int bx = win->content_x, by = win->content_y;
    int w = win->content_w;

    canvas_fill_rect(bx, by, w, win->content_h, 0x001E272E);

    /* Cabeçalho */
    canvas_fill_gradient(bx, by, w, 50, 0x00003580, 0x001E272E);
    canvas_draw_string(bx+w/2-56, by+8, "Rede / Networking", 0x0074B9FF, COLOR_TRANSPARENT);

    /* Status */
    bool up = netif_is_up();
    uint32_t status_col = up ? 0x0000B894 : 0x00D63031;
    canvas_draw_string(bx+w/2-32, by+28, up ? "CONECTADO" : "DESCONECTADO",
                       status_col, COLOR_TRANSPARENT);

    int y = by + 64;
    int lx = bx + 16;
    uint32_t lbl = 0x0074B9FF;
    uint32_t val = 0x00DFE6E9;
    int vx = lx + 112;

    /* MAC */
    char mac_str[20];
    if (e1000_ready()) {
        mac_to_str(net_mac, mac_str);
    } else {
        memcpy(mac_str, "N/A", 4);
    }
    canvas_draw_string(lx, y, "MAC:", lbl, COLOR_TRANSPARENT);
    canvas_draw_string(vx, y, mac_str, val, COLOR_TRANSPARENT); y += 22;

    /* IP */
    char ip_str[20];
    if (net_ip) ip_to_str(net_ip, ip_str);
    else memcpy(ip_str, "0.0.0.0", 8);
    canvas_draw_string(lx, y, "IP:", lbl, COLOR_TRANSPARENT);
    canvas_draw_string(vx, y, ip_str, val, COLOR_TRANSPARENT); y += 22;

    /* Mascara */
    char mask_str[20];
    if (net_mask) ip_to_str(net_mask, mask_str);
    else memcpy(mask_str, "0.0.0.0", 8);
    canvas_draw_string(lx, y, "Mascara:", lbl, COLOR_TRANSPARENT);
    canvas_draw_string(vx, y, mask_str, val, COLOR_TRANSPARENT); y += 22;

    /* Gateway */
    char gw_str[20];
    if (net_gateway) ip_to_str(net_gateway, gw_str);
    else memcpy(gw_str, "0.0.0.0", 8);
    canvas_draw_string(lx, y, "Gateway:", lbl, COLOR_TRANSPARENT);
    canvas_draw_string(vx, y, gw_str, val, COLOR_TRANSPARENT); y += 22;

    /* DNS */
    char dns_str[20];
    if (net_dns) ip_to_str(net_dns, dns_str);
    else memcpy(dns_str, "0.0.0.0", 8);
    canvas_draw_string(lx, y, "DNS:", lbl, COLOR_TRANSPARENT);
    canvas_draw_string(vx, y, dns_str, val, COLOR_TRANSPARENT); y += 22;

    /* Driver */
    canvas_draw_string(lx, y, "Driver:", lbl, COLOR_TRANSPARENT);
    canvas_draw_string(vx, y, e1000_ready() ? "Intel e1000 (OK)" : "Sem NIC",
                       val, COLOR_TRANSPARENT); y += 22;

    /* Separador */
    y += 8;
    canvas_draw_line(bx+8, y, bx+w-8, y, 0x00636E72); y += 12;
    canvas_draw_string(lx, y, "DHCP: auto  |  IPv4  |  Ethernet II",
                       0x00636E72, COLOR_TRANSPARENT);
}

void network_manager_open(void) {
    if (nm_win && nm_win->used) { wm_focus(nm_win); return; }

    nm_win = wm_create("Gerenciador de Rede",
                        fb.width/2 - 200,
                        fb.height/2 - 180,
                        400, 360, WIN_RESIZABLE);
    if (!nm_win) return;
    nm_win->bg_color = 0x001E272E;
    nm_win->on_paint = nm_on_paint;
}
