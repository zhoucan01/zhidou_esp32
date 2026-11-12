#ifndef OTA_NET_UPGRADE_H
#define OTA_NET_UPGRADE_H

#include <stdbool.h>
#include <stdint.h>

#include "OTAVerManager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* OTANetUpgrade: HTTP 通道固件下载（分段推进，不整包阻塞）。
 *
 * 本模块没有独立 FreeRTOS task，由版本管理器在 OTA_VER_MGR_STATE_DOWNLOAD
 * 中反复调用 ota_upg_process() 驱动。入参是版本管理器自持的 ota_ver_mgr_t，
 * 只读取其中的 fw_url / fw_size / fw_md5 / target，运行期状态全部自持在本模块内。
 *
 * 目标由 ver->target 决定：本机（ESP32）沿用 app_update 写 ota_0/ota_1 并切换
 * 启动分区；下挂芯片（AT32/ST32）直接写 partitions.csv 里对应的数据分区，
 * 不切换启动分区、不需要重启。
 *
 * 状态机（一次调用推进一步）：
 *
 *   IDLE -> CONNECT -> REQUEST -> RESPONSE -> FINISH -> CLOSE -> IDLE (DONE)
 *              |          |          | 失败                      (DONE_PENDING)
 *              +----------+----------+------> CLOSE -> IDLE (FAIL)
 *   EXIT: 立即中止并释放资源。
 *
 *   CONNECT  ：esp_http_client_init + open，建链并发出 GET 请求；
 *   REQUEST  ：fetch_headers 校验 200/长度，取目标分区并准备写入；
 *   RESPONSE ：分片 read + 写入 + md5 累计，读完比对 md5；
 *   FINISH   ：本机 esp_ota_end + esp_ota_set_boot_partition；下挂芯片只校验长度；
 *   CLOSE    ：未提交的写入会话 abort，释放 md5/HTTP 资源并清空运行时字段。
 *
 * 命名规范：类型/函数 snake_case，前缀 ota_upg_；宏全大写前缀 OTA_UPG_；
 * 内部静态变量 s_ 前缀；C99，只用 stdint/stdbool/stddef。 */

/* 单次 HTTP 事务超时（单位 ms），与 components/OTA 的 manual_ota_download 一致。 */
#define OTA_UPG_HTTP_TIMEOUT_MS     30000

/* HTTP 收发缓冲，同时作为 esp_http_client 的 buffer_size（4KB）。 */
#define OTA_UPG_HTTP_BUF_SIZE       4096

/* 每次 ota_upg_process() 最多搬运的固件字节数。
 * 上限避免一次调用长时间占用任务；下限避免 100ms 调度周期把吞吐压得过低。 */
#define OTA_UPG_RX_BUDGET_PER_CALL  (32U * 1024U)

/* 镜像大小上限，超出按协议不符失败（与 manual_ota_download 的 10MB 判定一致）。 */
#define OTA_UPG_MAX_IMAGE_SIZE      (10U * 1024U * 1024U)

/* 下载子状态。 */
typedef enum {
    OTA_UPG_STATE_IDLE = 0,     /* 空闲，等待入参校验 */
    OTA_UPG_STATE_CONNECT,      /* 建立连接 */
    OTA_UPG_STATE_REQUEST,      /* 请求固件并准备写入会话 */
    OTA_UPG_STATE_RESPONSE,     /* 接收固件数据 */
    OTA_UPG_STATE_FINISH,       /* 结束写入并设置启动分区 */
    OTA_UPG_STATE_CLOSE,        /* 释放资源并复位 */
    OTA_UPG_STATE_EXIT,         /* 立即中止 */
} ota_upg_state_t;

/* 推进结果 / 处理接口返回值。
 *   FAIL    - 失败，资源由 CLOSE 阶段释放；
 *   SUCCESS - 本拍推进正常，需继续调用；
 *   BUSY    - 处理接口专用：本拍未搬完，下一拍继续；
 *   DONE    - 推进接口专用：镜像已写入并设为启动分区，重启后生效；
 *   DONE_PENDING - 推进接口专用：镜像已写入数据分区（下挂芯片固件），
 *                  不切换启动分区、不需要重启，由调用方自行决定后续动作。 */
typedef enum {
    OTA_UPG_RET_FAIL = 0,
    OTA_UPG_RET_SUCCESS,
    OTA_UPG_RET_BUSY,
    OTA_UPG_RET_DONE,
    OTA_UPG_RET_DONE_PENDING,
} ota_upg_ret_t;

/* 驱动下载状态机推进一步。
 *
 * ver: 版本管理器状态，只读取 fw_url / fw_size / fw_md5 / target；四者在
 *      IDLE 入口校验，缺项或超长直接 FAIL，下载过程中不复用 ver 的其他字段。
 *      target 决定镜像落盘位置：本机（ESP32）走 app_update 并切换启动分区，
 *      下挂芯片（AT32/ST32）直接写入对应数据分区。
 *
 * 返回值：
 *   OTA_UPG_RET_SUCCESS      - 已前进一步，下一拍继续调用；
 *   OTA_UPG_RET_DONE         - 本机镜像已写入升级分区并设为启动分区（重启后生效）；
 *   OTA_UPG_RET_DONE_PENDING - 下挂芯片镜像已写入数据分区，无需重启；
 *   OTA_UPG_RET_FAIL         - 失败，已回 CLOSE/IDLE，可重新开始。
 *
 * 阻塞性：CONNECT/REQUEST 各含一次 HTTP 同步调用（受
 * OTA_UPG_HTTP_TIMEOUT_MS 约束）；RESPONSE 每次最多搬
 * OTA_UPG_RX_BUDGET_PER_CALL 字节，不会整包阻塞。 */
int ota_upg_process(const ota_ver_mgr_t *ver);

/* ---------- 分步处理接口 ---------- */

/* 建链：esp_http_client_init(url) + esp_http_client_open()，幂等。 */
int ota_upg_handle_connect(const ota_ver_mgr_t *ver);

/* 请求：fetch_headers 取状态码/长度并与 fw_size 比对，取升级分区并
 * esp_ota_begin，初始化 md5 上下文。 */
int ota_upg_handle_request(const ota_ver_mgr_t *ver);

/* 接收：按预算分片 read + esp_ota_write + md5 累计；读完比对 md5。
 * 返回 BUSY 表示本拍未搬完。 */
int ota_upg_handle_response(const ota_ver_mgr_t *ver);

/* 释放：未提交的写入会话 abort，释放 md5 与 HTTP 句柄，清空运行时字段。 */
int ota_upg_handle_close(const ota_ver_mgr_t *ver);

/* 复位到 IDLE 并释放全部资源。 */
void ota_upg_reset(void);

ota_upg_state_t ota_upg_get_state(void);
const char *ota_upg_state_name(ota_upg_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* OTA_NET_UPGRADE_H */
