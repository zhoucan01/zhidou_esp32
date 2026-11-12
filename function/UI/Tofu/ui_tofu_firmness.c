#include "ui_tofu_internal.h"

#define IMG(src, x, y, align) \
    { &(src), NULL, (x), (y), (align) }
#define IMG_HL(src, src_alt, x, y, align) \
    { &(src), &(src_alt), (x), (y), (align) }
#define LABEL(text, x, y, align, color, font, angle, pivot_x, pivot_y, hidden) \
    { (text), (x), (y), (align), (color), &(font), (angle), \
      (pivot_x), (pivot_y), (hidden) }

enum {
    UI_TOFU_FIRMNESS_NAME_LABEL = 0U,
    UI_TOFU_FIRMNESS_DESCRIPTION_LABEL = 2U,
};

const ui_image_config_t
    ui_tofu_firmness_images[UI_TOFU_FIRMNESS_IMAGE_COUNT] = {
        IMG(ui_img_homepage_png, 0, 0, LV_ALIGN_CENTER),
        IMG_HL(ui_img_back_png, ui_img_back_high_png,
               123, 180, LV_ALIGN_CENTER),
};

const ui_label_config_t
    ui_tofu_firmness_labels[UI_TOFU_FIRMNESS_LABEL_COUNT] = {
        LABEL("Soft", 34, -5, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Tofu Firmness", 213, -213, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_22, 900, 0, 0, false),
        LABEL("Soft. like pudding", -47, -60, LV_ALIGN_CENTER, 0xFFFFFF,
              UI_FONT_16, 900, 0, 0, false),
};

typedef struct {
    const lv_img_dsc_t *preview;
    const char *name;
    const char *description;
    const char *details_title;
} ui_tofu_firmness_config_t;

static const ui_tofu_firmness_config_t
    tofu_firmness_selections[UI_TOFU_FIRMNESS_COUNT] = {
        [UI_TOFU_FIRMNESS_SOFT] = {
            &ui_img_soft_png, "Soft", "Soft. like pudding", "Soft Tofu"
        },
        [UI_TOFU_FIRMNESS_MEDIUM_FIRM] = {
            &ui_img_medium_firm_png, "Medium Firm", "Smooth and holds its shape",
            "Medium Firm Tofu"
        },
        [UI_TOFU_FIRMNESS_FIRM] = {
            &ui_img_firm_png, "Firm", "Dense and easy to slice", "Firm Tofu"
        },
        [UI_TOFU_FIRMNESS_EXTRA_FIRM] = {
            &ui_img_extra_firm_png, "Extra Firm", "Very dense and chewy",
            "Extra Firm Tofu"
        },
};

static ui_tofu_firmness_t selected_tofu_firmness;

void ui_tofu_firmness_reset(void)
{
    selected_tofu_firmness = UI_TOFU_FIRMNESS_SOFT;
}

const void *ui_tofu_firmness_active_image_src(uint8_t index, bool focused)
{
    if (index == UI_TOFU_FIRMNESS_PREVIEW_IMAGE) {
        return tofu_firmness_selections[selected_tofu_firmness].preview;
    }

    const ui_image_config_t *image = &ui_tofu_firmness_images[index];
    return focused && image->src_alt != NULL ? image->src_alt : image->src;
}

const char *ui_tofu_firmness_active_label_text(uint8_t index)
{
    const ui_tofu_firmness_config_t *firmness =
        &tofu_firmness_selections[selected_tofu_firmness];
    if (index == UI_TOFU_FIRMNESS_NAME_LABEL) {
        return firmness->name;
    }
    if (index == UI_TOFU_FIRMNESS_DESCRIPTION_LABEL) {
        return firmness->description;
    }
    return ui_tofu_firmness_labels[index].text;
}

const char *ui_tofu_firmness_details_title(void)
{
    return tofu_firmness_selections[selected_tofu_firmness].details_title;
}

void ui_select_tofu_firmness(ui_tofu_firmness_t firmness)
{
    if (!ui_is_initialized() || firmness >= UI_TOFU_FIRMNESS_COUNT) {
        return;
    }

    selected_tofu_firmness = firmness;
    ui_internal_set_selected_image(UI_PAGE_TOFU_FIRMNESS,
                                   UI_IMAGE_INDEX_NONE);
    if (ui_get_current_page() == UI_PAGE_TOFU_FIRMNESS) {
        ui_show_page(UI_PAGE_TOFU_FIRMNESS);
    }
}

ui_tofu_firmness_t ui_get_selected_tofu_firmness(void)
{
    return selected_tofu_firmness;
}

void ui_select_tofu_firmness_back(bool selected)
{
    if (!ui_is_initialized()) {
        return;
    }

    ui_internal_set_selected_image(
        UI_PAGE_TOFU_FIRMNESS,
        selected ? UI_TOFU_FIRMNESS_BACK_BUTTON : UI_IMAGE_INDEX_NONE);
    if (ui_get_current_page() == UI_PAGE_TOFU_FIRMNESS) {
        ui_show_page(UI_PAGE_TOFU_FIRMNESS);
    }
}

bool ui_is_tofu_firmness_back_selected(void)
{
    return ui_get_selected_image(UI_PAGE_TOFU_FIRMNESS) ==
           UI_TOFU_FIRMNESS_BACK_BUTTON;
}
