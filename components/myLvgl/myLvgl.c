#include <stdbool.h>
#include <stdint.h>

#include "myLvgl.h"
#include "ui.h"
#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "FunctionSelection.h"
#include "DataCenter.h"
#include "ModbusMap.h"
#include "MyEncoder.h"
#include "PlantMilk/ui_plantmilk.h"
#include "plantmilk_ui_flow.h"
#include "Clean/ui_clean.h"
#include "clean_ui_flow.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "lv_port_disp.h"
#endif

extern volatile bool isPress;
extern volatile bool isLongPress;
extern volatile int encCounter;

/* 切换一个 UI 选项所需的有效编码器脉冲数。 */
#define UI_ENCODER_PULSES_PER_SWITCH 1           // 1：每切换一个UI项需要1个有效脉冲（现硬件每转一下1个脉冲）；3：每切换一个UI项需要3个有效脉冲

#if UI_ENCODER_PULSES_PER_SWITCH < 1
#error "UI_ENCODER_PULSES_PER_SWITCH must be at least 1"
#endif

/* 压制通道传感器尚未实现，暂时跳过此项检查。 */
#define MYLVGL_ASSUME_PRESS_CHANNEL_PRESENT 1   // 1：压制通道默认通过；0：使用预留接口传入的真实在位状态

static int selection_rotation_accumulator;
static volatile uint16_t display_frame_rate = MODBUS_DISPLAY_FPS_DEFAULT;
static volatile bool machine_check_coagulant_low_level;
static volatile bool press_parts_channel_present;
static bool tofu_flow_requested;
static bool tofu_flow_seen_running;
static bool tofu_collect_requested;
#if MYLVGL_UI_VALIDATION_MODE != 1
static FuncSecType_t last_external_flow = NOTASK_SEC;
#endif
#if MYLVGL_UI_VALIDATION_MODE == 1
static bool validation_transition_pending;
static TickType_t validation_transition_tick;
#endif

static int encoder_navigation_direction(int physical_direction)
{
    if (physical_direction == MY_ENCODER_FORWARD_DIRECTION) {
        return 1;
    }
    if (physical_direction == MY_ENCODER_REVERSE_DIRECTION) {
        return -1;
    }
    return 0;
}

#if MYLVGL_UI_VALIDATION_MODE != 1
static bool stop_running_flow(void);
#endif

#if MYLVGL_UI_VALIDATION_MODE == 1
static void schedule_validation_transition(uint32_t delay_ms)
{
    validation_transition_tick =
        xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);
    validation_transition_pending = true;
}

static bool validation_transition_elapsed(void)
{
    return validation_transition_pending &&
           (int32_t)(xTaskGetTickCount() - validation_transition_tick) >= 0;
}
#endif

void MachineCheckSetCoagulantLowLevel(bool detected)
{
    machine_check_coagulant_low_level = detected;
}

void PressPartsCheckSetChannelPresent(bool detected)
{
    press_parts_channel_present = detected;
}

bool UiValidationModeEnabled(void)
{
    return MYLVGL_UI_VALIDATION_MODE == 1;
}

void UiFlowConfigure(void)
{
    const bool validation = UiValidationModeEnabled();
    tofu_flow_requested = false;
    tofu_flow_seen_running = false;
    tofu_collect_requested = false;
#if MYLVGL_UI_VALIDATION_MODE != 1
    last_external_flow = NOTASK_SEC;
#endif
#if MYLVGL_UI_VALIDATION_MODE == 1
    validation_transition_pending = false;
#endif
    PlantMilkUiConfigure(validation, validation);
    CleanUiConfigure(validation, validation);
#ifdef ESP_PLATFORM
    ESP_LOGI("UIFlow", "validation=%d, external_sync=%d",
             validation, !validation);
#endif
}

static void refresh_machine_check_sensors(void)
{
    if (ui_get_current_page() != UI_PAGE_MACHINE_CHECK) {
        return;
    }

#if MYLVGL_UI_VALIDATION_MODE == 1
    const ui_machine_check_sensors_t sensors = {
        .lid_present = true,
        .coagulant_container_present = true,
        .coagulant_low_level = false,
        .water_tank_present = true,
        .water_tank_high_level = false, /* 预留，当前忽略此输入。 */
        .waste_tank_present = true,
    };
#else
    uint16_t inputs;
    const bool valid = DataCenterGetSensorSnapshot(&inputs);
    const ui_machine_check_sensors_t sensors = {
        /* 这些原始在位信号均为低电平有效：0 表示已安装或已闭合。 */
        .lid_present = valid && (inputs & (1U << lidAvail)) == 0U,
        .coagulant_container_present = valid && (inputs & (1U << brineBoxAvail)) == 0U,
        .coagulant_low_level = machine_check_coagulant_low_level,
        .water_tank_present = valid && (inputs & (1U << waterBoxAvail)) == 0U,             //清水箱默认不通过
        // .water_tank_present = true,                                                     //清水箱默认通过
        .water_tank_high_level = false, /* 预留，不读取高液位信号。 */
        .waste_tank_present = valid && (inputs & (1U << wasteBoxAvail)) == 0U,             //黄水箱默认不通过
        // .waste_tank_present = true                                                      //黄水箱默认通过
    };
#endif
    ui_machine_check_update(&sensors);
}

static void refresh_press_parts_sensors(void)
{
    if (ui_get_current_page() != UI_PAGE_PRESS_PARTS_CHECK) {
        return;
    }

#if MYLVGL_UI_VALIDATION_MODE == 1
    const ui_press_parts_sensors_t sensors = {
        .press_head_present = true,
        .press_channel_present = true,
    };
#else
    uint16_t inputs;
    const bool valid = DataCenterGetSensorSnapshot(&inputs);
    const ui_press_parts_sensors_t sensors = {
        .press_head_present = valid && (inputs & (1U << pressHeadAvail)) == 0U,
        .press_channel_present = (MYLVGL_ASSUME_PRESS_CHANNEL_PRESENT == 1) ||
                                 press_parts_channel_present,
    };
#endif
    ui_press_parts_check_update(&sensors);
}

static void refresh_external_flow_page(void)
{
#if MYLVGL_UI_VALIDATION_MODE != 1
    FuncSecType_t flow = GetCurTaskState();
    ui_page_id_t page = ui_get_current_page();
    const bool tofu_running_page = page == UI_PAGE_TOFU_MAKING ||
                                   page == UI_PAGE_STOP_TOFU_CONFIRM ||
                                   page == UI_PAGE_TOFU_STOPPING;
    if (flow != last_external_flow) {
#ifdef ESP_PLATFORM
        ESP_LOGI("UIFlow", "task=%d -> %d, page=%d, tofu_requested=%d",
                 last_external_flow, flow, page, tofu_flow_requested);
#endif
        last_external_flow = flow;
    }

    if (flow == TOUFU_SEC) {
        tofu_flow_requested = true;
        tofu_flow_seen_running = true;
        /* 以实际运行任务和当前页面为准，旧接管标志不能阻止从首页恢复制作页。
         * 已打开的停止确认页和停止等待页保留，避免周期同步覆盖用户操作。 */
        if (!tofu_running_page) {
            ui_show_page(UI_PAGE_TOFU_MAKING);
#ifdef ESP_PLATFORM
            ESP_LOGI("UIFlow", "tofu page=%d -> %d, initialized=%d",
                     page, ui_get_current_page(), ui_is_initialized());
#endif
        }
    } else if (flow != NOTASK_SEC || !tofu_running_page) {
        /* 在其他页面结束或切换任务时清除旧标志；运行页的结束跳转由后续处理。 */
        tofu_flow_requested = false;
        tofu_flow_seen_running = false;
    }

    if (flow == MILK_SEC || flow == SOYMILK_SEC) {
        PlantMilkUiFollowRunningFlow();
    } else if (flow == SELFCLEAN_SEC) {
        CleanUiFollowRunningFlow();
    }
#endif
}

static void refresh_tofu_flow_page(void)
{
    if (tofu_collect_requested) {
        if (!ui_tofu_ready_is_collecting()) {
            tofu_collect_requested = false;
            ui_show_page(UI_PAGE_HOME);
        }
        return;
    }
    ui_page_id_t page = ui_get_current_page();

#if MYLVGL_UI_VALIDATION_MODE == 1
    if (!validation_transition_elapsed()) {
        return;
    }

    if (page == UI_PAGE_TOFU_MAKING || page == UI_PAGE_STOP_TOFU_CONFIRM) {
        validation_transition_pending = false;
        tofu_flow_requested = false;
        tofu_flow_seen_running = false;
        ui_show_page(UI_PAGE_TOFU_READY);
    } else if (page == UI_PAGE_TOFU_STOPPING) {
        validation_transition_pending = false;
        tofu_flow_requested = false;
        tofu_flow_seen_running = false;
        ui_show_page(UI_PAGE_TOFU_STOPPED);
    }
#else
    FuncSecType_t flow = GetCurTaskState();

    if (tofu_flow_requested && flow == TOUFU_SEC) {
        tofu_flow_seen_running = true;
    }

    if ((page == UI_PAGE_TOFU_MAKING || page == UI_PAGE_STOP_TOFU_CONFIRM) &&
        tofu_flow_seen_running &&
        flow == NOTASK_SEC) {
        tofu_flow_requested = false;
        tofu_flow_seen_running = false;
        ui_show_page(UI_PAGE_TOFU_READY);
    } else if (page == UI_PAGE_TOFU_STOPPING && flow == NOTASK_SEC) {
        tofu_flow_requested = false;
        tofu_flow_seen_running = false;
        ui_show_page(UI_PAGE_TOFU_STOPPED);
    }
#endif
}

/**
 * @brief 设置 LVGL 显示刷新帧率。
 * @param fps 目标帧率，必须位于 ModbusMap 定义的允许范围内。
 * @return 设置成功返回 true，参数越界返回 false。
 */
bool DisplayFrameRateSet(uint16_t fps)
{
    if (fps < MODBUS_DISPLAY_FPS_MIN || fps > MODBUS_DISPLAY_FPS_MAX) {
        return false;
    }
    display_frame_rate = fps;
    return true;
}

/**
 * @brief 获取当前配置的 LVGL 显示刷新帧率。
 * @return 当前帧率值。
 */
uint16_t DisplayFrameRateGet(void)
{
    return display_frame_rate;
}

/**
 * @brief 根据逻辑方向移动焦点；圆盘页循环，普通按钮页到首尾后停止。
 * @param direction 本次编码器的物理方向。
 */
static void move_selection(int direction)
{
    /* 所有位置统一使用同一方向规则：正转选择逻辑上的下一项或下方项，
     * 反转选择上一项或上方项。 */
    const int navigation_direction = encoder_navigation_direction(direction);
    if (navigation_direction == 0) {
        return;
    }

    if (ui_plantmilk_is_page(ui_get_current_page())) {
        ui_plantmilk_move_selection(navigation_direction);
        return;
    }
    if (ui_clean_is_page(ui_get_current_page())) {
        ui_clean_move_selection(navigation_direction);
        return;
    }

    if (ui_get_current_page() == UI_PAGE_HOME) {
        ui_home_item_t item = ui_get_selected_home_item();
        if (navigation_direction > 0) {
            item = (ui_home_item_t)(((uint8_t)item + 1U) % UI_HOME_ITEM_COUNT);
        } else {
            item = item == UI_HOME_ITEM_TOFU
                       ? (ui_home_item_t)(UI_HOME_ITEM_COUNT - 1U)
                       : (ui_home_item_t)((uint8_t)item - 1U);
        }
        ui_select_home_item(item);
        return;
    }

    if (ui_get_current_page() == UI_PAGE_TOFU_FIRMNESS) {
        ui_tofu_firmness_t firmness = ui_get_selected_tofu_firmness();
        /* 圆盘循环包含 Back，保证两种方向均可返回主页。 */
        if (ui_is_tofu_firmness_back_selected()) {
            ui_select_tofu_firmness(navigation_direction > 0
                                        ? UI_TOFU_FIRMNESS_SOFT
                                        : UI_TOFU_FIRMNESS_EXTRA_FIRM);
            return;
        }

        if (navigation_direction > 0) {
            if (firmness == UI_TOFU_FIRMNESS_EXTRA_FIRM) {
                ui_select_tofu_firmness_back(true);
                return;
            }
            firmness = (ui_tofu_firmness_t)((uint8_t)firmness + 1U);
        } else {
            if (firmness == UI_TOFU_FIRMNESS_SOFT) {
                ui_select_tofu_firmness_back(true);
                return;
            }
            firmness = (ui_tofu_firmness_t)((uint8_t)firmness - 1U);
        }
        ui_select_tofu_firmness(firmness);
        return;
    }

    if (ui_get_current_page() == UI_PAGE_FIRM_TOFU) {
        if (ui_is_firm_tofu_editing()) {
            ui_adjust_firm_tofu_value((int8_t)navigation_direction);
            return;
        }

        int control = (int)ui_get_selected_firm_tofu_control() +
                      navigation_direction;
        if (control < 0) {
            control = UI_FIRM_TOFU_CONTROL_BACK;
        } else if (control >= UI_FIRM_TOFU_CONTROL_COUNT) {
            control = UI_FIRM_TOFU_CONTROL_COUNT - 1;
        }
        ui_select_firm_tofu_control((ui_firm_tofu_control_t)control);
        return;
    }

    if (ui_get_current_page() == UI_PAGE_MORE_OPTIONS) {
        if (ui_is_more_options_editing()) {
            ui_adjust_more_options_value((int8_t)navigation_direction);
            return;
        }

        int control = (int)ui_get_selected_more_options_control() +
                      navigation_direction;
        if (control < 0) {
            control = UI_MORE_OPTIONS_CONTROL_BACK;
        } else if (control >= UI_MORE_OPTIONS_CONTROL_COUNT) {
            control = UI_MORE_OPTIONS_CONTROL_COUNT - 1;
        }
        ui_select_more_options_control((ui_more_options_control_t)control);
        return;
    }

    if (ui_get_current_page() == UI_PAGE_ADD_INGREDIENTS) {
        ui_add_ingredients_control_t control =
            navigation_direction > 0 ? UI_ADD_INGREDIENTS_CONTROL_CONTINUE
                                     : UI_ADD_INGREDIENTS_CONTROL_BACK;
        ui_select_add_ingredients_control(control);
        return;
    }

    if (ui_get_current_page() == UI_PAGE_MACHINE_CHECK) {
        ui_machine_check_control_t control =
            navigation_direction > 0 ? UI_MACHINE_CHECK_CONTROL_CONTINUE
                                     : UI_MACHINE_CHECK_CONTROL_BACK;
        ui_select_machine_check_control(control);
        return;
    }

    if (ui_get_current_page() == UI_PAGE_PRESS_PARTS_CHECK) {
        ui_press_parts_control_t control =
            navigation_direction > 0 ? UI_PRESS_PARTS_CONTROL_START
                                     : UI_PRESS_PARTS_CONTROL_BACK;
        ui_select_press_parts_control(control);
        return;
    }

    if (ui_get_current_page() == UI_PAGE_TOFU_READY) {
        ui_tofu_ready_control_t control =
            navigation_direction > 0 ? UI_TOFU_READY_CONTROL_CLEAN
                                     : UI_TOFU_READY_CONTROL_COLLECT;
        ui_select_tofu_ready_control(control);
        return;
    }

    if (ui_get_current_page() == UI_PAGE_STOP_TOFU_CONFIRM) {
        /* Keep Making -> Stop，端点保持当前选择，不循环。 */
        ui_stop_tofu_control_t control =
            navigation_direction > 0 ? UI_STOP_TOFU_CONTROL_STOP
                                     : UI_STOP_TOFU_CONTROL_KEEP;
        ui_select_stop_tofu_control(control);
    }
}

static void handle_rotation(int change)
{
    if (ui_tofu_ready_is_collecting()) return;
    ui_page_id_t page = ui_get_current_page();
    if (page != UI_PAGE_HOME && page != UI_PAGE_TOFU_FIRMNESS &&
        page != UI_PAGE_FIRM_TOFU && page != UI_PAGE_MORE_OPTIONS &&
        page != UI_PAGE_ADD_INGREDIENTS &&
        page != UI_PAGE_MACHINE_CHECK &&
        page != UI_PAGE_PRESS_PARTS_CHECK &&
        page != UI_PAGE_TOFU_READY &&
        page != UI_PAGE_STOP_TOFU_CONFIRM &&
        !ui_plantmilk_is_page(page) && !ui_clean_is_page(page)) {
        selection_rotation_accumulator = 0;
        return;
    }

    selection_rotation_accumulator += change;
    while (selection_rotation_accumulator >=
           UI_ENCODER_PULSES_PER_SWITCH) {
        selection_rotation_accumulator -= UI_ENCODER_PULSES_PER_SWITCH;
        move_selection(1);
    }
    while (selection_rotation_accumulator <=
           -UI_ENCODER_PULSES_PER_SWITCH) {
        selection_rotation_accumulator += UI_ENCODER_PULSES_PER_SWITCH;
        move_selection(-1);
    }
}

static void show_firm_tofu_page(void)
{
    /* 每次重新进入时默认选中豆腐用量，
     * 同时保留用户此前选择的用量和凝固剂参数。 */
    ui_select_firm_tofu_control(UI_FIRM_TOFU_CONTROL_AMOUNT);
    ui_show_page(UI_PAGE_FIRM_TOFU);
}

/**
 * @brief 旋钮短按时处理当前选中的 UI 入口。
 *
 * 从公共首页进入豆腐功能或独立的植物奶页面。
 */
static void handle_short_press(void)
{
    if (ui_tofu_ready_is_collecting()) return;
    if (ui_clean_is_page(ui_get_current_page())) {
        CleanUiHandleShortPress();
        return;
    }
    if (ui_plantmilk_is_page(ui_get_current_page())) {
        PlantMilkUiHandleShortPress();
        return;
    }
    if (ui_get_current_page() == UI_PAGE_HOME &&
        ui_get_selected_home_item() == UI_HOME_ITEM_TOFU) {
        ui_show_page(UI_PAGE_TOFU_FIRMNESS);
    } else if (ui_get_current_page() == UI_PAGE_HOME &&
               ui_get_selected_home_item() == UI_HOME_ITEM_MILK) {
        PlantMilkUiEnter();
    } else if (ui_get_current_page() == UI_PAGE_HOME &&
               ui_get_selected_home_item() == UI_HOME_ITEM_CLEAN) {
        CleanUiEnter();
    } else if (ui_get_current_page() == UI_PAGE_TOFU_FIRMNESS) {
        if (ui_is_tofu_firmness_back_selected()) {
            ui_select_tofu_firmness_back(false);
            ui_show_page(UI_PAGE_HOME);
        } else {
            show_firm_tofu_page();
        }
    } else if (ui_get_current_page() == UI_PAGE_FIRM_TOFU) {
        ui_firm_tofu_control_t control =
            ui_get_selected_firm_tofu_control();
        if (control == UI_FIRM_TOFU_CONTROL_BACK) {
            ui_show_page(UI_PAGE_TOFU_FIRMNESS);
        } else if (control == UI_FIRM_TOFU_CONTROL_AMOUNT ||
                   control == UI_FIRM_TOFU_CONTROL_COAGULANT) {
            ui_set_firm_tofu_editing(!ui_is_firm_tofu_editing());
        } else if (control == UI_FIRM_TOFU_CONTROL_MORE_OPTIONS) {
            ui_select_more_options_control(
                UI_MORE_OPTIONS_CONTROL_INGREDIENT);
            ui_show_page(UI_PAGE_MORE_OPTIONS);
        } else if (control == UI_FIRM_TOFU_CONTROL_CONTINUE) {
            ui_show_page(UI_PAGE_ADD_INGREDIENTS);
        }
    } else if (ui_get_current_page() == UI_PAGE_MORE_OPTIONS) {
        ui_more_options_control_t control =
            ui_get_selected_more_options_control();
        if (control == UI_MORE_OPTIONS_CONTROL_BACK ||
            control == UI_MORE_OPTIONS_CONTROL_CONFIRM) {
            show_firm_tofu_page();
        } else if (control == UI_MORE_OPTIONS_CONTROL_INGREDIENT ||
                   control == UI_MORE_OPTIONS_CONTROL_COAGULANT ||
                   control == UI_MORE_OPTIONS_CONTROL_AMOUNT) {
            ui_set_more_options_editing(!ui_is_more_options_editing());
        } else if (control == UI_MORE_OPTIONS_CONTROL_RESTORE) {
            ui_restore_default_tofu_options();
            ui_show_page(UI_PAGE_MORE_OPTIONS);
        }
    } else if (ui_get_current_page() == UI_PAGE_ADD_INGREDIENTS) {
        if (ui_get_selected_add_ingredients_control() ==
            UI_ADD_INGREDIENTS_CONTROL_BACK) {
            show_firm_tofu_page();
        } else {
            ui_show_page(UI_PAGE_MACHINE_CHECK);
            refresh_machine_check_sensors();
        }
    } else if (ui_get_current_page() == UI_PAGE_MACHINE_CHECK) {
        ui_machine_check_control_t selected = ui_get_selected_machine_check_control();
        refresh_machine_check_sensors();
        if (selected == UI_MACHINE_CHECK_CONTROL_BACK) {
            ui_show_page(UI_PAGE_ADD_INGREDIENTS);
        } else if (ui_machine_check_can_continue()) {
            ui_show_page(UI_PAGE_PRESS_PARTS_CHECK);
            refresh_press_parts_sensors();
        }
    } else if (ui_get_current_page() == UI_PAGE_PRESS_PARTS_CHECK) {
        ui_press_parts_control_t selected = ui_get_selected_press_parts_control();
        refresh_press_parts_sensors();
        if (selected == UI_PRESS_PARTS_CONTROL_BACK) {
            ui_show_page(UI_PAGE_MACHINE_CHECK);
            refresh_machine_check_sensors();
        } else if (ui_press_parts_check_can_start() &&
                   GetCurTaskState() == NOTASK_SEC) {
#if MYLVGL_UI_VALIDATION_MODE == 1
            tofu_flow_requested = true;
            tofu_flow_seen_running = true;
            schedule_validation_transition(MYLVGL_VALIDATION_MAKING_MS);
            ui_show_page(UI_PAGE_TOFU_MAKING);
#else
            if (FuncTypeGroup == NULL || FuncOrderGroup == NULL) {
                return;
            }
            tofu_flow_requested = true;
            tofu_flow_seen_running = false;
            /* FunctionSelection 根据事件位的位置索引任务表。
             * 此处从 TOUFU_SEC 推导事件位，确保 UI 启动豆腐任务，
             * 因为旧的 TouFu_bit/Milk_bit 宏顺序
             * 与 FuncSecType_t 中的顺序不同。 */
            xEventGroupSetBits(FuncTypeGroup,
                               (EventBits_t)(1U << TOUFU_SEC));
            xEventGroupSetBits(FuncOrderGroup, FuncStart_bit);
            ui_show_page(UI_PAGE_TOFU_MAKING);
#endif
        }
    } else if (ui_get_current_page() == UI_PAGE_TOFU_MAKING) {
        ui_show_page(UI_PAGE_STOP_TOFU_CONFIRM);
    } else if (ui_get_current_page() == UI_PAGE_TOFU_READY) {
        if (ui_get_selected_tofu_ready_control() ==
            UI_TOFU_READY_CONTROL_CLEAN) {
            ui_clean_prepare(UI_CLEAN_DEEP);
        } else {
#if MYLVGL_UI_VALIDATION_MODE != 1
            if (GetCurTaskState() != NOTASK_SEC) return;
            if (ui_tofu_ready_collect()) tofu_collect_requested = true;
#else
            ui_show_page(UI_PAGE_HOME);
#endif
        }
    } else if (ui_get_current_page() == UI_PAGE_STOP_TOFU_CONFIRM) {
        if (ui_get_selected_stop_tofu_control() ==
            UI_STOP_TOFU_CONTROL_KEEP) {
            ui_show_page(UI_PAGE_TOFU_MAKING);
#if MYLVGL_UI_VALIDATION_MODE == 1
        } else {
            schedule_validation_transition(MYLVGL_VALIDATION_STOPPING_MS);
            ui_tofu_stopping_set_complete(false);
            ui_show_page(UI_PAGE_TOFU_STOPPING);
        }
#else
        } else if (stop_running_flow()) {
            ui_tofu_stopping_set_complete(false);
            ui_show_page(UI_PAGE_TOFU_STOPPING);
        } else if (GetCurTaskState() == NOTASK_SEC) {
            ui_show_page(UI_PAGE_TOFU_STOPPED);
        }
#endif
    } else if (ui_get_current_page() == UI_PAGE_TOFU_STOPPED) {
        ui_clean_prepare(UI_CLEAN_DEEP);
    }
}

/**
 * @brief 请求停止当前正在运行的功能流程。
 * @return 已发送停止事件返回 true；没有运行任务时返回 false。
 */
#if MYLVGL_UI_VALIDATION_MODE != 1
static bool stop_running_flow(void)
{
    FuncSecType_t flow = GetCurTaskState();
    FuncSecOrder_t order = GetCurTaskOrder();

    if (flow == NOTASK_SEC || order == FUNCSEC_STOP ||
        FuncTypeGroup == NULL || FuncOrderGroup == NULL) {
        return false;
    }

    xEventGroupSetBits(FuncTypeGroup, (EventBits_t)(1U << flow));
    xEventGroupSetBits(FuncOrderGroup, FuncStop_bit);
    return true;
}
#endif

/**
 * @brief 处理长按事件；运行中屏蔽长按，保留目标分支的输入策略。
 */
static void handle_long_press(void)
{
    if (ui_tofu_ready_is_collecting()) return;
    /* 保留本分支的处理规则：工艺运行期间忽略旋钮长按。
     * 仍可通过界面上的停止按钮或上位机停止工艺。 */
    ui_page_id_t current_page = ui_get_current_page();
    if (GetCurTaskState() != NOTASK_SEC ||
        current_page == UI_PAGE_TOFU_MAKING ||
        current_page == UI_PAGE_STOP_TOFU_CONFIRM ||
        current_page == UI_PAGE_TOFU_STOPPING ||
        current_page == UI_PAGE_PLANTMILK_MAKING ||
        current_page == UI_PAGE_PLANTMILK_STOP_CONFIRM ||
        current_page == UI_PAGE_PLANTMILK_STOPPING ||
        current_page == UI_PAGE_CLEAN_RINSING ||
        current_page == UI_PAGE_CLEAN_CLEANING ||
        current_page == UI_PAGE_CLEAN_STOP_CONFIRM) {
        return;
    }

    if (ui_clean_is_page(ui_get_current_page())) {
        CleanUiHandleLongPress();
        return;
    }
    if (ui_plantmilk_is_page(ui_get_current_page())) {
        PlantMilkUiHandleLongPress();
        return;
    }
#if MYLVGL_UI_VALIDATION_MODE == 1
    ui_page_id_t page = ui_get_current_page();
    if (page == UI_PAGE_TOFU_MAKING ||
        page == UI_PAGE_STOP_TOFU_CONFIRM) {
        schedule_validation_transition(MYLVGL_VALIDATION_STOPPING_MS);
        ui_tofu_stopping_set_complete(false);
        ui_show_page(UI_PAGE_TOFU_STOPPING);
    }
#else
    bool tofu_flow_active = GetCurTaskState() == TOUFU_SEC;

    if (stop_running_flow() && tofu_flow_active) {
        ui_tofu_stopping_set_complete(false);
        ui_show_page(UI_PAGE_TOFU_STOPPING);
    }
#endif
}

/**
 * @brief 动画结束后强制刷新活动屏幕并删除一次性定时器。
 * @param timer 触发回调的 LVGL 定时器。
 */
static void animation_refresh_cb(lv_timer_t *timer)
{
    lv_obj_invalidate(lv_scr_act());
    lv_timer_del(timer);
}

/**
 * @brief 创建一次性定时器，在动画结束后补充一次屏幕刷新。
 * @param animation_ms 动画预计持续时间，单位为毫秒。
 */
static void refresh_after_animation(uint32_t animation_ms)
{
    lv_timer_t *timer = lv_timer_create(animation_refresh_cb,
                                        animation_ms + 20U, NULL);
    lv_timer_set_repeat_count(timer, 1);
}

/**
 * @brief LVGL UI 任务，处理时基、旋钮、按键、帧率和界面刷新。
 * @param arg 非 NULL 时先绘制首帧，再等待应用完成初始化的任务通知。
 */
void lvUIshowTask(void *arg)
{
#ifdef ESP_PLATFORM
    if (arg != NULL) {
        /* 在本任务的 8 KiB 栈上立即绘制，避免使用 app_main 的小栈，
         * 也不等待默认 10 FPS 定时器和其他业务任务的首次调度。 */
        lv_refr_now(lv_disp_get_default());
        while (!lv_port_disp_first_frame_ready()) {
            lv_port_disp_service_backlight();
            if (!lv_port_disp_first_frame_ready()) {
                vTaskDelay(1);
            }
        }
        /* 首页可见后暂停交互。应用创建完任务和事件组才发通知，
         * 因此不会访问尚未初始化的业务资源或与启动测试并发操作 LVGL。 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
#endif
    (void)arg;
    UiFlowConfigure();

    int last_enc_counter = encCounter;
    uint16_t applied_fps = 0U;
    TickType_t previous_tick = xTaskGetTickCount();

    refresh_after_animation(100U);

    while (1) {
        TickType_t current_tick = xTaskGetTickCount();
        uint32_t elapsed_ms = (uint32_t)(current_tick - previous_tick) *
                              portTICK_PERIOD_MS;
        previous_tick = current_tick;
        if (elapsed_ms != 0U) {
            lv_tick_inc(elapsed_ms);
        }

        int current_enc_counter = encCounter;
        if (current_enc_counter != last_enc_counter) {
            int change = current_enc_counter - last_enc_counter;
            last_enc_counter = current_enc_counter;
            handle_rotation(change);
        }

        if (isLongPress) {
            isLongPress = false;
            isPress = false;
            handle_long_press();
        } else if (isPress) {
            isPress = false;
            selection_rotation_accumulator = 0;
            handle_short_press();
        }

        refresh_machine_check_sensors();
        refresh_press_parts_sensors();
        refresh_external_flow_page();
        refresh_tofu_flow_page();
        PlantMilkUiTick();
        CleanUiTick();

        uint16_t fps = DisplayFrameRateGet();
        if (fps != applied_fps) {
            uint32_t frame_period_ms = (1000U + fps - 1U) / fps;
            lv_disp_t *display = lv_disp_get_default();
            if (display != NULL) {
                lv_timer_set_period(_lv_disp_get_refr_timer(display),
                                    frame_period_ms);
                applied_fps = fps;
            }
        }

        lv_task_handler();
#ifdef ESP_PLATFORM
        lv_port_disp_service_backlight();
#endif
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
