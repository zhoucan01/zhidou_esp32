#include "ui_plantmilk_internal.h"
#include "../Clean/ui_clean.h"

/* Screen9 的后半逻辑页：显示沙漏，允许执行 Clean。 */
const ui_image_config_t ui_pm_stopped_images[UI_PM_STOPPED_IMAGE_COUNT] = {
    PM_IMG(ui_img_pm_wait_png, 5, -1),
    PM_IMG_HL(ui_img_pm_normal_png, ui_img_pm_high_png, -121, 0),
    PM_IMG(ui_img_pm_stopping_png, 14, -165),
};

const ui_label_config_t ui_pm_stopped_labels[UI_PM_STOPPED_LABEL_COUNT] = {
    PM_LABEL("Making stopped", 235, -210, 0xFFFFFF, 24),
    PM_LABEL("This batch is unfinished.", 177, -99, 0xF2BE9F, 22),
    PM_LABEL("Clean before making again.", 129, -98, 0xFFFFFF, 18),
    PM_LABEL("Clean", -82, -12, 0xFFFFFF, 20),
};

/* 停止后先进入清洗准备页，不直接启动硬件任务。 */
static void short_press(uint8_t selected)
{
    (void)selected;
    ui_clean_prepare(UI_CLEAN_DEEP);
}

const ui_pm_page_handlers_t ui_pm_stopped_handlers = { short_press, NULL, NULL };
