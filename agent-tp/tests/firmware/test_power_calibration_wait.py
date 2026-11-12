"""Exercise the production calibration wait with simulated fresh/stale polls.

Generated C and executable remain in the repository build directory.
"""
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[3]
SOURCE = (ROOT / 'task/DataCenter/DataCenter.c').read_text(encoding='utf-8')
WAIT = SOURCE[SOURCE.index('bool runPowerCalibration(uint32_t timeout_ms)'):
              SOURCE.index('bool DataCenterSetPowerMotorDirectionMode(')]
OUT = ROOT / 'build' / 'test_power_calibration_wait'
OUT.mkdir(parents=True, exist_ok=True)

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define TAG "test"
#define MB_COMMAND_ENABLE_POWER_FOR_CALIBRATION 1
#define MODBUS_SLAVE_POWER 3
#define pdMS_TO_TICKS(value) (value)
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
typedef uint32_t TickType_t;
typedef struct { int type, slave; bool reply_to_debug; } modbus_command_t;
static struct { bool powerEnabled; uint16_t powerCalibrationStatus; } g_DataPool;
static uint32_t power_calibration_status_generation;
static int input_cache_lock;
static uint32_t now_ms;
static int scenario;
static TickType_t xTaskGetTickCount(void) { return now_ms; }
static bool command_queue_push(const modbus_command_t *command)
{ (void)command; return true; }
static bool setPowerCalibration(void) { return true; }
static void vTaskDelay(TickType_t delay)
{
    now_ms += delay;
    if (scenario == 1 && now_ms % 20000U == 0U && now_ms <= 80000U) {
        g_DataPool.powerCalibrationStatus = 1;
        ++power_calibration_status_generation;
    } else if (scenario == 1 && now_ms == 100000U) {
        g_DataPool.powerCalibrationStatus = 2;
        ++power_calibration_status_generation;
    } else if (scenario == 2 && now_ms == 20000U) {
        g_DataPool.powerCalibrationStatus = 1;
        ++power_calibration_status_generation;
    } else if (scenario == 4 && now_ms == 10000U) {
        g_DataPool.powerCalibrationStatus = 3;
        ++power_calibration_status_generation;
    }
}
'''
HARNESS += WAIT
HARNESS += r'''
int main(void)
{
    for (scenario = 0; scenario <= 4; ++scenario) {
        now_ms = 0;
        g_DataPool.powerEnabled = true;
        g_DataPool.powerCalibrationStatus = scenario == 3 ? 1 : 0;
        power_calibration_status_generation = scenario == 3 ? 1 : 0;
        bool result = runPowerCalibration(60000U);
        if (scenario == 1) {
            assert(result && now_ms == 100000U);
        } else if (scenario == 2) {
            assert(!result && now_ms == 80000U);
        } else if (scenario == 4) {
            assert(!result && now_ms == 10000U);
        } else {
            assert(!result && now_ms == 60000U);
        }
    }
    return 0;
}
'''

c_file = OUT / 'wait.c'
binary = OUT / 'wait'
c_file.write_text(HARNESS, encoding='utf-8')
subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                str(c_file), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
print('power calibration wait: 5 scenarios passed')
