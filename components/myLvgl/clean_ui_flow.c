#include "clean_ui_flow.h"
#include "myLvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "Clean/ui_clean.h"
#include "DataCenter.h"
#include "FunctionSelection.h"

extern EventGroupHandle_t SelfCleanEventGroup;

static bool validation;
static bool simulated_sensors;
static bool requested;
static bool seen_running;
static bool stopping;
static ui_clean_mode_t active_mode;
static TickType_t started_at;
static TickType_t transition_at;

static void refresh_sensors(void)
{
    uint16_t inputs = 0U;
    const bool valid = simulated_sensors || DataCenterGetSensorSnapshot(&inputs);
    ui_clean_sensor_update(valid && (inputs & (1U << lidAvail)) == 0U,
                           valid && (inputs & (1U << waterBoxAvail)) == 0U);
}

static bool send_order(EventBits_t order)
{
    if (FuncTypeGroup == NULL || FuncOrderGroup == NULL) return false;
    xEventGroupSetBits(FuncTypeGroup, (EventBits_t)(1U << SELFCLEAN_SEC));
    xEventGroupSetBits(FuncOrderGroup, order);
    return true;
}

static bool request_start(ui_clean_mode_t mode)
{
    if (GetCurTaskState() != NOTASK_SEC) return false;
    if (!validation) {
        if (SelfCleanEventGroup != NULL) {
            if (mode == UI_CLEAN_DEEP)
                xEventGroupSetBits(SelfCleanEventGroup, SelfClean_ModeBit);
            else
                xEventGroupClearBits(SelfCleanEventGroup, SelfClean_ModeBit);
        }
        if (!send_order(FuncStart_bit)) return false;
    }
    requested = true;
    seen_running = validation;
    stopping = false;
    active_mode = mode;
    started_at = xTaskGetTickCount();
    transition_at = started_at + pdMS_TO_TICKS(MYLVGL_VALIDATION_MAKING_MS);
    return true;
}

void CleanUiFollowRunningFlow(void)
{
    if (validation || requested || GetCurTaskState() != SELFCLEAN_SEC) return;
    EventBits_t mode_bits = SelfCleanEventGroup != NULL
                               ? xEventGroupGetBits(SelfCleanEventGroup) : 0U;
    active_mode = (mode_bits & SelfClean_ModeBit) != 0U ? UI_CLEAN_DEEP : UI_CLEAN_RINSE;
    requested = seen_running = true;
    stopping = false;
    started_at = xTaskGetTickCount();
    ui_clean_show_running(active_mode);
}

static ui_clean_flow_state_t request_stop(void)
{
    if (!requested) return UI_CLEAN_FLOW_IDLE;
    if (!validation) {
        if (GetCurTaskState() != SELFCLEAN_SEC) {
            return seen_running && GetCurTaskState() == NOTASK_SEC
                       ? UI_CLEAN_FLOW_STOPPED : UI_CLEAN_FLOW_IDLE;
        }
        if (!send_order(FuncStop_bit)) return UI_CLEAN_FLOW_IDLE;
    }
    stopping = true;
    transition_at = xTaskGetTickCount() + pdMS_TO_TICKS(MYLVGL_VALIDATION_STOPPING_MS);
    return UI_CLEAN_FLOW_STOPPING;
}

static ui_clean_flow_state_t finish(ui_clean_flow_state_t state)
{
    requested = seen_running = stopping = false;
    return state;
}

static ui_clean_flow_state_t poll_state(void)
{
    if (!requested) return UI_CLEAN_FLOW_IDLE;
    if (validation) {
        if ((int32_t)(xTaskGetTickCount() - transition_at) >= 0)
            return finish(stopping ? UI_CLEAN_FLOW_STOPPED : UI_CLEAN_FLOW_READY);
        return stopping ? UI_CLEAN_FLOW_STOPPING : UI_CLEAN_FLOW_RUNNING;
    }
    FuncSecType_t current = GetCurTaskState();
    if (current == SELFCLEAN_SEC) seen_running = true;
    if (stopping && current == NOTASK_SEC) return finish(UI_CLEAN_FLOW_STOPPED);
    if (!stopping && seen_running && current == NOTASK_SEC)
        return finish(UI_CLEAN_FLOW_READY);
    return stopping ? UI_CLEAN_FLOW_STOPPING : UI_CLEAN_FLOW_RUNNING;
}

static uint16_t remaining_minutes(ui_clean_mode_t mode)
{
    const uint16_t total = mode == UI_CLEAN_RINSE ? 2U : 25U;
    if (validation || !requested || mode != active_mode) return total;
    uint32_t elapsed = (uint32_t)(xTaskGetTickCount() - started_at) *
                       portTICK_PERIOD_MS / 1000U;
    uint32_t total_seconds = (uint32_t)total * 60U;
    if (elapsed >= total_seconds) return 0U;
    return (uint16_t)((total_seconds - elapsed + 59U) / 60U);
}

static const ui_clean_flow_ops_t flow_ops = {
    .refresh_sensors = refresh_sensors,
    .start = request_start,
    .stop = request_stop,
    .poll = poll_state,
    .remaining_minutes = remaining_minutes,
};

void CleanUiConfigure(bool validation_mode, bool assume_sensors_present)
{
    validation = validation_mode;
    simulated_sensors = assume_sensors_present;
    requested = seen_running = stopping = false;
    active_mode = UI_CLEAN_RINSE;
    ui_clean_bind_flow(&flow_ops);
}

void CleanUiEnter(void) { ui_clean_enter(); }
void CleanUiHandleShortPress(void) { ui_clean_handle_short_press(); }
void CleanUiHandleLongPress(void) { ui_clean_handle_long_press(); }
void CleanUiTick(void) { ui_clean_tick(); }
