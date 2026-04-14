/*
 * gui/canvas.h — Primitivas de desenho 2D
 * Bresenham para linhas, círculos, rounded rects, texto bitmap.
 */
#ifndef _CANVAS_H
#define _CANVAS_H

#include <types.h>

/* Inicializa canvas apontando para o framebuffer */
void canvas_init(uint32_t *buf, uint32_t width, uint32_t height, uint32_t pitch);

/* Primitivas */
void canvas_putpixel(int x, int y, uint32_t color);
void canvas_fill_rect(int x, int y, int w, int h, uint32_t color);
void canvas_draw_rect(int x, int y, int w, int h, uint32_t color);
void canvas_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void canvas_draw_circle(int cx, int cy, int r, uint32_t color);
void canvas_fill_circle(int cx, int cy, int r, uint32_t color);
void canvas_draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);
void canvas_fill_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);

/* Gradiente vertical */
void canvas_fill_gradient(int x, int y, int w, int h,
                           uint32_t color_top, uint32_t color_bot);

/* Desenha um caractere bitmap 8x16 */
void canvas_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);

/* Desenha uma string */
void canvas_draw_string(int x, int y, const char *str, uint32_t fg, uint32_t bg);

/* Largura de uma string em pixels */
int canvas_string_width(const char *str);

/* Altura do caractere */
#define CHAR_HEIGHT 16
#define CHAR_WIDTH   8

#endif /* _CANVAS_H */
