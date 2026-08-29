#include <stdio.h>
#include "usart.h"
#include "driver/uart.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include <ctype.h>

#include "sys.h"

#define BUF_SIZE        1024

QueueHandle_t uart_queue; 
QueueHandle_t srcuart_queue;
void uart_init(void) {
    const uart_config_t poweruart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB
    };

    ESP_LOGI("uart", "Configuring UART%d parameters", ScreenUart);
    ESP_ERROR_CHECK(uart_param_config(ScreenUart, &poweruart_config));
    ESP_LOGI("uart", "Routing UART%d TX=%d RX=%d",
             ScreenUart, ScreenTx, ScreenRx);
    ESP_ERROR_CHECK(uart_set_pin(ScreenUart, ScreenTx, ScreenRx,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI("uart", "Installing UART%d driver", ScreenUart);
    ESP_ERROR_CHECK(uart_driver_install(ScreenUart, BUF_SIZE * 2, 0, 10,
                                        &uart_queue, 0));
    ESP_LOGI("uart", "UART%d initialized", ScreenUart);
    
}

// 【新增函数】供外部调用的数据读取函数
// 返回值：>0 表示读取到的字节数，0 表示超时未读取到
int Uart_GetData(uint8_t *buffer, int max_len, int wait_ms) {
    uart_event_t event;
    
    // 1. 等待事件发生（有数据、溢出等）
    // 如果 wait_ms 为 0，则不等待直接返回
    TickType_t wait_ticks = (wait_ms == 0) ? 0 : pdMS_TO_TICKS(wait_ms);
    
    if (xQueueReceive(uart_queue, (void *)&event, wait_ticks)) {
        if (event.type == UART_DATA) {
            // 2. 有数据事件，从底层读取
            // 限制读取长度不超过缓冲区大小
            int read_len = (event.size < max_len) ? event.size : max_len;
            return uart_read_bytes(ScreenUart, buffer, read_len, 0);
        }
    }
    return 0x00; // 超时或无数据
}



