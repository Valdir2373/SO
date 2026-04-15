/*
 * gui/desktop.c — Desktop, wallpaper e taskbar do Krypx
 * Loop principal da GUI: renderiza, processa eventos de teclado/mouse.
 */

#include <gui/desktop.h>
#include <gui/canvas.h>
#include <gui/window.h>
#include <drivers/framebuffer.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <kernel/timer.h>
#include <security/users.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <proc/elf.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <compat/detect.h>
#include <compat/linux_compat.h>
#include <apps/calculator.h>
#include <apps/task_manager.h>
#include <apps/about.h>
#include <apps/network_manager.h>
#include <apps/settings.h>
#include <lib/string.h>
#include <types.h>

/* Menu aberto? */
static bool menu_open = false;

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

/* Carrega e executa um binário Linux ELF — saída vai para o terminal */
static void term_run_linux(const char *path) {
    linux_compat_set_output(term_putchar);

    vfs_node_t *node = vfs_resolve(path);
    if (!node) {
        term_puts("Erro: arquivo nao encontrado: ");
        term_puts(path);
        term_puts("\n");
        return;
    }

    uint32_t size = node->size;
    if (size == 0 || size > 1024 * 1024) {
        term_puts("Erro: tamanho invalido\n");
        return;
    }

    uint8_t *data = (uint8_t *)kmalloc(size);
    if (!data) {
        term_puts("Erro: sem memoria\n");
        return;
    }

    vfs_open(node, O_RDONLY);
    vfs_read(node, 0, size, data);
    vfs_close(node);

    binary_type_t btype = detect_binary_type(data, size);
    if (btype != BINARY_LINUX_ELF && btype != BINARY_KRYPX_ELF) {
        kfree(data);
        term_puts("Erro: nao e um ELF i386 Linux valido\n");
        return;
    }

    if (!elf_validate(data, size)) {
        kfree(data);
        term_puts("Erro: ELF corrompido\n");
        return;
    }

    /* Extrai só o nome do arquivo para o PCB */
    const char *pname = path;
    {
        const char *t = path;
        while (*t) { if (*t == '/') pname = t + 1; t++; }
    }

    process_t *proc = process_create(pname, 0, 2);
    if (!proc) {
        kfree(data);
        term_puts("Erro: falha ao criar processo\n");
        return;
    }

    elf_load_result_t res;
    if (elf_load(proc, data, size, &res) != 0) {
        kfree(data);
        term_puts("Erro: falha ao carregar ELF\n");
        return;
    }
    kfree(data);

    proc->ctx.eip    = res.entry_point;
    proc->ctx.esp    = res.user_stack_top;
    proc->ctx.eflags = 0x202;
    proc->heap_start = res.heap_base;
    proc->heap_end   = res.heap_base;
    proc->compat_mode = (btype == BINARY_LINUX_ELF) ? COMPAT_LINUX : COMPAT_NONE;

    scheduler_add(proc);

    term_puts("[OK] Executando: ");
    term_puts(pname);
    term_puts("\n");
}

static void term_handle_command(const char *cmd) {
    if (strcmp(cmd, "help") == 0) {
        term_puts("Comandos: help, clear, version, ls, test_linux\n");
    } else if (strcmp(cmd, "clear") == 0) {
        int i;
        for (i = 0; i < TERM_LINES; i++) memset(term_buf[i], 0, TERM_COLS+1);
        term_row = 0; term_col = 0;
    } else if (strcmp(cmd, "version") == 0) {
        term_puts("Krypx v0.1.0 - Custom OS bare-metal x86\n");
    } else if (strcmp(cmd, "ls") == 0) {
        term_puts("hello.txt  docs/  test_linux\n");
    } else if (strcmp(cmd, "test_linux") == 0) {
        term_puts("[COMPAT] Carregando binario Linux...\n");
        term_run_linux("/test_linux");
    } else if (cmd[0] == '/') {
        /* Tenta executar caminho absoluto como ELF */
        term_run_linux(cmd);
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
    bool term_min = terminal_win && (terminal_win->flags & WIN_MINIMIZED);
    uint32_t term_col = term_min ? 0x00555566 : 0x00333333;
    canvas_fill_rounded_rect(86, ty + 6, 90, 28, 5, term_col);
    canvas_draw_string(96, ty + 12, "Terminal", 0x00DFE6E9, COLOR_TRANSPARENT);

    /* Botões de janelas minimizadas */
    wm_draw_taskbar_entries(184, ty + 6, 110, 28, fb.width - 220);

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

    /* Usuário logado */
    if (current_user) {
        canvas_draw_string(fb.width - 200, ty + 12, current_user->username,
                           0x0074B9FF, COLOR_TRANSPARENT);
    } else {
        canvas_draw_string(fb.width - 200, ty + 12, "Krypx v0.1",
                           0x00636E72, COLOR_TRANSPARENT);
    }
}

/* Menu inicial — lista de aplicativos */
typedef struct { const char *name; void (*open)(void); } menu_item_t;
static const menu_item_t menu_items[] = {
    { "Calculadora",      calculator_open     },
    { "Task Manager",     task_manager_open   },
    { "Rede",             network_manager_open},
    { "Configuracoes",    settings_open       },
    { "Sobre o Krypx",    about_open          },
    { NULL, NULL }
};
#define MENU_ITEM_H   28
#define MENU_W       180

static void draw_menu(void) {
    int ty = TASKBAR_Y(fb.height);
    int nitems = 0;
    while (menu_items[nitems].name) nitems++;
    int mh = nitems * MENU_ITEM_H + 8;
    int mx = 8, my = ty - mh - 4;

    canvas_fill_rounded_rect(mx, my, MENU_W, mh, 6, 0x00263545);
    canvas_draw_rounded_rect(mx, my, MENU_W, mh, 6, 0x00636E72);

    int i;
    for (i = 0; i < nitems; i++) {
        int iy = my + 4 + i * MENU_ITEM_H;
        canvas_draw_string(mx + 12, iy + 8, menu_items[i].name,
                           0x00DFE6E9, COLOR_TRANSPARENT);
        /* Linha divisória */
        if (i < nitems-1)
            canvas_draw_line(mx+4, iy+MENU_ITEM_H-1, mx+MENU_W-4, iy+MENU_ITEM_H-1, 0x00333333);
    }
}

/* Geometria do menu (recalculada a cada frame) */
static void menu_geometry(int *mx_out, int *my_out, int *mh_out, int *nitems_out) {
    int ty = TASKBAR_Y(fb.height);
    int nitems = 0;
    while (menu_items[nitems].name) nitems++;
    int mh = nitems * MENU_ITEM_H + 8;
    *mx_out    = 8;
    *my_out    = ty - mh - 4;
    *mh_out    = mh;
    *nitems_out = nitems;
}

/* Verifica se ponto (x,y) está dentro da área do menu */
static bool is_in_menu(int x, int y) {
    int mx, my, mh, nitems;
    menu_geometry(&mx, &my, &mh, &nitems);
    return (x >= mx && x <= mx + MENU_W && y >= my && y <= my + mh);
}

/* Trata clique dentro do menu */
static void menu_handle_click(int x, int y) {
    int mx, my, mh, nitems;
    menu_geometry(&mx, &my, &mh, &nitems);

    if (x < mx || x > mx + MENU_W || y < my || y > my + mh) {
        menu_open = false; return;
    }
    int idx = (y - my - 4) / MENU_ITEM_H;
    if (idx >= 0 && idx < nitems && menu_items[idx].open) {
        menu_items[idx].open();
        menu_open = false;
    }
}

/* Trata clique na área da taskbar */
static void desktop_taskbar_click(int x, int y) {
    int ty = TASKBAR_Y(fb.height);

    /* Botão Menu */
    if (x >= 8 && x <= 78 && y >= ty + 6 && y <= ty + 34) {
        menu_open = !menu_open;
        return;
    }
    /* Botão Terminal */
    if (x >= 86 && x <= 176 && y >= ty + 6 && y <= ty + 34) {
        if (terminal_win) {
            if (terminal_win->flags & WIN_MINIMIZED) {
                terminal_win->flags &= ~WIN_MINIMIZED;
                wm_focus(terminal_win);
            } else {
                terminal_win->flags |= WIN_MINIMIZED;
            }
        }
        return;
    }
    /* Botões de janelas minimizadas */
    wm_taskbar_entry_click(x, y, 184, ty + 6, 110, 28, (int)fb.width - 220);
}

void desktop_render(void) {
    /* wm_render() já faz canvas_init + fb_swap internamente,
     * mas precisamos desenhar o wallpaper ANTES das janelas.
     * Para isso, desenhamos no backbuffer antes de chamar wm_render. */
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);
    draw_wallpaper();
    draw_taskbar();
    if (menu_open) draw_menu();
    /* wm_render desenha janelas por cima e chama fb_swap */
    wm_render();
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
        /* Processo para o terminal */
        { process_t *p = process_create_app("Terminal", 128 * 1024);
          if (p) terminal_win->proc_pid = p->pid; }
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

/* ============================================================
 * Tela de login
 * ============================================================ */
static void draw_login_screen(const char *username, uint32_t ulen,
                               const char *pass_mask, uint32_t plen,
                               bool field_user, bool error)
{
    /* Inicializa canvas com o backbuffer */
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);

    /* Fundo escuro com gradiente */
    canvas_fill_rect(0, 0, fb.width, fb.height, 0x000C2461);

    /* Logo do sistema */
    int cx = (int)fb.width / 2;
    canvas_draw_string((uint32_t)(cx - 60), 80, "Krypx OS", 0x0074B9FF, COLOR_TRANSPARENT);
    canvas_draw_string((uint32_t)(cx - 80), 110, "Sistema Seguro — Ring 0/3", 0x00636E72, COLOR_TRANSPARENT);

    /* Caixa de login */
    int bx = cx - 160;
    int by = (int)fb.height / 2 - 100;
    canvas_fill_rounded_rect((uint32_t)bx, (uint32_t)by, 320, 200, 8, 0x001E272E);
    canvas_draw_rounded_rect((uint32_t)bx, (uint32_t)by, 320, 200, 8, 0x00636E72);

    canvas_draw_string((uint32_t)(bx + 10), (uint32_t)(by + 12), "Login — Krypx", 0x00DFE6E9, COLOR_TRANSPARENT);

    /* Campo usuário */
    uint32_t ub_col = field_user ? 0x0074B9FF : 0x00636E72;
    canvas_fill_rounded_rect((uint32_t)(bx+10), (uint32_t)(by+40), 300, 30, 4, 0x00141E26);
    canvas_draw_rounded_rect((uint32_t)(bx+10), (uint32_t)(by+40), 300, 30, 4, ub_col);
    canvas_draw_string((uint32_t)(bx+16), (uint32_t)(by+48), "Usuario: ", 0x00636E72, COLOR_TRANSPARENT);
    canvas_draw_string((uint32_t)(bx+90), (uint32_t)(by+48), username, 0x00DFE6E9, COLOR_TRANSPARENT);
    if (field_user && (timer_get_ticks() % 60 < 30))
        canvas_fill_rect((uint32_t)(bx+90+(int)ulen*8), (uint32_t)(by+48), 2, 14, 0x0074B9FF);

    /* Campo senha */
    uint32_t pb_col = !field_user ? 0x0074B9FF : 0x00636E72;
    canvas_fill_rounded_rect((uint32_t)(bx+10), (uint32_t)(by+84), 300, 30, 4, 0x00141E26);
    canvas_draw_rounded_rect((uint32_t)(bx+10), (uint32_t)(by+84), 300, 30, 4, pb_col);
    canvas_draw_string((uint32_t)(bx+16), (uint32_t)(by+92), "Senha:   ", 0x00636E72, COLOR_TRANSPARENT);
    canvas_draw_string((uint32_t)(bx+90), (uint32_t)(by+92), pass_mask, 0x00DFE6E9, COLOR_TRANSPARENT);
    if (!field_user && (timer_get_ticks() % 60 < 30))
        canvas_fill_rect((uint32_t)(bx+90+(int)plen*8), (uint32_t)(by+92), 2, 14, 0x0074B9FF);

    /* Botão Entrar */
    canvas_fill_rounded_rect((uint32_t)(bx+90), (uint32_t)(by+130), 140, 32, 6, 0x000984E3);
    canvas_draw_string((uint32_t)(bx+126), (uint32_t)(by+142), "Entrar (Enter)", 0x00FFFFFF, COLOR_TRANSPARENT);

    /* Mensagem de erro */
    if (error) {
        canvas_draw_string((uint32_t)(bx+10), (uint32_t)(by+174), "Credenciais invalidas!", 0x00D63031, COLOR_TRANSPARENT);
    }

    canvas_draw_string((uint32_t)(cx - 120), (uint32_t)(by + 215),
                       "Tab: trocar campo  |  Enter: confirmar",
                       0x00636E72, COLOR_TRANSPARENT);

    fb_swap();
}

static void login_screen(void) {
    char username[USERNAME_MAX]; uint32_t ulen = 0;
    char password[64];            uint32_t plen = 0;
    char pass_mask[64];           pass_mask[0] = '\0';
    bool field_user = true;
    bool error      = false;

    memset(username, 0, sizeof(username));
    memset(password, 0, sizeof(password));

    while (1) {
        draw_login_screen(username, ulen, pass_mask, plen, field_user, error);

        char c = keyboard_getchar();
        if (!c) { __asm__ volatile("hlt"); continue; }

        if (c == '\t') {
            field_user = !field_user;
            error = false;
        } else if (c == '\n') {
            /* Tenta autenticar */
            if (user_authenticate(username, password) == 0) return; /* sucesso */
            error = true;
            plen = 0; password[0] = '\0'; pass_mask[0] = '\0';
        } else if (c == '\b') {
            if (field_user) {
                if (ulen > 0) { ulen--; username[ulen] = '\0'; }
            } else {
                if (plen > 0) { plen--; password[plen] = '\0'; pass_mask[plen] = '\0'; }
            }
            error = false;
        } else if (c >= 0x20 && c < 0x7F) {
            if (field_user && ulen < USERNAME_MAX - 1) {
                username[ulen++] = c; username[ulen] = '\0';
            } else if (!field_user && plen < 63) {
                password[plen]   = c; plen++;
                password[plen]   = '\0';
                pass_mask[plen-1] = '*'; pass_mask[plen] = '\0';
            }
            error = false;
        }
    }
}

void desktop_run(void) {
    login_screen();

    uint32_t last_render  = 0;
    uint8_t  prev_btns    = 0;

    while (1) {
        /* Renderiza a ~30 FPS */
        uint32_t now = timer_get_ticks();
        if (now - last_render >= 33) {
            last_render = now;
            desktop_render();
        }

        /* ---- Mouse ---- */
        {
            int mx = mouse_get_x();
            int my = mouse_get_y();
            uint8_t btns = mouse_get_buttons();
            int ty = (int)fb.height - TASKBAR_HEIGHT;

            /* Sempre atualiza posição do cursor */
            wm_mouse_move(mx, my);

            /* Botão esquerdo acabou de ser pressionado */
            if ((btns & 1) && !(prev_btns & 1)) {
                if (menu_open && is_in_menu(mx, my)) {
                    /* Clique dentro do menu */
                    menu_handle_click(mx, my);
                } else if (my >= ty) {
                    /* Clique na taskbar */
                    if (menu_open) menu_open = false;
                    desktop_taskbar_click(mx, my);
                } else {
                    /* Clique no desktop ou janela */
                    if (menu_open) { menu_open = false; }
                    wm_mouse_down(mx, my, 0);
                }
            }

            /* Botão esquerdo solto */
            if (!(btns & 1) && (prev_btns & 1)) {
                wm_mouse_up(mx, my, 0);
            }

            prev_btns = btns;
        }

        /* ---- Teclado ---- */
        {
            char key = keyboard_getchar();
            if (key) {
                if (key == '\x1B' || key == 'm') {
                    menu_open = !menu_open;
                } else if (key == '1' && menu_open) {
                    calculator_open();     menu_open = false;
                } else if (key == '2' && menu_open) {
                    task_manager_open();   menu_open = false;
                } else if (key == '3' && menu_open) {
                    network_manager_open();menu_open = false;
                } else if (key == '4' && menu_open) {
                    settings_open();       menu_open = false;
                } else if (key == '5' && menu_open) {
                    about_open();          menu_open = false;
                } else {
                    wm_key_event(key);
                }
            }
        }

        __asm__ volatile ("hlt");
    }
}
