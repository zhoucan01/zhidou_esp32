#ifndef HOST_FREERTOS_TASK_H
#define HOST_FREERTOS_TASK_H
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
int xTaskCreate(TaskFunction_t task, const char *name, unsigned stack,
                void *arg, unsigned priority, void *handle);
void vTaskDelete(void *task);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
#endif
