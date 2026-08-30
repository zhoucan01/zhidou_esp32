#include <stdio.h>
#include "wifi.h"
#include "OTA.h" 

#if __has_include("wifi_credentials.local.h")
#include "wifi_credentials.local.h"
#else
#define WIFI_DEFAULT_SSID ""
#define WIFI_DEFAULT_PASSWORD ""
#endif

EventGroupHandle_t WiFiEventGroup;

#define TAG             "WIFI_CONNECT"
#define NVS_NAMESPACE   "NVSWifi"

extern char WiFiPassword[66];
extern char WiFiName[66];

nvs_handle_t WIFIInfHandle;

// 配置WiFi Station模式
wifi_config_t wifi_config = {
    .sta = {
    .ssid = "",
    .password = "",
    },
};

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);

static int GetWifiInfo(void);

//先检查nvsflash里面的数据，没有的话跳过，等待蓝牙发送相关数据，蓝牙发送完成后，再使用蓝牙的数据连接，连接成功后再写入nvsflash里面，供下次使用
static char usrwifissid[66];
static char usrwifipassword[66];
static int8_t WifiMem=0;
static bool wifi_started;
int WifiInit(void)
{
    WiFiEventGroup=xEventGroupCreate();
    int has_credentials = GetWifiInfo();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Suppress the repetitive Wi-Fi/Bluetooth coexistence reconnect message.
     * This only hides this internal tag; reconnect behavior remains enabled. */
    esp_log_level_set("wifi:Coexist", ESP_LOG_NONE);

    // 注册WiFi事件处理器
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip);


    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    if (has_credentials) {
        ESP_ERROR_CHECK(esp_wifi_start());
        wifi_started = true;
    } else {
        ESP_LOGI(TAG, "No WiFi credentials; radio remains stopped for BLE provisioning");
    }

    ESP_LOGI(TAG, "WiFi initialization finished. SSID: %s", usrwifissid);
    return 1;
}

int WiFiConn(void) 
{
    if (!wifi_started) {
        if (GetWifiInfo() == 0) {
            ESP_LOGI(TAG, "WiFi credentials are still empty");
            return 0;
        }

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_err_t start_err = esp_wifi_start();
        if (start_err != ESP_OK) {
            ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(start_err));
            return 0;
        }
        wifi_started = true;
        return 1; /* WIFI_EVENT_STA_START performs the connection. */
    }

    // 1. 先获取 Wi-Fi 当前的状态
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info); // 这个 API 可以用来判断状态

    // 2. 判断逻辑
    // ESP_ERR_WIFI_NOT_CONNECTING: 表示没有在连接中（可能是断开，也可能是连上了）
    // ESP_OK: 表示成功获取了 AP 信息，说明已经连上了
    if (err == ESP_ERR_WIFI_NOT_CONNECT) {
        // 只有在明确未连接时，才执行连接
        
        GetWifiInfo();
        if(GetWifiInfo()==0)
        {
            ESP_LOGI("ESP","输入为空");
            return 0;
        }
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        // 注意：这里不需要 start，因为 start 只需要在初始化时做一次
        esp_err_t conn_err = esp_wifi_connect();
        if (conn_err == ESP_OK) {
            ESP_LOGI("WIFI", "Reconnect triggered");
        } else {
            ESP_LOGE("WIFI", "Connect failed: %s", esp_err_to_name(conn_err));
        }
    } else {
        // 如果正在连接或者已经连上，就不做动作，避免报错
         ESP_LOGI("WIFI", "Skip connect: WiFi is busy or already connected");
    }
    return 1;
}

void WiFiEraseInfo(void)
{
    nvs_open(NVS_NAMESPACE,NVS_READWRITE,&WIFIInfHandle);
    nvs_set_str(WIFIInfHandle,"USRWifissid","");
    nvs_set_str(WIFIInfHandle,"USRWifiPassword","");
    nvs_commit(WIFIInfHandle);
    nvs_close(WIFIInfHandle);
}

//wifi回调函数

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {//wifi启动，触发开始事件后
        esp_wifi_connect();//连接wifi
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {//连接失败
        for(int i=0;i<5;i++)
        {
            err_t err=esp_wifi_connect();
            if(err==ESP_OK)
            {
                break;
            }
            ESP_LOGI(TAG, "WiFi disconnected, attempting to reconnect...");
        }//尝试5次
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        nvs_open(NVS_NAMESPACE,NVS_READWRITE,&WIFIInfHandle);
        if(nvs_get_i8(WIFIInfHandle,"WifiInfo",&WifiMem)==ESP_ERR_NVS_NOT_FOUND )//先检查这个状态被存储了么
        {
            nvs_set_i8(WIFIInfHandle,"WifiInfo",0);//没被存储就说明是第一次，设置为0，
        }
        nvs_get_i8(WIFIInfHandle,"WifiInfo",&WifiMem);//取值
        if(WifiMem==0)//是第一次就存进去
        {
            nvs_set_str(WIFIInfHandle,"USRWifissid",usrwifissid);
            nvs_set_str(WIFIInfHandle,"USRWifiPassword",usrwifipassword);
            nvs_set_i8(WIFIInfHandle,"WifiInfo",1);
        }
        nvs_commit(WIFIInfHandle);
        nvs_close(WIFIInfHandle);
        xEventGroupSetBits(WiFiEventGroup,WiFiConnBits);
        ESP_LOGI(TAG, "Starting one-shot LAN OTA check");
        start_ota_check();
    }
}


static int GetWifiInfo(void)
{
    nvs_open(NVS_NAMESPACE,NVS_READWRITE,&WIFIInfHandle);
    size_t len1 = sizeof(usrwifissid);
    size_t len2 = sizeof(usrwifipassword);   
    if(nvs_get_str(WIFIInfHandle,"USRWifissid",usrwifissid,&len1)==ESP_ERR_NVS_NOT_FOUND||nvs_get_str(WIFIInfHandle,"USRWifiPassword",usrwifipassword,&len2)==ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI("WIFITest0","NONE");
        nvs_set_str(WIFIInfHandle,"USRWifissid","");
        nvs_set_str(WIFIInfHandle,"USRWifiPassword","");
        nvs_commit(WIFIInfHandle);
    }
    
    else{
        ESP_LOGI("WIFITest0","NULL NONE");
        nvs_get_str(WIFIInfHandle,"USRWifissid",usrwifissid,&len1);
        nvs_get_str(WIFIInfHandle,"USRWifiPassword",usrwifipassword,&len2);
    }

    if (WIFI_DEFAULT_SSID[0] != '\0' && WIFI_DEFAULT_PASSWORD[0] != '\0') {
        strncpy(usrwifissid, WIFI_DEFAULT_SSID, sizeof(usrwifissid) - 1);
        strncpy(usrwifipassword, WIFI_DEFAULT_PASSWORD, sizeof(usrwifipassword) - 1);
        usrwifissid[sizeof(usrwifissid) - 1] = '\0';
        usrwifipassword[sizeof(usrwifipassword) - 1] = '\0';
        nvs_set_str(WIFIInfHandle, "USRWifissid", usrwifissid);
        nvs_set_str(WIFIInfHandle, "USRWifiPassword", usrwifipassword);
        nvs_set_i8(WIFIInfHandle, "WifiInfo", 1);
        nvs_commit(WIFIInfHandle);
        ESP_LOGI(TAG, "Using local Wi-Fi credentials for SSID: %s", usrwifissid);
    }
    nvs_close(WIFIInfHandle);

    if(usrwifissid[0]==0x00||usrwifipassword[0]==0x00)
    {
        memset(usrwifipassword,0x00,sizeof(usrwifipassword));
        memset(usrwifissid,0x00,sizeof(usrwifissid));
        memcpy(usrwifipassword,WiFiPassword,sizeof(WiFiPassword));
        memcpy(usrwifissid,WiFiName,sizeof(WiFiName));
        if(usrwifissid[0]==0x00||usrwifipassword[0]==0x00)
        {
            ESP_LOGI("wifi name&ssid","NULL");
            return 0;
        }
    }  
    strncpy((char*)wifi_config.sta.ssid, usrwifissid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, usrwifipassword, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';
    return 1;
}

bool GetWiFiConnection(void)
{
    EventBits_t bits = xEventGroupGetBits(WiFiEventGroup);
    return (bits & WiFiConnBits) != 0;
}
