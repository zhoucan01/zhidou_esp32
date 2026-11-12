#include "ui_tofu_internal.h"

#define IMG(src, x, y, align) \
    { &(src), NULL, (x), (y), (align) }
#define LABEL(text, x, y, align, color, font, angle, pivot_x, pivot_y, hidden) \
    { (text), (x), (y), (align), (color), &(font), (angle), \
      (pivot_x), (pivot_y), (hidden) }

const ui_image_config_t
    ui_tofu_stopping_images[UI_TOFU_STOPPING_IMAGE_COUNT] = {
        IMG(ui_img_wait_png_lz4, 10, -2, LV_ALIGN_CENTER),
        IMG(ui_img_grey_png, -123, -2, LV_ALIGN_CENTER),
        IMG(ui_img_hourglass_png, 17, -175, LV_ALIGN_CENTER),
};

const ui_label_config_t
    ui_tofu_stopping_labels[UI_TOFU_STOPPING_LABEL_COUNT] = {
        LABEL("Stopping...", 190, -206, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Please wait.", 111, -80,
              LV_ALIGN_CENTER, 0xF2BE9F,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Keep parts in place.", 99, -82,
              LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_18, 900, 0, 0, false),
        LABEL("Clean", -84, -9, LV_ALIGN_CENTER, 0x777671,
              UI_FONT_20, 900, 0, 0, false),
};

void ui_tofu_stopping_reset(void)
{
}

const void *ui_tofu_stopping_active_image_src(uint8_t index, bool focused)
{
    (void)focused;
    return ui_tofu_stopping_images[index].src;
}

const char *ui_tofu_stopping_active_label_text(uint8_t index)
{
    return ui_tofu_stopping_labels[index].text;
}

void ui_tofu_stopping_set_complete(bool complete)
{
    if (complete && ui_is_initialized()) {
        ui_show_page(UI_PAGE_TOFU_STOPPED);
    }
}

bool ui_tofu_stopping_is_complete(void)
{
    return ui_get_current_page() == UI_PAGE_TOFU_STOPPED;
}
