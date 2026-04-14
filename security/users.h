/*
 * security/users.h — Gerenciamento de usuários do Krypx
 * Suporte a múltiplos usuários com hash de senha SHA-256.
 */
#ifndef _USERS_H
#define _USERS_H

#include <types.h>

#define MAX_USERS        8
#define USERNAME_MAX    32
#define HOME_DIR_MAX    64
#define PASSWORD_HASH_SZ 32   /* SHA-256 = 256 bits = 32 bytes */

/* Níveis de privilégio */
#define PRIV_USER   0
#define PRIV_ADMIN  1
#define PRIV_ROOT   2

/* Tabela de usuários */
typedef struct {
    bool     active;
    uint32_t uid;
    uint32_t gid;
    char     username[USERNAME_MAX];
    uint8_t  password_hash[PASSWORD_HASH_SZ];
    char     home_dir[HOME_DIR_MAX];
    uint8_t  privileges;
} user_t;

/* Usuário logado atualmente */
extern user_t *current_user;

/* Cria usuário. Retorna UID ou -1 em erro. */
int user_create(const char *username, const char *password, uint8_t priv);

/* Autentica usuário. Retorna 0 se OK, -1 se falhar. */
int user_authenticate(const char *username, const char *password);

/* Retorna ponteiro para usuário pelo nome (NULL se não encontrado). */
user_t *user_find(const char *username);

/* Retorna ponteiro para usuário pelo UID (NULL se não encontrado). */
user_t *user_find_by_uid(uint32_t uid);

/* Troca para outro usuário (requer autenticação prévia). */
void user_switch(uint32_t uid);

/* Inicializa subsistema de usuários e cria root padrão. */
void users_init(void);

#endif /* _USERS_H */
