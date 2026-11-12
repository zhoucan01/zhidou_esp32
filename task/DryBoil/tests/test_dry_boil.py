"""Execute the real C task with simulated scheduling and board feedback."""
import argparse
from pathlib import Path
import shutil
import subprocess

MODULE = Path(__file__).resolve().parents[1]
ROOT = MODULE.parents[1]
OUT = ROOT / "build/test_dry_boil"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default=shutil.which("gcc"))
    args = parser.parse_args()
    if not args.cc:
        parser.error("Provide --cc with a host GCC compiler")
    (OUT / "freertos").mkdir(parents=True, exist_ok=True)
    files = {
        "esp_err.h": """
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
""",
        "esp_log.h": "#define ESP_LOGE(tag, ...) ((void)(tag))\n",
        "freertos/FreeRTOS.h": """
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdPASS 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)
""",
        "freertos/task.h": """
#include "FreeRTOS.h"
BaseType_t xTaskCreate(void (*task)(void *), const char *, unsigned, void *, unsigned, void *);
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskDelayUntil(TickType_t *, TickType_t);
void vTaskDelay(TickType_t);
void vTaskDelete(void *);
#define vTaskDelayUntil(w, p) ((void)xTaskDelayUntil(w, p))
""",
        "harness.c": r'''
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DryBoilPort.h"
#include "DataCenter.h"
#include "ModbusMap.h"
#include "freertos/task.h"

static jmp_buf finished;
static void (*entry)(void *);
static int scenario, step, stops, alarms, fail_create;
static int ready_waits, creations, deletions;
static bool cache_ready = true;
static uint16_t board_power[MB_POWER_INPUT_COUNT];
static TickType_t ticks;
static uint32_t command_sequence = 7;

BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                      void *arg, unsigned priority, void *handle)
{
    (void)name; (void)arg; (void)handle;
    ++creations;
    assert(stack >= 3072 && priority == 17);
    if (fail_create) return pdFALSE;
    entry = task;
    return pdPASS;
}
TickType_t xTaskGetTickCount(void) { return ticks; }

void vTaskDelay(TickType_t delay)
{
    dry_boil_status_t s;
    DryBoilTaskGetStatus(&s);
    assert(scenario == 29 && delay == 100 && creations == 1);
    assert(s.started && !s.sample_valid && !s.alarm_latched && stops == 0);
    assert(DryBoilTaskReset() == ESP_ERR_INVALID_STATE);
    ticks += delay;
    if (++ready_waits == 3) cache_ready = true;
}
void vTaskDelete(void *handle)
{
    assert(handle == NULL && (scenario == 29 || scenario == 30));
    ++deletions;
}

static bool sample_read(dry_boil_sample_t *s, void *context)
{
    (void)context;
    s->temperature_valid = true;
    s->temperature_c = 40;
    s->actual_power_0_13 = 6;
    if (scenario == 0) s->temperature_c += step * 0.04f; /* wet: 4.8 C/min */
    if (scenario == 1) s->temperature_c += step * 0.10f; /* dry: 12 C/min */
    if (scenario == 2) s->temperature_c = step == 0 ? 103 : 40;
    if (scenario == 3) s->temperature_c = NAN;
    if (scenario == 4) s->temperature_c = INFINITY;
    if (scenario == 5) s->temperature_c = 201;
    if (scenario == 6) s->temperature_valid = false;
    if (scenario == 7) return false;
    if (scenario == 8) s->actual_power_0_13 = 14;
    if (scenario == 9) {
        s->temperature_c = step == 0 ? 103 : 40;
        s->actual_power_0_13 = step == 0 ? 6 : 0;
    }
    if (scenario == 10) {
        s->actual_power_0_13 = step < 100 ? 0 : 6;
        s->temperature_c = 40 + (step < 100 ? 0 : (step - 100) * 0.10f);
    }
    if (scenario == 11 && step == 1) s->actual_power_0_13 = 0;
    if (scenario == 13) {
        s->temperature_c = step == 0 ? 103 : 40;
        s->actual_power_0_13 = step == 1 ? 0 : 6;
    }
    return true;
}
static void stop(void *context) { (void)context; ++stops; }
static void alarm_cb(const db_result_t *r, void *context)
{
    (void)context;
    assert(r->state == DB_DRY_CONFIRMED);
    ++alarms;
}

bool ModbusCopyCachedInputRegisters(uint8_t slave, uint16_t address,
                                    uint16_t quantity, uint16_t *registers)
{
    if (!cache_ready) return false;
    if (slave == MODBUS_SLAVE_POWER) {
        memcpy(registers, board_power + address, quantity * sizeof(uint16_t));
    } else {
        float temp = (scenario == 30 || scenario >= 31) && step == 0 ? 103 : 40;
        if (scenario == 32) temp = 103;
        if (scenario == 33 && step >= 2) temp = NAN;
        uint32_t bits;
        memcpy(&bits, &temp, sizeof(bits));
        assert(address == 0 && quantity == 2);
        registers[0] = (uint16_t)(bits >> 16);
        registers[1] = (uint16_t)bits;
    }
    return true;
}
void setPowerHeater1(uint16_t gear) { assert(gear == 0); ++stops; }
void setPowerHeater2(uint16_t gear) { assert(gear == 0); ++stops; }
uint32_t DataCenterHeatingCommandSequence(void) { return command_sequence; }
bool DataCenterStopHeatersIfUnchanged(uint32_t sequence)
{
    if (scenario == 34 && step == 0) ++command_sequence;
    if (sequence != command_sequence) return false;
    stops += 2;
    return true;
}

BaseType_t xTaskDelayUntil(TickType_t *wake, TickType_t period)
{
    dry_boil_status_t s;
    DryBoilTaskGetStatus(&s);
    assert(s.started && period == 500);
    if (scenario == 0) {
        assert(!s.alarm_latched && stops == 0);
        if (step == 150) { assert(s.result.state == DB_WATER_LIKELY); longjmp(finished, 1); }
    } else if (scenario == 1) {
        if (step == 68) assert(s.result.state == DB_LEARNING);
        if (step == 90) { assert(s.alarm_latched && alarms == 1 && stops > 0); longjmp(finished, 1); }
    } else if (scenario >= 2 && scenario <= 8) {
        assert(s.alarm_latched && alarms == 1 && stops == step + 1);
        assert(DryBoilTaskReset() == ESP_ERR_INVALID_STATE);
        if (step == 3) longjmp(finished, 1);
    } else if (scenario == 9 || scenario == 13) {
        if (step == 0) assert(s.alarm_latched);
        if (step == 1) { assert(s.alarm_latched); assert(DryBoilTaskReset() == ESP_OK); }
        if (step == 2) {
            assert(!s.reset_pending);
            if (scenario == 9) assert(!s.alarm_latched && !s.cycle_active && stops == 2);
            else assert(s.alarm_latched && stops == 3); /* Reject unsafe reset at consumption. */
            longjmp(finished, 1);
        }
    } else if (scenario == 10) {
        if (step < 100) assert(!s.cycle_active);
        if (step < 169) assert(s.result.state == DB_LEARNING);
        if (step == 190) { assert(s.alarm_latched); longjmp(finished, 1); }
    } else if (scenario == 11) {
        assert(s.cycle_active && !s.alarm_latched);
        if (step == 2) longjmp(finished, 1);
    } else if (scenario == 12) {
        if (step == 0) { ++step; ticks += 1000; return pdFALSE; }
        if (step == 2) { assert(s.alarm_latched && alarms == 1); longjmp(finished, 1); }
    } else if (scenario == 29 || scenario == 30) {
        assert(creations == 1 && s.sample.actual_power_0_13 == 6);
        assert(s.alarm_latched == (scenario == 30));
        assert(stops == (scenario == 30 ? 2 * (step + 1) : 0));
        if (scenario == 29) assert(ready_waits == 3);
        if (step == 2) {
            DryBoilTask(NULL); /* A duplicate entry must not reset live state. */
            assert(deletions == 1 && creations == 1);
            dry_boil_status_t after;
            DryBoilTaskGetStatus(&after);
            assert(after.alarm_latched == s.alarm_latched);
            assert(after.cycle_active == s.cycle_active);
            longjmp(finished, 1);
        }
    } else if (scenario >= 31) {
        if (scenario == 34) {
            assert(stops == 0);
            if (step == 1) { assert(!s.alarm_latched); longjmp(finished, 1); }
        } else {
            if (step <= 1) assert(s.alarm_latched && stops == 2 * (step + 1));
            if (step == 1) ++command_sequence; /* Next nonzero heating request. */
            if (step >= 2 && scenario == 31) {
                assert(!s.alarm_latched && stops == 4);
                ++command_sequence; /* Ordinary adjustment must not reset learning. */
                if (step == 2) assert(s.result.state == DB_LEARNING);
                if (step == 72) {
                    assert(s.result.state == DB_WATER_LIKELY);
                    longjmp(finished, 1);
                }
            }
            if (step == 3 && (scenario == 32 || scenario == 33)) {
                assert(s.alarm_latched && stops == 8);
                longjmp(finished, 1);
            }
        }
    } else if (scenario >= 20) {
        bool expected_alarm = scenario == 23 || scenario == 24 || scenario == 26;
        assert(s.alarm_latched == expected_alarm);
        if (expected_alarm) assert(stops == 2);
        else {
            assert(s.sample_valid && s.sample.temperature_c == 40);
            assert(s.sample.actual_power_0_13 == ((scenario == 25 || scenario == 28) ? 0 : 6));
            if (scenario == 28) assert(!s.cycle_active);
        }
        longjmp(finished, 1);
    }
    assert(step < 200);
    ++step;
    *wake += period;
    ticks = *wake;
    return pdPASS;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    scenario = atoi(argv[1]);
    assert(DryBoilTaskReset() == ESP_ERR_INVALID_STATE);
    if (scenario == 29 || scenario == 30 || scenario >= 31) {
        board_power[MB_POWER_INPUT_ENABLE] = 1;
        board_power[MB_POWER_INPUT_HEATER_1_ENABLED] = 1;
        board_power[MB_POWER_INPUT_HEATER_1_GEAR] = 6;
        cache_ready = scenario != 29;
        assert(xTaskCreate(DryBoilTask, "DryBoil", DRY_BOIL_TASK_STACK_SIZE,
                          NULL, DRY_BOIL_TASK_PRIORITY, NULL) == pdPASS);
    } else if (scenario < 20) {
        dry_boil_port_t port = {
            .read_sample = sample_read, .stop_heating = stop, .alarm = alarm_cb
        };
        assert(DryBoilTaskStartWithPort(NULL) == ESP_ERR_INVALID_ARG);
        fail_create = 1;
        assert(DryBoilTaskStartWithPort(&port) == ESP_ERR_NO_MEM);
        fail_create = 0;
        assert(DryBoilTaskStartWithPort(&port) == ESP_OK);
        assert(DryBoilTaskStartWithPort(&port) == ESP_ERR_INVALID_STATE);
    } else {
        dry_boil_heater_mode_t mode = DRY_BOIL_HEATER_2;
        board_power[MB_POWER_INPUT_ENABLE] = 1;
        board_power[MB_POWER_INPUT_HEATER_1_ENABLED] = 1;
        board_power[MB_POWER_INPUT_HEATER_2_ENABLED] = 1;
        board_power[MB_POWER_INPUT_HEATER_2_GEAR] = 6;
        if (scenario == 20) {
            mode = DRY_BOIL_HEATER_1;
            board_power[MB_POWER_INPUT_HEATER_1_GEAR] = 6;
            board_power[MB_POWER_INPUT_HEATER_2_GEAR] = 0;
        }
        if (scenario == 22 || scenario == 24) {
            mode = DRY_BOIL_HEATERS_SAME_GEAR;
            board_power[MB_POWER_INPUT_HEATER_1_GEAR] = scenario == 22 ? 6 : 3;
        }
        if (scenario == 23) board_power[MB_POWER_INPUT_HEATER_1_GEAR] = 1;
        if (scenario == 25) board_power[MB_POWER_INPUT_ENABLE] = 0;
        if (scenario == 26) board_power[MB_POWER_INPUT_HEATER_2_GEAR] = 300;
        if (scenario == 27 || scenario == 28) {
            /* Upper heating must neither trip lower-heater detection nor
             * contribute to its gear, including when the lower heater is off. */
            mode = DRY_BOIL_HEATER_1;
            board_power[MB_POWER_INPUT_HEATER_1_GEAR] = scenario == 27 ? 6 : 0;
            board_power[MB_POWER_INPUT_HEATER_2_GEAR] = 10;
        }
        assert(DryBoilTaskStart(0) == ESP_ERR_INVALID_ARG);
        cache_ready = false;
        assert(DryBoilTaskStart(mode) == ESP_ERR_INVALID_STATE);
        cache_ready = true;
        assert(DryBoilTaskStart(mode) == ESP_OK);
    }
    assert(entry);
    if (setjmp(finished) == 0) entry(NULL);
    printf("scenario %d passed\n", scenario);
    return 0;
}
''',
    }
    for filename, content in files.items():
        (OUT / filename).write_text(content, encoding="utf-8")
    executable = OUT / "test_dry_boil.exe"
    subprocess.run([
        args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(OUT), "-I", str(MODULE), "-I", str(ROOT / "task/DataCenter"),
        str(OUT / "harness.c"), str(MODULE / "dry_boil_detector.c"),
        str(MODULE / "DryBoilTask.c"), str(MODULE / "DryBoilPort.c"),
        "-o", str(executable),
    ], check=True)
    for scenario in list(range(14)) + list(range(20, 35)):
        subprocess.run([str(executable), str(scenario)], check=True)
    print("29 host scenarios passed (RTOS and hardware are simulated).")


if __name__ == "__main__":
    main()
