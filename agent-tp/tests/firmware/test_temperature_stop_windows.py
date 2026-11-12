"""Exercise production temperature gating/conversion across 3 s / 0.5 s cycles.

Run with python3 agent-tp/tests/firmware/test_temperature_stop_windows.py; requires GCC.
Only clock, RPM feedback and locks are simulated; no hardware timing claims.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build" / "test_temperature_stop_windows"
OUT.mkdir(parents=True, exist_ok=True)
source = (ROOT / "task/DataCenter/DataCenter.c").read_text()
gate = source[source.index("static float ntc_temperature_filtered;"):
              source.index("static bool ntc_temp_over98_active;")]
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "ntcTemp.c"
struct DataPool g_DataPool;
#define NTC_TEMPERATURE_MEDIAN_WINDOW 20U
#define NTC_ADC_MAX_STEP 200U
#define NTC_MOTOR_STOP_SETTLE_US 200000LL
static bool power_input_cache_valid;
static unsigned ntc_motor_stop_settle_samples_remaining;
static int64_t ntc_motor_stop_command_us;
static int64_t now_us;
int64_t esp_timer_get_time(void) { return now_us; }
#define portENTER_CRITICAL(lock) ((void)0)
#define portEXIT_CRITICAL(lock) ((void)0)
'''
harness += gate
harness += r'''
static bool sample(void)
{
    bool override;
    bool allowed = ntc_temperature_update_allowed(&override);
    ntcTempSetMotorStopSettleOverride(override);
    g_DataPool.ntc2FilteredRaw = filter_ntc2_adc(g_DataPool.ntc2Raw);
    if (allowed) {
        float t = ntcTempGetTemperature();
        assert(isfinite(t));
        g_DataPool.temperature = filter_ntc_temperature(t);
    }
    ntcTempSetMotorStopSettleOverride(false);
    return allowed;
}
int main(void)
{
    now_us = 1000000;
    g_DataPool.ntc1Raw = g_DataPool.ntc2Raw = g_DataPool.ntc3Raw = 2000;
    assert(sample()); /* Startup: cooling table works before power feedback. */
    assert(g_DataPool.temperature > 80.0f);
    ntcTempResetFilter();
    reset_ntc_temperature_filter();
    ntc2_adc_filter_ready = false;
    power_input_cache_valid = true;
    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater1Enabled = true;
    g_DataPool.powerHeater1Gear = 8;
    g_DataPool.temperature = 99.8f;
    g_DataPool.ntc1Raw = g_DataPool.ntc2Raw = g_DataPool.ntc3Raw = 1600;
    for (unsigned i = 0; i < 30; ++i) {
        now_us += 20000;
        (void)sample();
    }
    assert(ntc_adc_window_counts[0] == 20);
    assert(g_DataPool.temperature >= 98.0f);
    const float before = g_DataPool.temperature;
    g_DataPool.powerHeater1Enabled = false;
    g_DataPool.powerHeater1Gear = 0;
    const int64_t heater_off_us = now_us;

    for (unsigned cycle = 0; cycle < 5; ++cycle) {
        g_DataPool.powerMotorRpm = 100;
        ntc_motor_stop_settle_samples_remaining = 0;
        const float held = g_DataPool.temperature;
        const unsigned adc_count = ntc_adc_window_counts[0];
        for (unsigned i = 0; i < 150; ++i) {
            now_us += 20000;
            assert(!sample());
            assert(g_DataPool.temperature == held);
        }
        assert(ntc_adc_window_counts[0] == adc_count);

        ntc_motor_stop_command_us = now_us;
        ntc_motor_stop_settle_samples_remaining = 5;
        unsigned accepted = 0;
        for (unsigned i = 1; i <= 25; ++i) {
            now_us += 20000;
            /* A delayed station-3 RPM snapshot still says running at 200 ms. */
            g_DataPool.powerMotorRpm = i <= 10 ? 100 : 0;
            g_DataPool.ntc1Raw = g_DataPool.ntc2Raw = g_DataPool.ntc3Raw =
                1601 + cycle * 8 + i / 5;
            bool updated = sample();
            if (i < 10) assert(!updated);
            if (updated) ++accepted;
        }
        assert(accepted >= 8 && accepted < 20);
        assert(machine3_cooling_phase_latched ==
               (now_us - heater_off_us > NTC_HEATING_RECENT_US));
    }
    assert(g_DataPool.temperature < before - 0.2f);
    assert(ntc_adc_window_counts[0] == 20);
    /* A delayed zero-RPM update must not split the eligible 300 ms into two
     * small bursts: a real, coherent ADC change needs a contiguous 140 ms. */
    ntcTempResetFilter();
    reset_ntc_temperature_filter();
    ntc_motor_was_running = false;
    ntc_motor_stopped_since_us = 0;
    ntc_motor_stop_settle_samples_remaining = 0;
    g_DataPool.powerHeater1Enabled = true;
    g_DataPool.powerHeater1Gear = 10;
    g_DataPool.temperature = 82;
    g_DataPool.powerMotorRpm = 0;
    g_DataPool.ntc1Raw = g_DataPool.ntc2Raw = g_DataPool.ntc3Raw = 2300;
    for (unsigned i = 0; i < 30; ++i) {
        now_us += 20000; (void)sample();
    }
    g_DataPool.powerMotorRpm = 100;
    for (unsigned i = 0; i < 150; ++i) {
        now_us += 20000; assert(!sample());
    }
    ntc_motor_stop_command_us = now_us;
    ntc_motor_stop_settle_samples_remaining = 5;
    for (unsigned i = 1; i <= 25; ++i) {
        now_us += 20000;
        g_DataPool.powerMotorRpm = i <= 10 ? 100 : 0;
        g_DataPool.ntc1Raw = 2700;
        (void)sample();
    }
    assert(median_adc_window(0) == 2700);
    puts("PASS: 200 ms settle, late RPM, short stop sampling, retained ADC history,");
    puts("cooling transition and falling temperature across 3 s / 0.5 s cycles");
}
'''
path = OUT / "stop_windows.c"
path.write_text(harness)
executable = OUT / "stop_windows"
subprocess.run([
    "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-DMACHINE=3",
    "-DNTC_TEMP_LOG_ENABLE=0", "-DNTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE=1", "-I", str(ROOT / "function/ntcTemp"),
    "-I", str(ROOT / "agent-tp/tests/firmware/ntc_host"),
    "-I", str(ROOT / "task/DataCenter"), str(path), "-lm", "-o", str(executable),
], check=True)
subprocess.run([str(executable)], check=True)
