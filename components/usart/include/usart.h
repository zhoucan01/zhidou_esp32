#ifndef USART_H
#define USART_H

#include <stdint.h>
#include "driver/uart.h"

/* Initialize both UART ports and their RS485 DE GPIO pins. */
void uart_init(void);

/* RS485 DE pin control: HIGH = transmit, LOW = receive.
 * NOTE: DE is now driven entirely by the UART hardware in
 * UART_MODE_RS485_HALF_DUPLEX. These functions are kept as no-ops for
 * backward source compatibility but do nothing at runtime. */
void uart_de_high(uart_port_t uart);
void uart_de_low(uart_port_t uart);

/* RS485 transceiver settle delay in microseconds, derived from the UART's
 * baud rate (one character time = 10 bits). Kept for backward source
 * compatibility; with hardware RS485 DE control the settle delay is no
 * longer needed and callers should not rely on it. */
uint32_t uart_de_settle_us(uart_port_t uart);

/* Modbus RTU t3.5 inter-frame delay in microseconds per IEC 61158 /
 * Modbus over Serial Line V1.02. Baud > 19200 -> 1.75 ms fixed;
 * otherwise 3.5 * 11 bits / baud. */
uint32_t uart_modbus_t35_us(uart_port_t uart);

/* Wait for the RS485 bus to be idle for at least the Modbus t3.5 inter-frame
 * time before starting a new transaction. Drains residual RX FIFO bytes,
 * forces DE LOW, then holds off for t3.5. Call before each master
 * transaction (read or write) to comply with the RTU framing spec. */
void uart_modbus_wait_bus_idle(uart_port_t uart);

/* Reset the UART transport after a failed transaction: flushes the TX FIFO
 * so half-sent frames do not corrupt the next frame, drains the RX FIFO,
 * and forces DE LOW. Call from any TX/RX error path to recover cleanly. */
void uart_modbus_reset_transport(uart_port_t uart);

int Uart_GetData(uint8_t *buffer, int max_len, int wait_ms); 
int ScrUartGetData(uint8_t *buffer, int max_len, int wait_ms);

#endif // USART_H
