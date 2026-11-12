"""Compile real NTC filters: dense noise must not accumulate into trusted history.

Run: python3 agent-tp/tests/firmware/test_ntc_reaccept.py (requires GCC).
Controlled timestamps exercise production timing; no UART/physical claims.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/test_ntc_reaccept"
OUT.mkdir(parents=True, exist_ok=True)
SOURCE = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ntcTemp.c"
struct DataPool g_DataPool;
static int64_t now;
int64_t esp_timer_get_time(void) { return now; }
static unsigned adc(unsigned value) {
    now += 20000;
    return update_adc_window(0, value, now);
}
static float temp(float value) {
    now += 20000;
    return machine3_trend_filter(0, value, now);
}
static void seed_adc(void) {
    ntcTempResetFilter(); now = 1000000;
    for (unsigned i = 0; i < 20; ++i) assert(adc(2000) == 2000);
}
static void seed_temperature(void) {
    ntcTempResetFilter(); now = 1000000;
    for (unsigned i = 0; i < 100; ++i) assert(temp(85) == 85);
}
int main(void) {
    /* The new 3 s / 2 s motor-cooling database is empty until measured data
     * arrives, so existing cooling points remain the temporary fallback. */
    assert(MACHINE3_MOTOR_COOLING_COUNT == 0U);
    NtcTempMachine3OperatingConditions cooling =
        machine3_default_operating_conditions(false, 0U);
    size_t cooling_count = 0U;
    assert(cooling.motor_on_seconds == 3.0f && cooling.motor_off_seconds == 2.0f);
    assert(machine3_table_for_phase(0U, 0U, &cooling, &cooling_count) ==
           machine3_operating_table);
    assert(cooling_count == sizeof(machine3_operating_table) /
                            sizeof(machine3_operating_table[0]));

    /* A real heater-on feedback protects the heating table for 10 seconds,
     * even when the cooling ADC trend is already visible. After the window,
     * the existing trend latch takes over; power-off discards the history. */
    ntcTempResetFilter(); now = 1000000;
    machine3_last_heater_gear = 0U;
    machine3_last_heater_on_us = 0;
    memset(machine3_cooling_phase_latched, 0, sizeof(machine3_cooling_phase_latched));
    g_DataPool = (struct DataPool){0};
    g_DataPool.powerEnabled = true;
    g_DataPool.powerHeater1Enabled = true;
    assert(!machine3_heating_requested(0U)); /* Unknown gear is not evidence. */
    g_DataPool.powerHeater1Gear = 9U;
    g_DataPool.temperature = 98.0f;
    machine3_temperature_valid[0] = true;
    machine3_last_valid_temperatures[0] = 98.0f;
    assert(machine3_heating_requested(0U));
    const int64_t heater_on_us = machine3_last_heater_on_us;
    g_DataPool.powerHeater1Enabled = false;
    g_DataPool.powerHeater1Gear = 0U;
    ntc_adc_window_counts[0] = NTC_MEDIAN_WINDOW_SIZE;
    ntc_adc_window_next[0] = 0U;
    for (unsigned i = 0; i < NTC_MEDIAN_WINDOW_SIZE; ++i)
        ntc_adc_windows[0][i] = 1600U + i;
    now = heater_on_us + NTC_HEATING_RECENT_US;
    assert(machine3_heating_requested(0U));
    assert(!machine3_cooling_phase_latched[0]);
    ++now;
    assert(!machine3_heating_requested(0U));
    assert(machine3_cooling_phase_latched[0]);
    g_DataPool.powerHeater1Enabled = true;
    g_DataPool.powerHeater1Gear = 9U;
    assert(machine3_heating_requested(0U));
    assert(!machine3_cooling_phase_latched[0]);
    g_DataPool.powerEnabled = false;
    assert(!machine3_heating_requested(0U));
    assert(machine3_last_heater_on_us == 0);
    puts("PASS: heater feedback, 10-second hold, cooling trend and empty cooling fallback.");

    assert(normalize_output_temperature(-10) == 25);
    assert(normalize_output_temperature(25) == 25);
    assert(normalize_output_temperature(99) == 99);
    assert(normalize_output_temperature(100) == 100);
    assert(normalize_output_temperature(110) == 100);
    assert(isnan(normalize_output_temperature(NAN)));
    /* Missing water volumes select the nearest group in the same phase/channel. */
    assert(machine3_default_operating_conditions(false, 0U).water_volume_ml == 1150U);
    for (size_t i = 0; i < sizeof(machine3_heating_profiles) /
                            sizeof(machine3_heating_profiles[0]); ++i)
        assert(machine3_heating_profiles[i].conditions.water_volume_ml == 1150U);
    for (size_t i = 0; i < sizeof(machine3_operating_table) /
                            sizeof(machine3_operating_table[0]); ++i) {
        const NtcMachine3OperatingPoint *p = &machine3_operating_table[i];
        NtcTempMachine3OperatingConditions c = {
            p->heater_gear, p->motor_gear, p->heater_on_seconds,
            p->heater_off_seconds, p->motor_on_seconds,
            p->motor_off_seconds, 1150U,
        };
        const NtcMachine3OperatingPoint *exact = machine3_nearest_profile(
            machine3_operating_table,
            sizeof(machine3_operating_table) / sizeof(machine3_operating_table[0]),
            p->channel, p->phase, &c);
        assert(exact && machine3_same_profile(exact, p));
        /* ADC inputs are integers; ceil(maximum fractional node) lies outside
         * measured coverage. Use the adjacent in-range integer at that edge. */
        unsigned value = (unsigned)ceilf(p->point.adc);
        if (!machine3_operating_profile_contains(p->channel, p->phase, &c, value))
            value = (unsigned)floorf(p->point.adc);
        assert(isfinite(machine3_channel_temperature(p->channel, value, p->phase, &c)));
        c.water_volume_ml = 1000U;
        const NtcMachine3OperatingPoint *nearest = machine3_nearest_profile(
            machine3_operating_table,
            sizeof(machine3_operating_table) / sizeof(machine3_operating_table[0]),
            p->channel, p->phase, &c);
        assert(nearest && nearest->water_volume_ml == 1150U);
        assert(isfinite(machine3_channel_temperature(p->channel, value, p->phase, &c)));
    }
    puts("PASS: default water volume 1150 mL; 1000 mL queries select 1150 mL groups.");
    const NtcMachine3OperatingPoint fixtures[] = {
        {0, 1, 9, 1, 0, 1, 2, 6, 1150, {2000, 80}},
        {0, 1, 10, 1, 0, 8, 2, 6, 1600, {2000, 81}},
        {0, 1, 10, 1, 0, 9, 2, 6, 1150, {2000, 82}},
        {0, 1, 10, 1, 0, 2, 2, 6, 1150, {2000, 83}},
        {0, 0, 0, 1, 0, 1, 2, 6, 1150, {2000, 84}},
    };
    NtcTempMachine3OperatingConditions query = {10, 1, 1, 0, 2, 6, 1150};
    assert(machine3_nearest_profile(fixtures, 2, 0, 1, &query) == &fixtures[1]);
    assert(machine3_nearest_profile(fixtures, 3, 0, 1, &query) == &fixtures[1]);
    assert(machine3_nearest_profile(fixtures, 5, 0, 1, &query) == &fixtures[3]);
    assert(!machine3_nearest_profile(fixtures, 5, 1, 1, &query));
    assert(machine3_nearest_profile(fixtures, 5, 0, 0, &query) == &fixtures[4]);
    puts("PASS: power > motor > water priority; phase/channel isolation.");
    seed_adc();
    uint32_t trusted[NTC_MEDIAN_WINDOW_SIZE];
    memcpy(trusted, ntc_adc_windows[0], sizeof(trusted));
    for (unsigned i = 0; i < 10000; ++i) {
        unsigned value = i % 3 == 0 ? 2400 : i % 3 == 1 ? 1600 : 2800;
        assert(adc(value) == 2000);
    }
    assert(memcmp(trusted, ntc_adc_windows[0], sizeof(trusted)) == 0);
    /* Even seven coherent bad points per burst cannot accumulate over cycles. */
    for (unsigned cycle = 0; cycle < 1000; ++cycle) {
        for (unsigned i = 0; i < 7; ++i) assert(adc(2301) == 2000);
        assert(adc(2000) == 2000);
        assert(ntc_adc_candidates[0].count == 0);
    }
    for (unsigned i = 0; i < 7; ++i) assert(adc(2301) == 2000);
    now += NTC_STOP_EVIDENCE_MAX_GAP_US;
    assert(adc(2301) == 2000 && ntc_adc_candidates[0].count == 1);
    int64_t duplicate_time = now;
    for (unsigned i = 0; i < 100; ++i)
        assert(update_adc_window(0, 2301, duplicate_time) == 2000);
    assert(ntc_adc_candidates[0].count == 0);
    for (unsigned i = 0; i < 7; ++i) assert(adc(2301) == 2000);
    assert(adc(4095) == 2000 && ntc_adc_candidates[0].count == 0);
    assert(adc(0) == 2000 && !ntc_adc_sample_accepted[0]);
    assert(adc(UINT32_MAX) == 2000 && !ntc_adc_sample_accepted[0]);
    for (unsigned i = 0; i < 7; ++i) assert(adc(2301) == 2000);
    ntcTempResetTemperatureTrend();
    assert(adc(2301) == 2000 && ntc_adc_candidates[0].count == 1);
    for (unsigned i = 1; i < 7; ++i) assert(adc(2301) == 2000);
    assert(adc(2301) == 2301 && ntc_adc_sample_accepted[0]);
    /* Symmetric reacquisition: a persistent decrease is not an unfiltered spike. */
    seed_adc();
    for (unsigned i = 0; i < 7; ++i) assert(adc(1700) == 2000);
    assert(adc(1700) == 1700);
    /* Many points at a single instant cannot satisfy the confirmation duration. */
    seed_adc();
    for (unsigned i = 0; i < 100; ++i) {
        now += 100;
        assert(update_adc_window(0, 2301, now) == 2000);
    }

    seed_temperature();
    for (unsigned i = 0; i < 10000; ++i)
        assert(temp(i % 2 == 0 ? 87 : 83) == 85);
    assert(ntc_temperature_filters[0].sample_count == 100);
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        for (unsigned i = 0; i < 7; ++i) assert(temp(87) == 85);
        assert(temp(85) == 85);
    }
    for (unsigned i = 0; i < 7; ++i) assert(temp(87) == 85);
    assert(temp(NAN) == 85 && ntc_temperature_filters[0].candidate_count == 0);
    for (unsigned i = 0; i < 7; ++i) assert(temp(87) == 85);
    now += NTC_STOP_EVIDENCE_MAX_GAP_US;
    assert(temp(87) == 85 && ntc_temperature_filters[0].candidate_count == 1);

    /* Sparse recovery after a long gap: neither layer may freeze on the old
     * baseline simply because observations arrive more slowly than 100 ms. */
    const int64_t intervals[] = {200000, 1000000, 2000000};
    for (unsigned n = 0; n < sizeof(intervals) / sizeof(intervals[0]); ++n) {
        seed_adc(); now += 3000000;
        for (unsigned i = 0; i < 7; ++i) {
            now += intervals[n];
            assert(update_adc_window(0, 2301, now) == 2000);
        }
        now += intervals[n];
        assert(update_adc_window(0, 2301, now) == 2301);
        assert(ntc_adc_sample_accepted[0]);
        seed_temperature(); now += 3000000;
        for (unsigned i = 0; i < 7; ++i) {
            now += intervals[n];
            assert(machine3_trend_filter(0, 87, now) == 85);
        }
        now += intervals[n];
        assert(machine3_trend_filter(0, 87, now) == 87);
    }
    puts("PASS: ADC/temperature recover after a 3 s outage at 200 ms, 1 s and 2 s cadence.");

    seed_temperature();
    /* 2 C in a fresh 140 ms burst is not immediately a plausible new level. */
    for (unsigned i = 0; i < 8; ++i) assert(temp(87) == 85);
    seed_temperature();
    now += 3000000;
    for (unsigned i = 0; i < 7; ++i) assert(temp(87) == 85);
    assert(temp(87) == 87); /* Same 2 C change after a real 3 s gap is accepted. */

    seed_temperature();
    now += 3000000;
    ntcTempResetTemperatureTrend();
    assert(ntc_temperature_filters[0].sample_count == 1);
    for (unsigned i = 0; i < 7; ++i) assert(temp(87) == 85);
    assert(temp(87) == 87); /* Motor reset retains baseline, not startup bypass. */

    seed_temperature();
    for (unsigned i = 1; i <= 200; ++i) {
        float truth = 85 + (float)i * .004f;
        assert(fabsf(temp(truth) - truth) < .001f);
    }
    /* Fast ramps in both directions must reacquire while still moving. */
    for (int direction = -1; direction <= 1; direction += 2) {
        seed_temperature();
        float output = 85;
        for (unsigned i = 1; i <= 200; ++i) {
            const float truth = 85 + direction * (float)i * .06f;
            output = temp(truth);
            assert(fabsf(output - truth) < 1.0f);
        }
        assert(fabsf(output - (85 + direction * 12)) < 1.0f);
        seed_adc();
        for (unsigned i = 0; i < 7; ++i)
            assert(adc(2300 + i * 10) == 2000);
        assert(adc(2370) != 2000);
        seed_adc();
        for (unsigned i = 0; i < 7; ++i)
            assert(adc(1700 - i * 10) == 2000);
        assert(adc(1630) != 2000);
    }
    puts("PASS: moving ADC candidates and +/-3 C/s temperature ramps.");
    /* Both phases: backups never replace an unavailable primary. */
    for (unsigned heating = 0; heating <= 1; ++heating) {
        ntcTempResetFilter(); now = 1000000;
        /* Simulate a cold start; phase resets intentionally retain valid values. */
        memset(machine3_temperature_valid, 0, sizeof(machine3_temperature_valid));
        machine3_last_heater_gear = 0;
        memset(machine3_cooling_phase_latched, 0, sizeof(machine3_cooling_phase_latched));
        g_DataPool = (struct DataPool){0};
        g_DataPool.powerEnabled = heating;
        g_DataPool.powerHeater1Enabled = heating;
        g_DataPool.powerHeater1Gear = 8;
        g_DataPool.temperature = 95;
        NtcTempMachine3OperatingConditions c = machine3_default_operating_conditions(heating, 0U);
        unsigned raw[3];
        for (unsigned ch = 0; ch < 3; ++ch) {
            float best = INFINITY;
            for (unsigned a = 1; a < 4095; ++a) {
                float t = machine3_channel_temperature(ch, a, heating, &c);
                float error = fabsf(t - (ch == 0 ? 95 : 100));
                if (isfinite(t) && error < best) { best = error; raw[ch] = a; }
            }
            assert(best < .2f);
        }
        g_DataPool.ntc2Raw = raw[1]; g_DataPool.ntc3Raw = raw[2];
        g_DataPool.ntc1Raw = 4090;
        for (unsigned i = 0; i < 200; ++i) {
            now += 20000; assert(isnan(ntcTempGetTemperature()));
        }
        g_DataPool.ntc1Raw = raw[0];
        for (unsigned i = 0; i < 100; ++i) {
            now += 20000; ntcTempGetTemperature();
        }
        assert(fabsf(ntcTempGetTemperature() - 95) < .2f);
        g_DataPool.ntc1Raw = 4090;
        for (unsigned i = 0; i < 200; ++i) { now += 20000; ntcTempGetTemperature(); }
        assert(fabsf(ntcTempGetTemperature() - 95) < .2f); /* keep primary's last valid value */
        now += 20000;
        assert(fabsf(ntcTempGetTemperature() - 95) < .2f);
        /* Losing another channel also cannot change the primary source. */
        g_DataPool.ntc3Raw = 4095;
        for (unsigned i = 0; i < 200; ++i) { now += 20000; ntcTempGetTemperature(); }
        now += 20000; assert(fabsf(ntcTempGetTemperature() - 95) < .2f);
        g_DataPool.ntc1Raw = raw[0];
        for (unsigned i = 0; i < 100; ++i) { now += 20000; ntcTempGetTemperature(); }
        assert(fabsf(ntcTempGetTemperature() - 95) < .2f);
    }
    puts("PASS: heating/cooling primary startup/failure/recovery without backup switching.");
    /* Per-channel phase and profile selection never consult other sensors. */
    for (unsigned channel = 0; channel < 3; ++channel) {
        ntcTempResetFilter(); now = 20000000;
        g_DataPool = (struct DataPool){0};
        g_DataPool.powerEnabled = true;
        machine3_last_heater_gear = 9;
        machine3_last_heater_on_us = 1;
        memset(machine3_cooling_phase_latched, 0, sizeof(machine3_cooling_phase_latched));
        for (unsigned other = 0; other < 3; ++other) {
            machine3_temperature_valid[other] = true;
            machine3_last_valid_temperatures[other] = other == channel ? 90 : 100;
            ntc_adc_window_counts[other] = NTC_MEDIAN_WINDOW_SIZE;
            for (unsigned i = 0; i < NTC_MEDIAN_WINDOW_SIZE; ++i)
                ntc_adc_windows[other][i] = 1600 + i;
        }
        g_DataPool.temperature = 100;
        assert(machine3_heating_requested(channel));
        assert(!machine3_cooling_phase_latched[channel]);
        NtcTempMachine3OperatingConditions own = machine3_default_operating_conditions(true, channel);
        assert(own.heater_off_seconds == 15);
        for (unsigned other = 0; other < 3; ++other) {
            if (other == channel) continue;
            assert(!machine3_heating_requested(other));
            assert(machine3_cooling_phase_latched[other]);
            machine3_last_valid_temperatures[other] = -999;
            memset(ntc_adc_windows[other], 0, sizeof(ntc_adc_windows[other]));
        }
        g_DataPool.temperature = NAN;
        assert(machine3_heating_requested(channel));
        own = machine3_default_operating_conditions(true, channel);
        assert(own.heater_off_seconds == 15);
        machine3_last_valid_temperatures[channel] = 98;
        assert(!machine3_heating_requested(channel));
        assert(machine3_cooling_phase_latched[channel]);
    }
    puts("PASS: each channel owns phase/98C latch/profile; other NTCs and main temperature cannot affect it.");

    /* Motor-running admission uses actual same-channel ADC/time history. */
    g_DataPool = (struct DataPool){0}; seed_adc();
    g_DataPool.powerMotorRpm = 500;
    for (unsigned i = 0; i < 100; ++i) {
        assert(adc(2000 + i) >= 2000);
        assert(ntc_adc_sample_accepted[0]);
    }
    uint32_t motor_trusted[NTC_MEDIAN_WINDOW_SIZE];
    memcpy(motor_trusted, ntc_adc_windows[0], sizeof(motor_trusted));
    adc(2600);
    assert(!ntc_adc_sample_accepted[0]);
    assert(!memcmp(motor_trusted, ntc_adc_windows[0], sizeof(motor_trusted)));
    assert(!machine3_motor_adc_matches(1, 2000, now)); /* Other channel cannot borrow history. */
    now += NTC_CANDIDATE_MAX_GAP_US + 1;
    adc(2100); assert(!ntc_adc_sample_accepted[0]);
    g_DataPool.powerMotorRpm = 0;
    seed_adc();
    for (unsigned i = 0; i < NTC_MEDIAN_WINDOW_SIZE; ++i)
        ntc_adc_windows[0][i] = i % 2 ? 2100 : 1900;
    g_DataPool.powerMotorRpm = 500;
    assert(!machine3_motor_adc_matches(0, 2000, now + 20000));
    g_DataPool.powerMotorRpm = 0;
    puts("PASS: running motor accepts own ADC ramp; rejects off-trend, stale, noisy or missing history.");

    /* Full conversion path: rejected ADCs must not train temperature history. */
    ntcTempResetFilter(); now = 1000000;
    g_DataPool = (struct DataPool){0};
    g_DataPool.powerEnabled = g_DataPool.powerHeater1Enabled = true;
    g_DataPool.powerHeater1Gear = 8;
    g_DataPool.temperature = 99;
    g_DataPool.ntc1Raw = g_DataPool.ntc2Raw = g_DataPool.ntc3Raw = 1700;
    float baseline = 0;
    for (unsigned i = 0; i < 30; ++i) {
        now += 20000;
        baseline = ntcTempGetTemperature();
    }
    assert(isfinite(baseline));
    assert(ntcTempPrimarySampleAccepted());
    g_DataPool.powerMotorRpm = 500;
    for (unsigned i = 0; i < 10; ++i) {
        now += 20000;
        assert(ntcTempGetTemperature() == baseline);
        assert(ntcTempPrimarySampleAccepted());
    }
    g_DataPool.ntc1Raw = 4095;
    now += 20000;
    assert(ntcTempGetTemperature() == baseline);
    assert(!ntcTempPrimarySampleAccepted());
    g_DataPool.ntc1Raw = 1700;
    g_DataPool.powerMotorRpm = 0;
    unsigned history_count = ntc_temperature_filters[0].sample_count;
    for (unsigned i = 0; i < 1000; ++i) {
        now += 20000;
        g_DataPool.ntc1Raw = g_DataPool.ntc2Raw = g_DataPool.ntc3Raw =
            i % 2 ? 1100 : 2400;
        assert(ntcTempGetTemperature() == baseline);
    }
    assert(ntc_temperature_filters[0].sample_count == history_count);
    assert(!ntcTempPrimarySampleAccepted());
    puts("PASS: 10,000 alternating ADC spikes, 1,000 seven-point bursts, timing gaps,");
    puts("duplicate timestamps, invalid ADC, symmetric reacquisition, model isolation,");
    puts("temperature noise, motor boundaries, real elapsed-time changes and slow ramp.");
}
'''
path = OUT / "test.c"
path.write_text(SOURCE)
binary = OUT / "test"
subprocess.run([
    "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-DMACHINE=3",
    "-DNTC_TEMP_LOG_ENABLE=0", "-I", str(ROOT / "function/ntcTemp"),
    "-I", str(ROOT / "task/DataCenter"),
    "-I", str(ROOT / "agent-tp/tests/firmware/ntc_host"),
    str(path), "-lm", "-o", str(binary),
], check=True)
subprocess.run([str(binary)], check=True)
