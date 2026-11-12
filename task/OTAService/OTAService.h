#ifndef OTASERVICE_H
#define OTASERVICE_H

#include <stdbool.h>
#include "OTAVerManager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* OTA 服务总入口（框架版，业务逻辑待填充）。
 *
 * 职责划分：
 *   OTAServiceTask        —— 本模块，只有任务循环与状态调度骨架；
 *   ota_version_manager_* —— 版本查询/下载状态机（OTAVerManager.c），
 *                            无独立 task，由本任务在 RUN 状态下驱动。
 *
 * 任务栈必须留在内部 RAM：esp_ota_write() 期间会关闭 flash cache，
 * 栈若落在 PSRAM 会在升级过程中异常。 */
#define OTA_SERVICE_TASK_STACK_SIZE     8192
#define OTA_SERVICE_TASK_PRIORITY       4

#define OTA_SERVICE_TICK_MS             (100)

/* 任务状态机（由 OTAServiceTask 的 while (1) + switch 驱动）：
 *
 *   IDLE -> INIT -> RUN -> IDLE
 *     ^              |
 *     +--------------+
 *
 * IDLE: 等待外部检查请求或周期性到期
 * INIT: 一次性初始化（复位版本管理器、装载本机版本、升级分区自检）
 * RUN : 非阻塞驱动版本管理器，处理其返回值
 * EXIT: 收尾并删除任务
 */
typedef enum {
    OTA_SERVICE_STATE_IDLE = 0,     /* 空闲，等待请求或定时到期 */
    OTA_SERVICE_STATE_INIT,         /* 复位版本管理器、装载本机版本 */
    OTA_SERVICE_STATE_RUN,          /* 非阻塞推进 OTA 流程 */
    OTA_SERVICE_STATE_EXIT,         /* 收尾并停止任务 */
} ota_service_state_t;

/* 外部触发一轮版本检查的结果码。错误语义固定，不复用：
 *
 *   OK            - 已受理，OTAServiceTask 在下一拍把版本管理器切到 QUERY 并开始
 *                   本轮查询（AT32 → ESP32 依次处理）；
 *   NO_NETWORK    - 当前未联网（未拿到 IP），本轮不发起，需调用方稍后重试；
 *   BUSY          - 已有一轮检查在跑（含已受理未执行），本次不重复入队；
 *   INVALID_STATE - 服务不在 RUN 状态（启动中/正在退出），本次不发起。
 *
 * 触发是"登记请求"语义：返回 OK 表示已排队，不代表查询已完成；查询结果
 * 由 OTAServiceTask 的日志与状态机推进体现。重复调用在 BUSY 期被拒绝，
 * 因此不会叠加多个请求（幂等）。 */
typedef enum {
    OTA_SERVICE_TRIGGER_OK = 0,
    OTA_SERVICE_TRIGGER_NO_NETWORK,
    OTA_SERVICE_TRIGGER_BUSY,
    OTA_SERVICE_TRIGGER_INVALID_STATE,
} ota_service_trigger_ret_t;

void OTAServiceTask(void *pvParameters);

ota_service_state_t OTAServiceGetState(void);
const char *OTAServiceStateName(ota_service_state_t state);

/* 触发结果的名称，用于日志；未知值返回 "UNKNOWN"。 */
const char *OTAServiceTriggerRetName(ota_service_trigger_ret_t ret);

/* 外部触发一轮 OTA 版本检查（UART/蓝牙/上位机等外部入口调用）。
 *
 * 行为：先判定联网状态（WiFiConnectIsConnected()，口径为 STA 是否已拿到 IP），
 * 未联网直接返回 OTA_SERVICE_TRIGGER_NO_NETWORK，不触碰状态机；联网成功则
 * 登记请求并返回 OTA_SERVICE_TRIGGER_OK，由 OTAServiceTask 在下一拍在 RUN
 * 状态里把版本管理器从 IDLE 切到 QUERY。
 *
 * 非阻塞：本函数不含 HTTP 事务，可在任意任务或命令处理上下文中调用；耗时
 * 的云端查询在 OTAServiceTask 上下文执行，不占用调用者栈和优先级。
 *
 * 线程安全：与 OTAServiceTask 之间用临界区保护请求标志与状态判定，可跨任务
 * 调用，不可在中断中调用。 */
ota_service_trigger_ret_t OTAServiceTriggerCheck(void);
/* Query all three targets without downloading or applying firmware. */
ota_service_trigger_ret_t OTAServiceTriggerQuery(void);

/* Firmware version as "major.minor.patch.build", e.g. "0.0.1.2". The four
 * parts are defined in OTAService.c as OTA_SERVICE_FW_MAJOR_VERSION,
 * OTA_SERVICE_FW_MINOR_VERSION, OTA_SERVICE_FW_PATCH_VERSION and
 * OTA_SERVICE_FW_BUILD_VERSION, which map to
 * ESP32S3_VERSION_MAJOR/MINOR/PATCH/BUILD from
 * config/firmware_version.txt.
 * The string is a compile-time constant; never free it. */
const char *OTAServiceGetFirmwareVersion(void);

typedef enum {
    OTA_WEB_IDLE, OTA_WEB_CHECKING, OTA_WEB_DOWNLOADING, OTA_WEB_DOWNLOADED,
    OTA_WEB_LATEST, OTA_WEB_FAILED, OTA_WEB_AVAILABLE
} ota_web_phase_t;
typedef struct {
    ota_web_phase_t phase;
    unsigned received, total;
    bool busy;
    /* Latest version per successful cloud check; no-update uses the queried
     * current version because that response carries no target_version. */
    char at32_version[16], st32_version[16], esp32_version[16];
    /* Last successfully polled versions; empty before the first snapshot. */
    char at32_current[16], st32_current[16];
    bool at32_checked, st32_checked, esp32_checked;
    bool update_available;
} ota_web_status_t;
void OTAServiceReportVersion(ota_fw_target_t target, const char *version, bool available);
void OTAServiceGetProgress(ota_web_status_t *out);
/* Called by the OTA state machine, not by HTTP request handlers. */
void OTAServiceReportProgress(ota_web_phase_t phase, unsigned received, unsigned total);

#ifdef __cplusplus
}
#endif

#endif /* OTASERVICE_H */
