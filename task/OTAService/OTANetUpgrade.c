#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_rom_md5.h"

#include "OTANetUpgrade.h"
#include "OTAService.h"

/* HTTP 通道下载：建链/请求/接收/收尾四段，对应 components/OTA/OTA.c 的
 * manual_ota_download()。区别是接收段按字节预算分片，一次调用只搬一部分，
 * 避免整包阻塞 OTAServiceTask。 */

static const char *TAG = "OTANetUpg";

typedef struct {
    ota_upg_state_t          state;                                 /* 下载子状态 */
    esp_http_client_handle_t client;                                /* HTTP 句柄，NULL 表示未建链 */
    ota_fw_target_t          target;                                /* 本次下载的芯片目标 */
    const esp_partition_t   *partition;                             /* 目标分区：本机为 ota_x，下挂芯片为数据分区 */
    esp_ota_handle_t         ota_handle;                            /* 写入会话句柄，仅本机固件使用 */
    bool                     ota_begun;                             /* 写入会话是否仍未 end/abort */
    bool                     md5_ready;                             /* md5 上下文是否已初始化 */
    bool                     commit_ok;                             /* FINISH 是否已提交启动分区 */
    bool                     store_done;                            /* 下挂芯片镜像是否已完整落盘 */
    int                      content_length;                        /* 本次镜像长度（字节） */
    uint32_t                 rx_total;                              /* 已接收字节数 */
    uint32_t                 last_log_pct;                          /* 上次打印的进度百分比 */
    uint32_t                 expected_size;                         /* 云端声明的镜像长度 */
    char                     expected_md5[OTA_VERSION_MD5_HEX_LEN + 1]; /* 云端声明的摘要 */
    char                     url[OTA_VERSION_URL_MAX_LEN];          /* 固件下载地址 */
    md5_context_t            md5;                                   /* 流式摘要上下文 */
} ota_upg_t;

static ota_upg_t s_upg;

/* 接收缓冲固定为静态数组：升级全程不走堆，避免碎片影响 esp_ota_write()；
 * 静态数据位于内部 RAM，关 flash cache 时可直接访问。 */
static uint8_t s_upg_rx_buf[OTA_UPG_HTTP_BUF_SIZE];

const char *ota_upg_state_name(ota_upg_state_t state)
{
    switch (state) {
    case OTA_UPG_STATE_IDLE:     return "IDLE";
    case OTA_UPG_STATE_CONNECT:  return "CONNECT";
    case OTA_UPG_STATE_REQUEST:  return "REQUEST";
    case OTA_UPG_STATE_RESPONSE: return "RESPONSE";
    case OTA_UPG_STATE_FINISH:   return "FINISH";
    case OTA_UPG_STATE_CLOSE:    return "CLOSE";
    case OTA_UPG_STATE_EXIT:     return "EXIT";
    default:                     return "UNKNOWN";
    }
}

ota_upg_state_t ota_upg_get_state(void)
{
    return s_upg.state;
}

static void ota_upg_enter_state(ota_upg_state_t state)
{
    if (s_upg.state == state) {
        return;
    }

    ESP_LOGI(TAG, "state %s -> %s", ota_upg_state_name(s_upg.state), ota_upg_state_name(state));
    s_upg.state = state;
}

static void ota_upg_md5_to_hex(const uint8_t digest[ESP_ROM_MD5_DIGEST_LEN], char out[OTA_VERSION_MD5_HEX_LEN + 1])
{
    static const char hex_map[] = "0123456789abcdef";

    for (int i = 0; i < ESP_ROM_MD5_DIGEST_LEN; i++) {
        out[i * 2] = hex_map[(digest[i] >> 4) & 0x0Fu];
        out[i * 2 + 1] = hex_map[digest[i] & 0x0Fu];
    }
    out[OTA_VERSION_MD5_HEX_LEN] = '\0';
}

/* ---------- 分步处理接口 ---------- */

int ota_upg_handle_connect(const ota_ver_mgr_t *ver)
{
    (void)ver;

    /* 幂等：状态机异常重入时不重复建链。 */
    if (s_upg.client != NULL) {
        return OTA_UPG_RET_SUCCESS;
    }

    if (s_upg.url[0] == '\0') {
        ESP_LOGE(TAG, "connect: url is empty");
        return OTA_UPG_RET_FAIL;
    }

    esp_http_client_config_t config = { 0 };
    config.url = s_upg.url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = OTA_UPG_HTTP_TIMEOUT_MS;
    config.buffer_size = OTA_UPG_HTTP_BUF_SIZE;
    config.keep_alive_enable = false;

    s_upg.client = esp_http_client_init(&config);
    if (s_upg.client == NULL) {
        ESP_LOGE(TAG, "connect: failed to init http client");
        return OTA_UPG_RET_FAIL;
    }

    /* 下载接口与查询/详情接口共用同一设备 token；请求头必须在 open() 前设置，
     * open() 才会把 GET 连同请求头一起发出。 */
    esp_http_client_set_header(s_upg.client, "Authorization", "Bearer " OTA_CLOUD_TOKEN);

    /* open 完成 TCP 建链并发出 GET 请求，应答头留到 REQUEST 阶段取。 */
    esp_err_t err = esp_http_client_open(s_upg.client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "connect: open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(s_upg.client);
        s_upg.client = NULL;
        return OTA_UPG_RET_FAIL;
    }

    ESP_LOGI(TAG, "connect ok: %s", s_upg.url);
    return OTA_UPG_RET_SUCCESS;
}

int ota_upg_handle_request(const ota_ver_mgr_t *ver)
{
    (void)ver;

    if (s_upg.client == NULL) {
        ESP_LOGE(TAG, "request: client is not connected");
        return OTA_UPG_RET_FAIL;
    }

    /* 应答头：状态码 + Content-Length。 */
    int length = esp_http_client_fetch_headers(s_upg.client);
    int status = esp_http_client_get_status_code(s_upg.client);

    if (status != 200) {
        ESP_LOGE(TAG, "request: http status %d", status);
        return OTA_UPG_RET_FAIL;
    }

    /* chunked 应答不带 Content-Length（返回 -1），退回云端声明的镜像长度。 */
    if (length <= 0) {
        length = (int)s_upg.expected_size;
    }
    if (length <= 0 || (uint32_t)length > OTA_UPG_MAX_IMAGE_SIZE) {
        ESP_LOGE(TAG, "request: invalid content length %d", length);
        return OTA_UPG_RET_FAIL;
    }
    if (s_upg.expected_size != 0 && (uint32_t)length != s_upg.expected_size) {
        ESP_LOGE(TAG, "request: length mismatch, cloud %u, http %d", (unsigned)s_upg.expected_size, length);
        return OTA_UPG_RET_FAIL;
    }
    s_upg.content_length = length;

    const esp_partition_t *part = NULL;

    if (s_upg.target == OTA_FW_TARGET_ESP32) {
        /* 本机固件：沿用 app_update 的分区选择（ota_0/ota_1 轮换）。 */
        part = esp_ota_get_next_update_partition(NULL);
        if (part == NULL) {
            ESP_LOGE(TAG, "request: no ota partition");
            return OTA_UPG_RET_FAIL;
        }
    } else {
        /* 下挂芯片固件：直接写固定数据分区，不参与启动切换。 */
        const char *label = ota_fw_target_partition_label(s_upg.target);
        if (label == NULL) {
            ESP_LOGE(TAG, "request: target %d has no partition", (int)s_upg.target);
            return OTA_UPG_RET_FAIL;
        }

        part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
        if (part == NULL) {
            ESP_LOGE(TAG, "request: partition %s not found", label);
            return OTA_UPG_RET_FAIL;
        }
    }

    if ((uint32_t)length > part->size) {
        ESP_LOGE(TAG, "request: image %d exceeds partition %s (%u)", length, part->label, (unsigned)part->size);
        return OTA_UPG_RET_FAIL;
    }

    if (s_upg.target == OTA_FW_TARGET_ESP32) {
        /* 以镜像长度作为大小提示，便于 esp_ota_begin 规划擦除。 */
        esp_ota_handle_t handle = 0;
        esp_err_t err = esp_ota_begin(part, (size_t)length, &handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "request: esp_ota_begin failed: %s", esp_err_to_name(err));
            return OTA_UPG_RET_FAIL;
        }

        s_upg.ota_handle = handle;
        s_upg.ota_begun = true;
    } else {
        /* 数据分区没有写入会话，整片擦除一次，后续按偏移顺序写。 */
        esp_err_t err = esp_partition_erase_range(part, 0, part->size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "request: erase %s failed: %s", part->label, esp_err_to_name(err));
            return OTA_UPG_RET_FAIL;
        }
    }

    esp_rom_md5_init(&s_upg.md5);

    s_upg.partition = part;
    s_upg.md5_ready = true;
    s_upg.rx_total = 0;
    s_upg.last_log_pct = 0;

    ESP_LOGI(TAG, "request.ok: %d bytes -> partition %s (target %d)", length, part->label, (int)s_upg.target);
    return OTA_UPG_RET_SUCCESS;
}

int ota_upg_handle_response(const ota_ver_mgr_t *ver)
{
    (void)ver;

    /* 本机固件要求写入会话已建立；下挂芯片只要求分区已确定。 */
    bool ready = (s_upg.target == OTA_FW_TARGET_ESP32) ? s_upg.ota_begun : (s_upg.partition != NULL);

    if (s_upg.client == NULL || !ready || s_upg.content_length <= 0) {
        ESP_LOGE(TAG, "response: not ready");
        return OTA_UPG_RET_FAIL;
    }

    const uint32_t total = (uint32_t)s_upg.content_length;
    uint32_t budget = OTA_UPG_RX_BUDGET_PER_CALL;

    /* 一次调用最多搬 OTA_UPG_RX_BUDGET_PER_CALL 字节，剩下的下一拍继续。 */
    while (budget > 0 && s_upg.rx_total < total) {
        uint32_t left = total - s_upg.rx_total;
        int want = (left > sizeof(s_upg_rx_buf)) ? (int)sizeof(s_upg_rx_buf) : (int)left;

        int read_len = esp_http_client_read(s_upg.client, (char *)s_upg_rx_buf, want);
        if (read_len < 0) {
            ESP_LOGE(TAG, "response: read error at %u/%u", (unsigned)s_upg.rx_total, (unsigned)total);
            return OTA_UPG_RET_FAIL;
        }
        if (read_len == 0) {
            /* 未读满就返回 0：对端提前关闭，不接收残缺镜像。 */
            ESP_LOGE(TAG, "response: connection closed at %u/%u", (unsigned)s_upg.rx_total, (unsigned)total);
            return OTA_UPG_RET_FAIL;
        }

        esp_rom_md5_update(&s_upg.md5, s_upg_rx_buf, (uint32_t)read_len);

        /* 边收边写：镜像不在 RAM 整包缓存，收到一段就落盘一段。
         * 本机固件走写入会话，下挂芯片直接按偏移写数据分区。 */
        esp_err_t err;

        if (s_upg.target == OTA_FW_TARGET_ESP32) {
            err = esp_ota_write(s_upg.ota_handle, s_upg_rx_buf, (size_t)read_len);
        } else {
            err = esp_partition_write(s_upg.partition, s_upg.rx_total, s_upg_rx_buf, (size_t)read_len);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "response: write failed at %u: %s", (unsigned)s_upg.rx_total, esp_err_to_name(err));
            return OTA_UPG_RET_FAIL;
        }

        s_upg.rx_total += (uint32_t)read_len;
        OTAServiceReportProgress(OTA_WEB_DOWNLOADING, s_upg.rx_total, total);
        budget = ((uint32_t)read_len >= budget) ? 0 : (budget - (uint32_t)read_len);

        uint32_t pct = (s_upg.rx_total * 100U) / total;
        if (pct >= s_upg.last_log_pct + 10U) {
            s_upg.last_log_pct = pct;
            ESP_LOGI(TAG, "progress %u%% (%u/%u)", (unsigned)pct, (unsigned)s_upg.rx_total, (unsigned)total);
        }
    }

    if (s_upg.rx_total < total) {
        return OTA_UPG_RET_BUSY;
    }

    /* 收尾校验：云端下发的是 md5，逐段累计后比对，不符不提交。 */
    uint8_t digest[ESP_ROM_MD5_DIGEST_LEN];
    char hex[OTA_VERSION_MD5_HEX_LEN + 1];

    esp_rom_md5_final(digest, &s_upg.md5);
    s_upg.md5_ready = false;
    ota_upg_md5_to_hex(digest, hex);

    if (strcasecmp(hex, s_upg.expected_md5) != 0) {
        ESP_LOGE(TAG, "response: md5 mismatch, expect %s, got %s", s_upg.expected_md5, hex);
        return OTA_UPG_RET_FAIL;
    }

    ESP_LOGI(TAG, "response ok: %u bytes, md5 %s verified", (unsigned)s_upg.rx_total, hex);
    return OTA_UPG_RET_SUCCESS;
}

int ota_upg_handle_close(const ota_ver_mgr_t *ver)
{
    (void)ver;

    /* 仍打开的写入会话说明镜像未提交，必须 abort，否则下一次
     * esp_ota_begin() 会因会话未关闭而失败。下挂芯片没有写入会话，
     * 未收全的镜像由下一次下载的整片擦除覆盖。 */
    if (s_upg.ota_begun) {
        esp_err_t err = esp_ota_abort(s_upg.ota_handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "close: esp_ota_abort failed: %s", esp_err_to_name(err));
        }
        s_upg.ota_begun = false;
        ESP_LOGW(TAG, "close: incomplete image aborted");
    }

    if (s_upg.client != NULL) {
        esp_http_client_cleanup(s_upg.client);
        s_upg.client = NULL;
    }

    s_upg.partition = NULL;
    s_upg.ota_handle = 0;
    s_upg.md5_ready = false;
    s_upg.commit_ok = false;
    s_upg.store_done = false;
    s_upg.target = OTA_FW_TARGET_ESP32;
    s_upg.content_length = 0;
    s_upg.rx_total = 0;
    s_upg.last_log_pct = 0;
    s_upg.expected_size = 0;
    s_upg.expected_md5[0] = '\0';
    s_upg.url[0] = '\0';

    return OTA_UPG_RET_SUCCESS;
}

void ota_upg_reset(void)
{
    ota_upg_handle_close(NULL);
    s_upg.state = OTA_UPG_STATE_IDLE;
}

/* ---------- 状态机推进 ---------- */

int ota_upg_process(const ota_ver_mgr_t *ver)
{
    if (ver == NULL) {
        ESP_LOGE(TAG, "process: ver is NULL");
        return OTA_UPG_RET_FAIL;
    }

    switch (s_upg.state) {

    case OTA_UPG_STATE_IDLE: {
        /* 入口校验：URL、长度、摘要缺一不可，缺项说明状态机被异常驱动。 */
        size_t url_len = strlen(ver->fw_url);
        size_t md5_len = strlen(ver->fw_md5);

        if (url_len == 0 || url_len >= sizeof(s_upg.url) || ver->fw_size == 0 || ver->fw_size > OTA_UPG_MAX_IMAGE_SIZE || md5_len != (size_t)OTA_VERSION_MD5_HEX_LEN) {
            ESP_LOGE(TAG, "process: invalid firmware info, url_len %u, size %u, md5_len %u", (unsigned)url_len, (unsigned)ver->fw_size, (unsigned)md5_len);
            return OTA_UPG_RET_FAIL;
        }

        /* 本次下载用到的输入在此一次性拷入模块内，之后不再读 ver。 */
        strcpy(s_upg.url, ver->fw_url);
        strcpy(s_upg.expected_md5, ver->fw_md5);
        s_upg.expected_size = ver->fw_size;
        s_upg.target = ver->target;
        s_upg.commit_ok = false;
        s_upg.store_done = false;

        ESP_LOGI(TAG, "start download %s, %u bytes, md5 %s, target %d", s_upg.url, (unsigned)s_upg.expected_size, s_upg.expected_md5, (int)s_upg.target);
        ota_upg_enter_state(OTA_UPG_STATE_CONNECT);
        break;
    }

    case OTA_UPG_STATE_CONNECT:
        if (ota_upg_handle_connect(ver) != OTA_UPG_RET_SUCCESS) {
            ESP_LOGE(TAG, "connect failed");
            ota_upg_enter_state(OTA_UPG_STATE_CLOSE);
            break;
        }
        ota_upg_enter_state(OTA_UPG_STATE_REQUEST);
        break;

    case OTA_UPG_STATE_REQUEST:
        if (ota_upg_handle_request(ver) != OTA_UPG_RET_SUCCESS) {
            ESP_LOGE(TAG, "request failed");
            ota_upg_enter_state(OTA_UPG_STATE_CLOSE);
            break;
        }
        ota_upg_enter_state(OTA_UPG_STATE_RESPONSE);
        break;

    case OTA_UPG_STATE_RESPONSE: {
        int rret = ota_upg_handle_response(ver);

        if (rret == OTA_UPG_RET_FAIL) {
            ESP_LOGE(TAG, "receive failed at %u/%d", (unsigned)s_upg.rx_total, s_upg.content_length);
            ota_upg_enter_state(OTA_UPG_STATE_CLOSE);
            break;
        }
        if (rret == OTA_UPG_RET_BUSY) {
            break;      /* 本拍预算用完，下一拍继续 */
        }
        ota_upg_enter_state(OTA_UPG_STATE_FINISH);
        break;
    }

    case OTA_UPG_STATE_FINISH: {
        bool ok = false;

        if (s_upg.target == OTA_FW_TARGET_ESP32) {
            /* esp_ota_end() 校验镜像结构，调用后句柄即失效、不能再 abort，
             * 所以先清 ota_begun 再调用。 */
            if (s_upg.ota_begun) {
                s_upg.ota_begun = false;
                esp_err_t err = esp_ota_end(s_upg.ota_handle);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "finish: esp_ota_end failed: %s", esp_err_to_name(err));
                } else {
                    ok = true;
                }
            }

            if (ok && s_upg.partition != NULL) {
                esp_err_t err = esp_ota_set_boot_partition(s_upg.partition);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "finish: esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
                    ok = false;
                }
            } else {
                ok = false;
            }
        } else {
            /* 下挂芯片固件：数据分区没有镜像结构可校验，长度与 md5 已在
             * RESPONSE 阶段校验通过，这里只确认落盘长度与 Content-Length 一致。 */
            ok = (s_upg.partition != NULL && s_upg.rx_total == (uint32_t)s_upg.content_length);
            if (!ok) {
                ESP_LOGE(TAG, "finish: incomplete image %u/%u", (unsigned)s_upg.rx_total, (unsigned)s_upg.content_length);
            }
        }

        if (s_upg.target == OTA_FW_TARGET_ESP32) {
            s_upg.commit_ok = ok;
            if (ok) {
                ESP_LOGI(TAG, "image applied to %s, reboot to take effect", s_upg.partition->label);
            } else {
                ESP_LOGE(TAG, "image not committed");
            }
        } else {
            s_upg.store_done = ok;
            if (ok) {
                ESP_LOGI(TAG, "image stored to %s, %u bytes, no reboot needed", s_upg.partition->label, (unsigned)s_upg.rx_total);
            } else {
                ESP_LOGE(TAG, "image not stored");
            }
        }

        ota_upg_enter_state(OTA_UPG_STATE_CLOSE);
        break;
    }

    case OTA_UPG_STATE_CLOSE: {
        /* handle_close 会清 commit_ok / store_done，结果先取出来。 */
        bool committed = s_upg.commit_ok;
        bool stored = s_upg.store_done;
        bool local = (s_upg.target == OTA_FW_TARGET_ESP32);

        ota_upg_handle_close(ver);
        ota_upg_enter_state(OTA_UPG_STATE_IDLE);

        if (!local) {
            return stored ? OTA_UPG_RET_DONE_PENDING : OTA_UPG_RET_FAIL;
        }
        return committed ? OTA_UPG_RET_DONE : OTA_UPG_RET_FAIL;
    }

    case OTA_UPG_STATE_EXIT:
        ESP_LOGW(TAG, "exit requested, release resources");
        ota_upg_reset();
        return OTA_UPG_RET_FAIL;

    default:
        ESP_LOGE(TAG, "unknown state %d, reset", (int)s_upg.state);
        ota_upg_reset();
        return OTA_UPG_RET_FAIL;
    }

    return OTA_UPG_RET_SUCCESS;
}
