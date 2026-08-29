#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "sys.h"

#include "stdbool.h"
#include "NTP.h"
#include "wifi.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_sntp.h"
#include "time.h" 
#include "esp_netif_sntp.h"

static const char* TAG = "NTP";

static void time_sync_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "✅ NTP 时间同步成功");
}

esp_err_t  SntpInit(void)
{
    esp_sntp_config_t config = {
        .sync_cb = time_sync_cb,
        .renew_servers_after_new_IP = true,
        .server_from_dhcp = false,
        .servers = ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.cloudflare.com"),
        .num_of_servers = 2,
    };
    config.sync_cb = time_sync_cb;
    config.renew_servers_after_new_IP  = true;
    config.server_from_dhcp = false;

    esp_err_t ret = esp_netif_sntp_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SNTP 初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 阻塞等待首次同步，超时 30s
    ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(30000));
    if (ret == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "⚠️ NTP 同步超时，请检查网络或防火墙");
    }
    return ret;
}
