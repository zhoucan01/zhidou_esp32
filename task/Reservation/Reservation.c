#include "Reservation.h"

#include "sys.h"
#include "FunctionSelection.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include <time.h>

QueueHandle_t ReservationQueue = NULL;
Reservation_t reservation; // 全局预约变量

extern EventGroupHandle_t FunctionSelectionEventGroup;

void ReservationTask(void *pvParameters)
{

    ReservationQueue = xQueueCreate(3, sizeof(Reservation_t)); // 创建一个队列，最多容纳3个预约
    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

