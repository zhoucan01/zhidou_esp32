#include "ui_plantmilk_internal.h"

const ui_image_config_t ui_pm_check_images[UI_PM_CHECK_IMAGE_COUNT] = {
    PM_IMG_HL(ui_img_pm_back_png, ui_img_pm_back_1_png, 125, 188),
    PM_IMG(ui_img_pm_3_png, 18, -113),
    PM_IMG(ui_img_pm_174602179, 29, -41),
    PM_IMG(ui_img_pm_3_png, 18, 116),
    PM_IMG(ui_img_pm_1205051221, 29, 188),
    PM_IMG(ui_img_pm_progress_2_png, 99, 1),
    PM_IMG_HL(ui_img_grey_png, ui_img_pm_high_png, -120, 1),
};

const ui_label_config_t ui_pm_check_labels[UI_PM_CHECK_LABEL_COUNT] = {
    PM_LABEL("Oat Milk", 190, -209, 0xFFFFFF, 24),
    PM_LABEL("Lid", 62, -185, 0xFFFFFF, 22),
    PM_LABEL("Close lid", 52, -185, 0xF2BE9F, 16),
    PM_LABEL("Water tank", 112, 40, 0xFFFFFF, 22),
    PM_LABEL("Fit tank", 51, 41, 0xF2BE9F, 16),
    PM_LABEL("Use a cup large enough for this batch (1200ml)", 126, -217, 0xF2BE9F, 16),
    PM_LABEL("Start Making", -45, -45, 0xFFFFFF, 20),
};

static void refresh_sensors(void)
{
    if (ui_pm_flow.refresh_sensors != NULL) ui_pm_flow.refresh_sensors();
}

/* 检查通过且任务层接受启动后，才进入制作页。 */
static void short_press(uint8_t selected)
{
    if (selected == UI_PM_STEP_BACK) {
        ui_show_page(UI_PAGE_PLANTMILK_ADD_INGREDIENTS);
        return;
    }
    refresh_sensors();
    if (!ui_plantmilk_can_start() || ui_pm_flow.start == NULL || !ui_pm_flow.start()) return;
    ui_plantmilk_set_remaining_minutes(14);
    ui_show_page(UI_PAGE_PLANTMILK_MAKING);
}

const ui_pm_page_handlers_t ui_pm_check_handlers = { short_press, NULL, refresh_sensors };
