#include "ui_clean_internal.h"

#include <stdio.h>
#include <string.h>

enum {
    CL_SELECT_BACKGROUND,
    CL_SELECT_RINSE,
    CL_SELECT_DEEP,
    CL_SELECT_BACK,
};

enum {
    CL_PREP_BACK = UI_CLEAN_PREP_BACK,
    CL_PREP_GUIDE_BACKGROUND,
    CL_PREP_GUIDE_ICON,
    CL_PREP_GUIDE_NUMBER,
    CL_PREP_CHECKLIST,
    CL_PREP_STEP_1,
    CL_PREP_STEP_2,
    CL_PREP_START = UI_CLEAN_PREP_START,
    CL_PREP_STEP_3,
};

enum { CL_STOP_CONTINUE = 1U, CL_STOP_STOP = 2U };

const ui_image_config_t ui_cl_select_images[] = {
    CL_IMG(ui_img_cl_clean_png_lz4, 0, 0),
    CL_IMG_HL(ui_img_cl_03_selfclean_icon_rinse_cream_64x64_png_lz4,
              ui_img_cl_09_selfclean_icon_rinse_orange_64x64_png_lz4, 0, 162),
    CL_IMG_HL(ui_img_cl_508633414_lz4, ui_img_cl_1232396848_lz4, 0, -162),
    CL_IMG_HL(ui_img_back_png_lz4, ui_img_cl_back_1_png_lz4, 123, 180),
};

const ui_label_config_t ui_cl_select_labels[] = {
    CL_LABEL("Clean", 172, -212, 0xFFFFFF, 22),
    CL_LABEL("Rinse", 51, -18, 0xF2BE9F, 26),
    CL_LABEL("Water only", -75, -30, 0xFFFFFF, 16),
};

const ui_image_config_t ui_cl_rinse_prep_images[] = {
    CL_IMG_HL(ui_img_back_png_lz4, ui_img_cl_back_1_png_lz4, 123, 180),
    CL_IMG(ui_img_cl_17_selfclean_instruction_background_152x180_png_lz4, 3, -151),
    CL_IMG(ui_img_cl_group_36_png_lz4, 16, -154),
    CL_IMG(ui_img_cl_1527058981_lz4, 68, -200),
    CL_IMG(ui_img_cl_01_selfclean_checklist_background_280x180_png_lz4, 3, 81),
    CL_IMG(ui_img_cl_449874874_lz4, 51, 81),
    CL_IMG(ui_img_cl_1804218058_lz4, -29, 81),
    CL_IMG_HL(ui_img_normal_png_lz4, ui_img_high_png_lz4, -123, -3),
};

const ui_label_config_t ui_cl_rinse_prep_labels[] = {
    CL_LABEL("Prepare to rinse", 232, -212, 0xFFFFFF, 24),
    CL_LABEL("Dishwasher-safe\nparts", 34, -195, 0xFFFFFF, 14),
    CL_LABEL("Closed", -5, 163, 0xA6D3A1, 14),
    CL_LABEL("Ready", -29, 167, 0xA6D3A1, 14),
    CL_LABEL("Start Rinsing", -54, -43, 0xF2BE9F, 18),
};

const ui_image_config_t ui_cl_deep_prep_images[] = {
    CL_IMG_HL(ui_img_back_png_lz4, ui_img_cl_back_1_png_lz4, 123, 180),
    CL_IMG(ui_img_cl_17_selfclean_instruction_background_152x180_png_lz4, 3, -149),
    CL_IMG(ui_img_cl_group_36_png_lz4, 16, -152),
    CL_IMG(ui_img_cl_1527058981_lz4, 68, -198),
    CL_IMG(ui_img_cl_01_selfclean_checklist_background_280x180_png_lz4, 3, 86),
    CL_IMG(ui_img_cl_449874874_lz4, 64, 85),
    CL_IMG(ui_img_cl_41988670_lz4, 9, 85),
    CL_IMG_HL(ui_img_normal_png_lz4, ui_img_high_png_lz4, -125, 0),
    CL_IMG(ui_img_cl_1258173808_lz4, -53, 84),
};

const ui_label_config_t ui_cl_deep_prep_labels[] = {
    CL_LABEL("Prepare to clean", 238, -213, 0xFFFFFF, 24),
    CL_LABEL("Remove and clean\nparts", 34, -193, 0xFFFFFF, 14),
    CL_LABEL("Close lid", -24, 168, 0xA6D3A1, 14),
    CL_LABEL("Fit tank", -42, 172, 0xA6D3A1, 14),
    CL_LABEL("Start Cleaning", -51, -44, 0xF2BE9F, 18),
};

const ui_image_config_t ui_cl_running_images[] = {
    CL_IMG(ui_img_cl_808820240_lz4, 9, -2),
    CL_IMG(ui_img_cl_21_selfclean_icon_rinsing_88x88_png_lz4, 16, -156),
    CL_IMG_HL(ui_img_normal_png_lz4, ui_img_high_png_lz4, -116, -1),
};

const ui_label_config_t ui_cl_running_labels[] = {
    CL_LABEL("Rinsing", 195, -206, 0xFFFFFF, 24),
    CL_LABEL("Rinsing", 96, -75, 0xFFFFFF, 22),
    CL_LABEL("About 2 min left", 134, -74, 0xF2BE9F, 28),
    CL_LABEL("Stop", -87, -6, 0xF2BE9F, 18),
};

const ui_image_config_t ui_cl_complete_images[] = {
    CL_IMG(ui_img_cl_808820240_lz4, 12, -3),
    CL_IMG(ui_img_cl_735058827_lz4, 19, -157),
    CL_IMG_HL(ui_img_normal_png_lz4, ui_img_high_png_lz4, -118, -3),
};

const ui_label_config_t ui_cl_complete_labels[] = {
    CL_LABEL("Rinsing complete", 247, -214, 0xFFFFFF, 24),
    CL_LABEL("Empty and clean the waste\ntank", 198, -85, 0xFFFFFF, 22),
    CL_LABEL("Done", -82, -14, 0xFFFFFF, 18),
};

const ui_image_config_t ui_cl_stop_images[] = {
    CL_IMG(ui_img_cl_448x182_png_lz4, 6, -2),
    CL_IMG_HL(ui_img_1_png_lz4, ui_img_2_png_lz4, -118, -114),
    CL_IMG_HL(ui_img_1_png_lz4, ui_img_2_png_lz4, -118, 114),
};

const ui_label_config_t ui_cl_stop_labels[] = {
    CL_LABEL("Stop rinsing?", 216, -213, 0xFFFFFF, 22),
    CL_LABEL("Clean before making again.", 177, -151, 0xFFFFFF, 22),
    CL_LABEL("Continue Cleaning", -11, -201, 0xFFFFFF, 20),
    CL_LABEL("Stop Cleaning", -35, 52, 0xFFFFFF, 20),
};

static ui_clean_flow_ops_t flow;
static ui_clean_mode_t mode;
static bool lid_present;
static bool tank_present;
static bool stopping;
static uint8_t carousel_index;
static uint32_t carousel_tick;
static uint16_t shown_remaining_minutes;
static char remaining_text[32];

bool ui_clean_can_start(void)
{
    return lid_present && tank_present;
}

static ui_page_id_t prepare_page(void)
{
    return mode == UI_CLEAN_RINSE ? UI_PAGE_CLEAN_PREPARE_RINSE
                                  : UI_PAGE_CLEAN_PREPARE_CLEAN;
}

static ui_page_id_t running_page(void)
{
    return mode == UI_CLEAN_RINSE ? UI_PAGE_CLEAN_RINSING
                                  : UI_PAGE_CLEAN_CLEANING;
}

static void reset_carousel(void)
{
    carousel_index = 0U;
    carousel_tick = lv_tick_get();
}

void ui_clean_bind_flow(const ui_clean_flow_ops_t *ops)
{
    if (ops != NULL) flow = *ops;
    else memset(&flow, 0, sizeof(flow));
}

bool ui_clean_is_page(ui_page_id_t page)
{
    return page >= UI_PAGE_CLEAN_SELECT && page <= UI_PAGE_CLEAN_STOPPING;
}

void ui_clean_reset(void)
{
    memset(&flow, 0, sizeof(flow));
    mode = UI_CLEAN_RINSE;
    lid_present = tank_present = stopping = false;
    shown_remaining_minutes = UINT16_MAX;
    reset_carousel();
    ui_internal_set_selected_image(UI_PAGE_CLEAN_SELECT, CL_SELECT_RINSE);
    ui_internal_set_selected_image(UI_PAGE_CLEAN_PREPARE_RINSE, CL_PREP_START);
    ui_internal_set_selected_image(UI_PAGE_CLEAN_PREPARE_CLEAN, CL_PREP_START);
    ui_internal_set_selected_image(UI_PAGE_CLEAN_RINSING, 2U);
    ui_internal_set_selected_image(UI_PAGE_CLEAN_CLEANING, 2U);
    ui_internal_set_selected_image(UI_PAGE_CLEAN_COMPLETE, 2U);
    ui_internal_set_selected_image(UI_PAGE_CLEAN_STOP_CONFIRM, CL_STOP_CONTINUE);
    ui_internal_set_selected_image(UI_PAGE_CLEAN_STOPPING, UI_IMAGE_INDEX_NONE);
}

void ui_clean_enter(void)
{
    stopping = false;
    ui_select_image(UI_PAGE_CLEAN_SELECT,
                    mode == UI_CLEAN_RINSE ? CL_SELECT_RINSE : CL_SELECT_DEEP);
    ui_show_page(UI_PAGE_CLEAN_SELECT);
}

void ui_clean_prepare(ui_clean_mode_t selected_mode)
{
    mode = selected_mode;
    stopping = false;
    lid_present = tank_present = false;
    shown_remaining_minutes = UINT16_MAX;
    reset_carousel();
    ui_internal_set_selected_image(prepare_page(), CL_PREP_START);
    if (flow.refresh_sensors != NULL) flow.refresh_sensors();
    ui_show_page(prepare_page());
}

void ui_clean_show_running(ui_clean_mode_t selected_mode)
{
    mode = selected_mode;
    stopping = false;
    shown_remaining_minutes = UINT16_MAX;
    ui_show_page(running_page());
}

void ui_clean_sensor_update(bool lid, bool tank)
{
    bool changed = lid_present != lid || tank_present != tank;
    lid_present = lid;
    tank_present = tank;
    if (changed && (ui_get_current_page() == UI_PAGE_CLEAN_PREPARE_RINSE ||
                    ui_get_current_page() == UI_PAGE_CLEAN_PREPARE_CLEAN)) {
        ui_show_page(ui_get_current_page());
    }
}

void ui_clean_move_selection(int direction)
{
    if (direction == 0) return;
    ui_page_id_t page = ui_get_current_page();
    int selected = ui_get_selected_image(page);
    if (page == UI_PAGE_CLEAN_SELECT) {
        selected += direction > 0 ? 1 : -1;
        if (selected > CL_SELECT_BACK) selected = CL_SELECT_RINSE;
        if (selected < CL_SELECT_RINSE) selected = CL_SELECT_BACK;
        if (selected == CL_SELECT_RINSE) mode = UI_CLEAN_RINSE;
        if (selected == CL_SELECT_DEEP) mode = UI_CLEAN_DEEP;
    } else if (page == UI_PAGE_CLEAN_PREPARE_RINSE ||
               page == UI_PAGE_CLEAN_PREPARE_CLEAN) {
        selected = direction > 0 ? CL_PREP_START : CL_PREP_BACK;
    } else if (page == UI_PAGE_CLEAN_STOP_CONFIRM) {
        selected = direction > 0 ? CL_STOP_STOP : CL_STOP_CONTINUE;
    } else {
        return;
    }
    ui_select_image(page, (uint8_t)selected);
}

void ui_clean_handle_short_press(void)
{
    ui_page_id_t page = ui_get_current_page();
    uint8_t selected = ui_get_selected_image(page);
    if (page == UI_PAGE_CLEAN_SELECT) {
        if (selected == CL_SELECT_BACK) ui_show_page(UI_PAGE_HOME);
        else {
            ui_clean_prepare(selected == CL_SELECT_RINSE ? UI_CLEAN_RINSE : UI_CLEAN_DEEP);
        }
    } else if (page == UI_PAGE_CLEAN_PREPARE_RINSE ||
               page == UI_PAGE_CLEAN_PREPARE_CLEAN) {
        if (selected != CL_PREP_BACK && flow.refresh_sensors != NULL) {
            flow.refresh_sensors();
        }
        if (selected == CL_PREP_BACK) ui_show_page(UI_PAGE_CLEAN_SELECT);
        else if (ui_clean_can_start() && flow.start != NULL && flow.start(mode)) {
            ui_clean_show_running(mode);
        }
    } else if (page == UI_PAGE_CLEAN_RINSING || page == UI_PAGE_CLEAN_CLEANING) {
        ui_select_image(UI_PAGE_CLEAN_STOP_CONFIRM, CL_STOP_CONTINUE);
        ui_show_page(UI_PAGE_CLEAN_STOP_CONFIRM);
    } else if (page == UI_PAGE_CLEAN_COMPLETE) {
        ui_show_page(UI_PAGE_HOME);
    } else if (page == UI_PAGE_CLEAN_STOP_CONFIRM) {
        if (selected == CL_STOP_CONTINUE) ui_show_page(running_page());
        else if (flow.stop != NULL && flow.stop() != UI_CLEAN_FLOW_IDLE) {
            stopping = true;
            ui_show_page(UI_PAGE_CLEAN_STOPPING);
        }
    }
}

void ui_clean_handle_long_press(void)
{
    ui_page_id_t page = ui_get_current_page();
    if ((page == UI_PAGE_CLEAN_RINSING || page == UI_PAGE_CLEAN_CLEANING ||
         page == UI_PAGE_CLEAN_STOP_CONFIRM) && flow.stop != NULL &&
        flow.stop() != UI_CLEAN_FLOW_IDLE) {
        stopping = true;
        ui_show_page(UI_PAGE_CLEAN_STOP_CONFIRM);
    }
}

void ui_clean_tick(void)
{
    ui_page_id_t page = ui_get_current_page();
    if (!ui_clean_is_page(page)) return;
    if (flow.refresh_sensors != NULL &&
        (page == UI_PAGE_CLEAN_PREPARE_RINSE || page == UI_PAGE_CLEAN_PREPARE_CLEAN)) {
        flow.refresh_sensors();
    }
    if ((page == UI_PAGE_CLEAN_PREPARE_RINSE || page == UI_PAGE_CLEAN_PREPARE_CLEAN) &&
        lv_tick_elaps(carousel_tick) >= 1000U) {
        uint8_t count = page == UI_PAGE_CLEAN_PREPARE_RINSE ? 3U : 4U;
        carousel_index = (uint8_t)((carousel_index + 1U) % count);
        carousel_tick = lv_tick_get();
        ui_show_page(page);
    }
    /* 清洗在停止确认期间仍会继续，未申请停止时也要检查自然完成。 */
    if ((page == UI_PAGE_CLEAN_RINSING || page == UI_PAGE_CLEAN_CLEANING ||
         page == UI_PAGE_CLEAN_STOP_CONFIRM || page == UI_PAGE_CLEAN_STOPPING) &&
        flow.poll != NULL) {
        ui_clean_flow_state_t state = flow.poll();
        if (state == UI_CLEAN_FLOW_READY) {
            stopping = false;
            ui_show_page(UI_PAGE_CLEAN_COMPLETE);
        } else if (state == UI_CLEAN_FLOW_STOPPED) {
            stopping = false;
            ui_show_page(UI_PAGE_CLEAN_SELECT);
        } else if (flow.remaining_minutes != NULL && !stopping &&
                   (page == UI_PAGE_CLEAN_RINSING || page == UI_PAGE_CLEAN_CLEANING)) {
            uint16_t minutes = flow.remaining_minutes(mode);
            if (minutes != shown_remaining_minutes) {
                shown_remaining_minutes = minutes;
                ui_show_page(page);
            }
        }
    }
}

int16_t ui_clean_active_image_angle(ui_page_id_t page, uint8_t index)
{
    /* 现有背景已包含两组半圆形圆点。
     * 旋转半周可将橙色圆点移到选中的深度清洗一侧。 */
    return page == UI_PAGE_CLEAN_SELECT && index == CL_SELECT_BACKGROUND &&
                   mode == UI_CLEAN_DEEP
               ? 1800 : 0;
}

const void *ui_clean_active_image_src(ui_page_id_t page, uint8_t index,
                                      bool focused)
{
    const ui_image_config_t *image = &ui_page_configs[page].images[index];
    if ((page == UI_PAGE_CLEAN_PREPARE_RINSE || page == UI_PAGE_CLEAN_PREPARE_CLEAN) &&
        index == CL_PREP_GUIDE_ICON) {
        if (page == UI_PAGE_CLEAN_PREPARE_RINSE) {
            static const lv_img_dsc_t *const icons[] = {
                &ui_img_cl_group_36_png_lz4,
                &ui_img_cl_14_selfclean_icon_lid_64x64_png_lz4,
                &ui_img_cl_705142925_lz4,
            };
            return icons[carousel_index % 3U];
        }
        static const lv_img_dsc_t *const icons[] = {
            &ui_img_cl_group_36_png_lz4,
            &ui_img_cl_12_selfclean_icon_parts_64x64_png_lz4,
            &ui_img_cl_14_selfclean_icon_lid_64x64_png_lz4,
            &ui_img_cl_705142925_lz4,
        };
        return icons[carousel_index % 4U];
    }
    if ((page == UI_PAGE_CLEAN_PREPARE_RINSE || page == UI_PAGE_CLEAN_PREPARE_CLEAN) &&
        index == CL_PREP_GUIDE_NUMBER) {
        if (page == UI_PAGE_CLEAN_PREPARE_RINSE)
            return carousel_index == 0U ? &ui_img_cl_1527058981_lz4
                                        : &ui_img_cl_564171066_lz4;
        if (carousel_index == 0U) return &ui_img_cl_1527058981_lz4;
        if (carousel_index == 1U) return &ui_img_cl_564171066_lz4;
        return &ui_img_cl_2086925163_lz4;
    }
    if (page == UI_PAGE_CLEAN_CLEANING && index == 1U)
        return &ui_img_cl_1232396848_lz4;
    if ((page == UI_PAGE_CLEAN_PREPARE_RINSE || page == UI_PAGE_CLEAN_PREPARE_CLEAN) &&
        index == CL_PREP_START && !ui_clean_can_start()) focused = false;
    return focused && image->src_alt != NULL ? image->src_alt : image->src;
}

const char *ui_clean_active_label_text(ui_page_id_t page, uint8_t index)
{
    if (page == UI_PAGE_CLEAN_SELECT) {
        if (index == 1U) return mode == UI_CLEAN_RINSE ? "Rinse" : "Deep\nClean";
        if (index == 2U) return mode == UI_CLEAN_RINSE ? "Water only" : "With cleaning agent";
    }
    if (page == UI_PAGE_CLEAN_PREPARE_RINSE && index == 1U) {
        static const char *const text[] = { "Dishwasher-safe\nparts", "Close the lid", "Fit water tank" };
        return text[carousel_index % 3U];
    }
    if (page == UI_PAGE_CLEAN_PREPARE_CLEAN && index == 1U) {
        static const char *const text[] = { "Remove and clean\nparts", "Add cleaning agent", "Close the lid", "Fit water tank" };
        return text[carousel_index % 4U];
    }
    if ((page == UI_PAGE_CLEAN_PREPARE_RINSE || page == UI_PAGE_CLEAN_PREPARE_CLEAN)) {
        if (index == 2U) return lid_present ? "Closed" : "Close lid";
        if (index == 3U) return tank_present ? "Ready" : "Fit tank";
    }
    if (page == UI_PAGE_CLEAN_RINSING || page == UI_PAGE_CLEAN_CLEANING) {
        if (index == 0U || index == 1U)
            return page == UI_PAGE_CLEAN_RINSING ? "Rinsing" : "Cleaning";
        if (index == 2U) {
            uint16_t minutes = mode == UI_CLEAN_RINSE ? 2U : 25U;
            if (flow.remaining_minutes != NULL) minutes = flow.remaining_minutes(mode);
            snprintf(remaining_text, sizeof(remaining_text), "About %u min left", minutes);
            return remaining_text;
        }
    }
    if (page == UI_PAGE_CLEAN_COMPLETE && index == 0U)
        return mode == UI_CLEAN_RINSE ? "Rinsing complete" : "Cleaning complete";
    if (page == UI_PAGE_CLEAN_STOP_CONFIRM) {
        if (index == 0U) return mode == UI_CLEAN_RINSE ? "Stop rinsing?" : "Stop cleaning?";
        if (index == 1U && stopping) return "Stopping current cycle...";
        if (index == 2U) return mode == UI_CLEAN_RINSE ? "Continue Rinsing" : "Continue Cleaning";
        if (index == 3U) return mode == UI_CLEAN_RINSE ? "Stop Rinsing" : "Stop Cleaning";
    }
    return ui_page_configs[page].labels[index].text;
}

uint32_t ui_clean_active_label_color(ui_page_id_t page, uint8_t index)
{
    uint8_t selected = ui_get_selected_image(page);
    if ((page == UI_PAGE_CLEAN_PREPARE_RINSE || page == UI_PAGE_CLEAN_PREPARE_CLEAN) &&
        index == 4U) {
        if (!ui_clean_can_start()) return 0x777671;
        return selected == CL_PREP_START ? 0xF2BE9F : 0xFFFFFF;
    }
    if ((page == UI_PAGE_CLEAN_PREPARE_RINSE || page == UI_PAGE_CLEAN_PREPARE_CLEAN) &&
        index == 2U) return lid_present ? 0xA6D3A1 : 0xF2BE9F;
    if ((page == UI_PAGE_CLEAN_PREPARE_RINSE || page == UI_PAGE_CLEAN_PREPARE_CLEAN) &&
        index == 3U) return tank_present ? 0xA6D3A1 : 0xF2BE9F;
    if (page == UI_PAGE_CLEAN_STOP_CONFIRM &&
        ((index == 2U && selected == CL_STOP_CONTINUE) ||
         (index == 3U && selected == CL_STOP_STOP))) return 0xF2BE9F;
    return ui_page_configs[page].labels[index].color_hex;
}

void ui_clean_active_label_pos(ui_page_id_t page, uint8_t index,
                               const char *text, const lv_font_t *font,
                               int16_t *x, int16_t *y)
{
    const ui_label_config_t *label = &ui_page_configs[page].labels[index];
    lv_point_t design, active;
    lv_txt_get_size(&design, label->text, font, 0, 0, LV_COORD_MAX,
                    LV_TEXT_FLAG_NONE);
    lv_txt_get_size(&active, text, font, 0, 0, LV_COORD_MAX,
                    LV_TEXT_FLAG_NONE);
    *x = label->x + (active.x - design.x) / 2;
    *y = label->y + (active.y - design.y) / 2;
    if (page == UI_PAGE_CLEAN_SELECT && index == 1U) {
        /* 将双行模式名称旋转 90 度后，仍保持居中。 */
        *x += (active.y - design.y) / 2;
        *y -= (active.x - design.x) / 2;
    }
}
