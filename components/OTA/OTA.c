#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "OTA.h"

static const char *TAG = "OTA_CORE";

/* LAN-only OTA test server. Change this address if the PC WLAN address changes. */
#define OTA_CHECK_URL "http://192.168.1.3:8070/ota/check-update"
#define OTA_DOWNLOAD_URL_MAX_LEN 256
#define OTA_VERSION_MAX_LEN 32
#define OTA_HTTP_BUFFER_SIZE 4096
#define OTA_MIN_FREE_HEAP 30000

static char s_product_model[16] = "F1";
static int s_mp_version_code = 1;
static int s_device_serial_num = 1;
static char s_device_id[32] = {0};
static bool s_ota_check_started;

typedef struct {
    char *data;
    size_t length;
    bool failed;
} ota_http_response_t;

static esp_err_t manual_ota_download(const char *download_url);
static bool run_self_test(void);
static void generate_device_id(void);
static int compare_versions(const char *v1, const char *v2);
static const char *current_firmware_version(void);

void set_product_model(const char *model)
{
    if (model && strlen(model) < sizeof(s_product_model)) {
        strcpy(s_product_model, model);
        generate_device_id();
        ESP_LOGI(TAG, "Product model set to: %s", s_product_model);
    }
}

void set_version_code(int code)
{
    s_mp_version_code = code;
    generate_device_id();
    ESP_LOGI(TAG, "Version code set to: %d", s_mp_version_code);
}

void set_serial_number(int sn)
{
    if (sn >= 1 && sn <= 999999) {
        s_device_serial_num = sn;
        generate_device_id();
        ESP_LOGI(TAG, "Serial number set to: %d", s_device_serial_num);
    }
}

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
    ESP_LOGI(TAG, "Device info set to: %s", s_device_id);
}

const char *get_device_id(void)
{
    if (s_device_id[0] == '\0') {
        generate_device_id();
    }
    return s_device_id;
}

const char *get_product_model(void)
{
    return s_product_model;
}

int get_version_code(void)
{
    return s_mp_version_code;
}

int get_serial_number(void)
{
    return s_device_serial_num;
}

static void generate_device_id(void)
{
    snprintf(s_device_id, sizeof(s_device_id), "%s_%d%06d",
             s_product_model, s_mp_version_code, s_device_serial_num);
}

static const char *current_firmware_version(void)
{
    const esp_app_desc_t *description = esp_app_get_description();
    if (description && description->version[0] != '\0') {
        return description->version;
    }
    return "unknown";
}

static int compare_versions(const char *v1, const char *v2)
{
    int v1_major = 0;
    int v1_minor = 0;
    int v1_patch = 0;
    int v2_major = 0;
    int v2_minor = 0;
    int v2_patch = 0;

    if (!v1 || !v2 || sscanf(v1, "%d.%d.%d", &v1_major, &v1_minor, &v1_patch) < 1 ||
        sscanf(v2, "%d.%d.%d", &v2_major, &v2_minor, &v2_patch) < 1) {
        return v1 && v2 ? strcmp(v1, v2) : 0;
    }

    if (v1_major != v2_major) {
        return v1_major - v2_major;
    }
    if (v1_minor != v2_minor) {
        return v1_minor - v2_minor;
    }
    return v1_patch - v2_patch;
}

static const char *ota_state_name(esp_ota_img_states_t state)
{
    switch (state) {
        case ESP_OTA_IMG_NEW: return "NEW";
        case ESP_OTA_IMG_PENDING_VERIFY: return "PENDING_VERIFY";
        case ESP_OTA_IMG_VALID: return "VALID";
        case ESP_OTA_IMG_INVALID: return "INVALID";
        case ESP_OTA_IMG_ABORTED: return "ABORTED";
        case ESP_OTA_IMG_UNDEFINED: return "UNDEFINED";
        default: return "UNKNOWN";
    }
}

static bool run_self_test(void)
{
    ESP_LOGI(TAG, "[OTA TEST] Running first-boot self-test");

    nvs_handle_t test_handle;
    esp_err_t err = nvs_open("ota_test", NVS_READWRITE, &test_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[OTA TEST] NVS failed: %s", esp_err_to_name(err));
        return false;
    }
    nvs_close(test_handle);

    size_t free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "[OTA TEST] NVS OK, free heap=%u bytes", (unsigned)free_heap);
    if (free_heap < OTA_MIN_FREE_HEAP) {
        ESP_LOGE(TAG, "[OTA TEST] Free heap is below %u bytes", (unsigned)OTA_MIN_FREE_HEAP);
        return false;
    }

    return true;
}

void check_ota_state(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        ESP_LOGE(TAG, "[OTA BOOT] Cannot get running partition");
        return;
    }

    ESP_LOGI(TAG, "[OTA BOOT] version=%s partition=%s address=0x%08" PRIx32,
             current_firmware_version(), running->label, running->address);

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "[OTA BOOT] No OTA state yet (initial wired flash)");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "[OTA BOOT] Cannot read OTA state: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "[OTA BOOT] image state=%s", ota_state_name(state));
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }

    if (!run_self_test()) {
        ESP_LOGE(TAG, "[OTA RESULT] SELF-TEST FAILED; rolling back now");
        err = esp_ota_mark_app_invalid_rollback_and_reboot();
        ESP_LOGE(TAG, "[OTA RESULT] Rollback failed: %s", esp_err_to_name(err));
        return;
    }

    err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "[OTA RESULT] SUCCESS: version=%s confirmed valid",
                 current_firmware_version());
    } else {
        ESP_LOGE(TAG, "[OTA RESULT] Failed to confirm image: %s", esp_err_to_name(err));
    }
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    ota_http_response_t *response = (ota_http_response_t *)event->user_data;
    if (!response || event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }

    char *new_data = realloc(response->data, response->length + event->data_len + 1);
    if (!new_data) {
        response->failed = true;
        ESP_LOGE(TAG, "[OTA CHECK] Cannot allocate response buffer");
        return ESP_ERR_NO_MEM;
    }

    response->data = new_data;
    memcpy(response->data + response->length, event->data, event->data_len);
    response->length += event->data_len;
    response->data[response->length] = '\0';
    return ESP_OK;
}

static esp_err_t manual_ota_download(const char *download_url)
{
    esp_err_t err = ESP_FAIL;
    esp_http_client_handle_t client = NULL;
    esp_ota_handle_t ota_handle = 0;
    bool ota_started = false;
    char *buffer = NULL;

    ESP_LOGI(TAG, "[OTA DOWNLOAD] URL=%s", download_url);

    esp_http_client_config_t config = {
        .url = download_url,
        .timeout_ms = 30000,
        .keep_alive_enable = false,
        .buffer_size = OTA_HTTP_BUFFER_SIZE,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
    };

    client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "[OTA DOWNLOAD] HTTP client initialization failed");
        return ESP_ERR_NO_MEM;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[OTA DOWNLOAD] Connection failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        ESP_LOGE(TAG, "[OTA DOWNLOAD] Server returned HTTP %d", status_code);
        err = ESP_FAIL;
        goto cleanup;
    }

    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (!ota_partition) {
        ESP_LOGE(TAG, "[OTA DOWNLOAD] No inactive OTA partition");
        err = ESP_ERR_NOT_FOUND;
        goto cleanup;
    }

    if (content_length <= 0 || content_length > ota_partition->size) {
        ESP_LOGE(TAG, "[OTA DOWNLOAD] Invalid size=%" PRId64 ", slot=%" PRIu32,
                 content_length, ota_partition->size);
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    ESP_LOGI(TAG, "[OTA DOWNLOAD] target=%s image=%" PRId64 " bytes slot=%" PRIu32 " bytes",
             ota_partition->label, content_length, ota_partition->size);

    err = esp_ota_begin(ota_partition, (size_t)content_length, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[OTA DOWNLOAD] esp_ota_begin failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    ota_started = true;

    buffer = malloc(OTA_HTTP_BUFFER_SIZE);
    if (!buffer) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    int64_t total_read = 0;
    int last_progress = -10;
    while (total_read < content_length) {
        int remaining = (int)(content_length - total_read);
        int read_size = remaining < OTA_HTTP_BUFFER_SIZE ? remaining : OTA_HTTP_BUFFER_SIZE;
        int read_length = esp_http_client_read(client, buffer, read_size);
        if (read_length <= 0) {
            ESP_LOGE(TAG, "[OTA DOWNLOAD] Stream ended at %" PRId64 "/%" PRId64,
                     total_read, content_length);
            err = ESP_FAIL;
            goto cleanup;
        }

        err = esp_ota_write(ota_handle, buffer, read_length);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "[OTA DOWNLOAD] Flash write failed: %s", esp_err_to_name(err));
            goto cleanup;
        }

        total_read += read_length;
        int progress = (int)((total_read * 100) / content_length);
        if (progress >= last_progress + 10 || progress == 100) {
            ESP_LOGI(TAG, "[OTA DOWNLOAD] progress=%d%% (%" PRId64 "/%" PRId64 ")",
                     progress, total_read, content_length);
            last_progress = progress;
        }
    }

    err = esp_ota_end(ota_handle);
    ota_started = false;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[OTA VERIFY] Image validation failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    ESP_LOGI(TAG, "[OTA VERIFY] Image validation passed");

    err = esp_ota_set_boot_partition(ota_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[OTA SWITCH] Cannot select %s: %s",
                 ota_partition->label, esp_err_to_name(err));
        goto cleanup;
    }

    ESP_LOGI(TAG, "[OTA SWITCH] Next boot partition=%s", ota_partition->label);
    ESP_LOGI(TAG, "[OTA RESULT] DOWNLOAD SUCCESS; rebooting in 2 seconds");

cleanup:
    if (ota_started) {
        esp_ota_abort(ota_handle);
    }
    free(buffer);
    if (client) {
        esp_http_client_cleanup(client);
    }
    return err;
}

static void ota_check_task(void *parameter)
{
    (void)parameter;
    ota_http_response_t response = {0};
    char download_url[OTA_DOWNLOAD_URL_MAX_LEN] = {0};
    char latest_version[OTA_VERSION_MAX_LEN] = {0};
    const char *current_version = current_firmware_version();

    ESP_LOGI(TAG, "[OTA CHECK] server=%s", OTA_CHECK_URL);
    ESP_LOGI(TAG, "[OTA CHECK] device=%s current=%s", get_device_id(), current_version);

    cJSON *request_json = cJSON_CreateObject();
    if (!request_json) {
        ESP_LOGE(TAG, "[OTA CHECK] Cannot allocate request JSON");
        goto done;
    }
    cJSON_AddStringToObject(request_json, "device_id", get_device_id());
    cJSON_AddStringToObject(request_json, "model", s_product_model);
    cJSON_AddStringToObject(request_json, "firmware_version", current_version);
    char *post_data = cJSON_PrintUnformatted(request_json);
    cJSON_Delete(request_json);
    if (!post_data) {
        ESP_LOGE(TAG, "[OTA CHECK] Cannot serialize request JSON");
        goto done;
    }

    esp_http_client_config_t config = {
        .url = OTA_CHECK_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &response,
        .timeout_ms = 10000,
        .buffer_size = 2048,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "[OTA CHECK] HTTP client initialization failed");
        free(post_data);
        goto done;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(post_data);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[OTA CHECK] Request failed: %s", esp_err_to_name(err));
        goto done;
    }
    if (status_code != 200 || response.failed || !response.data) {
        ESP_LOGE(TAG, "[OTA CHECK] Invalid response: HTTP %d", status_code);
        goto done;
    }

    ESP_LOGI(TAG, "[OTA CHECK] response=%s", response.data);
    cJSON *root = cJSON_Parse(response.data);
    if (!root) {
        ESP_LOGE(TAG, "[OTA CHECK] Response is not valid JSON");
        goto done;
    }

    cJSON *has_update = cJSON_GetObjectItemCaseSensitive(root, "has_update");
    cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "download_url");
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "latest_version");
    bool update_available = cJSON_IsTrue(has_update) && cJSON_IsString(url) &&
                            cJSON_IsString(version);
    if (update_available) {
        snprintf(download_url, sizeof(download_url), "%s", url->valuestring);
        snprintf(latest_version, sizeof(latest_version), "%s", version->valuestring);
    }
    cJSON_Delete(root);

    if (!update_available || compare_versions(current_version, latest_version) >= 0) {
        ESP_LOGI(TAG, "[OTA RESULT] NO UPDATE: current=%s", current_version);
        goto done;
    }

    ESP_LOGI(TAG, "[OTA CHECK] UPDATE FOUND: %s -> %s", current_version, latest_version);
    err = manual_ota_download(download_url);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }
    ESP_LOGE(TAG, "[OTA RESULT] DOWNLOAD FAILED: %s", esp_err_to_name(err));

done:
    free(response.data);
    ESP_LOGI(TAG, "[OTA CHECK] task finished");
    vTaskDelete(NULL);
}

void start_ota_check(void)
{
    if (s_ota_check_started) {
        ESP_LOGI(TAG, "[OTA CHECK] Already started once during this boot");
        return;
    }

    s_ota_check_started = true;
    BaseType_t result = xTaskCreate(ota_check_task, "ota_check", 8192, NULL, 5, NULL);
    if (result != pdPASS) {
        s_ota_check_started = false;
        ESP_LOGE(TAG, "[OTA CHECK] Cannot create OTA task");
        return;
    }
    ESP_LOGI(TAG, "[OTA CHECK] Task created after Wi-Fi obtained an IP");
}
