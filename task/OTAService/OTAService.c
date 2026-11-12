#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include "DataCenter.h"
#include "ModbusMap.h"

#include "OTA.h"
#include "OTAVerManager.h"
#include "OTAService.h"
#include "WiFiConnect.h"

/* 仅搭建结构框架：本文件只包含任务循环与状态调度，
 * 版本查询/下载等业务逻辑全部委托给 OTAVerManager，
 * 具体动作以 TODO 标注，不在本文件实现。
 *
 * 外部触发走 OTAServiceTriggerCheck()：先判定联网，再登记请求，
 * 真正把版本管理器切到 QUERY 的动作留到本任务 RUN 状态里执行，
 * 避免在调用者任务里跑 HTTP 事务。 */

static const char *TAG = "OTAService";

/* 本服务对外发布的固件版本，由四个版本号组成，取值来自 config/firmware_version.txt
 * （主.次.修订.构建 四段点分数字，由 config/firmware_version.cmake 拆成四个宏并在 CMake 中注入）：
 *   MAJOR  - 对应 ESP32S3_VERSION_MAJOR
 *   MINOR  - 对应 ESP32S3_VERSION_MINOR
 *   PATCH  - 对应 ESP32S3_VERSION_PATCH
 *   BUILD  - 对应 ESP32S3_VERSION_BUILD
 * 拼接结果与 config/firmware_version.txt 的四段逐段一致。 */
#define OTA_SERVICE_FW_MAJOR_VERSION     ESP32S3_VERSION_MAJOR
#define OTA_SERVICE_FW_MINOR_VERSION     ESP32S3_VERSION_MINOR
#define OTA_SERVICE_FW_PATCH_VERSION     ESP32S3_VERSION_PATCH
#define OTA_SERVICE_FW_BUILD_VERSION     ESP32S3_VERSION_BUILD

#define OTA_SERVICE_STRINGIFY_(value)    #value
#define OTA_SERVICE_STRINGIFY(value)     OTA_SERVICE_STRINGIFY_(value)
#define OTA_SERVICE_FW_VERSION_STRING                             \
    OTA_SERVICE_STRINGIFY(OTA_SERVICE_FW_MAJOR_VERSION) "."       \
    OTA_SERVICE_STRINGIFY(OTA_SERVICE_FW_MINOR_VERSION) "."       \
    OTA_SERVICE_STRINGIFY(OTA_SERVICE_FW_PATCH_VERSION) "."       \
    OTA_SERVICE_STRINGIFY(OTA_SERVICE_FW_BUILD_VERSION)

const char *OTAServiceGetFirmwareVersion(void)
{
    return OTA_SERVICE_FW_VERSION_STRING;
}

static volatile ota_service_state_t s_state = OTA_SERVICE_STATE_IDLE;
static portMUX_TYPE progress_lock = portMUX_INITIALIZER_UNLOCKED;
static ota_web_status_t web_progress;
static void current_downstream_version(uint8_t slave, uint16_t address, char out[16])
{
    uint16_t registers[2];
    out[0] = '\0';
    if (ModbusCopyCachedInputRegisters(slave, address, 2, registers)) {
        snprintf(out, 16, "%u.%u.%u.%u", (unsigned)(registers[0] >> 8),
                 (unsigned)(registers[0] & 255U), (unsigned)(registers[1] >> 8),
                 (unsigned)(registers[1] & 255U));
    }
}
void OTAServiceGetProgress(ota_web_status_t *out)
{
    taskENTER_CRITICAL(&progress_lock);
    *out = web_progress;
    taskEXIT_CRITICAL(&progress_lock);
    current_downstream_version(MODBUS_SLAVE_ACTUATOR, MB_ACT_INPUT_VERSION_HIGH, out->at32_current);
    current_downstream_version(MODBUS_SLAVE_POWER, MB_POWER_INPUT_VERSION_HIGH, out->st32_current);
}
void OTAServiceReportProgress(ota_web_phase_t phase, unsigned received, unsigned total)
{
    taskENTER_CRITICAL(&progress_lock);
    web_progress.phase = phase;
    web_progress.received = received;
    web_progress.total = total;
    web_progress.busy = phase == OTA_WEB_CHECKING || phase == OTA_WEB_DOWNLOADING || phase == OTA_WEB_DOWNLOADED;
    taskEXIT_CRITICAL(&progress_lock);
}
void OTAServiceReportVersion(ota_fw_target_t target, const char *version, bool available)
{
    taskENTER_CRITICAL(&progress_lock);
    switch (target) {
    case OTA_FW_TARGET_AT32:
        strlcpy(web_progress.at32_version, version, 16);
        web_progress.at32_checked = true;
        break;
    case OTA_FW_TARGET_ST32:
        strlcpy(web_progress.st32_version, version, 16);
        web_progress.st32_checked = true;
        break;
    case OTA_FW_TARGET_ESP32:
        strlcpy(web_progress.esp32_version, version, 16);
        web_progress.esp32_checked = true;
        break;
    default: break;
    }
    web_progress.update_available |= available;
    taskEXIT_CRITICAL(&progress_lock);
}

/* 外部触发标志：由 OTAServiceTriggerCheck() 置位，OTAServiceTask 在 RUN 状态
 * 取走并清零。用临界区而不是原子变量，保证"判定 BUSY"和"置位"是一个整体，
 * 重复触发不会叠加出两轮检查。 */
static portMUX_TYPE s_trigger_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_check_requested = false;
static bool s_query_only;

const char *OTAServiceStateName(ota_service_state_t state)
{
    switch (state) {
    case OTA_SERVICE_STATE_IDLE: return "IDLE";
    case OTA_SERVICE_STATE_INIT: return "INIT";
    case OTA_SERVICE_STATE_RUN:  return "RUN";
    case OTA_SERVICE_STATE_EXIT: return "EXIT";
    default:                     return "UNKNOWN";
    }
}

const char *OTAServiceTriggerRetName(ota_service_trigger_ret_t ret)
{
    switch (ret) {
    case OTA_SERVICE_TRIGGER_OK:            return "OK";
    case OTA_SERVICE_TRIGGER_NO_NETWORK:    return "NO_NETWORK";
    case OTA_SERVICE_TRIGGER_BUSY:          return "BUSY";
    case OTA_SERVICE_TRIGGER_INVALID_STATE: return "INVALID_STATE";
    default:                                return "UNKNOWN";
    }
}

static void ota_service_set_state(ota_service_state_t state)
{
    if (s_state == state) {
        return;
    }
    ESP_LOGI(TAG, "state %s -> %s", OTAServiceStateName(s_state), OTAServiceStateName(state));
    s_state = state;
}

ota_service_state_t OTAServiceGetState(void)
{
    return s_state;
}

/* INIT：只做一次性准备，不触碰网络。 */
static void ota_service_do_init(void)
{
    /* TODO: 调用 ota_dep_store_load_version() 装载本机版本，失败时以
     *       OTA_SERVICE_FW_VERSION_STRING 兜底并记录告警。 */
    /* TODO: 升级分区自检（是否存在 PENDING_VERIFY 镜像、剩余空间是否够用）。
     *       当前沿用既有 check_ota_state()，后续应改为版本管理器的 INIT 阶段。 */
    check_ota_state();

    ota_version_manager_init();

    /* 初始化会清空状态机，任何在 INIT 之前登记、尚未执行的请求都作废，
     * 避免带着旧的触发标志进入 RUN 后立刻发起一轮查询。 */
    taskENTER_CRITICAL(&s_trigger_lock);
    s_check_requested = false;
    taskEXIT_CRITICAL(&s_trigger_lock);
}

/* 联网判定：口径沿用 WiFiConnect 的"STA 是否已拿到 IP"，不自行探测。 */
static bool ota_service_is_network_ready(void)
{
    return WiFiConnectIsConnected();
}

/* RUN 状态里取走外部触发请求：有请求且状态机空闲则切到 QUERY。
 * 状态机忙（上一轮还在跑）时丢弃本次请求，由调用方下次重新触发。 */
static void ota_service_service_trigger(void)
{
    bool requested = false;
    bool query_only;

    taskENTER_CRITICAL(&s_trigger_lock);
    requested = s_check_requested;
    query_only = s_query_only;
    s_check_requested = false;
    taskEXIT_CRITICAL(&s_trigger_lock);

    if (!requested) {
        return;
    }

    ota_ver_mgr_ret_t ret = ota_version_manager_request_start_mode(OTAServiceGetFirmwareVersion(), query_only);
    if (ret != OTA_VER_MGR_RET_SUCCESS) {
        ESP_LOGW(TAG, "triggered check not started, version manager is busy");
        OTAServiceReportProgress(OTA_WEB_FAILED, 0, 0);
        return;
    }

    ESP_LOGI(TAG, "triggered check accepted, query state entered");
}

static ota_service_trigger_ret_t ota_service_trigger(bool query_only)
{
    ota_service_state_t state = s_state;

    /* 只有 RUN 状态才会消费触发标志，其它状态登记了也不会被执行。 */
    if (state != OTA_SERVICE_STATE_RUN) {
        ESP_LOGW(TAG, "trigger rejected, service state %s", OTAServiceStateName(state));
        return OTA_SERVICE_TRIGGER_INVALID_STATE;
    }

    if (!ota_service_is_network_ready()) {
        ESP_LOGW(TAG, "trigger rejected, network is not ready");
        return OTA_SERVICE_TRIGGER_NO_NETWORK;
    }

    /* 已有一轮在跑或已登记未执行时按 BUSY 拒绝，避免重复发起查询。 */
    bool busy = false;

    taskENTER_CRITICAL(&s_trigger_lock);
    if (s_check_requested || !ota_version_manager_is_idle()) {
        busy = true;
    } else {
        s_query_only = query_only;
        taskENTER_CRITICAL(&progress_lock);
        web_progress.at32_version[0] = web_progress.st32_version[0] = web_progress.esp32_version[0] = '\0';
        web_progress.at32_checked = web_progress.st32_checked = web_progress.esp32_checked = false;
        web_progress.update_available = false;
        taskEXIT_CRITICAL(&progress_lock);
        OTAServiceReportProgress(OTA_WEB_CHECKING, 0, 0);
        s_check_requested = true;
    }
    taskEXIT_CRITICAL(&s_trigger_lock);

    if (busy) {
        ESP_LOGW(TAG, "trigger rejected, a check is already running");
        return OTA_SERVICE_TRIGGER_BUSY;
    }

    ESP_LOGI(TAG, "trigger accepted, scheduled on OTAServiceTask");
    return OTA_SERVICE_TRIGGER_OK;
}

ota_service_trigger_ret_t OTAServiceTriggerCheck(void) { return ota_service_trigger(false); }
ota_service_trigger_ret_t OTAServiceTriggerQuery(void) { return ota_service_trigger(true); }

void OTAServiceTask(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "started, firmware %s (major %d, minor %d, patch %d, build %d)",
             OTAServiceGetFirmwareVersion(),
                 OTA_SERVICE_FW_MAJOR_VERSION,
                 OTA_SERVICE_FW_MINOR_VERSION,
                 OTA_SERVICE_FW_PATCH_VERSION,
                 OTA_SERVICE_FW_BUILD_VERSION);

    ota_service_set_state(OTA_SERVICE_STATE_IDLE);

    while (1) {
        switch (s_state) {
        case OTA_SERVICE_STATE_IDLE:
            ota_service_set_state(OTA_SERVICE_STATE_INIT);
            break;

        case OTA_SERVICE_STATE_INIT:
            ota_service_do_init();
            ota_service_set_state(OTA_SERVICE_STATE_RUN);
            break;

        case OTA_SERVICE_STATE_RUN:
            ota_service_service_trigger();
            {
                ota_ver_mgr_ret_t ret = ota_version_manager_process(OTAServiceGetFirmwareVersion());
                if (ret == OTA_VER_MGR_RET_FAIL) OTAServiceReportProgress(OTA_WEB_FAILED, 0, 0);
                else if (ret == OTA_VER_MGR_RET_NO_NEW_VERSION) {
                    ota_web_status_t current;
                    OTAServiceGetProgress(&current);
                    OTAServiceReportProgress(current.phase == OTA_WEB_DOWNLOADED ?
                                             OTA_WEB_DOWNLOADED :
                                             (current.update_available ? OTA_WEB_AVAILABLE : OTA_WEB_LATEST),
                                             current.received, current.total);
                }
                if (ret == OTA_VER_MGR_RET_NO_NEW_VERSION || ret == OTA_VER_MGR_RET_FAIL) {
                    taskENTER_CRITICAL(&progress_lock);
                    web_progress.busy = false;
                    taskEXIT_CRITICAL(&progress_lock);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(OTA_SERVICE_TICK_MS));
            break;

        case OTA_SERVICE_STATE_EXIT:
            //暂时保留，后续用于主动退出OTA任务逻辑
            ESP_LOGW(TAG, "exit requested, stopping supervisor task");
            vTaskDelete(NULL);
            break;

        default:
            ota_service_set_state(OTA_SERVICE_STATE_IDLE);
            break;
        }
    }
}
