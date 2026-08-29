#include "mymqtt.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "wifi.h"

const char* MQTT_BROKER_URI = "mqtt://test.mosquitto.org";
const char* MQTT_USERNAME = "";  // 如果需要用户名验证
const char* MQTT_PASSWORD = "";  // 如果需要密码验证

extern EventGroupHandle_t WiFiEventGroup;

static esp_mqtt_client_handle_t mqtt_client;

static const char *TAG = "WIFI_MQTT_CLIENT";

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            // xEventGroupSetBits(wifi_event_group, MQTT_CONNECTED_BIT);
            
            // 连接成功后自动订阅默认主题
            mqtt_subscribe_topic("sdadqwdafa", 0);
            mqtt_publish_message("esp32/status", "ESP32 connected", 1);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
         //   xEventGroupClearBits(wifi_event_group, MQTT_CONNECTED_BIT);
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGI(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGI(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            }
            break;
            
        default:
            ESP_LOGI(TAG, "Other MQTT event id:%d", event->event_id);
            break;
    }
}

int MQTTInit(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };

    // 如果提供了用户名和密码，则设置认证信息
    if (strlen(MQTT_USERNAME) > 0) {
        mqtt_cfg.credentials.username = MQTT_USERNAME;
    }
    if (strlen(MQTT_PASSWORD) > 0) {
        mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;
    }
    EventBits_t uxBits;
    uxBits = xEventGroupGetBits(WiFiEventGroup);

    if((uxBits&WiFiConnBits)==0)
    {
        ESP_LOGI("MQTT","wifi not connect");
        return 0;
    }

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
    
    ESP_LOGI(TAG, "MQTT client started. Broker: %s", MQTT_BROKER_URI);
    return 1;
}

/* 发布MQTT消息 */
void mqtt_publish_message(const char* topic, const char* message, int qos)
{
    if (mqtt_client ) {
        int msg_id = esp_mqtt_client_publish(mqtt_client, topic, message, 0, qos, 0);
        ESP_LOGI(TAG, "Message published to %s, msg_id=%d", topic, msg_id);
    } else {
        ESP_LOGE(TAG, "MQTT client not connected, cannot publish message");
    }
}

/* 订阅MQTT主题 */
void mqtt_subscribe_topic(const char* topic, int qos)
{
    if (mqtt_client ) {
        int msg_id = esp_mqtt_client_subscribe(mqtt_client, topic, qos);
        ESP_LOGI(TAG, "Subscribed to %s, msg_id=%d", topic, msg_id);
    } else {
        ESP_LOGE(TAG, "MQTT client not connected, cannot subscribe");
    }
}

