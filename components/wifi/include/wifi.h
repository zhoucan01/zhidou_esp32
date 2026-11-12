#ifndef __WIFI__H
#define __WIFI__H

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"


#define WiFiConnBits        (1<<1)//WiFi连接位
#define WiFiInfoEraseBits   (1<<2)//WiFi信息擦除位

int WifiInit(void);
void WiFiEraseInfo(void);
int WiFiConn(void);
bool GetWiFiConnection(void);

#endif

