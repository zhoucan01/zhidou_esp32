#ifndef DEBUG_CONSOLE_H
#define DEBUG_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

#define CONTROL_MODBUS_FRAME_CAPACITY 256U

typedef enum {
    CONTROL_MODBUS_OK = 0,
    CONTROL_MODBUS_BAD_CRC,
    CONTROL_MODBUS_BAD_SLAVE,
    CONTROL_MODBUS_TIMEOUT,
    CONTROL_MODBUS_BAD_RESPONSE_CRC,
} control_modbus_result_t;

/* Common Modbus RTU gateway used by UART0 and Bluetooth. */
control_modbus_result_t ControlModbusProcess(
    const uint8_t *request, size_t request_length,
    uint8_t response[CONTROL_MODBUS_FRAME_CAPACITY], size_t *response_length);

void DebugConsoleTask(void *pvParameters);

#endif
