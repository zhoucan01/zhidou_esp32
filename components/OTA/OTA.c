#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "OTA.h"

static const char *TAG = "OTA_CORE";

// ================= ⚙️ 用户配置区域 =================
// 产品定义 - 这些可以动态修改
static char s_product_model[16] = "F1";        // 产品型号前缀
static int s_mp_version_code = 1;               // 量产版本代号 (0=内测, 1=量产第一版...)
static int s_device_serial_num = 1;             // 产品编号 (后六位，例如 1 -> 000001)

// 当前固件版本 - 编译新版本时修改这个宏！
#define CURRENT_FW_VERSION  "1.0.0"             // 当前运行的版本号
// ================================================

// 服务器配置
#define OTA_SERVER_HOST     "119.23.65.78"      // 您的服务器 IP
#define OTA_QUERY_PATH      "/ota/check-update" // OTA 查询接口
#define OTA_SERVER_PORT     443                 // HTTPS 端口

// 服务器证书
static const char server_cert_pem[] = 
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDjTCCAnWgAwIBAgIULejN4GY0udYHCSa3mr8/tnu18rcwDQYJKoZIhvcNAQEL\n"
    "BQAwVjELMAkGA1UEBhMCQ04xEDAOBgNVBAgMB0JlaWppbmcxEDAOBgNVBAcMB0Jl\n"
    "aWppbmcxDzANBgNVBAoMBlpoaWRvdTESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2\n"
    "MDMwNDAzMjM1MloXDTI3MDMwNDAzMjM1MlowVjELMAkGA1UEBhMCQ04xEDAOBgNV\n"
    "BAgMB0JlaWppbmcxEDAOBgNVBAcMB0JlaWppbmcxDzANBgNVBAoMBlpoaWRvdTES\n"
    "MBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKC\n"
    "AQEA1ecwKfB8GFzDvnICMPKXnsYEqFudQkYFQQHgF8pdfuT2wdxkjxMa3ZajQaIJ\n"
    "b/XHn9Y6FHRYMUmF1ufpI3yDdAWwR7h6bMrZXHSXsA9zP/9Yy/Rt1sCG22NDywH6\n"
    "U0zK2D3KSgIXuSZNCMYkq8tNxXMVFGzFTaA4VpanvT+nTTQ17OOqCtf6QNuyuigY\n"
    "a1pjKpW0NJRtaXz6SZl4T10RbEpjsm2q4cN/O+fbzGV8KB1DYL4Zwvp5D3OMcSg5\n"
    "fw+MfhGM38l0rXNlBXDGd9fAZ6mheZBt1wrMa7EMOBTy7dViF46QgWVpwLfVGzRU\n"
    "9cushobdobq+NcROhS6mUZ1VWwIDAQABo1MwUTAdBgNVHQ4EFgQUgybBlciSxKUw\n"
    "RSQDfjKlgXNmojIwHwYDVR0jBBgwFoAUgybBlciSxKUwRSQDfjKlgXNmojIwDwYD\n"
    "VR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAmmMHTgis2rPPReoViPHh\n"
    "fOEJ/cmjxCemHq8ToiTj0Ow8Q/i2ufFLa1Xs8nm+jGLpzak7/6n+VCMtdzt+eO8Y\n"
    "46UbCmwaXzLFOAp5/SQmQX/MZLCxuoVRb8gmSoZ3Tpm4tmONvpPmr2H30nECP4Nu\n"
    "68sdyEODt9OeM9nXGnwFmWl3OiBs5g2fP8PwJn8TyEYs8CXfaUg8xVaq7Y1tXs2t\n"
    "hJrSjI8d3cyQkADKakGbVD7FNrLs67o61eywcUI0toxlZTM1KXpOlxkNGVQA3ud0\n"
    "LzhqQlz5PYqJ+x0jl8awgIszLYxv2FdyuFAN9m0xMDfp00TOWN4RDi5Pe4s3+XaE\n"
    "cg==\n"
    "-----END CERTIFICATE-----\n";

// 生成的 Device ID 缓冲区
static char s_device_id[32] = {0};

// 函数声明
static esp_err_t manual_ota_download(const char *download_url);
static bool run_self_test(void);
static void generate_device_id(void);
static int compare_versions(const char *v1, const char *v2);
// ================= 设备信息设置接口 =================

/**
 * @brief 设置产品型号
 * @param model 产品型号字符串，如 "F1"
 */
void set_product_model(const char *model)
{
    if (model && strlen(model) < sizeof(s_product_model)) {
        strcpy(s_product_model, model);
        ESP_LOGI(TAG, "Product model set to: %s", s_product_model);
        // 重新生成设备ID
        generate_device_id();
    }
}

/**
 * @brief 设置量产版本代号
 * @param code 版本代号 (0=内测, 1=量产第一版...)
 */
void set_version_code(int code)
{
    s_mp_version_code = code;
    ESP_LOGI(TAG, "Version code set to: %d", s_mp_version_code);
    generate_device_id();
}

/**
 * @brief 设置产品编号
 * @param sn 产品编号 (1-999999)
 */
void set_serial_number(int sn)
{
    if (sn >= 1 && sn <= 999999) {
        s_device_serial_num = sn;
        ESP_LOGI(TAG, "Serial number set to: %d", s_device_serial_num);
        generate_device_id();
    }
}

/**
 * @brief 一次性设置所有设备信息
 * @param model 产品型号
 * @param code 版本代号
 * @param sn 产品编号
 */
void set_device_info(const char *model, int code, int sn)
{
    if (model && strlen(model) < sizeof(s_product_model)) {
        strcpy(s_product_model, model);
    }
    if (code >= 0) {
        s_mp_version_code = code;
    }
    if (sn >= 1 && sn <= 999999) {
        s_device_serial_num = sn;
    }
    generate_device_id();
    ESP_LOGI(TAG, "Device info set to: %s_%d%06d", 
             s_product_model, s_mp_version_code, s_device_serial_num);
}

/**
 * @brief 获取当前设备ID
 */
const char* get_device_id(void)
{
    if (s_device_id[0] == '\0') {
        generate_device_id();
    }
    return s_device_id;
}

/**
 * @brief 获取当前产品型号
 */
const char* get_product_model(void)
{
    return s_product_model;
}

/**
 * @brief 获取当前版本代号
 */
int get_version_code(void)
{
    return s_mp_version_code;
}

/**
 * @brief 获取当前产品编号
 */
int get_serial_number(void)
{
    return s_device_serial_num;
}

// ==================================================

/**
 * @brief 生成符合规范的 device_id
 */
static void generate_device_id(void) {
    snprintf(s_device_id, sizeof(s_device_id), "%s_%d%06d", 
             s_product_model, s_mp_version_code, s_device_serial_num);
    ESP_LOGI(TAG, "Device ID: %s", s_device_id);
}

/**
 * @brief 比较版本号
 */
static int compare_versions(const char *v1, const char *v2)
{
    int v1_major, v1_minor, v1_patch;
    int v2_major, v2_minor, v2_patch;
    
    sscanf(v1, "%d.%d.%d", &v1_major, &v1_minor, &v1_patch);
    sscanf(v2, "%d.%d.%d", &v2_major, &v2_minor, &v2_patch);
    
    if (v1_major != v2_major) return v1_major - v2_major;
    if (v1_minor != v2_minor) return v1_minor - v2_minor;
    return v1_patch - v2_patch;
}

// 自检任务函数
// 注意：不能在这里 vTaskDelete(NULL)。自删除只是把 TCB 挂到待终止链表，
// 随后由 Idle 任务释放 TCB 和栈，创建者手里的句柄立刻变成悬空指针，
// 再次 vTaskDelete(handle) 会在非法 TCB 上操作链表导致 LoadProhibited。
// 因此这里改为挂起自己，由创建者负责删除。
static void test_task_func(void *pvParameters)
{
    ESP_LOGD(TAG, "Test task running, waiting to be deleted by creator");
    vTaskSuspend(NULL);
}

/**
 * @brief 自检函数
 */
static bool run_self_test(void)
{
    ESP_LOGI(TAG, "🔍 Running CORE self-test for new firmware...");

    nvs_handle_t test_handle;
    esp_err_t err = nvs_open("test", NVS_READWRITE, &test_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ NVS test failed");
        return false;
    }
    nvs_close(test_handle);
    ESP_LOGI(TAG, "✅ NVS OK");

    TaskHandle_t temp_handle = NULL;
    BaseType_t task_created = xTaskCreate(
        test_task_func, "test_task", 2048, NULL, 1, &temp_handle
    );
    
    if (task_created != pdPASS) {
        // FreeRTOS 任务栈和 TCB 只能来自内部 RAM，PSRAM 余量再多也帮不上忙
        ESP_LOGE(TAG, "❌ Task creation test failed (need %u bytes stack, internal free=%u, "
                        "largest contiguous=%u, min free ever=%u, internal dma free=%u)",
                    (unsigned)2048,
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                    (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA));
        ESP_LOGE(TAG, "   total free heap=%u bytes (includes PSRAM), psram free=%u bytes",
                    (unsigned)esp_get_free_heap_size(),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return false;
    }
    
    // 只验证调度器；任务不会自删除，句柄仍然有效
    vTaskDelay(pdMS_TO_TICKS(50));
    if (temp_handle != NULL) {
        vTaskDelete(temp_handle);
        temp_handle = NULL;
    }
    ESP_LOGI(TAG, "✅ Task scheduler OK");

    size_t free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Free heap: %u bytes", free_heap);
    
    if (free_heap < 30000) {
        ESP_LOGE(TAG, "❌ Insufficient heap: %u bytes", free_heap);
        return false;
    }

    ESP_LOGI(TAG, "✅ Core self-test passed! Marking firmware as valid.");
    esp_ota_mark_app_valid_cancel_rollback();
    return true;
}

/**
 * @brief 检查OTA状态
 */
void check_ota_state(void)
{
    ESP_LOGI(TAG, "=== Checking OTA state ===");
    
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        ESP_LOGE(TAG, "Failed to get running partition");
        return;
    }
    
    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);
    
    if (err == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "⚠️ New firmware detected, running self-test...");
        if (!run_self_test()) {
            ESP_LOGE(TAG, "❌ Self-test FAILED! Rolling back...");
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
    }
}

/**
 * @brief HTTP 事件处理回调
 */
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer = NULL;
    static int output_len = 0;

    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!output_buffer) {
                output_buffer = malloc(evt->data_len + 1);
                output_len = 0;
            } else {
                char *new_buf = realloc(output_buffer, output_len + evt->data_len + 1);
                if (!new_buf) {
                    ESP_LOGE(TAG, "Memory realloc failed");
                    free(output_buffer);
                    output_buffer = NULL;
                    return ESP_FAIL;
                }
                output_buffer = new_buf;
            }
            memcpy(output_buffer + output_len, evt->data, evt->data_len);
            output_len += evt->data_len;
            output_buffer[output_len] = '\0';
            break;

        case HTTP_EVENT_ON_FINISH:
            if (output_buffer) {
                ESP_LOGD(TAG, "Response: %s", output_buffer);
                
                cJSON *root = cJSON_Parse(output_buffer);
                if (root) {
                    cJSON *status = cJSON_GetObjectItem(root, "status");
                    cJSON *has_update = cJSON_GetObjectItem(root, "has_update");
                    
                    if (status && cJSON_IsString(status) && 
                        strcmp(status->valuestring, "success") == 0) {
                        
                        if (has_update && cJSON_IsTrue(has_update)) {
                            cJSON *url_obj = cJSON_GetObjectItem(root, "download_url");
                            cJSON *latest_ver = cJSON_GetObjectItem(root, "latest_version");
                            
                            if (url_obj && cJSON_IsString(url_obj) && 
                                latest_ver && cJSON_IsString(latest_ver)) {
                                
                                const char *download_url = url_obj->valuestring;
                                const char *latest_version = latest_ver->valuestring;
                                
                                if (compare_versions(CURRENT_FW_VERSION, latest_version) >= 0) {
                                    ESP_LOGI(TAG, "Already at latest version: %s", CURRENT_FW_VERSION);
                                } else {
                                    ESP_LOGI(TAG, "✅ New version: %s -> %s", 
                                             CURRENT_FW_VERSION, latest_version);
                                    ESP_LOGI(TAG, "Download URL: %s", download_url);

                                    esp_err_t ret = manual_ota_download(download_url);
                                    
                                    if (ret == ESP_OK) {
                                        ESP_LOGI(TAG, "✅ OTA SUCCESSFUL! Rebooting...");
                                        vTaskDelay(pdMS_TO_TICKS(2000));
                                        esp_restart();
                                    } else {
                                        ESP_LOGE(TAG, "❌ OTA FAILED!");
                                    }
                                }
                            }
                        } else {
                            ESP_LOGI(TAG, "ℹ️ No update available (current: %s)", CURRENT_FW_VERSION);
                        }
                    }
                    cJSON_Delete(root);
                }
                free(output_buffer);
                output_buffer = NULL;
            }
            break;

        case HTTP_EVENT_DISCONNECTED:
            if (output_buffer) {
                free(output_buffer);
                output_buffer = NULL;
            }
            break;
            
        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief 手动 OTA 下载函数
 */
static esp_err_t manual_ota_download(const char *download_url)
{
    ESP_LOGI(TAG, "Starting OTA download from: %s", download_url);

    esp_http_client_config_t http_cfg = {
        .url = download_url,
        .cert_pem = server_cert_pem,
        .skip_cert_common_name_check = true,
        .timeout_ms = 30000,
        .keep_alive_enable = false,
        .buffer_size = 4096,
    };

    esp_http_client_handle_t http_client = esp_http_client_init(&http_cfg);
    if (!http_client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(http_client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(http_client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(http_client);
    ESP_LOGI(TAG, "Content length: %d bytes", content_length);

    if (content_length <= 0 || content_length > 10 * 1024 * 1024) {
        ESP_LOGE(TAG, "Invalid content length: %d", content_length);
        esp_http_client_cleanup(http_client);
        return ESP_FAIL;
    }

    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (!ota_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        esp_http_client_cleanup(http_client);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA partition: %s, size: %lu KB", 
             ota_partition->label, (unsigned long)(ota_partition->size / 1024));

    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(http_client);
        return err;
    }

    char *buffer = malloc(4096);
    if (!buffer) {
        ESP_LOGE(TAG, "Buffer allocation failed");
        esp_ota_abort(ota_handle);
        esp_http_client_cleanup(http_client);
        return ESP_ERR_NO_MEM;
    }

    int total_read = 0;
    int last_progress = 0;

    while (total_read < content_length) {
        int read_len = esp_http_client_read(http_client, buffer, 4096);
        if (read_len < 0) {
            ESP_LOGE(TAG, "Read error at %d/%d", total_read, content_length);
            free(buffer);
            esp_ota_abort(ota_handle);
            esp_http_client_cleanup(http_client);
            return ESP_FAIL;
        }

        if (read_len == 0) {
            ESP_LOGE(TAG, "Connection closed at %d/%d", total_read, content_length);
            free(buffer);
            esp_ota_abort(ota_handle);
            esp_http_client_cleanup(http_client);
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buffer, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Write failed: %s", esp_err_to_name(err));
            free(buffer);
            esp_ota_abort(ota_handle);
            esp_http_client_cleanup(http_client);
            return err;
        }

        total_read += read_len;
        int progress = (total_read * 100) / content_length;
        if (progress >= last_progress + 10) {
            ESP_LOGI(TAG, "Progress: %d%% (%d/%d)", progress, total_read, content_length);
            last_progress = progress;
        }
    }

    free(buffer);
    esp_http_client_cleanup(http_client);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ota_set_boot_partition(ota_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "OTA download and write completed!");
    return ESP_OK;
}

/**
 * @brief OTA 检查任务
 */
static void ota_check_task(void *pvParameters)
{
    ESP_LOGI(TAG, "OTA check task started");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Current firmware version: %s", CURRENT_FW_VERSION);
    ESP_LOGI(TAG, "Device info: %s_%d%06d", 
             s_product_model, s_mp_version_code, s_device_serial_num);

    // 生成 Device ID（如果还没生成）
    if (s_device_id[0] == '\0') {
        generate_device_id();
    }

    // 构建请求 JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", s_device_id);
    cJSON_AddStringToObject(root, "firmware_version", CURRENT_FW_VERSION);
    
    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Checking updates...");
    ESP_LOGI(TAG, "Request: %s", post_data);

    esp_http_client_config_t config = {
        .host = OTA_SERVER_HOST,
        .port = OTA_SERVER_PORT,
        .path = OTA_QUERY_PATH,
        .method = HTTP_METHOD_POST,
        .event_handler = _http_event_handler,
        .cert_pem = server_cert_pem,
        .skip_cert_common_name_check = true,
        .timeout_ms = 30000,
        .buffer_size = 2048,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        free(post_data);
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(post_data);
    
    ESP_LOGI(TAG, "OTA check finished");
    vTaskDelete(NULL);
}

/**
 * @brief 启动 OTA 检查
 */
void start_ota_check(void)
{
    xTaskCreate(ota_check_task, "ota_check", 8192, NULL, 5, NULL);
    ESP_LOGI(TAG, "OTA check task created");
}