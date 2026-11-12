#include "ui_plantmilk_internal.h"
#include "../Clean/ui_clean.h"

const ui_image_config_t ui_pm_no_filter_images[UI_PM_NO_FILTER_IMAGE_COUNT] = {
    PM_IMG(ui_img_pm_wait_png, 10, -3),
    PM_IMG_HL(ui_img_pm_normal_png, ui_img_pm_high_png, -119, -4),
    PM_IMG(ui_img_pm_making_png, 22, -163),
};

const ui_label_config_t ui_pm_no_filter_labels[UI_PM_NO_FILTER_LABEL_COUNT] = {
    PM_LABEL("Milk is ready", 213, -210, 0xFFFFFF, 24),
    PM_LABEL("Take your cup", 123, -80, 0xFFFFFF, 22),
    PM_LABEL("Caution contents may be hot", 111, -80, 0xF2BE9F, 14),
    PM_LABEL("Clean", -80, -17, 0xFFFFFF, 20),
};

/* 无滤网完成或收集豆渣后，先进入清洗准备页。 */
static void short_press(uint8_t selected)
{
    (void)selected;
    ui_clean_prepare(UI_CLEAN_DEEP);
}

const ui_pm_page_handlers_t ui_pm_no_filter_handlers = { short_press, NULL, NULL };
