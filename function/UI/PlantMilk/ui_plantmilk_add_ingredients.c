#include "ui_plantmilk_internal.h"

/* 屏幕旋转后，竖屏 X 坐标对应视觉上的高度；左右卡片及内部元素共用高度基准。 */
#define PM_ADD_CARD_X 18

const ui_image_config_t ui_pm_add_images[UI_PM_ADD_IMAGE_COUNT] = {
    PM_IMG_HL(ui_img_pm_back_png, ui_img_pm_back_1_png, 125, 188),
    PM_IMG(ui_img_pm_3_png, PM_ADD_CARD_X, -113),
    PM_IMG(ui_img_pm_filter_2_png, PM_ADD_CARD_X + 30, -171),
    PM_IMG(ui_img_pm_3_png, PM_ADD_CARD_X, 115),
    PM_IMG(ui_img_pm_filter_png, PM_ADD_CARD_X + 30, 57),
    PM_IMG(ui_img_pm_progress_png, 103, 0),
    PM_IMG_HL(ui_img_pm_normal_png, ui_img_pm_high_png, -123, -1),
};

const ui_label_config_t ui_pm_add_labels[UI_PM_ADD_LABEL_COUNT] = {
    PM_LABEL("Oat Milk", 190, -209, 0xFFFFFF, 24),
    PM_LABEL("Fit filter first", PM_ADD_CARD_X + 58, -176, 0xFFFFFF, 20),
    PM_LABEL("Add instant oats", PM_ADD_CARD_X + 78, 46, 0xFFFFFF, 20),
    PM_LABEL("Suggested: -scoops (-g)", 30, -211, 0xFFFFFF, 16),
    PM_LABEL("Continue", -65, -32, 0xFFFFFF, 20),
};

/* 返回参数页，或读取传感器后进入机器检查页。 */
static void short_press(uint8_t selected)
{
    if (selected == UI_PM_STEP_BACK) ui_show_page(UI_PAGE_PLANTMILK_OPTIONS);
    else {
        if (ui_pm_flow.refresh_sensors != NULL) ui_pm_flow.refresh_sensors();
        ui_show_page(UI_PAGE_PLANTMILK_MACHINE_CHECK);
    }
}

const ui_pm_page_handlers_t ui_pm_add_handlers = { short_press, NULL, NULL };
