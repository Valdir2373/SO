/*
 * security/users.c — Gerenciamento de usuários do Krypx
 * Mantém tabela de usuários na RAM. Root é criado na inicialização.
 */

#include <security/users.h>
#include <security/auth.h>
#include <lib/string.h>
#include <types.h>

static user_t user_table[MAX_USERS];
static uint32_t user_count = 0;
static uint32_t next_uid   = 0;

user_t *current_user = NULL;

void users_init(void) {
    memset(user_table, 0, sizeof(user_table));
    user_count = 0;
    next_uid   = 0;
    current_user = NULL;

    /* Cria usuário root com senha "krypx" */
    user_create("root", "krypx", PRIV_ROOT);
    /* Faz login automático como root na inicialização */
    current_user = user_find("root");
}

int user_create(const char *username, const char *password, uint8_t priv) {
    if (user_count >= MAX_USERS) return -1;
    if (!username || !password) return -1;

    /* Verifica duplicata */
    if (user_find(username)) return -1;

    uint32_t idx = user_count++;
    user_table[idx].active     = true;
    user_table[idx].uid        = next_uid++;
    user_table[idx].gid        = user_table[idx].uid;
    user_table[idx].privileges = priv;

    /* Copia username com tamanho limitado */
    uint32_t i;
    for (i = 0; i < USERNAME_MAX - 1 && username[i]; i++)
        user_table[idx].username[i] = username[i];
    user_table[idx].username[i] = '\0';

    /* Home dir: /home/username ou /root para root */
    if (priv == PRIV_ROOT) {
        memcpy(user_table[idx].home_dir, "/root", 6);
    } else {
        memcpy(user_table[idx].home_dir, "/home/", 6);
        uint32_t j = 6;
        for (i = 0; i < USERNAME_MAX - 8 && username[i]; i++, j++)
            user_table[idx].home_dir[j] = username[i];
        user_table[idx].home_dir[j] = '\0';
    }

    auth_hash_password(password, user_table[idx].password_hash);
    return (int)user_table[idx].uid;
}

int user_authenticate(const char *username, const char *password) {
    user_t *u = user_find(username);
    if (!u) return -1;
    if (!auth_check_password(password, u->password_hash)) return -1;
    current_user = u;
    return 0;
}

user_t *user_find(const char *username) {
    uint32_t i;
    for (i = 0; i < user_count; i++) {
        if (user_table[i].active && strcmp(user_table[i].username, username) == 0)
            return &user_table[i];
    }
    return NULL;
}

user_t *user_find_by_uid(uint32_t uid) {
    uint32_t i;
    for (i = 0; i < user_count; i++) {
        if (user_table[i].active && user_table[i].uid == uid)
            return &user_table[i];
    }
    return NULL;
}

void user_switch(uint32_t uid) {
    user_t *u = user_find_by_uid(uid);
    if (u) current_user = u;
}
