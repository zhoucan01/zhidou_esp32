"""测试实际植物奶故障回调及 Modbus 帧与日志并发输出，产物保存在 build/ccache/。"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/ccache/milk-fault-uart"

MILK_PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "FaultDeal.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef void *TaskHandle_t;
static bool isRun;
static bool faultPaused;
static void milkUnregisterFault(void) {}
static TaskHandle_t MilkHandle = (void *)1;
enum {lidAvail, waterBoxAvail};
static bool inputs[2];
static unsigned suspends, resumes, stopped;
bool getSensorData(int sensor) { return inputs[sensor]; }
void NoBushRun(int gear) { assert(gear == 0); ++stopped; }
void Heat1Run(int gear) { assert(gear == 0); ++stopped; }
void Heat2Run(int gear) { assert(gear == 0); ++stopped; }
void hosePumpOff(void) { ++stopped; }
void waterPumpOff(void) { ++stopped; }
void vTaskSuspend(TaskHandle_t task) { assert(task != NULL); ++suspends; }
void vTaskResume(TaskHandle_t task) { assert(task != NULL); ++resumes; }
'''

MILK_CHECKS = r'''
static void expect_ok(void) {
    faultResult_t result = milkFaultDec();
    assert(result.errCode == 0 && result.faultLevel == FAULT_NONE);
}
int main(void) {
    /* 未运行和手动暂停时必须返回已初始化结果，不能恢复任务或发送控制命令。 */
    expect_ok();
    assert(stopped == 0 && suspends == 0 && resumes == 0);
    inputs[lidAvail] = inputs[waterBoxAvail] = false;
    PlantMilkResume();
    expect_ok();
    assert(stopped == 0 && suspends == 0 && resumes == 0);
    PlantMilkPause();
    unsigned previous = stopped;
    expect_ok();
    assert(stopped == previous && resumes == 0);

    /* 原始输入为低电平在位；盖子、水箱任一缺失均须关闭执行器并暂停。 */
    for (unsigned missing = 1; missing < 4; ++missing) {
        inputs[lidAvail] = (missing & 1U) != 0;
        inputs[waterBoxAvail] = (missing & 2U) != 0;
        PlantMilkResume();
        stopped = suspends = resumes = 0;
        faultResult_t result = milkFaultDec();
        assert(result.errCode == 1 && result.faultLevel == FAULT_ERROR);
        assert(stopped == 5 && suspends == 1 && resumes == 0 && isRun && faultPaused);
        result = milkFaultDec();
        assert(result.errCode == 1 && result.faultLevel == FAULT_ERROR);
        assert(stopped == 5 && suspends == 1 && resumes == 0);
        inputs[lidAvail] = inputs[waterBoxAvail] = false;
        expect_ok();
        assert(isRun && !faultPaused && resumes == 1);
    }

    /* 防止空句柄被当成当前故障检测任务，意外挂起检测线程。 */
    PlantMilkResume();
    MilkHandle = NULL;
    inputs[lidAvail] = inputs[waterBoxAvail] = true;
    stopped = suspends = resumes = 0;
    expect_ok();
    assert(stopped == 0 && suspends == 0 && resumes == 0);
    puts("PASS: initialized milk results, active-low sensors, actuator shutdown, pause isolation and null task handle");
    return 0;
}
'''

UART_PREFIX = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MODBUS_FRAME_CAPACITY 256U
#ifdef _WIN32
#include <windows.h>
static HANDLE start_event;
static void signal_logger(void) { assert(SetEvent(start_event)); }
static void wait_for_frame(void) {
    assert(WaitForSingleObject(start_event, 5000) == WAIT_OBJECT_0);
}
static void yield_ms(unsigned ms) { Sleep(ms); }
#else
#include <pthread.h>
#include <time.h>
static pthread_mutex_t event_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t event_cond = PTHREAD_COND_INITIALIZER;
static int frame_started;
static void signal_logger(void) {
    pthread_mutex_lock(&event_mutex);
    frame_started = 1;
    pthread_cond_signal(&event_cond);
    pthread_mutex_unlock(&event_mutex);
}
static void wait_for_frame(void) {
    pthread_mutex_lock(&event_mutex);
    while (!frame_started) pthread_cond_wait(&event_cond, &event_mutex);
    pthread_mutex_unlock(&event_mutex);
}
static void yield_ms(unsigned ms) {
    struct timespec delay = {ms / 1000U, (ms % 1000U) * 1000000L};
    nanosleep(&delay, NULL);
}
#endif
static atomic_int logger_ready;
static void emit_log(void) {
    wait_for_frame();
    atomic_store(&logger_ready, 1);
    printf("FAULT: simulated detector log\n");
}
#ifdef _WIN32
static DWORD WINAPI logger(void *arg) { (void)arg; emit_log(); return 0; }
#else
static void *logger(void *arg) { (void)arg; emit_log(); return NULL; }
#endif
static unsigned wire_writes;
static void attempt_concurrent_log(void) {
    signal_logger();
    for (unsigned i = 0; i < 5000 && !atomic_load(&logger_ready); ++i) yield_ms(1);
    assert(atomic_load(&logger_ready));
    yield_ms(50);
}
int frame_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int written = vprintf(format, args);
    va_end(args);
    if (strcmp(format, "MODBUS-RX:[") == 0) {
        /* 旧实现输出前缀后，强制让另一线程写日志。 */
        attempt_concurrent_log();
    }
    return written;
}
void *frame_memcpy(void *destination, const void *source, size_t length) {
    void *result = memcpy(destination, source, length);
    if (length == sizeof("MODBUS-RX:[") - 1U &&
        memcmp(source, "MODBUS-RX:[", length) == 0) {
        /* 新实现仍在内存中组帧时，允许日志先完整输出。 */
        attempt_concurrent_log();
    }
    return result;
}
size_t frame_fwrite(const void *buffer, size_t size, size_t count, FILE *stream) {
    assert(stream == stdout);
    ++wire_writes;
    return fwrite(buffer, size, count, stream);
}
#define printf frame_printf
#define memcpy frame_memcpy
#define fwrite frame_fwrite
'''

UART_CHECKS = r'''
#undef printf
#undef memcpy
#undef fwrite
int main(int argc, char **argv) {
    assert(argc == 2 && freopen(argv[1], "wb", stdout) != NULL);
#ifdef _WIN32
    start_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    assert(start_event != NULL);
    HANDLE thread = CreateThread(NULL, 0, logger, NULL, 0, NULL);
    assert(thread != NULL);
#else
    pthread_t thread;
    assert(pthread_create(&thread, NULL, logger, NULL) == 0);
#endif
    uint8_t frame[256];
    for (unsigned i = 0; i < sizeof(frame); ++i) frame[i] = (uint8_t)i;
    print_frame(frame, sizeof(frame));
#ifdef _WIN32
    assert(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CloseHandle(start_event);
#else
    assert(pthread_join(thread, NULL) == 0);
#endif
    assert(fclose(stdout) == 0);
    char output[2048], expected[1024];
    FILE *capture = fopen(argv[1], "rb");
    assert(capture != NULL);
    size_t length = fread(output, 1, sizeof(output) - 1, capture);
    output[length] = '\0';
    fclose(capture);
    size_t used = (size_t)sprintf(expected, "MODBUS-RX:[");
    for (unsigned i = 0; i < sizeof(frame); ++i)
        used += (size_t)sprintf(expected + used, "%s%02X", i ? " " : "", frame[i]);
    used += (size_t)sprintf(expected + used, "]\r\n");
    const char *start = strstr(output, "MODBUS-RX:[");
    const char *log = strstr(output, "FAULT: simulated detector log");
    assert(start != NULL && length > used && memcmp(start, expected, used) == 0);
    assert(log != NULL && (log < start || log >= start + used));
    assert(wire_writes == 1);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=shutil.which("gcc"))
    parser.add_argument("--revision", help="可选：读取指定 Git 版本，用于验证修复前的失败")
    args = parser.parse_args()
    if not args.cc:
        parser.error("需要主机 GCC，请通过 --cc 指定")
    OUT.mkdir(parents=True, exist_ok=True)

    def source(file):
        if args.revision:
            return subprocess.check_output(
                ["git", "show", f"{args.revision}:{file}"], cwd=ROOT,
            ).decode("utf-8")
        return (ROOT / file).read_text(encoding="utf-8")

    milk = source("function/Milk/Milk.c")
    console = source("task/DebugConsole/DebugConsole.c")
    frame = console[console.index("static void print_frame("):
                    console.index("static void float_to_registers(")]
    checks = {
        "milk": MILK_PREFIX + milk[milk.index("void PlantMilkPause("):] + MILK_CHECKS,
        "uart": UART_PREFIX + frame + UART_CHECKS,
    }
    failures = []
    for name, code in checks.items():
        generated = OUT / f"{name}.c"
        generated.write_text(code, encoding="utf-8")
        binary = OUT / (name + (".exe" if os.name == "nt" else ""))
        subprocess.run([
            args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
            "-ftrivial-auto-var-init=pattern", "-pthread",
            "-I", str(ROOT / "task/FaultDeal"), str(generated), "-o", str(binary),
        ], check=True)
        command = [str(binary)]
        if name == "uart":
            command.append(str(OUT / "uart_capture.txt"))
        result = subprocess.run(command, capture_output=True, text=True, timeout=15)
        if result.returncode:
            print(name + ": " + result.stdout + result.stderr)
            failures.append(name)
        else:
            print(result.stdout.strip() or "PASS: complete 256-byte Modbus frame excludes concurrent log output")
    if failures:
        raise SystemExit("FAIL: " + ", ".join(failures))


if __name__ == "__main__":
    main()
