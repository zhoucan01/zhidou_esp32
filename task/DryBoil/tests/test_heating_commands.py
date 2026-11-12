"""Run real DataCenter queue/dispatch/ingress code with UART/RTOS stubs."""
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/test_heating_commands"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default=shutil.which("gcc"))
    args = parser.parse_args()
    if not args.cc:
        parser.error("Provide --cc")
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "sys.h").write_text(
        "#define ScreenBaud 1000000\n#define PowerBaud 1000000\n", encoding="utf-8"
    )
    source = (ROOT / "task/DataCenter/DataCenter.c").read_text(encoding="utf-8")

    def section(start, end):
        return source[source.index(start):source.index(end)]

    harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "HeatingCommand.h"
#define COMMAND_QUEUE_CAPACITY 32U
#define MODBUS_GATEWAY_FRAME_MAX 256U
typedef int portMUX_TYPE;
typedef void *TaskHandle_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffu
static const char *TAG = "test";
typedef int nmbs_t;
typedef int nmbs_error;
enum { NMBS_ERROR_NONE, NMBS_ERROR_TRANSPORT, NMBS_ERROR_TIMEOUT,
       NMBS_ERROR_INVALID_ARGUMENT };
static unsigned writes;
static uint16_t last_address, last_value;
static uint8_t power_motor_direction_mode;
static portMUX_TYPE power_motor_direction_lock;
static bool select_slave(nmbs_t *c, uint8_t s) { (void)c; (void)s; return true; }
static nmbs_error nmbs_write_single_register(nmbs_t *c, uint16_t a, uint16_t v)
{ (void)c; ++writes; last_address = a; last_value = v; return NMBS_ERROR_NONE; }
static nmbs_error nmbs_write_single_coil(nmbs_t *c, uint16_t a, bool v)
{ return nmbs_write_single_register(c, a, v); }
static bool execute_raw_gateway(void) { return true; }
static void xTaskNotifyGive(TaskHandle_t t) { (void)t; }
static TaskHandle_t xTaskGetCurrentTaskHandle(void) { return NULL; }
static unsigned ulTaskNotifyTake(int clear, unsigned wait)
{ (void)clear; (void)wait; return 0; }
'''
    harness += section("typedef enum {\n    MB_COMMAND_WRITE_COIL", "static bool command_queue_push")
    harness += section("static bool command_queue_push", "static void queue_coil")
    harness += section("static void queue_register", "static void queue_signed_motor_speed")
    harness += section("static nmbs_error execute_command(", "static void execute_command_with_retry")
    harness += section("bool ModbusGatewayTransaction(", "bool ModbusCopyCachedInputRegisters(")
    harness += section("static void queue_power_channel", "void setHighBroad")
    harness += r'''
static void drain(void)
{
    modbus_command_t c;
    while (command_queue_pop(&c)) assert(execute_command(NULL, &c) == NMBS_ERROR_NONE);
}
static void append_crc(uint8_t *frame, size_t size)
{
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < size - 2; ++i) {
        crc ^= frame[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
    }
    frame[size - 2] = (uint8_t)crc;
    frame[size - 1] = (uint8_t)(crc >> 8);
}
static void gateway(const uint8_t *frame, size_t size)
{
    uint8_t response[256];
    size_t length;
    /* RTOS wait is stubbed; inspect acceptance independently of UART reply. */
    (void)ModbusGatewayTransaction(frame, size, response, sizeof(response), &length);
}
int main(void)
{
    assert(DataCenterHeatingCommandSequence() == 0);
    setPowerHeater1(0); setPowerHeater2(0); setPowerEnabled(true); setPowerMotor(4, 1);
    assert(DataCenterHeatingCommandSequence() == 0);
    drain();

        modbus_command_t motor_command;
        assert(DataCenterGetPowerMotorDirectionMode() ==
            MODBUS_POWER_MOTOR_DIRECTION_UNRESTRICTED);
        assert(DataCenterSetPowerMotorDirectionMode(
         MODBUS_POWER_MOTOR_DIRECTION_FORWARD_ONLY));
        setPowerMotor(4, MODBUS_DIRECTION_DOWN);
        assert(command_queue_pop(&motor_command));
        assert(motor_command.address == MB_POWER_HOLD_SIGNED_MOTOR_GEAR &&
            motor_command.value == 4);
        assert(DataCenterSetPowerMotorDirectionMode(
         MODBUS_POWER_MOTOR_DIRECTION_REVERSE_ONLY));
        setPowerMotor(4, MODBUS_DIRECTION_UP);
        assert(command_queue_pop(&motor_command));
        assert(motor_command.address == MB_POWER_HOLD_SIGNED_MOTOR_GEAR &&
            motor_command.value == (uint16_t)-4);
        assert(DataCenterSetPowerMotorDirectionMode(
         MODBUS_POWER_MOTOR_DIRECTION_UNRESTRICTED));
        setPowerMotor(4, MODBUS_DIRECTION_DOWN);
        assert(command_queue_pop(&motor_command) && motor_command.value == (uint16_t)-4);
        assert(!DataCenterSetPowerMotorDirectionMode(3));
        assert(DataCenterGetPowerMotorDirectionMode() ==
            MODBUS_POWER_MOTOR_DIRECTION_UNRESTRICTED);

    setPowerHeater1(6);
    assert(DataCenterHeatingCommandSequence() == 1);
    drain();

    assert(DataCenterStopHeatersIfUnchanged(1));
    setPowerHeater1(6); /* Same nonzero level still is a new command. */
    assert(DataCenterHeatingCommandSequence() == 2);
    writes = 0;
    drain();
    assert(writes == 2 && last_address == MB_POWER_HOLD_HEATER_1 && last_value == 6);
    assert(!DataCenterStopHeatersIfUnchanged(1));
    assert(DataCenterStopHeatersIfUnchanged(2));
    writes = 0;
    drain();
    assert(writes == 2 && last_address == MB_POWER_HOLD_HEATER_2 && last_value == 0);
    assert(DataCenterHeatingCommandSequence() == 2);

    assert(DataCenterStopHeatersIfUnchanged(2));
    modbus_command_t old_stop;
    assert(command_queue_pop(&old_stop));
    setPowerHeater2(9);
    writes = 0;
    assert(execute_command(NULL, &old_stop) == NMBS_ERROR_NONE && writes == 0);
    drain();
    assert(writes == 2 && last_value == 9);

    for (unsigned i = 0; i < COMMAND_QUEUE_CAPACITY; ++i) setPowerHeater1(0);
    uint32_t before = DataCenterHeatingCommandSequence();
    setPowerHeater1(8); /* Queue rejection cannot unlock protection. */
    assert(DataCenterHeatingCommandSequence() == before);
    assert(!DataCenterStopHeatersIfUnchanged(before));
    assert(command_queue_pop(&old_stop));
    assert(!DataCenterStopHeatersIfUnchanged(before)); /* Need two slots atomically. */
    drain();

    uint8_t lower[] = {3, 6, 0, 2, 0, 5, 0xe9, 0xeb}; /* Known protocol vector. */
    assert(power_frame_starts_heating(lower, sizeof(lower)));
    gateway(lower, sizeof(lower));
    assert(DataCenterHeatingCommandSequence() == before + 1);
    drain();
    lower[7] ^= 1;
    gateway(lower, sizeof(lower));
    assert(DataCenterHeatingCommandSequence() == before + 1);
    drain();

    uint8_t multi[] = {3, 0x10, 0, 0, 0, 5, 10, 0, 1, 0, 0,
                       0, 0, 0, 7, 0, 4, 0, 0};
    append_crc(multi, sizeof(multi));
    assert(power_frame_starts_heating(multi, sizeof(multi)));
    gateway(multi, sizeof(multi));
    assert(DataCenterHeatingCommandSequence() == before + 2);
    drain();
    multi[14] = 0; /* Only enable/motor writes remain nonzero. */
    append_crc(multi, sizeof(multi));
    assert(!power_frame_starts_heating(multi, sizeof(multi)));
    multi[6] = 8;
    append_crc(multi, sizeof(multi));
    assert(!power_frame_starts_heating(multi, sizeof(multi)));
    for (size_t n = 0; n < sizeof(multi); ++n)
        assert(!power_frame_starts_heating(multi, n));
    uint8_t zero[] = {3, 6, 0, 2, 0, 0, 0, 0};
    append_crc(zero, sizeof(zero));
    gateway(zero, sizeof(zero));
    assert(DataCenterHeatingCommandSequence() == before + 2);
    drain();
    zero[3] = 4; zero[5] = 6;
    append_crc(zero, sizeof(zero));
    assert(!power_frame_starts_heating(zero, sizeof(zero)));
    zero[0] = 2; zero[3] = 2;
    append_crc(zero, sizeof(zero));
    assert(!power_frame_starts_heating(zero, sizeof(zero)));
    puts("DataCenter queue, stale-stop dispatch, and gateway command tests passed.");
}
'''
    cfile = OUT / "commands.c"
    cfile.write_text(harness, encoding="utf-8")
    executable = OUT / "commands.exe"
    subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(OUT), "-I", str(ROOT / "task/DataCenter"), str(cfile),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
