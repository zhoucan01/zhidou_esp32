#include "ui_tofu_internal.h"

#define IMG(src, x, y, align) \
    { &(src), NULL, (x), (y), (align) }
#define IMG_HL(src, src_alt, x, y, align) \
    { &(src), &(src_alt), (x), (y), (align) }
#define LABEL(text, x, y, align, color, font, angle, pivot_x, pivot_y, hidden) \
    { (text), (x), (y), (align), (color), &(font), (angle), \
      (pivot_x), (pivot_y), (hidden) }

const ui_image_config_t
    ui_stop_tofu_images[UI_STOP_TOFU_IMAGE_COUNT] = {
        IMG(ui_img_wait_png_lz4, 5, -6, LV_ALIGN_CENTER),
        IMG_HL(ui_img_1_png, ui_img_2_png,
               -125, 106, LV_ALIGN_CENTER),
        IMG_HL(ui_img_1_png, ui_img_2_png,
               -125, -119, LV_ALIGN_CENTER),
        IMG(ui_img_hourglass_png, 15, -174, LV_ALIGN_CENTER),
};

const ui_label_config_t
    ui_stop_tofu_labels[UI_STOP_TOFU_LABEL_COUNT] = {
        LABEL("Stop making tofu?", 232, -208, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("This batch will end.", 132, -94,
              LV_ALIGN_CENTER, 0xF2BE9F,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Keep Making", -46, -173,
              LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_20, 900, 0, 0, false),
        LABEL("Stop", -90, 91, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_20, 900, 0, 0, false),
};

const void *ui_stop_tofu_active_image_src(uint8_t index, bool focused)
{
    const ui_image_config_t *image = &ui_stop_tofu_images[index];
    return focused && image->src_alt != NULL ? image->src_alt : image->src;
}

const char *ui_stop_tofu_active_label_text(uint8_t index)
{
    return ui_stop_tofu_labels[index].text;
}

uint32_t ui_stop_tofu_active_label_color(uint8_t index)
{
    uint8_t selected = ui_get_selected_image(UI_PAGE_STOP_TOFU_CONFIRM);
    if ((index == 2U && selected == UI_STOP_TOFU_KEEP_BUTTON) ||
        (index == 3U && selected == UI_STOP_TOFU_STOP_BUTTON)) {
        return 0xF2BE9FU;
    }
    return ui_stop_tofu_labels[index].color_hex;
}

void ui_select_stop_tofu_control(ui_stop_tofu_control_t control)
{
    if (!ui_is_initialized() ||
        (control != UI_STOP_TOFU_CONTROL_STOP &&
         control != UI_STOP_TOFU_CONTROL_KEEP)) {
        return;
    }

    ui_internal_set_selected_image(UI_PAGE_STOP_TOFU_CONFIRM,
                                   (uint8_t)control);
    if (ui_get_current_page() == UI_PAGE_STOP_TOFU_CONFIRM) {
        ui_show_page(UI_PAGE_STOP_TOFU_CONFIRM);
    }
}

ui_stop_tofu_control_t ui_get_selected_stop_tofu_control(void)
{
    if (ui_get_selected_image(UI_PAGE_STOP_TOFU_CONFIRM) ==
        UI_STOP_TOFU_CONTROL_STOP) {
        return UI_STOP_TOFU_CONTROL_STOP;
    }
    return UI_STOP_TOFU_CONTROL_KEEP;
}
