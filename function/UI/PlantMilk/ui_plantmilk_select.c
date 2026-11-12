#include "ui_plantmilk_internal.h"

/* Screen1 配方选择页，从现有首页的 Plant Milk 入口进入。 */
const ui_image_config_t ui_pm_select_images[UI_PM_SELECT_IMAGE_COUNT] = {
    PM_IMG(ui_img_pm_homepage_png, 0, 0),
    PM_IMG(ui_img_pm_ring3_png, 0, 0),
    PM_IMG_HL(ui_img_pm_oat_png, ui_img_pm_oat_1_png, 72, 131),
    PM_IMG_HL(ui_img_pm_nut_png, ui_img_pm_nut_1_png, -72, 131),
    PM_IMG_HL(ui_img_pm_soy_png, ui_img_pm_soy_1_png, -72, -131),
    PM_IMG_HL(ui_img_pm_custom_png, ui_img_pm_custom_1_png, 72, -131),
    PM_IMG_HL(ui_img_pm_back_png, ui_img_pm_back_1_png, 123, 180),
};

const ui_label_config_t ui_pm_select_labels[UI_PM_SELECT_LABEL_COUNT] = {
    PM_LABEL("Plant Milk", 204, -214, 0xFFFFFF, 24),
    PM_LABEL("Oat\nMilk", 58, 6, 0xF2BE9F, 26),
    PM_LABEL("Smooth and creamy", -39, -73, 0xFFFFFF, 16),
};

/* 返回配方选择页时退出编辑，保留当前配方及其参数。 */
void ui_plantmilk_select_enter(void)
{
    ui_plantmilk_cancel_editing();
    ui_plantmilk_select_recipe(ui_plantmilk_get_recipe());
    ui_show_page(UI_PAGE_PLANTMILK_SELECT);
}

static void short_press(uint8_t selected)
{
    if (selected == UI_PM_SELECT_BACK) ui_show_page(UI_PAGE_HOME);
    else {
        /* 进入参数页后默认选中 Volume。 */
        ui_select_image(UI_PAGE_PLANTMILK_OPTIONS, UI_PM_OPTIONS_VOLUME);
        ui_show_page(UI_PAGE_PLANTMILK_OPTIONS);
    }
}

const ui_pm_page_handlers_t ui_pm_select_handlers = { short_press, NULL, NULL };
