#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"

#include "cJSON.h"

#include "OTAInfo.h"
#include "OTAService.h"
#include "OTAVerManager.h"
#include "OTANetUpgrade.h"
#include "OTAUart.h"
#include "DataCenter.h"
#include "WiFiConnect.h"

/* 除 QUERY 状态的云端版本查询外，本文件不含存储或升级逻辑，其余动作均以
 * TODO 标注，由外部依赖补齐。 */

static const char *TAG = "OTAVerMgr";

/* 升级镜像提交后到重启之间的等待（单位 ms，2 秒），用于把日志送出串口。 */
#define OTA_VER_MGR_REBOOT_DELAY_MS         2000U

/* 状态机自持在文件级静态变量中（类型定义见头文件）：模块不 malloc，
 * 避免升级过程中堆碎片影响 esp_ota_write()。零初始化即对应 IDLE 状态。 */
static ota_ver_mgr_t s_ver_mgr;
static bool s_query_only;

/* ---------- 新版本查询（HTTP 同步事务） ---------- */

/* 云端应答约 300 字节，用固定缓冲接收：本模块不 malloc，避免升级期间的
 * 堆碎片影响 esp_ota_write()。 */
#define OTA_VER_MGR_HTTP_RESP_MAX_LEN   (512)
#define OTA_VER_MGR_HTTP_BUF_SIZE       (1024)

typedef struct {
    char buf[OTA_VER_MGR_HTTP_RESP_MAX_LEN];
    int  len;
    bool overflow;
} ota_ver_mgr_http_resp_t;

static esp_err_t ota_ver_mgr_http_event_handler(esp_http_client_event_t *evt)
{
    ota_ver_mgr_http_resp_t *resp = (ota_ver_mgr_http_resp_t *)evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (resp == NULL || evt->data == NULL || evt->data_len <= 0) {
            break;
        }
        /* 超长应答不截断解析，只标记溢出，由调用方按失败处理。 */
        if (resp->len + evt->data_len >= (int)sizeof(resp->buf)) {
            resp->overflow = true;
            break;
        }
        memcpy(resp->buf + resp->len, evt->data, evt->data_len);
        resp->len += evt->data_len;
        resp->buf[resp->len] = '\0';
        break;

    default:
        break;
    }

    return ESP_OK;
}

/* 解析 {"code":0,"msg":"...","data":{"has_update":bool,"task_id":"...","target_version":"..."}}。
 * 应答有效时按是否有新版本返回 SUCCESS / NO_NEW_VERSION；无新版本时两个输出均置空串。 */
static ota_ver_mgr_ret_t ota_ver_mgr_parse_query_response(const char *json, char *out_version, int out_ver_len, char *out_task_id, int out_task_len)
{
    out_version[0] = '\0';
    out_task_id[0] = '\0';

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "query response is not valid json");
        return OTA_VER_MGR_RET_FAIL;
    }

    ota_ver_mgr_ret_t ret = OTA_VER_MGR_RET_FAIL;
    cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");

    if (!cJSON_IsNumber(code) || code->valueint != 0) {
        ESP_LOGE(TAG, "query response code is not 0");
        goto out;
    }
    if (!cJSON_IsObject(data)) {
        ESP_LOGE(TAG, "query response has no data object");
        goto out;
    }

    cJSON *has_update = cJSON_GetObjectItemCaseSensitive(data, "has_update");
    if (!cJSON_IsBool(has_update)) {
        ESP_LOGE(TAG, "query response has no has_update flag");
        goto out;
    }

    /* 无更新时 data 里只有 has_update，不带 task_id / target_version。 */
    if (!cJSON_IsTrue(has_update)) {
        ret = OTA_VER_MGR_RET_NO_NEW_VERSION;
        goto out;
    }

    cJSON *target_ver = cJSON_GetObjectItemCaseSensitive(data, "target_version");
    if (!cJSON_IsString(target_ver) || target_ver->valuestring == NULL) {
        ESP_LOGE(TAG, "query response has no target_version");
        goto out;
    }

    cJSON *task = cJSON_GetObjectItemCaseSensitive(data, "task_id");
    if (!cJSON_IsString(task) || task->valuestring == NULL) {
        ESP_LOGE(TAG, "query response has no task_id");
        goto out;
    }

    /* 版本号与 task_id 都按定长字符串处理：超长说明协议不符，按失败上报，
     * 不做截断。两项都校验通过后再落地，避免只写一半。 */
    if (strlen(target_ver->valuestring) >= (size_t)out_ver_len) {
        ESP_LOGE(TAG, "target_version too long: %s", target_ver->valuestring);
        goto out;
    }
    if (strlen(task->valuestring) >= (size_t)out_task_len) {
        ESP_LOGE(TAG, "task_id too long: %s", task->valuestring);
        goto out;
    }

    strcpy(out_version, target_ver->valuestring);
    strcpy(out_task_id, task->valuestring);
    ret = OTA_VER_MGR_RET_SUCCESS;

out:
    cJSON_Delete(root);
    return ret;
}

/* ---------- 版本号解析与比较（防降级） ---------- */

/* 版本号只接受固定 4 段："主.次.修订.构建"，每段 1~3 位十进制且不超过 255
 * （AT32 的版本寄存器是 4 个 uint8）。段数不是 4、空段、非数字字符、段值
 * 越界一律判非法：不做截断也不补 0，避免用残缺版本号比出错误的"有更新"。 */
#define OTA_VER_MGR_VERSION_SEG_COUNT       4U
#define OTA_VER_MGR_VERSION_SEG_VALUE_MAX   255U

typedef enum {
    OTA_VER_MGR_VERSION_INVALID = 0,    /* 任一方不是合法的 4 段版本号 */
    OTA_VER_MGR_VERSION_LOWER,          /* target 低于 current */
    OTA_VER_MGR_VERSION_SAME,           /* 两版本相同 */
    OTA_VER_MGR_VERSION_HIGHER,         /* target 高于 current */
} ota_ver_mgr_version_cmp_t;

static bool ota_ver_mgr_parse_version(const char *text, unsigned out[OTA_VER_MGR_VERSION_SEG_COUNT])
{
    unsigned index = 0U;
    unsigned value = 0U;
    unsigned digits = 0U;

    if (text == NULL || text[0] == '\0') {
        return false;
    }

    for (const char *p = text; ; ++p) {
        char c = *p;

        if (c >= '0' && c <= '9') {
            value = value * 10U + (unsigned)(c - '0');
            ++digits;
            if (digits > 3U || value > OTA_VER_MGR_VERSION_SEG_VALUE_MAX) {
                return false;
            }
        } else if (c == '.' || c == '\0') {
            if (digits == 0U || index >= OTA_VER_MGR_VERSION_SEG_COUNT) {
                return false;
            }
            out[index++] = value;
            value = 0U;
            digits = 0U;
            if (c == '\0') {
                /* 只认 4 段：3 段或 5 段都按非法版本号处理。 */
                return (index == OTA_VER_MGR_VERSION_SEG_COUNT) ? true : false;
            }
        } else {
            return false;
        }
    }
}

/* 逐段按数值比较（不能用 strcmp："0.0.1.10" 字符串上小于 "0.0.1.9"）。 */
static ota_ver_mgr_version_cmp_t ota_ver_mgr_compare_version(const char *target, const char *current)
{
    unsigned lhs[OTA_VER_MGR_VERSION_SEG_COUNT];
    unsigned rhs[OTA_VER_MGR_VERSION_SEG_COUNT];

    if (!ota_ver_mgr_parse_version(target, lhs)) {
        ESP_LOGE(TAG, "version %s is not 4 numeric segments", (target != NULL) ? target : "(null)");
        return OTA_VER_MGR_VERSION_INVALID;
    }
    if (!ota_ver_mgr_parse_version(current, rhs)) {
        ESP_LOGE(TAG, "version %s is not 4 numeric segments", (current != NULL) ? current : "(null)");
        return OTA_VER_MGR_VERSION_INVALID;
    }

    for (unsigned i = 0U; i < OTA_VER_MGR_VERSION_SEG_COUNT; ++i) {
        if (lhs[i] > rhs[i]) {
            return OTA_VER_MGR_VERSION_HIGHER;
        }
        if (lhs[i] < rhs[i]) {
            return OTA_VER_MGR_VERSION_LOWER;
        }
    }
    return OTA_VER_MGR_VERSION_SAME;
}

/* 解析 {"code":0,"msg":"...","data":{"url":"...","version":"...","size":N,"md5":"...","expires_at":N}}。
 * 应答有效回 SUCCESS 并回填三项，任何字段缺失/类型不符/超长均回 FAIL。 */
static ota_ver_mgr_ret_t ota_ver_mgr_parse_package_response(const char *json, char *out_url, int out_url_len, uint32_t *out_size, char *out_md5, int out_md5_len)
{
    out_url[0] = '\0';
    out_md5[0] = '\0';
    * out_size = 0;

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "package response is not valid json");
        return OTA_VER_MGR_RET_FAIL;
    }

    ota_ver_mgr_ret_t ret = OTA_VER_MGR_RET_FAIL;
    cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");

    /* 失败应答形如 {"code":404,"data":null} / {"code":401,"data":null}。 */
    if (!cJSON_IsNumber(code) || code->valueint != 0) {
        ESP_LOGE(TAG, "package response code is not 0");
        goto out;
    }
    if (!cJSON_IsObject(data)) {
        ESP_LOGE(TAG, "package response has no data object");
        goto out;
    }

    cJSON *url = cJSON_GetObjectItemCaseSensitive(data, "url");
    if (!cJSON_IsString(url) || url->valuestring == NULL) {
        ESP_LOGE(TAG, "package response has no url");
        goto out;
    }

    cJSON *size = cJSON_GetObjectItemCaseSensitive(data, "size");
    if (!cJSON_IsNumber(size) || size->valuedouble <= 0) {
        ESP_LOGE(TAG, "package response has invalid size");
        goto out;
    }

    cJSON *md5 = cJSON_GetObjectItemCaseSensitive(data, "md5");
    if (!cJSON_IsString(md5) || md5->valuestring == NULL) {
        ESP_LOGE(TAG, "package response has no md5");
        goto out;
    }

    /* 三项全部校验通过后再落地，避免只写一部分。md5 长度必须是 32 位十六进制，
     * 不符合说明协议不符，按失败上报，不做截断。 */
    if (strlen(url->valuestring) >= (size_t)out_url_len) {
        ESP_LOGE(TAG, "package url too long");
        goto out;
    }
    if (strlen(md5->valuestring) != (size_t)OTA_VERSION_MD5_HEX_LEN) {
            ESP_LOGE(TAG, "package md5 length is not %d: %s", OTA_VERSION_MD5_HEX_LEN, md5->valuestring);
        goto out;
    }
    if (strlen(md5->valuestring) >= (size_t)out_md5_len) {
        ESP_LOGE(TAG, "package md5 too long");
        goto out;
    }

    strcpy(out_url, url->valuestring);
    strcpy(out_md5, md5->valuestring);
    *out_size = (uint32_t)size->valuedouble;
    ret = OTA_VER_MGR_RET_SUCCESS;

out:
    cJSON_Delete(root);
    return ret;
}

ota_ver_mgr_ret_t ota_query_package_info(const char *url, const char *task_id, char *out_url, int out_url_len, uint32_t *out_size, char *out_md5, int out_md5_len)
{
    if (url == NULL || task_id == NULL || task_id[0] == '\0' || out_url == NULL || out_url_len <= 0 || out_size == NULL || out_md5 == NULL || out_md5_len <= 0) {
        ESP_LOGE(TAG, "package: invalid argument");
        return OTA_VER_MGR_RET_FAIL;
    }

    out_url[0] = '\0';
    out_md5[0] = '\0';
    *out_size = 0;

    char query_url[OTA_VERSION_URL_MAX_LEN];
    int n = snprintf(query_url, sizeof(query_url), "%s?task_id=%s", url, task_id);
    if (n < 0 || n >= (int)sizeof(query_url)) {
        ESP_LOGE(TAG, "package: url too long");
        return OTA_VER_MGR_RET_FAIL;
    }

    ota_ver_mgr_http_resp_t resp = { 0 };

    esp_http_client_config_t config = { 0 };
    config.url = query_url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = OTA_CLOUD_HTTP_TIMEOUT_MS;
    config.buffer_size = OTA_VER_MGR_HTTP_BUF_SIZE;
    config.event_handler = ota_ver_mgr_http_event_handler;
    config.user_data = &resp;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "package: failed to init http client");
        return OTA_VER_MGR_RET_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", "Bearer " OTA_CLOUD_TOKEN);

    ota_ver_mgr_ret_t ret = OTA_VER_MGR_RET_FAIL;
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "package: http request failed: %s", esp_err_to_name(err));
        goto out;
    }

    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "package: http status %d", status);
        goto out;
    }

    if (resp.overflow || resp.len == 0) {
        ESP_LOGE(TAG, "package: response empty or too long");
        goto out;
    }

    ESP_LOGD(TAG, "package response: %s", resp.buf);

    ret = ota_ver_mgr_parse_package_response(resp.buf, out_url, out_url_len, out_size, out_md5, out_md5_len);

out:
    esp_http_client_cleanup(client);
    return ret;
}

ota_ver_mgr_ret_t ota_query_new_version(const char *url, const char *product_id, const char *chip_type, const char *current_version, char *out_version, int out_ver_len, char *out_task_id, int out_task_len)
{
    if (url == NULL || product_id == NULL || chip_type == NULL || current_version == NULL || out_version == NULL || out_ver_len <= 0 || out_task_id == NULL || out_task_len <= 0) {
        ESP_LOGE(TAG, "query: invalid argument");
        return OTA_VER_MGR_RET_FAIL;
    }

    out_version[0] = '\0';
    out_task_id[0] = '\0';

    char query_url[OTA_VERSION_URL_MAX_LEN];
        int n = snprintf(query_url, sizeof(query_url), "%s?product_id=%s&chip_type=%s&current_version=%s", url, product_id, chip_type, current_version);
        if (n < 0 || n >= (int)sizeof(query_url)) {
        ESP_LOGE(TAG, "query: url too long");
        return OTA_VER_MGR_RET_FAIL;
    }

    ota_ver_mgr_http_resp_t resp = { 0 };

    esp_http_client_config_t config = { 0 };
    config.url = query_url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = OTA_CLOUD_HTTP_TIMEOUT_MS;
    config.buffer_size = OTA_VER_MGR_HTTP_BUF_SIZE;
    config.event_handler = ota_ver_mgr_http_event_handler;
    config.user_data = &resp;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "query: failed to init http client");
        return OTA_VER_MGR_RET_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", "Bearer " OTA_CLOUD_TOKEN);

    ota_ver_mgr_ret_t ret = OTA_VER_MGR_RET_FAIL;
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "query: http request failed: %s", esp_err_to_name(err));
        goto out;
    }

    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "query: http status %d", status);
        goto out;
    }

    if (resp.overflow || resp.len == 0) {
        ESP_LOGE(TAG, "query: response empty or too long");
        goto out;
    }

    ESP_LOGD(TAG, "query response: %s", resp.buf);

    ret = ota_ver_mgr_parse_query_response(resp.buf, out_version, out_ver_len, out_task_id, out_task_len);

    /* 防降级：云端 target_version 必须严格高于当前版本才继续取包/下载。
     * 两处输出必须一起清空，否则调用方会带着旧版本号继续 GET_NEW_VERSION_INFO。
     * 降级不算失败：回 NO_NEW_VERSION 让状态机顺延下一个目标，避免一颗芯片的
     * 异常下发拖住整轮检查。版本号不是合法 4 段属于协议不符，回 FAIL。 */
    if (ret == OTA_VER_MGR_RET_SUCCESS) {
        ota_ver_mgr_version_cmp_t cmp = ota_ver_mgr_compare_version(out_version, current_version);

        if (cmp == OTA_VER_MGR_VERSION_INVALID) {
            ESP_LOGE(TAG, "query: target %s / current %s, version format invalid", out_version, current_version);
            out_version[0] = '\0';
            out_task_id[0] = '\0';
            ret = OTA_VER_MGR_RET_FAIL;
            goto out;
        }

        if (cmp != OTA_VER_MGR_VERSION_HIGHER) {
            ESP_LOGW(TAG, "query: target %s is %s current %s, ignored",
                     out_version, (cmp == OTA_VER_MGR_VERSION_SAME) ? "same as" : "older than", current_version);
            out_version[0] = '\0';
            out_task_id[0] = '\0';
            ret = OTA_VER_MGR_RET_NO_NEW_VERSION;
        }
    }

out:
    esp_http_client_cleanup(client);
    return ret;
}

static const char *ota_ver_mgr_target_name(ota_fw_target_t target)
{
    switch (target) {
    case OTA_FW_TARGET_AT32:  return "AT32";
    case OTA_FW_TARGET_ST32:  return "ST32";
    case OTA_FW_TARGET_ESP32: return "ESP32";
    default:                  return "UNKNOWN";
    }
}

static const char *ota_ver_mgr_state_name(ota_ver_mgr_state_t state)
{
    switch (state) {
    case OTA_VER_MGR_STATE_IDLE:                    return "IDLE";
    case OTA_VER_MGR_STATE_QUERY:                   return "QUERY";
    case OTA_VER_MGR_STATE_WAIT_QUERY_RESULT:       return "WAIT_QUERY_RESULT";
    case OTA_VER_MGR_STATE_GET_NEW_VERSION_INFO:    return "GET_NEW_VERSION_INFO";
    case OTA_VER_MGR_STATE_WAIT_VERSION_INFO_REPLY: return "WAIT_VERSION_INFO_REPLY";
    case OTA_VER_MGR_STATE_DOWNLOAD:                return "DOWNLOAD";
    case OTA_VER_MGR_STATE_AT32_IAP:                return "AT32_IAP";
    case OTA_VER_MGR_STATE_EXIT:                    return "EXIT";
    default:                                        return "UNKNOWN";
    }
}

static void ota_ver_mgr_enter_state(ota_ver_mgr_state_t state)
{
    if (s_ver_mgr.state == state) {
        return;
    }

    ESP_LOGI(TAG, "state %s -> %s", ota_ver_mgr_state_name(s_ver_mgr.state), ota_ver_mgr_state_name(state));

    s_ver_mgr.state = state;
    if (state == OTA_VER_MGR_STATE_DOWNLOAD)
        OTAServiceReportProgress(OTA_WEB_DOWNLOADING, 0, s_ver_mgr.fw_size);
}

void ota_version_manager_init(void)
{
    s_ver_mgr.state = OTA_VER_MGR_STATE_IDLE;
    s_ver_mgr.target = OTA_FW_TARGET_AT32;
    s_ver_mgr.target_cur_ver[0] = '\0';
    s_ver_mgr.new_version[0] = '\0';
    s_ver_mgr.task_id[0] = '\0';
    s_ver_mgr.fw_url[0] = '\0';
    s_ver_mgr.fw_size = 0;
    s_ver_mgr.fw_md5[0] = '\0';

    /* 下载通道随版本管理器一起复位，避免残留未关闭的写入会话。 */
    ota_upg_reset();

    /* IAP 会话同样清零：若上次下挂芯片升级进行到一半，不能带着旧会话
         * （旧 key、旧包号、旧站号）进入下一轮。 */
    ota_uart_reset();
}

/* ---------- 下载目标 ---------- */

const char *ota_fw_target_chip_type(ota_fw_target_t target)
{
    switch (target) {
    case OTA_FW_TARGET_AT32:  return OTA_CLOUD_CHIP_TYPE_AT32;
    case OTA_FW_TARGET_ST32:  return OTA_CLOUD_CHIP_TYPE_ST32;
    case OTA_FW_TARGET_ESP32: return OTA_CLOUD_CHIP_TYPE_ESP32;
    default:                  return NULL;
    }
}

const char *ota_fw_target_partition_label(ota_fw_target_t target)
{
    switch (target) {
    case OTA_FW_TARGET_AT32:  return OTA_FW_PART_LABEL_AT32;
    case OTA_FW_TARGET_ST32:  return OTA_FW_PART_LABEL_ST32;
    case OTA_FW_TARGET_ESP32: return NULL;      /* 由 app_update 选择 ota_0/ota_1 */
    default:                  return NULL;
    }
}

bool ota_fw_target_is_local(ota_fw_target_t target)
{
    return (target == OTA_FW_TARGET_ESP32) ? true : false;
}

bool ota_fw_target_has_iap(ota_fw_target_t target)
{
    return (target == OTA_FW_TARGET_AT32 || target == OTA_FW_TARGET_ST32) ? true : false;
}

uint8_t ota_fw_target_slave_address(ota_fw_target_t target)
{
    switch (target) {
    case OTA_FW_TARGET_AT32:  return OTA_AT32_DEVICE_ADDRESS;
    case OTA_FW_TARGET_ST32:  return OTA_ST32_DEVICE_ADDRESS;
    default:                  return 0U;        /* 本机固件不走 Modbus */
    }
}

int ota_fw_target_version_index(ota_fw_target_t target)
{
    switch (target) {
    case OTA_FW_TARGET_AT32:  return OTA_AT32_VERSION_SLAVE_INDEX;
    case OTA_FW_TARGET_ST32:  return OTA_ST32_VERSION_SLAVE_INDEX;
    default:                  return -1;
    }
}

/* 下挂芯片的当前版本：取 DataCenter 维护的 version[] 中对应站号那一行
 * （4 个原始字节，由该站输入寄存器 0x000B~0x000C 实时刷新），
 * 拼成 "主.次.修订.构建"，如 {0,0,0,1} -> "0.0.0.1"。
 * 该数组由 DataCenterTask 轮询更新，先整体拷到局部副本再格式化，避免读到
 * 一半新旧字节混合的值。 */
static void ota_ver_mgr_load_target_version(ota_fw_target_t target, const char *cur_ver)
{
    if (ota_fw_target_is_local(target)) {
        strncpy(s_ver_mgr.target_cur_ver, (cur_ver != NULL) ? cur_ver : "", sizeof(s_ver_mgr.target_cur_ver) - 1);
        s_ver_mgr.target_cur_ver[sizeof(s_ver_mgr.target_cur_ver) - 1] = '\0';
        return;
    }

    const int version_index = ota_fw_target_version_index(target);
    if (version_index < 0) {
        /* 其它目标没有可读版本来源，空串上报（云端自行判断）。 */
        s_ver_mgr.target_cur_ver[0] = '\0';
        return;
    }

    char (*versions)[4] = getVersion();
    unsigned parts[4] = {0};
    if (versions != NULL) {
        char snapshot[4];
        memcpy(snapshot, versions[version_index], sizeof(snapshot));
        for (size_t i = 0; i < 4U; ++i) {
            parts[i] = (unsigned)(uint8_t)snapshot[i];
        }
    }

    int n = snprintf(s_ver_mgr.target_cur_ver, sizeof(s_ver_mgr.target_cur_ver),
                     "%u.%u.%u.%u", parts[0], parts[1], parts[2], parts[3]);
    if (n < 0 || n >= (int)sizeof(s_ver_mgr.target_cur_ver)) {
        /* 四段各最多 3 位，正常不会溢出；防御性置空，避免上报半截版本号。 */
        s_ver_mgr.target_cur_ver[0] = '\0';
    }
}

/* 清掉上一目标的查询/下载结果，避免带到下一个目标。 */
static void ota_ver_mgr_clear_round_info(void)
{
    s_ver_mgr.new_version[0] = '\0';
    s_ver_mgr.task_id[0] = '\0';
    s_ver_mgr.fw_url[0] = '\0';
    s_ver_mgr.fw_size = 0;
    s_ver_mgr.fw_md5[0] = '\0';
}

/* 一轮检查的入口：从下挂芯片开始，最后才轮到本机固件。
 * 只由 ota_version_manager_request_start() 在 IDLE 受理外部触发时调用。 */
static void ota_ver_mgr_start_round(const char *cur_ver)
{
    s_ver_mgr.target = OTA_FW_TARGET_AT32;
    ota_ver_mgr_load_target_version(s_ver_mgr.target, cur_ver);
    ota_ver_mgr_clear_round_info();
    ota_upg_reset();
    ota_uart_reset();
    ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_QUERY);
}

bool ota_version_manager_is_idle(void)
{
    return (s_ver_mgr.state == OTA_VER_MGR_STATE_IDLE) ? true : false;
}

ota_ver_mgr_ret_t ota_version_manager_request_start(const char *cur_ver)
{
    return ota_version_manager_request_start_mode(cur_ver, false);
}

ota_ver_mgr_ret_t ota_version_manager_request_start_mode(const char *cur_ver, bool query_only)
{
    /* 只接受 IDLE -> QUERY 这一条外部触发路径：运行中的一轮不允许被打断，
     * 否则清空 fw_url/task_id 会让正在进行的下载会话失去目标信息。 */
    if (!ota_version_manager_is_idle()) {
        ESP_LOGW(TAG, "start request rejected, state %s is busy", ota_ver_mgr_state_name(s_ver_mgr.state));
        return OTA_VER_MGR_RET_FAIL;
    }

    s_query_only = query_only;
    ota_ver_mgr_start_round(cur_ver);
    return OTA_VER_MGR_RET_SUCCESS;
}

/* 切到下一个目标；没有下一个目标时返回 false（本轮结束）。 */
static bool ota_ver_mgr_next_target(const char *cur_ver)
{
    switch (s_ver_mgr.target) {
    case OTA_FW_TARGET_AT32:
        s_ver_mgr.target = OTA_FW_TARGET_ST32;
        break;
    case OTA_FW_TARGET_ST32:
        s_ver_mgr.target = OTA_FW_TARGET_ESP32;
        break;
    default:
        return false;
    }

    ota_ver_mgr_load_target_version(s_ver_mgr.target, cur_ver);
    ota_ver_mgr_clear_round_info();
    ota_upg_reset();
    ota_uart_reset();
    return true;
}

/* 一步一个状态。任何真实依赖调用都放在对应分支的 TODO 处，
 * 保持"调用即返回"，不得在此函数内等待。 */
ota_ver_mgr_ret_t ota_version_manager_process(const char *cur_ver)
{
    switch (s_ver_mgr.state) {

    case OTA_VER_MGR_STATE_IDLE:
        /* IDLE 不自主发起查询：本模块不轮询、不检测联网、不设定时器，
            * 只停在此处等待外部显式触发（入口为 ota_version_manager_request_start()）。 */
        /* TODO: 临时测试用，联网成功后自动触发一次版本检查，验证完删除。 */
#ifdef WIFI_CONNECT_TEST
        {
            static bool s_auto_triggered = false;
            if (!s_auto_triggered && WiFiConnectIsConnected()) {
                s_auto_triggered = true;
                OTAServiceTriggerCheck();
            }
        }
#endif
        break;

    case OTA_VER_MGR_STATE_QUERY: {
        /* url 由 OTA_CLOUD_BASE_IP/PORT/API 合成，每次进 QUERY 重新拼，
         * 便于后续改成 NVS 下发地址。 */
        char url[OTA_VERSION_URL_MAX_LEN];
        int n = snprintf(url, sizeof(url), "http://%s:%d%s", OTA_CLOUD_BASE_IP, OTA_CLOUD_BASE_PORT, OTA_CLOUD_BASE_API);
        if (n < 0 || n >= (int)sizeof(url)) {
            ESP_LOGE(TAG, "query: url too long");
            ota_ver_mgr_clear_round_info();
            ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
            return OTA_VER_MGR_RET_FAIL;
        }

        const char *chip_type = ota_fw_target_chip_type(s_ver_mgr.target);
        if (chip_type == NULL) {
            ESP_LOGE(TAG, "query: invalid target %d", (int)s_ver_mgr.target);
            ota_ver_mgr_clear_round_info();
            ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
            return OTA_VER_MGR_RET_FAIL;
        }

        ESP_LOGI(TAG, "query new version, chip %s current %s", chip_type, s_ver_mgr.target_cur_ver);

        ota_ver_mgr_ret_t qret = ota_query_new_version(url, OTA_CLOUD_PRODUCT_ID, chip_type, s_ver_mgr.target_cur_ver, s_ver_mgr.new_version, (int)sizeof(s_ver_mgr.new_version), s_ver_mgr.task_id, (int)sizeof(s_ver_mgr.task_id));

        if (qret == OTA_VER_MGR_RET_FAIL) {
            /* 查询失败只影响本轮：回 IDLE 后等待外部再次启动检查。
                * 下挂芯片查询失败按"整轮放弃"处理，不再继续查本机固件。 */
            ESP_LOGW(TAG, "query %s failed, back to idle", chip_type);
            ota_ver_mgr_clear_round_info();
            ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
            return OTA_VER_MGR_RET_FAIL;
        }

        if (qret == OTA_VER_MGR_RET_NO_NEW_VERSION) {
            /* 无新版本：解析已在本次调用内完成，无需再经 WAIT_QUERY_RESULT。
             * 下挂芯片直接顺延到本机固件，本机固件才回 IDLE。 */
            ESP_LOGI(TAG, "chip %s already latest version %s", chip_type, s_ver_mgr.target_cur_ver);
            OTAServiceReportVersion(s_ver_mgr.target, s_ver_mgr.target_cur_ver, false);
            ota_ver_mgr_clear_round_info();

            if (ota_ver_mgr_next_target(cur_ver)) {
                /* 停在本状态，下一拍按新目标重新发查询。 */
                break;
                }

            ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
            return OTA_VER_MGR_RET_NO_NEW_VERSION;
        }

        ESP_LOGI(TAG, "chip %s new version %s -> %s, task %s", chip_type, s_ver_mgr.target_cur_ver, s_ver_mgr.new_version, s_ver_mgr.task_id);

        OTAServiceReportVersion(s_ver_mgr.target, s_ver_mgr.new_version, true);
        if (s_query_only) {
            if (ota_ver_mgr_next_target(cur_ver)) break;
            ota_ver_mgr_clear_round_info();
            ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
            return OTA_VER_MGR_RET_NO_NEW_VERSION;
        }

        ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_GET_NEW_VERSION_INFO);
        break;
    }

    case OTA_VER_MGR_STATE_WAIT_QUERY_RESULT:
        // TODO :预留，云端回复解析，15秒超时逻辑
        ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_GET_NEW_VERSION_INFO);
        break;

    case OTA_VER_MGR_STATE_GET_NEW_VERSION_INFO: {
        /* task_id 是上一步回报的，缺失说明状态机被异常驱动，按失败回 IDLE。 */
        if (s_ver_mgr.task_id[0] == '\0') {
            ESP_LOGE(TAG, "package: task_id is empty");
            ota_ver_mgr_clear_round_info();
            ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
            return OTA_VER_MGR_RET_FAIL;
        }

        /* url 与 query 同源合成，便于后续改成 NVS 下发地址。 */
        char url[OTA_VERSION_URL_MAX_LEN];
        int n = snprintf(url, sizeof(url), "http://%s:%d%s", OTA_CLOUD_BASE_IP, OTA_CLOUD_BASE_PORT, OTA_CLOUD_PACKAGE_API);
        if (n < 0 || n >= (int)sizeof(url)) {
            ESP_LOGE(TAG, "package: url too long");
            ota_ver_mgr_clear_round_info();
            ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
            return OTA_VER_MGR_RET_FAIL;
        }

        ESP_LOGI(TAG, "query package info, task %s", s_ver_mgr.task_id);

        ota_ver_mgr_ret_t pret = ota_query_package_info(url, s_ver_mgr.task_id, s_ver_mgr.fw_url, (int)sizeof(s_ver_mgr.fw_url), &s_ver_mgr.fw_size, s_ver_mgr.fw_md5, (int)sizeof(s_ver_mgr.fw_md5));

        if (pret != OTA_VER_MGR_RET_SUCCESS) {
                    /* 失败只影响本轮：回 IDLE 后等待外部再次启动检查。
             * 下挂芯片取包失败同样放弃整轮，避免只升级本机造成版本错配。 */
            ESP_LOGW(TAG, "package failed, back to idle");
            ota_ver_mgr_clear_round_info();
            ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
            return OTA_VER_MGR_RET_FAIL;
        }

        ESP_LOGI(TAG, "package %s, size %u, md5 %s", s_ver_mgr.new_version, (unsigned)s_ver_mgr.fw_size, s_ver_mgr.fw_md5);

        ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_DOWNLOAD);
        break;
    }

    case OTA_VER_MGR_STATE_WAIT_VERSION_INFO_REPLY:
        // TODO:预留，云端回复解析，15秒超时逻辑
        ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_DOWNLOAD);
        break;

    case OTA_VER_MGR_STATE_DOWNLOAD: {
        /* 下载通道自持状态，每拍推进一步：建链 -> 请求 -> 接收 -> 收尾。
         * 只要还没出结果就停在本状态，由下一拍继续搬运。 */
        int uret = ota_upg_process(&s_ver_mgr);

        if (uret == OTA_UPG_RET_DONE_PENDING) {
            OTAServiceReportProgress(OTA_WEB_DOWNLOADED, s_ver_mgr.fw_size, s_ver_mgr.fw_size);

            /* 下挂芯片（AT32 / ST32）：镜像只落盘还不够，必须经 MBUS485 推给芯片
                * 并在芯片侧校验版本成功后才写 OTA_Info。因此先不写记录，转 IAP
                * 状态，由它成功后再补写；失败则整轮放弃（不升级后续目标）。 */
            if (ota_fw_target_has_iap(s_ver_mgr.target)) {
                ESP_LOGI(TAG, "chip image stored in %s, pushing it over MBUS485 (slave 0x%02X)",
                            ota_fw_target_partition_label(s_ver_mgr.target), ota_fw_target_slave_address(s_ver_mgr.target));

                if (!ota_uart_start(s_ver_mgr.new_version, s_ver_mgr.fw_size, s_ver_mgr.target)) {
                    ESP_LOGE(TAG, "chip iap start failed, abort this round");
                    ota_uart_reset();
                    ota_ver_mgr_clear_round_info();
                    ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
                    return OTA_VER_MGR_RET_FAIL;
                }

                ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_AT32_IAP);
                break;
            }

            /* 其它目标（当前无）：只落盘并记录，写入由后续流程读取。 */
            ESP_LOGI(TAG, "chip image stored in %s, record version %s", ota_fw_target_partition_label(s_ver_mgr.target), s_ver_mgr.new_version);

            bool saved = ota_info_write(s_ver_mgr.target, s_ver_mgr.new_version, s_ver_mgr.fw_md5, s_ver_mgr.fw_size);

            ota_ver_mgr_clear_round_info();

            if (!saved) {
                ESP_LOGE(TAG, "chip image record failed, abort this round");
                ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
                return OTA_VER_MGR_RET_FAIL;
            }

            if (ota_ver_mgr_next_target(cur_ver)) {
                ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_QUERY);
                break;
            }

            ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
            return OTA_VER_MGR_RET_SUCCESS;
        }

        if (uret == OTA_UPG_RET_DONE) {
            OTAServiceReportProgress(OTA_WEB_DOWNLOADED, s_ver_mgr.fw_size, s_ver_mgr.fw_size);
            /* 镜像已写入升级分区并设为启动分区：清掉本轮固件信息后立即重启，
             * 新镜像在下次启动生效（下载通道已在 CLOSE 阶段释放全部资源）。 */
            ESP_LOGI(TAG, "ota image applied, restarting to take effect");
            ota_ver_mgr_clear_round_info();
            ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);

            /* 留出串口日志送出的时间，与 components/OTA 的 manual_ota_download
             * 成功后的处理一致；esp_restart() 不返回。 */
            vTaskDelay(pdMS_TO_TICKS(OTA_VER_MGR_REBOOT_DELAY_MS));
            esp_restart();

            return OTA_VER_MGR_RET_SUCCESS;
        }

        if (uret == OTA_UPG_RET_FAIL) {
            ESP_LOGW(TAG, "ota upgrade failed, back to idle");
            ota_ver_mgr_clear_round_info();
            ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
            return OTA_VER_MGR_RET_FAIL;
        }

        break;
    }

    case OTA_VER_MGR_STATE_AT32_IAP: {
        /* 下挂芯片（AT32 / ST32）的镜像已在上一状态落盘到各自分区，这里逐拍
            * 把它经 MBUS485 按目标站号推给对应 bootloader。一次调用最多一次串口
            * 事务，不阻塞。 */
        ota_uart_ret_t iret = ota_uart_process();

        if (iret == OTA_UART_RET_RUNNING) {
            /* 包号换算成字节数上报，复用 Web 端"下载中"的进度条：
                * 下挂芯片升级期间 fw_size 不变，进度语义仍是"已传输 / 总量"。 */
            uint16_t sent = 0U;
            uint16_t total = 0U;
            ota_uart_get_progress(&sent, &total);
            if (total > 0U) {
                uint32_t done = (uint32_t)sent * OTA_UART_PAYLOAD_SIZE;
                if (done > s_ver_mgr.fw_size) {
                    done = s_ver_mgr.fw_size;
                }
                OTAServiceReportProgress(OTA_WEB_DOWNLOADING, (unsigned)done, (unsigned)s_ver_mgr.fw_size);
            }
            break;
        }

        if (iret == OTA_UART_RET_DONE) {
            /* 升级成功的判据是目标芯片实读版本已等于目标版本（比对在 OTAUart
                * 内），此时才写 OTA_Info：记录与芯片版本一致，下一轮查询不会重下。 */
            ESP_LOGI(TAG, "%s upgraded to %s, writing the record",
                        ota_ver_mgr_target_name(s_ver_mgr.target), s_ver_mgr.new_version);

            bool saved = ota_info_write(s_ver_mgr.target, s_ver_mgr.new_version, s_ver_mgr.fw_md5, s_ver_mgr.fw_size);

            ota_uart_reset();
            ota_ver_mgr_clear_round_info();

            if (!saved) {
                ESP_LOGE(TAG, "%s image record failed, abort this round", ota_ver_mgr_target_name(s_ver_mgr.target));
                ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
                return OTA_VER_MGR_RET_FAIL;
            }

            /* 下挂芯片升级成功，才允许顺延到下一个目标。 */
            if (ota_ver_mgr_next_target(cur_ver)) {
                ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_QUERY);
                break;
            }

            ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
            return OTA_VER_MGR_RET_SUCCESS;
        }

        if (iret == OTA_UART_RET_FAIL) {
            /* 需求：下挂芯片升级失败即退出 OTA 流程，不再升级下一个目标，
                * 本机固件保持原样，等下一次外部触发重新整轮检查。 */
            ESP_LOGE(TAG, "%s iap failed, abort this round (later targets are kept as is)",
                        ota_ver_mgr_target_name(s_ver_mgr.target));
            ota_uart_reset();
            ota_ver_mgr_clear_round_info();
            ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
            return OTA_VER_MGR_RET_FAIL;
        }

        /* OTA_UART_RET_IDLE：会话被意外清掉，按失败收尾。 */
        ESP_LOGE(TAG, "%s iap session lost, abort this round", ota_ver_mgr_target_name(s_ver_mgr.target));
        ota_uart_reset();
        ota_ver_mgr_clear_round_info();
        ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
        return OTA_VER_MGR_RET_FAIL;
    }

    case OTA_VER_MGR_STATE_EXIT:
        break;

    default:
        ota_ver_mgr_enter_state(OTA_VER_MGR_STATE_IDLE);
        return OTA_VER_MGR_RET_FAIL;
    }

    return OTA_VER_MGR_RET_SUCCESS;
}
