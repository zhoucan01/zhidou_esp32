#include "plantmilk_ui_flow.h"
#include "myLvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "PlantMilk/ui_plantmilk.h"
#include "FunctionSelection.h"
#include "DataCenter.h"

/* 第二个功能独立维护制作状态，避免影响豆腐功能的状态和跳转。 */
static bool validation;
static bool simulated_sensors;
static bool requested;
static FuncSecType_t active_flow = MILK_SEC;
static bool seen_running;
static bool stopping;
static TickType_t started_at;
static TickType_t transition_at;

static bool request_start(void);
static ui_plantmilk_flow_state_t request_stop(void);
static void refresh_sensors(void);
static ui_plantmilk_flow_state_t poll_state(void);
static uint16_t remaining_minutes(void);

/* 这里只提供硬件与任务能力，不决定逻辑页面的跳转目标。 */
static const ui_plantmilk_flow_ops_t flow_ops = {
    .refresh_sensors = refresh_sensors,
    .start = request_start,
    .stop = request_stop,
    .poll = poll_state,
    .remaining_minutes = remaining_minutes,
};

/**
 * @brief 设置第二个功能的运行模式，清空流程状态并注册页面所需的任务回调。
 * @param validation_mode true：仅验证 UI，不下发硬件命令；false：使用实际制作任务。
 * @param assume_sensors_present true：模拟盖子、水箱均在位；false：读取实际传感器。
 */
void PlantMilkUiConfigure(bool validation_mode, bool assume_sensors_present)
{
    validation = validation_mode;
    simulated_sensors = assume_sensors_present;
    requested = seen_running = stopping = false;
    active_flow = MILK_SEC;
    ui_plantmilk_bind_flow(&flow_ops);
}

/**
 * @brief 向功能调度层提交目标任务及其操作命令。
 * @param flow 目标任务编号，转换为对应的任务选择事件位。
 * @param order 启动、停止等操作的事件位。
 * @return 事件组可用且已提交命令返回 true；事件组未初始化返回 false。
 * @note 返回 true 仅表示命令已提交，实际执行状态需通过任务状态接口确认。
 */
static bool send_order(FuncSecType_t flow, EventBits_t order)
{
    if (FuncTypeGroup == NULL || FuncOrderGroup == NULL) return false;
    xEventGroupSetBits(FuncTypeGroup, (EventBits_t)(1U << flow));
    xEventGroupSetBits(FuncOrderGroup, order);
    return true;
}

/**
 * @brief 刷新盖子和水箱在位状态，并通知 UI 更新检查结果。
 * @note 实际在位输入为低电平有效；模拟模式直接视为在位，不读取水箱高液位。
 */
static void refresh_sensors(void)
{
    uint16_t inputs = 0U;
    const bool valid = simulated_sensors || DataCenterGetSensorSnapshot(&inputs);
    ui_plantmilk_check_update(valid && (inputs & (1U << lidAvail)) == 0U,
                             valid && (inputs & (1U << waterBoxAvail)) == 0U);
}

/**
 * @brief 清除本次制作的运行标记，并向页面返回结束结果。
 * @param state 制作完成或停止完成的结果状态。
 * @return 原样返回 state，由当前逻辑页面决定跳转目标。
 */
static ui_plantmilk_flow_state_t finish(ui_plantmilk_flow_state_t state)
{
    requested = seen_running = stopping = false;
    return state;
}

/**
 * @brief 从公共首页进入第二个功能，将入口处理交给配方选择页。
 * @note 选择页负责退出参数编辑、保留当前配方并显示页面。
 */
void PlantMilkUiEnter(void)
{
    ui_plantmilk_select_enter();
}

/**
 * @brief 在任务空闲时申请制作，并记录开始时间及验证模式的完成时间。
 * @return 启动命令已提交或验证流程已建立返回 true；任务忙或提交失败返回 false。
 * @note 正常模式启动现有 MilkTask；验证模式仅建立 5 秒的模拟制作流程。
 */
void PlantMilkUiFollowRunningFlow(void)
{
    FuncSecType_t current = GetCurTaskState();
    if (validation || (current != MILK_SEC && current != SOYMILK_SEC) ||
        (requested && active_flow == current)) {
        return;
    }
    active_flow = current;
    requested = seen_running = true;
    stopping = false;
    started_at = xTaskGetTickCount();
    if (current == SOYMILK_SEC) ui_plantmilk_select_recipe(UI_PM_SOY);
    ui_show_page(UI_PAGE_PLANTMILK_MAKING);
}

static bool request_start(void)
{
    if (GetCurTaskState() != NOTASK_SEC) return false;
    /* 沿用现有任务命令；MilkTask 仍按固定工艺执行，暂未使用 UI 的工艺参数。 */
    if (!validation && !send_order(MILK_SEC, FuncStart_bit)) return false;
    active_flow = MILK_SEC;
    requested = true;
    seen_running = validation;
    stopping = false;
    started_at = xTaskGetTickCount();
    transition_at = started_at + pdMS_TO_TICKS(MYLVGL_VALIDATION_MAKING_MS);
    return true;
}

/**
 * @brief 为本次制作申请停止，并返回供页面判断的停止状态。
 * @return 已申请停止返回 STOPPING；已观察到任务运行且当前已结束返回 STOPPED；
 *         无制作请求、任务尚未运行或命令提交失败时返回 IDLE，保持当前页面。
 * @note 正常模式提交停止命令后等待任务结束；验证模式模拟等待 2 秒。
 */
static ui_plantmilk_flow_state_t request_stop(void)
{
    if (!requested) return UI_PM_FLOW_IDLE;
    if (!validation) {
        if (GetCurTaskState() != active_flow) {
            if (seen_running && GetCurTaskState() == NOTASK_SEC) {
                return finish(UI_PM_FLOW_STOPPED);
            }
            return UI_PM_FLOW_IDLE;
        }
        if (!send_order(active_flow, FuncStop_bit)) return UI_PM_FLOW_IDLE;
    }
    stopping = true;
    transition_at = xTaskGetTickCount() + pdMS_TO_TICKS(MYLVGL_VALIDATION_STOPPING_MS);
    return UI_PM_FLOW_STOPPING;
}

/**
 * @brief 将旋钮短按事件交给当前逻辑页面，由该页面处理按钮及页面跳转。
 */
void PlantMilkUiHandleShortPress(void)
{
    ui_plantmilk_handle_short_press();
}

/**
 * @brief 将旋钮长按事件交给当前逻辑页面；未注册长按处理的页面不响应。
 */
void PlantMilkUiHandleLongPress(void)
{
    ui_plantmilk_handle_long_press();
}

/**
 * @brief 由 UI 主循环周期调用，分发当前页面的传感器刷新或任务状态检查。
 */
void PlantMilkUiTick(void)
{
    ui_plantmilk_tick();
}

/**
 * @brief 查询本次制作进度；正常模式读取任务状态，验证模式判断预设等待时间。
 * @return IDLE：未申请制作；RUNNING：等待运行或制作中；STOPPING：等待停止；
 *         READY：制作完成；STOPPED：停止完成。
 * @note 正常完成须先观察到 MilkTask 运行，再确认任务空闲，避免启动时误判完成。
 *       返回 READY 或 STOPPED 时会清除流程标记，具体页面跳转由调用页面处理。
 */
static ui_plantmilk_flow_state_t poll_state(void)
{
    if (!requested) return UI_PM_FLOW_IDLE;
    TickType_t now = xTaskGetTickCount();
    if (validation) {
        if ((int32_t)(now - transition_at) >= 0)
            return finish(stopping ? UI_PM_FLOW_STOPPED : UI_PM_FLOW_READY);
        return stopping ? UI_PM_FLOW_STOPPING : UI_PM_FLOW_RUNNING;
    }
    FuncSecType_t flow = GetCurTaskState();
    if (flow == active_flow) seen_running = true;
    if (stopping && flow == NOTASK_SEC) return finish(UI_PM_FLOW_STOPPED);
    if (!stopping && seen_running && flow == NOTASK_SEC) return finish(UI_PM_FLOW_READY);
    return stopping ? UI_PM_FLOW_STOPPING : UI_PM_FLOW_RUNNING;
}

/**
 * @brief 根据固定工艺的预计总时长计算剩余分钟数，秒数不足一分钟时向上取整。
 * @return 正常制作按 800 秒总时长估算，超时返回 0；验证模式或未申请制作返回 14。
 * @note 该结果仅用于显示，不能作为实际制作完成的判断条件。
 */
static uint16_t remaining_minutes(void)
{
    if (validation || !requested) return 14;
    /* 现有固定工艺约 13 分 20 秒，此值仅用于估算显示，不决定制作是否完成。 */
    uint32_t elapsed_seconds = (uint32_t)(xTaskGetTickCount() - started_at) * portTICK_PERIOD_MS / 1000U;
    uint32_t seconds = elapsed_seconds >= 800U ? 0U : 800U - elapsed_seconds;
    return (uint16_t)((seconds + 59U) / 60U);
}
