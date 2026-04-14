/*
 * security/auth.h — Autenticação de usuários
 * Hash de senha com SHA-256 simples.
 */
#ifndef _AUTH_H
#define _AUTH_H

#include <types.h>

/* Calcula hash SHA-256 da senha.
 * out deve apontar para um buffer de 32 bytes. */
void auth_hash_password(const char *password, uint8_t out[32]);

/* Verifica senha comparando hash. */
bool auth_check_password(const char *password, const uint8_t stored_hash[32]);

#endif /* _AUTH_H */
