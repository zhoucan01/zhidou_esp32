#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi.h"
#ifdef __cplusplus
extern "C" {
#endif

// #define WIFI_CONNECT_TEST

/* Saved successful credentials take priority. These are only the factory
 * fallback when no saved SSID exists. The portal never reverts to them. */
#define WIFI_CONNECT_SSID "NUVELL"
#define WIFI_CONNECT_PASSWORD "NUVELL20260206"
#define WIFI_CONNECT_AUTH_MODE WIFI_AUTH_WPA2_PSK
#define WIFI_CONNECT_TASK_STACK_SIZE 4096
#define WIFI_CONNECT_TASK_PRIORITY 4
#define WIFI_CONNECT_TASK_TICK_MS 200
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_CONNECT_RETRY_DELAY_MS 5000
#define WIFI_CONNECT_MAX_RETRY 0
#define WIFI_CONNECT_SCAN_LIMIT 16

typedef enum { WIFI_CONNECT_IDLE, WIFI_CONNECT_CONNECTING, WIFI_CONNECT_CONNECTED, WIFI_CONNECT_FAILED } wifi_connect_phase_t;
typedef struct {
    wifi_connect_phase_t phase;
    int reason;
    char ssid[33], ip[16];
    bool ap_active, scanning, saved;
    unsigned scan_count;
    int scan_error;
    int ap_error;
} wifi_connect_status_t;

/* Start after WifiInit(), or create WiFiConnectTask directly once in main.
 * This task alone owns driver configuration, scans and reconnect requests. */
esp_err_t WiFiConnectStart(void);
void WiFiConnectTask(void *argument);
/* Queue a new attempt: ESP_OK means accepted, not connected. A successful
 * association + IP acquisition persists credentials in NVSWifi. Failed
 * attempts leave the previously saved credentials intact. */
esp_err_t WiFiConnectConfigure(const char *ssid, const char *password);
/* Queue an idempotent APSTA transition; the AP stays up during STA changes. */
esp_err_t WiFiConnectOpenAP(const char *name);
/* Queue a nonblocking scan; results/status are copied under a short lock. */
esp_err_t WiFiConnectScan(void);
void WiFiConnectGetStatus(wifi_connect_status_t *out);
unsigned WiFiConnectGetScan(wifi_ap_record_t *out, unsigned capacity);
bool WiFiConnectIsConnected(void);
uint32_t WiFiConnectGetRetryCount(void);
int8_t WiFiConnectGetRssi(void);
void WiFiConnectStop(void);
void WiFiConnectResume(void);
void WiFiConnectForget(void);
#ifdef __cplusplus
}
#endif
#endif
