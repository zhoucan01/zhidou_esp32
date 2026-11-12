#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"

#include "driver/gpio.h"
#include "sys.h"
#include "nvs_flash.h"
#include <esp_log.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "SoyMilkMake.h"
#include "SelfClean.h"
#include "Milk.h"
#include "usart.h"

#include "FunctionSelection.h"
#include "FaultDeal.h"

#include "UsartExchange.h"
#include "SystemMoniter.h"
#include "Reservation.h"
#include "DataCenter.h"
#include "ModbusMap.h"
#include "DebugConsole.h"
#include "DryBoilTask.h"

#include "NTP.h"

#include "BlueDataDeal.h"

#include "Bluetooch.h"
#include "GATT.h"

#include "wifi.h"
#include "WiFiConnect.h"
#include "OTA.h"
#include "OTAService.h"
#include "WiFiPortal.h"

#include "Music.h"

#include "Press.h"
#include "crc.h"

#include "RGB.h"

#include "driver\gpio.h"
#include "MyEncoder.h"
#include "lv_conf.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "ui.h"
#include "myLvgl.h"


/* ========================================================================
 * Default Modbus pressure test configuration (compile-time project switch)
 * ========================================================================
 * These macros control whether DataCenterTask automatically enters the
 * acquisition pressure-test loop after boot, instead of normal business
 * polling. Define them here (main.c) as the project configuration entry;
 * DataCenter.c reads them at runtime via DataCenterScheduleDefaultTest().
 *
 *   MODBUS_TEST_DEFAULT_MODE
 *     0 = disabled (default; normal business polling, production)
 *     1 = normal pressure test  (business timeouts 100 ms / 20 ms)
 *     2 = violent pressure test (short 10 ms / 5 ms timeouts, fast fail)
 *
 *   MODBUS_TEST_DEFAULT_SLAVE
 *     Target slave: 0x02 = actuator (UART2), 0x03 = power (UART1).
 *
 *   MODBUS_TEST_DEFAULT_START_DELAY_MS
 *     Delay between boot and test start, so LCD/Wi-Fi/BLE can settle.
 *     Default 10000 ms (10 s). Set to 0 to disable the schedule.
 *
 * Production firmware MUST keep MODBUS_TEST_DEFAULT_MODE at 0. Only enable
 * for bench / stress testing. The operator can still stop a running test
 * with "TEST:STOP" on the debug console.
 */
#ifndef MODBUS_TEST_DEFAULT_MODE
#define MODBUS_TEST_DEFAULT_MODE           MODBUS_TEST_DEFAULT_MODE_DISABLED
#endif
#ifndef MODBUS_TEST_DEFAULT_SLAVE
#define MODBUS_TEST_DEFAULT_SLAVE          0x02U
#endif
#ifndef MODBUS_TEST_DEFAULT_START_DELAY_MS
#define MODBUS_TEST_DEFAULT_START_DELAY_MS 10000U
#endif

/* 仅首帧异常慢时触发的上限，正常情况下完成即继续，不固定等待。 */
#define UI_FIRST_FRAME_WAIT_MS 1500U

/* 主任务周期诊断：打印当前工艺和系统堆内存，设为 0 可关闭。
 * 默认每 5 秒打印一次，避免诊断日志本身影响串口和 UI 响应。 */
#define MAIN_RUNTIME_LOG_ENABLE 0
#define MAIN_RUNTIME_LOG_PERIOD_MS 500U

// 声明显示任务句柄
extern TaskHandle_t DisplayTaskHandle;
// 声明制作豆浆任务句柄
extern TaskHandle_t TouFuHandle;
// 待机待机任务句柄
extern TaskHandle_t standbyTaskHandle;
//电流互斥锁
extern SemaphoreHandle_t current_mutex;
extern QueueHandle_t ReservationQueue;
extern EventGroupHandle_t FunctionSelectionEventGroup;

extern uint16_t DatafdTempServerHandle; 
extern uint16_t DatafdPresServerHandle;
extern uint16_t DatafdTimeServerHandle; 
extern uint16_t SysMoniterServerHandle;

Reservation_t TestReservation; // 测试预约变量

static TaskHandle_t dry_boil_handle;

static const my_encoder_task_config_t encoder_config = {
    .boot_hold_callback = WiFiPortalRequestStart,
};

/* 站2（下位机）编码器数据打印：每秒打印一次 Modbus 上报的累计圈数与三路编码器
 * 位置，用来确认下位机数据是否正常。调试用，默认关闭；把下面这个宏改成 1 即可
 * 重新打开（任务和创建点都被这个宏包住）。 */
#define MODBUS_ENCODER_LOG_ENABLE 0

#if MODBUS_ENCODER_LOG_ENABLE
static void ModbusEncoderLogTask(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        ESP_LOGI("encdata",
                 "turns=%.3f riseL=%" PRId32 " riseR=%" PRId32 " pressR=%" PRId32,
                 getTurns(), g_DataPool.riseLeftEncoderPos,
                 g_DataPool.riseRightEncoderPos,
                 g_DataPool.pressRightEncoderPos);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

#if MAIN_RUNTIME_LOG_ENABLE
static void PrintHeapUsage(const char *name, uint32_t caps)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, caps);
    const size_t total = heap_caps_get_total_size(caps);
    const size_t used = total >= info.total_free_bytes
                            ? total - info.total_free_bytes : 0U;
    const unsigned used_percent = total == 0U ? 0U :
        (unsigned)((uint64_t)used * 100U / total);

    ESP_LOGI("Runtime", "%s heap: used=%u/%u B (%u%%), free=%u B, "
             "largest=%u B, min_free=%u B",
             name, (unsigned)used, (unsigned)total, used_percent,
             (unsigned)info.total_free_bytes,
             (unsigned)info.largest_free_block,
             (unsigned)info.minimum_free_bytes);
}

static void PrintRuntimeStatus(void)
{
    static const char *const flow_names[FUNCSEC_NUM] = {
        [SOYMILK_SEC] = "SoyMilk",
        [PRESSTOUFU_SEC] = "Press",
        [TOUFU_SEC] = "Tofu",
        [MILK_SEC] = "PlantMilk",
        [SELFCLEAN_SEC] = "SelfClean",
        [BRINE_SEC] = "Brine",
        [NOTASK_SEC] = "Idle",
    };
    static const char *const order_names[FUNCSECORDER_NUM] = {
        [FUNCSEC_START] = "Start",
        [FUNCSEC_PAUSE] = "Pause",
        [FUNCSEC_RESUME] = "Resume",
        [FUNCSEC_STOP] = "Stop",
    };
    const FuncSecType_t flow = GetCurTaskState();
    const FuncSecOrder_t order = GetCurTaskOrder();
    const char *flow_name = (unsigned)flow < FUNCSEC_NUM
                               ? flow_names[flow] : "Unknown";
    const char *order_name = (unsigned)order < FUNCSECORDER_NUM
                                ? order_names[order] : "Unknown";

    /* flow/order 是工艺调度状态，rtos_tasks 是系统任务总数。
     * 内存数值统计系统可分配堆，不代表静态数据和 LVGL 私有池的细分占用。 */
    ESP_LOGI("Runtime", "flow=%s(%u), order=%s(%u), rtos_tasks=%u",
             flow_name, (unsigned)flow, order_name, (unsigned)order,
             (unsigned)uxTaskGetNumberOfTasks());
    PrintHeapUsage("INTERNAL", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    PrintHeapUsage("PSRAM", MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
#endif

static void StorageInit(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void app_main(void)
{
    /* 在 NVS、蓝牙、Wi-Fi 等初始化之前关闭背光，隐藏 LCD 上电随机显存。 */
    ScreenBacklightHoldOff();
    const int64_t display_boot_started_us = esp_timer_get_time();
    ESP_LOGI("main", "Modbus test config: mode=%u slave=0x%02X delay_ms=%lu",
             MODBUS_TEST_DEFAULT_MODE, MODBUS_TEST_DEFAULT_SLAVE,
             (unsigned long)MODBUS_TEST_DEFAULT_START_DELAY_MS);

    ESP_LOGI("main", "Initializing NVS");
    StorageInit();
    ESP_LOGI("main", "NVS initialized");
    BluetoothPrepareDeviceName();
    //初始化串口
    ESP_LOGI("main", "Initializing UART");
    uart_init();
    esp_log_level_set("NimBLE", ESP_LOG_WARN); 
    esp_log_level_set("wifi", ESP_LOG_WARN);    // 只显示警告及以上
    esp_log_level_set("coexist", ESP_LOG_WARN);
    ScreenInit();
    ESP_LOGI("DisplayBoot", "LCD ready: %" PRId64 " ms from app entry",
             (esp_timer_get_time() - display_boot_started_us) / 1000);
    /* 只保存目标亮度；首帧传输完成后由 UI 任务实际点亮。 */
    ScreenSetBrightness(100);


    lv_init();
    lv_port_disp_init();
    ui_init();
    ESP_LOGI("DisplayBoot", "UI ready: %" PRId64 " ms from app entry",
             (esp_timer_get_time() - display_boot_started_us) / 1000);

    /* 先在专用 PSRAM 栈上画出首页；通信/业务初始化完前只显示、不处理输入。
     * 参数仅用作启动握手开关，不解引用。 */
    TaskHandle_t startup_ui_task = NULL;
    if (xTaskCreatePinnedToCoreWithCaps(lvUIshowTask, "uiTask", 8192,
                                      (void *)1, 6, &startup_ui_task, 1,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE("main", "Failed to create startup UI task");
        abort();
    }
    const int64_t first_frame_wait_started_us = esp_timer_get_time();
    while (!lv_port_disp_first_frame_ready() &&
           esp_timer_get_time() - first_frame_wait_started_us <
               (int64_t)UI_FIRST_FRAME_WAIT_MS * 1000) {
        vTaskDelay(1);
    }
    if (lv_port_disp_first_frame_ready()) {
        ESP_LOGI("DisplayBoot", "First frame ready: %" PRId64 " ms from app entry",
                 (esp_timer_get_time() - display_boot_started_us) / 1000);
    } else {
        ESP_LOGW("DisplayBoot", "First frame still pending after %u ms; backlight stays gated",
                 UI_FIRST_FRAME_WAIT_MS);
    }

    ESP_LOGI("main", "Initializing Wi-Fi");
    WifiInit();
    ESP_LOGI("main", "Initializing buzzer");
    BeepInit();
    set_device_info("F1", 0, 1);
    ESP_LOGI("main", "Basic peripherals initialized");

#if (MODBUS_TEST_DEFAULT_MODE != MODBUS_TEST_DEFAULT_MODE_DISABLED)
    /* Schedule the default pressure test before creating DataCenterTask,
     * so the task picks up the schedule on its first iteration. The task
     * waits MODBUS_TEST_DEFAULT_START_DELAY_MS after startup before
     * actually entering the test loop. */
    {
        const bool aggressive =
            (MODBUS_TEST_DEFAULT_MODE == MODBUS_TEST_DEFAULT_MODE_VIOLENT);
        ESP_LOGW("main", "Scheduling default Modbus pressure test: "
                 "slave=0x%02X mode=%u delay_ms=%lu aggressive=%d",
                 MODBUS_TEST_DEFAULT_SLAVE, MODBUS_TEST_DEFAULT_MODE,
                 (unsigned long)MODBUS_TEST_DEFAULT_START_DELAY_MS,
                 aggressive);
        DataCenterScheduleDefaultTest(MODBUS_TEST_DEFAULT_SLAVE, aggressive,
                                      MODBUS_TEST_DEFAULT_START_DELAY_MS);
    }
#endif

    /* Create the complete task set atomically. Without suspending scheduling,
     * each newly-created high-priority task can preempt app_main before the
     * UART processing task is created. */
    BaseType_t task_result;
    vTaskSuspendAll();

#define CREATE_TASK_OR_ABORT(call, task_name) do {                         \
        task_result = (call);                                               \
        if (task_result != pdPASS) {                                        \
            /* ESP_LOG takes a mutex and must not run while the scheduler is \
             * suspended. Resume first so the actual allocation failure is  \
             * reported instead of triggering xQueueSemaphoreTake assert. */\
            xTaskResumeAll();                                               \
            ESP_LOGE("main", "Failed to create %s; internal free=%u "      \
                     "largest block=%u", (task_name),                       \
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),\
                     (unsigned)heap_caps_get_largest_free_block(            \
                         MALLOC_CAP_INTERNAL));                             \
            abort();                                                        \
        }                                                                   \
    } while (0)
    // 方向全部改为正方向
    DataCenterSetPowerMotorDirectionMode(MODBUS_POWER_MOTOR_DIRECTION_FORWARD_ONLY);
    /* 固定路由器联网任务：必须在 WifiInit() 之后运行（此处位于暂停调度区间，
    * 任务在 xTaskResumeAll() 之后才真正开始执行）。 */
    CREATE_TASK_OR_ABORT(
        xTaskCreate(WiFiConnectTask, "WiFiConnect", WIFI_CONNECT_TASK_STACK_SIZE,
                    NULL, WIFI_CONNECT_TASK_PRIORITY, NULL),
        "WiFiConnect");
    CREATE_TASK_OR_ABORT(
        xTaskCreate(OTAServiceTask, "OTAServiceTask", OTA_SERVICE_TASK_STACK_SIZE,
                    NULL, OTA_SERVICE_TASK_PRIORITY, NULL),
        "OTAServiceTask");
    CREATE_TASK_OR_ABORT(
        xTaskCreate(WiFiPortalTask, "WiFiPortal", 4096, NULL, 4, NULL),
        "WiFiPortal");
    /* Communication tasks are critical and must get internal stack memory
     * before optional UI / encoder tasks. */
    CREATE_TASK_OR_ABORT(
        xTaskCreate(DataCenterTask, "DataCenterTask", 4096, NULL, 21, NULL),
        "DataCenterTask");
    CREATE_TASK_OR_ABORT(
        xTaskCreate(PowerRecoveryTask, "PowerRecovery", 1536, NULL, 8, NULL),
        "PowerRecovery");
    CREATE_TASK_OR_ABORT(
        xTaskCreate(DebugConsoleTask, "DebugConsole", 4096, NULL, 9, NULL),
        "DebugConsole");
    CREATE_TASK_OR_ABORT(
        xTaskCreate(BlueDataDealTask, "BlueDataDealTask", 4096, NULL, 20, NULL),
        "BlueDataDealTask");
    CREATE_TASK_OR_ABORT(
        xTaskCreate(SystemMonitorTask, "MachineControl", 4096, NULL, 19, NULL),
        "MachineControl");
    CREATE_TASK_OR_ABORT(
        xTaskCreate(FuncMoniterTask, "Function", 4096, NULL, 10, NULL),
        "Function");
    CREATE_TASK_OR_ABORT(
        xTaskCreate(FaultDealTask, "FaultDealTask", 3072, NULL, 18, NULL),
        "FaultDealTask");
    // CREATE_TASK_OR_ABORT(
    //     xTaskCreate(DryBoilTask, "DryBoil", DRY_BOIL_TASK_STACK_SIZE, NULL,
    //                 DRY_BOIL_TASK_PRIORITY, &dry_boil_handle),
    //     "DryBoil");
    CREATE_TASK_OR_ABORT(
        xTaskCreate(enctest, "test", 4096, (void *)&encoder_config, 5, NULL),
        "encoder");

    /* 站2 编码器数据打印（调试用，见 MODBUS_ENCODER_LOG_ENABLE）。 */
#if MODBUS_ENCODER_LOG_ENABLE
    CREATE_TASK_OR_ABORT(
        xTaskCreate(ModbusEncoderLogTask, "encDataLog", 2560, NULL, 5, NULL),
        "encDataLog");
#endif
#undef CREATE_TASK_OR_ABORT
    xTaskResumeAll();
    /* 提前送达的通知会被保留，即使首帧稍慢也不会丢失启动握手。 */
    xTaskNotifyGive(startup_ui_task);


    // while(1)
    // {
    //     valveOn();
    //     ESP_LOGI("main","valveOn");
    //     vTaskDelay(pdMS_TO_TICKS(1000*50));
    //     valveOff();
    //     ESP_LOGI("main","valveOff");
    //     vTaskDelay(pdMS_TO_TICKS(1000*50));
    // }
    // Play_Music();

#if MAIN_RUNTIME_LOG_ENABLE
    while (1) {
        PrintRuntimeStatus();
        vTaskDelay(pdMS_TO_TICKS(MAIN_RUNTIME_LOG_PERIOD_MS));
    }
#endif
}
        
