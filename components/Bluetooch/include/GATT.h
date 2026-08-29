#ifndef __GATT__H
#define __GATT__H

#include "stdbool.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "stdint.h"

#define WIFI_SSID_MAX_LEN   32  // IEEE 802.11 标准最大 SSID 长度
#define WIFI_PASS_MAX_LEN   64  // 密码一般不超过 63 字符 + '\0'

typedef struct 
{
    char g_wifi_ssid[WIFI_SSID_MAX_LEN + 1];
    char g_wifi_password[WIFI_PASS_MAX_LEN + 1];
    bool g_wifi_config_ready;
}WiFiInfo_t;

extern WiFiInfo_t WiFiInfo;


int gatt_svr_init(void);
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);



#endif