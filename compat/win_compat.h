/*
 * compat/win_compat.h — Compatibilidade Windows PE (stub)
 *
 * Detecção de binários .exe e mensagem de status.
 * Implementação completa (PE loader + Win32 API translation) é futura.
 */
#ifndef _COMPAT_WIN_H
#define _COMPAT_WIN_H

#include <types.h>

/* Inicializa o subsistema Windows compat (reservado para uso futuro) */
void win_compat_init(void);

/*
 * Tenta "executar" um binário PE.
 * Atualmente exibe mensagem informativa e retorna false.
 */
bool win_compat_load(const uint8_t *data, size_t size);

#endif /* _COMPAT_WIN_H */
