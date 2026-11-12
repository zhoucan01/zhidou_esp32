#include "ui_tofu_internal.h"

#define IMG(src, x, y, align) \
    { &(src), NULL, (x), (y), (align) }
#define IMG_HL(src, src_alt, x, y, align) \
    { &(src), &(src_alt), (x), (y), (align) }
#define LABEL(text, x, y, align, color, font, angle, pivot_x, pivot_y, hidden) \
    { (text), (x), (y), (align), (color), &(font), (angle), \
      (pivot_x), (pivot_y), (hidden) }

enum {
    UI_HOME_TITLE_LABEL = 0U,
    UI_HOME_TIME_LABEL,
    UI_HOME_HINT_LABEL,
};

const ui_image_config_t ui_home_images[UI_HOME_IMAGE_COUNT] = {
    IMG(ui_img_homepage_png, 0, 0, LV_ALIGN_CENTER),
    IMG_HL(ui_img_2096355461, ui_img_1863853840,
           72, 131, LV_ALIGN_CENTER),
    IMG_HL(ui_img_1713777493, ui_img_1460433628,
           -72, 131, LV_ALIGN_CENTER),
    IMG_HL(ui_img_322449093, ui_img_1272990486,
           -72, -131, LV_ALIGN_CENTER),
    IMG_HL(ui_img_38353654, ui_img_503676461,
           72, -131, LV_ALIGN_CENTER),
    IMG(ui_img_bluetooth_png, 135, 144, LV_ALIGN_CENTER),
    IMG(ui_img_clock_png, 135, 175, LV_ALIGN_CENTER),
    IMG(ui_img_countdown_png, 135, 82, LV_ALIGN_CENTER),
    IMG(ui_img_1837598647, 135, 113, LV_ALIGN_CENTER),
};

const ui_label_config_t ui_home_labels[UI_HOME_LABEL_COUNT] = {
    LABEL("  Tofu\nStudio", 59, -8, LV_ALIGN_CENTER, 0xFFFFFF,
          UI_FONT_22, 900, 0, 0, false),
    LABEL("10:28", 156, 201, LV_ALIGN_CENTER, 0xFFFFFF,
          UI_FONT_12, 900, 0, 0, false),
    LABEL("Turn to explore press to select", -2, -100,
          LV_ALIGN_CENTER, 0xFFFFFF, UI_FONT_16, 900, 0, 0, false),
};

typedef struct {
    uint8_t button_index;
    const lv_img_dsc_t *preview;
    const char *title;
} ui_home_selection_config_t;

static const ui_home_selection_config_t home_selections[UI_HOME_ITEM_COUNT] = {
    [UI_HOME_ITEM_TOFU] = {
        UI_HOME_BUTTON_TOFU, &ui_img_tofu_png, "  Tofu\nStudio"
    },
    [UI_HOME_ITEM_MILK] = {
        UI_HOME_BUTTON_MILK, &ui_img_milk_png, " Plant\n  Milk"
    },
    [UI_HOME_ITEM_CLEAN] = {
        UI_HOME_BUTTON_CLEAN, &ui_img_clean_png, " Clean"
    },
    [UI_HOME_ITEM_SETTINGS] = {
        UI_HOME_BUTTON_SETTINGS, &ui_img_setting_png, "Settings"
    },
};

static const ui_home_selection_config_t *home_selection_for_button(
    uint8_t button_index)
{
    for (uint8_t item = 0U; item < UI_HOME_ITEM_COUNT; ++item) {
        if (home_selections[item].button_index == button_index) {
            return &home_selections[item];
        }
    }
    return NULL;
}

const void *ui_home_active_image_src(uint8_t index, bool focused)
{
    if (index == UI_HOME_PREVIEW_IMAGE) {
        const ui_home_selection_config_t *selection =
            home_selection_for_button(
                ui_get_selected_image(UI_PAGE_HOME));
        if (selection != NULL) {
            return selection->preview;
        }
    }

    const ui_image_config_t *image = &ui_home_images[index];
    return focused && image->src_alt != NULL ? image->src_alt : image->src;
}

const char *ui_home_active_label_text(uint8_t index)
{
    if (index == UI_HOME_TITLE_LABEL) {
        const ui_home_selection_config_t *selection =
            home_selection_for_button(
                ui_get_selected_image(UI_PAGE_HOME));
        if (selection != NULL) {
            return selection->title;
        }
    }
    return ui_home_labels[index].text;
}

void ui_select_home_item(ui_home_item_t item)
{
    if (item < UI_HOME_ITEM_COUNT) {
        ui_set_button(UI_PAGE_HOME, home_selections[item].button_index);
    }
}

ui_home_item_t ui_get_selected_home_item(void)
{
    const ui_home_selection_config_t *selection = home_selection_for_button(
        ui_get_selected_image(UI_PAGE_HOME));
    if (selection == NULL) {
        return UI_HOME_ITEM_TOFU;
    }
    return (ui_home_item_t)(selection - home_selections);
}
