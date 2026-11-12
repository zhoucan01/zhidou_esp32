#include "ui_plantmilk_internal.h"

const ui_image_config_t ui_pm_making_images[UI_PM_MAKING_IMAGE_COUNT] = {
    PM_IMG_HL(ui_img_pm_normal_png, ui_img_pm_high_png, -119, -4),
    PM_IMG(ui_img_pm_wait_png, 7, -4),
    PM_IMG(ui_img_pm_making_png, 15, -170),
};

const ui_label_config_t ui_pm_making_labels[UI_PM_MAKING_LABEL_COUNT] = {
    PM_LABEL("Making Plant Milk", 251, -215, 0xFFFFFF, 24),
    PM_LABEL("1200ml", 89, -102, 0xFFFFFF, 22),
    PM_LABEL("About 14 min left", 137, -103, 0xF2BE9F, 28),
    PM_LABEL("Stop", -84, -10, 0xFFFFFF, 20),
};

/* 制作页和停止确认页共用完成判断，按滤网参数进入对应完成页。 */
bool ui_pm_making_check_completion(void)
{
    if (ui_pm_flow.poll == NULL) return false;
    ui_plantmilk_flow_state_t state = ui_pm_flow.poll();
    if (state == UI_PM_FLOW_READY) {
        ui_show_page(ui_plantmilk_get_options()->use_filter ? UI_PAGE_PLANTMILK_READY
                                                          : UI_PAGE_PLANTMILK_READY_NO_FILTER);
        return true;
    }
    if (state == UI_PM_FLOW_RUNNING && ui_get_current_page() == UI_PAGE_PLANTMILK_MAKING &&
        ui_pm_flow.remaining_minutes != NULL)
        ui_plantmilk_set_remaining_minutes(ui_pm_flow.remaining_minutes());
    return false;
}

static void tick(void)
{
    ui_pm_making_check_completion();
}

/* 短按打开停止确认页；长按按原有逻辑直接申请停止。 */
static void short_press(uint8_t selected)
{
    (void)selected;
    ui_select_image(UI_PAGE_PLANTMILK_STOP_CONFIRM, UI_PM_STOP_KEEP);
    ui_show_page(UI_PAGE_PLANTMILK_STOP_CONFIRM);
}

const ui_pm_page_handlers_t ui_pm_making_handlers = { short_press, ui_pm_stop_request, tick };
