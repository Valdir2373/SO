/*
 * gui/window.c — Window Manager Stacking
 * Renderiza janelas de baixo para cima (z-order).
 * Gerencia foco, drag, e decorações (titlebar, botões).
 */

#include <gui/window.h>
#include <gui/canvas.h>
#include <drivers/framebuffer.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <types.h>

static window_t windows[MAX_WINDOWS];
static window_t *wm_bottom = 0;   /* Janela mais abaixo (fundo) */
static window_t *wm_top    = 0;   /* Janela mais acima (topo) */
static window_t *wm_focused = 0;
static uint32_t  next_wid   = 1;

/* Mouse state */
static int  mouse_x = 400, mouse_y = 300;
static bool mouse_ldown = false;

/* ---- Helpers ---- */

static void wm_draw_titlebar(window_t *win) {
    bool focused = (win == wm_focused);
    uint32_t tb_color = focused ? KRYPX_TITLEBAR : KRYPX_TITLEBAR_IN;

    /* Fundo da titlebar */
    canvas_fill_rect(win->x, win->y, win->w, TITLEBAR_HEIGHT, tb_color);

    /* Título centralizado */
    int tw = canvas_string_width(win->title);
    int tx = win->x + (win->w - tw) / 2;
    canvas_draw_string(tx, win->y + 6, win->title, KRYPX_TEXT, COLOR_TRANSPARENT);

    /* Botão fechar (X vermelho) */
    int bx = win->x + win->w - BTN_SIZE - BTN_MARGIN;
    int by = win->y + (TITLEBAR_HEIGHT - BTN_SIZE) / 2;
    canvas_fill_rounded_rect(bx, by, BTN_SIZE, BTN_SIZE, 3, 0x00E74C3C);
    canvas_draw_string(bx + 3, by + 1, "x", COLOR_WHITE, COLOR_TRANSPARENT);

    /* Botão minimizar (amarelo) */
    bx -= BTN_SIZE + 4;
    canvas_fill_rounded_rect(bx, by, BTN_SIZE, BTN_SIZE, 3, 0x00F39C12);

    /* Botão maximizar (verde) */
    bx -= BTN_SIZE + 4;
    canvas_fill_rounded_rect(bx, by, BTN_SIZE, BTN_SIZE, 3, 0x0027AE60);
}

static void wm_draw_border(window_t *win) {
    bool focused = (win == wm_focused);
    uint32_t bc = focused ? KRYPX_TITLEBAR : 0x00444444;
    canvas_draw_rect(win->x, win->y, win->w, win->h, bc);
    canvas_draw_rect(win->x+1, win->y+1, win->w-2, win->h-2, 0x00222222);
}

static void wm_draw_window(window_t *win) {
    if (!win->used || !(win->flags & WIN_VISIBLE)) return;
    if (win->flags & WIN_MINIMIZED) return;

    /* Conteúdo */
    canvas_fill_rect(win->content_x, win->content_y,
                     win->content_w, win->content_h, win->bg_color);

    /* Callback de pintura */
    if (win->on_paint) win->on_paint(win);

    /* Decorações */
    if (!(win->flags & WIN_NO_TITLEBAR)) {
        wm_draw_titlebar(win);
    }
    wm_draw_border(win);
}

static void draw_cursor(int x, int y) {
    /* Cursor simples em forma de seta */
    canvas_draw_line(x, y, x+10, y+6, COLOR_WHITE);
    canvas_draw_line(x, y, x+4,  y+12, COLOR_WHITE);
    canvas_draw_line(x+4, y+12, x+10, y+6, COLOR_WHITE);
    canvas_fill_circle(x, y, 2, COLOR_WHITE);
}

/* ---- API ---- */

void wm_init(void) {
    uint32_t i;
    for (i = 0; i < MAX_WINDOWS; i++) {
        windows[i].used = false;
    }
    wm_bottom = wm_top = wm_focused = 0;
}

window_t *wm_create(const char *title, int x, int y, int w, int h, uint32_t flags) {
    /* Encontra slot livre */
    uint32_t i;
    window_t *win = 0;
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].used) { win = &windows[i]; break; }
    }
    if (!win) return 0;

    memset(win, 0, sizeof(window_t));
    win->id    = next_wid++;
    win->flags = flags | WIN_VISIBLE;
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->bg_color = KRYPX_BG;
    strncpy(win->title, title, 127);

    /* Área de conteúdo (sem titlebar e bordas) */
    win->content_x = x + BORDER_WIDTH;
    win->content_y = y + ((flags & WIN_NO_TITLEBAR) ? BORDER_WIDTH : TITLEBAR_HEIGHT);
    win->content_w = w - 2 * BORDER_WIDTH;
    win->content_h = h - ((flags & WIN_NO_TITLEBAR) ? 2*BORDER_WIDTH
                                                     : TITLEBAR_HEIGHT + BORDER_WIDTH);
    win->used = true;

    /* Insere no topo da z-order */
    win->below = wm_top;
    win->above = 0;
    if (wm_top) wm_top->above = win;
    wm_top = win;
    if (!wm_bottom) wm_bottom = win;

    wm_focused = win;
    return win;
}

void wm_close(window_t *win) {
    if (!win || !win->used) return;

    /* Remove da z-order */
    if (win->above) win->above->below = win->below;
    else            wm_top            = win->below;
    if (win->below) win->below->above = win->above;
    else            wm_bottom         = win->above;

    if (wm_focused == win) wm_focused = wm_top;

    if (win->on_close) win->on_close(win);
    win->used = false;
}

void wm_focus(window_t *win) {
    if (!win || !win->used || win == wm_top) {
        if (win) wm_focused = win;
        return;
    }

    /* Remove da posição atual */
    if (win->above) win->above->below = win->below;
    else wm_top = win->below;
    if (win->below) win->below->above = win->above;
    else wm_bottom = win->above;

    /* Coloca no topo */
    win->below = wm_top;
    win->above = 0;
    if (wm_top) wm_top->above = win;
    wm_top = win;
    if (!wm_bottom) wm_bottom = win;

    wm_focused = win;
}

void wm_render(void) {
    if (!fb.ready) return;

    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);

    /* Desenha de baixo para cima */
    window_t *w = wm_bottom;
    while (w) {
        wm_draw_window(w);
        w = w->above;
    }

    /* Cursor */
    draw_cursor(mouse_x, mouse_y);

    fb_swap();
}

window_t *wm_window_at(int x, int y) {
    /* Percorre de cima para baixo (topo tem prioridade) */
    window_t *w = wm_top;
    while (w) {
        if (w->used && (w->flags & WIN_VISIBLE) && !(w->flags & WIN_MINIMIZED)) {
            if (x >= w->x && x < w->x + w->w &&
                y >= w->y && y < w->y + w->h) {
                return w;
            }
        }
        w = w->below;
    }
    return 0;
}

void wm_invalidate(window_t *win) {
    (void)win;
    wm_render();
}

void wm_mouse_move(int x, int y) {
    mouse_x = x;
    mouse_y = y;

    /* Drag de janela */
    if (mouse_ldown && wm_focused && wm_focused->drag_active) {
        wm_focused->x = x - wm_focused->drag_off_x;
        wm_focused->y = y - wm_focused->drag_off_y;
        /* Atualiza área de conteúdo */
        wm_focused->content_x = wm_focused->x + BORDER_WIDTH;
        wm_focused->content_y = wm_focused->y + TITLEBAR_HEIGHT;
    }
}

void wm_mouse_down(int x, int y, uint8_t btn) {
    if (btn != 0) return;  /* Só botão esquerdo */
    mouse_ldown = true;

    window_t *win = wm_window_at(x, y);
    if (!win) return;

    wm_focus(win);

    /* Verifica se clicou na titlebar → inicia drag */
    if (!(win->flags & WIN_NO_TITLEBAR)) {
        if (y >= win->y && y < win->y + TITLEBAR_HEIGHT) {
            /* Verifica botão fechar */
            int bx = win->x + win->w - BTN_SIZE - BTN_MARGIN;
            int by = win->y + (TITLEBAR_HEIGHT - BTN_SIZE) / 2;
            if (x >= bx && x < bx + BTN_SIZE && y >= by && y < by + BTN_SIZE) {
                wm_close(win);
                return;
            }
            win->drag_active = true;
            win->drag_off_x  = x - win->x;
            win->drag_off_y  = y - win->y;
        }
    }
}

void wm_mouse_up(int x, int y, uint8_t btn) {
    (void)x; (void)y;
    if (btn == 0) {
        mouse_ldown = false;
        if (wm_focused) wm_focused->drag_active = false;
    }
}

void wm_key_event(char key) {
    if (wm_focused && wm_focused->on_keydown) {
        wm_focused->on_keydown(wm_focused, key);
    }
}
