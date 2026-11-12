#include "ui_plantmilk_internal.h"

/* Screen9 的前半逻辑页：不显示沙漏，Clean 禁用。 */
const ui_image_config_t ui_pm_stopping_images[UI_PM_STOPPING_IMAGE_COUNT] = {
    PM_IMG(ui_img_pm_wait_png, 8, -1),
    PM_IMG(ui_img_grey_png, -119, -4),
};

const ui_label_config_t ui_pm_stopping_labels[UI_PM_STOPPING_LABEL_COUNT] = {
    PM_LABEL("Stopping...", 206, -211, 0xFFFFFF, 24),
    PM_LABEL("This batch is unfinished.", 175, -164, 0xF2BE9F, 22),
    PM_LABEL("Please wait. Keep parts in place.", 148, -162, 0xFFFFFF, 18),
    PM_LABEL("Clean", -79, -20, 0x777671, 20),
};

/* 仅任务真正停止或验证等待结束后进入已停止页；本页不响应按键。 */
static void tick(void)
{
    if (ui_pm_flow.poll != NULL && ui_pm_flow.poll() == UI_PM_FLOW_STOPPED)
        ui_show_page(UI_PAGE_PLANTMILK_STOPPED);
}

const ui_pm_page_handlers_t ui_pm_stopping_handlers = { NULL, NULL, tick };
