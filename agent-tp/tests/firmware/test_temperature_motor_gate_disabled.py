"""Verify default sampling continues with a running motor and no power cache."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/test_temperature_motor_gate_disabled'
OUT.mkdir(parents=True, exist_ok=True)
source = (ROOT / 'task/DataCenter/DataCenter.c').read_text()
gate = source[source.index('static bool ntc_temperature_update_allowed('):
              source.index('static uint32_t filter_ntc2_adc(')]
harness = r'''
#include <assert.h>
#include <math.h>
#include "ntcTemp.c"
struct DataPool g_DataPool;
static int64_t now;
int64_t esp_timer_get_time(void) { return now; }
'''
harness += gate
harness += r'''
int main(void) {
    assert(NTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE == 0);
    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater1Enabled = true;
    g_DataPool.powerHeater1Gear = 19;
    g_DataPool.powerMotorGear = -1;
    g_DataPool.powerMotorRpm = 996;
    g_DataPool.temperature = 30;
    g_DataPool.ntc1Raw = 3390;
    g_DataPool.ntc2Raw = 3385;
    g_DataPool.ntc3Raw = 3379;
    float initial = NAN, final = NAN;
    for (unsigned i = 0; i < 200; ++i) {
        bool override = true;
        now += 20000;
        assert(ntc_temperature_update_allowed(&override));
        assert(!override);
        g_DataPool.ntc1Raw = 3390 - i / 4;
        final = ntcTempGetTemperature();
        assert(isfinite(final));
        if (i == 0) initial = final;
    }
    assert(final > initial + 0.1f);
    assert(ntcTempNtc1IsInRange(g_DataPool.ntc1Raw));
    assert(isfinite(ntcTempGetNtc1Temperature()));
}
'''
path = OUT / 'test.c'
path.write_text(harness)
exe = OUT / 'test'
subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-DMACHINE=3', '-DNTC_TEMP_LOG_ENABLE=0',
                '-DNTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE=0',
                '-I' + str(ROOT / 'function/ntcTemp'),
                '-I' + str(ROOT / 'task/DataCenter'),
                '-I' + str(ROOT / 'agent-tp/tests/firmware/ntc_host'),
                str(path), '-lm', '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
print('PASS: default gate disabled, 996 RPM, no stop override, ADC decrease updates temperature.')
