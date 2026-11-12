#include "ui_tofu_internal.h"

#define IMG(src, x, y, align) \
    { &(src), NULL, (x), (y), (align) }
#define IMG_HL(src, src_alt, x, y, align) \
    { &(src), &(src_alt), (x), (y), (align) }
#define LABEL(text, x, y, align, color, font, angle, pivot_x, pivot_y, hidden) \
    { (text), (x), (y), (align), (color), &(font), (angle), \
      (pivot_x), (pivot_y), (hidden) }

const ui_image_config_t
    ui_tofu_stopped_images[UI_TOFU_STOPPED_IMAGE_COUNT] = {
        IMG(ui_img_wait_png_lz4, 12, -1, LV_ALIGN_CENTER),
        IMG_HL(ui_img_grey_png, ui_img_high_png,
               -117, -1, LV_ALIGN_CENTER),
        IMG(ui_img_hourglass_png, 19, -174, LV_ALIGN_CENTER),
};

const ui_label_config_t
    ui_tofu_stopped_labels[UI_TOFU_STOPPED_LABEL_COUNT] = {
        LABEL("Making stopped", 230, -212, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("This batch is unfinished.", 179, -90,
              LV_ALIGN_CENTER, 0xF2BE9F,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Clean before making again.", 131, -89,
              LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_18, 900, 0, 0, false),
        LABEL("Clean", -78, -8, LV_ALIGN_CENTER, 0xF2BE9F,
              UI_FONT_20, 900, 0, 0, false),
};

const void *ui_tofu_stopped_active_image_src(uint8_t index, bool focused)
{
    const ui_image_config_t *image = &ui_tofu_stopped_images[index];
    return focused && image->src_alt != NULL ? image->src_alt : image->src;
}

const char *ui_tofu_stopped_active_label_text(uint8_t index)
{
    return ui_tofu_stopped_labels[index].text;
}
