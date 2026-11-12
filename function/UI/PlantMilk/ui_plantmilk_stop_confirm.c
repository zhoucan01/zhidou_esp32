#include "ui_plantmilk_internal.h"

const ui_image_config_t ui_pm_stop_images[UI_PM_STOP_IMAGE_COUNT] = {
    PM_IMG(ui_img_pm_wait_png, 9, -1),
    PM_IMG_HL(ui_img_pm_1_png, ui_img_pm_2_png, -123, -116),
    PM_IMG_HL(ui_img_pm_1_png, ui_img_pm_2_png, -123, 116),
    PM_IMG(ui_img_pm_stopping_png, 19, -153),
};

const ui_label_config_t ui_pm_stop_labels[UI_PM_STOP_LABEL_COUNT] = {
    PM_LABEL("Stop making milk?", 247, -210, 0xFFFFFF, 24),
    PM_LABEL("This batch will end.", 133, -83, 0xF2BE9F, 22),
    PM_LABEL("Keep Making", -44, -172, 0xFFFFFF, 20),
    PM_LABEL("Stop", -89, 103, 0xFFFFFF, 20),
};

/* 制作页长按和本页停止按钮共用申请；等待停止期间禁止执行清洗。 */
void ui_pm_stop_request(void)
{
    if (ui_pm_flow.stop == NULL) return;
    ui_plantmilk_flow_state_t state = ui_pm_flow.stop();
    if (state == UI_PM_FLOW_STOPPING) ui_show_page(UI_PAGE_PLANTMILK_STOPPING);
    else if (state == UI_PM_FLOW_STOPPED) ui_show_page(UI_PAGE_PLANTMILK_STOPPED);
}

static void short_press(uint8_t selected)
{
    if (selected == UI_PM_STOP_KEEP) ui_show_page(UI_PAGE_PLANTMILK_MAKING);
    else ui_pm_stop_request();
}

static void tick(void)
{
    /* 停止确认弹窗打开期间，制作自然结束也必须进入完成页。 */
    ui_pm_making_check_completion();
}

const ui_pm_page_handlers_t ui_pm_stop_handlers = { short_press, ui_pm_stop_request, tick };
