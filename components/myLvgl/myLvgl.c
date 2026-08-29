#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "myLvgl.h"

#include "ui.h"
#include "lvgl.h"

#include "freeRTOS/FreeRTOS.h"
#include "freeRTOS/task.h"

#include "time.h"
#include <sys/time.h>

extern volatile bool isPress;
extern volatile bool isLongPress;
extern volatile int encCounter;

#include "FunctionSelection.h"
#include "DataCenter.h"
#include "freeRTOS/event_groups.h"
#include "esp_log.h"

typedef enum {
    MODE_CLEAN = 0,
    MODE_PLANT_MILK,
    MODE_TOFU,
    MODE_COUNT
} mode_selection_t;

typedef enum {
    CLEAN_DEEP = 0,
    CLEAN_QUICK,
    CLEAN_COUNT
} clean_selection_t;

static mode_selection_t mode_selection = MODE_PLANT_MILK;
static clean_selection_t clean_selection = CLEAN_DEEP;
static int mode_rotation_accumulator = 0;
static lv_obj_t *screen_history[4];
static int screen_history_depth = 0;
volatile mode_flag_t currentModeFlag = MODE_FLAG_NONE;

static void load_next_screen(lv_obj_t *screen)
{
    if (screen_history_depth < (int)(sizeof(screen_history) / sizeof(screen_history[0]))) {
        screen_history[screen_history_depth++] = lv_scr_act();
    }
    lv_disp_load_scr(screen);
}

static void return_to_previous_screen(void)
{
    if (screen_history_depth > 0) {
        lv_obj_t *previous_screen = screen_history[--screen_history_depth];
        lv_disp_load_scr(previous_screen);
    }
}

static void set_only_visible(lv_obj_t *visible,
                             lv_obj_t *first,
                             lv_obj_t *second,
                             lv_obj_t *third)
{
    lv_obj_add_flag(first, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(second, LV_OBJ_FLAG_HIDDEN);
    if (third != NULL) {
        lv_obj_add_flag(third, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(visible, LV_OBJ_FLAG_HIDDEN);
}

static void show_mode_selection(void)
{
    lv_obj_t *selected = ui_plantmilkmode;

    if (mode_selection == MODE_CLEAN) {
        selected = ui_cleanmode;
    } else if (mode_selection == MODE_TOFU) {
        selected = ui_toufumode;
    }

    set_only_visible(selected, ui_plantmilkmode, ui_cleanmode, ui_toufumode);
}

static void show_clean_selection(void)
{
    lv_obj_t *selected = clean_selection == CLEAN_DEEP ? ui_Image14 : ui_Image16;
    set_only_visible(selected, ui_Image14, ui_Image16, NULL);
}

static void show_screen4_result(lv_obj_t *result)
{
    set_only_visible(result, ui_Image13, ui_Image19, ui_Image26);
    load_next_screen(ui_Screen4);
}

static int wrap_selection(int selection, int change, int count)
{
    selection = (selection + change) % count;
    return selection < 0 ? selection + count : selection;
}

static void handle_rotation(int change)
{
    lv_obj_t *screen = lv_scr_act();

    if (screen == ui_Screen3) {
        mode_rotation_accumulator += change;
        while (mode_rotation_accumulator >= 3) {
            mode_rotation_accumulator -= 3;
            mode_selection = (mode_selection_t)wrap_selection(mode_selection, 1, MODE_COUNT);
            show_mode_selection();
        }
        while (mode_rotation_accumulator <= -3) {
            mode_rotation_accumulator += 3;
            mode_selection = (mode_selection_t)wrap_selection(mode_selection, -1, MODE_COUNT);
            show_mode_selection();
        }
    } else if (screen == ui_Screen1) {
        clean_selection = (clean_selection_t)wrap_selection(clean_selection,
                                                             change > 0 ? 1 : -1,
                                                             CLEAN_COUNT);
        show_clean_selection();
    }
}

static void handle_press(void)
{
    lv_obj_t *screen = lv_scr_act();

    if (screen == ui_Screen2) {
        mode_selection = MODE_CLEAN;
        mode_rotation_accumulator = 0;
        show_mode_selection();
        load_next_screen(ui_Screen3);
    } else if (screen == ui_Screen3) {
        if (mode_selection == MODE_PLANT_MILK) {
            currentModeFlag = MODE_FLAG_PLANT_MILK; 
            if (FuncTypeGroup != NULL && FuncOrderGroup != NULL) {
                xEventGroupSetBits(FuncTypeGroup, SoyMilk_bit);
                xEventGroupSetBits(FuncOrderGroup, FuncStart_bit);
            }
            show_screen4_result(ui_Image13);
        } else if (mode_selection == MODE_TOFU) {
            currentModeFlag = MODE_FLAG_TOFU;
            if (FuncTypeGroup != NULL && FuncOrderGroup != NULL) {
                xEventGroupSetBits(FuncTypeGroup, Milk_bit);
                xEventGroupSetBits(FuncOrderGroup, FuncStart_bit);
            }
            show_screen4_result(ui_Image19);
        } else {
            currentModeFlag = MODE_FLAG_CLEAN;
            clean_selection = CLEAN_DEEP;
            show_clean_selection();
            load_next_screen(ui_Screen1);
        }
    } else if (screen == ui_Screen1 && clean_selection == CLEAN_DEEP) {
        currentModeFlag = MODE_FLAG_CLEAN_DEEP;
        if (FuncTypeGroup != NULL && FuncOrderGroup != NULL) {
            xEventGroupSetBits(FuncTypeGroup, SelfClean);
            xEventGroupSetBits(FuncOrderGroup, FuncStart_bit);
        }
        show_screen4_result(ui_Image26);
    }
}

static void animation_refresh_cb(lv_timer_t *timer)
{
    lv_obj_invalidate(lv_scr_act());
    lv_timer_del(timer);
}

static void refresh_after_animation(uint32_t animation_ms)
{
    lv_timer_t *timer = lv_timer_create(animation_refresh_cb, animation_ms + 20, NULL);
    lv_timer_set_repeat_count(timer, 1);
}
lv_obj_t * versionObj = NULL;
void lvUIshowTask(void *arg)
{
    int last_enc_counter = encCounter;
    char (*uiversion)[4];
    uiversion = getVersion();
    refresh_after_animation(100);
    char tbuf[16] = {0};
    int sec_tick = 0;
    // 德国时区 (Europe/Berlin): CET=UTC+1, CEST=UTC+2, 自动夏令时
    setenv("TZ", "CET-1CEST-2,M3.5.0/2,M10.5.0/3", 1);
    tzset();
    int x = -65;
    int y = 240;
    {
      lv_obj_set_x(ui_TimeLable, x);
      lv_obj_set_y(ui_TimeLable, y);
      lv_obj_set_x(ui_TimeLable2, x);
      lv_obj_set_y(ui_TimeLable2, y);
      lv_obj_set_x(ui_TimeLable3, x);
      lv_obj_set_y(ui_TimeLable3, y);
      lv_obj_set_x(ui_TimeLable4, x);
      lv_obj_set_y(ui_TimeLable4, y);
    }
    versionObj = lv_label_create(ui_Screen2);
    lv_obj_set_width(versionObj, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(versionObj, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(versionObj,0);
    lv_obj_set_y(versionObj,-100);
    lv_obj_set_align(versionObj, LV_ALIGN_CENTER);
    lv_label_set_text_fmt(versionObj,
        "version: \nesp32:%d.%d.%d.%d\nstf1:%d.%d.%d.%d\nstg4:%d.%d.%d.%d",
        uiversion[0][0], uiversion[0][1], uiversion[0][2], uiversion[0][3],
        uiversion[1][0], uiversion[1][1], uiversion[1][2], uiversion[1][3],
        uiversion[2][0], uiversion[2][1], uiversion[2][2], uiversion[2][3]);
    lv_obj_set_style_transform_pivot_x(versionObj, lv_obj_get_width(versionObj) / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(versionObj, lv_obj_get_height(versionObj) / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_angle(versionObj, 2700, LV_PART_MAIN);   

    while (1) {
        // 每秒更新一次时间 (50ms * 20 = 1s)
        if (sec_tick == 0) {
            time_t now = time(NULL);
            struct tm *t = localtime(&now);
            // 时:分 + AM/PM (上午=AM, 下午=PM)
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d %s",
                     t->tm_hour, t->tm_min, (t->tm_hour < 12) ? "AM" : "PM");

            // 每个屏幕各有一个时间标签, 全部刷新, 切屏后立即正确
            lv_label_set_text(ui_TimeLable,  tbuf);
            lv_label_set_text(ui_TimeLable2, tbuf);
            lv_label_set_text(ui_TimeLable3, tbuf);
            lv_label_set_text(ui_TimeLable4, tbuf);
        }
        if (++sec_tick >= 1200) sec_tick = 0;

        int current_enc_counter = encCounter;

        if (current_enc_counter != last_enc_counter) {
            int change = current_enc_counter - last_enc_counter;
            last_enc_counter = current_enc_counter;
            handle_rotation(change);
        }

        if (isLongPress) {
            isLongPress = false;
            isPress = false;
            return_to_previous_screen();
        } else if (isPress) {
            isPress = false;
            handle_press();
        }

        lv_tick_inc(50);
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
