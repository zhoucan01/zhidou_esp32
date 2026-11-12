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
    UI_MACHINE_CHECK_TITLE_LABEL = 0U,
    UI_MACHINE_CHECK_LID_HEADING_LABEL,
    UI_MACHINE_CHECK_LID_STATUS_LABEL,
    UI_MACHINE_CHECK_COAGULANT_HEADING_LABEL,
    UI_MACHINE_CHECK_COAGULANT_STATUS_LABEL,
    UI_MACHINE_CHECK_WATER_HEADING_LABEL,
    UI_MACHINE_CHECK_WATER_STATUS_LABEL,
    UI_MACHINE_CHECK_WASTE_HEADING_LABEL,
    UI_MACHINE_CHECK_WASTE_STATUS_LABEL,
    UI_MACHINE_CHECK_INSTRUCTION_LABEL,
    UI_MACHINE_CHECK_CONTINUE_LABEL,
};

const ui_image_config_t
    ui_machine_check_images[UI_MACHINE_CHECK_IMAGE_COUNT] = {
        IMG_HL(ui_img_back_png, ui_img_back_high_png,
               123, 180, LV_ALIGN_CENTER),
        IMG(ui_img_check1_png, 87, -3, LV_ALIGN_CENTER),
        IMG(ui_img_check_png, 35, -115, LV_ALIGN_CENTER),
        IMG(ui_img_check_png, 36, 114, LV_ALIGN_CENTER),
        IMG(ui_img_check_png, -41, -115, LV_ALIGN_CENTER),
        IMG(ui_img_check_png, -40, 116, LV_ALIGN_CENTER),
        IMG_HL(ui_img_grey_png, ui_img_high_png,
               -128, 2, LV_ALIGN_CENTER),
        IMG(ui_img_machinelid_png, 34, -44, LV_ALIGN_CENTER),
        IMG(ui_img_machinecoagulant_png, 36, 185, LV_ALIGN_CENTER),
        IMG(ui_img_machinewatertank_png, -41, -43, LV_ALIGN_CENTER),
        IMG(ui_img_machinedriptray_png, -38, 185, LV_ALIGN_CENTER),
};

const ui_label_config_t
    ui_machine_check_labels[UI_MACHINE_CHECK_LABEL_COUNT] = {
        LABEL("Check the machine", 244, -217, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Lid", 74, -193, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Closed lid", 68, -195, LV_ALIGN_CENTER, 0xF2BE9F,
              UI_FONT_16, 900, 0, 0, false),
        LABEL("Coagulant", 116, 34, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Fit container", 82, 35, LV_ALIGN_CENTER, 0xF2BE9F,
              UI_FONT_16, 900, 0, 0, false),
        LABEL("Water tank", 45, -189, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Fit water tank", 10, -188, LV_ALIGN_CENTER, 0xF2BE9F,
              UI_FONT_16, 900, 0, 0, false),
        LABEL("Drip tray", 30, 36, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Fit drip tray", -1, 35, LV_ALIGN_CENTER, 0xF2BE9F,
              UI_FONT_16, 900, 0, 0, false),
        LABEL("Empty drip tray", -22, -211, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_16, 900, 0, 0, false),
        LABEL("Continue", -72, -33, LV_ALIGN_CENTER, 0xF5F3EB,
              UI_FONT_20, 900, 0, 0, false),
};

static ui_machine_check_sensors_t sensor_state;

static bool lid_ready(void)
{
    return sensor_state.lid_present;
}

static bool coagulant_ready(void)
{
    return sensor_state.coagulant_container_present &&
           !sensor_state.coagulant_low_level;
}

static bool water_ready(void)
{
    return sensor_state.water_tank_present;
}

static bool waste_ready(void)
{
    return sensor_state.waste_tank_present;
}

static bool sensors_equal(const ui_machine_check_sensors_t *left,
                          const ui_machine_check_sensors_t *right)
{
    return left->lid_present == right->lid_present &&
           left->coagulant_container_present ==
               right->coagulant_container_present &&
           left->coagulant_low_level == right->coagulant_low_level &&
           left->water_tank_present == right->water_tank_present &&
           left->waste_tank_present == right->waste_tank_present;
}

void ui_machine_check_reset(void)
{
    memset(&sensor_state, 0, sizeof(sensor_state));
}

bool ui_machine_check_can_continue(void)
{
    return lid_ready() && coagulant_ready() && water_ready() && waste_ready();
}

const void *ui_machine_check_active_image_src(uint8_t index, bool focused)
{
    const ui_image_config_t *image = &ui_machine_check_images[index];
    bool highlighted = false;

    switch (index) {
        case UI_MACHINE_CHECK_BACK_BUTTON:
            highlighted = focused;
            break;
        case UI_MACHINE_CHECK_LID_IMAGE:
            return lid_ready() ? &ui_img_check_png : &ui_img_lid_png;
        case UI_MACHINE_CHECK_COAGULANT_IMAGE:
            return coagulant_ready() ? &ui_img_check_png : &ui_img_lid_png;
        case UI_MACHINE_CHECK_WATER_IMAGE:
            return water_ready() ? &ui_img_check_png : &ui_img_lid_png;
        case UI_MACHINE_CHECK_WASTE_IMAGE:
            return waste_ready() ? &ui_img_check_png : &ui_img_lid_png;
        case UI_MACHINE_CHECK_CONTINUE_BUTTON:
            highlighted = focused && ui_machine_check_can_continue();
            break;
        case UI_MACHINE_CHECK_LID_ICON:
            return lid_ready() ? &ui_img_machinelid_png : &ui_img_174602179;
        case UI_MACHINE_CHECK_COAGULANT_ICON:
            return coagulant_ready() ? &ui_img_machinecoagulant_png :
                                      &ui_img_1313577400;
        case UI_MACHINE_CHECK_WATER_ICON:
            return water_ready() ? &ui_img_machinewatertank_png :
                                   &ui_img_1205051221;
        case UI_MACHINE_CHECK_WASTE_ICON:
            return waste_ready() ? &ui_img_machinedriptray_png :
                                   &ui_img_2052261345;
        default:
            break;
    }

    return highlighted && image->src_alt != NULL ? image->src_alt : image->src;
}

const char *ui_machine_check_active_label_text(uint8_t index)
{
    switch (index) {
        case UI_MACHINE_CHECK_LID_STATUS_LABEL:
            return lid_ready() ? "Closed" : "Closed Lid";
        case UI_MACHINE_CHECK_COAGULANT_STATUS_LABEL:
            if (!sensor_state.coagulant_container_present) {
                return "Fit container";
            }
            return sensor_state.coagulant_low_level ? "Add coagulant" :
                                                      "Ready";
        case UI_MACHINE_CHECK_WATER_STATUS_LABEL:
            return water_ready() ? "Ready" : "Fit water tank";
        case UI_MACHINE_CHECK_WASTE_STATUS_LABEL:
            return waste_ready() ? "Ready" : "Fit drip tray";
        default:
            return ui_machine_check_labels[index].text;
    }
}

uint32_t ui_machine_check_active_label_color(uint8_t index)
{
    switch (index) {
        case UI_MACHINE_CHECK_LID_STATUS_LABEL:
            return lid_ready() ? 0xAACFA3U : 0xF2BE9FU;
        case UI_MACHINE_CHECK_COAGULANT_STATUS_LABEL:
            return coagulant_ready() ? 0xAACFA3U : 0xF2BE9FU;
        case UI_MACHINE_CHECK_WATER_STATUS_LABEL:
            return water_ready() ? 0xAACFA3U : 0xF2BE9FU;
        case UI_MACHINE_CHECK_WASTE_STATUS_LABEL:
            return waste_ready() ? 0xAACFA3U : 0xF2BE9FU;
        case UI_MACHINE_CHECK_CONTINUE_LABEL:
            if (!ui_machine_check_can_continue()) {
                return 0x777671U;
            }
            return ui_get_selected_machine_check_control() ==
                           UI_MACHINE_CHECK_CONTROL_CONTINUE
                       ? 0xF2BE9FU : 0xFFFFFFU;
        default:
            return ui_machine_check_labels[index].color_hex;
    }
}

void ui_select_machine_check_control(ui_machine_check_control_t control)
{
    if (!ui_is_initialized() ||
        (control != UI_MACHINE_CHECK_CONTROL_BACK &&
         control != UI_MACHINE_CHECK_CONTROL_CONTINUE)) {
        return;
    }

    ui_internal_set_selected_image(UI_PAGE_MACHINE_CHECK, (uint8_t)control);
    if (ui_get_current_page() == UI_PAGE_MACHINE_CHECK) {
        ui_show_page(UI_PAGE_MACHINE_CHECK);
    }
}

ui_machine_check_control_t ui_get_selected_machine_check_control(void)
{
    uint8_t selected = ui_get_selected_image(UI_PAGE_MACHINE_CHECK);
    if (selected == UI_MACHINE_CHECK_CONTROL_BACK) {
        return UI_MACHINE_CHECK_CONTROL_BACK;
    }
    return UI_MACHINE_CHECK_CONTROL_CONTINUE;
}

void ui_machine_check_update(const ui_machine_check_sensors_t *sensors)
{
    if (sensors == NULL || sensors_equal(&sensor_state, sensors)) {
        return;
    }

    bool was_ready = ui_machine_check_can_continue();
    sensor_state = *sensors;
    sensor_state.water_tank_high_level = false;
    if (!was_ready && ui_machine_check_can_continue()) {
        ui_internal_set_selected_image(UI_PAGE_MACHINE_CHECK,
                                       UI_MACHINE_CHECK_CONTROL_CONTINUE);
    }

    if (ui_is_initialized() &&
        ui_get_current_page() == UI_PAGE_MACHINE_CHECK) {
        ui_show_page(UI_PAGE_MACHINE_CHECK);
    }
}

void ui_machine_check_set_water_tank_high_level_handler(
    ui_water_tank_high_level_handler_t handler)
{
    /* 预留供后续使用；水箱就绪状态仅检查是否在位。 */
    (void)handler;
}
