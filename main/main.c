#include <stdio.h>
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

#include "NTP.h"

#include "BlueDataDeal.h"

#include "Bluetooch.h"
#include "GATT.h"

#include "wifi.h"
#include "OTA.h"

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

    ESP_LOGI("main", "Initializing NVS");
    StorageInit();
    ESP_LOGI("main", "NVS initialized");
    check_ota_state();
    BluetoothInit();
    /* BluetoothInit only configures NimBLE. Start its host before any
     * potentially slow UI, LCD, or Wi-Fi initialization. */
    nimble_port_freertos_init(bleprph_host_task);
    //初始化串口
    ESP_LOGI("main", "Initializing UART");
    uart_init();
    ESP_LOGI("main", "Initializing Wi-Fi");
    WifiInit();
    ESP_LOGI("main", "Initializing buzzer");
    BeepInit();
    ESP_LOGI("main", "Basic peripherals initialized");
    
    set_device_info("F1", 0, 1);
    esp_log_level_set("NimBLE", ESP_LOG_WARN); 
    esp_log_level_set("wifi", ESP_LOG_WARN);    // 只显示警告及以上
    esp_log_level_set("coexist", ESP_LOG_WARN);
    ScreenInit();
    ScreenSetBrightness(100);

  
    lv_init();
    lv_port_disp_init();
    ui_init();

    
    /* Create the complete task set atomically. Without suspending scheduling,
     * each newly-created high-priority task can preempt app_main before the
     * UART processing task is created. */
    BaseType_t task_result;
    vTaskSuspendAll();

#define CREATE_TASK_OR_ABORT(call, task_name) do {                         \
        task_result = (call);                                               \
        if (task_result != pdPASS) {                                        \
            ESP_LOGE("main", "Failed to create %s; internal free=%u",      \
                     (task_name),                                           \
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));\
            abort();                                                        \
        }                                                                   \
    } while (0)

    /* Communication tasks are critical and must get internal stack memory
     * before optional UI / encoder tasks. */
    CREATE_TASK_OR_ABORT(
        xTaskCreate(DataCenterTask, "DataCenterTask", 3072, NULL, 21, NULL),
        "DataCenterTask");
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
    CREATE_TASK_OR_ABORT(
        xTaskCreate(enctest, "test", 4096, NULL, 5, NULL),
        "encoder");
    CREATE_TASK_OR_ABORT(
        xTaskCreatePinnedToCoreWithCaps(lvUIshowTask, "uiTask", 8192, NULL,
                                        6, NULL, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        "uiTask");

#undef CREATE_TASK_OR_ABORT
    xTaskResumeAll();

    // while(1)
    // {
    //     ESP_LOGI("main","turn:%f",getTurns());
    //     CheckDelay(1);
    // }
    Play_Music();  
}
        
