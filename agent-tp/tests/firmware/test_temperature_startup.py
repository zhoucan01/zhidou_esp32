"""Verify station-2 temperature is available before station-3 power feedback.

Generated host harness and binary stay under the repository build directory.
"""
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build' / 'test_temperature_startup'
OUT.mkdir(parents=True, exist_ok=True)
source = (ROOT / 'task/DataCenter/DataCenter.c').read_text(encoding='utf-8')
gate = source[source.index('static bool ntc_temperature_update_allowed('):
              source.index('static uint32_t filter_ntc2_adc(')]
harness = r'''
#include <assert.h>
#include <math.h>
#include <string.h>
#include "ntcTemp.c"
struct DataPool g_DataPool;
static int64_t now_us;
int64_t esp_timer_get_time(void) { return now_us; }
static bool power_input_cache_valid;
static int64_t ntc_motor_stop_command_us;
static uint8_t ntc_motor_stop_settle_samples_remaining;
static bool ntc_motor_was_running;
static int64_t ntc_motor_stopped_since_us;
static int input_cache_lock;
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define NTC_MOTOR_STOP_SETTLE_US 200000LL
static void reset_ntc_temperature_filter(void) {}
'''
harness += gate
harness += r'''
int main(void)
{
    assert(NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE == 1);
    g_DataPool.ntc1Raw = 2000U;
    g_DataPool.ntc2Raw = 2000U;
    g_DataPool.ntc3Raw = 2000U;
    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater1Enabled = true;
    assert(!machine3_heating_requested()); /* Enabled, but gear not known. */
    g_DataPool.powerEnabled = false;
    g_DataPool.powerHeater1Enabled = false;
    /* No station-3 snapshot yet: all gear and RPM fields are unknown/zero. */
    for (unsigned i = 0; i < 20U; ++i) {
        bool override = true;
        now_us += 20000;
        assert(ntc_temperature_update_allowed(&override));
        assert(!override);
        float temperature = ntcTempGetTemperature();
        assert(isfinite(temperature) && fabsf(temperature - 87.0f) < 1.0f);
    }
    /* Below the measured cooling span, use only measured low-temperature
     * nodes from this machine; never claim a 4095/open-circuit ADC is valid. */
    assert(ntcTempIsInRange(3400U));
    assert(ntcTempIsInRange(2600U));
    assert(ntcTempIsInRange(2800U));
    assert(ntcTempIsInRange(3100U));
    assert(!ntcTempIsInRange(0U));
    assert(!ntcTempIsInRange(4095U));
    g_DataPool.ntc1Raw = 3400U;
    g_DataPool.ntc2Raw = 3400U;
    g_DataPool.ntc3Raw = 3400U;
    ntcTempResetFilter();
    for (unsigned i = 0; i < 20U; ++i) {
        now_us += 20000;
        float temperature = ntcTempGetTemperature();
        assert(isfinite(temperature) && temperature > 25.0f && temperature < 60.0f);
    }
    power_input_cache_valid = true;
    g_DataPool.powerMotorRpm = 100U;
    bool override = false;
    assert(!ntc_temperature_update_allowed(&override));
    return 0;
}
'''
path = OUT / 'test.c'
path.write_text(harness, encoding='utf-8')
binary = OUT / 'test'
subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-DMACHINE=3', '-DNTC_TEMP_LOG_ENABLE=0',
                '-I' + str(ROOT / 'function/ntcTemp'),
                '-I' + str(ROOT / 'task/DataCenter'),
                '-I' + str(ROOT / 'agent-tp/tests/firmware/ntc_host'),
                str(path), '-lm', '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
print('PASS: startup cooling temperature before power snapshot')
