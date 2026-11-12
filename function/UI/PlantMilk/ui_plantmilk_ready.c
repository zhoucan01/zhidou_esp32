#include "ui_plantmilk_internal.h"
#include "../Clean/ui_clean.h"

const ui_image_config_t ui_pm_ready_images[UI_PM_READY_IMAGE_COUNT] = {
    PM_IMG(ui_img_pm_wait_png, 9, 1),
    PM_IMG_HL(ui_img_pm_1_png, ui_img_pm_2_png, -122, -113),
    PM_IMG_HL(ui_img_pm_1_png, ui_img_pm_2_png, -121, 116),
    PM_IMG(ui_img_pm_making_png, 17, -165),
};

const ui_label_config_t ui_pm_ready_labels[UI_PM_READY_LABEL_COUNT] = {
    PM_LABEL("Milk is ready", 213, -210, 0xFFFFFF, 24),
    PM_LABEL("Take your cup", 122, -76, 0xFFFFFF, 22),
    PM_LABEL("Caution contents may be hot", 110, -76, 0xF2BE9F, 14),
    PM_LABEL("Collect Pulp", -50, -165, 0xFFFFFF, 20),
    PM_LABEL("Clean", -82, 97, 0xFFFFFF, 20),
};

static void short_press(uint8_t selected)
{
    if (selected == UI_PM_READY_COLLECT) {
        /* 现有框架没有收集豆渣的执行器接口，此处仅切换到收集后的清洗页。 */
        ui_show_page(UI_PAGE_PLANTMILK_READY_NO_FILTER);
    } else {
        ui_clean_prepare(UI_CLEAN_DEEP);
    }
}

const ui_pm_page_handlers_t ui_pm_ready_handlers = { short_press, NULL, NULL };
