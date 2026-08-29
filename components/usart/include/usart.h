#ifndef USART_H
#define USART_H

void uart_init(void);
int Uart_GetData(uint8_t *buffer, int max_len, int wait_ms); 
int ScrUartGetData(uint8_t *buffer, int max_len, int wait_ms);

#endif // USART_H
