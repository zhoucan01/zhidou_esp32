#ifndef SYS_H
#define SYS_H

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void CheckDelay(int delay_s);

#include "driver/gpio.h"
#include "driver/uart.h"
//LCD pin
#define lcdCs   GPIO_NUM_12
#define lcdRs   GPIO_NUM_11
#define lcdWr   GPIO_NUM_10
#define lcdRd   GPIO_NUM_9
#define lcdRst  GPIO_NUM_46
#define lcdD0   GPIO_NUM_3
#define lcdD1   GPIO_NUM_20
#define lcdD2   GPIO_NUM_19
#define lcdD3   GPIO_NUM_8
#define lcdD4   GPIO_NUM_18
#define lcdD5   GPIO_NUM_17
#define lcdD6   GPIO_NUM_16
#define lcdD7   GPIO_NUM_15
#define lcdD8   GPIO_NUM_7
#define lcdD9   GPIO_NUM_6
#define lcdD10  GPIO_NUM_5
#define lcdD11  GPIO_NUM_4
#define lcdD12  GPIO_NUM_13
#define lcdD13  GPIO_NUM_14
#define lcdD14  GPIO_NUM_21
#define lcdD15  GPIO_NUM_47
#define lcdBl   GPIO_NUM_48
//Beep Pin
#define BEEP    GPIO_NUM_45
//Enc Pin
#define encCntA GPIO_NUM_42
#define encCntB GPIO_NUM_41
#define encKey  GPIO_NUM_40

#define ScreenUart                  UART_NUM_2

#define ScreenTx                    GPIO_NUM_1
#define ScreenRx                    GPIO_NUM_2

#define BeepTimerID                  LEDC_TIMER_3    //定时器3
#define Beepchannal                  LEDC_CHANNEL_4 

#endif

