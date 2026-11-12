"""Exercise production backlight and DMA completion code with mocked hardware.

Run with --cc pointing to a host GCC. Outputs stay in build/ccache/.
Real power-on pin levels and panel scanout still require board validation.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/ccache/display-startup"


def function(source, name):
    match = re.search(
        rf"(?:static\s+)?(?:void|bool)\s+{name}\([^;]*?\)\s*\{{", source, re.S
    )
    if match is None:
        raise RuntimeError(f"Missing production function: {name}")
    depth = 1
    end = match.end()
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define LCD_BL_LEDC_MODE 0
#define LCD_BL_LEDC_CHANNEL 3
#define LCD_BL_MAX_DUTY 1023U
#define lcdBl 48
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_INTR_DISABLE 0
#define ESP_OK 0
typedef int esp_err_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define portENTER_CRITICAL_ISR(p) ((void)(p))
#define portEXIT_CRITICAL_ISR(p) ((void)(p))
static unsigned errors;
#define ESP_ERROR_CHECK(call) do { if ((call) != ESP_OK) ++errors; } while (0)
typedef struct {
    uint64_t pin_bit_mask;
    int mode, pull_up_en, pull_down_en, intr_type;
} gpio_config_t;
static bool in_isr, gpio_latched_low;
static unsigned gpio_configured, duty_updates, applied_duty;
static int gpio_set_level(int pin, int level) {
    assert(!in_isr && pin == lcdBl && level == 0);
    gpio_latched_low = true;
    return ESP_OK;
}
static int gpio_config(const gpio_config_t *config) {
    assert(!in_isr && gpio_latched_low);
    assert(config->pin_bit_mask == (1ULL << lcdBl));
    assert(config->mode == GPIO_MODE_OUTPUT);
    assert(config->pull_up_en == GPIO_PULLUP_DISABLE);
    assert(config->pull_down_en == GPIO_PULLDOWN_ENABLE);
    ++gpio_configured;
    return ESP_OK;
}
static int ledc_set_duty(int mode, int channel, unsigned duty) {
    assert(!in_isr && mode == LCD_BL_LEDC_MODE && channel == LCD_BL_LEDC_CHANNEL);
    applied_duty = duty;
    return ESP_OK;
}
static int ledc_update_duty(int mode, int channel) {
    assert(!in_isr && mode == LCD_BL_LEDC_MODE && channel == LCD_BL_LEDC_CHANNEL);
    ++duty_updates;
    return ESP_OK;
}
static bool backlight_pwm_ready, first_frame_ready;
static uint8_t requested_brightness = 100U;
void ScreenSetBrightness(uint8_t percent);
typedef void *esp_lcd_panel_handle_t;
typedef void *esp_lcd_panel_io_handle_t;
typedef struct { int unused; } esp_lcd_panel_io_event_data_t;
typedef struct { bool last; } lv_disp_drv_t;
typedef struct { int32_t x1, y1, x2, y2; } lv_area_t;
typedef uint16_t lv_color_t;
typedef uint32_t ui_alias_u32_t __attribute__((__may_alias__));
static int64_t render_start_us, pending_frame_start_us;
static portMUX_TYPE frame_time_lock;
static bool first_frame_complete, first_frame_revealed;
static unsigned flush_ready_count, recorded_frames, transfers;
static int draw_result;
static esp_lcd_panel_handle_t g_panel_handle = (void *)1;
static int64_t esp_timer_get_time(void) { return 123456; }
static bool lv_disp_flush_is_last(lv_disp_drv_t *driver) { return driver->last; }
static void lv_disp_flush_ready(lv_disp_drv_t *driver) {
    assert(driver != NULL);
    ++flush_ready_count;
}
static void display_stats_record_frame(uint32_t duration) {
    assert(in_isr && duration == 123456);
    ++recorded_frames;
}
static int esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel,
                                    int x1, int y1, int x2, int y2, void *pixels) {
    assert(panel != NULL && pixels != NULL);
    assert(x1 == 0 && x2 == 320 && y2 > y1);
    ++transfers;
    return draw_result;
}
'''

STARTUP_MOCKS = r'''
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
static lv_disp_drv_t startup_driver = {.last = true};
static unsigned startup_refreshes, startup_delays, startup_notifications;
static bool startup_pending;
static lv_disp_drv_t *lv_disp_get_default(void) { return &startup_driver; }
static void lv_refr_now(lv_disp_drv_t *display) {
    assert(display == &startup_driver && applied_duty == 0);
    ++startup_refreshes;
    startup_pending = true; /* Submitting the frame is not completion. */
}
static void vTaskDelay(unsigned ticks) {
    assert(ticks == 1 && startup_pending && applied_duty == 0);
    ++startup_delays;
    startup_pending = false;
    in_isr = true;
    disp_flush_ready_cb(NULL, NULL, &startup_driver);
    in_isr = false;
    assert(applied_duty == 0);
}
static uint32_t ulTaskNotifyTake(int clear, uint32_t timeout) {
    /* App initialization is gated until the first frame is visible. */
    assert(clear == pdTRUE && timeout == portMAX_DELAY);
    assert(lv_port_disp_first_frame_ready() && applied_duty != 0);
    ++startup_notifications;
    return 1;
}
'''

CHECKS = r'''
static void reset_frame_gate(void) {
    first_frame_complete = first_frame_revealed = false;
}
int main(void) {
    /* Cold boot: hold the active-high backlight low before PWM exists. */
    ScreenBacklightHoldOff();
    assert(gpio_configured == 1 && gpio_latched_low && duty_updates == 0);
    ScreenSetBrightness(100);
    assert(requested_brightness == 100 && duty_updates == 0);
    backlight_pwm_ready = true; /* ScreenInit configures LEDC with duty=0. */
    ScreenSetBrightness(100);
    assert(applied_duty == 0);
    assert(!lv_port_disp_first_frame_ready());
    ScreenSetBrightness(37);
    assert(applied_duty == 0 && requested_brightness == 37);
    lv_port_disp_service_backlight();
    assert(applied_duty == 0 && !first_frame_revealed);

    /* One full screen consists of eight 60-line DMA strips. Queueing the
     * last strip is insufficient: only its completion releases the gate. */
    lv_disp_drv_t driver = {0};
    uint16_t pixels[320 * 60] = {0};
    for (int strip = 0; strip < 8; ++strip) {
        driver.last = strip == 7;
        lv_area_t area = {0, strip * 60, 319, strip * 60 + 59};
        disp_flush(&driver, &area, pixels);
        lv_port_disp_service_backlight();
        assert(!first_frame_complete && applied_duty == 0);
        in_isr = true;
        disp_flush_ready_cb(NULL, NULL, &driver);
        in_isr = false;
        assert(applied_duty == 0); /* No LEDC calls from the ISR. */
        if (strip != 7) {
            lv_port_disp_service_backlight();
            assert(!first_frame_revealed && applied_duty == 0);
        }
    }
    assert(first_frame_complete && transfers == 8 && flush_ready_count == 8);
    assert(recorded_frames == 1);
    assert(!lv_port_disp_first_frame_ready());
    lv_port_disp_service_backlight();
    assert(first_frame_revealed && first_frame_ready);
    assert(lv_port_disp_first_frame_ready());
    assert(applied_duty == (1023U * 37 + 50U) / 100U);
    unsigned previous = duty_updates;
    lv_port_disp_service_backlight();
    assert(duty_updates == previous); /* Subsequent UI ticks are idempotent. */
    ScreenSetBrightness(0);
    assert(applied_duty == 0);
    ScreenSetBrightness(255);
    assert(requested_brightness == 100 && applied_duty == 1023);

    /* Actual UI startup branch: force the frame immediately, wait for DMA,
     * reveal it and then wait for the app-ready notification. */
    ScreenBacklightHoldOff();
    reset_frame_gate();
    ScreenSetBrightness(55);
    exercise_ui_startup(NULL);
    assert(startup_refreshes == 0 && startup_notifications == 0);
    exercise_ui_startup((void *)1);
    assert(startup_refreshes == 1 && startup_delays == 1 && startup_notifications == 1);
    assert(applied_duty == (1023U * 55 + 50U) / 100U);

    /* Hold-off can be rearmed without forgetting the desired brightness. */
    ScreenBacklightHoldOff();
    reset_frame_gate();
    ScreenSetBrightness(0);
    in_isr = true;
    disp_flush_ready_cb(NULL, NULL, &driver);
    in_isr = false;
    lv_port_disp_service_backlight();
    assert(first_frame_ready && applied_duty == 0);

    /* Missing panel or a rejected DMA request must never reveal random GRAM. */
    ScreenBacklightHoldOff();
    reset_frame_gate();
    ScreenSetBrightness(100);
    g_panel_handle = NULL;
    lv_area_t area = {0, 0, 319, 59};
    disp_flush(&driver, &area, pixels);
    lv_port_disp_service_backlight();
    assert(!first_frame_revealed && applied_duty == 0);
    g_panel_handle = (void *)1;
    draw_result = -1;
    disp_flush(&driver, &area, pixels);
    lv_port_disp_service_backlight();
    assert(errors == 1 && !first_frame_revealed && applied_duty == 0);
    puts("PASS: early GPIO hold-off, full-frame DMA completion, ISR isolation,");
    puts("      deferred brightness, zero/clamped brightness, one-time release and failure paths");
    puts("PASS: immediate UI startup frame, DMA wait and application-ready handshake");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=shutil.which("gcc"))
    args = parser.parse_args()
    if not args.cc:
        raise SystemExit("A host GCC is required; pass --cc.")
    rgb = (ROOT / "components/RGB/RGB.c").read_text(encoding="utf-8")
    display = (ROOT / "components/myLvgl/lv_port_disp.c").read_text(encoding="utf-8")
    boot = (ROOT / "main/main.c").read_text(encoding="utf-8")
    boot = boot[boot.index("void app_main(void)"):]
    assert boot.index("ScreenBacklightHoldOff();") < boot.index("StorageInit();")
    assert boot.index("ui_init();") < boot.index("WifiInit();")
    assert boot.count("xTaskCreatePinnedToCoreWithCaps(lvUIshowTask") == 1
    assert boot.index("xTaskNotifyGive(startup_ui_task);") > boot.index("WifiInit();")
    ui = (ROOT / "components/myLvgl/myLvgl.c").read_text(encoding="utf-8")
    assert re.search(r"lv_task_handler\(\);\s*#ifdef ESP_PLATFORM\s*"
                     r"lv_port_disp_service_backlight\(\);", ui)
    assert re.search(r"\.duty\s*=\s*0\s*,", rgb)
    startup = function(ui, "lvUIshowTask")
    branch_start = startup.index("if (arg != NULL) {")
    branch_end = startup.index("\n#endif", branch_start)
    startup_branch = startup[branch_start:branch_end]
    assert startup.index("ulTaskNotifyTake(") < startup.index("UiFlowConfigure();")
    production = "\n".join(function(rgb, name) for name in (
        "ScreenBacklightHoldOff", "ScreenSetBrightness", "ScreenNotifyFirstFrameReady"
    ))
    production += "\n" + "\n".join(function(display, name) for name in (
        "lv_port_disp_first_frame_ready", "lv_port_disp_service_backlight",
        "disp_flush_ready_cb", "invert_pixels", "disp_flush"
    ))
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / "test.c"
    startup_test = "\nstatic void exercise_ui_startup(void *arg) {\n" + startup_branch + "\n}\n"
    source.write_text(PREFIX + production + STARTUP_MOCKS + startup_test + CHECKS,
                      encoding="utf-8")
    executable = OUT / "test.exe"
    subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
