#ifndef UI_TOFU_INTERNAL_H
#define UI_TOFU_INTERNAL_H

#include "ui.h"
#include "ui_lz4_assets.h"

#define UI_HOME_IMAGE_COUNT 9U
#define UI_HOME_LABEL_COUNT 3U
#define UI_TOFU_FIRMNESS_IMAGE_COUNT 2U
#define UI_TOFU_FIRMNESS_LABEL_COUNT 3U
#define UI_FIRM_TOFU_IMAGE_COUNT 7U
#define UI_FIRM_TOFU_LABEL_COUNT 4U
#define UI_MORE_OPTIONS_IMAGE_COUNT 8U
#define UI_MORE_OPTIONS_LABEL_COUNT 6U
#define UI_ADD_INGREDIENTS_IMAGE_COUNT 7U
#define UI_ADD_INGREDIENTS_LABEL_COUNT 6U
#define UI_MACHINE_CHECK_IMAGE_COUNT 11U
#define UI_MACHINE_CHECK_LABEL_COUNT 11U
#define UI_PRESS_PARTS_IMAGE_COUNT 7U
#define UI_PRESS_PARTS_LABEL_COUNT 7U
#define UI_TOFU_MAKING_IMAGE_COUNT 3U
#define UI_TOFU_MAKING_LABEL_COUNT 5U
#define UI_TOFU_READY_IMAGE_COUNT 4U
#define UI_TOFU_READY_LABEL_COUNT 5U
#define UI_STOP_TOFU_IMAGE_COUNT 4U
#define UI_STOP_TOFU_LABEL_COUNT 4U
#define UI_TOFU_STOPPING_IMAGE_COUNT 3U
#define UI_TOFU_STOPPING_LABEL_COUNT 4U
#define UI_TOFU_STOPPED_IMAGE_COUNT 3U
#define UI_TOFU_STOPPED_LABEL_COUNT 4U

extern const ui_image_config_t ui_home_images[UI_HOME_IMAGE_COUNT];
extern const ui_label_config_t ui_home_labels[UI_HOME_LABEL_COUNT];
extern const ui_image_config_t
    ui_tofu_firmness_images[UI_TOFU_FIRMNESS_IMAGE_COUNT];
extern const ui_label_config_t
    ui_tofu_firmness_labels[UI_TOFU_FIRMNESS_LABEL_COUNT];
extern const ui_image_config_t
    ui_firm_tofu_images[UI_FIRM_TOFU_IMAGE_COUNT];
extern const ui_label_config_t
    ui_firm_tofu_labels[UI_FIRM_TOFU_LABEL_COUNT];
extern const ui_image_config_t
    ui_more_options_images[UI_MORE_OPTIONS_IMAGE_COUNT];
extern const ui_label_config_t
    ui_more_options_labels[UI_MORE_OPTIONS_LABEL_COUNT];
extern const ui_image_config_t
    ui_add_ingredients_images[UI_ADD_INGREDIENTS_IMAGE_COUNT];
extern const ui_label_config_t
    ui_add_ingredients_labels[UI_ADD_INGREDIENTS_LABEL_COUNT];
extern const ui_image_config_t
    ui_machine_check_images[UI_MACHINE_CHECK_IMAGE_COUNT];
extern const ui_label_config_t
    ui_machine_check_labels[UI_MACHINE_CHECK_LABEL_COUNT];
extern const ui_image_config_t
    ui_press_parts_images[UI_PRESS_PARTS_IMAGE_COUNT];
extern const ui_label_config_t
    ui_press_parts_labels[UI_PRESS_PARTS_LABEL_COUNT];
extern const ui_image_config_t
    ui_tofu_making_images[UI_TOFU_MAKING_IMAGE_COUNT];
extern const ui_label_config_t
    ui_tofu_making_labels[UI_TOFU_MAKING_LABEL_COUNT];
extern const ui_image_config_t
    ui_tofu_ready_images[UI_TOFU_READY_IMAGE_COUNT];
extern const ui_label_config_t
    ui_tofu_ready_labels[UI_TOFU_READY_LABEL_COUNT];
extern const ui_image_config_t
    ui_stop_tofu_images[UI_STOP_TOFU_IMAGE_COUNT];
extern const ui_label_config_t
    ui_stop_tofu_labels[UI_STOP_TOFU_LABEL_COUNT];
extern const ui_image_config_t
    ui_tofu_stopping_images[UI_TOFU_STOPPING_IMAGE_COUNT];
extern const ui_label_config_t
    ui_tofu_stopping_labels[UI_TOFU_STOPPING_LABEL_COUNT];
extern const ui_image_config_t
    ui_tofu_stopped_images[UI_TOFU_STOPPED_IMAGE_COUNT];
extern const ui_label_config_t
    ui_tofu_stopped_labels[UI_TOFU_STOPPED_LABEL_COUNT];

const void *ui_home_active_image_src(uint8_t index, bool focused);
const char *ui_home_active_label_text(uint8_t index);
void ui_tofu_firmness_reset(void);
const void *ui_tofu_firmness_active_image_src(uint8_t index, bool focused);
const char *ui_tofu_firmness_active_label_text(uint8_t index);
const char *ui_tofu_firmness_details_title(void);

void ui_firm_tofu_reset(void);
void ui_firm_tofu_cancel_editing(void);
const void *ui_firm_tofu_active_image_src(uint8_t index, bool focused);
bool ui_firm_tofu_image_hidden(uint8_t index);
void ui_firm_tofu_active_image_pos(uint8_t index, int16_t *x, int16_t *y);
const char *ui_firm_tofu_active_label_text(uint8_t index);
uint32_t ui_firm_tofu_active_label_color(uint8_t index);
bool ui_firm_tofu_label_hidden(uint8_t index);
void ui_firm_tofu_active_label_pos(uint8_t index, int16_t *x, int16_t *y);
void ui_firm_tofu_adjust_amount(int8_t direction);
void ui_firm_tofu_adjust_coagulant(int8_t direction);

void ui_more_options_reset(void);
void ui_more_options_restore_defaults(void);
void ui_more_options_cancel_editing(void);
const void *ui_more_options_active_image_src(uint8_t index, bool focused);
bool ui_more_options_image_hidden(uint8_t index);
void ui_more_options_active_image_pos(uint8_t index, int16_t *x, int16_t *y);
const char *ui_more_options_active_label_text(uint8_t index);
uint32_t ui_more_options_active_label_color(uint8_t index);
const lv_font_t *ui_more_options_active_label_font(uint8_t index);
void ui_more_options_active_label_pos(uint8_t index, int16_t *x, int16_t *y);

const void *ui_add_ingredients_active_image_src(uint8_t index, bool focused);
const char *ui_add_ingredients_active_label_text(uint8_t index);

void ui_machine_check_reset(void);
const void *ui_machine_check_active_image_src(uint8_t index, bool focused);
const char *ui_machine_check_active_label_text(uint8_t index);
uint32_t ui_machine_check_active_label_color(uint8_t index);

void ui_press_parts_check_reset(void);
const void *ui_press_parts_active_image_src(uint8_t index, bool focused);
const char *ui_press_parts_active_label_text(uint8_t index);
uint32_t ui_press_parts_active_label_color(uint8_t index);

const void *ui_tofu_making_active_image_src(uint8_t index, bool focused);
const char *ui_tofu_making_active_label_text(uint8_t index);

const void *ui_tofu_ready_active_image_src(uint8_t index, bool focused);
const char *ui_tofu_ready_active_label_text(uint8_t index);
uint32_t ui_tofu_ready_active_label_color(uint8_t index);

const void *ui_stop_tofu_active_image_src(uint8_t index, bool focused);
const char *ui_stop_tofu_active_label_text(uint8_t index);
uint32_t ui_stop_tofu_active_label_color(uint8_t index);

void ui_tofu_stopping_reset(void);
const void *ui_tofu_stopping_active_image_src(uint8_t index, bool focused);
const char *ui_tofu_stopping_active_label_text(uint8_t index);

const void *ui_tofu_stopped_active_image_src(uint8_t index, bool focused);
const char *ui_tofu_stopped_active_label_text(uint8_t index);

void ui_internal_set_selected_image(ui_page_id_t page, uint8_t index);

#endif
