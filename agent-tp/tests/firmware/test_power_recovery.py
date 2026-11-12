"""Host checks of production target tracking, recovery decisions and RTU writes.

Run: python3 agent-tp/tests/firmware/test_power_recovery.py (GCC required). UART/time/locks are
stubbed; all generated files stay under build/.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build" / "test_power_recovery"
OUT.mkdir(parents=True, exist_ok=True)
(OUT / "sys.h").write_text("#define ScreenBaud 1000000\n#define PowerBaud 1000000\n")
source = (ROOT / "task/DataCenter/DataCenter.c").read_text()
state = source[source.index("typedef enum {"):
               source.index("static void queue_coil(")]
execute = source[source.index("static nmbs_error execute_command("):
                 source.index("static float registers_to_float(")]
setters = source[source.index("static void queue_power_channel("):
                 source.index("bool setPowerCalibration(")]
register_queue = source[source.index("static void queue_register("):
                        source.index("static void queue_signed_motor_speed(")]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "DataCenter.h"
#include "HeatingCommand.h"
#define COMMAND_QUEUE_CAPACITY 32U
#define MODBUS_GATEWAY_FRAME_MAX 256U
#define NTC_MOTOR_STOP_SETTLE_SAMPLES 5U
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
typedef void *TaskHandle_t;
typedef unsigned TickType_t;
typedef int nmbs_t;
typedef int nmbs_error;
typedef int uart_port_t;
#define NMBS_ERROR_NONE 0
#define NMBS_ERROR_TRANSPORT 1
#define NMBS_ERROR_INVALID_ARGUMENT 2
#define NMBS_ERROR_TIMEOUT 3
#define PowerUart 1
#define ScreenUart 2
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
static int64_t now_us;
static int64_t ntc_motor_stop_command_us;
static unsigned ntc_motor_stop_settle_samples_remaining;
static int input_cache_lock;
static bool power_calibration_response_trace;
static int64_t esp_timer_get_time(void) { return now_us; }
static TickType_t xTaskGetTickCount(void) { return (TickType_t)(now_us / 1000); }
static void vTaskDelayUntil(TickType_t *wake, TickType_t delay) { *wake += delay; }
static void vTaskDelay(TickType_t delay) { (void)delay; }
static void uart_modbus_reset_transport(int uart) { (void)uart; }
static void xTaskNotifyGive(TaskHandle_t t) { (void)t; }
static bool select_slave(nmbs_t *client, uint8_t slave)
{ (void)client; return slave == MODBUS_SLAVE_POWER; }
static bool execute_raw_gateway(void) { return true; }
static unsigned writes;
static uint16_t written_address[20], written_value[20];
static bool cancel_on_zero;
static unsigned fail_writes;
static nmbs_error nmbs_write_single_register(nmbs_t *, uint16_t, uint16_t);
static nmbs_error nmbs_write_single_coil(nmbs_t *c, uint16_t a, bool v)
{ (void)c; (void)a; (void)v; return NMBS_ERROR_NONE; }
modbus_test_stats_t g_ModbusTestStats;
'''
harness += state + register_queue + setters + execute
harness += r'''
static nmbs_error nmbs_write_single_register(nmbs_t *c, uint16_t a, uint16_t v)
{
    (void)c;
    assert(writes < 20);
    written_address[writes] = a;
    written_value[writes++] = v;
    if (cancel_on_zero && v == 0) {
        cancel_on_zero = false;
        setPowerEnabled(false); /* Simulate stop arriving while waiting for ACK. */
    }
    if (fail_writes) { --fail_writes; return NMBS_ERROR_TIMEOUT; }
    return NMBS_ERROR_NONE;
}
static void reset(void)
{
    command_queue = (command_ring_buffer_t){0};
    g_TargetHeater1Gear = g_TargetHeater2Gear = 0;
    g_TargetPowerMotorGear = 0;
    memset(power_target_sequence, 0, sizeof(power_target_sequence));
    memset(power_recovery_pending, 0, sizeof(power_recovery_pending));
    memset(power_low_samples, 0, sizeof(power_low_samples));
    memset(power_low_sequence, 0, sizeof(power_low_sequence));
    now_us = 10000000;
    power_recovery_feedback.valid = true;
    power_recovery_feedback.enabled = true;
    power_recovery_feedback.meter_valid = true;
    power_recovery_feedback.calibration_status = 2;
    power_recovery_feedback.rpm = 100;
    power_recovery_feedback.power_mw = 199999;
    power_recovery_feedback.received_us = now_us;
    g_ModbusTestStats.active = false;
    writes = fail_writes = 0;
    cancel_on_zero = false;
}
static void target(uint16_t address, uint16_t value)
{
    queue_power_channel(address, value);
    modbus_command_t command;
    assert(command_queue_pop(&command)); /* Ordinary writes tested separately. */
}
static void check(void)
{
    now_us += 1000000;
    power_recovery_feedback.received_us = now_us;
    power_recovery_check();
}
static void run_recovery(uint16_t address, uint16_t value)
{
    modbus_command_t command;
    assert(command_queue_pop(&command));
    assert(command.type == MB_COMMAND_RECOVER_POWER_CHANNEL);
    assert(command.address == address && command.value == value);
    execute_command_with_retry(NULL, &command);
    assert(writes == 2 && written_address[0] == address && written_address[1] == address);
    assert(written_value[0] == 0 && written_value[1] == value);
    assert(!power_recovery_pending[power_target_index(address)]);
}
static void gateway(uint8_t *frame, size_t size)
{
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < size - 2; ++i) {
        crc ^= frame[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
    }
    frame[size - 2] = (uint8_t)crc;
    frame[size - 1] = (uint8_t)(crc >> 8);
    memcpy(gateway_transaction.request, frame, size);
    gateway_transaction.request_length = size;
    modbus_command_t command = {.type = MB_COMMAND_RAW_GATEWAY,
        .slave = MODBUS_SLAVE_POWER};
    assert(command_queue_push(&command));
    assert(command_queue_pop(&command));
}
int main(void)
{
    reset();
    target(MB_POWER_HOLD_HEATER_1, 4);
    uint32_t heating_sequence = DataCenterHeatingCommandSequence();
    check(); assert(command_queue.count == 0);
    check(); assert(command_queue.count == 1);
    check(); check(); assert(command_queue.count == 1); /* No duplicate in-flight request. */
    run_recovery(MB_POWER_HOLD_HEATER_1, 4);
    assert(g_TargetHeater1Gear == 4);
    assert(DataCenterHeatingCommandSequence() == heating_sequence);

    reset(); target(MB_POWER_HOLD_HEATER_2, 8);
    check(); check(); run_recovery(MB_POWER_HOLD_HEATER_2, 8);
    assert(g_TargetHeater2Gear == 8);

    reset(); target(MB_POWER_HOLD_HEATER_1, 3);
    check(); check(); assert(command_queue.count == 0);
    target(MB_POWER_HOLD_HEATER_1, 4);
    power_recovery_feedback.power_mw = 200000;
    check(); check(); assert(command_queue.count == 0);
    power_recovery_feedback.power_mw = 100000;
    check();
    power_recovery_feedback.power_mw = 300000;
    check();
    power_recovery_feedback.power_mw = 100000;
    check(); assert(command_queue.count == 0);
    check(); assert(command_queue.count == 1);

    reset(); target(MB_POWER_HOLD_HEATER_1, 8);
    check(); target(MB_POWER_HOLD_HEATER_1, 8); /* Repeated target preserves low interval. */
    check(); assert(command_queue.count == 1);

    reset(); target(MB_POWER_HOLD_SIGNED_MOTOR_GEAR, (uint16_t)-6);
    check(); assert(command_queue.count == 0);
    power_recovery_feedback.rpm = 0;
    check(); run_recovery(MB_POWER_HOLD_SIGNED_MOTOR_GEAR, (uint16_t)-6);
    assert(g_TargetPowerMotorGear == -6);
    reset(); target(MB_POWER_HOLD_SIGNED_MOTOR_GEAR, 0);
    power_recovery_feedback.rpm = 0;
    check(); assert(command_queue.count == 0);

    reset(); target(MB_POWER_HOLD_HEATER_1, 8);
    check(); check();
    modbus_command_t command;
    assert(command_queue_pop(&command));
    target(MB_POWER_HOLD_HEATER_1, 0);
    execute_command_with_retry(NULL, &command);
    assert(writes == 0 && !power_recovery_pending[0]); /* Stale restart canceled. */

    reset(); target(MB_POWER_HOLD_HEATER_1, 8);
    check(); check();
    assert(command_queue_pop(&command));
    cancel_on_zero = true;
    execute_command_with_retry(NULL, &command);
    assert(writes == 1 && written_value[0] == 0);
    assert(g_TargetHeater1Gear == 0 && g_TargetPowerMotorGear == 0);

    reset(); target(MB_POWER_HOLD_HEATER_1, 8);
    check(); check();
    assert(command_queue_pop(&command));
    fail_writes = MODBUS_COMMAND_RETRIES;
    execute_command_with_retry(NULL, &command);
    assert(!power_recovery_pending[0]); /* Failures permit later recovery attempts. */

    reset(); target(MB_POWER_HOLD_HEATER_1, 8);
    assert(DataCenterStopHeatersIfUnchanged(DataCenterHeatingCommandSequence()));
    assert(g_TargetHeater1Gear == 0 && g_TargetHeater2Gear == 0);

    for (unsigned reason = 0; reason < 5; ++reason) {
        reset(); target(MB_POWER_HOLD_HEATER_1, 8);
        if (reason == 0) power_recovery_feedback.meter_valid = false;
        if (reason == 1) power_recovery_feedback.enabled = false;
        if (reason == 2) power_recovery_feedback.calibration_status = 1;
        if (reason == 3) g_ModbusTestStats.active = true;
        if (reason == 4) power_recovery_feedback.valid = false;
        check(); check(); assert(command_queue.count == 0);
    }
    reset(); target(MB_POWER_HOLD_HEATER_1, 8);
    now_us += 3000000;
    power_recovery_check(); power_recovery_check();
    assert(command_queue.count == 0);

    reset(); target(MB_POWER_HOLD_HEATER_1, 8);
    command_queue.count = COMMAND_QUEUE_CAPACITY;
    check(); check(); assert(!power_recovery_pending[0]);
    queue_power_channel(MB_POWER_HOLD_HEATER_1, 0);
    assert(g_TargetHeater1Gear == 8); /* Rejected write does not replace target. */

    reset();
    uint8_t frame[] = {3, 6, 0, 2, 0, 10, 0, 0};
    gateway(frame, sizeof(frame));
    assert(g_TargetHeater1Gear == 10);
    uint8_t multi[] = {3, 0x10, 0, 2, 0, 3, 6, 0, 8, 0, 9, 0xff, 0xfc, 0, 0};
    gateway(multi, sizeof(multi));
    assert(g_TargetHeater1Gear == 8 && g_TargetHeater2Gear == 9 && g_TargetPowerMotorGear == -4);
    frame[5] = 0; /* Incorrect CRC must not cancel a target. */
    remember_power_frame(frame, sizeof(frame));
    assert(g_TargetHeater1Gear == 8);
    uint8_t disable[] = {3, 6, 0, 0, 0, 0, 0, 0};
    gateway(disable, sizeof(disable));
    assert(g_TargetHeater1Gear == 0 && g_TargetHeater2Gear == 0 && g_TargetPowerMotorGear == 0);
    puts("PASS: recovery thresholds, ordered writes, signed motor target, stop races,");
    puts("queue overflow, retry cleanup, feedback validity and gateway target tracking");
}
'''
path = OUT / "power_recovery.c"
path.write_text(harness)
executable = OUT / "power_recovery"
subprocess.run([
    "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
    "-DNTC_TEMP_MOTOR_STOP_OPTIMIZATION_ENABLE=1",
    "-I", str(OUT),
    "-I", str(ROOT / "task/DataCenter"), "-I", str(ROOT / "function/ntcTemp"),
    str(path), "-o", str(executable),
], check=True)
subprocess.run([str(executable)], check=True)
