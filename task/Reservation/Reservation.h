#ifndef RESERVATION_H
#define RESERVATION_H

#include "stdbool.h"
#include "stdint.h"
typedef struct 
{
    bool isReserved; // 是否预约
    bool isExecuted; // 预约是否已执行
    uint8_t hour;     // 预约小时
    uint8_t minute;   // 预约分钟
} Reservation_t;

void ReservationTask(void *pvParameters);

#endif