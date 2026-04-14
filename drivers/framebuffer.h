/*
 * drivers/framebuffer.h — Driver Framebuffer VESA/VBE modo gráfico
 * Usa informações de framebuffer passadas pelo GRUB via Multiboot.
 * Double buffering: backbuffer em RAM, flush para VRAM.
 */
#ifndef _FRAMEBUFFER_H
#define _FRAMEBUFFER_H

#include <types.h>
#include <multiboot.h>

/* Cores ARGB 32bpp */
#define COLOR_BLACK        0x00000000
#define COLOR_WHITE        0x00FFFFFF
#define COLOR_RED          0x00FF0000
#define COLOR_GREEN        0x0000FF00
#define COLOR_BLUE         0x000000FF
#define COLOR_CYAN         0x0000FFFF
#define COLOR_YELLOW       0x00FFFF00
#define COLOR_MAGENTA      0x00FF00FF
#define COLOR_DARK_GREY    0x00333333
#define COLOR_LIGHT_GREY   0x00AAAAAA
#define COLOR_ORANGE       0x00FF8800
#define COLOR_TRANSPARENT  0xFF000000   /* Flag: não desenha */

/* Cor do tema Krypx */
#define KRYPX_BG           0x001E272E
#define KRYPX_TITLEBAR     0x002E86DE
#define KRYPX_TITLEBAR_IN  0x00636E72
#define KRYPX_ACCENT       0x0000CEC9
#define KRYPX_BUTTON       0x000984E3
#define KRYPX_TEXT         0x00DFE6E9
#define KRYPX_DESKTOP      0x000C2461
#define KRYPX_TASKBAR      0x001E272E

typedef struct {
    uint32_t *vram;        /* Ponteiro para VRAM (framebuffer hardware) */
    uint32_t *backbuf;     /* Backbuffer em RAM (double buffering) */
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;       /* Bytes por linha */
    uint8_t   bpp;         /* Bits por pixel (esperado: 32) */
    bool      ready;
} framebuffer_t;

extern framebuffer_t fb;

/* Inicializa o framebuffer com dados do Multiboot */
bool fb_init(multiboot_info_t *mbi);

/* Retorna true se o framebuffer está em modo gráfico */
bool fb_is_ready(void);

/* Define um pixel no backbuffer */
static inline void fb_putpixel(int x, int y, uint32_t color) {
    if ((uint32_t)x >= fb.width || (uint32_t)y >= fb.height) return;
    fb.backbuf[y * (fb.pitch / 4) + x] = color;
}

/* Lê um pixel do backbuffer */
static inline uint32_t fb_getpixel(int x, int y) {
    if ((uint32_t)x >= fb.width || (uint32_t)y >= fb.height) return 0;
    return fb.backbuf[y * (fb.pitch / 4) + x];
}

/* Preenche retângulo no backbuffer */
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);

/* Copia bloco de pixels para o backbuffer */
void fb_blit(int x, int y, int w, int h, const uint32_t *src);

/* Limpa o backbuffer com uma cor */
void fb_clear(uint32_t color);

/* Copia backbuffer → VRAM (vsync swap) */
void fb_swap(void);

/* Copia só regiões sujas (otimização) */
void fb_swap_region(int x, int y, int w, int h);

/* Desenha borda de retângulo */
void fb_draw_rect(int x, int y, int w, int h, uint32_t color);

#endif /* _FRAMEBUFFER_H */
