#include "BlueDataDeal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "Bluetooch.h"
#include "DebugConsole.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "BlueDataDeal";

QueueHandle_t BlueDataQueue = NULL;

static int hex_nibble(uint8_t value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool BlueDataSubmit(const uint8_t *data, size_t length, TickType_t timeout)
{
    static const char prefix[] = "MODBUS-TX:[";
    static size_t prefix_index;
    static BlueDataMessage_t message;
    static int high_nibble = -1;
    static bool in_frame;
    bool accepted = true;

    if (data == NULL || length == 0U || BlueDataQueue == NULL) return false;
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        if (!in_frame) {
            if (toupper((unsigned char)byte) == prefix[prefix_index]) {
                if (++prefix_index == sizeof(prefix) - 1U) {
                    in_frame = true;
                    prefix_index = 0U;
                    message.length = 0U;
                    high_nibble = -1;
                }
            } else {
                prefix_index = toupper((unsigned char)byte) == prefix[0] ?
                               1U : 0U;
            }
            continue;
        }
        if (byte == ']') {
            if (high_nibble < 0 && message.length >= 4U) {
                accepted = xQueueSend(BlueDataQueue, &message, timeout) ==
                           pdTRUE && accepted;
            } else {
                accepted = false;
            }
            in_frame = false;
            high_nibble = -1;
            message.length = 0U;
            continue;
        }
        if (isspace((unsigned char)byte)) continue;
        int nibble = hex_nibble(byte);
        if (nibble < 0) {
            in_frame = false;
            high_nibble = -1;
            message.length = 0U;
            accepted = false;
        } else if (high_nibble < 0) {
            high_nibble = nibble;
        } else if (message.length < sizeof(message.data)) {
            message.data[message.length++] =
                (uint8_t)((high_nibble << 4) | nibble);
            high_nibble = -1;
        } else {
            in_frame = false;
            high_nibble = -1;
            message.length = 0U;
            accepted = false;
        }
    }
    return accepted;
}

void BlueDataDealTask(void *pvParameters)
{
    (void)pvParameters;
    BlueDataMessage_t message;
    uint8_t response[CONTROL_MODBUS_FRAME_CAPACITY];
    char response_text[sizeof("MODBUS-RX:[]\r\n") +
                       CONTROL_MODBUS_FRAME_CAPACITY * 3U];

    BlueDataQueue = xQueueCreate(5, sizeof(message));
    if (BlueDataQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create Bluetooth Modbus queue");
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        if (xQueueReceive(BlueDataQueue, &message, portMAX_DELAY) != pdTRUE)
            continue;

        size_t response_length = 0;
        control_modbus_result_t result = ControlModbusProcess(
            message.data, message.length, response, &response_length);
        if (result == CONTROL_MODBUS_OK) {
            size_t used = (size_t)snprintf(response_text,
                                           sizeof(response_text),
                                           "MODBUS-RX:[");
            for (size_t i = 0; i < response_length; ++i) {
                used += (size_t)snprintf(response_text + used,
                                         sizeof(response_text) - used,
                                         "%s%02X", i == 0U ? "" : " ",
                                         response[i]);
            }
            memcpy(response_text + used, "]\r\n", 3U);
            used += 3U;
            BluetoothSendControlResponse((const uint8_t *)response_text, used);
        } else {
            ESP_LOGW(TAG, "Bluetooth Modbus request rejected: result=%d",
                     (int)result);
        }
    }
}
