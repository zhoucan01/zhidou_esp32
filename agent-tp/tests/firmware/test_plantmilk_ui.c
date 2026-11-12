/* 使用真实 LVGL、页面、解码与流程代码，仅模拟 RTOS 时间、事件及传感器。
 * 输出原生 320x480 PPM 图片，便于离线检查页面布局。 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "freertos/task.h"
#include "ui.h"
#include "PlantMilk/ui_plantmilk_internal.h"
#include "plantmilk_ui_flow.h"
#include "Clean/ui_clean.h"
#include "FunctionSelection.h"
#include "DataCenter.h"
#include "lvgl_layer_memory.inc"
/* 直接测试正式控制代码中的传感器和按键处理函数。 */
#include "../../../components/myLvgl/myLvgl.c"

static TickType_t now;
static FuncSecType_t flow = NOTASK_SEC;
static unsigned types, orders;
static unsigned clean_mode_bits, event_writes;
volatile bool isPress, isLongPress;
volatile int encCounter;
EventGroupHandle_t FuncTypeGroup = (void *)1;
EventGroupHandle_t FuncOrderGroup = (void *)2;
EventGroupHandle_t SelfCleanEventGroup = (void *)3;
bool sensorState[sensorNum];
static TaskFunction_t collect_worker;
static bool collect_worker_active, fail_collect_create;
static unsigned collect_task_creations;
static char collect_motor_sequence[16];
static unsigned collect_motor_steps;
static TickType_t rise_started_at, rise_stopped_at, press_started_at;
static bool rise_started, rise_stopped, press_started;

static void record_collect_motor(char step)
{
    assert(collect_motor_steps + 1 < sizeof(collect_motor_sequence));
    collect_motor_sequence[collect_motor_steps++] = step;
    collect_motor_sequence[collect_motor_steps] = 0;
}

int xTaskCreate(TaskFunction_t task, const char *name, unsigned stack,
                void *arg, unsigned priority, void *handle)
{
    assert(strcmp(name, "TofuCollect") == 0 && stack == 4096);
    assert(arg == NULL && priority == 13 && handle == NULL);
    ++collect_task_creations;
    if (fail_collect_create) return 0;
    assert(collect_worker == NULL);
    collect_worker = task;
    return pdPASS;
}

void vTaskDelete(void *task)
{
    assert(task == NULL && collect_worker_active);
    assert(strcmp(collect_motor_sequence, "RrPpMG") == 0);
}

void riseMoterUp(uint16_t speed)
{
    assert(collect_worker_active && speed == 23 && collect_motor_steps == 0);
    rise_started = true;
    rise_started_at = now;
    record_collect_motor('R');
}
void riseMoterStop(void)
{
    assert(sensorState[riseHighPos] && !press_started);
    rise_stopped = true;
    rise_stopped_at = now;
    record_collect_motor('r');
}
void pressMoterUp(uint16_t speed)
{
    assert(collect_worker_active && speed == 800 && rise_stopped);
    assert(now - rise_stopped_at == 300);
    press_started = true;
    press_started_at = now;
    record_collect_motor('P');
}
void pressMoterStop(void)
{
    assert(sensorState[pressHighPos] && press_started);
    record_collect_motor('p');
}
void Play_Music(void) { record_collect_motor('M'); }
void riseMoterSetCurrentGear(uint8_t gear)
{
    assert(gear == 0);
    record_collect_motor('G');
}

TickType_t xTaskGetTickCount(void) { return now; }
void vTaskDelay(TickType_t ticks)
{
    now += ticks;
    if (collect_worker_active) {
        if (rise_started && now - rise_started_at >= 200) sensorState[riseHighPos] = true;
        if (press_started && now - press_started_at >= 200) sensorState[pressHighPos] = true;
    }
}

static void finish_collect_worker(void)
{
    assert(collect_worker && ui_tofu_ready_is_collecting());
    TaskFunction_t worker = collect_worker;
    collect_worker = NULL;
    collect_worker_active = true;
    rise_started = rise_stopped = press_started = false;
    collect_motor_steps = 0;
    collect_motor_sequence[0] = 0;
    sensorState[riseHighPos] = sensorState[pressHighPos] = false;
    worker(NULL);
    assert(strcmp(collect_motor_sequence, "RrPpMG") == 0);
    assert(!ui_tofu_ready_is_collecting());
    collect_worker_active = false;
}
FuncSecType_t GetCurTaskState(void) { return flow; }
FuncSecOrder_t GetCurTaskOrder(void) { return FUNCSEC_START; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits)
{
    ++event_writes;
    if (group == FuncTypeGroup) types |= bits;
    else if (group == FuncOrderGroup) orders |= bits;
    else if (group == SelfCleanEventGroup) clean_mode_bits |= bits;
    return bits;
}
EventBits_t xEventGroupGetBits(EventGroupHandle_t group)
{
    assert(group == SelfCleanEventGroup);
    return clean_mode_bits;
}
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits)
{
    ++event_writes;
    EventBits_t previous = clean_mode_bits;
    assert(group == SelfCleanEventGroup);
    clean_mode_bits &= ~bits;
    return previous;
}

static lv_color_t frame[320 * 480];
static lv_color_t rows[320 * 60];
static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels)
{
    for (int y = area->y1; y <= area->y2; ++y) {
        size_t width = area->x2 - area->x1 + 1;
        memcpy(&frame[y * 320 + area->x1], pixels, width * sizeof(*pixels));
        pixels += width;
    }
    lv_disp_flush_ready(driver);
}

static void check_page(ui_page_id_t page)
{
    assert(ui_get_current_page() == page);
    assert(lv_scr_act() == ui_Screen);
}

static void tick_all_flows(void)
{
    refresh_tofu_flow_page();
    PlantMilkUiTick();
    CleanUiTick();
}

static void test_external_flow_navigation(void)
{
    const FuncSecType_t tasks[] = {TOUFU_SEC, MILK_SEC, SOYMILK_SEC, SELFCLEAN_SEC, SELFCLEAN_SEC};
    const ui_page_id_t pages[] = {UI_PAGE_TOFU_MAKING, UI_PAGE_PLANTMILK_MAKING,
        UI_PAGE_PLANTMILK_MAKING, UI_PAGE_CLEAN_RINSING, UI_PAGE_CLEAN_CLEANING};
    for (unsigned i = 0; i < sizeof(tasks) / sizeof(tasks[0]); ++i) {
        ui_plantmilk_reset();
        ui_clean_reset();
        UiFlowConfigure();
        clean_mode_bits = i == 4 ? SelfClean_ModeBit : 0;
        ui_show_page(UI_PAGE_HOME);
        flow = tasks[i];
        unsigned before = event_writes;
        refresh_external_flow_page();
#if MYLVGL_UI_VALIDATION_MODE == 0
        check_page(pages[i]);
        handle_long_press();
        check_page(pages[i]);
        /* 周期性状态更新不得关闭停止确认弹窗。 */
        handle_short_press();
        ui_page_id_t confirmation = ui_get_current_page();
        refresh_external_flow_page();
        handle_long_press();
        check_page(confirmation);
        flow = NOTASK_SEC;
        tick_all_flows();
        check_page(i == 0 ? UI_PAGE_TOFU_READY : i < 3 ? UI_PAGE_PLANTMILK_READY : UI_PAGE_CLEAN_COMPLETE);
#else
        (void)pages;
        check_page(UI_PAGE_HOME);
#endif
        assert(event_writes == before);
    }
    flow = NOTASK_SEC;
    UiFlowConfigure();
    ui_show_page(UI_PAGE_HOME);
    puts("PASS: external Tofu/Milk/Soy Milk/Rinse/Deep Clean navigation, completion, no duplicate start, long-press blocking, test-mode isolation");
}

static void test_external_tofu_reentry(void)
{
#if MYLVGL_UI_VALIDATION_MODE == 0
    const ui_page_id_t origins[] = {UI_PAGE_HOME, UI_PAGE_HOME, UI_PAGE_TOFU_FIRMNESS,
                                    UI_PAGE_PLANTMILK_SELECT, UI_PAGE_CLEAN_SELECT};
    ui_plantmilk_reset();
    ui_clean_reset();
    UiFlowConfigure();
    unsigned before = event_writes;

    /* 任务仍在运行时，从首页或其他功能页面恢复制作页，不依赖首次接管标志。 */
    flow = TOUFU_SEC;
    for (unsigned i = 0; i < sizeof(origins) / sizeof(origins[0]); ++i) {
        ui_show_page(origins[i]);
        refresh_external_flow_page();
        tick_all_flows();
        check_page(UI_PAGE_TOFU_MAKING);

        handle_short_press();
        refresh_external_flow_page();
        tick_all_flows();
        check_page(UI_PAGE_STOP_TOFU_CONFIRM);
    }

    /* 任务在首页结束也应清除旧标志，下次启动必须重新接管。 */
    ui_show_page(UI_PAGE_HOME);
    flow = NOTASK_SEC;
    refresh_external_flow_page();
    tick_all_flows();
    assert(!tofu_flow_requested && !tofu_flow_seen_running);
    flow = TOUFU_SEC;
    refresh_external_flow_page();
    check_page(UI_PAGE_TOFU_MAKING);

    /* 切换到其他外部任务后，再次启动豆腐时不能残留旧的接管状态。 */
    flow = MILK_SEC;
    refresh_external_flow_page();
    check_page(UI_PAGE_PLANTMILK_MAKING);
    assert(!tofu_flow_requested && !tofu_flow_seen_running);
    flow = TOUFU_SEC;
    refresh_external_flow_page();
    check_page(UI_PAGE_TOFU_MAKING);

    ui_show_page(UI_PAGE_TOFU_STOPPING);
    refresh_external_flow_page();
    check_page(UI_PAGE_TOFU_STOPPING);
    assert(event_writes == before);
    flow = NOTASK_SEC;
    tick_all_flows();
    check_page(UI_PAGE_TOFU_STOPPED);
    UiFlowConfigure();
    ui_show_page(UI_PAGE_HOME);
    puts("PASS: external Tofu page recovery, off-page completion, task re-entry, stop-dialog preservation and no duplicate start");
#endif
}

static void test_shared_ui_mode(void)
{
    assert(UiValidationModeEnabled() == (MYLVGL_UI_VALIDATION_MODE == 1));
    memset(sensorState, 1, sizeof(sensorState)); /* 所有实体传感器均未检测到在位。 */
    TestSensorSnapshotReset();
    flow = NOTASK_SEC;
    types = orders = event_writes = 0;
#if MYLVGL_UI_VALIDATION_MODE == 1
    for (unsigned feature = 0; feature < 4; ++feature) {
        for (unsigned path = 0; path < 4; ++path) {
            ui_plantmilk_reset();
            ui_clean_reset();
            UiFlowConfigure();
            if (feature == 0) {
                ui_show_page(UI_PAGE_MACHINE_CHECK);
                refresh_machine_check_sensors();
                assert(ui_machine_check_can_continue());
                ui_show_page(UI_PAGE_PRESS_PARTS_CHECK);
                refresh_press_parts_sensors();
                assert(ui_press_parts_check_can_start());
                ui_select_press_parts_control(UI_PRESS_PARTS_CONTROL_START);
            } else if (feature == 1) {
                ui_show_page(UI_PAGE_PLANTMILK_MACHINE_CHECK);
                PlantMilkUiTick();
                ui_select_image(UI_PAGE_PLANTMILK_MACHINE_CHECK, UI_PM_STEP_ACTION);
            } else {
                CleanUiEnter();
                if (feature == 3) ui_clean_move_selection(1);
                handle_short_press();
                CleanUiTick();
            }
            handle_short_press();
            ui_page_id_t running = feature == 0 ? UI_PAGE_TOFU_MAKING :
                                   feature == 1 ? UI_PAGE_PLANTMILK_MAKING :
                                   feature == 2 ? UI_PAGE_CLEAN_RINSING : UI_PAGE_CLEAN_CLEANING;
            check_page(running);
            if (path == 1 || path == 2) handle_short_press();
            if (path == 2) {
                if (feature == 0) ui_select_stop_tofu_control(UI_STOP_TOFU_CONTROL_STOP);
                else if (feature == 1) ui_select_image(UI_PAGE_PLANTMILK_STOP_CONFIRM, UI_PM_STOP_STOP);
                else ui_clean_move_selection(1);
                handle_short_press();
            } else if (path == 3) {
                handle_long_press();
            }
            ui_page_id_t waiting = ui_get_current_page();
            now += (path != 2 ? MYLVGL_VALIDATION_MAKING_MS : MYLVGL_VALIDATION_STOPPING_MS) - 1U;
            tick_all_flows();
            check_page(waiting);
            ++now;
            tick_all_flows();
            ui_page_id_t complete = path != 2
                ? (feature == 0 ? UI_PAGE_TOFU_READY : feature == 1 ? UI_PAGE_PLANTMILK_READY : UI_PAGE_CLEAN_COMPLETE)
                : (feature == 0 ? UI_PAGE_TOFU_STOPPED : feature == 1 ? UI_PAGE_PLANTMILK_STOPPED : UI_PAGE_CLEAN_SELECT);
            check_page(complete);
            assert(event_writes == 0); /* 不发送启动、停止或清洗模式等硬件命令。 */
        }
    }
    puts("PASS: shared UI test mode, all sensor bypasses, Tofu/PlantMilk/Rinse/Deep Clean completion, explicit stops and blocked encoder long presses without hardware commands");
#else
    UiFlowConfigure();
    ui_show_page(UI_PAGE_MACHINE_CHECK);
    refresh_machine_check_sensors();
    assert(!ui_machine_check_can_continue());
    ui_show_page(UI_PAGE_PRESS_PARTS_CHECK);
    refresh_press_parts_sensors();
    assert(!ui_press_parts_check_can_start());
    ui_show_page(UI_PAGE_PLANTMILK_MACHINE_CHECK);
    PlantMilkUiTick();
    ui_select_image(UI_PAGE_PLANTMILK_MACHINE_CHECK, UI_PM_STEP_ACTION);
    assert(ui_get_selected_image(UI_PAGE_PLANTMILK_MACHINE_CHECK) == UI_PM_STEP_BACK);
    handle_short_press();
    check_page(UI_PAGE_PLANTMILK_ADD_INGREDIENTS);
    ui_clean_reset();
    UiFlowConfigure();
    CleanUiEnter();
    handle_short_press();
    CleanUiTick();
    handle_short_press();
    check_page(UI_PAGE_CLEAN_SELECT);
    assert(event_writes == 0);
    puts("PASS: shared normal mode retains hardware sensor gates for all three features");
#endif
}

static void check_hardware_sensor_gates(bool ready)
{
    flow = NOTASK_SEC;
    ui_show_page(UI_PAGE_MACHINE_CHECK);
    refresh_machine_check_sensors();
    assert(ui_machine_check_can_continue() == (UiValidationModeEnabled() || ready));
    ui_show_page(UI_PAGE_PRESS_PARTS_CHECK);
    refresh_press_parts_sensors();
    assert(ui_press_parts_check_can_start() == (UiValidationModeEnabled() || ready));

    /* 通过运行时桥接配置，在测试模式构建中也验证真实传感器模式。 */
    PlantMilkUiConfigure(false, false);
    ui_show_page(UI_PAGE_PLANTMILK_MACHINE_CHECK);
    PlantMilkUiTick();
    assert(ui_plantmilk_can_start() == ready);
    if (!ready) {
        unsigned before = event_writes;
        ui_select_image(UI_PAGE_PLANTMILK_MACHINE_CHECK, UI_PM_STEP_ACTION);
        assert(ui_get_selected_image(UI_PAGE_PLANTMILK_MACHINE_CHECK) == UI_PM_STEP_BACK);
        handle_short_press();
        check_page(UI_PAGE_PLANTMILK_ADD_INGREDIENTS);
        assert(event_writes == before);
    }
    for (unsigned mode = UI_CLEAN_RINSE; mode <= UI_CLEAN_DEEP; ++mode) {
        ui_clean_reset();
        CleanUiConfigure(false, false);
        CleanUiEnter();
        if (mode == UI_CLEAN_DEEP) ui_clean_move_selection(1);
        handle_short_press();
        CleanUiTick();
        unsigned before = event_writes;
        handle_short_press();
        if (ready) {
            check_page(mode == UI_CLEAN_RINSE ? UI_PAGE_CLEAN_RINSING : UI_PAGE_CLEAN_CLEANING);
            assert(event_writes > before);
        } else {
            check_page(UI_PAGE_CLEAN_SELECT);
            assert(event_writes == before);
        }
    }
}

static void test_sensor_snapshot_validity(void)
{
    uint16_t inputs = 0;
    TestSensorSnapshotReset();
    memset(sensorState, 0, sizeof(sensorState)); /* 启动时的零初值不得被判定为已在位。 */
    assert(!DataCenterGetSensorSnapshot(NULL));
    assert(!DataCenterGetSensorSnapshot(&inputs) && inputs == UINT16_MAX);
    check_hardware_sensor_gates(false);

    TestSensorSnapshotUpdate();
    assert(DataCenterGetSensorSnapshot(&inputs) && inputs == 0);
    check_hardware_sensor_gates(true);
    now += DATACENTER_SENSOR_TIMEOUT_MS - 1U;
    assert(DataCenterGetSensorSnapshot(&inputs));
    ++now;
    assert(!DataCenterGetSensorSnapshot(&inputs) && inputs == UINT16_MAX);
    check_hardware_sensor_gates(false);

    /* 收到新的有效应答后，无需重启 UI 即可恢复全部检查条件。 */
    TestSensorSnapshotUpdate();
    assert(DataCenterGetSensorSnapshot(&inputs));
    check_hardware_sensor_gates(true);
    memset(sensorState, 1, sizeof(sensorState));
    TestSensorSnapshotUpdate();
    assert(DataCenterGetSensorSnapshot(&inputs) && inputs == UINT16_MAX);
    check_hardware_sensor_gates(false);

    /* 按键事件到达时，上一帧渲染的检查结果仍可能显示为就绪。 */
    memset(sensorState, 0, sizeof(sensorState));
#if MYLVGL_UI_VALIDATION_MODE == 0
    TestSensorSnapshotUpdate();
    ui_show_page(UI_PAGE_MACHINE_CHECK);
    refresh_machine_check_sensors();
    assert(ui_machine_check_can_continue());
    now += DATACENTER_SENSOR_TIMEOUT_MS;
    ui_select_machine_check_control(UI_MACHINE_CHECK_CONTROL_CONTINUE);
    handle_short_press();
    check_page(UI_PAGE_MACHINE_CHECK);
    assert(ui_get_selected_machine_check_control() == UI_MACHINE_CHECK_CONTROL_BACK);

    TestSensorSnapshotUpdate();
    ui_show_page(UI_PAGE_PRESS_PARTS_CHECK);
    refresh_press_parts_sensors();
    assert(ui_press_parts_check_can_start());
    now += DATACENTER_SENSOR_TIMEOUT_MS;
    ui_select_press_parts_control(UI_PRESS_PARTS_CONTROL_START);
    unsigned before = event_writes;
    handle_short_press();
    check_page(UI_PAGE_PRESS_PARTS_CHECK);
    assert(ui_get_selected_press_parts_control() == UI_PRESS_PARTS_CONTROL_BACK);
    assert(event_writes == before);
#endif
    TestSensorSnapshotUpdate();
    PlantMilkUiConfigure(false, false);
    ui_show_page(UI_PAGE_PLANTMILK_MACHINE_CHECK);
    PlantMilkUiTick();
    assert(ui_plantmilk_can_start());
    now += DATACENTER_SENSOR_TIMEOUT_MS;
    ui_select_image(UI_PAGE_PLANTMILK_MACHINE_CHECK, UI_PM_STEP_ACTION);
    unsigned before_pm = event_writes;
    handle_short_press();
    check_page(UI_PAGE_PLANTMILK_MACHINE_CHECK);
    assert(ui_get_selected_image(UI_PAGE_PLANTMILK_MACHINE_CHECK) == UI_PM_STEP_BACK);
    assert(event_writes == before_pm);

    for (unsigned mode = UI_CLEAN_RINSE; mode <= UI_CLEAN_DEEP; ++mode) {
        TestSensorSnapshotUpdate();
        ui_clean_reset();
        CleanUiConfigure(false, false);
        CleanUiEnter();
        if (mode == UI_CLEAN_DEEP) ui_clean_move_selection(1);
        handle_short_press();
        CleanUiTick();
        now += DATACENTER_SENSOR_TIMEOUT_MS;
        unsigned before_clean = event_writes;
        handle_short_press();
        check_page(mode == UI_CLEAN_RINSE ? UI_PAGE_CLEAN_PREPARE_RINSE : UI_PAGE_CLEAN_PREPARE_CLEAN);
        assert(ui_get_selected_image(ui_get_current_page()) == UI_CLEAN_PREP_BACK);
        assert(event_writes == before_clean);
    }
    puts("PASS: production sensor snapshot reader, startup invalid data, 1-second timeout boundary, recovery, active-low inputs and stale-button startup lockout");
}

static void test_post_making_clean(void)
{
    const ui_page_id_t origins[] = {
        UI_PAGE_TOFU_READY, UI_PAGE_TOFU_STOPPED,
        UI_PAGE_PLANTMILK_READY, UI_PAGE_PLANTMILK_READY_NO_FILTER,
        UI_PAGE_PLANTMILK_STOPPED,
    };
    flow = NOTASK_SEC;
    for (unsigned validation = 0; validation < 2; ++validation) {
        for (unsigned i = 0; i < sizeof(origins) / sizeof(origins[0]); ++i) {
            ui_clean_reset();
            CleanUiConfigure(validation, validation);
            memset(sensorState, 0, sizeof(sensorState));
            TestSensorSnapshotUpdate();
            /* 先前选中的漂洗或返回选项，不应影响重新进入清洗功能时的选中项。 */
            ui_clean_prepare(UI_CLEAN_RINSE);
            ui_clean_move_selection(-1);
            ui_show_page(origins[i]);
            if (origins[i] == UI_PAGE_TOFU_READY)
                ui_select_tofu_ready_control(UI_TOFU_READY_CONTROL_CLEAN);
            else if (origins[i] == UI_PAGE_PLANTMILK_READY)
                ui_select_image(origins[i], UI_PM_READY_CLEAN);
            types = orders = clean_mode_bits = event_writes = 0;
            handle_short_press();
            check_page(UI_PAGE_CLEAN_PREPARE_CLEAN);
            assert(ui_get_selected_image(UI_PAGE_CLEAN_PREPARE_CLEAN) == 7U);
            assert(event_writes == 0);

            /* 返回和重新进入都只执行页面导航。 */
            ui_clean_move_selection(-1);
            handle_short_press();
            check_page(UI_PAGE_CLEAN_SELECT);
            assert(event_writes == 0);
            ui_clean_prepare(UI_CLEAN_DEEP);
            handle_short_press();
            check_page(UI_PAGE_CLEAN_CLEANING);
            if (validation) {
                assert(event_writes == 0);
                now += MYLVGL_VALIDATION_MAKING_MS;
            } else {
                assert(types == (1U << SELFCLEAN_SEC) && orders == FuncStart_bit);
                assert(clean_mode_bits == SelfClean_ModeBit);
                flow = SELFCLEAN_SEC;
                CleanUiTick();
                flow = NOTASK_SEC;
            }
            CleanUiTick();
            check_page(UI_PAGE_CLEAN_COMPLETE);
        }
    }
    /* 从结果页跳转后，若缺少传感器数据，仍须禁止启动。 */
    for (unsigned i = 0; i < sizeof(origins) / sizeof(origins[0]); ++i) {
        ui_clean_reset();
        CleanUiConfigure(false, false);
        TestSensorSnapshotReset();
        ui_show_page(origins[i]);
        if (origins[i] == UI_PAGE_TOFU_READY)
            ui_select_tofu_ready_control(UI_TOFU_READY_CONTROL_CLEAN);
        else if (origins[i] == UI_PAGE_PLANTMILK_READY)
            ui_select_image(origins[i], UI_PM_READY_CLEAN);
        event_writes = 0;
        handle_short_press();
        check_page(UI_PAGE_CLEAN_PREPARE_CLEAN);
        assert(ui_get_selected_image(UI_PAGE_CLEAN_PREPARE_CLEAN) == UI_CLEAN_PREP_BACK);
        handle_short_press();
        check_page(UI_PAGE_CLEAN_SELECT);
        assert(event_writes == 0);
    }
    ui_show_page(UI_PAGE_TOFU_READY);
    ui_select_tofu_ready_control(UI_TOFU_READY_CONTROL_COLLECT);
    handle_short_press();
#if MYLVGL_UI_VALIDATION_MODE != 1
    check_page(UI_PAGE_TOFU_READY);
    finish_collect_worker();
    refresh_tofu_flow_page();
#endif
    check_page(UI_PAGE_HOME);
    puts("PASS: all Tofu/PlantMilk result Clean buttons open Start Cleaning without hardware commands; explicit Start runs Deep Clean, with sensor gates and Back preserved");
}

static ui_clean_flow_state_t clean_state;
static ui_clean_mode_t clean_started_mode;
static unsigned clean_stop_calls;

static void clean_refresh_sensors(void)
{
    ui_clean_sensor_update(true, true);
}

static bool clean_start(ui_clean_mode_t mode)
{
    clean_started_mode = mode;
    clean_state = UI_CLEAN_FLOW_RUNNING;
    return true;
}

static ui_clean_flow_state_t clean_stop(void)
{
    ++clean_stop_calls;
    clean_state = UI_CLEAN_FLOW_STOPPING;
    return clean_state;
}

static ui_clean_flow_state_t clean_poll(void)
{
    return clean_state;
}

static void test_clean_completion(void)
{
    const ui_clean_flow_ops_t ops = {
        .refresh_sensors = clean_refresh_sensors,
        .start = clean_start,
        .stop = clean_stop,
        .poll = clean_poll,
    };

    for (unsigned mode = UI_CLEAN_RINSE; mode <= UI_CLEAN_DEEP; ++mode) {
        for (unsigned path = 0; path < 5; ++path) {
            ui_clean_reset();
            ui_clean_bind_flow(&ops);
            clean_stop_calls = 0;
            ui_clean_enter();
            if (mode == UI_CLEAN_DEEP) ui_clean_move_selection(1);
            ui_clean_handle_short_press();
            check_page(mode == UI_CLEAN_RINSE ? UI_PAGE_CLEAN_PREPARE_RINSE
                                             : UI_PAGE_CLEAN_PREPARE_CLEAN);
            ui_clean_tick();
            ui_clean_handle_short_press();
            ui_page_id_t running = mode == UI_CLEAN_RINSE
                                       ? UI_PAGE_CLEAN_RINSING : UI_PAGE_CLEAN_CLEANING;
            check_page(running);
            assert(clean_started_mode == mode);

            if (path != 0) {
                ui_clean_handle_short_press();
                check_page(UI_PAGE_CLEAN_STOP_CONFIRM);
                ui_clean_tick();
                check_page(UI_PAGE_CLEAN_STOP_CONFIRM);
            }
            if (path == 2) {
                /* 选择继续后保持当前流程，且仍可正常完成制作。 */
                ui_clean_handle_short_press();
                check_page(running);
            } else if (path == 3 || path == 4) {
                if (path == 3) {
                    ui_clean_move_selection(1);
                    ui_clean_handle_short_press();
                } else {
                    ui_clean_handle_long_press();
                }
                assert(clean_stop_calls == 1);
                ui_clean_tick();
                check_page(path == 3 ? UI_PAGE_CLEAN_STOPPING : UI_PAGE_CLEAN_STOP_CONFIRM);
                if (path == 3) {
                    assert(ui_get_selected_image(UI_PAGE_CLEAN_STOPPING) == UI_IMAGE_INDEX_NONE);
                    const ui_page_config_t *page = &ui_page_configs[UI_PAGE_CLEAN_STOPPING];
                    assert(strcmp(page->labels[0].text, "Stopping...") == 0);
                    assert(strcmp(page->labels[1].text, "Please wait") == 0);
                    assert(strcmp(page->labels[2].text, "keep parts in place") == 0);
                    assert(strcmp(page->labels[3].text, "Clean") == 0);
                    for (unsigned turn = 0; turn < 4; ++turn) {
                        handle_rotation((turn & 1U ? MY_ENCODER_REVERSE_DIRECTION
                                                  : MY_ENCODER_FORWARD_DIRECTION) * UI_ENCODER_PULSES_PER_SWITCH);
                        handle_short_press();
                        handle_long_press();
                        check_page(UI_PAGE_CLEAN_STOPPING);
                        assert(ui_get_selected_image(UI_PAGE_CLEAN_STOPPING) == UI_IMAGE_INDEX_NONE);
                        assert(clean_stop_calls == 1);
                    }
                }
            }

            clean_state = path < 3 ? UI_CLEAN_FLOW_READY : UI_CLEAN_FLOW_STOPPED;
            ui_clean_tick();
            check_page(path < 3 ? UI_PAGE_CLEAN_COMPLETE : UI_PAGE_CLEAN_SELECT);
            assert(clean_stop_calls == (path < 3 ? 0U : 1U));
        }
    }
    puts("PASS: Rinse/Deep Clean completion, continue, Stop opens Screen7 until stopped, Screen7 input lockout, unchanged long-press path");
}

static void press(uint8_t selected)
{
    ui_select_image(ui_get_current_page(), selected);
    PlantMilkUiHandleShortPress();
}

static void check_navigation_bounds(ui_page_id_t page, uint8_t first, uint8_t last)
{
    ui_show_page(page);
    ui_select_image(page, first);
    for (unsigned i = 0; i < 12; ++i) ui_plantmilk_move_selection(-1);
    assert(ui_get_selected_image(page) == first);
    for (unsigned i = 0; i < 12; ++i) ui_plantmilk_move_selection(1);
    assert(ui_get_selected_image(page) == last);
    for (unsigned i = 0; i < 12; ++i) ui_plantmilk_move_selection(-1);
    assert(ui_get_selected_image(page) == first);
}

static void set_check_sensors(ui_page_id_t page, unsigned missing)
{
    if (page == UI_PAGE_MACHINE_CHECK) {
        const ui_machine_check_sensors_t sensors = {
            .lid_present = missing != 1,
            .coagulant_container_present = missing != 2,
            .coagulant_low_level = missing == 3,
            .water_tank_present = missing != 4,
            .waste_tank_present = missing != 5,
        };
        ui_machine_check_update(&sensors);
    } else if (page == UI_PAGE_PRESS_PARTS_CHECK) {
        const ui_press_parts_sensors_t sensors = {missing != 1, missing != 2};
        ui_press_parts_check_update(&sensors);
    } else if (page == UI_PAGE_PLANTMILK_MACHINE_CHECK) {
        ui_plantmilk_check_update(missing != 1, missing != 2);
    } else if (page == UI_PAGE_CLEAN_PREPARE_RINSE || page == UI_PAGE_CLEAN_PREPARE_CLEAN) {
        ui_clean_sensor_update(missing != 1, missing != 2);
    }
}

static void start(bool validation)
{
    TestSensorSnapshotUpdate();
    PlantMilkUiConfigure(validation, validation);
    flow = NOTASK_SEC;
    types = orders = 0;
    ui_show_page(UI_PAGE_PLANTMILK_MACHINE_CHECK);
    PlantMilkUiTick();
    ui_select_image(UI_PAGE_PLANTMILK_MACHINE_CHECK, UI_PM_STEP_ACTION);
    PlantMilkUiHandleShortPress();
    check_page(UI_PAGE_PLANTMILK_MAKING);
    if (!validation) { assert(types == (1U << MILK_SEC)); assert(orders == FuncStart_bit); }
    else assert(types == 0 && orders == 0);
}

static void screenshot(const char *directory, const char *name)
{
    printf("Render: %s\n", name);
    lv_tick_inc(200);
    lv_refr_now(NULL);
    char filename[1024];
    snprintf(filename, sizeof(filename), "%s/%s.ppm", directory, name);
    FILE *file = fopen(filename, "wb");
    assert(file);
    fprintf(file, "P6\n320 480\n255\n");
    for (unsigned i = 0; i < 320U * 480U; ++i) {
        lv_color32_t color = {.full = lv_color_to32(frame[i])};
        unsigned char rgb[] = {color.ch.red, color.ch.green, color.ch.blue};
        fwrite(rgb, 1, 3, file);
    }
    fclose(file);
}

static void test_encoder_navigation(const char *directory)
{
    /* 按页面的可交互控件遍历，跳过装饰图片；走正式物理方向转换入口。 */
    static const struct {
        ui_page_id_t page;
        unsigned count;
        uint8_t controls[8];
        bool cyclic;
    } cases[] = {
        {UI_PAGE_HOME, 4, {1, 2, 3, 4}, true},
        {UI_PAGE_FIRM_TOFU, 5, {0, 1, 2, 3, 4}},
        {UI_PAGE_MORE_OPTIONS, 6, {0, 1, 2, 3, 4, 5}},
        {UI_PAGE_ADD_INGREDIENTS, 2, {0, 6}},
        {UI_PAGE_MACHINE_CHECK, 2, {0, 6}},
        {UI_PAGE_PRESS_PARTS_CHECK, 2, {0, 2}},
        {UI_PAGE_TOFU_READY, 2, {1, 2}},
        {UI_PAGE_STOP_TOFU_CONFIRM, 2, {UI_STOP_TOFU_CONTROL_KEEP, UI_STOP_TOFU_CONTROL_STOP}},
        {UI_PAGE_PLANTMILK_SELECT, 5, {2, 3, 4, 5, 6}, true},
        {UI_PAGE_PLANTMILK_OPTIONS, 8, {0, 1, 2, 3, 4, 5, 6, 7}},
        {UI_PAGE_PLANTMILK_ADD_INGREDIENTS, 2, {0, 6}},
        {UI_PAGE_PLANTMILK_MACHINE_CHECK, 2, {0, 6}},
        {UI_PAGE_PLANTMILK_READY, 2, {1, 2}},
        {UI_PAGE_PLANTMILK_STOP_CONFIRM, 2, {1, 2}},
        {UI_PAGE_CLEAN_SELECT, 3, {1, 2, 3}, true},
        {UI_PAGE_CLEAN_PREPARE_RINSE, 2, {0, 7}},
        {UI_PAGE_CLEAN_PREPARE_CLEAN, 2, {0, 7}},
        {UI_PAGE_CLEAN_STOP_CONFIRM, 2, {1, 2}},
    };
    unsigned before = event_writes;
    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        ui_page_id_t page = cases[c].page;
        unsigned count = cases[c].count;
        set_check_sensors(page, 0);
        ui_show_page(page);
        ui_select_image(page, cases[c].controls[0]);
        selection_rotation_accumulator = 0;
        unsigned index = 0;
        for (unsigned turn = 0; turn < count + 10; ++turn) {
            handle_rotation(MY_ENCODER_FORWARD_DIRECTION * UI_ENCODER_PULSES_PER_SWITCH);
            if (index + 1 < count) ++index;
            else if (cases[c].cyclic) index = 0;
            assert(ui_get_selected_image(page) == cases[c].controls[index]);
        }
        if (page == UI_PAGE_ADD_INGREDIENTS) screenshot(directory, "tofu_continue");
        for (unsigned turn = 0; turn < count + 10; ++turn) {
            handle_rotation(MY_ENCODER_REVERSE_DIRECTION * UI_ENCODER_PULSES_PER_SWITCH);
            if (index > 0) --index;
            else if (cases[c].cyclic) index = count - 1;
            assert(ui_get_selected_image(page) == cases[c].controls[index]);
        }
        if (page == UI_PAGE_ADD_INGREDIENTS) screenshot(directory, "tofu_back");
        /* 在端点连续旋转后，反转一步即离开端点，无额外计数积压。 */
        handle_rotation(20 * MY_ENCODER_REVERSE_DIRECTION * UI_ENCODER_PULSES_PER_SWITCH);
        handle_rotation(MY_ENCODER_FORWARD_DIRECTION * UI_ENCODER_PULSES_PER_SWITCH);
        index = cases[c].cyclic ? (index + count - 20 % count + 1) % count : 1;
        assert(ui_get_selected_image(page) == cases[c].controls[index]);
        if (page == UI_PAGE_CLEAN_SELECT) screenshot(directory, "clean_select");
        check_page(page);
    }

    for (unsigned firmness = 0; firmness < UI_TOFU_FIRMNESS_COUNT; ++firmness) {
        ui_select_tofu_firmness((ui_tofu_firmness_t)firmness);
        ui_show_page(UI_PAGE_TOFU_MAKING);
        selection_rotation_accumulator = 0;
        handle_short_press();
        check_page(UI_PAGE_STOP_TOFU_CONFIRM);
        assert(ui_get_selected_stop_tofu_control() == UI_STOP_TOFU_CONTROL_KEEP);
        lv_obj_t *keep = lv_obj_get_child(ui_Screen, UI_STOP_TOFU_KEEP_BUTTON);
        lv_obj_t *stop = lv_obj_get_child(ui_Screen, UI_STOP_TOFU_STOP_BUTTON);
        assert(lv_img_get_src(keep) == ui_page_configs[UI_PAGE_STOP_TOFU_CONFIRM].images[UI_STOP_TOFU_KEEP_BUTTON].src_alt);
        assert(lv_img_get_src(stop) == ui_page_configs[UI_PAGE_STOP_TOFU_CONFIRM].images[UI_STOP_TOFU_STOP_BUTTON].src);
        if (firmness == UI_TOFU_FIRMNESS_SOFT) screenshot(directory, "tofu_stop_default_keep");
        for (unsigned turn = 0; turn < 20; ++turn) {
            handle_rotation(MY_ENCODER_FORWARD_DIRECTION * UI_ENCODER_PULSES_PER_SWITCH);
            assert(ui_get_selected_stop_tofu_control() == UI_STOP_TOFU_CONTROL_STOP);
        }
        ui_show_page(UI_PAGE_STOP_TOFU_CONFIRM);
        assert(ui_get_selected_stop_tofu_control() == UI_STOP_TOFU_CONTROL_STOP);
        assert(lv_img_get_src(stop) == ui_page_configs[UI_PAGE_STOP_TOFU_CONFIRM].images[UI_STOP_TOFU_STOP_BUTTON].src_alt);
        if (firmness == UI_TOFU_FIRMNESS_SOFT) screenshot(directory, "tofu_stop_selected_stop");
        for (unsigned turn = 0; turn < 20; ++turn) {
            handle_rotation(MY_ENCODER_REVERSE_DIRECTION * UI_ENCODER_PULSES_PER_SWITCH);
            assert(ui_get_selected_stop_tofu_control() == UI_STOP_TOFU_CONTROL_KEEP);
        }
        handle_rotation(MY_ENCODER_FORWARD_DIRECTION * UI_ENCODER_PULSES_PER_SWITCH);
        assert(ui_get_selected_stop_tofu_control() == UI_STOP_TOFU_CONTROL_STOP);
        ui_show_page(UI_PAGE_TOFU_MAKING);
        handle_short_press(); /* 上次选中 Stop，再次进入也必须恢复 Keep Making。 */
        assert(ui_get_selected_stop_tofu_control() == UI_STOP_TOFU_CONTROL_KEEP);
        handle_short_press();
        check_page(UI_PAGE_TOFU_MAKING);
        assert(event_writes == before);
        assert(ui_get_selected_tofu_firmness() == (ui_tofu_firmness_t)firmness);
    }
    puts("PASS: all four Tofu firmness levels default to Keep Making on stop-dialog entry, forward selects Stop, reverse selects Keep, endpoints never wrap, redraw preserves focus and Keep resumes without stop commands");

    ui_show_page(UI_PAGE_TOFU_FIRMNESS);
    /* 四种硬度和 Back 组成五项循环，连续三圈验证首尾和双向反转。 */
    for (int direction = -1; direction <= 1; direction += 2) {
        ui_select_tofu_firmness(UI_TOFU_FIRMNESS_SOFT);
        int index = 0;
        for (unsigned turn = 0; turn < 15; ++turn) {
            index = (index + direction + 5) % 5;
            handle_rotation((direction > 0 ? MY_ENCODER_FORWARD_DIRECTION
                                           : MY_ENCODER_REVERSE_DIRECTION) * UI_ENCODER_PULSES_PER_SWITCH);
            assert(ui_is_tofu_firmness_back_selected() == (index == 4));
            if (index != 4) assert(ui_get_selected_tofu_firmness() == index);
        }
    }
    handle_rotation(MY_ENCODER_REVERSE_DIRECTION * UI_ENCODER_PULSES_PER_SWITCH);
    assert(ui_is_tofu_firmness_back_selected());
    handle_short_press();
    check_page(UI_PAGE_HOME);
    assert(event_writes == before);

    /* 实际短按 Back 必须返回上页，不能落入 Continue/Start 的业务分支。 */
    static const struct { ui_page_id_t page, parent; } back_cases[] = {
        {UI_PAGE_ADD_INGREDIENTS, UI_PAGE_FIRM_TOFU},
        {UI_PAGE_MACHINE_CHECK, UI_PAGE_ADD_INGREDIENTS},
        {UI_PAGE_PRESS_PARTS_CHECK, UI_PAGE_MACHINE_CHECK},
        {UI_PAGE_PLANTMILK_ADD_INGREDIENTS, UI_PAGE_PLANTMILK_OPTIONS},
        {UI_PAGE_PLANTMILK_MACHINE_CHECK, UI_PAGE_PLANTMILK_ADD_INGREDIENTS},
        {UI_PAGE_CLEAN_PREPARE_RINSE, UI_PAGE_CLEAN_SELECT},
        {UI_PAGE_CLEAN_PREPARE_CLEAN, UI_PAGE_CLEAN_SELECT},
    };
    for (unsigned c = 0; c < sizeof(back_cases) / sizeof(back_cases[0]); ++c) {
        ui_show_page(back_cases[c].page);
        handle_rotation(MY_ENCODER_FORWARD_DIRECTION * UI_ENCODER_PULSES_PER_SWITCH);
        handle_rotation(MY_ENCODER_REVERSE_DIRECTION * UI_ENCODER_PULSES_PER_SWITCH);
        handle_short_press();
        check_page(back_cases[c].parent);
        assert(event_writes == before);
    }
    ui_plantmilk_reset();
    ui_clean_reset();
    ui_select_tofu_firmness(UI_TOFU_FIRMNESS_SOFT);
    puts("PASS: physical encoder directions on all 19 navigable pages, circular selectors wrap with Back reachable, ordinary buttons stop at endpoints, rapid-turn reversal and safe return");
}

static void test_sensor_focus_lock(const char *directory)
{
    static const struct {
        ui_page_id_t page, parent;
        uint8_t action;
        unsigned sensor_count;
        const char *frame_name;
    } cases[] = {
        {UI_PAGE_MACHINE_CHECK, UI_PAGE_ADD_INGREDIENTS, UI_MACHINE_CHECK_CONTROL_CONTINUE, 5, "tofu_check_back"},
        {UI_PAGE_PRESS_PARTS_CHECK, UI_PAGE_MACHINE_CHECK, UI_PRESS_PARTS_CONTROL_START, 2, "press_check_back"},
        {UI_PAGE_PLANTMILK_MACHINE_CHECK, UI_PAGE_PLANTMILK_ADD_INGREDIENTS, UI_PM_STEP_ACTION, 2, "milk_check_back"},
        {UI_PAGE_CLEAN_PREPARE_RINSE, UI_PAGE_CLEAN_SELECT, UI_CLEAN_PREP_START, 2, "rinse_check_back"},
        {UI_PAGE_CLEAN_PREPARE_CLEAN, UI_PAGE_CLEAN_SELECT, UI_CLEAN_PREP_START, 2, "clean_check_back"},
    };
    memset(sensorState, 0, sizeof(sensorState));
    TestSensorSnapshotUpdate();
    flow = NOTASK_SEC;
    UiFlowConfigure();
    unsigned before = event_writes;
    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        ui_page_id_t page = cases[c].page;
        for (unsigned missing = 1; missing <= cases[c].sensor_count; ++missing) {
            ui_show_page(UI_PAGE_HOME);
            set_check_sensors(page, missing);
            ui_show_page(page);
            assert(ui_get_selected_image(page) == 0U);
            ui_select_image(page, cases[c].action);
            assert(ui_get_selected_image(page) == 0U);
            for (unsigned turn = 0; turn < 10; ++turn) {
                handle_rotation((turn & 1U ? MY_ENCODER_REVERSE_DIRECTION : MY_ENCODER_FORWARD_DIRECTION) * UI_ENCODER_PULSES_PER_SWITCH);
                assert(ui_get_selected_image(page) == 0U);
            }
            /* 不仅逻辑索引返回 Back，实际绘制也必须使用 Back 高亮资源。 */
            lv_obj_t *back = lv_obj_get_child(ui_Screen, 0);
            assert(lv_img_get_src(back) == ui_page_configs[page].images[0].src_alt);
            assert(!lv_obj_has_flag(back, LV_OBJ_FLAG_HIDDEN));
            if (missing == 1) screenshot(directory, cases[c].frame_name);

            set_check_sensors(page, 0);
            bool tofu_check = page == UI_PAGE_MACHINE_CHECK ||
                              page == UI_PAGE_PRESS_PARTS_CHECK;
            assert(ui_get_selected_image(page) == (tofu_check ? cases[c].action : 0U));
            if (tofu_check) {
                lv_obj_t *action = lv_obj_get_child(ui_Screen, cases[c].action);
                assert(lv_img_get_src(action) == ui_page_configs[page].images[cases[c].action].src_alt);
                if (missing == 1) screenshot(directory, page == UI_PAGE_MACHINE_CHECK
                    ? "tofu_check_ready_focus" : "press_check_ready_focus");
                handle_rotation(MY_ENCODER_REVERSE_DIRECTION * UI_ENCODER_PULSES_PER_SWITCH);
                assert(ui_get_selected_image(page) == 0U);
                set_check_sensors(page, 0);
                ui_show_page(page); /* 检查持续通过时，刷新不能抢走手动选择的 Back。 */
                assert(ui_get_selected_image(page) == 0U);
                ui_show_page(UI_PAGE_HOME);
                ui_show_page(page); /* 已通过检查的页面再次进入时默认聚焦动作键。 */
                assert(ui_get_selected_image(page) == cases[c].action);
            }
            handle_rotation(MY_ENCODER_FORWARD_DIRECTION * UI_ENCODER_PULSES_PER_SWITCH);
            assert(ui_get_selected_image(page) == cases[c].action);
            set_check_sensors(page, missing); /* 原先已聚焦 Start/Continue，传感器掉线立即回到 Back。 */
            assert(ui_get_selected_image(page) == 0U);
            handle_short_press();
            check_page(cases[c].parent);
            assert(event_writes == before);
        }
    }
    ui_show_page(UI_PAGE_MACHINE_CHECK);
    refresh_machine_check_sensors();
    assert(ui_get_selected_machine_check_control() == UI_MACHINE_CHECK_CONTROL_CONTINUE);
    handle_short_press();
    check_page(UI_PAGE_PRESS_PARTS_CHECK);
    assert(ui_get_selected_press_parts_control() == UI_PRESS_PARTS_CONTROL_START);
    assert(event_writes == before); /* 自动聚焦及按下 Continue 均不启动制作。 */
    ui_show_page(UI_PAGE_HOME);
    puts("PASS: Tofu checks automatically focus Continue/Start on readiness and entry, preserve manual Back during redraw, and Continue opens the focused Start page without starting hardware");
    puts("PASS: all 5 sensor-gated pages force and visibly highlight Back for every failed sensor, reject encoder/direct focus, restore navigation on recovery, and return without hardware commands");
}

static void test_tofu_collect_button(void)
{
#if MYLVGL_UI_VALIDATION_MODE != 1
    UiFlowConfigure();
    flow = TOUFU_SEC;
    ui_show_page(UI_PAGE_TOFU_READY);
    ui_select_tofu_ready_control(UI_TOFU_READY_CONTROL_COLLECT);
    unsigned before = collect_task_creations;
    handle_short_press();
    assert(collect_task_creations == before);
    flow = NOTASK_SEC;
    fail_collect_create = true;
    handle_short_press();
    check_page(UI_PAGE_TOFU_READY);
    assert(!ui_tofu_ready_is_collecting() && !collect_worker);
    fail_collect_create = false;
    unsigned motor_before = collect_motor_steps;
    handle_short_press();
    assert(ui_tofu_ready_is_collecting() && collect_worker);
    assert(collect_motor_steps == motor_before); /* Only the worker moves motors. */
    before = collect_task_creations;
    handle_rotation(MY_ENCODER_FORWARD_DIRECTION * UI_ENCODER_PULSES_PER_SWITCH);
    handle_short_press();
    handle_long_press();
    assert(collect_task_creations == before);
    assert(ui_get_selected_tofu_ready_control() == UI_TOFU_READY_CONTROL_COLLECT);
    refresh_tofu_flow_page();
    check_page(UI_PAGE_TOFU_READY);
    finish_collect_worker();
    refresh_tofu_flow_page();
    check_page(UI_PAGE_HOME);
    puts("PASS: Ready does not lift; Collect Pulp starts one background job; rise high -> stop -> 300ms -> press high -> stop -> music -> low gear; retry after create failure");
#endif
}

static void test_blending_time_stability(const char *directory)
{
    static lv_color_t at_one_minute[320 * 480];
    ui_plantmilk_select_recipe(UI_PM_OAT);
    ui_plantmilk_restore_defaults();
    ui_show_page(UI_PAGE_PLANTMILK_OPTIONS);
    ui_select_image(UI_PAGE_PLANTMILK_OPTIONS, UI_PM_OPTIONS_BLEND);
    ui_plantmilk_toggle_editing();
    lv_obj_t *label = lv_obj_get_child(ui_Screen, UI_MAX_PAGE_IMAGES + 2);
    lv_obj_t *previous = lv_obj_get_child(ui_Screen, UI_PM_OPTIONS_PREVIOUS);
    lv_obj_t *next = lv_obj_get_child(ui_Screen, UI_PM_OPTIONS_NEXT);
    lv_coord_t label_x = 0, label_y = 0, width = 0, height = 0;
    lv_coord_t previous_x = 0, previous_y = 0, next_x = 0, next_y = 0;
    for (unsigned seconds = 30; seconds <= 180; seconds += 15) {
        assert(ui_plantmilk_get_options()->blending_seconds == seconds);
        lv_tick_inc(200);
        lv_refr_now(NULL);
        if (seconds == 30) {
            label_x = lv_obj_get_x(label);
            label_y = lv_obj_get_y(label);
            width = lv_obj_get_width(label);
            height = lv_obj_get_height(label);
            previous_x = lv_obj_get_x(previous);
            previous_y = lv_obj_get_y(previous);
            next_x = lv_obj_get_x(next);
            next_y = lv_obj_get_y(next);
        }
        assert(lv_obj_get_x(label) == label_x && lv_obj_get_y(label) == label_y);
        assert(lv_obj_get_width(label) == width && lv_obj_get_height(label) == height);
        assert(lv_obj_get_x(previous) == previous_x && lv_obj_get_y(previous) == previous_y);
        assert(lv_obj_get_x(next) == next_x && lv_obj_get_y(next) == next_y);
        if (seconds == 60) {
            assert(strcmp(lv_label_get_text(label), "01:00") == 0);
            memcpy(at_one_minute, frame, sizeof(frame));
            screenshot(directory, "blend_01_00");
        } else if (seconds == 75) {
            assert(strcmp(lv_label_get_text(label), "01:15") == 0);
            const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
            lv_point_t prefix;
            lv_txt_get_size(&prefix, "01:", font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            /* 原生标签旋转 90 度；分钟和冒号区域的像素必须完全不动。 */
            for (int row = label_y; row < label_y + prefix.x; ++row) {
                for (int column = label_x - font->line_height; column <= label_x; ++column) {
                    unsigned pixel = row * 320 + column;
                    assert(frame[pixel].full == at_one_minute[pixel].full);
                }
            }
            screenshot(directory, "blend_01_15");
        }
        if (seconds != 180) ui_plantmilk_move_selection(1);
    }
    puts("PASS: Blending Time 00:30..03:00 keeps label bounds and arrows fixed; 01:00 to 01:15 leaves every minute/colon pixel unchanged");
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("Initializing LVGL");
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
    assert(ui_is_initialized());
    test_layer_memory_failure(disp);
    test_encoder_navigation(argv[1]);
    ui_plantmilk_check_update(true, true);

    ui_show_page(UI_PAGE_PLANTMILK_SELECT);
    for (int direction = -1; direction <= 1; direction += 2) {
        ui_plantmilk_select_recipe(UI_PM_OAT);
        int expected = UI_PM_SELECT_OAT;
        for (unsigned turn = 0; turn < 10; ++turn) {
            expected += direction;
            if (expected > UI_PM_SELECT_BACK) expected = UI_PM_SELECT_OAT;
            if (expected < UI_PM_SELECT_OAT) expected = UI_PM_SELECT_BACK;
            ui_plantmilk_move_selection(direction);
            assert(ui_get_selected_image(UI_PAGE_PLANTMILK_SELECT) == expected);
            if (expected != UI_PM_SELECT_BACK)
                assert(ui_plantmilk_get_recipe() == expected - UI_PM_SELECT_OAT);
        }
    }
    check_navigation_bounds(UI_PAGE_PLANTMILK_ADD_INGREDIENTS, UI_PM_STEP_BACK, UI_PM_STEP_ACTION);
    check_navigation_bounds(UI_PAGE_PLANTMILK_MACHINE_CHECK, UI_PM_STEP_BACK, UI_PM_STEP_ACTION);
    check_navigation_bounds(UI_PAGE_PLANTMILK_READY, UI_PM_READY_COLLECT, UI_PM_READY_CLEAN);
    check_navigation_bounds(UI_PAGE_PLANTMILK_STOP_CONFIRM, UI_PM_STOP_KEEP, UI_PM_STOP_STOP);
    check_navigation_bounds(UI_PAGE_PLANTMILK_MAKING, 0, 0);
    check_navigation_bounds(UI_PAGE_PLANTMILK_READY_NO_FILTER, 1, 1);
    check_navigation_bounds(UI_PAGE_PLANTMILK_STOPPING, UI_IMAGE_INDEX_NONE, UI_IMAGE_INDEX_NONE);
    check_navigation_bounds(UI_PAGE_PLANTMILK_STOPPED, 1, 1);
    check_navigation_bounds(UI_PAGE_PLANTMILK_OPTIONS, UI_PM_OPTIONS_BACK, UI_PM_OPTIONS_CONTINUE);
    ui_plantmilk_reset();

    /* 遍历和重置第二个功能时，豆腐参数必须保持不变。 */
    ui_select_firm_tofu_control(UI_FIRM_TOFU_CONTROL_AMOUNT);
    ui_adjust_firm_tofu_value(1);
    ui_select_firm_tofu_control(UI_FIRM_TOFU_CONTROL_COAGULANT);
    ui_adjust_firm_tofu_value(1);
    ui_tofu_amount_t old_amount = ui_get_selected_tofu_amount();
    ui_coagulant_t old_coagulant = ui_get_selected_coagulant();
    PlantMilkUiEnter();
    check_page(UI_PAGE_PLANTMILK_SELECT);
    screenshot(argv[1], "select");
    for (unsigned i = 0; i < UI_PM_RECIPE_COUNT; ++i) {
        ui_plantmilk_select_recipe((ui_plantmilk_recipe_t)i);
        assert(ui_plantmilk_get_recipe() == i);
        static const char *const recipe_frames[] = {
            "select_oat", "select_nut", "select_soy", "select_custom"
        };
        screenshot(argv[1], recipe_frames[i]);
        lv_obj_t *description = lv_obj_get_child(ui_Screen, UI_MAX_PAGE_IMAGES + 2);
        lv_area_t description_area, screen_area;
        lv_obj_get_coords(description, &description_area);
        lv_obj_get_transformed_area(description, &description_area, true, false);
        lv_obj_get_coords(ui_Screen, &screen_area);
        /* Native y is the visual horizontal axis after the 90-degree rotation. */
        assert(abs(description_area.y1 + description_area.y2 -
                   screen_area.y1 - screen_area.y2) <= 2);
        printf("PASS: recipe %u description centered after rotation\n", i);
        assert(lv_img_get_angle(lv_obj_get_child(ui_Screen, UI_PM_SELECT_RING)) == i * 900);
        if (i == UI_PM_NUT) screenshot(argv[1], "select_nut");
        press(UI_PM_SELECT_OAT + i);
        check_page(UI_PAGE_PLANTMILK_OPTIONS);
        assert(lv_img_get_angle(lv_obj_get_child(ui_Screen, UI_PM_SELECT_RING)) == 0);
        assert(ui_plantmilk_get_options()->volume_ml == 1200);
        assert(ui_plantmilk_get_options()->cook == (i == UI_PM_SOY));
        press(UI_PM_OPTIONS_BACK);
        check_page(UI_PAGE_PLANTMILK_SELECT);
    }
    ui_plantmilk_select_recipe(UI_PM_OAT);
    press(UI_PM_SELECT_OAT);
    screenshot(argv[1], "options");

    for (unsigned control = UI_PM_OPTIONS_BLEND; control <= UI_PM_OPTIONS_FILTER; ++control) {
        if (control == UI_PM_OPTIONS_COOK || control == UI_PM_OPTIONS_FILTER) continue;
        press(control);
        assert(ui_plantmilk_is_editing());
        for (unsigned i = 0; i < 1000; ++i) ui_plantmilk_move_selection(1);
        const ui_plantmilk_options_t *p = ui_plantmilk_get_options();
        if (control == UI_PM_OPTIONS_BLEND) assert(p->blending_seconds == UI_PM_BLEND_MAX_SECONDS);
        if (control == UI_PM_OPTIONS_SPEED) assert(p->blade_speed == UI_PM_BLADE_MAX);
        if (control == UI_PM_OPTIONS_VOLUME) assert(p->volume_ml == UI_PM_VOLUME_MAX_ML);
        assert(ui_plantmilk_active_image_src(UI_PAGE_PLANTMILK_OPTIONS, UI_PM_OPTIONS_NEXT, false) == &ui_img_pm_1408384802);
        for (unsigned i = 0; i < 1000; ++i) ui_plantmilk_move_selection(-1);
        if (control == UI_PM_OPTIONS_BLEND) assert(p->blending_seconds == UI_PM_BLEND_MIN_SECONDS);
        if (control == UI_PM_OPTIONS_SPEED) assert(p->blade_speed == UI_PM_BLADE_MIN);
        if (control == UI_PM_OPTIONS_VOLUME) assert(p->volume_ml == UI_PM_VOLUME_MIN_ML);
        assert(ui_plantmilk_active_image_src(UI_PAGE_PLANTMILK_OPTIONS, UI_PM_OPTIONS_PREVIOUS, false) == &ui_img_pm_disabled_png);
        press(control);
    }
    /* 开关参数双向循环；豆浆 Cook 可选中但固定为 On。 */
    for (unsigned i = 0; i < UI_PM_RECIPE_COUNT; ++i) {
        ui_plantmilk_select_recipe((ui_plantmilk_recipe_t)i);
        ui_plantmilk_restore_defaults();
        assert(ui_plantmilk_get_options()->cook == (i == UI_PM_SOY));
        for (unsigned focus = 0; focus < UI_PM_OPTIONS_CONTROL_COUNT; ++focus) {
            ui_select_image(UI_PAGE_PLANTMILK_OPTIONS, focus);
            lv_obj_t *hint = lv_obj_get_child(ui_Screen, UI_MAX_PAGE_IMAGES + 11);
            assert(lv_obj_has_flag(hint, LV_OBJ_FLAG_HIDDEN) == (focus != UI_PM_OPTIONS_COOK));
            assert(strcmp(lv_label_get_text(hint), i == UI_PM_SOY ? "Soy milk must be cooked" : "Do not use raw ingredients") == 0);
            if (i == UI_PM_OAT && focus == UI_PM_OPTIONS_COOK) screenshot(argv[1], "options_oat_cook_hint");
        }
        for (unsigned control = UI_PM_OPTIONS_COOK; control <= UI_PM_OPTIONS_FILTER; control += 2) {
            press(control);
            const ui_plantmilk_options_t *p = ui_plantmilk_get_options();
            if (i == UI_PM_SOY && control == UI_PM_OPTIONS_COOK) {
                assert(!ui_plantmilk_is_editing());
                assert(p->cook);
                assert(ui_plantmilk_image_hidden(UI_PAGE_PLANTMILK_OPTIONS, UI_PM_OPTIONS_PREVIOUS));
                assert(ui_plantmilk_image_hidden(UI_PAGE_PLANTMILK_OPTIONS, UI_PM_OPTIONS_NEXT));
                ui_select_image(UI_PAGE_PLANTMILK_OPTIONS, UI_PM_OPTIONS_SPEED);
                ui_plantmilk_move_selection(1);
                assert(ui_get_selected_image(UI_PAGE_PLANTMILK_OPTIONS) == UI_PM_OPTIONS_COOK);
                screenshot(argv[1], "options_soy_cook_fixed");
                PlantMilkUiHandleShortPress();
                assert(!ui_plantmilk_is_editing() && p->cook);
                ui_plantmilk_move_selection(1);
                assert(ui_get_selected_image(UI_PAGE_PLANTMILK_OPTIONS) == UI_PM_OPTIONS_VOLUME);
                ui_plantmilk_move_selection(-1);
                assert(ui_get_selected_image(UI_PAGE_PLANTMILK_OPTIONS) == UI_PM_OPTIONS_COOK);
                PlantMilkUiHandleShortPress();
                assert(!ui_plantmilk_is_editing() && p->cook);
                ui_plantmilk_move_selection(-1);
                assert(ui_get_selected_image(UI_PAGE_PLANTMILK_OPTIONS) == UI_PM_OPTIONS_SPEED);
                continue;
            }
            assert(ui_plantmilk_is_editing());
            const int directions[] = {1, 1, -1, -1, 1, -1};
            bool expected = control == UI_PM_OPTIONS_COOK ? p->cook : p->use_filter;
            for (unsigned turn = 0; turn < sizeof(directions) / sizeof(directions[0]); ++turn) {
                ui_plantmilk_move_selection(directions[turn]);
                expected = !expected;
                assert((control == UI_PM_OPTIONS_COOK ? p->cook : p->use_filter) == expected);
                assert(ui_plantmilk_active_image_src(UI_PAGE_PLANTMILK_OPTIONS, UI_PM_OPTIONS_PREVIOUS, false) == &ui_img_pm_previous_png);
                assert(ui_plantmilk_active_image_src(UI_PAGE_PLANTMILK_OPTIONS, UI_PM_OPTIONS_NEXT, false) == &ui_img_pm_next_png);
                if (i == UI_PM_SOY) assert(p->cook);
            }
            press(control);
        }
        /* 豆浆的其他数值参数仍可编辑。 */
        if (i == UI_PM_SOY) {
            press(UI_PM_OPTIONS_BLEND);
            ui_plantmilk_move_selection(1);
            assert(ui_plantmilk_get_options()->blending_seconds == 45);
            press(UI_PM_OPTIONS_BLEND);
            press(UI_PM_OPTIONS_SPEED);
            ui_plantmilk_move_selection(1);
            assert(ui_plantmilk_get_options()->blade_speed == 4);
            press(UI_PM_OPTIONS_SPEED);
            press(UI_PM_OPTIONS_VOLUME);
            ui_plantmilk_move_selection(-1);
            assert(ui_plantmilk_get_options()->volume_ml == 1100);
            press(UI_PM_OPTIONS_VOLUME);
            ui_plantmilk_restore_defaults();
            assert(ui_plantmilk_get_options()->cook);
        }
    }
    ui_plantmilk_select_recipe(UI_PM_OAT);
    press(UI_PM_OPTIONS_RESTORE);
    press(UI_PM_OPTIONS_BLEND);
    screenshot(argv[1], "options_edit_blend");
    press(UI_PM_OPTIONS_BLEND);
    press(UI_PM_OPTIONS_VOLUME);
    screenshot(argv[1], "options_edit_volume");
    press(UI_PM_OPTIONS_VOLUME);
    press(UI_PM_OPTIONS_CONTINUE);
    check_page(UI_PAGE_PLANTMILK_ADD_INGREDIENTS);
    screenshot(argv[1], "add");
    press(UI_PM_STEP_BACK);
    check_page(UI_PAGE_PLANTMILK_OPTIONS);
    press(UI_PM_OPTIONS_CONTINUE);
    press(UI_PM_STEP_ACTION);
    check_page(UI_PAGE_PLANTMILK_MACHINE_CHECK);

    PlantMilkUiConfigure(false, false);
    for (unsigned inputs = 0; inputs < 4; ++inputs) {
        sensorState[lidAvail] = !(inputs & 1U);
        sensorState[waterBoxAvail] = !(inputs & 2U);
        TestSensorSnapshotUpdate();
        PlantMilkUiTick();
        assert(ui_plantmilk_can_start() == (inputs == 3));
        types = orders = 0;
        if (inputs != 3) {
            ui_select_image(UI_PAGE_PLANTMILK_MACHINE_CHECK, UI_PM_STEP_ACTION);
            assert(ui_get_selected_image(UI_PAGE_PLANTMILK_MACHINE_CHECK) == UI_PM_STEP_BACK);
            check_page(UI_PAGE_PLANTMILK_MACHINE_CHECK);
            assert(types == 0 && orders == 0);
        }
        if (inputs == 0) screenshot(argv[1], "check_missing");
    }
    screenshot(argv[1], "check_ready");
    /* 机器检查页允许返回；任务忙或事件接口不可用时，不跳入制作页。 */
    press(UI_PM_STEP_BACK);
    check_page(UI_PAGE_PLANTMILK_ADD_INGREDIENTS);
    press(UI_PM_STEP_ACTION);
    flow = SELFCLEAN_SEC;
    types = orders = 0;
    press(UI_PM_STEP_ACTION);
    check_page(UI_PAGE_PLANTMILK_MACHINE_CHECK);
    assert(types == 0 && orders == 0);
    flow = NOTASK_SEC;
    FuncOrderGroup = NULL;
    press(UI_PM_STEP_ACTION);
    check_page(UI_PAGE_PLANTMILK_MACHINE_CHECK);
    assert(types == 0 && orders == 0);
    FuncOrderGroup = (void *)2;
    start(false);
    /* 任务尚未开始运行时，长按停止不能提前跳入已停止页。 */
    PlantMilkUiHandleLongPress();
    check_page(UI_PAGE_PLANTMILK_MAKING);
    assert(orders == FuncStart_bit);
    flow = MILK_SEC;
    PlantMilkUiTick();
    screenshot(argv[1], "making");
    PlantMilkUiHandleShortPress();
    check_page(UI_PAGE_PLANTMILK_STOP_CONFIRM);
    screenshot(argv[1], "stop_confirm");
    press(UI_PM_STOP_KEEP);
    check_page(UI_PAGE_PLANTMILK_MAKING);
    PlantMilkUiHandleShortPress();
    types = orders = 0;
    press(UI_PM_STOP_STOP);
    check_page(UI_PAGE_PLANTMILK_STOPPING);
    assert(types == (1U << MILK_SEC) && orders == FuncStop_bit);
    screenshot(argv[1], "stopping");
    PlantMilkUiHandleShortPress();
    check_page(UI_PAGE_PLANTMILK_STOPPING);
    flow = NOTASK_SEC;
    PlantMilkUiTick();
    check_page(UI_PAGE_PLANTMILK_STOPPED);
    screenshot(argv[1], "stopped");
    types = orders = 0;
    PlantMilkUiHandleShortPress();
    check_page(UI_PAGE_CLEAN_PREPARE_CLEAN);
    assert(types == 0 && orders == 0);

    /* 停止确认弹窗打开时，无滤网制作自然结束也必须进入完成页。 */
    ui_show_page(UI_PAGE_PLANTMILK_OPTIONS);
    press(UI_PM_OPTIONS_FILTER);
    ui_plantmilk_move_selection(-1);
    press(UI_PM_OPTIONS_FILTER);
    assert(!ui_plantmilk_get_options()->use_filter);
    start(false);
    flow = MILK_SEC;
    PlantMilkUiTick();
    PlantMilkUiHandleShortPress();
    check_page(UI_PAGE_PLANTMILK_STOP_CONFIRM);
    flow = NOTASK_SEC;
    PlantMilkUiTick();
    check_page(UI_PAGE_PLANTMILK_READY_NO_FILTER);
    ui_plantmilk_restore_defaults();

    /* Clean 仅导航到准备页；任务忙或事件接口缺失也不能在此处启动硬件。 */
    start(false);
    flow = MILK_SEC;
    PlantMilkUiTick();
    now += 60000;
    PlantMilkUiTick();
    assert(strcmp(ui_plantmilk_active_label_text(UI_PAGE_PLANTMILK_MAKING, 2), "About 13 min left") == 0);
    flow = NOTASK_SEC;
    PlantMilkUiTick();
    check_page(UI_PAGE_PLANTMILK_READY);
    flow = MILK_SEC;
    types = orders = 0;
    press(UI_PM_READY_CLEAN);
    check_page(UI_PAGE_CLEAN_PREPARE_CLEAN);
    assert(types == 0 && orders == 0);
    flow = NOTASK_SEC;
    FuncTypeGroup = NULL;
    ui_show_page(UI_PAGE_PLANTMILK_READY);
    press(UI_PM_READY_CLEAN);
    check_page(UI_PAGE_CLEAN_PREPARE_CLEAN);
    assert(types == 0 && orders == 0);
    FuncTypeGroup = (void *)1;
    ui_show_page(UI_PAGE_PLANTMILK_READY);
    press(UI_PM_READY_CLEAN);
    check_page(UI_PAGE_CLEAN_PREPARE_CLEAN);
    assert(types == 0 && orders == 0);

    /* 停止按钮按下前任务恰好结束，直接进入已停止页，不重复下发停止命令。 */
    start(false);
    flow = MILK_SEC;
    PlantMilkUiTick();
    PlantMilkUiHandleShortPress();
    flow = NOTASK_SEC;
    types = orders = 0;
    press(UI_PM_STOP_STOP);
    check_page(UI_PAGE_PLANTMILK_STOPPED);
    assert(types == 0 && orders == 0);

    start(true);
    now += 5000;
    PlantMilkUiTick();
    check_page(UI_PAGE_PLANTMILK_READY);
    screenshot(argv[1], "ready_filter");
    press(UI_PM_READY_COLLECT);
    check_page(UI_PAGE_PLANTMILK_READY_NO_FILTER);
    screenshot(argv[1], "ready_no_filter");
    PlantMilkUiHandleShortPress();
    check_page(UI_PAGE_CLEAN_PREPARE_CLEAN);
    start(true);
    PlantMilkUiHandleLongPress();
    check_page(UI_PAGE_PLANTMILK_STOPPING);
    now += 1999;
    PlantMilkUiTick();
    check_page(UI_PAGE_PLANTMILK_STOPPING);
    ++now;
    PlantMilkUiTick();
    check_page(UI_PAGE_PLANTMILK_STOPPED);
    PlantMilkUiHandleShortPress();
    check_page(UI_PAGE_CLEAN_PREPARE_CLEAN);
    assert(types == 0 && orders == 0);
    PlantMilkUiEnter();
    press(UI_PM_SELECT_BACK);
    check_page(UI_PAGE_HOME);

    assert(ui_get_selected_tofu_amount() == old_amount);
    assert(ui_get_selected_coagulant() == old_coagulant);
    puts("PASS: real LVGL render, cyclic recipe navigation, bounded options/flow navigation, bounded numeric edits/arrows, cyclic booleans, Soy Cook locked On, sensor gates, normal/validation lifecycle, stopping lockout, Tofu state isolation");
    test_clean_completion();
    test_shared_ui_mode();
    test_external_flow_navigation();
    test_external_tofu_reentry();
    test_sensor_snapshot_validity();
    test_post_making_clean();
    test_sensor_focus_lock(argv[1]);
    test_blending_time_stability(argv[1]);
    test_tofu_collect_button();
    ui_show_page(UI_PAGE_PLANTMILK_SELECT);
    lv_mem_monitor_t memory;
    for (unsigned turn = 0; turn < 200; ++turn) {
        ui_plantmilk_select_recipe(turn % UI_PM_RECIPE_COUNT);
        lv_tick_inc(200);
        lv_refr_now(NULL);
        assert(lv_mem_test() == LV_RES_OK);
    }
    ui_plantmilk_select_recipe(UI_PM_CUSTOM);
    screenshot(argv[1], "select_custom_final");
    lv_mem_monitor(&memory);
    assert(rendered_layers > 200U);
    printf("PASS: 200 recipe switches, %u layers, largest=%u B; pool=%u B, peak=%u B, largest free=%u B\n",
           rendered_layers, largest_layer_bytes, (unsigned)memory.total_size,
           (unsigned)memory.max_used, (unsigned)memory.free_biggest_size);
    return 0;
}
