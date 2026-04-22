
#ifndef _NET_WIFI_H
#define _NET_WIFI_H

#include <types.h>

#define WIFI_MAX_NETWORKS   16
#define WIFI_SSID_LEN       33
#define WIFI_PASS_LEN       64

typedef enum {
    WIFI_SEC_OPEN = 0,
    WIFI_SEC_WEP,
    WIFI_SEC_WPA,
    WIFI_SEC_WPA2
} wifi_security_t;

typedef enum {
    WIFI_STATE_IDLE = 0,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_ASSOCIATING,
    WIFI_STATE_HANDSHAKE,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_ERROR
} wifi_state_t;

typedef struct {
    char     ssid[WIFI_SSID_LEN];
    uint8_t  bssid[6];
    int8_t   rssi;               
    uint8_t  channel;
    wifi_security_t security;
    bool     valid;
} wifi_network_t;


extern wifi_network_t g_wifi_networks[WIFI_MAX_NETWORKS];
extern int            g_wifi_network_count;
extern wifi_state_t   g_wifi_state;
extern char           g_wifi_status_msg[80];
extern int            g_wifi_connected_idx;   


void wifi_init(void);


void wifi_scan(void);


void wifi_connect(int idx, const char *password);


void wifi_disconnect(void);


void wifi_poll(void);


bool wifi_is_connected(void);


void wifi_on_beacon(const uint8_t *bssid, const char *ssid, uint8_t ssid_len,
                    int8_t rssi, uint8_t channel, wifi_security_t sec);


void wifi_on_eapol(const uint8_t *data, uint16_t len);


void wifi_on_assoc_ok(void);
void wifi_on_assoc_fail(void);


void wifi_driver_send(const uint8_t *frame, uint16_t len);


void wifi_rx_frame(const uint8_t *frame, uint16_t len);

#endif
