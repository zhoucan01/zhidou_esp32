/* Documentation renderer: real UI/LVGL with the existing host-only I/O stubs. */
#define main existing_ui_regression_main
#include "../../tests/firmware/test_plantmilk_ui.c"
#undef main

static void guide_capture(const char *directory, ui_page_id_t page, const char *name)
{
    ui_show_page(page);
    screenshot(directory, name);
    assert(lv_mem_test() == LV_RES_OK);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    lv_init();
    static lv_disp_draw_buf_t draw;
    static lv_disp_drv_t driver;
    lv_disp_draw_buf_init(&draw, rows, NULL, 320 * 60);
    lv_disp_drv_init(&driver);
    driver.hor_res = 320;
    driver.ver_res = 480;
    driver.draw_buf = &draw;
    driver.flush_cb = flush;
    lv_disp_t *disp = lv_disp_drv_register(&driver);
    disp->driver->draw_ctx->layer_init = checked_layer_create;
    ui_init();
    UiFlowConfigure();
    /* Installed fixture for the cleaning carousel; no real radio/actuator I/O. */
    CleanUiConfigure(false, true);
    const ui_machine_check_sensors_t installed = {
        .lid_present = true, .coagulant_container_present = true,
        .coagulant_low_level = false, .water_tank_present = true,
        .water_tank_high_level = false, .waste_tank_present = true,
    };
    const ui_press_parts_sensors_t press_installed = {true, true};
    ui_machine_check_update(&installed);
    ui_press_parts_check_update(&press_installed);
    ui_plantmilk_check_update(true, true);
    for (unsigned page = 0; page < UI_PAGE_COUNT; ++page) {
        if (page == UI_PAGE_CLEAN_PREPARE_RINSE || page == UI_PAGE_CLEAN_PREPARE_CLEAN) {
            ui_clean_prepare(page == UI_PAGE_CLEAN_PREPARE_RINSE ? UI_CLEAN_RINSE : UI_CLEAN_DEEP);
            ui_clean_sensor_update(true, true);
        } else if (page == UI_PAGE_CLEAN_RINSING || page == UI_PAGE_CLEAN_CLEANING) {
            ui_clean_show_running(page == UI_PAGE_CLEAN_RINSING ? UI_CLEAN_RINSE : UI_CLEAN_DEEP);
        }
        char name[32];
        snprintf(name, sizeof(name), "page-%02u", page);
        guide_capture(argv[1], (ui_page_id_t)page, name);
    }
    for (unsigned item = 1; item < UI_HOME_ITEM_COUNT; ++item) {
        ui_select_home_item((ui_home_item_t)item);
        char name[32]; snprintf(name, sizeof(name), "home-%u", item);
        guide_capture(argv[1], UI_PAGE_HOME, name);
    }
    for (unsigned item = 1; item < UI_TOFU_FIRMNESS_COUNT; ++item) {
        ui_select_tofu_firmness((ui_tofu_firmness_t)item);
        char name[32]; snprintf(name, sizeof(name), "firmness-%u", item);
        guide_capture(argv[1], UI_PAGE_TOFU_FIRMNESS, name);
    }
    ui_select_tofu_firmness(UI_TOFU_FIRMNESS_SOFT);
    guide_capture(argv[1], UI_PAGE_FIRM_TOFU, "tofu-parameters");
    ui_set_firm_tofu_editing(true);
    guide_capture(argv[1], UI_PAGE_FIRM_TOFU, "tofu-edit");
    ui_set_firm_tofu_editing(false);
    const ui_machine_check_sensors_t missing = {0};
    ui_machine_check_update(&missing);
    guide_capture(argv[1], UI_PAGE_MACHINE_CHECK, "tofu-check-missing");
    ui_machine_check_update(&installed);
    const ui_press_parts_sensors_t press_missing = {false, false};
    ui_press_parts_check_update(&press_missing);
    guide_capture(argv[1], UI_PAGE_PRESS_PARTS_CHECK, "press-check-missing");
    ui_press_parts_check_update(&press_installed);
    for (unsigned recipe = 1; recipe < UI_PM_RECIPE_COUNT; ++recipe) {
        ui_plantmilk_select_recipe((ui_plantmilk_recipe_t)recipe);
        char name[32]; snprintf(name, sizeof(name), "recipe-%u", recipe);
        guide_capture(argv[1], UI_PAGE_PLANTMILK_SELECT, name);
    }
    ui_plantmilk_select_recipe(UI_PM_SOY);
    ui_select_image(UI_PAGE_PLANTMILK_OPTIONS, UI_PM_OPTIONS_COOK);
    guide_capture(argv[1], UI_PAGE_PLANTMILK_OPTIONS, "soy-cook-locked");
    ui_plantmilk_select_recipe(UI_PM_OAT);
    ui_select_image(UI_PAGE_PLANTMILK_OPTIONS, UI_PM_OPTIONS_BLEND);
    ui_show_page(UI_PAGE_PLANTMILK_OPTIONS);
    ui_plantmilk_toggle_editing();
    guide_capture(argv[1], UI_PAGE_PLANTMILK_OPTIONS, "milk-edit");
    ui_plantmilk_cancel_editing();
    ui_plantmilk_check_update(false, false);
    guide_capture(argv[1], UI_PAGE_PLANTMILK_MACHINE_CHECK, "milk-check-missing");
    ui_clean_prepare(UI_CLEAN_RINSE);
    ui_clean_sensor_update(false, false);
    guide_capture(argv[1], UI_PAGE_CLEAN_PREPARE_RINSE, "clean-check-missing");
    ui_clean_show_running(UI_CLEAN_RINSE);
    guide_capture(argv[1], UI_PAGE_CLEAN_STOP_CONFIRM, "rinse-stop-confirm");
    ui_clean_prepare(UI_CLEAN_DEEP);
    ui_clean_sensor_update(true, true);
    for (unsigned step = 1; step < 4; ++step) {
        lv_tick_inc(1001);
        ui_clean_tick();
        char name[32]; snprintf(name, sizeof(name), "deep-step-%u", step);
        screenshot(argv[1], name);
    }
    lv_img_cache_invalidate_src(NULL);
    assert(lv_mem_test() == LV_RES_OK);
    printf("GUIDE_RENDER_OK pages=%u layers=%u largest_layer=%u\n", UI_PAGE_COUNT, rendered_layers, largest_layer_bytes);
    return 0;
}
