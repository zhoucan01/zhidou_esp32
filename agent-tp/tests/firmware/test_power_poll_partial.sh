#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
out=build/test_power_poll_partial
mkdir -p "$out"
printf '#define ScreenBaud 1000000\n#define PowerBaud 1000000\n' > "$out/sys.h"
sed -n '/^static void poll_power_registers(/,/^static void poll_group(/{ /^static void poll_group(/!p; }' task/DataCenter/DataCenter.c > "$out/poll.inc"
cat > "$out/test.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ModbusMap.h"
typedef int nmbs_t;
#define NMBS_ERROR_NONE 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
static int input_cache_lock;
static struct { int lock; } command_queue;
static uint16_t power_input_cache[MB_POWER_CACHE_COUNT];
static bool power_input_cache_valid;
static unsigned power_calibration_status_generation;
static char version[3][4];
enum { breakMoter };
static bool moterState[1];
static struct { bool valid, meter_valid, enabled; uint16_t calibration_status;
    unsigned rpm, power_mw; int64_t received_us; } power_recovery_feedback;
static bool input_ok, cycle_ok, calibration_ok;
static unsigned calls;
static int64_t clock_us;
static int64_t esp_timer_get_time(void) { return clock_us; }
static bool select_slave(nmbs_t *c, unsigned slave)
{ (void)c; assert(slave == 3); return true; }
static int nmbs_read_input_registers(nmbs_t *c, unsigned a, unsigned n, uint16_t *r)
{
    (void)c; assert(a == 0 && n == MB_POWER_INPUT_COUNT);
    memset(r, 0, n * sizeof(*r));
    r[MB_POWER_INPUT_METER_VOLTAGE_HIGH + 1] = 23000;
    r[MB_POWER_INPUT_METER_CURRENT_HIGH + 1] = 1234;
    r[MB_POWER_INPUT_METER_ACTIVE_POWER_HIGH + 1] = 5678;
    r[MB_POWER_INPUT_METER_SAMPLE_VALID] = 1;
    return input_ok ? 0 : -1;
}
static uint32_t registers_to_u32(uint16_t h, uint16_t l)
{ return ((uint32_t)h << 16) | l; }
static void registers_to_version(const uint16_t *r, char *v)
{ v[0] = r[0] >> 8; v[1] = r[0]; v[2] = r[1] >> 8; v[3] = r[1]; }
static struct {
C
sed -n 's/.*g_DataPool\.\([a-zA-Z0-9_]*\).*/    uint32_t \1;/p' "$out/poll.inc" | sort -u >> "$out/test.c"
cat >> "$out/test.c" <<'C'
} g_DataPool;
static int nmbs_read_holding_registers(nmbs_t *c, unsigned a, unsigned n, uint16_t *r)
{
    (void)c; ++calls; clock_us += 100000;
    /* Input values must already be published before either FC03 request. */
    assert(g_DataPool.powerMeterActivePowerMw == 5678);
    if (a == MB_POWER_HOLD_CYCLE_THRESHOLD) {
        assert(n == 2); r[0] = 10; r[1] = 50; return cycle_ok ? 0 : -1;
    }
    assert(a == MB_POWER_HOLD_CALIBRATION_STATUS && n == 1);
    r[0] = 1; return calibration_ok ? 0 : -1;
}
#include "poll.inc"
int main(void)
{
    nmbs_t client = 0;
    for (unsigned mask = 0; mask < 8; ++mask) {
        memset(&g_DataPool, 0, sizeof(g_DataPool));
        g_DataPool.powerCycleThreshold = 77;
        g_DataPool.powerCalibrationStatus = 88;
        memset(power_input_cache, 0, sizeof(power_input_cache));
        power_input_cache[MB_POWER_INPUT_COUNT] = 77;
        power_input_cache_valid = false;
        power_calibration_status_generation = calls = 0;
        power_recovery_feedback.valid = false;
        input_ok = mask & 1; cycle_ok = mask & 2; calibration_ok = mask & 4;
        clock_us = 1000;
        poll_power_registers(&client);
        assert(calls == (input_ok ? 2U : 0U));
        assert(power_input_cache_valid == input_ok);
        assert(g_DataPool.powerMeterVoltageMv == (input_ok ? 23000U : 0U));
        assert(g_DataPool.powerMeterCurrentMa == (input_ok ? 1234U : 0U));
        assert(g_DataPool.powerMeterActivePowerMw == (input_ok ? 5678U : 0U));
        assert(power_input_cache[MB_POWER_INPUT_METER_SAMPLE_VALID] == input_ok);
        assert(g_DataPool.powerCycleThreshold == (input_ok && cycle_ok ? 10U : 77U));
        assert(power_input_cache[MB_POWER_INPUT_COUNT] == (input_ok && cycle_ok ? 10U : 77U));
        assert(g_DataPool.powerCalibrationStatus == (input_ok && calibration_ok ? 1U : 88U));
        assert(power_calibration_status_generation == (unsigned)(input_ok && calibration_ok));
        assert(power_recovery_feedback.valid == (input_ok && calibration_ok));
        if (input_ok) assert(power_recovery_feedback.received_us == 1000);
    }
    puts("PASS: all 8 input/cycle/calibration success-failure combinations");
}
C
cc -std=c99 -Wall -Wextra -Werror -I "$out" -I task/DataCenter "$out/test.c" -o "$out/test"
"$out/test"
