/*
 * apps/task_manager.c — Gerenciador de tarefas
 * Lista processos, uso de memória e CPU.
 */

#include <apps/task_manager.h>
#include <gui/window.h>
#include <gui/canvas.h>
#include <drivers/framebuffer.h>
#include <proc/process.h>
#include <mm/pmm.h>
#include <kernel/timer.h>
#include <lib/string.h>
#include <types.h>

#define TM_W  480
#define TM_H  320

static window_t *tm_win = NULL;

static const char *state_name(proc_state_t s) {
    switch(s) {
        case PROC_READY:   return "Pronto  ";
        case PROC_RUNNING: return "Rodando ";
        case PROC_BLOCKED: return "Bloq.   ";
        case PROC_ZOMBIE:  return "Zumbi   ";
        default:           return "--------";
    }
}

static void uint_to_str(uint32_t v, char *out) {
    if (v == 0) { out[0]='0'; out[1]='\0'; return; }
    char tmp[12]; int len = 0;
    while (v > 0) { tmp[len++] = (char)('0' + v%10); v /= 10; }
    int i;
    for (i = 0; i < len; i++) out[i] = tmp[len-1-i];
    out[len] = '\0';
}

static void tm_on_paint(window_t *win) {
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);
    int bx = win->content_x, by = win->content_y;
    int w = win->content_w;

    canvas_fill_rect(bx, by, w, win->content_h, 0x001A2433);

    /* Cabeçalho da tabela */
    canvas_fill_rect(bx, by, w, CHAR_HEIGHT + 8, 0x00263545);
    int hx = bx + 4, hy = by + 4;
    canvas_draw_string(hx,      hy, "PID", 0x0074B9FF, COLOR_TRANSPARENT);
    canvas_draw_string(hx+40,   hy, "Nome                  ", 0x0074B9FF, COLOR_TRANSPARENT);
    canvas_draw_string(hx+220,  hy, "Estado  ", 0x0074B9FF, COLOR_TRANSPARENT);
    canvas_draw_string(hx+300,  hy, "Prio", 0x0074B9FF, COLOR_TRANSPARENT);
    canvas_draw_string(hx+350,  hy, "UID", 0x0074B9FF, COLOR_TRANSPARENT);

    /* Linhas de processos */
    int y = by + CHAR_HEIGHT + 12;
    uint32_t row = 0;
    uint32_t pid;
    for (pid = 0; pid < 64 && y < by + win->content_h - 60; pid++) {
        process_t *p = process_get(pid);
        if (!p || p->state == PROC_UNUSED) continue;

        uint32_t row_col = (row % 2 == 0) ? 0x001A2433 : 0x001E2A3A;
        canvas_fill_rect(bx, y-2, w, CHAR_HEIGHT+4, row_col);

        char pidstr[8], priostr[4], uidstr[8];
        uint_to_str(p->pid,      pidstr);
        uint_to_str(p->priority, priostr);
        uint_to_str(p->uid,      uidstr);

        uint32_t col = (p->state == PROC_RUNNING) ? 0x0000B894 : 0x00DFE6E9;
        canvas_draw_string(bx+4,   y, pidstr,             col, COLOR_TRANSPARENT);
        canvas_draw_string(bx+44,  y, p->name,            col, COLOR_TRANSPARENT);
        canvas_draw_string(bx+224, y, state_name(p->state),col, COLOR_TRANSPARENT);
        canvas_draw_string(bx+304, y, priostr,            col, COLOR_TRANSPARENT);
        canvas_draw_string(bx+354, y, uidstr,             col, COLOR_TRANSPARENT);

        y += CHAR_HEIGHT + 4;
        row++;
    }

    if (row == 0) {
        canvas_draw_string(bx + w/2 - 56, by + 80, "Sem processos ativos",
                           0x00636E72, COLOR_TRANSPARENT);
    }

    /* Barra inferior: estatísticas */
    int sy = by + win->content_h - 50;
    canvas_fill_rect(bx, sy, w, 50, 0x00263545);
    canvas_draw_line(bx, sy, bx+w, sy, 0x00636E72);

    uint32_t free_pages = pmm_get_free_pages();
    uint32_t free_kb    = free_pages * 4;
    char fstr[32];
    uint_to_str(free_kb, fstr);
    uint32_t flen = strlen(fstr);
    fstr[flen] = ' '; fstr[flen+1]='K'; fstr[flen+2]='B'; fstr[flen+3]=' ';
    fstr[flen+4]='l'; fstr[flen+5]='i'; fstr[flen+6]='v'; fstr[flen+7]='r';
    fstr[flen+8]='e'; fstr[flen+9]='s'; fstr[flen+10]='\0';

    canvas_draw_string(bx+4, sy+4,  "RAM livre:", 0x0074B9FF, COLOR_TRANSPARENT);
    canvas_draw_string(bx+88, sy+4, fstr, 0x00DFE6E9, COLOR_TRANSPARENT);

    uint32_t secs = timer_get_seconds();
    char usstr[32];
    uint_to_str(secs, usstr);
    canvas_draw_string(bx+4,   sy+22, "Uptime:", 0x0074B9FF, COLOR_TRANSPARENT);
    canvas_draw_string(bx+64,  sy+22, usstr, 0x00DFE6E9, COLOR_TRANSPARENT);
    canvas_draw_string(bx+64 + (int)strlen(usstr)*CHAR_WIDTH + 4, sy+22,
                       "s", 0x00DFE6E9, COLOR_TRANSPARENT);

    char pstr[16];
    uint_to_str(row, pstr);
    canvas_draw_string(bx+240, sy+4, "Processos:", 0x0074B9FF, COLOR_TRANSPARENT);
    canvas_draw_string(bx+320, sy+4, pstr, 0x00DFE6E9, COLOR_TRANSPARENT);
}

void task_manager_open(void) {
    if (tm_win && tm_win->used) { wm_focus(tm_win); return; }

    tm_win = wm_create("Gerenciador de Tarefas",
                        fb.width/2 - TM_W/2,
                        fb.height/2 - TM_H/2,
                        TM_W, TM_H, WIN_RESIZABLE);
    if (!tm_win) return;
    tm_win->bg_color = 0x001A2433;
    tm_win->on_paint = tm_on_paint;
}
