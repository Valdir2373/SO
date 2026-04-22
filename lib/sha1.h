
#ifndef _LIB_SHA1_H
#define _LIB_SHA1_H

#include <types.h>

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t  buf[64];
} sha1_ctx_t;

void sha1_init(sha1_ctx_t *ctx);
void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, uint32_t len);
void sha1_final(sha1_ctx_t *ctx, uint8_t digest[20]);
void sha1(const uint8_t *data, uint32_t len, uint8_t digest[20]);

void hmac_sha1(const uint8_t *key, uint32_t klen,
               const uint8_t *msg, uint32_t mlen,
               uint8_t mac[20]);


void pbkdf2_hmac_sha1(const uint8_t *pass, uint32_t plen,
                      const uint8_t *salt, uint32_t slen,
                      uint32_t iters, uint8_t *out, uint32_t olen);

#endif
