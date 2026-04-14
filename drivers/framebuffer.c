/*
 * drivers/framebuffer.c — Driver Framebuffer com double buffering
 * Inicializado com dados do GRUB Multiboot (modo gráfico VBE).
 */

#include <drivers/framebuffer.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <types.h>

framebuffer_t fb;

/* Backbuffer estático de 1280x768x4 bytes = ~3.75 MB
 * Alocado em BSS para não inflar o binário.
 * Suporte a resoluções até 1280x1024.
 */
#define FB_MAX_WIDTH  1280
#define FB_MAX_HEIGHT 1024
static uint32_t backbuffer_storage[FB_MAX_WIDTH * FB_MAX_HEIGHT];

bool fb_init(multiboot_info_t *mbi) {
    if (!(mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER)) return false;
    if (mbi->framebuffer_type != 1) return false;  /* Não é RGB direto */
    if (mbi->framebuffer_bpp != 32 && mbi->framebuffer_bpp != 24) return false;

    fb.vram    = (uint32_t *)(uint32_t)mbi->framebuffer_addr;
    fb.width   = mbi->framebuffer_width;
    fb.height  = mbi->framebuffer_height;
    fb.pitch   = mbi->framebuffer_pitch;
    fb.bpp     = mbi->framebuffer_bpp;
    fb.ready   = true;

    /* Usa backbuffer estático (capaz de até 1280x1024) */
    if (fb.width <= FB_MAX_WIDTH && fb.height <= FB_MAX_HEIGHT) {
        fb.backbuf = backbuffer_storage;
    } else {
        fb.backbuf = fb.vram;
    }

    /* Mapeia a VRAM (pode estar em endereço alto de MMIO PCI) */
    uint32_t fb_phys = (uint32_t)mbi->framebuffer_addr;
    uint32_t fb_size = fb.pitch * fb.height;

    extern void     vmm_map_range(uint32_t *dir, uint32_t virt, uint32_t phys,
                                   uint32_t size, uint32_t flags);
    extern uint32_t *vmm_get_current_dir(void);
    vmm_map_range(vmm_get_current_dir(), fb_phys, fb_phys, fb_size,
                  0x03 /* PRESENT | WRITABLE */);

    fb_clear(0x000C2461);  /* Desktop background */
    return true;
}

bool fb_is_ready(void) { return fb.ready; }

void fb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (!fb.backbuf) return;
    int px, py;
    uint32_t pitch32 = fb.pitch / 4;
    for (py = y; py < y + h; py++) {
        if ((uint32_t)py >= fb.height) break;
        if (py < 0) continue;
        for (px = x; px < x + w; px++) {
            if ((uint32_t)px >= fb.width) break;
            if (px < 0) continue;
            fb.backbuf[py * pitch32 + px] = color;
        }
    }
}

void fb_draw_rect(int x, int y, int w, int h, uint32_t color) {
    fb_fill_rect(x,       y,       w, 1, color);  /* Topo */
    fb_fill_rect(x,       y+h-1,   w, 1, color);  /* Fundo */
    fb_fill_rect(x,       y,       1, h, color);  /* Esquerda */
    fb_fill_rect(x+w-1,   y,       1, h, color);  /* Direita */
}

void fb_blit(int x, int y, int w, int h, const uint32_t *src) {
    if (!fb.backbuf || !src) return;
    uint32_t pitch32 = fb.pitch / 4;
    int py, px;
    for (py = 0; py < h; py++) {
        int dst_y = y + py;
        if (dst_y < 0 || (uint32_t)dst_y >= fb.height) continue;
        for (px = 0; px < w; px++) {
            int dst_x = x + px;
            if (dst_x < 0 || (uint32_t)dst_x >= fb.width) continue;
            uint32_t color = src[py * w + px];
            if (color == COLOR_TRANSPARENT) continue;
            fb.backbuf[dst_y * pitch32 + dst_x] = color;
        }
    }
}

void fb_clear(uint32_t color) {
    uint32_t total = (fb.pitch / 4) * fb.height;
    uint32_t i;
    for (i = 0; i < total; i++) fb.backbuf[i] = color;
}

void fb_swap(void) {
    if (fb.backbuf == fb.vram) return;  /* Sem double buffering */
    uint32_t bytes = fb.pitch * fb.height;
    memcpy(fb.vram, fb.backbuf, bytes);
}

void fb_swap_region(int x, int y, int w, int h) {
    if (fb.backbuf == fb.vram) return;
    uint32_t pitch32 = fb.pitch / 4;
    int py;
    for (py = y; py < y + h && (uint32_t)py < fb.height; py++) {
        if (py < 0) continue;
        uint32_t *src = fb.backbuf + py * pitch32 + x;
        uint32_t *dst = fb.vram   + py * pitch32 + x;
        uint32_t  len = (uint32_t)w;
        if ((uint32_t)(x + w) > fb.width) len = fb.width - x;
        memcpy(dst, src, len * 4);
    }
}
