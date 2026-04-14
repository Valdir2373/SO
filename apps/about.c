/*
 * apps/about.c — Sobre o Krypx
 * Exibe informacoes do sistema, versao, hardware.
 */

#include <apps/about.h>
#include <gui/window.h>
#include <gui/canvas.h>
#include <drivers/framebuffer.h>
#include <mm/pmm.h>
#include <kernel/timer.h>
#include <system.h>
#include <lib/string.h>
#include <types.h>

static window_t *about_win = NULL;

static void about_on_paint(window_t *win) {
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);
    int bx = win->content_x, by = win->content_y;
    int w  = win->content_w;

    canvas_fill_rect(bx, by, w, win->content_h, 0x001E272E);

    /* Logo / Header */
    canvas_fill_gradient(bx, by, w, 60, 0x00003580, 0x001E272E);
    int cx = bx + w/2;
    canvas_draw_string(cx - 40, by + 8,  "Krypx OS", 0x0074B9FF, COLOR_TRANSPARENT);
    canvas_draw_string(cx - 56, by + 28, "Custom Bare-Metal OS", 0x00636E72, COLOR_TRANSPARENT);

    int y = by + 72;
    int lx = bx + 16;
    uint32_t lbl = 0x0074B9FF;
    uint32_t val = 0x00DFE6E9;

    /* Versao */
    canvas_draw_string(lx,       y, "Versao:", lbl, COLOR_TRANSPARENT);
    canvas_draw_string(lx + 80,  y, KRYPX_VERSION_STR, val, COLOR_TRANSPARENT); y += 20;

    /* Arquitetura */
    canvas_draw_string(lx,       y, "Arch:", lbl, COLOR_TRANSPARENT);
    canvas_draw_string(lx + 80,  y, "x86 32-bit (i686)", val, COLOR_TRANSPARENT); y += 20;

    /* Build */
    canvas_draw_string(lx,       y, "Build:", lbl, COLOR_TRANSPARENT);
    canvas_draw_string(lx + 80,  y, "gcc -m32 (fallback)", val, COLOR_TRANSPARENT); y += 20;

    /* Uptime */
    uint32_t secs = timer_get_seconds();
    char uptstr[32];
    uint32_t hh = secs/3600, mm = (secs/60)%60, ss = secs%60;
    uptstr[0] = '0'+hh/10; uptstr[1] = '0'+hh%10;
    uptstr[2] = 'h'; uptstr[3] = ' ';
    uptstr[4] = '0'+mm/10; uptstr[5] = '0'+mm%10;
    uptstr[6] = 'm'; uptstr[7] = ' ';
    uptstr[8] = '0'+ss/10; uptstr[9] = '0'+ss%10;
    uptstr[10]= 's'; uptstr[11]='\0';
    canvas_draw_string(lx,       y, "Uptime:", lbl, COLOR_TRANSPARENT);
    canvas_draw_string(lx + 80,  y, uptstr, val, COLOR_TRANSPARENT); y += 20;

    /* Memoria livre */
    uint32_t free_pages = pmm_get_free_pages();
    char memstr[32];
    uint32_t free_mb = (free_pages * 4) / 1024;
    memstr[0] = '0' + free_mb/100 % 10;
    memstr[1] = '0' + free_mb/10  % 10;
    memstr[2] = '0' + free_mb     % 10;
    memcpy(memstr+3, " MB livres", 11);
    canvas_draw_string(lx,       y, "RAM:", lbl, COLOR_TRANSPARENT);
    canvas_draw_string(lx + 80,  y, memstr, val, COLOR_TRANSPARENT); y += 20;

    /* Tela */
    char resstr[32];
    resstr[0] = '0'+fb.width/1000%10;
    resstr[1] = '0'+fb.width/100%10;
    resstr[2] = '0'+fb.width/10%10;
    resstr[3] = '0'+fb.width%10;
    resstr[4] = 'x';
    resstr[5] = '0'+fb.height/1000%10;
    resstr[6] = '0'+fb.height/100%10;
    resstr[7] = '0'+fb.height/10%10;
    resstr[8] = '0'+fb.height%10;
    memcpy(resstr+9, " 32bpp", 7);
    canvas_draw_string(lx,       y, "Tela:", lbl, COLOR_TRANSPARENT);
    canvas_draw_string(lx + 80,  y, resstr, val, COLOR_TRANSPARENT); y += 30;

    /* Separador */
    canvas_draw_line(bx+8, y, bx+w-8, y, 0x00636E72);  y += 12;

    canvas_draw_string(cx-96, y, "(C) 2026 Valdir2373 - Todos os direitos", 0x00636E72, COLOR_TRANSPARENT);
}

void about_open(void) {
    if (about_win && about_win->used) { wm_focus(about_win); return; }

    about_win = wm_create("Sobre o Krypx",
                           fb.width/2 - 180,
                           fb.height/2 - 160,
                           360, 320, WIN_RESIZABLE);
    if (!about_win) return;
    about_win->bg_color = 0x001E272E;
    about_win->on_paint = about_on_paint;
}
