/*
 * apps/settings.c — Painel de configuracoes do Krypx
 * Mostra e permite modificar configuracoes basicas do sistema.
 */

#include <apps/settings.h>
#include <gui/window.h>
#include <gui/canvas.h>
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

/* Seções de configurações */
static const char *sections[] = {
    "Sistema",
    "Exibicao",
    "Usuarios",
    "Sobre",
    NULL
};

static int sel_section = 0;

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

    /* Painel lateral de seções */
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

    /* Separador vertical */
    canvas_draw_line(bx + sidebar_w, by, bx + sidebar_w, by + win->content_h, 0x00636E72);

    /* Conteúdo da seção selecionada */
    int cx = bx + sidebar_w + 12;
    int cy = by + 12;
    uint32_t lbl = 0x0074B9FF;
    uint32_t val = 0x00DFE6E9;

    switch (sel_section) {
        case 0: { /* Sistema */
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
        case 1: { /* Exibicao */
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
            canvas_draw_string(cx+80, cy, rstr, val, COLOR_TRANSPARENT); cy += 20;
            canvas_draw_string(cx, cy, "Profundidade:", lbl, COLOR_TRANSPARENT);
            canvas_draw_string(cx+104, cy, "32bpp", val, COLOR_TRANSPARENT); cy += 20;
            canvas_draw_string(cx, cy, "Driver:", lbl, COLOR_TRANSPARENT);
            canvas_draw_string(cx+80, cy, "VBE/VESA", val, COLOR_TRANSPARENT); cy += 20;
            break;
        }
        case 2: { /* Usuarios */
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
        case 3: { /* Sobre */
            canvas_draw_string(cx, cy, "Sobre o Krypx", lbl, COLOR_TRANSPARENT); cy += 24;
            canvas_draw_string(cx, cy, "Krypx OS v" KRYPX_VERSION_STR, val, COLOR_TRANSPARENT); cy += 20;
            canvas_draw_string(cx, cy, "Custom Bare-Metal OS", val, COLOR_TRANSPARENT); cy += 20;
            canvas_draw_string(cx, cy, "(C) 2026 Valdir2373", 0x00636E72, COLOR_TRANSPARENT); cy += 20;
            canvas_draw_string(cx, cy, "Zero deps, Zero libc", 0x00636E72, COLOR_TRANSPARENT); cy += 20;
            break;
        }
    }
}

static void set_on_keydown(window_t *win, char c) {
    (void)win;
    int nsec = 0;
    while (sections[nsec]) nsec++;
    if (c == '\t' || c == 's') {
        sel_section = (sel_section + 1) % nsec;
    }
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
}
