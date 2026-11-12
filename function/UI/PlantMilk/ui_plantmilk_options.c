#include "ui_plantmilk_internal.h"

/* 每张卡片复用一个图片槽位，仅正在编辑的卡片显示左右箭头。 */
const ui_image_config_t ui_pm_options_images[UI_PM_OPTIONS_IMAGE_COUNT] = {
    PM_IMG_HL(ui_img_pm_back_png, ui_img_pm_back_1_png, 123, 180),
    PM_IMG_HL(ui_img_pm_plantmilk_card_png, ui_img_pm_plantmilk_card_1_png, 55, -151),
    PM_IMG_HL(ui_img_pm_plantmilk_card_png, ui_img_pm_plantmilk_card_1_png, 55, -1),
    PM_IMG_HL(ui_img_pm_plantmilk_card_png, ui_img_pm_plantmilk_card_1_png, 55, 149),
    PM_IMG_HL(ui_img_pm_plantmilk_volume_usefilter_png, ui_img_pm_plantmilk_volume_usefilter_1_png, -30, -113),
    PM_IMG_HL(ui_img_pm_plantmilk_volume_usefilter_png, ui_img_pm_plantmilk_volume_usefilter_1_png, -30, 112),
    PM_IMG_HL(ui_img_pm_1_png, ui_img_pm_2_png, -123, -111),
    PM_IMG_HL(ui_img_pm_1_png, ui_img_pm_2_png, -123, 111),
    PM_IMG(ui_img_pm_previous_png, 39, -194),
    PM_IMG(ui_img_pm_next_png, 39, -108),
};

const ui_label_config_t ui_pm_options_labels[UI_PM_OPTIONS_LABEL_COUNT] = {
    PM_LABEL("Oat Milk", 190, -208, 0xFFFFFF, 24),
    PM_LABEL("Blending Time", 139, -200, 0xFFFFFF, 16),
    PM_LABEL("00:30", 82, -197, 0xFFFFFF, 22),
    PM_LABEL("Blade Speed", 131, -43, 0xFFFFFF, 16),
    PM_LABEL("3", 56, -39, 0xFFFFFF, 22),
    PM_LABEL("Cook", 99, 102, 0xFFFFFF, 16),
    PM_LABEL("On", 69, 106, 0xFFFFFF, 22),
    PM_LABEL("Volume", 23, -200, 0xFFFFFF, 16),
    PM_LABEL("1200", -8, -197, 0xFFFFFF, 22),
    PM_LABEL("Use Filter", 30, 30, 0xFFFFFF, 16),
    PM_LABEL("On", -16, 37, 0xFFFFFF, 22),
    PM_LABEL("Est. 14 min", -33, -210, 0xFFFFFF, 16),
    PM_LABEL("Restore defaults", -28, -184, 0xFFFFFF, 20),
    PM_LABEL("Continue", -66, 71, 0xFFFFFF, 20),
};

/* 返回配方、恢复默认、进入加料页，其余参数项进入或退出编辑。 */
static void short_press(uint8_t selected)
{
    if (selected == UI_PM_OPTIONS_BACK) ui_plantmilk_select_enter();
    else if (selected == UI_PM_OPTIONS_RESTORE) ui_plantmilk_restore_defaults();
    else if (selected == UI_PM_OPTIONS_CONTINUE) ui_show_page(UI_PAGE_PLANTMILK_ADD_INGREDIENTS);
    else ui_plantmilk_toggle_editing();
}

const ui_pm_page_handlers_t ui_pm_options_handlers = { short_press, NULL, NULL };
