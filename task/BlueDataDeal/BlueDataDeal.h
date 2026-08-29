#ifndef BLUEDATADEAL__H
#define BLUEDATADEAL__H

#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define BLUE_DATA_MAX_LEN 20

typedef struct {
    size_t length;
    uint8_t data[BLUE_DATA_MAX_LEN];
} BlueDataMessage_t;

extern QueueHandle_t BlueDataQueue;

//用于处理蓝牙数据

void BlueDataDealTask(void *pvParameters);

#endif
