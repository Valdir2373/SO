


#include <apps/image_viewer.h>
#include <gui/window.h>
#include <gui/canvas.h>
#include <gui/clipboard.h>
#include <drivers/framebuffer.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/png.h>
#include <lib/jpeg.h>
#include <proc/process.h>
#include <types.h>

#define IV_MAX_W  800
#define IV_MAX_H  600

static window_t *iv_win   = 0;
static uint32_t *iv_pixels = 0;
static int       iv_iw = 0, iv_ih = 0;  
static float     iv_zoom = 1.0f;
static int       iv_pan_x = 0, iv_pan_y = 0;
static char      iv_filename[128] = "";
static char      iv_status[64]    = "";

static void iv_free(void) {
    if (iv_pixels) { kfree(iv_pixels); iv_pixels = 0; }
    iv_iw = iv_ih = 0;
}

static bool iv_load(const char *path) {
    iv_free();
    if (!path || !path[0]) return false;

    
    int plen = (int)strlen(path);
    const char *ext = path + plen;
    while (ext > path && *ext != '.') ext--;

    bool is_png = strcmp(ext, ".png") == 0 || strcmp(ext, ".PNG") == 0;
    bool is_jpg = strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0 ||
                  strcmp(ext, ".JPG") == 0 || strcmp(ext, ".JPEG") == 0;
    if (!is_png && !is_jpg) {
        memcpy(iv_status, "Formato nao suportado (PNG/JPG)", 32);
        return false;
    }

    vfs_node_t *node = vfs_resolve(path);
    if (!node || node->size == 0) {
        memcpy(iv_status, "Arquivo nao encontrado", 23);
        return false;
    }
    if (node->size > 8 * 1024 * 1024) {
        memcpy(iv_status, "Arquivo muito grande (max 8MB)", 30);
        return false;
    }

    uint8_t *buf = (uint8_t *)kmalloc(node->size);
    if (!buf) { memcpy(iv_status, "Sem memoria", 12); return false; }
    vfs_read(node, 0, node->size, buf);

    int nw = 0, nh = 0;
    uint32_t *pix = 0;
    if (is_png)      pix = png_decode(buf, node->size, &nw, &nh);
    else if (is_jpg) pix = jpeg_decode(buf, node->size, &nw, &nh);
    kfree(buf);

    if (!pix || nw <= 0 || nh <= 0) {
        memcpy(iv_status, "Falha ao decodificar", 21);
        return false;
    }

    iv_pixels = pix;
    iv_iw = nw;
    iv_ih = nh;

    
    int cw = iv_win ? iv_win->content_w : IV_MAX_W;
    int ch = iv_win ? iv_win->content_h - 28 : IV_MAX_H - 28;
    float zx = (float)cw / (float)nw;
    float zy = (float)ch / (float)nh;
    iv_zoom = zx < zy ? zx : zy;
    if (iv_zoom > 2.0f) iv_zoom = 2.0f;
    iv_pan_x = iv_pan_y = 0;

    iv_status[0] = '\0';
    return true;
}

static void iv_on_paint(window_t *win) {
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);
    int bx = win->content_x, by = win->content_y;
    int cw = win->content_w, ch = win->content_h;

    canvas_fill_rect(bx, by, cw, ch, 0x00111111);

    if (iv_pixels && iv_iw > 0 && iv_ih > 0) {
        int dw = (int)((float)iv_iw * iv_zoom);
        int dh = (int)((float)iv_ih * iv_zoom);
        int ox = bx + (cw - dw) / 2 + iv_pan_x;
        int oy = by + 28 + (ch - 28 - dh) / 2 + iv_pan_y;
        canvas_draw_scaled_bitmap(ox, oy, dw, dh, iv_pixels, iv_iw, iv_ih);

        
        char zstr[16];
        int zp = (int)(iv_zoom * 100.0f);
        char tmp[8]; int ti = 0;
        if (zp >= 100) { tmp[ti++] = (char)('0' + zp/100); zp %= 100; }
        tmp[ti++] = (char)('0' + zp/10);
        tmp[ti++] = (char)('0' + zp%10);
        tmp[ti++] = '%'; tmp[ti] = 0;
        memcpy(zstr, tmp, (uint32_t)ti+1);
        canvas_draw_string(bx + 4, by + 6, zstr, 0x00DFE6E9, 0x00333333);
    } else {
        
        canvas_draw_string(bx + cw/2 - 80, by + ch/2 - 8,
                           "Nenhuma imagem carregada", 0x00636E72, COLOR_TRANSPARENT);
    }

    
    canvas_fill_rect(bx, by, cw, 24, 0x001E272E);

    
    int btnx = bx + 4;
    uint32_t btn_col = 0x00333344;

    canvas_fill_rounded_rect(btnx, by+2, 52, 20, 3, btn_col);
    canvas_draw_string(btnx+4, by+5, "Abrir", 0x00DFE6E9, COLOR_TRANSPARENT);
    btnx += 56;

    canvas_fill_rounded_rect(btnx, by+2, 36, 20, 3, btn_col);
    canvas_draw_string(btnx+8, by+5, "Z+", 0x00DFE6E9, COLOR_TRANSPARENT);
    btnx += 40;

    canvas_fill_rounded_rect(btnx, by+2, 36, 20, 3, btn_col);
    canvas_draw_string(btnx+8, by+5, "Z-", 0x00DFE6E9, COLOR_TRANSPARENT);
    btnx += 40;

    canvas_fill_rounded_rect(btnx, by+2, 60, 20, 3, btn_col);
    canvas_draw_string(btnx+4, by+5, "Ajustar", 0x00DFE6E9, COLOR_TRANSPARENT);

    
    if (iv_filename[0])
        canvas_draw_string(bx + cw - 200, by + 6, iv_filename, 0x0074B9FF, COLOR_TRANSPARENT);
    if (iv_status[0])
        canvas_draw_string(bx + 4, by + ch - 16, iv_status, 0x00D63031, COLOR_TRANSPARENT);
}


static char iv_path_input[128] = "";
static bool iv_path_active     = false;

static void iv_on_keydown(window_t *win, char key) {
    (void)win;
    if (iv_path_active) {
        if (key == '\r' || key == '\n') {
            iv_path_active = false;
            if (iv_path_input[0]) {
                strncpy(iv_filename, iv_path_input, 127);
                iv_load(iv_path_input);
            }
        } else if (key == 27) {
            iv_path_active = false;
        } else if ((key == '\b' || key == 127)) {
            int l = (int)strlen(iv_path_input);
            if (l > 0) iv_path_input[l-1] = '\0';
        } else if (key >= 32 && strlen(iv_path_input) < 127) {
            int l = (int)strlen(iv_path_input);
            iv_path_input[l] = key; iv_path_input[l+1] = '\0';
        }
        return;
    }

    
    if (key == '+' || key == '=') {
        iv_zoom *= 1.2f;
        if (iv_zoom > 8.0f) iv_zoom = 8.0f;
    } else if (key == '-') {
        iv_zoom /= 1.2f;
        if (iv_zoom < 0.05f) iv_zoom = 0.05f;
    } else if (key == 'f' || key == 'F') {
        
        if (iv_win && iv_iw > 0 && iv_ih > 0) {
            int cw2 = iv_win->content_w;
            int ch2 = iv_win->content_h - 28;
            float zx = (float)cw2 / (float)iv_iw;
            float zy = (float)ch2 / (float)iv_ih;
            iv_zoom = zx < zy ? zx : zy;
        }
        iv_pan_x = iv_pan_y = 0;
    } else if (key == 'o' || key == 'O') {
        
        iv_path_active = true;
        iv_path_input[0] = '\0';
    }
}

static void iv_on_click(window_t *win, int mx, int my) {
    int bx = win->content_x, by = win->content_y;
    if (my < by + 24) {
        
        int bx2 = bx + 4;
        if (mx >= bx2 && mx < bx2+52) { 
            iv_path_active = true; iv_path_input[0] = '\0';
        } else {
            bx2 += 56;
            if (mx >= bx2 && mx < bx2+36) { 
                iv_zoom *= 1.2f; if (iv_zoom > 8.0f) iv_zoom = 8.0f;
            } else {
                bx2 += 40;
                if (mx >= bx2 && mx < bx2+36) { 
                    iv_zoom /= 1.2f; if (iv_zoom < 0.05f) iv_zoom = 0.05f;
                } else {
                    bx2 += 40;
                    if (mx >= bx2 && mx < bx2+60) { 
                        if (iv_iw > 0 && iv_ih > 0) {
                            int cw2 = win->content_w;
                            int ch2 = win->content_h - 28;
                            float zx = (float)cw2 / (float)iv_iw;
                            float zy = (float)ch2 / (float)iv_ih;
                            iv_zoom = zx < zy ? zx : zy;
                        }
                        iv_pan_x = iv_pan_y = 0;
                    }
                }
            }
        }
    }
}

void image_viewer_open(const char *path) {
    if (iv_win && iv_win->used) {
        wm_focus(iv_win);
        if (path && path[0]) {
            strncpy(iv_filename, path, 127);
            iv_load(path);
        }
        return;
    }

    iv_win = wm_create("Visualizador de Imagens",
                       (int)fb.width/2  - IV_MAX_W/2,
                       (int)fb.height/2 - IV_MAX_H/2,
                       IV_MAX_W, IV_MAX_H, WIN_RESIZABLE);
    if (!iv_win) return;

    iv_win->bg_color   = 0x00111111;
    iv_win->on_paint   = iv_on_paint;
    iv_win->on_keydown = iv_on_keydown;
    iv_win->on_click   = iv_on_click;

    iv_free();
    iv_path_active = false;

    if (path && path[0]) {
        strncpy(iv_filename, path, 127);
        iv_load(path);
    }

    { process_t *p = process_create_app("Imagens", 32*1024);
      if (p) iv_win->proc_pid = p->pid; }
}
