#include "usart.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"   /* esp_rom_delay_us */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys.h"

#define BUF_SIZE 1024

/* Map UART port -> baud rate. Populated in uart_init(). */
static uint32_t uart_baud_rate[UART_NUM_MAX];

static void uart_init_port(uart_port_t uart, int tx, int rx, int de, int baud)
{
    /* Start in receive mode before assigning DE to the UART RTS signal. */
    if (de >= 0) {
        ESP_ERROR_CHECK(gpio_reset_pin((gpio_num_t)de));
        ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)de, 0));
        ESP_ERROR_CHECK(gpio_set_direction((gpio_num_t)de, GPIO_MODE_OUTPUT));
    }
    const uart_config_t config = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    ESP_LOGI("uart", "Configuring UART%d at %d-8-N-1", uart, baud);
    ESP_ERROR_CHECK(uart_param_config(uart, &config));

    /* The RS485 half-duplex driver controls active-high DE through RTS. */
    esp_err_t pin_err = uart_set_pin(uart, tx, rx,
                                     de >= 0 ? de : UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE);
    if (pin_err != ESP_OK) {
        ESP_LOGE("uart", "uart_set_pin failed: uart=%d tx=%d rx=%d de=%d err=%s",
                 uart, tx, rx, de, esp_err_to_name(pin_err));
    }
    ESP_ERROR_CHECK(pin_err);

    /* TX buffer must be non-zero, otherwise uart_write_bytes blocks forever
     * or returns -1 when the driver cannot queue bytes. */
    ESP_ERROR_CHECK(uart_driver_install(uart, BUF_SIZE * 2, BUF_SIZE, 0,
                                        NULL, 0));

    ESP_ERROR_CHECK(uart_set_mode(uart, de >= 0 ? UART_MODE_RS485_HALF_DUPLEX
                                               : UART_MODE_UART));
    if (de >= 0) {
        uart_set_rx_full_threshold(uart, 1);  /* wake read as soon as 1 byte arrives */
    }
    uart_baud_rate[uart] = (uint32_t)baud;

    ESP_LOGI("uart", "UART%d initialized: TX=%d RX=%d DE=%d (RS485 half-duplex)",
             uart, tx, rx, de);
}

/* Compatibility no-ops: the half-duplex driver owns DE via UART RTS. */
void uart_de_high(uart_port_t uart)
{
    (void)uart;
}

void uart_de_low(uart_port_t uart)
{
    (void)uart;
}

/* RS485 transceiver settle delay: one character time (10 bits) in microseconds.
 * At 1M baud = 10us, 115200 baud = ~87us, 9600 baud = ~1042us.
 * Capped at 1000us for slow links, floored at 10us for fast links. */
uint32_t uart_de_settle_us(uart_port_t uart)
{
    if (uart >= UART_NUM_MAX || uart_baud_rate[uart] == 0) {
        return 50;  /* fallback for uninitialized UART */
    }
    uint32_t char_time_us = 10000000U / uart_baud_rate[uart];
    if (char_time_us < 10U) {
        char_time_us = 10U;
    } else if (char_time_us > 1000U) {
        char_time_us = 1000U;
    }
    return char_time_us;
}

/* Modbus RTU t3.5 inter-frame delay per IEC 61158 / Modbus over Serial Line
 * V1.02 specification:
 *   baud > 19200  -> 1.75 ms (fixed by spec)
 *   baud <= 19200 -> 3.5 * (11 bits) * 1e6 / baud  microseconds
 * The t3.5 is the minimum idle time on the bus that separates two consecutive
 * RTU frames. A master must wait for the bus to be idle for at least t3.5
 * before starting a new transaction. */
uint32_t uart_modbus_t35_us(uart_port_t uart)
{
    if (uart >= UART_NUM_MAX || uart_baud_rate[uart] == 0) {
        return 1750;  /* fallback for uninitialized UART */
    }
    uint32_t baud = uart_baud_rate[uart];
    if (baud > 19200U) {
        return 1750U;  /* spec fixed value for high baud rates */
    }
    /* 3.5 character times; each RTU character = 11 bits.
     * t3.5 = 3.5 * 11 * 1e6 / baud = 38500000 / baud microseconds */
    return 38500000U / baud;
}

/* Wait for the RS485 bus to be idle (released) for at least the Modbus t3.5
 * inter-frame time before starting a new transaction. Drains any residual
 * bytes from the UART RX FIFO first, then holds off for t3.5. */
void uart_modbus_wait_bus_idle(uart_port_t uart)
{
    if (uart >= UART_NUM_MAX) {
        return;
    }
    /* Drain any leftover bytes in the RX FIFO so the t3.5 idle window
     * starts from a known-clean state. */
    size_t dropped = 0;
    uint8_t tmp;
    while (uart_read_bytes(uart, &tmp, 1, 0) > 0) {
        ++dropped;
    }
    if (dropped > 0) {
        ESP_LOGW("uart", "wait_bus_idle: drained %u residual bytes from UART%d",
                 (unsigned)dropped, uart);
    }
    /* The half-duplex driver releases DE after TX; preserve the t3.5 gap. */
    esp_rom_delay_us(uart_modbus_t35_us(uart));
}

/* Reset the UART TX/RX path after a failed transaction. Waits for any
 * in-flight TX to drain so half-sent frames do not corrupt the next frame,
 * flushes the RX FIFO of partial slave responses, and returns to receive mode.
 * Call after any TX/RX error path to recover cleanly. */
void uart_modbus_reset_transport(uart_port_t uart)
{
    if (uart >= UART_NUM_MAX) {
        return;
    }
    /* Wait for any in-flight TX bytes to finish so we don't leave a half frame
     * on the bus. Polling avoids blocking the task if the TX path is stuck. */
    uart_wait_tx_idle_polling(uart);
    /* Drain RX FIFO of partial/garbage bytes from a failed slave response. */
    uart_flush_input(uart);
    /* DE is managed by the half-duplex driver; do not toggle the GPIO. */
    ESP_LOGW("uart", "transport reset on UART%d (TX drained, RX flushed)", uart);
}

void uart_init(void)
{
    uart_init_port(ScreenUart, ScreenTx, ScreenRx, ScreenDe, ScreenBaud);
    uart_init_port(PowerUart, PowerTx, PowerRx, PowerDe, PowerBaud);
}

int Uart_GetData(uint8_t *buffer, int max_len, int wait_ms)
{
    if (buffer == NULL || max_len <= 0 || wait_ms < 0) {
        return 0;
    }
    /* DE remains under the half-duplex driver's control. */
    return uart_read_bytes(ScreenUart, buffer, max_len,
                           pdMS_TO_TICKS((uint32_t)wait_ms));
}

int ScrUartGetData(uint8_t *buffer, int max_len, int wait_ms)
{
    /* Alias kept for callers that expect the screen-link read helper. */
    return Uart_GetData(buffer, max_len, wait_ms);
}
