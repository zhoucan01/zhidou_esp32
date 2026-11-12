"""Host tests of the real encoder ISR/task without diagnostic logging.

GPIO/FreeRTOS are simulated; generated files remain in build/ccache/encoder-host/.
Run: python agent-tp/tests/firmware/test_encoder_interrupts.py --cc <host gcc>
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/ccache/encoder-host"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default=shutil.which("gcc"))
    args = parser.parse_args()
    if not args.cc:
        parser.error("Provide --cc")
    files = {
        "esp_attr.h": "#define IRAM_ATTR\n#define DRAM_ATTR\n",
        "esp_intr_alloc.h": "#define ESP_INTR_FLAG_IRAM 1\n#define ESP_INTR_FLAG_LEVEL1 2\n",
        "esp_timer.h": "#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
        "sys.h": "#define encCntA 42\n#define encCntB 41\n#define encKey 40\n",
        "soc/gpio_struct.h": """
#pragma once
#include <stdint.h>
typedef struct { uint32_t in; struct { uint32_t val; } in1; } gpio_dev_t;
extern gpio_dev_t GPIO;
""",
        "driver/gpio.h": """
#pragma once
#include <assert.h>
#include <stdint.h>
typedef int esp_err_t;
typedef int gpio_num_t;
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERROR_CHECK(e) assert((e) == ESP_OK)
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_ANYEDGE 3
int gpio_config(const gpio_config_t *);
int gpio_install_isr_service(int);
int gpio_set_intr_type(int, int);
int gpio_isr_handler_add(int, void (*)(void *), void *);
""",
        "freertos/FreeRTOS.h": """
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define portENTER_CRITICAL_ISR(p) ((void)(p))
#define portEXIT_CRITICAL_ISR(p) ((void)(p))
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
#define portYIELD_FROM_ISR() ((void)0)
""",
        "freertos/task.h": """
#include "FreeRTOS.h"
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskNotifyGiveFromISR(TaskHandle_t, BaseType_t *);
unsigned ulTaskNotifyTake(int, TickType_t);
""",
    }
    for name, content in files.items():
        dest = OUT / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(content, encoding="utf-8")
    harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "MyEncoder.c"

gpio_dev_t GPIO;
static int64_t now;
static bool in_isr;
static unsigned waits, notifications;
static void (*handlers[49])(void *);
static void test_decoder(void);
static void start_portal(void);
static unsigned portal_starts;
int64_t esp_timer_get_time(void) { return now; }
int gpio_config(const gpio_config_t *g)
{
    assert(g->pin_bit_mask == ((1ULL << 42) | (1ULL << 41) | (1ULL << 40)));
    assert(g->intr_type == GPIO_INTR_DISABLE && g->pull_up_en == GPIO_PULLUP_ENABLE);
    return ESP_OK;
}
int gpio_install_isr_service(int flags)
{ assert(flags & ESP_INTR_FLAG_IRAM); return ESP_OK; }
int gpio_set_intr_type(int pin, int type)
{ assert(pin >= 40 && pin <= 42 && type == GPIO_INTR_ANYEDGE); return ESP_OK; }
int gpio_isr_handler_add(int pin, void (*handler)(void *), void *arg)
{ assert(arg == NULL); handlers[pin] = handler; return ESP_OK; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)1; }
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken)
{ assert(in_isr && task == (void *)1); ++notifications; *woken = pdTRUE; }
static void pins(unsigned ab, unsigned key)
{
    GPIO.in1.val = ((ab & 1) << 10) | (((ab >> 1) & 1) << 9) | (key << 8);
}
static void edge(unsigned ab, unsigned key, unsigned us)
{
    now += us;
    pins(ab, key);
    in_isr = true;
    encoder_gpio_isr(NULL);
    in_isr = false;
}
static void cycle(int direction, unsigned edge_us)
{
    const unsigned positive[] = {1, 3, 0, 2};
    const unsigned negative[] = {2, 0, 3, 1};
    for (unsigned i = 0; i < 4; ++i)
        edge(direction > 0 ? positive[previous_ab] : negative[previous_ab], 1, edge_us);
}
unsigned ulTaskNotifyTake(int clear, TickType_t timeout)
{
    assert(clear == pdTRUE);
    assert(handlers[40] && handlers[41] && handlers[42]);
    assert(boot_hold_callback == start_portal);
    switch (waits++) {
    case 0:
        assert(timeout == portMAX_DELAY);
        cycle(1, 10000); cycle(1, 5000); cycle(-1, 5000);
        now += 300000; cycle(1, 5000);
        assert(encCounter == 2 && notifications == 0);
        edge(previous_ab, 0, 1000);
        return 1;
    case 1:
    case 4:
    case 6:
    case 8:
        assert(timeout == 10);
        now += 10000;
        return 0;
    case 2:
        assert(timeout == 1990 && !isPress && !isLongPress);
        now += 1990000;
        return 0;
    case 3:
        assert(timeout == portMAX_DELAY && isLongPress && !isPress);
        isLongPress = false;
        edge(previous_ab, 1, 1000);
        return 1;
    case 5:
        assert(timeout == portMAX_DELAY && !isPress && !isLongPress);
        edge(previous_ab, 0, 1000);
        return 1;
    case 7:
        assert(timeout == 1990 && !isPress && !isLongPress);
        edge(previous_ab, 1, 100000);
        return 1;
    default:
        assert(timeout == portMAX_DELAY && isPress && !isLongPress);
        assert(notifications == 4);
        assert(encCounter == 2 && waits == 10);
        test_decoder();
        exit(0);
    }
}
static void reset_decoder(unsigned initial)
{
    previous_ab = initial; quarter_steps = 0; encCounter = 0;
    stable_button_level = button_level = 1;
    long_press_sent = false; isPress = isLongPress = false;
    provisioning_press = false;
    notifications = 0;
    pins(initial, 1);
}
static void start_portal(void) { ++portal_starts; }
static void test_boot_provisioning(void)
{
    reset_decoder(3); portal_starts = 0;
    now = 2000000; edge(3, 0, 0); now += 10000; (void)process_button();
    now = 4000000; (void)process_button();
    assert(!portal_starts && !isLongPress && !isPress);
    now = 7000000; (void)process_button();
    assert(portal_starts == 1 && !isLongPress && !isPress);
    now += 100000; (void)process_button();
    edge(3, 1, 1000); now += 10000; (void)process_button();
    assert(portal_starts == 1 && !isLongPress && !isPress);

    reset_decoder(3); portal_starts = 0;
    now = 25000000; edge(3, 0, 0); now += 10000; (void)process_button();
    now = 30000000; (void)process_button();
    assert(portal_starts == 1 && !isLongPress);

    reset_decoder(3); portal_starts = 0;
    now = 26000000; edge(3, 0, 0); now += 10000; (void)process_button();
    now = 28000000; (void)process_button();
    assert(!portal_starts && isLongPress);
    boot_hold_callback = NULL;
}
static void test_decoder(void)
{
    for (unsigned initial = 0; initial < 4; ++initial) {
        reset_decoder(initial); cycle(1, 100); assert(encCounter == 1);
        cycle(-1, 100); assert(encCounter == 0);
    }
    reset_decoder(3);
    const unsigned bounce[] = {2, 3, 2, 0, 2, 0, 1, 3};
    for (unsigned i = 0; i < sizeof(bounce)/sizeof(bounce[0]); ++i) edge(bounce[i], 1, 50);
    assert(encCounter == 1);
    reset_decoder(3);
    edge(2, 1, 100); edge(0, 1, 100); edge(2, 1, 100); edge(3, 1, 100);
    assert(encCounter == 0); /* Reversing a partial step. */
    edge(0, 1, 100); assert(encCounter == 0); /* Invalid two-bit transition. */
    cycle(1, 100); assert(encCounter == 1);
    reset_decoder(3);
    for (unsigned i = 0; i < 5000; ++i) cycle(1, 50);
    for (unsigned i = 0; i < 3000; ++i) cycle(-1, 50);
    assert(encCounter == 2000 && notifications == 0);
    cycle(1, 100); assert(encCounter == 2001);

    reset_decoder(3);
    edge(3, 0, 1000); assert(process_button() == 10);
    edge(3, 1, 2000); assert(process_button() == portMAX_DELAY);
    assert(!isPress && !isLongPress); /* Bounce never becomes a click. */
    edge(3, 0, 1000); now += 10000; (void)process_button();
    edge(3, 1, 100000); now += 10000; (void)process_button();
    assert(isPress && !isLongPress);
    isPress = false; (void)process_button(); assert(!isPress);
    /* Regression: a menu click used to enable 100 / elapsed_ms acceleration.
     * At 9 ms this turned a single step into -11. Keep every step at +/-1. */
    cycle(-1, 16250); assert(encCounter == -1); /* 65 ms */
    cycle(-1, 2250);  assert(encCounter == -2); /* 9 ms */
    cycle(-1, 9750);  assert(encCounter == -3); /* 39 ms */
    cycle(-1, 2500);  assert(encCounter == -4); /* 10 ms */
    cycle(1, 250);    assert(encCounter == -3); /* 1 ms, reverse direction */
    edge(3, 0, 1000); now += 10000; (void)process_button();
    now += 1990000; (void)process_button();
    assert(isLongPress && !isPress);
    isLongPress = false;
    edge(3, 1, 1000); now += 10000; (void)process_button();
    assert(!isPress && !isLongPress);
    cycle(1, 250); assert(encCounter == -2); /* Long press also leaves scale unchanged. */
    encCounter = INT_MAX; cycle(1, 100); assert(encCounter == INT_MAX);
    encCounter = INT_MIN; cycle(-1, 100); assert(encCounter == INT_MIN);
    boot_hold_callback = start_portal;
    test_boot_provisioning();
    puts("Encoder ISR/task tests passed: direction, bounce, invalid edges, 8000-step burst,");
    puts("GPIO setup, button task wakeups/deadlines, debounce, short/long press and saturation.");
    puts("Boot provisioning callback, one-shot 5 s hold and 30 s window boundaries passed.");
}
int main(void)
{
    static const my_encoder_task_config_t config = {.boot_hold_callback = start_portal};
    now = BOOT_PROVISION_WINDOW_US + 1;
    pins(3, 1);
    enctest((void *)&config);
    return 1;
}
'''
    path = OUT / "encoder_tests.c"
    path.write_text(harness, encoding="utf-8")
    executable = OUT / "encoder_tests.exe"
    temporary = ROOT / "build/ccache/tmp"
    temporary.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["TMP"] = env["TEMP"] = str(temporary)
    subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(OUT), "-I", str(ROOT / "components/MyEncoder/include"),
                    "-I", str(ROOT / "components/MyEncoder"), str(path),
                    "-o", str(executable)], check=True, env=env)
    subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
