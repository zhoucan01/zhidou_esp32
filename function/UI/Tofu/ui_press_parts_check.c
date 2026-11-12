#include "ui_tofu_internal.h"

#include <string.h>

#define IMG(src, x, y, align) \
    { &(src), NULL, (x), (y), (align) }
#define IMG_HL(src, src_alt, x, y, align) \
    { &(src), &(src_alt), (x), (y), (align) }
#define LABEL(text, x, y, align, color, font, angle, pivot_x, pivot_y, hidden) \
    { (text), (x), (y), (align), (color), &(font), (angle), \
      (pivot_x), (pivot_y), (hidden) }

enum {
    UI_PRESS_PARTS_TITLE_LABEL = 0U,
    UI_PRESS_PARTS_HEAD_HEADING_LABEL,
    UI_PRESS_PARTS_HEAD_STATUS_LABEL,       /* 对应 SquareLine 控件 Label43。 */
    UI_PRESS_PARTS_CHANNEL_HEADING_LABEL,
    UI_PRESS_PARTS_CHANNEL_STATUS_LABEL,    /* 对应 SquareLine 控件 Label45。 */
    UI_PRESS_PARTS_INSTRUCTION_LABEL,       /* 对应 SquareLine 控件 Label46。 */
    UI_PRESS_PARTS_START_LABEL,
};

const ui_image_config_t
    ui_press_parts_images[UI_PRESS_PARTS_IMAGE_COUNT] = {
        IMG_HL(ui_img_back_png, ui_img_back_high_png,
               123, 180, LV_ALIGN_CENTER),
        IMG(ui_img_segment_png, 91, -4, LV_ALIGN_CENTER),
        IMG_HL(ui_img_grey_png, ui_img_high_png,
               -122, -5, LV_ALIGN_CENTER),
        IMG(ui_img_444953556, 10, 112, LV_ALIGN_CENTER),
        IMG(ui_img_444953556, 9, -118, LV_ALIGN_CENTER),
        IMG(ui_img_881831439, 39, -185, LV_ALIGN_CENTER),
        IMG(ui_img_1092992141, 41, 36, LV_ALIGN_CENTER),
};

const ui_label_config_t
    ui_press_parts_labels[UI_PRESS_PARTS_LABEL_COUNT] = {
        LABEL("Fit the parts", 204, -216, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Fit press head", 71, -196, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_18, 900, 0, 0, false),
        LABEL("Install first", 22, -196, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_16, 900, 0, 0, false),
        LABEL("Fit mould assembly", 99, 26, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_18, 900, 0, 0, false),
        LABEL("Install next", 23, 27, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_16, 900, 0, 0, false),
        LABEL("Fit press head", -14, -216, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_16, 900, 0, 0, false),
        LABEL("Start Making", -47, -43, LV_ALIGN_CENTER, 0xF5F3EB,
              UI_FONT_20, 900, 0, 0, false),
};

static ui_press_parts_sensors_t sensor_state;

void ui_press_parts_check_reset(void)
{
    memset(&sensor_state, 0, sizeof(sensor_state));
}

bool ui_press_parts_check_can_start(void)
{
    return sensor_state.press_head_present &&
           sensor_state.press_channel_present;
}

const void *ui_press_parts_active_image_src(uint8_t index, bool focused)
{
    const ui_image_config_t *image = &ui_press_parts_images[index];
    bool highlighted = false;

    switch (index) {
        case UI_PRESS_PARTS_BACK_BUTTON:
            highlighted = focused;
            break;
        case UI_PRESS_PARTS_START_BUTTON:
            highlighted = focused && ui_press_parts_check_can_start();
            break;
        case UI_PRESS_PARTS_MOULD_IMAGE:
            return sensor_state.press_channel_present
                       ? &ui_img_machinepress_2_png
                       : &ui_img_444953556;
        case UI_PRESS_PARTS_HEAD_IMAGE:
            return sensor_state.press_head_present
                       ? &ui_img_machinepress_2_png
                       : &ui_img_444953556;
        case UI_PRESS_PARTS_HEAD_ICON:
            return sensor_state.press_head_present
                       ? &ui_img_machinepress_png
                       : &ui_img_881831439;
        case UI_PRESS_PARTS_MOULD_ICON:
            return sensor_state.press_channel_present
                       ? &ui_img_machinemould_png
                       : &ui_img_1092992141;
        default:
            break;
    }

    return highlighted && image->src_alt != NULL ? image->src_alt : image->src;
}

const char *ui_press_parts_active_label_text(uint8_t index)
{
    switch (index) {
        case UI_PRESS_PARTS_HEAD_STATUS_LABEL:
            return sensor_state.press_head_present ? "Installed" : "Install first";
        case UI_PRESS_PARTS_CHANNEL_STATUS_LABEL:
            if (!sensor_state.press_channel_present) {
                return "Install next";
            }
            return sensor_state.press_head_present ? "Installed" : "Waiting";
        case UI_PRESS_PARTS_INSTRUCTION_LABEL:
            if (!sensor_state.press_head_present) {
                return sensor_state.press_channel_present
                           ? "Remove press chamber. Fit press head first."
                           : "Fit press head";
            }
            return sensor_state.press_channel_present ? "Ready"
                                                      : "Fit mould assembly";
        default:
            return ui_press_parts_labels[index].text;
    }
}

uint32_t ui_press_parts_active_label_color(uint8_t index)
{
    if (index == UI_PRESS_PARTS_HEAD_STATUS_LABEL) {
        return 0xF2BE9FU;
    }
    if (index == UI_PRESS_PARTS_CHANNEL_STATUS_LABEL) {
        /* 即使模具已安装，也必须先等待压头安装到位。 */
        return sensor_state.press_channel_present &&
                       !sensor_state.press_head_present
                   ? 0xFFFFFFU
                   : 0xF2BE9FU;
    }
    return ui_press_parts_labels[index].color_hex;
}

void ui_select_press_parts_control(ui_press_parts_control_t control)
{
    if (!ui_is_initialized() ||
        (control != UI_PRESS_PARTS_CONTROL_BACK &&
         control != UI_PRESS_PARTS_CONTROL_START)) {
        return;
    }

    ui_internal_set_selected_image(UI_PAGE_PRESS_PARTS_CHECK,
                                   (uint8_t)control);
    if (ui_get_current_page() == UI_PAGE_PRESS_PARTS_CHECK) {
        ui_show_page(UI_PAGE_PRESS_PARTS_CHECK);
    }
}

ui_press_parts_control_t ui_get_selected_press_parts_control(void)
{
    if (ui_get_selected_image(UI_PAGE_PRESS_PARTS_CHECK) ==
        UI_PRESS_PARTS_CONTROL_BACK) {
        return UI_PRESS_PARTS_CONTROL_BACK;
    }
    return UI_PRESS_PARTS_CONTROL_START;
}

void ui_press_parts_check_update(const ui_press_parts_sensors_t *sensors)
{
    if (sensors == NULL ||
        (sensor_state.press_head_present == sensors->press_head_present &&
         sensor_state.press_channel_present == sensors->press_channel_present)) {
        return;
    }

    bool was_ready = ui_press_parts_check_can_start();
    sensor_state = *sensors;
    if (!was_ready && ui_press_parts_check_can_start()) {
        ui_internal_set_selected_image(UI_PAGE_PRESS_PARTS_CHECK,
                                       UI_PRESS_PARTS_CONTROL_START);
    }
    if (ui_is_initialized() &&
        ui_get_current_page() == UI_PAGE_PRESS_PARTS_CHECK) {
        ui_show_page(UI_PAGE_PRESS_PARTS_CHECK);
    }
}
