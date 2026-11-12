/* 编译正式的传感器快照读取函数，仅模拟时间、缓存和锁。 */
#include <stddef.h>
#include <string.h>
#include "DataCenter.h"
#include "freertos/task.h"

enum { MB_ACT_INPUT_DISCRETE_BITMAP = 0x000e };
static uint16_t actuator_input_cache[MB_ACT_INPUT_DISCRETE_BITMAP + 1];
static bool actuator_input_cache_valid;
static int64_t actuator_input_cache_time_us;
static int input_cache_lock;
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
static int64_t esp_timer_get_time(void)
{
    return (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS * 1000;
}

#include "sensor_snapshot_impl.inc"

void TestSensorSnapshotReset(void)
{
    memset(actuator_input_cache, 0, sizeof(actuator_input_cache));
    actuator_input_cache_valid = false;
    actuator_input_cache_time_us = 0;
}

void TestSensorSnapshotUpdate(void)
{
    uint16_t inputs = 0;
    for (unsigned i = 0; i < sensorNum; ++i) {
        if (sensorState[i]) inputs |= (uint16_t)(1U << i);
    }
    actuator_input_cache[MB_ACT_INPUT_DISCRETE_BITMAP] = inputs;
    actuator_input_cache_time_us = esp_timer_get_time();
    actuator_input_cache_valid = true;
}
