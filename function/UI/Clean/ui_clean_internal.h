#ifndef UI_CLEAN_INTERNAL_H
#define UI_CLEAN_INTERNAL_H

#include "ui_clean.h"
#include "ui_clean_assets.h"
#include "../Tofu/ui_tofu_internal.h"

#define CL_IMG(src, x, y) { &(src), NULL, (x), (y), LV_ALIGN_CENTER }
#define CL_IMG_HL(src, alt, x, y) \
    { &(src), &(alt), (x), (y), LV_ALIGN_CENTER }
#define CL_LABEL(text, x, y, color, size) \
    { (text), (x), (y), LV_ALIGN_CENTER, (color), &UI_FONT_##size, \
      900, 0, 0, false }

#define UI_CL_SELECT_IMAGE_COUNT 4U
#define UI_CL_SELECT_LABEL_COUNT 3U
#define UI_CL_RINSE_PREP_IMAGE_COUNT 8U
#define UI_CL_RINSE_PREP_LABEL_COUNT 5U
#define UI_CL_DEEP_PREP_IMAGE_COUNT 9U
#define UI_CL_DEEP_PREP_LABEL_COUNT 5U
#define UI_CL_RUNNING_IMAGE_COUNT 3U
#define UI_CL_RUNNING_LABEL_COUNT 4U
#define UI_CL_COMPLETE_IMAGE_COUNT 3U
#define UI_CL_COMPLETE_LABEL_COUNT 3U
#define UI_CL_STOP_IMAGE_COUNT 3U
#define UI_CL_STOP_LABEL_COUNT 4U
#define UI_CL_STOPPING_IMAGE_COUNT 3U
#define UI_CL_STOPPING_LABEL_COUNT 4U

extern const ui_image_config_t ui_cl_select_images[UI_CL_SELECT_IMAGE_COUNT];
extern const ui_label_config_t ui_cl_select_labels[UI_CL_SELECT_LABEL_COUNT];
extern const ui_image_config_t ui_cl_rinse_prep_images[UI_CL_RINSE_PREP_IMAGE_COUNT];
extern const ui_label_config_t ui_cl_rinse_prep_labels[UI_CL_RINSE_PREP_LABEL_COUNT];
extern const ui_image_config_t ui_cl_deep_prep_images[UI_CL_DEEP_PREP_IMAGE_COUNT];
extern const ui_label_config_t ui_cl_deep_prep_labels[UI_CL_DEEP_PREP_LABEL_COUNT];
extern const ui_image_config_t ui_cl_running_images[UI_CL_RUNNING_IMAGE_COUNT];
extern const ui_label_config_t ui_cl_running_labels[UI_CL_RUNNING_LABEL_COUNT];
extern const ui_image_config_t ui_cl_complete_images[UI_CL_COMPLETE_IMAGE_COUNT];
extern const ui_label_config_t ui_cl_complete_labels[UI_CL_COMPLETE_LABEL_COUNT];
extern const ui_image_config_t ui_cl_stop_images[UI_CL_STOP_IMAGE_COUNT];
extern const ui_label_config_t ui_cl_stop_labels[UI_CL_STOP_LABEL_COUNT];
extern const ui_image_config_t ui_cl_stopping_images[UI_CL_STOPPING_IMAGE_COUNT];
extern const ui_label_config_t ui_cl_stopping_labels[UI_CL_STOPPING_LABEL_COUNT];

const void *ui_clean_active_image_src(ui_page_id_t page, uint8_t index,
                                      bool focused);
int16_t ui_clean_active_image_angle(ui_page_id_t page, uint8_t index);
const char *ui_clean_active_label_text(ui_page_id_t page, uint8_t index);
uint32_t ui_clean_active_label_color(ui_page_id_t page, uint8_t index);
void ui_clean_active_label_pos(ui_page_id_t page, uint8_t index,
                               const char *text, const lv_font_t *font,
                               int16_t *x, int16_t *y);

#endif
