#ifndef UI_PLANTMILK_INTERNAL_H
#define UI_PLANTMILK_INTERNAL_H

#include "ui_plantmilk.h"
#include "ui_plantmilk_assets.h"
#include "../Tofu/ui_tofu_internal.h"

#define PM_IMG(src, x, y) { &(src), NULL, (x), (y), LV_ALIGN_CENTER }
#define PM_IMG_HL(src, alt, x, y) \
    { &(src), &(alt), (x), (y), LV_ALIGN_CENTER }
#define PM_LABEL(text, x, y, color, size) \
    { (text), (x), (y), LV_ALIGN_CENTER, (color), &UI_FONT_##size, \
      900, 0, 0, false }

#define UI_PM_SELECT_IMAGE_COUNT 7U
#define UI_PM_SELECT_LABEL_COUNT 3U
#define UI_PM_OPTIONS_IMAGE_COUNT 10U
#define UI_PM_OPTIONS_LABEL_COUNT 14U
#define UI_PM_ADD_IMAGE_COUNT 7U
#define UI_PM_ADD_LABEL_COUNT 5U
#define UI_PM_CHECK_IMAGE_COUNT 7U
#define UI_PM_CHECK_LABEL_COUNT 7U
#define UI_PM_MAKING_IMAGE_COUNT 3U
#define UI_PM_MAKING_LABEL_COUNT 4U
#define UI_PM_READY_IMAGE_COUNT 4U
#define UI_PM_READY_LABEL_COUNT 5U
#define UI_PM_NO_FILTER_IMAGE_COUNT 3U
#define UI_PM_NO_FILTER_LABEL_COUNT 4U
#define UI_PM_STOP_IMAGE_COUNT 4U
#define UI_PM_STOP_LABEL_COUNT 4U
#define UI_PM_STOPPING_IMAGE_COUNT 2U
#define UI_PM_STOPPING_LABEL_COUNT 4U
#define UI_PM_STOPPED_IMAGE_COUNT 3U
#define UI_PM_STOPPED_LABEL_COUNT 4U

#define PM_DECLARE(name, images, labels) \
    extern const ui_image_config_t name##_images[images]; \
    extern const ui_label_config_t name##_labels[labels]
PM_DECLARE(ui_pm_select, UI_PM_SELECT_IMAGE_COUNT, UI_PM_SELECT_LABEL_COUNT);
PM_DECLARE(ui_pm_options, UI_PM_OPTIONS_IMAGE_COUNT, UI_PM_OPTIONS_LABEL_COUNT);
PM_DECLARE(ui_pm_add, UI_PM_ADD_IMAGE_COUNT, UI_PM_ADD_LABEL_COUNT);
PM_DECLARE(ui_pm_check, UI_PM_CHECK_IMAGE_COUNT, UI_PM_CHECK_LABEL_COUNT);
PM_DECLARE(ui_pm_making, UI_PM_MAKING_IMAGE_COUNT, UI_PM_MAKING_LABEL_COUNT);
PM_DECLARE(ui_pm_ready, UI_PM_READY_IMAGE_COUNT, UI_PM_READY_LABEL_COUNT);
PM_DECLARE(ui_pm_no_filter, UI_PM_NO_FILTER_IMAGE_COUNT, UI_PM_NO_FILTER_LABEL_COUNT);
PM_DECLARE(ui_pm_stop, UI_PM_STOP_IMAGE_COUNT, UI_PM_STOP_LABEL_COUNT);
PM_DECLARE(ui_pm_stopping, UI_PM_STOPPING_IMAGE_COUNT, UI_PM_STOPPING_LABEL_COUNT);
PM_DECLARE(ui_pm_stopped, UI_PM_STOPPED_IMAGE_COUNT, UI_PM_STOPPED_LABEL_COUNT);
#undef PM_DECLARE

/* 每个逻辑页面在自己的 .c 文件内实现按键和周期处理。 */
typedef struct {
    void (*short_press)(uint8_t selected);
    void (*long_press)(void);
    void (*tick)(void);
} ui_pm_page_handlers_t;

extern const ui_pm_page_handlers_t ui_pm_select_handlers;
extern const ui_pm_page_handlers_t ui_pm_options_handlers;
extern const ui_pm_page_handlers_t ui_pm_add_handlers;
extern const ui_pm_page_handlers_t ui_pm_check_handlers;
extern const ui_pm_page_handlers_t ui_pm_making_handlers;
extern const ui_pm_page_handlers_t ui_pm_ready_handlers;
extern const ui_pm_page_handlers_t ui_pm_no_filter_handlers;
extern const ui_pm_page_handlers_t ui_pm_stop_handlers;
extern const ui_pm_page_handlers_t ui_pm_stopping_handlers;
extern const ui_pm_page_handlers_t ui_pm_stopped_handlers;

/* 公共任务接口由 myLvgl 组件注册；未注册时不执行硬件操作。 */
extern ui_plantmilk_flow_ops_t ui_pm_flow;
bool ui_pm_making_check_completion(void);
void ui_pm_stop_request(void);

const void *ui_plantmilk_active_image_src(ui_page_id_t page, uint8_t index, bool focused);
bool ui_plantmilk_image_hidden(ui_page_id_t page, uint8_t index);
void ui_plantmilk_active_image_pos(ui_page_id_t page, uint8_t index, int16_t *x, int16_t *y);
const char *ui_plantmilk_active_label_text(ui_page_id_t page, uint8_t index);
bool ui_plantmilk_label_hidden(ui_page_id_t page, uint8_t index);
uint32_t ui_plantmilk_active_label_color(ui_page_id_t page, uint8_t index);
void ui_plantmilk_active_label_pos(ui_page_id_t page, uint8_t index,
                                  const char *text, const lv_font_t *font,
                                  int16_t *x, int16_t *y);

#endif
