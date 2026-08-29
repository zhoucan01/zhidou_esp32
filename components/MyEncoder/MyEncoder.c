#include <stdio.h>
#include "Encoder.h"

#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <encoder.h>
#include "esp_bit_defs.h"
#include <esp_log.h>
#include "stdbool.h"

#include "sys.h"

#define EV_QUEUE_LEN 5

static const char *TAG = "encoder_example";

static QueueHandle_t event_queue;
static rotary_encoder_handle_t re;

volatile bool isPress=false;
volatile bool isLongPress=false;
volatile int encCounter=0;

static void encoder_event_handler(const rotary_encoder_event_t *event, void *ctx)
{
    QueueHandle_t queue = (QueueHandle_t)ctx;
    xQueueSendToBack(queue, event, 0);
}

void enctest(void *arg)
{
    // Create queue for rotary encoder events
    event_queue = xQueueCreate(EV_QUEUE_LEN, sizeof(rotary_encoder_event_t));

    // Create an encoder
    rotary_encoder_config_t config = ROTARY_ENCODER_DEFAULT_CONFIG();
    config.pin_a = encCntA;
    config.pin_b = encCntB;
    config.pin_btn = encKey;
    config.callback = encoder_event_handler;
    config.btn_long_press_time_us = 2000000;
    config.callback_ctx = event_queue;
    ESP_ERROR_CHECK(rotary_encoder_create(&config, &re));

    rotary_encoder_event_t e;
    int32_t val = 0;

   // ESP_LOGI(TAG, "Initial value: %" PRIi32, val);
    while (1)
    {
        xQueueReceive(event_queue, &e, portMAX_DELAY);

        switch (e.type)
        {
            case RE_ET_BTN_PRESSED:
                
             //   ESP_LOGI(TAG, "Button pressed");
                break;
            case RE_ET_BTN_RELEASED:
                
            //    ESP_LOGI(TAG, "Button released");
                break;
            case RE_ET_BTN_CLICKED:
                
               // ESP_LOGI(TAG, "Button clicked");
                rotary_encoder_enable_acceleration(re, 100);
                isPress=true;
             //   ESP_LOGI(TAG, "Acceleration enabled");
                break;
            case RE_ET_BTN_LONG_PRESSED:
             //   ESP_LOGI(TAG, "Looooong pressed button");
                rotary_encoder_disable_acceleration(re);
                isLongPress=true;
            //    ESP_LOGI(TAG, "Acceleration disabled");
                break;
            case RE_ET_CHANGED:
                val += e.diff;
                encCounter=val;
              //  ESP_LOGI(TAG, "Value = %" PRIi32, val);
                break;
            default:
                break;
        }
    }
}
