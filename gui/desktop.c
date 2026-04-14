/*
 * gui/desktop.c — Desktop, wallpaper e taskbar do Krypx
 * Loop principal da GUI: renderiza, processa eventos de teclado/mouse.
 */

#include <gui/desktop.h>
#include <gui/canvas.h>
#include <gui/window.h>
#include <drivers/framebuffer.h>
#include <drivers/keyboard.h>
#include <kernel/timer.h>
#include <lib/string.h>
#include <types.h>

#define TASKBAR_HEIGHT  40
#define TASKBAR_Y(h)    ((h) - TASKBAR_HEIGHT)

/* Janela do terminal embutida */
static window_t *terminal_win = 0;
#define TERM_LINES   20
#define TERM_COLS    70
static char term_buf[TERM_LINES][TERM_COLS + 1];
static int  term_row = 0;
static int  term_col = 0;
static char term_input[256];
static int  term_input_len = 0;

static void term_scroll(void) {
    int i;
    for (i = 0; i < TERM_LINES - 1; i++) {
        memcpy(term_buf[i], term_buf[i+1], TERM_COLS + 1);
    }
    memset(term_buf[TERM_LINES-1], 0, TERM_COLS + 1);
    term_row = TERM_LINES - 1;
}

static void term_putchar(char c) {
    if (c == '\n') {
        term_col = 0;
        term_row++;
        if (term_row >= TERM_LINES) term_scroll();
    } else if (c == '\b') {
        if (term_col > 0) {
            term_col--;
            term_buf[term_row][term_col] = ' ';
        }
    } else {
        if (term_col >= TERM_COLS) { term_col = 0; term_row++; }
        if (term_row >= TERM_LINES) term_scroll();
        term_buf[term_row][term_col++] = c;
    }
}

static void term_puts(const char *s) {
    while (*s) term_putchar(*s++);
}

static void term_handle_command(const char *cmd) {
    if (strcmp(cmd, "help") == 0) {
        term_puts("Comandos: help, clear, version, ls\n");
    } else if (strcmp(cmd, "clear") == 0) {
        int i;
        for (i = 0; i < TERM_LINES; i++) memset(term_buf[i], 0, TERM_COLS+1);
        term_row = 0; term_col = 0;
    } else if (strcmp(cmd, "version") == 0) {
        term_puts("Krypx v0.1.0 - Custom OS bare-metal x86\n");
    } else if (strcmp(cmd, "ls") == 0) {
        term_puts("hello.txt  docs/\n");
    } else if (cmd[0]) {
        term_puts("Comando desconhecido: ");
        term_puts(cmd);
        term_puts("\n");
    }
}

static void term_on_paint(window_t *win) {
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);

    int bx = win->content_x, by = win->content_y;
    int row, col;
    for (row = 0; row < TERM_LINES; row++) {
        for (col = 0; col < TERM_COLS; col++) {
            char c = term_buf[row][col];
            if (!c) c = ' ';
            canvas_draw_char(bx + col * CHAR_WIDTH,
                             by + row * CHAR_HEIGHT,
                             c, 0x0000FF00, COLOR_TRANSPARENT);
        }
    }

    /* Linha de input */
    int input_y = by + TERM_LINES * CHAR_HEIGHT + 4;
    canvas_fill_rect(bx, input_y, win->content_w, CHAR_HEIGHT + 4, 0x00111111);
    canvas_draw_string(bx, input_y + 2, "> ", 0x0000CEC9, COLOR_TRANSPARENT);
    canvas_draw_string(bx + 16, input_y + 2, term_input, 0x00FFFFFF, COLOR_TRANSPARENT);

    /* Cursor piscante baseado em ticks */
    if ((timer_get_ticks() / 500) % 2 == 0) {
        int cx = bx + 16 + term_input_len * CHAR_WIDTH;
        canvas_fill_rect(cx, input_y + 2, 2, CHAR_HEIGHT, 0x00FFFFFF);
    }
}

static void term_on_keydown(window_t *win, char key) {
    (void)win;
    if (key == '\n') {
        term_input[term_input_len] = 0;
        term_puts("> ");
        term_puts(term_input);
        term_puts("\n");
        term_handle_command(term_input);
        term_input_len = 0;
        term_input[0]  = 0;
    } else if (key == '\b') {
        if (term_input_len > 0) {
            term_input_len--;
            term_input[term_input_len] = 0;
        }
    } else if (key >= 32 && key < 127 && term_input_len < 255) {
        term_input[term_input_len++] = key;
        term_input[term_input_len]   = 0;
    }
}

static void draw_wallpaper(void) {
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);

    /* Gradiente do desktop */
    canvas_fill_gradient(0, 0, fb.width, fb.height - TASKBAR_HEIGHT,
                         0x000C2461, 0x001A1A2E);

    /* Logo "KRYPX" no desktop */
    int lx = fb.width / 2 - 100;
    int ly = fb.height / 2 - 50;
    canvas_draw_string(lx, ly, "K R Y P X", 0x00FFFFFF, COLOR_TRANSPARENT);
    canvas_draw_string(lx + 4, ly + 20, "Custom OS v0.1", 0x00636E72, COLOR_TRANSPARENT);

    /* Grade de pontos como decoração */
    int x, y;
    for (y = 20; y < (int)fb.height - TASKBAR_HEIGHT - 20; y += 40) {
        for (x = 20; x < (int)fb.width - 20; x += 40) {
            canvas_fill_circle(x, y, 1, 0x00FFFFFF & 0x00333333);
        }
    }
}

static void draw_taskbar(void) {
    int ty = TASKBAR_Y(fb.height);

    /* Fundo da taskbar */
    canvas_fill_gradient(0, ty, fb.width, TASKBAR_HEIGHT,
                         0x001E272E, 0x00141414);
    canvas_fill_rect(0, ty, fb.width, 1, KRYPX_ACCENT);  /* Linha superior */

    /* Botão Menu */
    canvas_fill_rounded_rect(8, ty + 6, 70, 28, 5, KRYPX_BUTTON);
    canvas_draw_string(18, ty + 12, "Menu", COLOR_WHITE, COLOR_TRANSPARENT);

    /* Botão Terminal */
    canvas_fill_rounded_rect(86, ty + 6, 90, 28, 5, 0x00333333);
    canvas_draw_string(96, ty + 12, "Terminal", 0x00DFE6E9, COLOR_TRANSPARENT);

    /* Relógio (usa timer_get_seconds) */
    uint32_t secs  = timer_get_seconds();
    uint32_t hh    = (secs / 3600) % 24;
    uint32_t mm    = (secs / 60) % 60;
    uint32_t ss    = secs % 60;
    char clock[16];
    clock[0]  = '0' + hh / 10; clock[1]  = '0' + hh % 10;
    clock[2]  = ':';
    clock[3]  = '0' + mm / 10; clock[4]  = '0' + mm % 10;
    clock[5]  = ':';
    clock[6]  = '0' + ss / 10; clock[7]  = '0' + ss % 10;
    clock[8]  = 0;
    canvas_draw_string(fb.width - 80, ty + 12, clock,
                       0x00DFE6E9, COLOR_TRANSPARENT);

    /* Indicador de memória livre */
    canvas_draw_string(fb.width - 200, ty + 12, "Krypx v0.1",
                       0x00636E72, COLOR_TRANSPARENT);
}

void desktop_render(void) {
    draw_wallpaper();
    draw_taskbar();
}

void desktop_init(void) {
    wm_init();

    /* Cria janela do terminal */
    terminal_win = wm_create("Krypx Terminal",
                             50, 50,
                             TERM_COLS * CHAR_WIDTH + 20,
                             TERM_LINES * CHAR_HEIGHT + 80,
                             0);
    if (terminal_win) {
        terminal_win->bg_color    = 0x00111111;
        terminal_win->on_paint    = term_on_paint;
        terminal_win->on_keydown  = term_on_keydown;
        term_puts("Krypx Terminal v0.1\n");
        term_puts("Digite 'help' para ver comandos\n");
        term_puts("\n");
    }

    /* Cria janela de boas-vindas */
    window_t *about = wm_create("Sobre o Krypx",
                                fb.width/2 - 200, fb.height/2 - 120,
                                400, 240, WIN_RESIZABLE);
    if (about) {
        about->bg_color = 0x001E272E;
        about->on_paint = 0;  /* Usa conteúdo padrão */
    }
}

void desktop_run(void) {
    uint32_t last_render = 0;

    while (1) {
        /* Renderiza a ~30 FPS (a cada ~33 ms) */
        uint32_t now = timer_get_ticks();
        if (now - last_render >= 33) {
            last_render = now;
            desktop_render();
            wm_render();
        }

        /* Processa teclas */
        char key = keyboard_getchar();
        if (key) {
            wm_key_event(key);
        }

        __asm__ volatile ("hlt");
    }
}
