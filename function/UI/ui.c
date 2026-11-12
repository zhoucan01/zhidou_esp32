/* 所有逻辑功能共用一个 LVGL 屏幕和一个对象池。 */
#include "ui.h"
#include "Tofu/ui_tofu_internal.h"
#include "PlantMilk/ui_plantmilk_internal.h"
#include "Clean/ui_clean_internal.h"
#include "ui_lz4_decoder.h"
#include <stdio.h>
#include <string.h>

#define PAGE(images, image_count, labels, label_count) \
    { (images), (image_count), (labels), (label_count) }

const ui_page_config_t ui_page_configs[UI_PAGE_COUNT] = {
    [UI_PAGE_HOME] = PAGE(ui_home_images, UI_HOME_IMAGE_COUNT,
                          ui_home_labels, UI_HOME_LABEL_COUNT),
    [UI_PAGE_TOFU_FIRMNESS] =
        PAGE(ui_tofu_firmness_images, UI_TOFU_FIRMNESS_IMAGE_COUNT,
             ui_tofu_firmness_labels, UI_TOFU_FIRMNESS_LABEL_COUNT),
    [UI_PAGE_FIRM_TOFU] = PAGE(ui_firm_tofu_images,
                               UI_FIRM_TOFU_IMAGE_COUNT,
                               ui_firm_tofu_labels,
                               UI_FIRM_TOFU_LABEL_COUNT),
    [UI_PAGE_MORE_OPTIONS] =
        PAGE(ui_more_options_images, UI_MORE_OPTIONS_IMAGE_COUNT,
             ui_more_options_labels, UI_MORE_OPTIONS_LABEL_COUNT),
    [UI_PAGE_ADD_INGREDIENTS] =
        PAGE(ui_add_ingredients_images, UI_ADD_INGREDIENTS_IMAGE_COUNT,
             ui_add_ingredients_labels, UI_ADD_INGREDIENTS_LABEL_COUNT),
    [UI_PAGE_MACHINE_CHECK] =
        PAGE(ui_machine_check_images, UI_MACHINE_CHECK_IMAGE_COUNT,
             ui_machine_check_labels, UI_MACHINE_CHECK_LABEL_COUNT),
    [UI_PAGE_PRESS_PARTS_CHECK] =
        PAGE(ui_press_parts_images, UI_PRESS_PARTS_IMAGE_COUNT,
             ui_press_parts_labels, UI_PRESS_PARTS_LABEL_COUNT),
    [UI_PAGE_TOFU_MAKING] =
        PAGE(ui_tofu_making_images, UI_TOFU_MAKING_IMAGE_COUNT,
             ui_tofu_making_labels, UI_TOFU_MAKING_LABEL_COUNT),
    [UI_PAGE_TOFU_READY] =
        PAGE(ui_tofu_ready_images, UI_TOFU_READY_IMAGE_COUNT,
             ui_tofu_ready_labels, UI_TOFU_READY_LABEL_COUNT),
    [UI_PAGE_STOP_TOFU_CONFIRM] =
        PAGE(ui_stop_tofu_images, UI_STOP_TOFU_IMAGE_COUNT,
             ui_stop_tofu_labels, UI_STOP_TOFU_LABEL_COUNT),
    [UI_PAGE_TOFU_STOPPING] =
        PAGE(ui_tofu_stopping_images, UI_TOFU_STOPPING_IMAGE_COUNT,
             ui_tofu_stopping_labels, UI_TOFU_STOPPING_LABEL_COUNT),
    [UI_PAGE_TOFU_STOPPED] =
        PAGE(ui_tofu_stopped_images, UI_TOFU_STOPPED_IMAGE_COUNT,
             ui_tofu_stopped_labels, UI_TOFU_STOPPED_LABEL_COUNT),
    [UI_PAGE_PLANTMILK_SELECT] = PAGE(ui_pm_select_images, UI_PM_SELECT_IMAGE_COUNT, ui_pm_select_labels, UI_PM_SELECT_LABEL_COUNT),
    [UI_PAGE_PLANTMILK_OPTIONS] = PAGE(ui_pm_options_images, UI_PM_OPTIONS_IMAGE_COUNT, ui_pm_options_labels, UI_PM_OPTIONS_LABEL_COUNT),
    [UI_PAGE_PLANTMILK_ADD_INGREDIENTS] = PAGE(ui_pm_add_images, UI_PM_ADD_IMAGE_COUNT, ui_pm_add_labels, UI_PM_ADD_LABEL_COUNT),
    [UI_PAGE_PLANTMILK_MACHINE_CHECK] = PAGE(ui_pm_check_images, UI_PM_CHECK_IMAGE_COUNT, ui_pm_check_labels, UI_PM_CHECK_LABEL_COUNT),
    [UI_PAGE_PLANTMILK_MAKING] = PAGE(ui_pm_making_images, UI_PM_MAKING_IMAGE_COUNT, ui_pm_making_labels, UI_PM_MAKING_LABEL_COUNT),
    [UI_PAGE_PLANTMILK_READY] = PAGE(ui_pm_ready_images, UI_PM_READY_IMAGE_COUNT, ui_pm_ready_labels, UI_PM_READY_LABEL_COUNT),
    [UI_PAGE_PLANTMILK_READY_NO_FILTER] = PAGE(ui_pm_no_filter_images, UI_PM_NO_FILTER_IMAGE_COUNT, ui_pm_no_filter_labels, UI_PM_NO_FILTER_LABEL_COUNT),
    [UI_PAGE_PLANTMILK_STOP_CONFIRM] = PAGE(ui_pm_stop_images, UI_PM_STOP_IMAGE_COUNT, ui_pm_stop_labels, UI_PM_STOP_LABEL_COUNT),
    [UI_PAGE_PLANTMILK_STOPPING] = PAGE(ui_pm_stopping_images, UI_PM_STOPPING_IMAGE_COUNT, ui_pm_stopping_labels, UI_PM_STOPPING_LABEL_COUNT),
    [UI_PAGE_PLANTMILK_STOPPED] = PAGE(ui_pm_stopped_images, UI_PM_STOPPED_IMAGE_COUNT, ui_pm_stopped_labels, UI_PM_STOPPED_LABEL_COUNT),
    [UI_PAGE_CLEAN_SELECT] = PAGE(ui_cl_select_images, UI_CL_SELECT_IMAGE_COUNT, ui_cl_select_labels, UI_CL_SELECT_LABEL_COUNT),
    [UI_PAGE_CLEAN_PREPARE_RINSE] = PAGE(ui_cl_rinse_prep_images, UI_CL_RINSE_PREP_IMAGE_COUNT, ui_cl_rinse_prep_labels, UI_CL_RINSE_PREP_LABEL_COUNT),
    [UI_PAGE_CLEAN_PREPARE_CLEAN] = PAGE(ui_cl_deep_prep_images, UI_CL_DEEP_PREP_IMAGE_COUNT, ui_cl_deep_prep_labels, UI_CL_DEEP_PREP_LABEL_COUNT),
    [UI_PAGE_CLEAN_RINSING] = PAGE(ui_cl_running_images, UI_CL_RUNNING_IMAGE_COUNT, ui_cl_running_labels, UI_CL_RUNNING_LABEL_COUNT),
    [UI_PAGE_CLEAN_CLEANING] = PAGE(ui_cl_running_images, UI_CL_RUNNING_IMAGE_COUNT, ui_cl_running_labels, UI_CL_RUNNING_LABEL_COUNT),
    [UI_PAGE_CLEAN_COMPLETE] = PAGE(ui_cl_complete_images, UI_CL_COMPLETE_IMAGE_COUNT, ui_cl_complete_labels, UI_CL_COMPLETE_LABEL_COUNT),
    [UI_PAGE_CLEAN_STOP_CONFIRM] = PAGE(ui_cl_stop_images, UI_CL_STOP_IMAGE_COUNT, ui_cl_stop_labels, UI_CL_STOP_LABEL_COUNT),
    [UI_PAGE_CLEAN_STOPPING] = PAGE(ui_cl_stopping_images, UI_CL_STOPPING_IMAGE_COUNT, ui_cl_stopping_labels, UI_CL_STOPPING_LABEL_COUNT),
};

lv_obj_t *ui_Screen;

static lv_obj_t *image_slots[UI_MAX_PAGE_IMAGES];
static lv_obj_t *label_slots[UI_MAX_PAGE_LABELS];
static uint8_t image_slot_count;
static uint8_t label_slot_count;
static ui_page_id_t current_page;
static bool initialized;
static uint8_t selected_image[UI_PAGE_COUNT];
static const void *override_src[UI_PAGE_COUNT][UI_MAX_PAGE_IMAGES];
static const char *override_label_text[UI_PAGE_COUNT][UI_MAX_PAGE_LABELS];
static int16_t override_label_x[UI_PAGE_COUNT][UI_MAX_PAGE_LABELS];
static int16_t override_label_y[UI_PAGE_COUNT][UI_MAX_PAGE_LABELS];
static bool override_label_pos_valid[UI_PAGE_COUNT][UI_MAX_PAGE_LABELS];

/**
 * @brief 根据传感器检查结果修正目标页面的选中控件。
 * @param page 目标逻辑页面。
 * @param requested 原本希望选中的图片或控件索引。
 * @return 检查未通过时返回该页的返回按钮索引，否则保留 requested。
 * @note 页面进入、传感器重绘和旋钮选择共用此规则，避免禁用按钮占据焦点。
 */
static uint8_t sensor_checked_selection(ui_page_id_t page, uint8_t requested)
{
    switch (page) {
        case UI_PAGE_MACHINE_CHECK:
            if (!ui_machine_check_can_continue()) return UI_MACHINE_CHECK_CONTROL_BACK;
            break;
        case UI_PAGE_PRESS_PARTS_CHECK:
            if (!ui_press_parts_check_can_start()) return UI_PRESS_PARTS_CONTROL_BACK;
            break;
        case UI_PAGE_PLANTMILK_MACHINE_CHECK:
            if (!ui_plantmilk_can_start()) return UI_PM_STEP_BACK;
            break;
        case UI_PAGE_CLEAN_PREPARE_RINSE:
        case UI_PAGE_CLEAN_PREPARE_CLEAN:
            if (!ui_clean_can_start()) return UI_CLEAN_PREP_BACK;
            break;
        default:
            break;
    }
    return requested;
}

/**
 * @brief 直接保存指定页面的选中图片索引，供页面内部逻辑和初始化使用。
 * @param page 目标逻辑页面，无效页面编号会被忽略。
 * @param index 图片索引，或 UI_IMAGE_INDEX_NONE 表示无选中项。
 * @note 此函数不校验图片索引、不执行传感器检查，也不触发页面重绘。
 */
void ui_internal_set_selected_image(ui_page_id_t page, uint8_t index)
{
    if (page < UI_PAGE_COUNT) {
        selected_image[page] = index;
    }
}

/**
 * @brief 获取图片槽位当前应显示的资源，优先使用外部覆盖资源。
 * @param page 有效的逻辑页面编号。
 * @param index 该页面内有效的图片索引。
 * @return 覆盖资源或页面根据选中状态提供的资源；无匹配页面时返回 NULL。
 */
static const void *active_image_src(ui_page_id_t page, uint8_t index)
{
    if (override_src[page][index] != NULL) {
        return override_src[page][index];
    }

    bool focused = selected_image[page] == index;
    if (ui_plantmilk_is_page(page)) {
        return ui_plantmilk_active_image_src(page, index, focused);
    }
    if (ui_clean_is_page(page)) {
        return ui_clean_active_image_src(page, index, focused);
    }
    switch (page) {
        case UI_PAGE_HOME:
            return ui_home_active_image_src(index, focused);
        case UI_PAGE_TOFU_FIRMNESS:
            return ui_tofu_firmness_active_image_src(index, focused);
        case UI_PAGE_FIRM_TOFU:
            return ui_firm_tofu_active_image_src(index, focused);
        case UI_PAGE_MORE_OPTIONS:
            return ui_more_options_active_image_src(index, focused);
        case UI_PAGE_ADD_INGREDIENTS:
            return ui_add_ingredients_active_image_src(index, focused);
        case UI_PAGE_MACHINE_CHECK:
            return ui_machine_check_active_image_src(index, focused);
        case UI_PAGE_PRESS_PARTS_CHECK:
            return ui_press_parts_active_image_src(index, focused);
        case UI_PAGE_TOFU_MAKING:
            return ui_tofu_making_active_image_src(index, focused);
        case UI_PAGE_TOFU_READY:
            return ui_tofu_ready_active_image_src(index, focused);
        case UI_PAGE_STOP_TOFU_CONFIRM:
            return ui_stop_tofu_active_image_src(index, focused);
        case UI_PAGE_TOFU_STOPPING:
            return ui_tofu_stopping_active_image_src(index, focused);
        case UI_PAGE_TOFU_STOPPED:
            return ui_tofu_stopped_active_image_src(index, focused);
        default:
            return NULL;
    }
}

/**
 * @brief 查询图片在当前页面状态下是否需要隐藏。
 * @param page 有效的逻辑页面编号。
 * @param index 该页面内有效的图片索引。
 * @return true 表示隐藏；未定义动态隐藏规则的页面默认显示图片。
 */
static bool active_image_hidden(ui_page_id_t page, uint8_t index)
{
    if (ui_plantmilk_is_page(page)) return ui_plantmilk_image_hidden(page, index);
    if (page == UI_PAGE_FIRM_TOFU) {
        return ui_firm_tofu_image_hidden(index);
    }
    if (page == UI_PAGE_MORE_OPTIONS) {
        return ui_more_options_image_hidden(index);
    }
    return false;
}

/**
 * @brief 获取文字槽位当前应显示的内容，优先使用外部覆盖文字。
 * @param page 有效的逻辑页面编号。
 * @param index 该页面内有效的文字索引。
 * @return 覆盖文字或页面提供的动态文字；无匹配页面时返回 NULL。
 */
static const char *active_label_text(ui_page_id_t page, uint8_t index)
{
    if (override_label_text[page][index] != NULL) {
        return override_label_text[page][index];
    }

    if (ui_plantmilk_is_page(page)) return ui_plantmilk_active_label_text(page, index);
    if (ui_clean_is_page(page)) return ui_clean_active_label_text(page, index);

    switch (page) {
        case UI_PAGE_HOME:
            return ui_home_active_label_text(index);
        case UI_PAGE_TOFU_FIRMNESS:
            return ui_tofu_firmness_active_label_text(index);
        case UI_PAGE_FIRM_TOFU:
            return ui_firm_tofu_active_label_text(index);
        case UI_PAGE_MORE_OPTIONS:
            return ui_more_options_active_label_text(index);
        case UI_PAGE_ADD_INGREDIENTS:
            return ui_add_ingredients_active_label_text(index);
        case UI_PAGE_MACHINE_CHECK:
            return ui_machine_check_active_label_text(index);
        case UI_PAGE_PRESS_PARTS_CHECK:
            return ui_press_parts_active_label_text(index);
        case UI_PAGE_TOFU_MAKING:
            return ui_tofu_making_active_label_text(index);
        case UI_PAGE_TOFU_READY:
            return ui_tofu_ready_active_label_text(index);
        case UI_PAGE_STOP_TOFU_CONFIRM:
            return ui_stop_tofu_active_label_text(index);
        case UI_PAGE_TOFU_STOPPING:
            return ui_tofu_stopping_active_label_text(index);
        case UI_PAGE_TOFU_STOPPED:
            return ui_tofu_stopped_active_label_text(index);
        default:
            return NULL;
    }
}

/**
 * @brief 查询文字的隐藏状态，优先使用页面动态规则，否则读取默认配置。
 * @param page 有效的逻辑页面编号。
 * @param index 该页面内有效的文字索引。
 * @return true 表示隐藏，false 表示显示。
 */
static bool active_label_hidden(ui_page_id_t page, uint8_t index)
{
    if (ui_plantmilk_is_page(page)) return ui_plantmilk_label_hidden(page, index);
    if (page == UI_PAGE_FIRM_TOFU) {
        return ui_firm_tofu_label_hidden(index);
    }
    return ui_page_configs[page].labels[index].hidden;
}

/**
 * @brief 获取文字当前颜色，用于显示选中状态或传感器检查结果。
 * @param page 有效的逻辑页面编号。
 * @param index 该页面内有效的文字索引。
 * @return 页面动态颜色或默认颜色，格式为 0xRRGGBB。
 */
static uint32_t active_label_color(ui_page_id_t page, uint8_t index)
{
    if (ui_plantmilk_is_page(page)) return ui_plantmilk_active_label_color(page, index);
    if (ui_clean_is_page(page)) return ui_clean_active_label_color(page, index);
    if (page == UI_PAGE_FIRM_TOFU) {
        return ui_firm_tofu_active_label_color(index);
    }
    if (page == UI_PAGE_MORE_OPTIONS) {
        return ui_more_options_active_label_color(index);
    }
    if (page == UI_PAGE_MACHINE_CHECK) {
        return ui_machine_check_active_label_color(index);
    }
    if (page == UI_PAGE_PRESS_PARTS_CHECK) {
        return ui_press_parts_active_label_color(index);
    }
    if (page == UI_PAGE_TOFU_READY) {
        return ui_tofu_ready_active_label_color(index);
    }
    if (page == UI_PAGE_STOP_TOFU_CONFIRM) {
        return ui_stop_tofu_active_label_color(index);
    }
    return ui_page_configs[page].labels[index].color_hex;
}

/**
 * @brief 获取文字当前字体，处理植物奶参数字体和豆腐更多选项的动态字体。
 * @param page 有效的逻辑页面编号。
 * @param index 该页面内有效的文字索引。
 * @return 页面指定的动态字体或默认配置中的字体指针。
 */
static const lv_font_t *active_label_font(ui_page_id_t page, uint8_t index)
{
    if (page == UI_PAGE_PLANTMILK_OPTIONS && index == 2U) {
        return ui_font_tabular_22();
    }
    if (page == UI_PAGE_MORE_OPTIONS) {
        return ui_more_options_active_label_font(index);
    }
    return ui_page_configs[page].labels[index].font;
}

/**
 * @brief 设置文字位置，并处理动态文字旋转后的对齐和位置补偿。
 * @param page 有效的逻辑页面编号。
 * @param index 该页面内有效的文字索引，同时对应共用文字槽位。
 * @param label 该文字的默认布局配置。
 * @param text 当前实际显示的文字，用于计算尺寸变化。
 * @param font 当前实际使用的字体。
 * @note 优先使用外部覆盖坐标，再交由植物奶或清洗页面处理专属布局。
 *       豆腐页面根据文字尺寸补偿 90 度旋转后的视觉位置；凝固剂编辑时
 *       还会调整两侧箭头位置，使名称右对齐或在箭头之间居中。
 */
static void apply_label_position(ui_page_id_t page, uint8_t index,
                                 const ui_label_config_t *label,
                                 const char *text, const lv_font_t *font)
{
    if (override_label_pos_valid[page][index]) {
        lv_obj_set_pos(label_slots[index], override_label_x[page][index],
                       override_label_y[page][index]);
        return;
    }

    if (ui_plantmilk_is_page(page)) {
        int16_t pm_x, pm_y;
        ui_plantmilk_active_label_pos(page, index, text, font, &pm_x, &pm_y);
        lv_obj_set_pos(label_slots[index], pm_x, pm_y);
        return;
    }
    if (ui_clean_is_page(page)) {
        int16_t clean_x, clean_y;
        ui_clean_active_label_pos(page, index, text, font, &clean_x, &clean_y);
        lv_obj_set_pos(label_slots[index], clean_x, clean_y);
        return;
    }

    int16_t base_x = label->x;
    int16_t base_y = label->y;
    if (page == UI_PAGE_FIRM_TOFU) {
        ui_firm_tofu_active_label_pos(index, &base_x, &base_y);
    } else if (page == UI_PAGE_MORE_OPTIONS) {
        ui_more_options_active_label_pos(index, &base_x, &base_y);
    }

    int32_t x = base_x;
    int32_t y = base_y;

    /*
     * 不同凝固剂名称的长度差异较大。
     * 浏览和选中时将旋转后的文字右边缘与用量数值的右边缘对齐；
     * 编辑时氯化镁保持右对齐，柠檬酸在固定的两个箭头之间居中，
     * 同时保持文字在卡片横向上的视觉中心不变。
     */
    uint8_t amount_label_index = 0U;
    bool right_align_coagulant = false;
    if (page == UI_PAGE_FIRM_TOFU && index == 2U) {
        amount_label_index = 1U;
        right_align_coagulant = true;
    } else if (page == UI_PAGE_MORE_OPTIONS && index == 2U) {
        amount_label_index = 3U;
        right_align_coagulant = true;
    }

    if (right_align_coagulant) {
        const ui_label_config_t *amount_label =
            &ui_page_configs[page].labels[amount_label_index];
        lv_point_t design_size;
        lv_point_t active_size;
        lv_point_t amount_size;
        lv_txt_get_size(&design_size, label->text, font, 0, 0,
                        LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_txt_get_size(&active_size, text, font, 0, 0,
                        LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_txt_get_size(&amount_size, amount_label->text,
                        amount_label->font, 0, 0,
                        LV_COORD_MAX, LV_TEXT_FLAG_NONE);

        x += ((active_size.x - design_size.x) +
              (active_size.y - design_size.y)) / 2;
        int32_t amount_right_edge =
            amount_label->y - amount_size.y / 2 + amount_size.x;
        bool editing_coagulant =
            (page == UI_PAGE_FIRM_TOFU && ui_is_firm_tofu_editing() &&
             ui_get_selected_firm_tofu_control() == UI_FIRM_TOFU_CONTROL_COAGULANT) ||
            (page == UI_PAGE_MORE_OPTIONS && ui_is_more_options_editing() &&
             ui_get_selected_more_options_control() == UI_MORE_OPTIONS_CONTROL_COAGULANT);
        /* 编辑卡片的右箭头位于数值右边缘附近；留出箭头宽度及文字间距。 */
        if (editing_coagulant) amount_right_edge -= 22;
        y = amount_right_edge + active_size.y / 2 - active_size.x;
        if (editing_coagulant) {
            int16_t card_x, card_y;
            uint8_t previous_arrow, next_arrow;
            if (page == UI_PAGE_FIRM_TOFU) {
                ui_firm_tofu_active_image_pos(UI_FIRM_TOFU_COAGULANT_IMAGE, &card_x, &card_y);
                previous_arrow = UI_FIRM_TOFU_AMOUNT_PREVIOUS_ARROW;
                next_arrow = UI_FIRM_TOFU_AMOUNT_NEXT_ARROW;
            } else {
                ui_more_options_active_image_pos(UI_MORE_OPTIONS_COAGULANT_IMAGE, &card_x, &card_y);
                previous_arrow = UI_MORE_OPTIONS_AMOUNT_PREVIOUS_ARROW;
                next_arrow = UI_MORE_OPTIONS_AMOUNT_NEXT_ARROW;
            }
            /* 文字旋转 90 度后，y 对应视觉上的左右位置。
             * 两个箭头固定在氯化镁编辑态的位置，切换参数只移动文字。 */
            int32_t previous_y = amount_right_edge - design_size.x - 14;
            int32_t next_y = card_y + 200;
            lv_obj_set_pos(image_slots[previous_arrow], card_x, (lv_coord_t)previous_y);
            lv_obj_set_pos(image_slots[next_arrow], card_x, (lv_coord_t)next_y);
            if (ui_get_selected_coagulant() == UI_COAGULANT_CITRIC_ACID) {
                y = (previous_y + next_y + active_size.y - active_size.x) / 2;
            }
        }
        lv_obj_set_pos(label_slots[index], (lv_coord_t)x, (lv_coord_t)y);
        return;
    }

    if (label->angle == 900 && strcmp(text, label->text) != 0) {
        lv_point_t design_size;
        lv_point_t active_size;
        lv_txt_get_size(&design_size, label->text, font, 0, 0,
                        LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_txt_get_size(&active_size, text, font, 0, 0,
                        LV_COORD_MAX, LV_TEXT_FLAG_NONE);

        int32_t width_delta = active_size.x - design_size.x;
        int32_t height_delta = active_size.y - design_size.y;
        bool edge_title = index == 0U &&
                          (page == UI_PAGE_FIRM_TOFU ||
                           page == UI_PAGE_TOFU_MAKING ||
                           page == UI_PAGE_TOFU_STOPPING);
        if (edge_title) {
            /* 这些标题紧贴屏幕边缘。运行时标题变长后，
             * 需要进行水平居中，但若垂直移动，
             * 会裁切中等硬度豆腐和制作已停止等
             * 较长标题的文字。 */
            x += width_delta / 2;
            y += height_delta / 2;
        } else if ((page == UI_PAGE_MACHINE_CHECK ||
                    page == UI_PAGE_PRESS_PARTS_CHECK) &&
                   index > 0U) {
            /* 状态文字从卡片标题下方开始显示。保持起点固定，
             * 使较长的故障信息向卡片内部延伸。 */
            x += width_delta / 2;
            y += height_delta / 2;
        } else {
            x += (width_delta + height_delta) / 2;
            y += (height_delta - width_delta) / 2;
        }
    }

    lv_obj_set_pos(label_slots[index], (lv_coord_t)x, (lv_coord_t)y);
}

/**
 * @brief 查询共用 UI 是否已初始化且屏幕对象有效。
 * @return true 表示初始化完成且 ui_Screen 非空，否则返回 false。
 */
bool ui_is_initialized(void)
{
    return initialized && ui_Screen != NULL;
}

/**
 * @brief 切换到指定逻辑页面，或重新绘制当前逻辑页面。
 * @param page 目标逻辑页面编号。
 * @note 通过更新共用图片和文字槽位完成切换，同时处理焦点、编辑状态和缓存。
 *       完成后更新 current_page 并请求屏幕重绘；未初始化或页面无效时不执行。
 */
void ui_show_page(ui_page_id_t page)
{
    if (!ui_is_initialized() || page >= UI_PAGE_COUNT) {
        return;
    }

    bool page_changed = page != current_page;
    if (page_changed) {
        /* 进入豆腐准备/检查页默认选中动作键，未通过检查时下方退回 Back。
         * 页内重绘保留旋钮选择，避免抢走用户手动选择的 Back 焦点。 */
        if (page == UI_PAGE_ADD_INGREDIENTS) {
            selected_image[page] = UI_ADD_INGREDIENTS_CONTROL_CONTINUE;
        } else if (page == UI_PAGE_MACHINE_CHECK) {
            selected_image[page] = UI_MACHINE_CHECK_CONTROL_CONTINUE;
        } else if (page == UI_PAGE_PRESS_PARTS_CHECK) {
            selected_image[page] = UI_PRESS_PARTS_CONTROL_START;
        } else if (page == UI_PAGE_STOP_TOFU_CONFIRM) {
            /* 所有硬度共用此页；每次进入默认继续制作，页内重绘保留选择。 */
            selected_image[page] = UI_STOP_TOFU_CONTROL_KEEP;
        }
        lv_img_cache_invalidate_src(NULL);
    }

    if (page != UI_PAGE_FIRM_TOFU) {
        ui_firm_tofu_cancel_editing();
    }
    if (page != UI_PAGE_MORE_OPTIONS) {
        ui_more_options_cancel_editing();
    }
    if (page != UI_PAGE_PLANTMILK_OPTIONS) ui_plantmilk_cancel_editing();

    selected_image[page] = sensor_checked_selection(page, selected_image[page]);

    const ui_page_config_t *config = &ui_page_configs[page];

    for (uint8_t i = 0U; i < image_slot_count; ++i) {
        if (i >= config->image_count) {
            lv_obj_add_flag(image_slots[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const ui_image_config_t *image = &config->images[i];
        const void *new_src = active_image_src(page, i);
        if (!page_changed) {
            const void *old_src = lv_img_get_src(image_slots[i]);
            if (old_src != NULL && old_src != new_src) {
                lv_img_cache_invalidate_src(old_src);
            }
        }
        lv_img_set_src(image_slots[i], new_src);
        lv_obj_set_align(image_slots[i], image->align);
        int16_t image_x = image->x;
        int16_t image_y = image->y;
        if (page == UI_PAGE_FIRM_TOFU) {
            ui_firm_tofu_active_image_pos(i, &image_x, &image_y);
        } else if (page == UI_PAGE_MORE_OPTIONS) {
            ui_more_options_active_image_pos(i, &image_x, &image_y);
        } else if (ui_plantmilk_is_page(page)) {
            ui_plantmilk_active_image_pos(page, i, &image_x, &image_y);
        }
        lv_obj_set_pos(image_slots[i], image_x, image_y);
        /* 各功能独立控制指示图的旋转角度。由于不同功能复用同一组图片槽位，
         * 切换到其他页面时需要清除该角度。 */
        int16_t image_angle = 0;
        if (page == UI_PAGE_PLANTMILK_SELECT && i == UI_PM_SELECT_RING) {
            image_angle = (int16_t)(ui_plantmilk_get_recipe() * 900);
        } else if (ui_clean_is_page(page)) {
            image_angle = ui_clean_active_image_angle(page, i);
        }
        lv_img_set_angle(image_slots[i], image_angle);
        if (active_image_hidden(page, i)) {
            lv_obj_add_flag(image_slots[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(image_slots[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    for (uint8_t i = 0U; i < label_slot_count; ++i) {
        if (i >= config->label_count) {
            lv_obj_add_flag(label_slots[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const ui_label_config_t *label = &config->labels[i];
        const char *text = active_label_text(page, i);
        const lv_font_t *font = active_label_font(page, i);
        lv_label_set_text(label_slots[i], text);
        lv_obj_set_align(label_slots[i], label->align);
        lv_obj_set_style_text_color(label_slots[i],
                                    lv_color_hex(active_label_color(page, i)),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(label_slots[i],
                                  page == UI_PAGE_CLEAN_STOPPING && i == 3U
                                      ? 128U : LV_OPA_COVER,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label_slots[i], font,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_transform_angle(label_slots[i], label->angle,
                                         LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_transform_pivot_x(label_slots[i], label->pivot_x,
                                           LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_transform_pivot_y(label_slots[i], label->pivot_y,
                                           LV_PART_MAIN | LV_STATE_DEFAULT);
        apply_label_position(page, i, label, text, font);
        if (active_label_hidden(page, i)) {
            lv_obj_add_flag(label_slots[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(label_slots[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    current_page = page;
    lv_obj_invalidate(ui_Screen);
}

/**
 * @brief 获取当前逻辑页面编号。
 * @return 最近一次成功调用 ui_show_page() 后记录的页面；初始或销毁后为首页。
 */
ui_page_id_t ui_get_current_page(void)
{
    return current_page;
}

/**
 * @brief 设置指定页面的选中图片，并按传感器检查结果修正焦点。
 * @param page 目标逻辑页面。
 * @param index 图片索引，或 UI_IMAGE_INDEX_NONE 表示无选中项。
 * @note 仅当选中项变化且目标页为当前页时重绘，不会切换到其他页面。
 *       未初始化、页面无效或图片索引越界时不执行。
 */
void ui_select_image(ui_page_id_t page, uint8_t index)
{
    if (!ui_is_initialized() || page >= UI_PAGE_COUNT) {
        return;
    }

    const ui_page_config_t *config = &ui_page_configs[page];
    if (index != UI_IMAGE_INDEX_NONE && index >= config->image_count) {
        return;
    }
    index = sensor_checked_selection(page, index);
    if (selected_image[page] == index) {
        return;
    }

    selected_image[page] = index;
    if (current_page != page) {
        return;
    }
    ui_show_page(page);
}

/**
 * @brief 查询指定页面保存的选中图片索引。
 * @param page 要查询的逻辑页面。
 * @return 选中图片索引；无选中项或页面无效时返回 UI_IMAGE_INDEX_NONE。
 */
uint8_t ui_get_selected_image(ui_page_id_t page)
{
    if (page >= UI_PAGE_COUNT) {
        return UI_IMAGE_INDEX_NONE;
    }
    return selected_image[page];
}

/**
 * @brief 设置指定页面图片槽位的覆盖资源，当前页资源变化时立即重绘。
 * @param page 目标逻辑页面。
 * @param index 该页面内的图片索引。
 * @param src 图片源指针；NULL 表示取消覆盖，恢复页面自身的资源选择规则。
 * @note 保存资源指针而不复制资源，调用方需保证资源在使用期间有效。
 *       未初始化或页面、图片索引无效时不执行。
 */
void ui_set_image_src(ui_page_id_t page, uint8_t index, const void *src)
{
    if (!ui_is_initialized() || page >= UI_PAGE_COUNT) {
        return;
    }

    const ui_page_config_t *config = &ui_page_configs[page];
    if (index >= config->image_count || override_src[page][index] == src) {
        return;
    }

    override_src[page][index] = src;
    if (current_page == page) {
        ui_show_page(page);
    }
}

/**
 * @brief 将指定页面中具有备用高亮图片的按钮设为选中状态。
 * @param page 目标逻辑页面。
 * @param index 按钮图片索引，或 UI_IMAGE_INDEX_NONE 表示取消选中。
 * @note 普通索引必须对应配置了 src_alt 的图片；实际焦点仍受传感器检查约束。
 *       更新当前页时按需重绘，不会切换页面。
 */
void ui_set_button(ui_page_id_t page, uint8_t index)
{
    if (!ui_is_initialized() || page >= UI_PAGE_COUNT) {
        return;
    }
    if (index == UI_IMAGE_INDEX_NONE) {
        ui_select_image(page, index);
        return;
    }

    const ui_page_config_t *config = &ui_page_configs[page];
    if (index >= config->image_count || config->images[index].src_alt == NULL) {
        return;
    }
    ui_select_image(page, index);
}

/**
 * @brief 设置指定页面文字槽位的覆盖内容，目标为当前页时立即重绘。
 * @param page 目标逻辑页面。
 * @param index 该页面内的文字索引。
 * @param text UTF-8 文字指针；NULL 表示取消覆盖，恢复页面自身的文字选择规则。
 * @note 保存文字指针供后续重绘使用，调用方需保证文字在覆盖生效期间有效。
 *       未初始化或页面、文字索引无效时不执行。
 */
void ui_set_label_text(ui_page_id_t page, uint8_t index, const char *text)
{
    if (!ui_is_initialized() || page >= UI_PAGE_COUNT ||
        index >= ui_page_configs[page].label_count) {
        return;
    }

    override_label_text[page][index] = text;
    if (current_page == page) {
        ui_show_page(page);
    }
}

/**
 * @brief 覆盖指定页面文字的位置，后续重绘时优先使用此坐标。
 * @param page 目标逻辑页面。
 * @param index 该页面内的文字索引。
 * @param x 相对文字对齐位置的水平偏移，单位：像素。
 * @param y 相对文字对齐位置的垂直偏移，单位：像素。
 * @note 当前页会立即更新对应文字对象；未初始化或参数索引无效时不执行。
 */
void ui_set_label_pos(ui_page_id_t page, uint8_t index, int16_t x, int16_t y)
{
    if (!ui_is_initialized() || page >= UI_PAGE_COUNT ||
        index >= ui_page_configs[page].label_count) {
        return;
    }

    override_label_x[page][index] = x;
    override_label_y[page][index] = y;
    override_label_pos_valid[page][index] = true;
    if (current_page == page) {
        lv_obj_set_pos(label_slots[index], x, y);
        lv_obj_invalidate(label_slots[index]);
    }
}

/**
 * @brief 选中当前逻辑页面中的按钮，是 ui_set_button() 的当前页封装。
 * @param index 按钮图片索引，或 UI_IMAGE_INDEX_NONE 表示取消选中。
 */
void ui_select_button(uint8_t index)
{
    ui_set_button(current_page, index);
}

/**
 * @brief 查询当前逻辑页面的选中图片或按钮索引。
 * @return 当前页保存的选中索引；无选中项时返回 UI_IMAGE_INDEX_NONE。
 */
uint8_t ui_get_selected_button(void)
{
    return ui_get_selected_image(current_page);
}

/**
 * @brief 覆盖当前逻辑页面中的图片资源，是 ui_set_image_src() 的当前页封装。
 * @param index 当前页面内的图片索引。
 * @param src 使用期间需保持有效的图片源指针；NULL 表示取消覆盖。
 */
void ui_swap_image(uint8_t index, const void *src)
{
    ui_set_image_src(current_page, index, src);
}

/**
 * @brief 初始化共用屏幕、图片和文字对象池，重置各功能状态并加载首页。
 * @note 调用前需已注册 LVGL 默认显示设备；重复调用已初始化的 UI 时直接返回。
 *       对象池大小取所有页面的最大需求，超出固定容量时删除新建屏幕并退出。
 */
void ui_init(void)
{
    if (initialized) {
        return;
    }
    ui_lz4_decoder_init();
    lv_disp_t *display = lv_disp_get_default();
    lv_disp_set_theme(display,
                      lv_theme_default_init(display,
                                            lv_palette_main(LV_PALETTE_BLUE),
                                            lv_palette_main(LV_PALETTE_RED),
                                            true, &UI_FONT_DEFAULT));

    ui_Screen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Screen, lv_color_hex(0x000000),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Screen, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);

    image_slot_count = 0U;
    label_slot_count = 0U;
    for (uint8_t page = 0U; page < UI_PAGE_COUNT; ++page) {
        if (ui_page_configs[page].image_count > image_slot_count) {
            image_slot_count = ui_page_configs[page].image_count;
        }
        if (ui_page_configs[page].label_count > label_slot_count) {
            label_slot_count = ui_page_configs[page].label_count;
        }
    }

    if (image_slot_count > UI_MAX_PAGE_IMAGES ||
        label_slot_count > UI_MAX_PAGE_LABELS) {
        printf("UI: page configuration exceeds fixed slot capacity\n");
        lv_obj_del(ui_Screen);
        ui_Screen = NULL;
        return;
    }

    for (uint8_t i = 0U; i < image_slot_count; ++i) {
        image_slots[i] = lv_img_create(ui_Screen);
        lv_obj_add_flag(image_slots[i],
                        LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_ADV_HITTEST);
        lv_obj_clear_flag(image_slots[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    for (uint8_t i = 0U; i < label_slot_count; ++i) {
        label_slots[i] = lv_label_create(ui_Screen);
        lv_obj_set_size(label_slots[i], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_add_flag(label_slots[i], LV_OBJ_FLAG_HIDDEN);
    }

    memset(selected_image, UI_IMAGE_INDEX_NONE, sizeof(selected_image));
    selected_image[UI_PAGE_HOME] = UI_HOME_BUTTON_TOFU;
    selected_image[UI_PAGE_FIRM_TOFU] = UI_FIRM_TOFU_AMOUNT_IMAGE;
    selected_image[UI_PAGE_MORE_OPTIONS] = UI_MORE_OPTIONS_INGREDIENT_IMAGE;
    selected_image[UI_PAGE_ADD_INGREDIENTS] =
        UI_ADD_INGREDIENTS_CONTROL_CONTINUE;
    selected_image[UI_PAGE_MACHINE_CHECK] =
        UI_MACHINE_CHECK_CONTROL_CONTINUE;
    selected_image[UI_PAGE_PRESS_PARTS_CHECK] =
        UI_PRESS_PARTS_CONTROL_START;
    selected_image[UI_PAGE_TOFU_READY] = UI_TOFU_READY_CONTROL_COLLECT;
    selected_image[UI_PAGE_STOP_TOFU_CONFIRM] =
        UI_STOP_TOFU_CONTROL_KEEP;
    selected_image[UI_PAGE_TOFU_STOPPED] = UI_TOFU_STOPPED_CLEAN_BUTTON;
    ui_tofu_firmness_reset();
    ui_firm_tofu_reset();
    ui_more_options_reset();
    ui_machine_check_reset();
    ui_press_parts_check_reset();
    ui_tofu_stopping_reset();
    ui_plantmilk_reset();
    ui_clean_reset();
    memset(override_src, 0, sizeof(override_src));
    memset(override_label_text, 0, sizeof(override_label_text));
    memset(override_label_pos_valid, 0, sizeof(override_label_pos_valid));

    initialized = true;
    ui_show_page(UI_PAGE_HOME);
    lv_disp_load_scr(ui_Screen);
}

/**
 * @brief 删除共用屏幕及其子对象，清理图片缓存并重置页面和各功能状态。
 * @note 同时清空对象槽位、选中项和覆盖配置，未初始化时不执行。
 */
void ui_destroy(void)
{
    if (!initialized) {
        return;
    }

    lv_img_cache_invalidate_src(NULL);

    if (ui_Screen != NULL) {
        lv_obj_del(ui_Screen);
    }
    ui_Screen = NULL;

    memset(image_slots, 0, sizeof(image_slots));
    memset(label_slots, 0, sizeof(label_slots));
    memset(selected_image, UI_IMAGE_INDEX_NONE, sizeof(selected_image));
    memset(override_src, 0, sizeof(override_src));
    memset(override_label_text, 0, sizeof(override_label_text));
    memset(override_label_pos_valid, 0, sizeof(override_label_pos_valid));
    image_slot_count = 0U;
    label_slot_count = 0U;
    current_page = UI_PAGE_HOME;
    ui_tofu_firmness_reset();
    ui_firm_tofu_reset();
    ui_more_options_reset();
    ui_machine_check_reset();
    ui_press_parts_check_reset();
    ui_tofu_stopping_reset();
    ui_plantmilk_reset();
    ui_clean_reset();
    initialized = false;
}
