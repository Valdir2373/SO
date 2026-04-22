


#include <net/wifi.h>
#include <net/wpa2.h>
#include <net/dhcp.h>
#include <net/netif.h>
#include <lib/string.h>
#include <kernel/timer.h>


wifi_network_t g_wifi_networks[WIFI_MAX_NETWORKS];
int            g_wifi_network_count = 0;
wifi_state_t   g_wifi_state         = WIFI_STATE_IDLE;
char           g_wifi_status_msg[80] = "";
int            g_wifi_connected_idx  = -1;


static uint8_t g_pmk[32];
static uint8_t g_ptk[48];   
static uint8_t g_snonce[32];
static uint8_t g_anonce[32];
static uint64_t g_replay_ctr;
static int     g_connect_idx;
static uint32_t g_state_timer;


extern void rtl8188eu_send_frame(const uint8_t *frame, uint16_t len);




static uint16_t build_auth_req(const uint8_t *bssid, const uint8_t *my_mac,
                                uint8_t *out) {
    memset(out, 0, 30);
    out[0] = 0xB0; out[1] = 0x00;  
    out[2] = 0x00; out[3] = 0x00;  
    memcpy(out+4,  bssid,  6);      
    memcpy(out+10, my_mac, 6);      
    memcpy(out+16, bssid,  6);      
    out[22]=0x00; out[23]=0x00;     
    
    out[24]=0; out[25]=0; out[26]=1; out[27]=0; out[28]=0; out[29]=0;
    return 30;
}


static uint16_t build_assoc_req(const uint8_t *bssid, const uint8_t *my_mac,
                                  const char *ssid, uint8_t ssid_len,
                                  uint8_t *out) {
    uint16_t pos = 0;
    memset(out, 0, 256);
    out[pos++]=0x00; out[pos++]=0x00; 
    out[pos++]=0x00; out[pos++]=0x00; 
    memcpy(out+pos, bssid,  6); pos+=6; 
    memcpy(out+pos, my_mac, 6); pos+=6; 
    memcpy(out+pos, bssid,  6); pos+=6; 
    out[pos++]=0x00; out[pos++]=0x00;   
    
    out[pos++]=0x31; out[pos++]=0x04;
    out[pos++]=0x0A; out[pos++]=0x00;
    
    out[pos++]=0x00; out[pos++]=ssid_len;
    memcpy(out+pos, ssid, ssid_len); pos+=ssid_len;
    
    out[pos++]=0x01; out[pos++]=0x08;
    out[pos++]=0x82; out[pos++]=0x84; out[pos++]=0x8B; out[pos++]=0x96;
    out[pos++]=0x24; out[pos++]=0x30; out[pos++]=0x48; out[pos++]=0x6C;
    
    out[pos++]=0x30; out[pos++]=0x14;
    out[pos++]=0x01; out[pos++]=0x00; 
    
    out[pos++]=0x00; out[pos++]=0x0F; out[pos++]=0xAC; out[pos++]=0x04;
    
    out[pos++]=0x01; out[pos++]=0x00;
    out[pos++]=0x00; out[pos++]=0x0F; out[pos++]=0xAC; out[pos++]=0x04;
    
    out[pos++]=0x01; out[pos++]=0x00;
    out[pos++]=0x00; out[pos++]=0x0F; out[pos++]=0xAC; out[pos++]=0x02;
    
    out[pos++]=0x00; out[pos++]=0x00;
    return pos;
}


static uint16_t build_eapol_data_frame(const uint8_t *bssid, const uint8_t *my_mac,
                                        const uint8_t *eapol, uint16_t elen,
                                        uint8_t *out) {
    uint16_t pos = 0;
    out[pos++]=0x08; out[pos++]=0x01; 
    out[pos++]=0x00; out[pos++]=0x00;
    memcpy(out+pos, bssid, 6);  pos+=6; 
    memcpy(out+pos, my_mac, 6); pos+=6; 
    memcpy(out+pos, bssid, 6);  pos+=6; 
    out[pos++]=0x00; out[pos++]=0x00;
    
    out[pos++]=0xAA; out[pos++]=0xAA; out[pos++]=0x03;
    out[pos++]=0x00; out[pos++]=0x00; out[pos++]=0x00;
    out[pos++]=0x88; out[pos++]=0x8E;
    memcpy(out+pos, eapol, elen); pos+=elen;
    return pos;
}



void wifi_init(void) {
    g_wifi_state        = WIFI_STATE_IDLE;
    g_wifi_network_count= 0;
    g_wifi_connected_idx= -1;
    g_wifi_status_msg[0]= '\0';
}

void wifi_scan(void) {
    g_wifi_network_count = 0;
    g_wifi_state = WIFI_STATE_SCANNING;
    g_state_timer = timer_get_ticks();
    memcpy(g_wifi_status_msg, "Procurando redes...", 20);
}

void wifi_connect(int idx, const char *password) {
    if (idx < 0 || idx >= g_wifi_network_count) return;
    wifi_network_t *net = &g_wifi_networks[idx];
    g_connect_idx = idx;

    
    if (net->security == WIFI_SEC_WPA2 && password && password[0]) {
        wpa2_derive_pmk(password, net->ssid, (uint8_t)strlen(net->ssid), g_pmk);
    } else {
        memset(g_pmk, 0, 32);
    }

    
    wpa2_gen_nonce(g_snonce);

    g_wifi_state = WIFI_STATE_CONNECTING;
    g_state_timer = timer_get_ticks();
    memcpy(g_wifi_status_msg, "Conectando...", 14);
}

void wifi_disconnect(void) {
    g_wifi_state        = WIFI_STATE_IDLE;
    g_wifi_connected_idx= -1;
    memcpy(g_wifi_status_msg, "Desconectado", 13);
}

bool wifi_is_connected(void) {
    return g_wifi_state == WIFI_STATE_CONNECTED;
}


void wifi_on_beacon(const uint8_t *bssid, const char *ssid, uint8_t ssid_len,
                    int8_t rssi, uint8_t channel, wifi_security_t sec) {
    
    int i;
    for (i = 0; i < g_wifi_network_count; i++) {
        if (memcmp(g_wifi_networks[i].bssid, bssid, 6) == 0) {
            g_wifi_networks[i].rssi    = rssi;
            g_wifi_networks[i].channel = channel;
            return;
        }
    }
    if (g_wifi_network_count >= WIFI_MAX_NETWORKS) return;
    wifi_network_t *n = &g_wifi_networks[g_wifi_network_count++];
    if (ssid_len >= WIFI_SSID_LEN) ssid_len = WIFI_SSID_LEN-1;
    memcpy(n->ssid, ssid, ssid_len);
    n->ssid[ssid_len] = '\0';
    memcpy(n->bssid, bssid, 6);
    n->rssi    = rssi;
    n->channel = channel;
    n->security= sec;
    n->valid   = true;
}


void wifi_on_eapol(const uint8_t *data, uint16_t len) {
    if (len < sizeof(eapol_key_t)+4) return;
    const eapol_key_t *k = (const eapol_key_t *)(data + 4);
    uint16_t info;
    info = ((uint16_t)((uint8_t*)&k->key_info)[0]<<8)|((uint8_t*)&k->key_info)[1];

    wifi_network_t *net = &g_wifi_networks[g_connect_idx];
    extern uint8_t net_mac[6];

    if ((info & EAPOL_KEY_INFO_ACK) && !(info & EAPOL_KEY_INFO_MIC)) {
        
        memcpy(g_anonce, k->nonce, 32);

        
        g_replay_ctr = 0;
        int i; for(i=0;i<8;i++) g_replay_ctr=(g_replay_ctr<<8)|((uint8_t*)&k->replay_ctr)[i];

        
        wpa2_derive_ptk(g_pmk, g_anonce, g_snonce, net->bssid, net_mac, g_ptk);

        
        uint8_t eapol_buf[256];
        uint16_t elen = wpa2_build_eapol_msg2(g_ptk, g_anonce, g_snonce, 0,
                                               eapol_buf, sizeof(eapol_buf));
        uint8_t frame[512];
        uint16_t flen = build_eapol_data_frame(net->bssid, net_mac,
                                                eapol_buf, elen, frame);
        wifi_driver_send(frame, flen);
        g_wifi_state = WIFI_STATE_HANDSHAKE;
        memcpy(g_wifi_status_msg, "Handshake WPA2...", 18);
    }
    else if ((info & EAPOL_KEY_INFO_MIC) && (info & EAPOL_KEY_INFO_ACK) &&
             (info & EAPOL_KEY_INFO_INSTALL)) {
        
        
        int i; for(i=0;i<8;i++) g_replay_ctr=(g_replay_ctr<<8)|((uint8_t*)&k->replay_ctr)[i];

        uint8_t eapol_buf[256];
        uint16_t elen = wpa2_build_eapol_msg4(g_ptk, g_replay_ctr, eapol_buf, sizeof(eapol_buf));
        uint8_t frame[512];
        uint16_t flen = build_eapol_data_frame(net->bssid, net_mac, eapol_buf, elen, frame);
        wifi_driver_send(frame, flen);

        
        g_wifi_state        = WIFI_STATE_CONNECTED;
        g_wifi_connected_idx= g_connect_idx;
        memcpy(g_wifi_status_msg, "Conectado! Obtendo IP...", 25);
        dhcp_request();
    }
}

void wifi_on_assoc_ok(void) {
    g_wifi_state = WIFI_STATE_HANDSHAKE;
    memcpy(g_wifi_status_msg, "Associado. Aguardando WPA2...", 30);
}

void wifi_on_assoc_fail(void) {
    g_wifi_state = WIFI_STATE_ERROR;
    memcpy(g_wifi_status_msg, "Erro: falha na associação", 26);
}


void wifi_rx_frame(const uint8_t *frame, uint16_t len) {
    if (len < 24) return;
    uint8_t fc0 = frame[0], fc1 = frame[1];
    uint8_t type    = (fc0 >> 2) & 0x03;
    uint8_t subtype = (fc0 >> 4) & 0x0F;

    if (type == 0) { 
        if (subtype == 8 || subtype == 5) { 
            if (g_wifi_state == WIFI_STATE_SCANNING) {
                
                const uint8_t *bssid = frame + 16;
                if (len < 36) return;
                const uint8_t *body = frame + 36;
                uint16_t blen = (uint16_t)(len - 36);
                
                char ssid[33]; uint8_t ssid_len = 0;
                uint8_t channel = 0;
                wifi_security_t sec = WIFI_SEC_OPEN;
                uint16_t i = 0;
                while (i+1 < blen) {
                    uint8_t id = body[i], elen = body[i+1];
                    if (i+2+elen > blen) break;
                    if (id == 0 && elen <= 32) { 
                        memcpy(ssid, body+i+2, elen);
                        ssid_len = elen;
                        ssid[elen] = '\0';
                    } else if (id == 3 && elen == 1) { 
                        channel = body[i+2];
                    } else if (id == 48) { 
                        sec = WIFI_SEC_WPA2;
                    } else if (id == 221 && elen >= 4 &&
                               body[i+2]==0x00 && body[i+3]==0x50 &&
                               body[i+4]==0xF2 && body[i+5]==0x01) {
                        sec = WIFI_SEC_WPA; 
                    }
                    i += 2 + elen;
                }
                int8_t rssi = -70; 
                if (ssid_len > 0)
                    wifi_on_beacon(bssid, ssid, ssid_len, rssi, channel, sec);
            }
        } else if (subtype == 0x0B) { 
            uint16_t seq    = (uint16_t)((frame[26])|(frame[27]<<8));
            uint16_t status = (uint16_t)((frame[28])|(frame[29]<<8));
            if (seq == 2 && status == 0) {
                
                wifi_network_t *net = &g_wifi_networks[g_connect_idx];
                extern uint8_t net_mac[6];
                uint8_t out[256];
                uint16_t flen = build_assoc_req(net->bssid, net_mac,
                                                net->ssid, (uint8_t)strlen(net->ssid), out);
                wifi_driver_send(out, flen);
                g_wifi_state = WIFI_STATE_ASSOCIATING;
            } else {
                wifi_on_assoc_fail();
            }
        } else if (subtype == 0x01) { 
            uint16_t status = (uint16_t)((frame[26])|(frame[27]<<8));
            if (status == 0) wifi_on_assoc_ok();
            else             wifi_on_assoc_fail();
        }
    } else if (type == 2) { 
        
        uint16_t hdr_len = 24;
        if (fc1 & 0x08) hdr_len = 30; 
        if (len < hdr_len + 8) return;
        const uint8_t *llc = frame + hdr_len;
        if (llc[6]==0x88 && llc[7]==0x8E)
            wifi_on_eapol(llc+8, (uint16_t)(len - hdr_len - 8));
    }
}


void wifi_poll(void) {
    uint32_t now = timer_get_ticks();
    if (g_wifi_state == WIFI_STATE_SCANNING) {
        if (now - g_state_timer > 3000) {
            g_wifi_state = WIFI_STATE_IDLE;
            if (g_wifi_network_count == 0)
                memcpy(g_wifi_status_msg, "Nenhuma rede encontrada", 24);
            else {
                memcpy(g_wifi_status_msg, "Scan concluído: ", 17);
                g_wifi_status_msg[16] = (char)('0' + g_wifi_network_count);
                g_wifi_status_msg[17] = '\0';
            }
        }
    } else if (g_wifi_state == WIFI_STATE_CONNECTING ||
               g_wifi_state == WIFI_STATE_ASSOCIATING ||
               g_wifi_state == WIFI_STATE_HANDSHAKE) {
        if (now - g_state_timer > 10000) {
            g_wifi_state = WIFI_STATE_ERROR;
            memcpy(g_wifi_status_msg, "Timeout ao conectar", 20);
        }
    } else if (g_wifi_state == WIFI_STATE_CONNECTED) {
        
        extern uint32_t net_ip;
        if (net_ip) memcpy(g_wifi_status_msg, "Conectado (IP obtido)", 22);
    }
}
