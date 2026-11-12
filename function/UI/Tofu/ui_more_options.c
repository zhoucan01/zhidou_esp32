#include "ui_tofu_internal.h"

#define IMG(src, x, y, align) \
    { &(src), NULL, (x), (y), (align) }
#define IMG_HL(src, src_alt, x, y, align) \
    { &(src), &(src_alt), (x), (y), (align) }
#define LABEL(text, x, y, align, color, font, angle, pivot_x, pivot_y, hidden) \
    { (text), (x), (y), (align), (color), &(font), (angle), \
      (pivot_x), (pivot_y), (hidden) }

enum {
    UI_MORE_OPTIONS_TITLE_LABEL = 0U,
    UI_MORE_OPTIONS_INGREDIENT_LABEL,
    UI_MORE_OPTIONS_COAGULANT_LABEL,
    UI_MORE_OPTIONS_AMOUNT_LABEL,
    UI_MORE_OPTIONS_RESTORE_LABEL,
    UI_MORE_OPTIONS_CONFIRM_LABEL,
};

const ui_image_config_t
    ui_more_options_images[UI_MORE_OPTIONS_IMAGE_COUNT] = {
        IMG_HL(ui_img_back_png, ui_img_back_high_png,
               123, 180, LV_ALIGN_CENTER),
        IMG_HL(ui_img_ingredient_png, ui_img_938874969,
               69, -10, LV_ALIGN_CENTER),
        IMG_HL(ui_img_coagulant_png, ui_img_795035558,
               6, -7, LV_ALIGN_CENTER),
        IMG_HL(ui_img_more_tofu_png, ui_img_more_tofu_high_png,
               -52, -6, LV_ALIGN_CENTER),
        IMG_HL(ui_img_1_png, ui_img_2_png,
               -120, -119, LV_ALIGN_CENTER),
        IMG_HL(ui_img_1_png, ui_img_2_png,
               -122, 108, LV_ALIGN_CENTER),
        IMG(ui_img_previous_png, -47, 116, LV_ALIGN_CENTER),
        IMG(ui_img_next_png, -49, 199, LV_ALIGN_CENTER),
};

const ui_label_config_t
    ui_more_options_labels[UI_MORE_OPTIONS_LABEL_COUNT] = {
        LABEL("More Options", 211, -217, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Soybeans", 123, 118, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_18, 900, 0, 0, false),
        LABEL("Magnesium Chloride", 107, 23, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_18, 900, 0, 0, false),
        LABEL("150g", -22, 170, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_18, 900, 0, 0, false),
        LABEL("Restore Defaults", -37, -180, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_18, 900, 0, 0, false),
        LABEL("Confirm", -75, 79, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_18, 900, 0, 0, false),
};

static const char *const tofu_amount_labels[UI_TOFU_AMOUNT_COUNT] = {
    [UI_TOFU_AMOUNT_100G] = "100g",
    [UI_TOFU_AMOUNT_150G] = "150g",
    [UI_TOFU_AMOUNT_200G] = "200g",
};

static const char *const coagulant_labels[UI_COAGULANT_COUNT] = {
    [UI_COAGULANT_MAGNESIUM_CHLORIDE] = "Magnesium Chloride",
    [UI_COAGULANT_CITRIC_ACID] = "Citric Acid",
};

typedef struct {
    const char *name;
} ui_ingredient_option_t;

/* 每新增一种配料，在 ui_ingredient_t 中增加枚举值，并在此增加对应条目。
 * 导航会根据 UI_INGREDIENT_COUNT 自动循环切换。 */
static const ui_ingredient_option_t
    ingredient_options[UI_INGREDIENT_COUNT] = {
        [UI_INGREDIENT_SOYBEANS] = { "Soybeans" },
        [UI_INGREDIENT_PLANT_MILK] = { "PlantMilk" },
};

static bool editing;
static ui_ingredient_t selected_ingredient;

void ui_more_options_reset(void)
{
    editing = false;
    selected_ingredient = UI_INGREDIENT_SOYBEANS;
}

void ui_more_options_restore_defaults(void)
{
    editing = false;
    selected_ingredient = UI_INGREDIENT_SOYBEANS;
}

void ui_more_options_cancel_editing(void)
{
    editing = false;
}

const void *ui_more_options_active_image_src(uint8_t index, bool focused)
{
    if (ui_get_selected_more_options_control() == UI_MORE_OPTIONS_CONTROL_COAGULANT) {
        if (index == UI_MORE_OPTIONS_AMOUNT_PREVIOUS_ARROW) return &ui_img_previous_png;
        if (index == UI_MORE_OPTIONS_AMOUNT_NEXT_ARROW) return &ui_img_next_png;
    }
    if (index == UI_MORE_OPTIONS_AMOUNT_PREVIOUS_ARROW) {
        return ui_get_selected_tofu_amount() == UI_TOFU_AMOUNT_100G
                   ? &ui_img_disabled_png
                   : &ui_img_previous_png;
    }
    if (index == UI_MORE_OPTIONS_AMOUNT_NEXT_ARROW) {
        return ui_get_selected_tofu_amount() == UI_TOFU_AMOUNT_200G
                   ? &ui_img_1408384802
                   : &ui_img_next_png;
    }

    if (focused && editing) {
        if (index == UI_MORE_OPTIONS_INGREDIENT_IMAGE) {
            return &ui_img_ingredient_select_png;
        }
    }

    const ui_image_config_t *image = &ui_more_options_images[index];
    return focused && image->src_alt != NULL ? image->src_alt : image->src;
}

bool ui_more_options_image_hidden(uint8_t index)
{
    if (index != UI_MORE_OPTIONS_AMOUNT_PREVIOUS_ARROW &&
        index != UI_MORE_OPTIONS_AMOUNT_NEXT_ARROW) {
        return false;
    }
    uint8_t selected = ui_get_selected_image(UI_PAGE_MORE_OPTIONS);
    return !editing ||
           (selected != UI_MORE_OPTIONS_AMOUNT_IMAGE &&
            selected != UI_MORE_OPTIONS_COAGULANT_IMAGE);
}

void ui_more_options_active_image_pos(uint8_t index, int16_t *x, int16_t *y)
{
    if (x == NULL || y == NULL || index >= UI_MORE_OPTIONS_IMAGE_COUNT) {
        return;
    }

    *x = ui_more_options_images[index].x;
    *y = ui_more_options_images[index].y;

    uint8_t selected = ui_get_selected_image(UI_PAGE_MORE_OPTIONS);
    if (index == UI_MORE_OPTIONS_COAGULANT_IMAGE &&
        selected == UI_MORE_OPTIONS_COAGULANT_IMAGE) {
        *x = 4;
        *y = -8;
    } else if (index == UI_MORE_OPTIONS_AMOUNT_IMAGE &&
               selected == UI_MORE_OPTIONS_AMOUNT_IMAGE) {
        *x = -47;
        *y = -7;
    } else if (index == UI_MORE_OPTIONS_RESTORE_BUTTON &&
               selected == UI_MORE_OPTIONS_RESTORE_BUTTON) {
        *x = -121;
    }
}

const char *ui_more_options_active_label_text(uint8_t index)
{
    if (index == UI_MORE_OPTIONS_INGREDIENT_LABEL) {
        return ingredient_options[selected_ingredient].name;
    }
    if (index == UI_MORE_OPTIONS_AMOUNT_LABEL) {
        return tofu_amount_labels[ui_get_selected_tofu_amount()];
    }
    if (index == UI_MORE_OPTIONS_COAGULANT_LABEL) {
        return coagulant_labels[ui_get_selected_coagulant()];
    }
    return ui_more_options_labels[index].text;
}

uint32_t ui_more_options_active_label_color(uint8_t index)
{
    uint8_t selected = ui_get_selected_image(UI_PAGE_MORE_OPTIONS);
    if ((index == UI_MORE_OPTIONS_INGREDIENT_LABEL &&
         selected == UI_MORE_OPTIONS_INGREDIENT_IMAGE) ||
        (index == UI_MORE_OPTIONS_COAGULANT_LABEL &&
         selected == UI_MORE_OPTIONS_COAGULANT_IMAGE) ||
        (index == UI_MORE_OPTIONS_AMOUNT_LABEL &&
         selected == UI_MORE_OPTIONS_AMOUNT_IMAGE)) {
        return 0xF2BE9FU;
    }
    return ui_more_options_labels[index].color_hex;
}

const lv_font_t *ui_more_options_active_label_font(uint8_t index)
{
    uint8_t selected = ui_get_selected_image(UI_PAGE_MORE_OPTIONS);
    if ((index == UI_MORE_OPTIONS_INGREDIENT_LABEL &&
         selected == UI_MORE_OPTIONS_INGREDIENT_IMAGE) ||
        (index == UI_MORE_OPTIONS_COAGULANT_LABEL &&
         selected == UI_MORE_OPTIONS_COAGULANT_IMAGE) ||
        (index == UI_MORE_OPTIONS_AMOUNT_LABEL &&
         selected == UI_MORE_OPTIONS_AMOUNT_IMAGE)) {
        return &UI_FONT_22;
    }
    return ui_more_options_labels[index].font;
}

void ui_more_options_active_label_pos(uint8_t index, int16_t *x, int16_t *y)
{
    if (x == NULL || y == NULL || index >= UI_MORE_OPTIONS_LABEL_COUNT) {
        return;
    }

    *x = ui_more_options_labels[index].x;
    *y = ui_more_options_labels[index].y;

    uint8_t selected = ui_get_selected_image(UI_PAGE_MORE_OPTIONS);
    if (index == UI_MORE_OPTIONS_INGREDIENT_LABEL &&
        selected == UI_MORE_OPTIONS_INGREDIENT_IMAGE) {
        *x = 133;
        /* 编辑态箭头使数值可用区域变窄，因此将配料名称
         * 居中放在两个箭头之间，
         * 不再沿用高亮态靠近右边缘的位置。 */
        *y = editing ? 80 : 105;
    } else if (index == UI_MORE_OPTIONS_COAGULANT_LABEL &&
               selected == UI_MORE_OPTIONS_COAGULANT_IMAGE) {
        *x = editing ? 130 : 128;
        /* 公共布局处理凝固剂对齐，编辑态柠檬酸在固定箭头之间居中。 */
        *y = -19;
    } else if (index == UI_MORE_OPTIONS_AMOUNT_LABEL &&
               selected == UI_MORE_OPTIONS_AMOUNT_IMAGE) {
        /* 编辑态复用高亮卡片，数值保持在同一行，
         * 并居中放在新增的独立箭头图片之间。 */
        *x = -10;
        *y = editing ? 144 : 162;
    }
}

void ui_select_more_options_control(ui_more_options_control_t control)
{
    if (!ui_is_initialized() || control >= UI_MORE_OPTIONS_CONTROL_COUNT) {
        return;
    }

    ui_internal_set_selected_image(UI_PAGE_MORE_OPTIONS, (uint8_t)control);
    editing = false;
    if (ui_get_current_page() == UI_PAGE_MORE_OPTIONS) {
        ui_show_page(UI_PAGE_MORE_OPTIONS);
    }
}

ui_more_options_control_t ui_get_selected_more_options_control(void)
{
    uint8_t selected = ui_get_selected_image(UI_PAGE_MORE_OPTIONS);
    if (selected >= UI_MORE_OPTIONS_CONTROL_COUNT) {
        return UI_MORE_OPTIONS_CONTROL_INGREDIENT;
    }
    return (ui_more_options_control_t)selected;
}

void ui_set_more_options_editing(bool value)
{
    ui_more_options_control_t control =
        ui_get_selected_more_options_control();
    if (value && control != UI_MORE_OPTIONS_CONTROL_INGREDIENT &&
        control != UI_MORE_OPTIONS_CONTROL_COAGULANT &&
        control != UI_MORE_OPTIONS_CONTROL_AMOUNT) {
        return;
    }
    if (!ui_is_initialized() || editing == value) {
        return;
    }

    editing = value;
    if (ui_get_current_page() == UI_PAGE_MORE_OPTIONS) {
        ui_show_page(UI_PAGE_MORE_OPTIONS);
    }
}

bool ui_is_more_options_editing(void)
{
    return editing;
}

ui_ingredient_t ui_get_selected_ingredient(void)
{
    return selected_ingredient;
}

void ui_adjust_more_options_value(int8_t direction)
{
    if (!ui_is_initialized() || direction == 0) {
        return;
    }

    ui_more_options_control_t control =
        ui_get_selected_more_options_control();
    if (control == UI_MORE_OPTIONS_CONTROL_INGREDIENT) {
        int value = (int)selected_ingredient + (direction > 0 ? 1 : -1);
        if (value < 0) {
            value = UI_INGREDIENT_COUNT - 1;
        } else if (value >= UI_INGREDIENT_COUNT) {
            value = 0;
        }
        selected_ingredient = (ui_ingredient_t)value;
    } else if (control == UI_MORE_OPTIONS_CONTROL_COAGULANT) {
        ui_firm_tofu_adjust_coagulant(direction);
    } else if (control == UI_MORE_OPTIONS_CONTROL_AMOUNT) {
        ui_firm_tofu_adjust_amount(direction);
    } else {
        return;
    }

    if (ui_get_current_page() == UI_PAGE_MORE_OPTIONS) {
        ui_show_page(UI_PAGE_MORE_OPTIONS);
    }
}
