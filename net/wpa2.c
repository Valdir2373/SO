


#include <net/wpa2.h>
#include <lib/sha1.h>
#include <lib/string.h>
#include <kernel/timer.h>


void wpa2_derive_pmk(const char *passphrase,
                     const char *ssid, uint8_t ssid_len,
                     uint8_t pmk[32]) {
    pbkdf2_hmac_sha1((const uint8_t *)passphrase, (uint32_t)strlen(passphrase),
                     (const uint8_t *)ssid, (uint32_t)ssid_len,
                     4096, pmk, 32);
}

static void prf(const uint8_t *key, uint32_t klen,
                const uint8_t *label, uint32_t llen,  
                const uint8_t *data, uint32_t dlen,
                uint8_t *out, uint32_t olen) {
    uint8_t buf[256];
    uint32_t blen = llen + 1 + dlen + 1;
    if (blen > sizeof(buf)) return;
    memcpy(buf, label, llen);
    buf[llen] = 0x00;
    memcpy(buf + llen + 1, data, dlen);

    uint32_t done = 0;
    uint8_t ctr = 0;
    while (done < olen) {
        buf[llen + 1 + dlen] = ctr;
        uint8_t mac[20];
        hmac_sha1(key, klen, buf, blen, mac);
        uint32_t copy = olen - done;
        if (copy > 20) copy = 20;
        memcpy(out + done, mac, copy);
        done += copy;
        ctr++;
    }
}


void wpa2_derive_ptk(const uint8_t pmk[32],
                     const uint8_t anonce[32], const uint8_t snonce[32],
                     const uint8_t aa[6],      const uint8_t spa[6],
                     uint8_t ptk[48]) {
    
    uint8_t data[76];
    int aa_lo = (memcmp(aa, spa, 6) < 0);
    memcpy(data,    aa_lo ? aa  : spa, 6);
    memcpy(data+6,  aa_lo ? spa : aa,  6);
    int an_lo = (memcmp(anonce, snonce, 32) < 0);
    memcpy(data+12, an_lo ? anonce : snonce, 32);
    memcpy(data+44, an_lo ? snonce : anonce, 32);

    static const uint8_t label[] = "Pairwise key expansion";
    prf(pmk, 32, label, 22, data, 76, ptk, 48);
}

bool wpa2_verify_mic(const uint8_t kck[16],
                     const uint8_t *frame, uint16_t frame_len,
                     const uint8_t expected_mic[16]) {
    uint8_t mac[20];
    hmac_sha1(kck, 16, frame, frame_len, mac);
    return (memcmp(mac, expected_mic, 16) == 0);
}

void wpa2_gen_nonce(uint8_t nonce[32]) {
    uint32_t t = timer_get_ticks();
    int i;
    for (i = 0; i < 32; i++)
        nonce[i] = (uint8_t)(t ^ (t >> (i&7)) ^ (uint8_t)(i * 0x6B));
}


static const uint8_t eapol_hdr[8] = {0xAA,0xAA,0x03,0x00,0x00,0x00,0x88,0x8E};


static void be16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static void be64(uint8_t *p, uint64_t v) {
    int i; for (i=7;i>=0;i--) { p[i]=(uint8_t)v; v>>=8; }
}


uint16_t wpa2_build_eapol_msg2(const uint8_t kck[16],
                                const uint8_t anonce[32],
                                const uint8_t snonce[32],
                                const uint8_t pmkid[16],
                                uint8_t *out, uint16_t out_max) {
    (void)anonce; (void)pmkid;
    if (out_max < 128) return 0;
    memset(out, 0, 128);

    
    out[0] = 0x01; out[1] = 0x03;
    be16(out+2, 95);   

    eapol_key_t *k = (eapol_key_t *)(out + 4);
    k->desc_type = 2;  
    uint16_t info = EAPOL_KEY_VER_HMAC_SHA1 |
                    EAPOL_KEY_INFO_PAIRWISE |
                    EAPOL_KEY_INFO_MIC;
    be16((uint8_t*)&k->key_info, info);
    be16((uint8_t*)&k->key_len, 16);
    memcpy(k->nonce, snonce, 32);
    k->key_data_len = 0;

    
    uint8_t mac[20];
    hmac_sha1(kck, 16, out, 4+95, mac);
    memcpy(k->mic, mac, 16);

    return (uint16_t)(4 + 95);
}


uint16_t wpa2_build_eapol_msg4(const uint8_t kck[16],
                                uint64_t replay_ctr,
                                uint8_t *out, uint16_t out_max) {
    if (out_max < 99) return 0;
    memset(out, 0, 99);

    out[0] = 0x01; out[1] = 0x03;
    be16(out+2, 95);

    eapol_key_t *k = (eapol_key_t *)(out + 4);
    k->desc_type = 2;
    uint16_t info = EAPOL_KEY_VER_HMAC_SHA1 |
                    EAPOL_KEY_INFO_PAIRWISE |
                    EAPOL_KEY_INFO_MIC |
                    EAPOL_KEY_INFO_SECURE;
    be16((uint8_t*)&k->key_info, info);
    be64((uint8_t*)&k->replay_ctr, replay_ctr);

    uint8_t mac[20];
    hmac_sha1(kck, 16, out, 4+95, mac);
    memcpy(k->mic, mac, 16);

    return 99;
}
