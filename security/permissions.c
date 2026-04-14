/*
 * security/permissions.c — Verificação de permissões de arquivo
 */

#include <security/permissions.h>
#include <types.h>

bool perm_check(uint32_t uid, uint32_t gid,
                uint32_t uid_owner, uint32_t gid_owner,
                uint16_t mode, uint8_t requested)
{
    /* root (uid=0) tem acesso total */
    if (uid == 0) return true;

    uint8_t effective = 0;

    if (uid == uid_owner) {
        /* Owner bits */
        if (mode & PERM_OWNER_R) effective |= 4;
        if (mode & PERM_OWNER_W) effective |= 2;
        if (mode & PERM_OWNER_X) effective |= 1;
    } else if (gid == gid_owner) {
        /* Group bits */
        if (mode & PERM_GROUP_R) effective |= 4;
        if (mode & PERM_GROUP_W) effective |= 2;
        if (mode & PERM_GROUP_X) effective |= 1;
    } else {
        /* Other bits */
        if (mode & PERM_OTHER_R) effective |= 4;
        if (mode & PERM_OTHER_W) effective |= 2;
        if (mode & PERM_OTHER_X) effective |= 1;
    }

    return (effective & requested) == requested;
}

void perm_to_string(uint16_t mode, char *out) {
    out[0] = (mode & PERM_OWNER_R) ? 'r' : '-';
    out[1] = (mode & PERM_OWNER_W) ? 'w' : '-';
    out[2] = (mode & PERM_OWNER_X) ? 'x' : '-';
    out[3] = (mode & PERM_GROUP_R) ? 'r' : '-';
    out[4] = (mode & PERM_GROUP_W) ? 'w' : '-';
    out[5] = (mode & PERM_GROUP_X) ? 'x' : '-';
    out[6] = (mode & PERM_OTHER_R) ? 'r' : '-';
    out[7] = (mode & PERM_OTHER_W) ? 'w' : '-';
    out[8] = (mode & PERM_OTHER_X) ? 'x' : '-';
    out[9] = '\0';
}
