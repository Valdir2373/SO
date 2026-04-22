
#ifndef _LIB_AES_H
#define _LIB_AES_H

#include <types.h>


typedef struct { uint32_t rk[44]; } aes128_ctx_t;

void aes128_init(aes128_ctx_t *ctx, const uint8_t key[16]);
void aes128_encrypt(const aes128_ctx_t *ctx, const uint8_t in[16], uint8_t out[16]);
void aes128_decrypt(const aes128_ctx_t *ctx, const uint8_t in[16], uint8_t out[16]);

void aes_ccmp_encrypt(const uint8_t key[16],
                      const uint8_t nonce[13],
                      const uint8_t *aad, uint16_t aad_len,
                      uint8_t *data, uint16_t data_len,
                      uint8_t mic_out[8]);

int  aes_ccmp_decrypt(const uint8_t key[16],
                      const uint8_t nonce[13],
                      const uint8_t *aad, uint16_t aad_len,
                      uint8_t *data, uint16_t data_len,
                      const uint8_t mic_in[8]);

#endif
