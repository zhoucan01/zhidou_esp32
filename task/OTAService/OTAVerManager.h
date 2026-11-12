#ifndef OTA_VER_MANAGER_H
#define OTA_VER_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* OTAVerManager: 版本管理器（框架版，业务逻辑待填充）。
 *
 * 本模块没有独立 FreeRTOS task，由 OTAServiceTask 在 OTA_SERVICE_STATE_RUN
 * 中反复调用 ota_version_manager_process() 驱动。cur_ver 由调用方每拍传入。
 *
 *   - 非阻塞：一次调用只推进一步，绝不在内部 wait/delay/vTaskDelay；
 *     唯一例外是 QUERY 状态的 HTTP 查询，受 OTA_CLOUD_HTTP_TIMEOUT_MS 约束；
 *   - 状态自持：状态机运行在模块内的文件级静态变量上，调用方不持有句柄；
  *   - 不自触发：本模块不轮询、不等定时，IDLE 只等待外部显式启动一轮检查；
  *   - 返回 SUCCESS / FAIL，调用方据此决定"继续调用 / 结束本轮"；
  *     查询接口另用 NO_NEW_VERSION 区分"应答有效但云端无新版本"。
 *
 * 状态机（一轮检查内按 target 依次处理，先 AT32，再 ST32，最后 ESP32）：
 *
 *   IDLE -> QUERY(AT32) -> GET_NEW_VERSION_INFO -> WAIT_VERSION_INFO_REPLY
 *             | 无新版本                              v
 *             |           DOWNLOAD(AT32) -> DONE_PENDING（镜像只落盘）
  *             |                                   | 下挂目标转 IAP
 *             |                                   v
 *             |             AT32_IAP -> DONE（版本核对通过才写 OTA_Info）
 *             |                                   | 顺延下一目标
 *             |                                   v
  *             |                                QUERY(ST32) -> ... -> AT32_IAP(ST32 镜像)
 *             |                                                        | DONE
 *             |                                                        v
  *             |                                QUERY(ESP32) -> ... -> DOWNLOAD
  *             |                                                        | DONE
  *             |                                                        v
  *             |                                        IDLE（重启后生效）
  *             + 查询/取包/下载/IAP 失败 -> IDLE (FAIL，整轮放弃)
  *             + ESP32 无新版本 --------> IDLE (NO_NEW_VERSION)
  *   EXIT: 任意状态收尾并退出。
  *
  * 下挂芯片（AT32 / ST32）分两步：先把镜像下载到各自分区（AT_Firmware /
  * ST_Firmware），再由 OTAUart 按目标站号（0x02 / 0x03）经 MBUS485 推给对应
  * bootloader，实读版本与目标版本一致后才写 OTA_Info，不切换启动分区、不重启
  * 本机；下挂芯片升级失败即整轮放弃，不再升级后续目标。
  * 本机固件（ESP32）保持原有 app_update 流程，写完即重启。
 *
 * 命名规范（本模块）：
 *   - 类型/函数：snake_case，模块前缀 ota_ver_mgr_ / ota_version_manager_
 *   - 枚举常量/宏：全大写，前缀 OTA_VER_MGR_ / OTA_DEP_ / OTA_VERSION_
 *   - 内部静态变量：s_ 前缀
 *   - C 标准：C99（ESP-IDF 以 gnu17 编译），只用 stdint/stdbool/stddef，
 *     不使用 VLA、可变参数宏嵌套和 C++ 特性。
 */

/* ---------- 版本信息结构 ---------- */

#define OTA_VERSION_STRING_MAX_LEN  16  /* "major.minor.patch.build" + '\0' */
#define OTA_VERSION_URL_MAX_LEN     256
#define OTA_VERSION_SHA256_LEN      32

/* 云端固件摘要为 md5，32 个十六进制字符；缓冲区另含 '\0'。 */
#define OTA_VERSION_MD5_HEX_LEN     32

/* 云端下发的升级任务 ID，形如 "task-20261027-2971805e"。
 * 按定长字符串处理：超长视为协议不符按失败上报，不做截断。 */
#define OTA_TASK_ID_MAX_LEN         48

/* arch / protocol / function 三段式版本；本机 ESP32 当前上报的是
 * OTAService 发布的四段式固件版本串（major.minor.patch.build）。
 * version[] 为可上报/入库的字符串形式，三段整数为比较用的规范化形式。 */
typedef struct {
    uint8_t  arch;                                  /* 架构版本 */
    uint8_t  protocol;                              /* 协议版本 */
    uint8_t  function;                              /* 功能版本 */
    char     version[OTA_VERSION_STRING_MAX_LEN];   /* "0.0.1.2" */
    char     url[OTA_VERSION_URL_MAX_LEN];          /* 固件下载地址 */
    uint32_t image_size;                            /* 字节数 */
    uint8_t  sha256[OTA_VERSION_SHA256_LEN];        /* 校验摘要 */
    bool     valid;                                 /* 结构体是否被填充过 */
} ota_version_info_t;

/* ---------- 下载目标 ---------- */

/* 一轮检查里依次处理的目标芯片。序号同时用作 OTA_Info 的槽位下标，
 * 槽位数见 OTAINFO_SLOT_COUNT。
 *
 *   AT32  - 下挂设备固件，镜像写入 AT_Firmware 数据分区，不做启动分区切换；
  *   ST32  - 下挂设备固件，镜像写入 ST_Firmware 数据分区（0xFC2000/248K），
  *           与 AT32 分开存放，同样不做启动分区切换；
  *   ESP32 - 本机固件，镜像写入 ota_0/ota_1，重启后由 bootloader 切换。
  *
  * ST32 与 AT32 除设备站号（AT32 0x02、ST32 0x03）外协议完全一致，因此共用
  * OTAUart 的同一套 IAP 通道，只在传站号处分支。 */
 typedef enum {
     OTA_FW_TARGET_AT32 = 0,                 /* 下挂 AT32F415，站号 0x02 */
     OTA_FW_TARGET_ST32,                     /* 下挂 ST 芯片，站号 0x03 */
     OTA_FW_TARGET_ESP32,                    /* 本机 ESP32 */
 } ota_fw_target_t;

/* ---------- 内部状态机 ---------- */

typedef enum {
    OTA_VER_MGR_STATE_IDLE = 0,             /* 空闲，等待检查请求 */
    OTA_VER_MGR_STATE_QUERY,                /* 发起"查询最新版本"请求 */
    OTA_VER_MGR_STATE_WAIT_QUERY_RESULT,    /* 等待查询应答 */
    OTA_VER_MGR_STATE_GET_NEW_VERSION_INFO, /* 取新版本详情（URL/大小/摘要） */
    OTA_VER_MGR_STATE_WAIT_VERSION_INFO_REPLY, /* 等待详情应答 */
    OTA_VER_MGR_STATE_DOWNLOAD,             /* 分段下载并写入升级分区 */
    OTA_VER_MGR_STATE_AT32_IAP,             /* 把已落盘的 AT32 镜像推给下挂芯片 */
    OTA_VER_MGR_STATE_EXIT,                 /* 收尾并释放资源 */
} ota_ver_mgr_state_t;

/* ---------- 状态机运行时数据 ---------- */

/* 状态机自持在模块内的唯一实例上，调用方不持有句柄。类型公开是为了让
 * 下载通道（OTANetUpgrade.c）能读到本拍要下载的固件信息，但它只读不写。
 * 本模块不 malloc，避免升级过程中的堆碎片影响 esp_ota_write()。 */
typedef struct {
    ota_ver_mgr_state_t  state;             /* 当前状态 */
    /* 本轮正在处理的下载目标，先 AT32 后 ESP32。 */
    ota_fw_target_t target;
    /* 目标芯片的当前版本，作为 current_version 上报云端。AT32 取
     * getVersion()[OTA_AT32_VERSION_SLAVE_INDEX]（站号 0x02 实读版本）。 */
    char     target_cur_ver[OTA_VERSION_STRING_MAX_LEN];
    /* QUERY 阶段云端回报的新版本号，无新版本时为空串。 */
    char     new_version[OTA_VERSION_STRING_MAX_LEN];
    /* QUERY 阶段云端回报的升级任务 ID，无新版本时为空串。 */
    char     task_id[OTA_TASK_ID_MAX_LEN];
    /* GET_NEW_VERSION_INFO 阶段云端回报的固件下载地址。 */
    char     fw_url[OTA_VERSION_URL_MAX_LEN];
    /* GET_NEW_VERSION_INFO 阶段云端回报的固件字节数。 */
    uint32_t fw_size;
    /* GET_NEW_VERSION_INFO 阶段云端回报的固件摘要，32 位小写十六进制串。 */
    char     fw_md5[OTA_VERSION_MD5_HEX_LEN + 1];
} ota_ver_mgr_t;

/* ---------- 返回状态码 ---------- */

/* 状态机推进结果。成功语义细分两档：查询类接口返回 NO_NEW_VERSION 时
 * 表示"云端应答有效、只是没有更新版本"，与"推进正常"区分开，调用方可据此
 * 决定是否继续后续下载状态。 */
typedef enum {
    OTA_VER_MGR_RET_FAIL = 0,               /* 本轮推进失败 */
    OTA_VER_MGR_RET_SUCCESS,                /* 本拍推进正常，且存在新版本 */
    OTA_VER_MGR_RET_NO_NEW_VERSION,         /* 应答有效，但云端无新版本 */
} ota_ver_mgr_ret_t;

/* ---------- 云端接口配置 ---------- */

/* 新版本查询地址由 OTA_CLOUD_BASE_IP + OTA_CLOUD_BASE_PORT + OTA_CLOUD_BASE_API
 * 拼成：http://47.97.0.9:1688/api/ota/check */
#define OTA_CLOUD_BASE_IP           "47.97.0.9"
#define OTA_CLOUD_BASE_PORT         1688
#define OTA_CLOUD_BASE_API          "/api/ota/check"

/* 固件信息查询地址：http://47.97.0.9:1688/api/ota/package?task_id=...，
 * 与查询接口同源，只换路径。 */
#define OTA_CLOUD_PACKAGE_API       "/api/ota/package"

/* 设备身份与鉴权：现有协议固定写死，后续由设备信息接口/安全存储提供。 */
#define OTA_CLOUD_PRODUCT_ID        "PRD-LEGACY-0001"
#define OTA_CLOUD_CHIP_TYPE_ESP32   "ESP32"
#define OTA_CLOUD_CHIP_TYPE_AT32    "AT32F415"
#define OTA_CLOUD_CHIP_TYPE_ST32    "FU6815"
/* 兼容旧调用点：本机固件的芯片类型。 */
#define OTA_CLOUD_CHIP_TYPE         OTA_CLOUD_CHIP_TYPE_ESP32

/* AT32F415 的当前版本不再用宏写死：取自 DataCenter 的 getVersion() 中站号
 * 0x02 那一行（4 个原始字节），上报时拼成 "主.次.修订.构建"，如 "0.0.0.1"。
 * ST 芯片同理由站号 0x03 那一行提供（getVersion() 的第三行）。 */
#define OTA_AT32_VERSION_SLAVE_INDEX 1
#define OTA_ST32_VERSION_SLAVE_INDEX 2

/* 下挂芯片的 Modbus 设备站号。两者共用同一条 485 总线与同一套 IAP 帧格式，
 * 仅站号不同（见各芯片 bootloader 的 README）。 */
#define OTA_AT32_DEVICE_ADDRESS     0x02U
#define OTA_ST32_DEVICE_ADDRESS     0x03U

/* 下挂芯片镜像的存储分区标签，与 partitions.csv 一致：
 *   Firmware_1  0xF84000  248K
 *   Firmware_2  0xFC2000  248K */
#define OTA_FW_PART_LABEL_AT32      "Firmware_1"
#define OTA_FW_PART_LABEL_ST32      "Firmware_2"
#define OTA_CLOUD_TOKEN             "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9." \
                                    "eyJkZXZpY2VfaWQiOiJOVVZFTEwtREVNTy0wMDEiLCJyb2xlIjoiZGV2aWNlIiwiaWF0IjoxNzkyOTgyMjA0LCJleHAiOjE3OTU1NzQyMDR9." \
                                    "la7igbC1KfbHcvp97rRytWZyCmY68hvL4OkecmGG-5c"

/* 单次 HTTP 事务超时（单位 ms）。QUERY 状态整体阻塞时长受它约束。 */
#define OTA_CLOUD_HTTP_TIMEOUT_MS   10000

void ota_version_manager_init(void);

/* 外部触发一轮检查：仅当状态机处于 IDLE 时受理，受理后状态机切到 QUERY，
 * 由随后的 ota_version_manager_process() 逐拍推进。
 *
 * cur_ver: 与 ota_version_manager_process() 相同，本机当前版本字符串
 *          （见 OTAServiceGetFirmwareVersion()）。
 *
 * 返回：OTA_VER_MGR_RET_SUCCESS - 已进入 QUERY，本轮从下挂芯片开始；
 *       OTA_VER_MGR_RET_FAIL    - 状态机不在 IDLE，状态保持不变。
 *
 * 调用约束：由 OTAServiceTask 在 RUN 状态内调用，与
 * ota_version_manager_process() 同任务串行，不得跨任务并发驱动状态机。 */
ota_ver_mgr_ret_t ota_version_manager_request_start(const char *cur_ver);
ota_ver_mgr_ret_t ota_version_manager_request_start_mode(const char *cur_ver, bool query_only);

/* 状态机是否空闲（IDLE）。供外部触发接口判定 BUSY，只读单个状态字段，
 * 可在其它任务里作为"忙/闲"的近似判断使用。 */
bool ota_version_manager_is_idle(void);

/* 驱动状态机推进一步；状态机骨架见 OTAVerManager.c。
 *
 * cur_ver: 本机当前版本字符串，形如 "0.0.1.2"（见 OTAServiceGetFirmwareVersion()），
 *          作为本机固件（ESP32 目标）的 current_version 上报给云端，由调用方
 *          每拍传入。下挂芯片（AT32）的当前版本取自 DataCenter 的
 *          getVersion() 中站号 0x02 那一行，与 cur_ver 无关。
 *
 * 除 QUERY 状态外均不阻塞：QUERY 会同步发起一次 HTTP 查询，最长阻塞
  * OTA_CLOUD_HTTP_TIMEOUT_MS，这是状态机里唯一的阻塞点。IDLE 为被动状态，
  * 本模块不再自行判定联网、不按下发周期轮询，需由外部显式启动一轮检查。 */
ota_ver_mgr_ret_t ota_version_manager_process(const char *cur_ver);

/* ---------- 新版本查询 ---------- */

/* 向云端查询是否有新版本（GET {url}?product_id=&chip_type=&current_version=）。
 *
 * 入参：
 *   url             - 查询地址，由 OTA_CLOUD_BASE_IP/PORT/API 合成；
 *   product_id      - 产品 ID，固定 OTA_CLOUD_PRODUCT_ID；
 *   chip_type       - 芯片类型，取下挂/本机目标的
 *                     ota_fw_target_chip_type(target)（如 "AT32F415"/"ESP32"）；
 *   current_version - 本机当前版本，必须是与 target_version 同格式的 4 段
 *                     "主.次.修订.构建"（如 "0.0.1.2"）；
 *   out_version     - 有新版本时回填云端 target_version，否则置空串，
 *                     长度不得小于 OTA_VERSION_STRING_MAX_LEN；
 *   out_task_id     - 有新版本时回填云端 task_id，否则置空串，
 *                     长度不得小于 OTA_TASK_ID_MAX_LEN；
 *   out_ver_len     - out_version 缓冲区长度；
 *   out_task_len    - out_task_id 缓冲区长度。
 *
 * 防降级：云端 target_version 必须严格大于 current_version 才算有新版本，
 * 相同或更低一律按 OTA_VER_MGR_RET_NO_NEW_VERSION 处理（记录 WARN 日志）；
 * 两版本号中任一方不是合法 4 段数值版本时按协议不符处理，回
 * OTA_VER_MGR_RET_FAIL。本层只拦"云端下发旧版本"，不替代 Secure Boot 的
 * 启动级防回滚。
 *
 * 返回值：
 *   OTA_VER_MGR_RET_SUCCESS        - 云端有新版本且版本号高于当前版本，
 *                                    out_version 与 out_task_id 已回填；
 *   OTA_VER_MGR_RET_NO_NEW_VERSION - 应答有效且已解析，云端无新版本，
 *                                    或 target_version 不高于 current_version，
 *                                    两个输出均为空串；
 *   OTA_VER_MGR_RET_FAIL           - 网络失败、非 200 应答、应答非法、
 *                                    版本号或 task_id 超长、版本号不是合法 4 段。
 *
 * 注意：内部使用 esp_http_client 同步事务，会阻塞调用任务，不可在中断或
 * 高频上下文调用。 */
ota_ver_mgr_ret_t ota_query_new_version(const char *url, const char *product_id, const char *chip_type, const char *current_version, char *out_version, int out_ver_len, char *out_task_id, int out_task_len);

/* ---------- 固件信息查询 ---------- */

/* 按 task_id 向云端查询固件下载信息（GET {url}?task_id=）。
 *
 * 入参：
 *   url         - 查询地址，由 OTA_CLOUD_BASE_IP/PORT/PACKAGE_API 合成；
 *   task_id     - 第一步 ota_query_new_version() 回报的升级任务 ID；
 *   out_url     - 固件下载地址，长度不得小于 OTA_VERSION_URL_MAX_LEN；
 *   out_url_len - out_url 缓冲区长度；
 *   out_size    - 固件字节数；
 *   out_md5     - 固件摘要，32 位小写十六进制串加 '\0'；
 *   out_md5_len - out_md5 缓冲区长度，不得小于 OTA_VERSION_MD5_HEX_LEN + 1。
 *
 * 返回值：
 *   OTA_VER_MGR_RET_SUCCESS - 应答有效，三项均已回填；
 *   OTA_VER_MGR_RET_FAIL    - 网络失败、非 200 应答、应答非法（含 code!=0，
 *                             如 404 Task not found / 401 Invalid device token）、
 *                             字段缺失或超长。
 *
 * 注意：内部使用 esp_http_client 同步事务，会阻塞调用任务。 */
ota_ver_mgr_ret_t ota_query_package_info(const char *url, const char *task_id, char *out_url, int out_url_len, uint32_t *out_size, char *out_md5, int out_md5_len);

/* 下载目标 -> 云端 chip_type 字符串；未知目标返回 NULL。 */
const char *ota_fw_target_chip_type(ota_fw_target_t target);

/* 下载目标 -> 本机镜像用的存储分区标签；ESP32 返回 NULL（由 app_update 选分区）。 */
const char *ota_fw_target_partition_label(ota_fw_target_t target);

/* 下载目标 -> 是否为本机固件（需要切换启动分区并重启）。 */
bool ota_fw_target_is_local(ota_fw_target_t target);

/* 下载目标 -> 是否走下挂芯片 IAP 通道（AT32 与 ST32 共用 OTAUart）。 */
bool ota_fw_target_has_iap(ota_fw_target_t target);

/* 下载目标 -> Modbus 站号；本机固件返回 0。 */
uint8_t ota_fw_target_slave_address(ota_fw_target_t target);

/* 下载目标 -> getVersion() 中的行号（AT32=1 即站号 0x02，ST32=2 即站号 0x03）；
 * 本机固件返回 -1。 */
int ota_fw_target_version_index(ota_fw_target_t target);

#ifdef __cplusplus
}
#endif

#endif /* OTA_VER_MANAGER_H */
