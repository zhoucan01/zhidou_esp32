#include "ui_tofu_internal.h"

#define IMG(src, x, y, align) \
    { &(src), NULL, (x), (y), (align) }
#define LABEL(text, x, y, align, color, font, angle, pivot_x, pivot_y, hidden) \
    { (text), (x), (y), (align), (color), &(font), (angle), \
      (pivot_x), (pivot_y), (hidden) }

enum {
    UI_TOFU_MAKING_TITLE_LABEL = 0U,
    UI_TOFU_MAKING_STATUS_LABEL,
    UI_TOFU_MAKING_AMOUNT_LABEL,
    UI_TOFU_MAKING_TIME_LABEL,
    UI_TOFU_MAKING_STOP_LABEL,
};

const ui_image_config_t
    ui_tofu_making_images[UI_TOFU_MAKING_IMAGE_COUNT] = {
        IMG(ui_img_high_png, -119, -4, LV_ALIGN_CENTER),
        IMG(ui_img_wait_png_lz4, 17, -4, LV_ALIGN_CENTER),
        IMG(ui_img_1036112462, 25, -166, LV_ALIGN_CENTER),
};

const ui_label_config_t
    ui_tofu_making_labels[UI_TOFU_MAKING_LABEL_COUNT] = {
        LABEL("Soft Tofu", 193, -216, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Making tofu", 123, -102, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_20, 900, 0, 0, false),
        LABEL("150g", 55, -100, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_16, 900, 0, 0, false),
        LABEL("About 50 min left", 135, -97, LV_ALIGN_CENTER, 0xF2BE9F,
              UI_FONT_28, 900, 0, 0, false),
        LABEL("Stop", -86, -6, LV_ALIGN_CENTER, 0xF2BE9F,
              UI_FONT_20, 900, 0, 0, false),
};

static const char *const tofu_amount_labels[UI_TOFU_AMOUNT_COUNT] = {
    [UI_TOFU_AMOUNT_100G] = "100g",
    [UI_TOFU_AMOUNT_150G] = "150g",
    [UI_TOFU_AMOUNT_200G] = "200g",
};

const void *ui_tofu_making_active_image_src(uint8_t index, bool focused)
{
    (void)focused;
    return ui_tofu_making_images[index].src;
}

const char *ui_tofu_making_active_label_text(uint8_t index)
{
    if (index == UI_TOFU_MAKING_TITLE_LABEL) {
        return ui_tofu_firmness_details_title();
    }
    if (index == UI_TOFU_MAKING_AMOUNT_LABEL) {
        return tofu_amount_labels[ui_get_selected_tofu_amount()];
    }
    return ui_tofu_making_labels[index].text;
}
