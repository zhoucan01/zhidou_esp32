"""Host checks for display windows and the real station-1 Modbus handler.

Run: python agent-tp/tests/firmware/test_display_diagnostics.py (requires GCC on PATH).
Generated headers, executable and compiler output stay under build/.
Hardware drivers and locks are stubbed; DMA/dual-core timing needs board testing.
"""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build" / "test_display_diagnostics"
OUT.mkdir(parents=True, exist_ok=True)
(OUT / "sys.h").write_text(
    "#define ScreenBaud 1000000\n#define PowerBaud 1000000\n", encoding="utf-8"
)
(OUT / "freertos").mkdir(exist_ok=True)
(OUT / "esp_timer.h").write_text(
    "#include <stdint.h>\nint64_t esp_timer_get_time(void);\n", encoding="utf-8"
)
(OUT / "freertos" / "FreeRTOS.h").write_text("""
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define portENTER_CRITICAL_ISR(p) ((void)(p))
#define portEXIT_CRITICAL_ISR(p) ((void)(p))
""", encoding="utf-8")

# Compile the production RTU handler up to the UART dispatch boundary, with
# only board/business dependencies stubbed. No copied register mapping logic.
console = (ROOT / "task/DebugConsole/DebugConsole.c").read_text(encoding="utf-8")
handler = console[console.index("#define MODBUS_FRAME_CAPACITY"):
                  console.index("static void handle_frame(")]
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ModbusMap.h"
#include "display_stats.h"
#define CONTROL_MODBUS_FRAME_CAPACITY 256U
typedef enum {
    CONTROL_MODBUS_OK = 0, CONTROL_MODBUS_BAD_CRC,
    CONTROL_MODBUS_BAD_SLAVE, CONTROL_MODBUS_TIMEOUT,
    CONTROL_MODBUS_BAD_RESPONSE_CRC,
} control_modbus_result_t;
typedef void *EventGroupHandle_t;
static EventGroupHandle_t FuncTypeGroup = (void *)1;
static EventGroupHandle_t FuncOrderGroup = (void *)1;
#define BIT(n) (1U << (n))
#define FuncStart_bit 1U
#define FuncPause_bit 2U
#define FuncResume_bit 4U
#define FuncStop_bit 8U
static unsigned xEventGroupSetBits(EventGroupHandle_t group, unsigned bits) {
    (void)group; return bits;
}
static int64_t now_us;
int64_t esp_timer_get_time(void) { return now_us; }
#define MALLOC_CAP_SPIRAM 1U
#define MALLOC_CAP_INTERNAL 2U
#define MALLOC_CAP_8BIT 4U
#define pdMS_TO_TICKS(x) (x)
static size_t heap_caps_get_free_size(unsigned caps) {
    if (caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return 0x00123456U;
    assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return 0x00023456U;
}
static struct {
    uint16_t statusWord, faultCode, powerMotorRpm, powerMotorState;
    bool powerHeater1Enabled, powerHeater2Enabled, powerEnabled;
    float temperature, voltage, current;
} g_DataPool;
enum { sensorNum = 18, moterNum = 10, NOTASK_SEC = 6 };
typedef unsigned FuncSecType_t;
static bool sensorState[sensorNum], moterState[moterNum];
static char version[3][4] = {{0, 0, 0, 3}, {0}, {0}};
static uint8_t BluetoothMac[6] = {0x10, 0x32, 0x54, 0x76, 0xAB, 0xCD};
static uint16_t display_fps = MODBUS_DISPLAY_FPS_DEFAULT;
static uint8_t power_motor_direction_mode;
static bool DataCenterSetPowerMotorDirectionMode(uint8_t mode) {
    if (mode > MODBUS_POWER_MOTOR_DIRECTION_REVERSE_ONLY) return false;
    power_motor_direction_mode = mode; return true;
}
static uint8_t DataCenterGetPowerMotorDirectionMode(void) {
    return power_motor_direction_mode;
}
static bool DisplayFrameRateSet(uint16_t fps) {
    if (fps < MODBUS_DISPLAY_FPS_MIN || fps > MODBUS_DISPLAY_FPS_MAX) return false;
    display_fps = fps; return true;
}
static uint16_t DisplayFrameRateGet(void) { return display_fps; }
static FuncSecType_t GetCurTaskState(void) { return NOTASK_SEC; }
static unsigned GetCurTaskOrder(void) { return 0; }
static bool ModbusGatewayTransaction(const uint8_t *request,
                                     size_t request_length,
                                     uint8_t *response,
                                     size_t response_capacity,
                                     size_t *response_length) {
    (void)request; (void)request_length; (void)response;
    (void)response_capacity; (void)response_length; return false;
}
static bool ModbusCopyCachedInputRegisters(unsigned slave, unsigned start,
                                           unsigned count, uint16_t *out) {
    assert(slave == 2 || slave == 3);
    for (unsigned i = 0; i < count; ++i) out[i] = start + i;
    return true;
}
'''
checks = r'''
static void expect_stats(uint32_t frames, uint32_t average, uint32_t maximum) {
    display_stats_t s;
    display_stats_get(&s);
    assert(s.frames == frames && s.average_us == average && s.maximum_us == maximum);
}
static void expect_read(unsigned address, unsigned count, bool valid) {
    uint8_t req[8] = {1, 4, address >> 8, address, count >> 8, count};
    uint8_t response[256];
    append_crc(req, 6);
    size_t n = handle_local_frame(req, sizeof(req), response);
    assert(frame_crc_valid(response, n));
    if (valid) {
        assert(n == 5 + 2 * count && response[1] == 4 && response[2] == 2 * count);
    } else {
        assert(n == 5 && response[1] == 0x84 && response[2] == 2);
    }
}
int main(void) {
    display_stats_init();
    expect_stats(0, 0, 0);
    now_us = 200000; display_stats_record_frame(1000);
    now_us = 500000; display_stats_record_frame(3000);
    now_us = 999999; display_stats_record_frame(2000);
    expect_stats(0, 0, 0);
    now_us = 1000000;
    expect_stats(3, 2000, 3000);
    expect_stats(3, 2000, 3000); /* reads must not reset the window */

    /* Real response builder: byte order, frame length, legacy reads and CRC. */
    uint8_t request[] = {1, 4, 0, 0, 0, 30, 0x70, 0x02};
    uint8_t response[256];
    assert(frame_crc_valid(request, sizeof(request)));
    size_t length = handle_local_frame(request, sizeof(request), response);
    assert(length == 65 && response[2] == 0x3C);
    assert(frame_crc_valid(response, length));
    assert(response[3] == 0 && response[4] == 0 &&
           response[5] == 0 && response[6] == 3); /* firmware 0.0.0.3 */
    assert(response[9] == 1 && response[10] == 10); /* protocol 0x010A */
    const uint8_t expected[] = {
        0x00, 0x12, 0x34, 0x56, /* PSRAM */
        0x00, 0x02, 0x34, 0x56, /* internal heap */
        0x00, 0x00, 0x00, 0x03, /* 3 completed frames */
        0x00, 0x00, 0x07, 0xD0, /* 2000 us average */
        0x00, 0x00, 0x0B, 0xB8, /* 3000 us maximum */
    };
    assert(memcmp(response + 3 + 2 * 0x14, expected, sizeof(expected)) == 0);
    expect_read(0, 16, true);
    expect_read(0, 18, true);
    expect_read(0, 28, true);
    expect_read(0, 30, true);
    expect_read(0x14, 10, true);
    expect_read(0x1B, 1, true);
    expect_read(0x1C, 2, true);
    expect_read(0x1E, 3, true);
    expect_read(0x1A, 8, true);
    expect_read(0, 34, true);
    expect_read(0, 35, false);
    expect_read(0xFF, 2, false);
        sensorState[16] = true;
        sensorState[17] = true;
        uint8_t sensor_bitmap_request[] = {1, 4, 0, 9, 0, 2, 0, 0};
        append_crc(sensor_bitmap_request, 6);
        length = handle_local_frame(sensor_bitmap_request,
                        sizeof(sensor_bitmap_request), response);
        assert(length == 9 && response[3] == 0 && response[4] == 0 &&
            response[5] == 0 && response[6] == 3);
    expect_read(0x100, 40, true);
    expect_read(0x200, 14, true);
    uint8_t diagnostic_request[] = {1, 4, 0, 0x14, 0, 10, 0x30, 0x09};
    assert(frame_crc_valid(diagnostic_request, sizeof(diagnostic_request)));
    uint8_t version_request[] = {1, 4, 0, 0, 0, 2, 0x71, 0xCB};
    assert(frame_crc_valid(version_request, sizeof(version_request)));
    length = handle_local_frame(version_request, sizeof(version_request), response);
    assert(length == 9 && response[2] == 4);
    assert(response[3] == 0 && response[4] == 0 &&
           response[5] == 0 && response[6] == 3);
    uint8_t mac_request[] = {1, 4, 0, 0x1E, 0, 3, 0, 0};
    length = handle_local_frame(mac_request, sizeof(mac_request), response);
    assert(length == 11 && response[2] == 6);
    assert(memcmp(response + 3, BluetoothMac, sizeof(BluetoothMac)) == 0);
    uint8_t write_request[] = {1, 6, 0, 0x12, 0, 1, 0, 0};
    length = handle_local_frame(write_request, sizeof(write_request), response);
    assert(length == 5 && response[1] == 0x86 && response[2] == 2);

    /* Station-1 holding 0x0010 controls target display FPS. */
    assert(DisplayFrameRateGet() == 10);
    uint8_t fps_write[] = {1, 6, 0, 0x10, 0, 15, 0, 0};
    append_crc(fps_write, 6);
    length = handle_local_frame(fps_write, sizeof(fps_write), response);
    assert(length == sizeof(fps_write) && memcmp(response, fps_write, length) == 0);
    assert(DisplayFrameRateGet() == 15);
    uint8_t fps_read[] = {1, 3, 0, 0x10, 0, 1, 0, 0};
    append_crc(fps_read, 6);
    length = handle_local_frame(fps_read, sizeof(fps_read), response);
    assert(length == 7 && response[1] == 3 && response[2] == 2 &&
           response[3] == 0 && response[4] == 15);
    fps_write[4] = 0; fps_write[5] = 0; append_crc(fps_write, 6);
    length = handle_local_frame(fps_write, sizeof(fps_write), response);
    assert(length == 5 && response[1] == 0x86 && response[2] == 3);
    assert(DisplayFrameRateGet() == 15);
    fps_write[5] = 20; append_crc(fps_write, 6);
    length = handle_local_frame(fps_write, sizeof(fps_write), response);
    assert(length == sizeof(fps_write) && DisplayFrameRateGet() == 20);
    fps_write[5] = 21; append_crc(fps_write, 6);
    length = handle_local_frame(fps_write, sizeof(fps_write), response);
    assert(length == 5 && response[2] == 3 && DisplayFrameRateGet() == 20);
    uint8_t fps_multi[] = {1, 0x10, 0, 0x0F, 0, 2, 4, 0x12, 0x34, 0, 18, 0, 0};
    append_crc(fps_multi, 11);
    length = handle_local_frame(fps_multi, 13, response);
    assert(length == 8 && response[1] == 0x10 && DisplayFrameRateGet() == 18);
    assert(local_holding[0x0F] == 0x1234);
    fps_multi[10] = 0; append_crc(fps_multi, 11);
    length = handle_local_frame(fps_multi, 13, response);
    assert(length == 5 && response[1] == 0x90 && response[2] == 3);
    assert(DisplayFrameRateGet() == 18 && local_holding[0x0F] == 0x1234);

        /* Station-1 holding 0x0011 sets and reads the station-3 direction limit. */
        uint8_t direction_write[] = {1, 6, 0, 0x11, 0, 1, 0, 0};
        append_crc(direction_write, 6);
        length = handle_local_frame(direction_write, sizeof(direction_write), response);
        assert(length == sizeof(direction_write) &&
            memcmp(response, direction_write, length) == 0);
        assert(DataCenterGetPowerMotorDirectionMode() ==
            MODBUS_POWER_MOTOR_DIRECTION_FORWARD_ONLY);
        uint8_t direction_read[] = {1, 3, 0, 0x11, 0, 1, 0, 0};
        append_crc(direction_read, 6);
        length = handle_local_frame(direction_read, sizeof(direction_read), response);
        assert(length == 7 && response[1] == 3 && response[3] == 0 && response[4] == 1);
        direction_write[5] = 2; append_crc(direction_write, 6);
        length = handle_local_frame(direction_write, sizeof(direction_write), response);
        assert(length == sizeof(direction_write) &&
            DataCenterGetPowerMotorDirectionMode() ==
             MODBUS_POWER_MOTOR_DIRECTION_REVERSE_ONLY);
        direction_write[5] = 3; append_crc(direction_write, 6);
        length = handle_local_frame(direction_write, sizeof(direction_write), response);
        assert(length == 5 && response[1] == 0x86 && response[2] == 3);
        assert(DataCenterGetPowerMotorDirectionMode() ==
            MODBUS_POWER_MOTOR_DIRECTION_REVERSE_ONLY);
        direction_write[5] = 0; append_crc(direction_write, 6);
        length = handle_local_frame(direction_write, sizeof(direction_write), response);
        assert(length == sizeof(direction_write) &&
            DataCenterGetPowerMotorDirectionMode() ==
             MODBUS_POWER_MOTOR_DIRECTION_UNRESTRICTED);

        uint8_t direction_multi[] = {1, 0x10, 0, 0x10, 0, 2, 4, 0, 12, 0, 2, 0, 0};
        append_crc(direction_multi, 11);
        length = handle_local_frame(direction_multi, sizeof(direction_multi), response);
        assert(length == 8 && DisplayFrameRateGet() == 12 &&
            DataCenterGetPowerMotorDirectionMode() ==
             MODBUS_POWER_MOTOR_DIRECTION_REVERSE_ONLY);
        direction_multi[10] = 3; append_crc(direction_multi, 11);
        length = handle_local_frame(direction_multi, sizeof(direction_multi), response);
        assert(length == 5 && response[1] == 0x90 && response[2] == 3);
        assert(DisplayFrameRateGet() == 12 &&
            DataCenterGetPowerMotorDirectionMode() ==
             MODBUS_POWER_MOTOR_DIRECTION_REVERSE_ONLY);

        uint8_t direction_snapshot_read[] = {1, 4, 0, 0x21, 0, 1, 0, 0};
        append_crc(direction_snapshot_read, 6);
        length = handle_local_frame(direction_snapshot_read,
                                    sizeof(direction_snapshot_read), response);
        assert(length == 7 && frame_crc_valid(response, length) &&
            response[1] == 4 && response[2] == 2 &&
            response[3] == 0 && response[4] == 2);

    /* Boundary completion belongs to the new window, idle windows clear. */
    display_stats_record_frame(4000);
    now_us = 2000000; display_stats_record_frame(8000);
    expect_stats(1, 4000, 4000);
    now_us = 3000000; expect_stats(1, 8000, 8000);
    now_us = 4000000; expect_stats(0, 0, 0);
    now_us = 4500000; display_stats_record_frame(1000);
    now_us = 7000000; expect_stats(0, 0, 0); /* skipped windows */

    /* Long uptime and accumulation above 32 bits must remain accurate. */
    now_us = 10000000000LL;
    display_stats_init();
    display_stats_record_frame(UINT32_MAX);
    display_stats_record_frame(UINT32_MAX);
    now_us += 1000000;
    expect_stats(2, UINT32_MAX, UINT32_MAX);
    now_us += 1000000;
    display_stats_record_frame(1);
    display_stats_record_frame(2);
    now_us += 1000000;
    expect_stats(2, 1, 2);
    puts("PASS: display windows, idle reset, uint32 packing, Modbus reads/CRC/bounds");
    return 0;
}
'''
(OUT / "test.c").write_text(prefix + handler + checks, encoding="utf-8")
compiler = shutil.which("gcc")
if not compiler:
    raise SystemExit("GCC is required for host diagnostics tests")
binary = OUT / "test.exe"
subprocess.run([
    compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
    "-Wno-unused-function", "-Wno-unused-variable",
    "-I", str(OUT), "-I", str(ROOT / "components/myLvgl/include"),
    "-I", str(ROOT / "task/DataCenter"),
    str(OUT / "test.c"), str(ROOT / "components/myLvgl/display_stats.c"),
    "-o", str(binary),
], check=True)
subprocess.run([str(binary)], check=True)
