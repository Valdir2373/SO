/*
 * gui/window.h — Estrutura de janela e Window Manager Stacking
 */
#ifndef _WINDOW_H
#define _WINDOW_H

#include <types.h>

#define MAX_WINDOWS      32
#define TITLEBAR_HEIGHT  28
#define BORDER_WIDTH      2
#define BTN_SIZE         14   /* Botões de fechar/minimizar */
#define BTN_MARGIN        6

/* Flags de janela */
#define WIN_VISIBLE     (1<<0)
#define WIN_FOCUSED     (1<<1)
#define WIN_MINIMIZED   (1<<2)
#define WIN_MAXIMIZED   (1<<3)
#define WIN_RESIZABLE   (1<<4)
#define WIN_NO_TITLEBAR (1<<5)
#define WIN_TOPMOST     (1<<6)

typedef struct window {
    uint32_t id;
    char     title[128];
    int      x, y, w, h;         /* Incluindo decorações */
    int      content_x, content_y, content_w, content_h;
    uint32_t flags;
    uint32_t bg_color;

    uint32_t *buf;   /* Buffer privado do conteúdo */

    /* Callbacks */
    void (*on_paint)(struct window *win);
    void (*on_close)(struct window *win);
    void (*on_keydown)(struct window *win, char key);

    /* Z-order (lista duplamente encadeada) */
    struct window *above;   /* Janela acima desta (NULL = topo) */
    struct window *below;   /* Janela abaixo desta (NULL = fundo) */

    /* Drag state */
    bool drag_active;
    int  drag_off_x, drag_off_y;

    bool used;
} window_t;

/* Inicializa o Window Manager */
void wm_init(void);

/* Cria uma janela */
window_t *wm_create(const char *title, int x, int y, int w, int h, uint32_t flags);

/* Fecha e libera uma janela */
void wm_close(window_t *win);

/* Traz para o topo */
void wm_focus(window_t *win);

/* Renderiza todas as janelas (backbuffer → tela) */
void wm_render(void);

/* Notifica evento de mouse (de drivers/mouse ou teclado de teste) */
void wm_mouse_move(int x, int y);
void wm_mouse_down(int x, int y, uint8_t btn);
void wm_mouse_up(int x, int y, uint8_t btn);

/* Notifica tecla para janela focada */
void wm_key_event(char key);

/* Retorna janela sob ponto (x, y) */
window_t *wm_window_at(int x, int y);

/* Pede redraw de uma janela */
void wm_invalidate(window_t *win);

#endif /* _WINDOW_H */
