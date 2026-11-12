#include "wifi.h"
#include "WiFiConnect.h"
EventGroupHandle_t WiFiEventGroup;
extern char WiFiName[66], WiFiPassword[66];
int WifiInit(void)
{
    WiFiEventGroup = xEventGroupCreate();
    ESP_ERROR_CHECK(WiFiEventGroup ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_sta() ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    return 1;
}
int WiFiConn(void) { return WiFiConnectConfigure(WiFiName, WiFiPassword) == ESP_OK; }
void WiFiEraseInfo(void) { WiFiConnectForget(); }
bool GetWiFiConnection(void) { return WiFiConnectIsConnected(); }
