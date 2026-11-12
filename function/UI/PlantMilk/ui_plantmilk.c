#include "ui_plantmilk_internal.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    const char *center_name;
    const char *description;
    const char *ingredient;
    ui_plantmilk_options_t defaults;
} ui_pm_recipe_config_t;

/* 在此扩展配方名称与默认值，不影响页面导航及豆腐功能的状态。 */
static const ui_pm_recipe_config_t recipes[UI_PM_RECIPE_COUNT] = {
    [UI_PM_OAT] = { "Oat Milk", "Oat\nMilk", "Smooth and creamy", "Add instant oats", {30, 1200, 3, false, true} },
    [UI_PM_NUT] = { "Nut Milk", "Nut\nMilk", "Rich and nutty", "Add nuts", {30, 1200, 3, false, true} },
    [UI_PM_SOY] = { "Soy Milk", "Soy\nMilk", "Full-bodied and balanced", "Add soybeans", {30, 1200, 3, true, true} },
    [UI_PM_CUSTOM] = { "Custom Milk", "Custom\n      Milk", "Set your own recipe", "Add ingredients", {30, 1200, 3, false, true} },
};

static ui_plantmilk_recipe_t recipe;
static ui_plantmilk_options_t options[UI_PM_RECIPE_COUNT];
static bool editing;
static bool lid_present;
static bool tank_present;
static uint16_t remaining_minutes;
static char time_text[12];
static char speed_text[8];
static char volume_text[12];
static char cup_text[80];
static char remaining_text[40];

ui_plantmilk_flow_ops_t ui_pm_flow;

/* 公共层只查表分发事件，跳转条件和目标均在对应页面文件中。 */
static const ui_pm_page_handlers_t *const page_handlers[] = {
    [UI_PAGE_PLANTMILK_SELECT - UI_PAGE_PLANTMILK_SELECT] = &ui_pm_select_handlers,
    [UI_PAGE_PLANTMILK_OPTIONS - UI_PAGE_PLANTMILK_SELECT] = &ui_pm_options_handlers,
    [UI_PAGE_PLANTMILK_ADD_INGREDIENTS - UI_PAGE_PLANTMILK_SELECT] = &ui_pm_add_handlers,
    [UI_PAGE_PLANTMILK_MACHINE_CHECK - UI_PAGE_PLANTMILK_SELECT] = &ui_pm_check_handlers,
    [UI_PAGE_PLANTMILK_MAKING - UI_PAGE_PLANTMILK_SELECT] = &ui_pm_making_handlers,
    [UI_PAGE_PLANTMILK_READY - UI_PAGE_PLANTMILK_SELECT] = &ui_pm_ready_handlers,
    [UI_PAGE_PLANTMILK_READY_NO_FILTER - UI_PAGE_PLANTMILK_SELECT] = &ui_pm_no_filter_handlers,
    [UI_PAGE_PLANTMILK_STOP_CONFIRM - UI_PAGE_PLANTMILK_SELECT] = &ui_pm_stop_handlers,
    [UI_PAGE_PLANTMILK_STOPPING - UI_PAGE_PLANTMILK_SELECT] = &ui_pm_stopping_handlers,
    [UI_PAGE_PLANTMILK_STOPPED - UI_PAGE_PLANTMILK_SELECT] = &ui_pm_stopped_handlers,
};

void ui_plantmilk_bind_flow(const ui_plantmilk_flow_ops_t *ops)
{
    if (ops != NULL) ui_pm_flow = *ops;
    else memset(&ui_pm_flow, 0, sizeof(ui_pm_flow));
}

void ui_plantmilk_handle_short_press(void)
{
    ui_page_id_t page = ui_get_current_page();
    if (!ui_plantmilk_is_page(page)) return;
    const ui_pm_page_handlers_t *handlers = page_handlers[page - UI_PAGE_PLANTMILK_SELECT];
    if (handlers != NULL && handlers->short_press != NULL) handlers->short_press(ui_get_selected_image(page));
}

void ui_plantmilk_handle_long_press(void)
{
    ui_page_id_t page = ui_get_current_page();
    if (!ui_plantmilk_is_page(page)) return;
    const ui_pm_page_handlers_t *handlers = page_handlers[page - UI_PAGE_PLANTMILK_SELECT];
    if (handlers != NULL && handlers->long_press != NULL) handlers->long_press();
}

void ui_plantmilk_tick(void)
{
    ui_page_id_t page = ui_get_current_page();
    if (!ui_plantmilk_is_page(page)) return;
    const ui_pm_page_handlers_t *handlers = page_handlers[page - UI_PAGE_PLANTMILK_SELECT];
    if (handlers != NULL && handlers->tick != NULL) handlers->tick();
}

bool ui_plantmilk_is_page(ui_page_id_t page)
{
    return page >= UI_PAGE_PLANTMILK_SELECT && page <= UI_PAGE_PLANTMILK_STOPPED;
}

void ui_plantmilk_reset(void)
{
    recipe = UI_PM_OAT;
    for (unsigned i = 0; i < UI_PM_RECIPE_COUNT; ++i) options[i] = recipes[i].defaults;
    editing = lid_present = tank_present = false;
    remaining_minutes = 14;
    ui_internal_set_selected_image(UI_PAGE_PLANTMILK_SELECT, UI_PM_SELECT_OAT);
    ui_internal_set_selected_image(UI_PAGE_PLANTMILK_OPTIONS, UI_PM_OPTIONS_VOLUME);
    ui_internal_set_selected_image(UI_PAGE_PLANTMILK_ADD_INGREDIENTS, UI_PM_STEP_ACTION);
    ui_internal_set_selected_image(UI_PAGE_PLANTMILK_MACHINE_CHECK, UI_PM_STEP_ACTION);
    ui_internal_set_selected_image(UI_PAGE_PLANTMILK_MAKING, 0);
    ui_internal_set_selected_image(UI_PAGE_PLANTMILK_READY, UI_PM_READY_COLLECT);
    ui_internal_set_selected_image(UI_PAGE_PLANTMILK_READY_NO_FILTER, 1);
    ui_internal_set_selected_image(UI_PAGE_PLANTMILK_STOP_CONFIRM, UI_PM_STOP_KEEP);
    ui_internal_set_selected_image(UI_PAGE_PLANTMILK_STOPPING, UI_IMAGE_INDEX_NONE);
    ui_internal_set_selected_image(UI_PAGE_PLANTMILK_STOPPED, 1);
}

ui_plantmilk_recipe_t ui_plantmilk_get_recipe(void) { return recipe; }
const ui_plantmilk_options_t *ui_plantmilk_get_options(void) { return &options[recipe]; }
const char *ui_plantmilk_recipe_name(void) { return recipes[recipe].name; }
void ui_plantmilk_cancel_editing(void) { editing = false; }
bool ui_plantmilk_is_editing(void) { return editing; }

void ui_plantmilk_select_recipe(ui_plantmilk_recipe_t value)
{
    if (value >= UI_PM_RECIPE_COUNT) return;
    recipe = value;
    ui_select_image(UI_PAGE_PLANTMILK_SELECT, UI_PM_SELECT_OAT + value);
}

void ui_plantmilk_restore_defaults(void)
{
    options[recipe] = recipes[recipe].defaults;
    editing = false;
    if (ui_get_current_page() == UI_PAGE_PLANTMILK_OPTIONS) ui_show_page(UI_PAGE_PLANTMILK_OPTIONS);
}

void ui_plantmilk_toggle_editing(void)
{
    uint8_t selected = ui_get_selected_image(UI_PAGE_PLANTMILK_OPTIONS);
    /* 豆浆必须加热，Cook 可选中但不允许进入参数编辑。 */
    if (recipe == UI_PM_SOY && selected == UI_PM_OPTIONS_COOK) return;
    if (selected >= UI_PM_OPTIONS_BLEND && selected <= UI_PM_OPTIONS_FILTER) {
        editing = !editing;
        ui_show_page(UI_PAGE_PLANTMILK_OPTIONS);
    }
}

static bool at_bound(uint8_t control, bool upper)
{
    const ui_plantmilk_options_t *p = &options[recipe];
    switch (control) {
        case UI_PM_OPTIONS_BLEND: return upper ? p->blending_seconds >= UI_PM_BLEND_MAX_SECONDS : p->blending_seconds <= UI_PM_BLEND_MIN_SECONDS;
        case UI_PM_OPTIONS_SPEED: return upper ? p->blade_speed >= UI_PM_BLADE_MAX : p->blade_speed <= UI_PM_BLADE_MIN;
        case UI_PM_OPTIONS_COOK: return recipe == UI_PM_SOY;
        case UI_PM_OPTIONS_VOLUME: return upper ? p->volume_ml >= UI_PM_VOLUME_MAX_ML : p->volume_ml <= UI_PM_VOLUME_MIN_ML;
        case UI_PM_OPTIONS_FILTER: return false;
        default: return true;
    }
}

static int clamp(int value, int lower, int upper)
{
    return value < lower ? lower : value > upper ? upper : value;
}

void ui_plantmilk_move_selection(int direction)
{
    if (direction == 0) return;
    int step = direction > 0 ? 1 : -1;
    ui_page_id_t page = ui_get_current_page();
    int selected = ui_get_selected_image(page);
    if (page == UI_PAGE_PLANTMILK_SELECT) {
        /* 圆盘配方循环包含 Back；参数和流程按钮仍在首尾停止。 */
        selected += step;
        if (selected > UI_PM_SELECT_BACK) selected = UI_PM_SELECT_OAT;
        if (selected < UI_PM_SELECT_OAT) selected = UI_PM_SELECT_BACK;
        if (selected != UI_PM_SELECT_BACK) recipe = (ui_plantmilk_recipe_t)(selected - UI_PM_SELECT_OAT);
    } else if (page == UI_PAGE_PLANTMILK_OPTIONS) {
        if (editing) {
            ui_plantmilk_options_t *p = &options[recipe];
            switch (selected) {
                case UI_PM_OPTIONS_BLEND: p->blending_seconds = clamp(p->blending_seconds + step * (int)UI_PM_BLEND_STEP_SECONDS, UI_PM_BLEND_MIN_SECONDS, UI_PM_BLEND_MAX_SECONDS); break;
                case UI_PM_OPTIONS_SPEED: p->blade_speed = clamp(p->blade_speed + step * (int)UI_PM_BLADE_STEP, UI_PM_BLADE_MIN, UI_PM_BLADE_MAX); break;
                case UI_PM_OPTIONS_COOK: p->cook = recipe == UI_PM_SOY ? true : !p->cook; break;
                case UI_PM_OPTIONS_VOLUME: p->volume_ml = clamp(p->volume_ml + step * (int)UI_PM_VOLUME_STEP_ML, UI_PM_VOLUME_MIN_ML, UI_PM_VOLUME_MAX_ML); break;
                case UI_PM_OPTIONS_FILTER: p->use_filter = !p->use_filter; break;
                default: break;
            }
            ui_show_page(page);
            return;
        }
        selected = clamp(selected + step, UI_PM_OPTIONS_BACK, UI_PM_OPTIONS_CONTINUE);
    } else if (page == UI_PAGE_PLANTMILK_ADD_INGREDIENTS || page == UI_PAGE_PLANTMILK_MACHINE_CHECK) {
        selected = step > 0 ? UI_PM_STEP_ACTION : UI_PM_STEP_BACK;
    } else if (page == UI_PAGE_PLANTMILK_READY) {
        selected = clamp(selected + step, UI_PM_READY_COLLECT, UI_PM_READY_CLEAN);
    } else if (page == UI_PAGE_PLANTMILK_STOP_CONFIRM) {
        selected = clamp(selected + step, UI_PM_STOP_KEEP, UI_PM_STOP_STOP);
    } else {
        return;
    }
    ui_select_image(page, (uint8_t)selected);
}

void ui_plantmilk_check_update(bool lid, bool tank)
{
    if (lid_present == lid && tank_present == tank) return;
    lid_present = lid;
    tank_present = tank;
    if (ui_get_current_page() == UI_PAGE_PLANTMILK_MACHINE_CHECK) ui_show_page(UI_PAGE_PLANTMILK_MACHINE_CHECK);
}

bool ui_plantmilk_can_start(void) { return lid_present && tank_present; }

void ui_plantmilk_set_remaining_minutes(uint16_t minutes)
{
    if (minutes == remaining_minutes) return;
    remaining_minutes = minutes;
    if (ui_get_current_page() == UI_PAGE_PLANTMILK_MAKING) ui_show_page(UI_PAGE_PLANTMILK_MAKING);
}

const void *ui_plantmilk_active_image_src(ui_page_id_t page, uint8_t index, bool focused)
{
    const ui_image_config_t *image = &ui_page_configs[page].images[index];
    if (page == UI_PAGE_PLANTMILK_OPTIONS && index >= UI_PM_OPTIONS_PREVIOUS) {
        bool next = index == UI_PM_OPTIONS_NEXT;
        if (at_bound(ui_get_selected_image(page), next)) return next ? &ui_img_pm_1408384802 : &ui_img_pm_disabled_png;
        return next ? &ui_img_pm_next_png : &ui_img_pm_previous_png;
    }
    if (page == UI_PAGE_PLANTMILK_MACHINE_CHECK) {
        if (index == 2) return lid_present ? &ui_img_machinelid_png : &ui_img_pm_174602179;
        if (index == 4) return tank_present ? &ui_img_machinewatertank_png : &ui_img_pm_1205051221;
        if (index == UI_PM_STEP_ACTION) focused = focused && ui_plantmilk_can_start();
    }
    return focused && image->src_alt != NULL ? image->src_alt : image->src;
}

bool ui_plantmilk_image_hidden(ui_page_id_t page, uint8_t index)
{
    if (page == UI_PAGE_PLANTMILK_ADD_INGREDIENTS && index == 2) return !options[recipe].use_filter;
    return page == UI_PAGE_PLANTMILK_OPTIONS && index >= UI_PM_OPTIONS_PREVIOUS && !editing;
}

void ui_plantmilk_active_image_pos(ui_page_id_t page, uint8_t index, int16_t *x, int16_t *y)
{
    if (page != UI_PAGE_PLANTMILK_OPTIONS || index < UI_PM_OPTIONS_PREVIOUS) return;
    uint8_t control = ui_get_selected_image(page);
    if (control < UI_PM_OPTIONS_BLEND || control > UI_PM_OPTIONS_FILTER) return;
    const ui_image_config_t *card = &ui_pm_options_images[control];
    *x = card->x - (control <= UI_PM_OPTIONS_COOK ? 16 : 14);
    *y = card->y + (index == UI_PM_OPTIONS_NEXT ? 1 : -1) * (control <= UI_PM_OPTIONS_COOK ? 43 : 89);
}

bool ui_plantmilk_label_hidden(ui_page_id_t page, uint8_t index)
{
    if (page == UI_PAGE_PLANTMILK_OPTIONS && index == 11)
        return ui_get_selected_image(page) != UI_PM_OPTIONS_COOK;
    return ui_page_configs[page].labels[index].hidden;
}

const char *ui_plantmilk_active_label_text(ui_page_id_t page, uint8_t index)
{
    const ui_plantmilk_options_t *p = &options[recipe];
    if (page == UI_PAGE_PLANTMILK_SELECT) {
        if (index == 1) return recipes[recipe].center_name;
        if (index == 2) return recipes[recipe].description;
    }
    if ((page == UI_PAGE_PLANTMILK_OPTIONS || page == UI_PAGE_PLANTMILK_ADD_INGREDIENTS || page == UI_PAGE_PLANTMILK_MACHINE_CHECK) && index == 0) return recipes[recipe].name;
    if (page == UI_PAGE_PLANTMILK_OPTIONS) {
        switch (index) {
            case 2: snprintf(time_text, sizeof(time_text), "%02u:%02u", p->blending_seconds / 60, p->blending_seconds % 60); return time_text;
            case 4: snprintf(speed_text, sizeof(speed_text), "%u", p->blade_speed); return speed_text;
            case 6: return p->cook ? "On" : "Off";
            case 8: snprintf(volume_text, sizeof(volume_text), "%uml", p->volume_ml); return volume_text;
            case 10: return p->use_filter ? "On" : "Off";
            case 11: return recipe == UI_PM_SOY ? "Soy milk must be cooked" : "Do not use raw ingredients";
            default: break;
        }
    }
    if (page == UI_PAGE_PLANTMILK_ADD_INGREDIENTS) {
        if (index == 1) return p->use_filter ? "Fit filter first" : "No filter required";
        if (index == 2) return recipes[recipe].ingredient;
    }
    if (page == UI_PAGE_PLANTMILK_MACHINE_CHECK) {
        if (index == 2) return lid_present ? "Closed" : "Close lid";
        if (index == 4) return tank_present ? "Ready" : "Fit tank";
        if (index == 5) {
            snprintf(cup_text, sizeof(cup_text), "Use a cup large enough for this batch (%uml)", p->volume_ml);
            return cup_text;
        }
    }
    if (page == UI_PAGE_PLANTMILK_MAKING) {
        if (index == 1) { snprintf(volume_text, sizeof(volume_text), "%uml", p->volume_ml); return volume_text; }
        if (index == 2) { snprintf(remaining_text, sizeof(remaining_text), "About %u min left", remaining_minutes); return remaining_text; }
    }
    return ui_page_configs[page].labels[index].text;
}

uint32_t ui_plantmilk_active_label_color(ui_page_id_t page, uint8_t index)
{
    uint8_t selected = ui_get_selected_image(page);
    if (page == UI_PAGE_PLANTMILK_OPTIONS) {
        if (index >= 2 && index <= 10 && index % 2 == 0 && selected == index / 2) return 0xF2BE9F;
        if ((index == 12 && selected == UI_PM_OPTIONS_RESTORE) || (index == 13 && selected == UI_PM_OPTIONS_CONTINUE)) return 0xF2BE9F;
    }
    if (page == UI_PAGE_PLANTMILK_MACHINE_CHECK) {
        if (index == 2) return lid_present ? 0xAACFA3 : 0xF2BE9F;
        if (index == 4) return tank_present ? 0xAACFA3 : 0xF2BE9F;
        if (index == 6) return !ui_plantmilk_can_start() ? 0x777671 : selected == UI_PM_STEP_ACTION ? 0xF2BE9F : 0xFFFFFF;
    }
    if ((page == UI_PAGE_PLANTMILK_ADD_INGREDIENTS && index == 4 && selected == UI_PM_STEP_ACTION) ||
        (page == UI_PAGE_PLANTMILK_MAKING && index == 3 && selected == 0) ||
        (page == UI_PAGE_PLANTMILK_READY && ((index == 3 && selected == 1) || (index == 4 && selected == 2))) ||
        ((page == UI_PAGE_PLANTMILK_READY_NO_FILTER || page == UI_PAGE_PLANTMILK_STOPPED) && index == 3 && selected == 1) ||
        (page == UI_PAGE_PLANTMILK_STOP_CONFIRM && ((index == 2 && selected == 1) || (index == 3 && selected == 2)))) return 0xF2BE9F;
    return ui_page_configs[page].labels[index].color_hex;
}

void ui_plantmilk_active_label_pos(ui_page_id_t page, uint8_t index,
                                  const char *text, const lv_font_t *font,
                                  int16_t *x, int16_t *y)
{
    const ui_label_config_t *label = &ui_page_configs[page].labels[index];
    lv_point_t design, active;
    lv_txt_get_size(&design, label->text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_txt_get_size(&active, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    *x = label->x + (active.x - design.x) / 2;
    *y = label->y + (active.y - design.y) / 2;
    if (page == UI_PAGE_PLANTMILK_SELECT && index == 1) {
        *x = label->x + ((active.x - design.x) + (active.y - design.y)) / 2;
        *y = label->y + ((active.y - design.y) - (active.x - design.x)) / 2;
    }
    if (page == UI_PAGE_PLANTMILK_SELECT && index == 2) {
        /* The description is rotated 90 degrees around its top-left corner.
         * Center its transformed width on the screen for every recipe. */
        *x = label->x + ((active.x - design.x) + (active.y - design.y)) / 2;
        *y = (active.y - active.x) / 2;
    }
    if (page == UI_PAGE_PLANTMILK_OPTIONS && editing && index >= 2 && index <= 10 && index % 2 == 0) {
        uint8_t control = index / 2;
        if (ui_get_selected_image(page) == control) {
            const ui_image_config_t *card = &ui_pm_options_images[control];
            int16_t center_x = card->x - (control <= UI_PM_OPTIONS_COOK ? 16 : 14);
            *x = center_x + (active.x + active.y) / 2;
            *y = card->y + (active.y - active.x) / 2;
        }
    }
}
