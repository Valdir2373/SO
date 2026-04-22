


#include <lib/sha1.h>
#include <lib/string.h>

#define ROL32(v,n) (((v)<<(n)) | ((v)>>(32-(n))))

static void sha1_transform(uint32_t s[5], const uint8_t b[64]) {
    uint32_t w[80], a, b2, c, d, e, t;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)b[i*4]<<24)|((uint32_t)b[i*4+1]<<16)|
               ((uint32_t)b[i*4+2]<<8)|(uint32_t)b[i*4+3];
    for (; i < 80; i++)
        w[i] = ROL32(1, w[i-3]^w[i-8]^w[i-14]^w[i-16]);
    a=s[0]; b2=s[1]; c=s[2]; d=s[3]; e=s[4];
    for (i = 0; i < 80; i++) {
        uint32_t f, k;
        if      (i<20){ f=(b2&c)|((~b2)&d); k=0x5A827999U; }
        else if (i<40){ f=b2^c^d;            k=0x6ED9EBA1U; }
        else if (i<60){ f=(b2&c)|(b2&d)|(c&d); k=0x8F1BBCDCU; }
        else           { f=b2^c^d;            k=0xCA62C1D6U; }
        t=ROL32(5,a)+f+e+k+w[i]; e=d; d=c; c=ROL32(30,b2); b2=a; a=t;
    }
    s[0]+=a; s[1]+=b2; s[2]+=c; s[3]+=d; s[4]+=e;
}

void sha1_init(sha1_ctx_t *ctx) {
    ctx->state[0]=0x67452301U; ctx->state[1]=0xEFCDAB89U;
    ctx->state[2]=0x98BADCFEU; ctx->state[3]=0x10325476U;
    ctx->state[4]=0xC3D2E1F0U;
    ctx->count[0]=ctx->count[1]=0;
}

void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    uint32_t i, j = (ctx->count[0]>>3)&63;
    if ((ctx->count[0] += len<<3) < (len<<3)) ctx->count[1]++;
    ctx->count[1] += len>>29;
    if (j+len >= 64) {
        uint32_t k = 64-j;
        memcpy(ctx->buf+j, data, k);
        sha1_transform(ctx->state, ctx->buf);
        for (i = k; i+63 < len; i += 64)
            sha1_transform(ctx->state, data+i);
        j = 0;
    } else { i = 0; }
    memcpy(ctx->buf+j, data+i, len-i);
}

void sha1_final(sha1_ctx_t *ctx, uint8_t digest[20]) {
    uint8_t fin[8];
    int i;
    for (i=0;i<8;i++) fin[i]=(uint8_t)(i<4 ? ctx->count[1]>>(24-i*8) : ctx->count[0]>>(56-(i*8)));
    uint8_t c = 0x80;
    sha1_update(ctx, &c, 1);
    while ((ctx->count[0]>>3&63) != 56) { c=0; sha1_update(ctx, &c, 1); }
    sha1_update(ctx, fin, 8);
    for (i=0;i<5;i++) {
        digest[i*4]  =(uint8_t)(ctx->state[i]>>24);
        digest[i*4+1]=(uint8_t)(ctx->state[i]>>16);
        digest[i*4+2]=(uint8_t)(ctx->state[i]>>8);
        digest[i*4+3]=(uint8_t)(ctx->state[i]);
    }
}

void sha1(const uint8_t *data, uint32_t len, uint8_t digest[20]) {
    sha1_ctx_t ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, digest);
}

void hmac_sha1(const uint8_t *key, uint32_t klen,
               const uint8_t *msg, uint32_t mlen,
               uint8_t mac[20]) {
    uint8_t k0[64], ipad[64], opad[64], inner[20];
    int i;
    memset(k0, 0, 64);
    if (klen > 64) sha1(key, klen, k0);
    else memcpy(k0, key, klen);
    for (i=0;i<64;i++) { ipad[i]=k0[i]^0x36; opad[i]=k0[i]^0x5C; }
    sha1_ctx_t ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, ipad, 64);
    sha1_update(&ctx, msg, mlen);
    sha1_final(&ctx, inner);
    sha1_init(&ctx);
    sha1_update(&ctx, opad, 64);
    sha1_update(&ctx, inner, 20);
    sha1_final(&ctx, mac);
}


void pbkdf2_hmac_sha1(const uint8_t *pass, uint32_t plen,
                      const uint8_t *salt, uint32_t slen,
                      uint32_t iters, uint8_t *out, uint32_t olen) {
    uint32_t block = 1, done = 0;
    while (done < olen) {
        
        uint8_t s2[256];
        uint32_t s2len = slen + 4;
        if (slen < 252) {
            memcpy(s2, salt, slen);
            s2[slen]  =(uint8_t)(block>>24);
            s2[slen+1]=(uint8_t)(block>>16);
            s2[slen+2]=(uint8_t)(block>>8);
            s2[slen+3]=(uint8_t)(block);
        }
        uint8_t u[20], t[20];
        hmac_sha1(pass, plen, s2, s2len, u);
        memcpy(t, u, 20);
        uint32_t j;
        for (j = 1; j < iters; j++) {
            hmac_sha1(pass, plen, u, 20, u);
            int k; for (k=0;k<20;k++) t[k]^=u[k];
        }
        uint32_t copy = olen - done;
        if (copy > 20) copy = 20;
        memcpy(out + done, t, copy);
        done += copy;
        block++;
    }
}
