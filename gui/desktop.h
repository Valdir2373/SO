/*
 * gui/desktop.h — Desktop com taskbar e ícones
 */
#ifndef _DESKTOP_H
#define _DESKTOP_H

#include <types.h>
#include <multiboot.h>

/* Inicializa o desktop (fundo + taskbar) */
void desktop_init(void);

/* Renderiza o loop principal do desktop */
void desktop_run(void);

/* Desenha o desktop e taskbar no backbuffer */
void desktop_render(void);

#endif /* _DESKTOP_H */
