#ifndef HOST_FREERTOS_H
#define HOST_FREERTOS_H
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define portTICK_PERIOD_MS 1U
#endif
