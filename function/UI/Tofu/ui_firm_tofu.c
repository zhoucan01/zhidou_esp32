#include "ui_tofu_internal.h"

#define IMG(src, x, y, align) \
    { &(src), NULL, (x), (y), (align) }
#define IMG_HL(src, src_alt, x, y, align) \
    { &(src), &(src_alt), (x), (y), (align) }
#define LABEL(text, x, y, align, color, font, angle, pivot_x, pivot_y, hidden) \
    { (text), (x), (y), (align), (color), &(font), (angle), \
      (pivot_x), (pivot_y), (hidden) }

enum {
    UI_FIRM_TOFU_TITLE_LABEL = 0U,
    UI_FIRM_TOFU_AMOUNT_LABEL,
    UI_FIRM_TOFU_COAGULANT_LABEL,
    UI_FIRM_TOFU_DESCRIPTION_LABEL,
};

const ui_image_config_t ui_firm_tofu_images[UI_FIRM_TOFU_IMAGE_COUNT] = {
    IMG_HL(ui_img_back_png, ui_img_back_high_png,
           123, 180, LV_ALIGN_CENTER),
    IMG(ui_img_firm_tofu__high_200g_png, 51, -8, LV_ALIGN_CENTER),
    IMG(ui_img_coagulant_png, -25, -6, LV_ALIGN_CENTER),
    IMG_HL(ui_img_more_options_png, ui_img_more_options_high_png,
           -126, -147, LV_ALIGN_CENTER),
    IMG_HL(ui_img_firm_continue_png, ui_img_firm_continuehigh_png,
           -126, 83, LV_ALIGN_CENTER),
    IMG(ui_img_previous_png, 49, 120, LV_ALIGN_CENTER),
    IMG(ui_img_next_png, 49, 202, LV_ALIGN_CENTER),
};

const ui_label_config_t ui_firm_tofu_labels[UI_FIRM_TOFU_LABEL_COUNT] = {
    LABEL("Soft Tofu", 191, -211, LV_ALIGN_CENTER, 0xFFFFFF,
          UI_FONT_22, 900, 0, 0, false),
    LABEL("150g", 87, 158, LV_ALIGN_CENTER, 0xF2BE9F,
          UI_FONT_22, 900, 0, 0, false),
    LABEL("Magnesium Chloride", 79, 19, LV_ALIGN_CENTER, 0xFFFFFF,
          UI_FONT_18, 900, 0, 0, false),
    LABEL("A mineral salt that helps tofu set", 72, -220,
          LV_ALIGN_CENTER, 0xFFFFFF, UI_FONT_16,
          900, 0, 0, false),
};

static const lv_img_dsc_t *const
    tofu_amount_highlight_images[UI_TOFU_AMOUNT_COUNT] = {
        [UI_TOFU_AMOUNT_100G] = &ui_img_firm_tofu__high_200g_png,
        [UI_TOFU_AMOUNT_150G] = &ui_img_firm_tofu__high_200g_png,
        [UI_TOFU_AMOUNT_200G] = &ui_img_firm_tofu__high_200g_png,
};

static const char *const tofu_amount_labels[UI_TOFU_AMOUNT_COUNT] = {
    [UI_TOFU_AMOUNT_100G] = "100g",
    [UI_TOFU_AMOUNT_150G] = "150g",
    [UI_TOFU_AMOUNT_200G] = "200g",
};

typedef struct {
    const lv_img_dsc_t *normal;
    const lv_img_dsc_t *highlight;
    const char *name;
    const char *description;
} ui_coagulant_config_t;

static const ui_coagulant_config_t coagulant_selections[UI_COAGULANT_COUNT] = {
    [UI_COAGULANT_MAGNESIUM_CHLORIDE] = {
        &ui_img_coagulant_png, &ui_img_795035558,
        "Magnesium Chloride", "A mineral salt that helps tofu set"
    },
    [UI_COAGULANT_CITRIC_ACID] = {
        &ui_img_coagulant_png, &ui_img_795035558,
        "Citric Acid", "An acid that helps tofu set"
    },
};

static ui_tofu_amount_t selected_tofu_amount;
static ui_coagulant_t selected_coagulant;
static bool firm_tofu_editing;

void ui_firm_tofu_reset(void)
{
    selected_tofu_amount = UI_TOFU_AMOUNT_150G;
    selected_coagulant = UI_COAGULANT_MAGNESIUM_CHLORIDE;
    firm_tofu_editing = false;
}

void ui_firm_tofu_cancel_editing(void)
{
    firm_tofu_editing = false;
}

const void *ui_firm_tofu_active_image_src(uint8_t index, bool focused)
{
    /* 用量和凝固剂复用独立箭头，凝固剂的两个值均可循环切换。 */
    if (ui_get_selected_firm_tofu_control() == UI_FIRM_TOFU_CONTROL_COAGULANT) {
        if (index == UI_FIRM_TOFU_AMOUNT_PREVIOUS_ARROW) return &ui_img_previous_png;
        if (index == UI_FIRM_TOFU_AMOUNT_NEXT_ARROW) return &ui_img_next_png;
    }
    if (index == UI_FIRM_TOFU_AMOUNT_PREVIOUS_ARROW) {
        return selected_tofu_amount == UI_TOFU_AMOUNT_100G
                   ? &ui_img_disabled_png
                   : &ui_img_previous_png;
    }
    if (index == UI_FIRM_TOFU_AMOUNT_NEXT_ARROW) {
        return selected_tofu_amount == UI_TOFU_AMOUNT_200G
                   ? &ui_img_1408384802
                   : &ui_img_next_png;
    }

    if (index == UI_FIRM_TOFU_AMOUNT_IMAGE) {
        if (focused) {
            return tofu_amount_highlight_images[selected_tofu_amount];
        }
        return &ui_img_firm_tofu_200g_png;
    }

    if (index == UI_FIRM_TOFU_COAGULANT_IMAGE) {
        const ui_coagulant_config_t *coagulant =
            &coagulant_selections[selected_coagulant];
        return focused ? coagulant->highlight : coagulant->normal;
    }

    const ui_image_config_t *image = &ui_firm_tofu_images[index];
    return focused && image->src_alt != NULL ? image->src_alt : image->src;
}

bool ui_firm_tofu_image_hidden(uint8_t index)
{
    if (index != UI_FIRM_TOFU_AMOUNT_PREVIOUS_ARROW &&
        index != UI_FIRM_TOFU_AMOUNT_NEXT_ARROW) {
        return false;
    }
    uint8_t selected = ui_get_selected_image(UI_PAGE_FIRM_TOFU);
    return !firm_tofu_editing ||
           (selected != UI_FIRM_TOFU_AMOUNT_IMAGE &&
            selected != UI_FIRM_TOFU_COAGULANT_IMAGE);
}

void ui_firm_tofu_active_image_pos(uint8_t index, int16_t *x, int16_t *y)
{
    if (x == NULL || y == NULL || index >= UI_FIRM_TOFU_IMAGE_COUNT) {
        return;
    }

    *x = ui_firm_tofu_images[index].x;
    *y = ui_firm_tofu_images[index].y;

    uint8_t selected = ui_get_selected_image(UI_PAGE_FIRM_TOFU);
    if (index == UI_FIRM_TOFU_AMOUNT_IMAGE) {
        if (selected != UI_FIRM_TOFU_AMOUNT_IMAGE) {
            *x = 49;
            *y = -7;
        }
        return;
    }

    if (index == UI_FIRM_TOFU_COAGULANT_IMAGE &&
        selected == UI_FIRM_TOFU_COAGULANT_IMAGE) {
        /*
         * 高亮态和编辑态资源的尺寸相同，保持其中心位置固定，
         * 使按下旋钮时只增加编辑箭头。
         */
        *x = -21;
        *y = -7;
    }
}

const char *ui_firm_tofu_active_label_text(uint8_t index)
{
    if (index == UI_FIRM_TOFU_TITLE_LABEL) {
        return ui_tofu_firmness_details_title();
    }
    if (index == UI_FIRM_TOFU_AMOUNT_LABEL) {
        return tofu_amount_labels[selected_tofu_amount];
    }
    if (index == UI_FIRM_TOFU_COAGULANT_LABEL) {
        return coagulant_selections[selected_coagulant].name;
    }
    if (index == UI_FIRM_TOFU_DESCRIPTION_LABEL) {
        return coagulant_selections[selected_coagulant].description;
    }
    return ui_firm_tofu_labels[index].text;
}

uint32_t ui_firm_tofu_active_label_color(uint8_t index)
{
    if (index == UI_FIRM_TOFU_AMOUNT_LABEL ||
        index == UI_FIRM_TOFU_COAGULANT_LABEL) {
        uint8_t selected = ui_get_selected_image(UI_PAGE_FIRM_TOFU);
        uint8_t image_index = index == UI_FIRM_TOFU_AMOUNT_LABEL
                                  ? UI_FIRM_TOFU_AMOUNT_IMAGE
                                  : UI_FIRM_TOFU_COAGULANT_IMAGE;
        return selected == image_index ? 0xF2BE9FU : 0xFFFFFFU;
    }
    return ui_firm_tofu_labels[index].color_hex;
}

bool ui_firm_tofu_label_hidden(uint8_t index)
{
    (void)index;
    return ui_firm_tofu_labels[index].hidden;
}

void ui_firm_tofu_active_label_pos(uint8_t index, int16_t *x, int16_t *y)
{
    if (x == NULL || y == NULL || index >= UI_FIRM_TOFU_LABEL_COUNT) {
        return;
    }

    *x = ui_firm_tofu_labels[index].x;
    *y = ui_firm_tofu_labels[index].y;

    uint8_t selected = ui_get_selected_image(UI_PAGE_FIRM_TOFU);
    if (index == UI_FIRM_TOFU_AMOUNT_LABEL) {
        if (selected != UI_FIRM_TOFU_AMOUNT_IMAGE) {
            *x = 85;
            *y = 158;
        } else if (firm_tofu_editing) {
            /* 新箭头位于固定高亮卡片中数值的两侧。
             * 文字旋转 90 度后，增大 y 坐标会使文字向右移动。 */
            *x = 88;
            *y = 148;
        }
        return;
    }

    if (index == UI_FIRM_TOFU_COAGULANT_LABEL &&
        selected == UI_FIRM_TOFU_COAGULANT_IMAGE &&
        firm_tofu_editing) {
        /* 保持编辑态文字的行高；公共布局固定箭头位置，
         * 氯化镁右对齐，柠檬酸在箭头之间居中。 */
        *x = 80;
    }
}

void ui_select_firm_tofu_control(ui_firm_tofu_control_t control)
{
    if (!ui_is_initialized() || control >= UI_FIRM_TOFU_CONTROL_COUNT) {
        return;
    }

    ui_internal_set_selected_image(UI_PAGE_FIRM_TOFU, (uint8_t)control);
    firm_tofu_editing = false;
    if (ui_get_current_page() == UI_PAGE_FIRM_TOFU) {
        ui_show_page(UI_PAGE_FIRM_TOFU);
    }
}

ui_firm_tofu_control_t ui_get_selected_firm_tofu_control(void)
{
    uint8_t selected = ui_get_selected_image(UI_PAGE_FIRM_TOFU);
    if (selected >= UI_FIRM_TOFU_CONTROL_COUNT) {
        return UI_FIRM_TOFU_CONTROL_AMOUNT;
    }
    return (ui_firm_tofu_control_t)selected;
}

void ui_set_firm_tofu_editing(bool editing)
{
    ui_firm_tofu_control_t control = ui_get_selected_firm_tofu_control();
    if (editing && control != UI_FIRM_TOFU_CONTROL_AMOUNT &&
        control != UI_FIRM_TOFU_CONTROL_COAGULANT) {
        return;
    }
    if (!ui_is_initialized() || firm_tofu_editing == editing) {
        return;
    }

    firm_tofu_editing = editing;
    if (ui_get_current_page() == UI_PAGE_FIRM_TOFU) {
        ui_show_page(UI_PAGE_FIRM_TOFU);
    }
}

bool ui_is_firm_tofu_editing(void)
{
    return firm_tofu_editing;
}

ui_tofu_amount_t ui_get_selected_tofu_amount(void)
{
    return selected_tofu_amount;
}

ui_coagulant_t ui_get_selected_coagulant(void)
{
    return selected_coagulant;
}

void ui_restore_default_tofu_options(void)
{
    selected_tofu_amount = UI_TOFU_AMOUNT_150G;
    selected_coagulant = UI_COAGULANT_MAGNESIUM_CHLORIDE;
    firm_tofu_editing = false;
    ui_more_options_restore_defaults();
}

void ui_adjust_firm_tofu_value(int8_t direction)
{
    if (!ui_is_initialized() || direction == 0) {
        return;
    }

    ui_firm_tofu_control_t control = ui_get_selected_firm_tofu_control();
    if (control == UI_FIRM_TOFU_CONTROL_AMOUNT) {
        ui_firm_tofu_adjust_amount(direction);
    } else if (control == UI_FIRM_TOFU_CONTROL_COAGULANT) {
        ui_firm_tofu_adjust_coagulant(direction);
    } else {
        return;
    }

    if (ui_get_current_page() == UI_PAGE_FIRM_TOFU) {
        ui_show_page(UI_PAGE_FIRM_TOFU);
    }
}

void ui_firm_tofu_adjust_amount(int8_t direction)
{
    if (direction > 0 &&
        selected_tofu_amount < UI_TOFU_AMOUNT_200G) {
        selected_tofu_amount =
            (ui_tofu_amount_t)((uint8_t)selected_tofu_amount + 1U);
    } else if (direction < 0 &&
               selected_tofu_amount > UI_TOFU_AMOUNT_100G) {
        selected_tofu_amount =
            (ui_tofu_amount_t)((uint8_t)selected_tofu_amount - 1U);
    }
}

void ui_firm_tofu_adjust_coagulant(int8_t direction)
{
    (void)direction;
    selected_coagulant = selected_coagulant ==
                                 UI_COAGULANT_MAGNESIUM_CHLORIDE
                             ? UI_COAGULANT_CITRIC_ACID
                             : UI_COAGULANT_MAGNESIUM_CHLORIDE;
}
