#include "ui_tofu_internal.h"

#define IMG(src, x, y, align) \
    { &(src), NULL, (x), (y), (align) }
#define IMG_HL(src, src_alt, x, y, align) \
    { &(src), &(src_alt), (x), (y), (align) }
#define LABEL(text, x, y, align, color, font, angle, pivot_x, pivot_y, hidden) \
    { (text), (x), (y), (align), (color), &(font), (angle), \
      (pivot_x), (pivot_y), (hidden) }

enum {
    UI_ADD_INGREDIENTS_TITLE_LABEL = 0U,
    UI_ADD_INGREDIENTS_FILTER_LABEL,
    UI_ADD_INGREDIENTS_FILTER_HINT_LABEL,
    UI_ADD_INGREDIENTS_SOYBEANS_LABEL,
    UI_ADD_INGREDIENTS_AMOUNT_LABEL,
    UI_ADD_INGREDIENTS_CONTINUE_LABEL,
};

const ui_image_config_t
    ui_add_ingredients_images[UI_ADD_INGREDIENTS_IMAGE_COUNT] = {
        IMG_HL(ui_img_back_png, ui_img_back_high_png,
               123, 180, LV_ALIGN_CENTER),
        IMG(ui_img_add_png, -9, -117, LV_ALIGN_CENTER),
        IMG(ui_img_filter_2_png, 39, -183, LV_ALIGN_CENTER),
        IMG(ui_img_add_png, -9, 110, LV_ALIGN_CENTER),
        IMG(ui_img_filter_png, 30, 47, LV_ALIGN_CENTER),
        IMG(ui_img_add_ingredents_png, 94, -5, LV_ALIGN_CENTER),
        IMG_HL(ui_img_normal_png, ui_img_high_png,
               -125, -3, LV_ALIGN_CENTER),
};

const ui_label_config_t
    ui_add_ingredients_labels[UI_ADD_INGREDIENTS_LABEL_COUNT] = {
        LABEL("Add ingredients", 226, -214, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Fit filter", 48, -187, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Install first", 12, -190, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_16, 900, 0, 0, false),
        LABEL("Add soybeans", 76, 39, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("2 scoops(150g)", 23, 38, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_16, 900, 0, 0, false),
        LABEL("Continue", -66, -38, LV_ALIGN_CENTER, 0xF2BE9F,
              UI_FONT_20, 900, 0, 0, false),
};

static const char *const tofu_amount_labels[UI_TOFU_AMOUNT_COUNT] = {
    [UI_TOFU_AMOUNT_100G] = "1 scoop(140g)",
    [UI_TOFU_AMOUNT_150G] = "2 scoops(140g)",
    [UI_TOFU_AMOUNT_200G] = "3 scoops(140g)",
};

const void *ui_add_ingredients_active_image_src(uint8_t index, bool focused)
{
    const ui_image_config_t *image = &ui_add_ingredients_images[index];
    return focused && image->src_alt != NULL ? image->src_alt : image->src;
}

const char *ui_add_ingredients_active_label_text(uint8_t index)
{
    if (index == UI_ADD_INGREDIENTS_AMOUNT_LABEL) {
        return tofu_amount_labels[ui_get_selected_tofu_amount()];
    }
    return ui_add_ingredients_labels[index].text;
}

void ui_select_add_ingredients_control(ui_add_ingredients_control_t control)
{
    if (!ui_is_initialized() ||
        (control != UI_ADD_INGREDIENTS_CONTROL_BACK &&
         control != UI_ADD_INGREDIENTS_CONTROL_CONTINUE)) {
        return;
    }

    ui_internal_set_selected_image(UI_PAGE_ADD_INGREDIENTS,
                                   (uint8_t)control);
    if (ui_get_current_page() == UI_PAGE_ADD_INGREDIENTS) {
        ui_show_page(UI_PAGE_ADD_INGREDIENTS);
    }
}

ui_add_ingredients_control_t ui_get_selected_add_ingredients_control(void)
{
    uint8_t selected = ui_get_selected_image(UI_PAGE_ADD_INGREDIENTS);
    if (selected != UI_ADD_INGREDIENTS_CONTROL_CONTINUE) {
        return UI_ADD_INGREDIENTS_CONTROL_BACK;
    }
    return UI_ADD_INGREDIENTS_CONTROL_CONTINUE;
}
