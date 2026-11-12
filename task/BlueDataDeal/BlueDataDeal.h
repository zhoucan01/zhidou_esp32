#ifndef BLUEDATADEAL__H
#define BLUEDATADEAL__H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define BLUE_DATA_MAX_LEN 256U

typedef struct {
    size_t length;
    uint8_t data[BLUE_DATA_MAX_LEN];
} BlueDataMessage_t;

extern QueueHandle_t BlueDataQueue;

/* Feed Bluetooth text chunks into the MODBUS-TX:[...] stream parser. */
bool BlueDataSubmit(const uint8_t *data, size_t length, TickType_t timeout);

void BlueDataDealTask(void *pvParameters);

#endif
