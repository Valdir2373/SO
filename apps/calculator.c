/*
 * apps/calculator.c — Calculadora com interface gráfica
 * Grade 4x5 de botões: 0-9, +, -, *, /, =, C, ., +/-
 */

#include <apps/calculator.h>
#include <gui/window.h>
#include <gui/canvas.h>
#include <drivers/framebuffer.h>
#include <lib/string.h>
#include <types.h>

#define CALC_W  220
#define CALC_H  290
#define DISP_H   48
#define BTN_W    46
#define BTN_H    38
#define BTN_PAD   4
#define BTN_COLS  4
#define BTN_ROWS  5

static window_t *calc_win = NULL;

/* Estado da calculadora */
static char  disp[32];        /* Display atual */
static int   disp_len = 0;
static double acc = 0;        /* Acumulador */
static double cur = 0;        /* Número atual */
static char  pending_op = 0;  /* +, -, *, / */
static bool  new_number = true;

/* Botões: label, tipo */
static const char *btn_labels[BTN_ROWS][BTN_COLS] = {
    {"C",  "+/-", "%",  "/"},
    {"7",  "8",   "9",  "*"},
    {"4",  "5",   "6",  "-"},
    {"1",  "2",   "3",  "+"},
    {"0",  ".",   "=",  "="},
};

/* Converte double para string (sem printf/libc) */
static void double_to_str(double v, char *out, int maxlen) {
    if (v < 0) { *out++ = '-'; v = -v; maxlen--; }
    int64_t iv = (int64_t)v;
    double frac = v - (double)iv;

    /* Converte parte inteira */
    char tmp[32]; int tlen = 0;
    if (iv == 0) { tmp[tlen++] = '0'; }
    else {
        int64_t t = iv;
        while (t > 0 && tlen < 20) { tmp[tlen++] = (char)('0' + t % 10); t /= 10; }
    }
    int i;
    for (i = tlen - 1; i >= 0 && maxlen > 1; i--, maxlen--) *out++ = tmp[i];

    /* Se há fração significativa, mostra até 4 casas */
    if (frac > 0.00001 && maxlen > 2) {
        *out++ = '.'; maxlen--;
        int decimals = 4;
        while (decimals-- > 0 && maxlen > 1) {
            frac *= 10;
            int d = (int)frac;
            *out++ = (char)('0' + d);
            frac -= d;
            maxlen--;
        }
        /* Remove zeros finais */
        while (*(out-1) == '0') out--;
        if (*(out-1) == '.') out--;
    }
    *out = '\0';
}

static void calc_apply_op(void) {
    switch (pending_op) {
        case '+': acc = acc + cur; break;
        case '-': acc = acc - cur; break;
        case '*': acc = acc * cur; break;
        case '/': if (cur != 0) acc = acc / cur; else acc = 0; break;
        default:  acc = cur; break;
    }
    double_to_str(acc, disp, sizeof(disp));
    disp_len = strlen(disp);
    new_number = true;
}

static void calc_handle_key(const char *label) {
    if (label[0] >= '0' && label[0] <= '9') {
        if (new_number) { disp_len = 0; disp[0] = '\0'; new_number = false; }
        if (disp_len < 14) { disp[disp_len++] = label[0]; disp[disp_len] = '\0'; }
        /* Parse current number */
        double v = 0; int neg = 0;
        const char *p = disp;
        if (*p == '-') { neg = 1; p++; }
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
        if (*p == '.') {
            p++; double fac = 0.1;
            while (*p >= '0' && *p <= '9') { v += (*p - '0') * fac; fac *= 0.1; p++; }
        }
        cur = neg ? -v : v;
    } else if (label[0] == '.') {
        if (new_number) { disp[0]='0'; disp[1]='.'; disp[2]='\0'; disp_len=2; new_number=false; }
        else if (!strchr(disp, '.') && disp_len < 14) {
            disp[disp_len++] = '.'; disp[disp_len] = '\0';
        }
    } else if (label[0] == 'C') {
        acc = 0; cur = 0; pending_op = 0; new_number = true;
        disp[0] = '0'; disp[1] = '\0'; disp_len = 1;
    } else if (label[0] == '=') {
        calc_apply_op();
        pending_op = 0;
    } else if (label[0] == '+' || label[0] == '-' || label[0] == '*' || label[0] == '/') {
        if (pending_op) calc_apply_op();
        else { acc = cur; }
        pending_op = label[0];
        new_number = true;
    } else if (label[0] == '+' && label[1] == '/') {
        /* Toggle sign */
        if (disp[0] == '-') { memmove(disp, disp+1, disp_len); disp_len--; }
        else { memmove(disp+1, disp, disp_len+1); disp[0] = '-'; disp_len++; }
    } else if (label[0] == '%') {
        cur = cur / 100.0;
        double_to_str(cur, disp, sizeof(disp));
        disp_len = strlen(disp);
    }
}

static void calc_on_paint(window_t *win) {
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);
    int bx = win->content_x, by = win->content_y;

    /* Fundo */
    canvas_fill_rect(bx, by, win->content_w, win->content_h, 0x001E272E);

    /* Display */
    canvas_fill_rect(bx, by, win->content_w, DISP_H, 0x00141E26);
    canvas_draw_rect(bx, by, win->content_w, DISP_H, 0x00636E72);
    /* Texto alinhado à direita */
    int dw = disp_len * CHAR_WIDTH;
    int dx = bx + win->content_w - dw - 8;
    canvas_draw_string(dx, by + (DISP_H - CHAR_HEIGHT)/2, disp, 0x00FFFFFF, COLOR_TRANSPARENT);

    /* Botões */
    int row, col;
    for (row = 0; row < BTN_ROWS; row++) {
        for (col = 0; col < BTN_COLS; col++) {
            /* A última linha: botão "0" ocupa col 0-1, "." col 2, "=" col 3 */
            if (row == BTN_ROWS-1 && col == 1) continue; /* skip — 0 é largo */

            const char *lbl = btn_labels[row][col];
            int bw = (row == BTN_ROWS-1 && col == 0) ? BTN_W*2 + BTN_PAD : BTN_W;
            int px = bx + col * (BTN_W + BTN_PAD) + BTN_PAD;
            int py = by + DISP_H + BTN_PAD + row * (BTN_H + BTN_PAD);

            /* Cor do botão */
            uint32_t btn_col;
            if (lbl[0] == 'C' || lbl[0] == '+' || lbl[0] == '%' || lbl[0] == '/') {
                btn_col = 0x00555555;
            } else if (lbl[0] == '=' && lbl[1] == '\0') {
                btn_col = 0x000984E3;
            } else if (lbl[0] == '*' || lbl[0] == '-' || lbl[0] == '+') {
                btn_col = 0x00FF9500;
            } else {
                btn_col = 0x00333333;
            }

            canvas_fill_rounded_rect(px, py, bw, BTN_H, 4, btn_col);
            /* Label centrada */
            int llen = strlen(lbl);
            int lx = px + (bw - llen * CHAR_WIDTH) / 2;
            int ly = py + (BTN_H - CHAR_HEIGHT) / 2;
            canvas_draw_string(lx, ly, lbl, 0x00FFFFFF, COLOR_TRANSPARENT);
        }
    }
}

static void calc_on_keydown(window_t *win, char c) {
    (void)win;
    char lbl[3] = {c, '\0', '\0'};
    if (c == '\b' || c == 27) {
        /* Backspace ou Esc: C */
        const char *cl = "C";
        calc_handle_key(cl);
    } else if (c == '\n') {
        const char *el = "=";
        calc_handle_key(el);
    } else if (c >= '0' && c <= '9') {
        calc_handle_key(lbl);
    } else if (c == '+' || c == '-' || c == '*' || c == '/') {
        calc_handle_key(lbl);
    } else if (c == '.') {
        calc_handle_key(lbl);
    }
}

void calculator_open(void) {
    if (calc_win && calc_win->used) {
        wm_focus(calc_win);
        return;
    }

    /* Inicializa estado */
    acc = 0; cur = 0; pending_op = 0; new_number = true;
    disp[0] = '0'; disp[1] = '\0'; disp_len = 1;

    calc_win = wm_create("Calculadora",
                          fb.width/2 - CALC_W/2,
                          fb.height/2 - CALC_H/2,
                          CALC_W, CALC_H, 0);
    if (!calc_win) return;
    calc_win->bg_color   = 0x001E272E;
    calc_win->on_paint   = calc_on_paint;
    calc_win->on_keydown = calc_on_keydown;
}
