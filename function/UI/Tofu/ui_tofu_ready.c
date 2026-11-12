#include "ui_tofu_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "DataCenter.h"
#include "Music.h"

static portMUX_TYPE collect_lock = portMUX_INITIALIZER_UNLOCKED;
static bool collecting;

bool ui_tofu_ready_is_collecting(void)
{
    portENTER_CRITICAL(&collect_lock);
    bool active = collecting;
    portEXIT_CRITICAL(&collect_lock);
    return active;
}

static void collect_task(void *arg)
{
    (void)arg;
    /* Preserve the original Press.c steps 6 and 7 and their timings. */
    riseMoterUp(23);
    while (getSensorData(riseHighPos) != 1) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    riseMoterStop();
    vTaskDelay(pdMS_TO_TICKS(300));

    pressMoterUp(800); /* 80 rpm */
    while (getSensorData(pressHighPos) != 1) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    pressMoterStop();
    Play_Music();
    riseMoterSetCurrentGear(0);

    portENTER_CRITICAL(&collect_lock);
    collecting = false;
    portEXIT_CRITICAL(&collect_lock);
    vTaskDelete(NULL);
}

bool ui_tofu_ready_collect(void)
{
    portENTER_CRITICAL(&collect_lock);
    if (collecting) {
        portEXIT_CRITICAL(&collect_lock);
        return false;
    }
    collecting = true;
    portEXIT_CRITICAL(&collect_lock);

    if (xTaskCreate(collect_task, "TofuCollect", 4096, NULL, 13, NULL) != pdPASS) {
        portENTER_CRITICAL(&collect_lock);
        collecting = false;
        portEXIT_CRITICAL(&collect_lock);
        return false;
    }
    return true;
}

#define IMG(src, x, y, align) \
    { &(src), NULL, (x), (y), (align) }
#define IMG_HL(src, src_alt, x, y, align) \
    { &(src), &(src_alt), (x), (y), (align) }
#define LABEL(text, x, y, align, color, font, angle, pivot_x, pivot_y, hidden) \
    { (text), (x), (y), (align), (color), &(font), (angle), \
      (pivot_x), (pivot_y), (hidden) }

const ui_image_config_t
    ui_tofu_ready_images[UI_TOFU_READY_IMAGE_COUNT] = {
        IMG(ui_img_wait_png_lz4, 13, -3, LV_ALIGN_CENTER),
        IMG_HL(ui_img_1_png, ui_img_2_png,
               -117, -112, LV_ALIGN_CENTER),
        IMG_HL(ui_img_1_png, ui_img_2_png,
               -117, 112, LV_ALIGN_CENTER),
        IMG(ui_img_1036112462, 25, -166, LV_ALIGN_CENTER),
};

const ui_label_config_t
    ui_tofu_ready_labels[UI_TOFU_READY_LABEL_COUNT] = {
        LABEL("Tofu is ready", 210, -214, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Please take out your tofu.", 192, -100,
              LV_ALIGN_CENTER, 0xF2BE9F,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Caution: hot surfaces.", 110, -100,
              LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_18, 900, 0, 0, false),
        LABEL("Collect Pulp", -44, -162, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_20, 900, 0, 0, false),
        LABEL("Clean", -78, 94, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_20, 900, 0, 0, false),
};

const void *ui_tofu_ready_active_image_src(uint8_t index, bool focused)
{
    const ui_image_config_t *image = &ui_tofu_ready_images[index];
    return focused && image->src_alt != NULL ? image->src_alt : image->src;
}

const char *ui_tofu_ready_active_label_text(uint8_t index)
{
    return ui_tofu_ready_labels[index].text;
}

uint32_t ui_tofu_ready_active_label_color(uint8_t index)
{
    uint8_t selected = ui_get_selected_image(UI_PAGE_TOFU_READY);
    if ((index == 3U && selected == UI_TOFU_READY_COLLECT_BUTTON) ||
        (index == 4U && selected == UI_TOFU_READY_CLEAN_BUTTON)) {
        return 0xF2BE9FU;
    }
    return ui_tofu_ready_labels[index].color_hex;
}

void ui_select_tofu_ready_control(ui_tofu_ready_control_t control)
{
    if (!ui_is_initialized() ||
        (control != UI_TOFU_READY_CONTROL_COLLECT &&
         control != UI_TOFU_READY_CONTROL_CLEAN)) {
        return;
    }

    ui_internal_set_selected_image(UI_PAGE_TOFU_READY, (uint8_t)control);
    if (ui_get_current_page() == UI_PAGE_TOFU_READY) {
        ui_show_page(UI_PAGE_TOFU_READY);
    }
}

ui_tofu_ready_control_t ui_get_selected_tofu_ready_control(void)
{
    if (ui_get_selected_image(UI_PAGE_TOFU_READY) ==
        UI_TOFU_READY_CONTROL_CLEAN) {
        return UI_TOFU_READY_CONTROL_CLEAN;
    }
    return UI_TOFU_READY_CONTROL_COLLECT;
}
