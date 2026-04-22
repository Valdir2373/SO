

#include <apps/settings.h>
#include <gui/window.h>
#include <gui/canvas.h>
#include <gui/desktop.h>
#include <proc/process.h>
#include <drivers/framebuffer.h>
#include <security/users.h>
#include <kernel/timer.h>
#include <mm/pmm.h>
#include <lib/string.h>
#include <types.h>
#include <system.h>

#define SET_W  400
#define SET_H  380

static window_t *set_win = NULL;


static const char *sections[] = {
    "Sistema",
    "Exibicao",
    "Usuarios",
    "Sobre",
    NULL
};

static int sel_section = 0;


static char wp_path[128] = "";
static bool wp_input_active = false;
static char wp_status[64]   = "";

static void uint_to_str2(uint32_t v, char *out) {
    if (v == 0) { out[0]='0'; out[1]='\0'; return; }
    char tmp[12]; int len = 0;
    while (v > 0) { tmp[len++] = (char)('0' + v%10); v /= 10; }
    int i;
    for (i = 0; i < len; i++) out[i] = tmp[len-1-i];
    out[len] = '\0';
}

static void set_on_paint(window_t *win) {
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);
    int bx = win->content_x, by = win->content_y;
    int w = win->content_w;

    canvas_fill_rect(bx, by, w, win->content_h, 0x001E272E);

    
    int sidebar_w = 100;
    canvas_fill_rect(bx, by, sidebar_w, win->content_h, 0x00151E27);

    int i;
    for (i = 0; sections[i]; i++) {
        int sy = by + 8 + i * 36;
        uint32_t bg = (i == sel_section) ? 0x000984E3 : 0x00151E27;
        canvas_fill_rect(bx, sy-2, sidebar_w, 32, bg);
        canvas_draw_string(bx + 8, sy + 8, sections[i],
                           0x00DFE6E9, COLOR_TRANSPARENT);
    }

    
    canvas_draw_line(bx + sidebar_w, by, bx + sidebar_w, by + win->content_h, 0x00636E72);

    
    int cx = bx + sidebar_w + 12;
    int cy = by + 12;
    uint32_t lbl = 0x0074B9FF;
    uint32_t val = 0x00DFE6E9;

    switch (sel_section) {
        case 0: { 
            canvas_draw_string(cx, cy, "Sistema", lbl, COLOR_TRANSPARENT); cy += 24;
            canvas_draw_string(cx, cy, "Versao:", lbl, COLOR_TRANSPARENT);
            canvas_draw_string(cx+80, cy, KRYPX_VERSION_STR, val, COLOR_TRANSPARENT); cy += 20;

            char uptstr[16];
            uint32_t s = timer_get_seconds();
            uint_to_str2(s, uptstr);
            canvas_draw_string(cx, cy, "Uptime:", lbl, COLOR_TRANSPARENT);
            canvas_draw_string(cx+80, cy, uptstr, val, COLOR_TRANSPARENT);
            canvas_draw_string(cx+80+(int)strlen(uptstr)*8, cy, "s", val, COLOR_TRANSPARENT); cy += 20;

            char fstr[16];
            uint_to_str2(pmm_get_free_pages()*4, fstr);
            canvas_draw_string(cx, cy, "RAM livre:", lbl, COLOR_TRANSPARENT);
            canvas_draw_string(cx+80, cy, fstr, val, COLOR_TRANSPARENT);
            canvas_draw_string(cx+80+(int)strlen(fstr)*8, cy, " KB", val, COLOR_TRANSPARENT); cy += 20;

            canvas_draw_string(cx, cy, "Arch:", lbl, COLOR_TRANSPARENT);
            canvas_draw_string(cx+80, cy, "x86 32-bit", val, COLOR_TRANSPARENT); cy += 20;
            break;
        }
        case 1: { 
            canvas_draw_string(cx, cy, "Exibicao", lbl, COLOR_TRANSPARENT); cy += 24;
            
            char rstr[32];
            char tmp1[8], tmp2[8];
            uint_to_str2(fb.width,  tmp1);
            uint_to_str2(fb.height, tmp2);
            memcpy(rstr, tmp1, strlen(tmp1)+1);
            rstr[strlen(rstr)] = 'x';
            rstr[strlen(rstr)+1] = '\0';
            memcpy(rstr+strlen(rstr), tmp2, strlen(tmp2)+1);
            canvas_draw_string(cx, cy, "Resolucao:", lbl, COLOR_TRANSPARENT);
            canvas_draw_string(cx+96, cy, rstr, val, COLOR_TRANSPARENT); cy += 20;
            canvas_draw_string(cx, cy, "Profundidade:", lbl, COLOR_TRANSPARENT);
            canvas_draw_string(cx+112, cy, "32bpp", val, COLOR_TRANSPARENT); cy += 20;
            canvas_draw_string(cx, cy, "Driver:", lbl, COLOR_TRANSPARENT);
            canvas_draw_string(cx+96, cy, "VBE/VESA", val, COLOR_TRANSPARENT); cy += 28;

            
            canvas_draw_string(cx, cy, "Papel de Parede", lbl, COLOR_TRANSPARENT); cy += 20;
            canvas_draw_string(cx, cy, "Formatos: PNG, JPG, JPEG", 0x00636E72, COLOR_TRANSPARENT); cy += 18;

            
            int box_x = cx, box_y = cy, box_w = 210, box_h = 20;
            uint32_t box_border = wp_input_active ? 0x000984E3 : 0x00636E72;
            canvas_fill_rect(box_x, box_y, box_w, box_h, 0x00151E27);
            canvas_draw_rect(box_x, box_y, box_w, box_h, box_border);
            
            const char *disp = (wp_path[0] != '\0') ? wp_path : "/wallpaper.png";
            uint32_t disp_col = (wp_path[0] != '\0') ? val : 0x00636E72;
            canvas_draw_string(box_x+4, box_y+3, disp, disp_col, COLOR_TRANSPARENT);
            if (wp_input_active) {
                
                int cpos = box_x + 4 + (int)strlen(wp_path)*8;
                canvas_fill_rect(cpos, box_y+3, 2, 13, 0x00DFE6E9);
            }
            cy += box_h + 4;

            
            int btn_x = cx, btn_y = cy, btn_w = 80, btn_h = 22;
            canvas_fill_rounded_rect(btn_x, btn_y, btn_w, btn_h, 3, 0x000984E3);
            canvas_draw_string(btn_x + 12, btn_y + 5, "Aplicar", 0x00FFFFFF, COLOR_TRANSPARENT);
            cy += btn_h + 8;

            
            if (wp_status[0])
                canvas_draw_string(cx, cy, wp_status, 0x0000B894, COLOR_TRANSPARENT);
            break;
        }
        case 2: { 
            canvas_draw_string(cx, cy, "Usuarios", lbl, COLOR_TRANSPARENT); cy += 24;
            if (current_user) {
                canvas_draw_string(cx, cy, "Logado:", lbl, COLOR_TRANSPARENT);
                canvas_draw_string(cx+80, cy, current_user->username, val, COLOR_TRANSPARENT); cy += 20;
                canvas_draw_string(cx, cy, "UID:", lbl, COLOR_TRANSPARENT);
                char uidstr[8];
                uint_to_str2(current_user->uid, uidstr);
                canvas_draw_string(cx+80, cy, uidstr, val, COLOR_TRANSPARENT); cy += 20;
                const char *priv_name = "Usuario";
                if (current_user->privileges == 2) priv_name = "Root";
                else if (current_user->privileges == 1) priv_name = "Admin";
                canvas_draw_string(cx, cy, "Nivel:", lbl, COLOR_TRANSPARENT);
                canvas_draw_string(cx+80, cy, priv_name, val, COLOR_TRANSPARENT); cy += 20;
                canvas_draw_string(cx, cy, "Home:", lbl, COLOR_TRANSPARENT);
                canvas_draw_string(cx+80, cy, current_user->home_dir, val, COLOR_TRANSPARENT); cy += 20;
            }
            break;
        }
        case 3: { 
            canvas_draw_string(cx, cy, "Sobre o Krypx", lbl, COLOR_TRANSPARENT); cy += 24;
            canvas_draw_string(cx, cy, "Krypx OS v" KRYPX_VERSION_STR, val, COLOR_TRANSPARENT); cy += 20;
            canvas_draw_string(cx, cy, "Custom Bare-Metal OS", val, COLOR_TRANSPARENT); cy += 20;
            canvas_draw_string(cx, cy, "Zero deps. Zero libc. Puro metal.", 0x00636E72, COLOR_TRANSPARENT); cy += 20;
            canvas_draw_string(cx, cy, "Zero deps, Zero libc", 0x00636E72, COLOR_TRANSPARENT); cy += 20;
            break;
        }
    }
}

static void set_on_keydown(window_t *win, char c) {
    (void)win;
    if (wp_input_active) {
        if (c == '\r' || c == '\n') {
            
            if (wp_path[0]) {
                desktop_set_wallpaper(wp_path);
                memcpy(wp_status, "Papel de parede aplicado!", 26);
            }
            wp_input_active = false;
        } else if (c == 27) {
            wp_input_active = false;
        } else if (c == 8 || c == 127) {
            int l = (int)strlen(wp_path);
            if (l > 0) wp_path[l-1] = '\0';
        } else if (c >= 32 && strlen(wp_path) < 127) {
            int l = (int)strlen(wp_path);
            wp_path[l] = c;
            wp_path[l+1] = '\0';
        }
        return;
    }
    int nsec = 0;
    while (sections[nsec]) nsec++;
    if (c == '\t' || c == 's') {
        sel_section = (sel_section + 1) % nsec;
    }
}

static void set_on_click(window_t *win, int mx, int my) {
    
    int bx = win->content_x, by = win->content_y;
    int sidebar_w = 100;
    if (mx >= bx && mx < bx + sidebar_w) {
        int i;
        for (i = 0; sections[i]; i++) {
            int sy = by + 8 + i * 36;
            if (my >= sy-2 && my < sy+30) {
                sel_section = i;
                wp_input_active = false;
                return;
            }
        }
        return;
    }
    
    if (sel_section != 1) return;
    int cx = bx + sidebar_w + 12;
    
    int wp_top = by + 12 + 130;
    int box_y  = wp_top;      
    int btn_y  = box_y + 24;  
    
    if (mx >= cx && mx < cx+210 && my >= box_y && my < box_y+20) {
        wp_input_active = true;
        wp_status[0] = '\0';
        return;
    }
    
    if (mx >= cx && mx < cx+80 && my >= btn_y && my < btn_y+22) {
        wp_input_active = false;
        if (wp_path[0]) {
            desktop_set_wallpaper(wp_path);
            memcpy(wp_status, "Papel de parede aplicado!", 26);
        }
        return;
    }
    wp_input_active = false;
}

void settings_open(void) {
    if (set_win && set_win->used) { wm_focus(set_win); return; }

    set_win = wm_create("Configuracoes",
                         fb.width/2 - SET_W/2,
                         fb.height/2 - SET_H/2,
                         SET_W, SET_H, WIN_RESIZABLE);
    if (!set_win) return;
    set_win->bg_color   = 0x001E272E;
    set_win->on_paint   = set_on_paint;
    set_win->on_keydown = set_on_keydown;
    set_win->on_click   = set_on_click;
    { process_t *p = process_create_app("Configuracoes", 32 * 1024);
      if (p) set_win->proc_pid = p->pid; }
}
