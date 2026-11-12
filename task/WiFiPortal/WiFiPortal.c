#include "WiFiPortal.h"
#include "portal_protocol.h"
#include "WiFiConnect.h"
#include "OTAService.h"
#include "Music.h"
#include "Bluetooch.h"
#include <stdlib.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "cJSON.h"

extern const char *DeviceName;
extern const unsigned char page_start[] asm("_binary_portal_html_start");
extern const unsigned char page_end[] asm("_binary_portal_html_end");
static portMUX_TYPE portal_lock = portMUX_INITIALIZER_UNLOCKED;
static bool requested, ota_pending;
static bool ota_query_only;
static int64_t ota_deadline;
static httpd_handle_t server;
static char ap_name[32];

void WiFiPortalRequestStart(void)
{
    if (esp_timer_get_time() > 30000000LL) return;
    portENTER_CRITICAL(&portal_lock); requested = true; portEXIT_CRITICAL(&portal_lock);
}
static esp_err_t json_reply(httpd_req_t *req, cJSON *root)
{
    if (!root) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t ret = httpd_resp_sendstr(req, body);
    free(body); return ret;
}
static esp_err_t error_reply(httpd_req_t *req, const char *code, const char *message)
{
    httpd_resp_set_status(req, code);
    cJSON *root = cJSON_CreateObject();
    if (root) cJSON_AddStringToObject(root, "error", message);
    return json_reply(req, root);
}
static cJSON *read_json(httpd_req_t *req)
{
    char content_type[64], origin[80];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strncmp(content_type, "application/json", 16) ||
        (content_type[16] && content_type[16] != ';')) return NULL;
    if (httpd_req_get_hdr_value_len(req, "Origin")) {
        if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK ||
            (strcmp(origin, WIFI_PORTAL_ORIGIN) && strcmp(origin, WIFI_PORTAL_ORIGIN ":80"))) return NULL;
    }
    if (req->content_len == 0 || req->content_len > 512) return NULL;
    char body[513]; size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) return NULL;
        received += n;
    }
    body[received] = 0;
    if (memchr(body, 0, received) || strstr(body, "\\u0000")) return NULL;
    const char *end;
    cJSON *root = cJSON_ParseWithOpts(body, &end, true);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return NULL; }
    return root;
}
static esp_err_t redirect_handler(httpd_req_t *req);
static esp_err_t index_handler(httpd_req_t *req)
{
    char host[64];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
        (strcmp(host, WIFI_PORTAL_IP) && strcmp(host, WIFI_PORTAL_IP ":80"))) return redirect_handler(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'");
    return httpd_resp_send(req, (const char *)page_start, page_end - page_start - 1);
}
static esp_err_t redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", WIFI_PORTAL_URL);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "Open " WIFI_PORTAL_URL);
}
static esp_err_t connect_handler(httpd_req_t *req)
{
    ota_web_status_t progress; OTAServiceGetProgress(&progress);
    if (progress.busy) {
        ESP_LOGW("WiFiPortal", "CONNECT:HTTP_REJECTED OTA busy");
        return error_reply(req, "409 Conflict", "升级期间不能切换网络");
    }
    cJSON *body = read_json(req);
    if (!body) {
        ESP_LOGW("WiFiPortal", "CONNECT:HTTP_REJECTED invalid JSON/body/origin; request not queued");
        return error_reply(req, "400 Bad Request", "请求格式错误");
    }
    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(body, "ssid");
    cJSON *password = cJSON_GetObjectItemCaseSensitive(body, "password");
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(ssid) && cJSON_IsString(password))
        err = WiFiConnectConfigure(ssid->valuestring, password->valuestring);
    ESP_LOGI("WiFiPortal", "CONNECT:HTTP_SUBMIT result=%s (0x%x)", esp_err_to_name(err), (unsigned)err);
    cJSON_Delete(body);
    if (err != ESP_OK) return error_reply(req, err == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "503 Service Unavailable",
        "网络名称须为1至32字节；密码留空或8至63字节，64位须为十六进制密钥；也可能设备正忙，请重试");
    return json_reply(req, cJSON_CreateObject());
}
static esp_err_t scan_handler(httpd_req_t *req)
{
    cJSON *body = read_json(req);
    if (!body) return error_reply(req, "400 Bad Request", "请求格式错误");
    cJSON_Delete(body);
    ota_web_status_t progress; OTAServiceGetProgress(&progress);
    if (progress.busy) return error_reply(req, "409 Conflict", "升级期间请勿扫描网络");
    if (WiFiConnectScan() != ESP_OK) return error_reply(req, "503 Service Unavailable", "设备正忙，请稍后重试");
    return json_reply(req, cJSON_CreateObject());
}
static esp_err_t ota_handler(httpd_req_t *req)
{
    cJSON *body = read_json(req);
    if (!body) return error_reply(req, "400 Bad Request", "请求格式错误");
    cJSON_Delete(body);
    portENTER_CRITICAL(&portal_lock); bool pending = ota_pending; portEXIT_CRITICAL(&portal_lock);
    if (pending) return error_reply(req, "409 Conflict", "正在联网，请稍候");
    bool query_only = strcmp(req->uri, "/api/ota/check") == 0;
    ota_service_trigger_ret_t ret = query_only ? OTAServiceTriggerQuery() : OTAServiceTriggerCheck();
    if (ret == OTA_SERVICE_TRIGGER_NO_NETWORK) {
        portENTER_CRITICAL(&portal_lock);
        ota_query_only = query_only;
        ota_pending = true; ota_deadline = esp_timer_get_time() + 30000000LL;
        portEXIT_CRITICAL(&portal_lock);
        WiFiConnectResume();
    } else if (ret != OTA_SERVICE_TRIGGER_OK) {
        return error_reply(req, "409 Conflict", "升级服务正忙或尚未就绪，请稍后重试");
    }
    return json_reply(req, cJSON_CreateObject());
}
static esp_err_t status_handler(httpd_req_t *req)
{
    wifi_connect_status_t wifi; WiFiConnectGetStatus(&wifi);
    ota_web_status_t ota; OTAServiceGetProgress(&ota);
    cJSON *root = cJSON_CreateObject();
    if (!root) return error_reply(req, "503 Service Unavailable", "内存不足");
    cJSON_AddStringToObject(root, "name", ap_name);
    cJSON_AddStringToObject(root, "version", OTAServiceGetFirmwareVersion());
    cJSON *w = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddNumberToObject(w, "phase", wifi.phase);
    cJSON_AddNumberToObject(w, "reason", wifi.reason);
    cJSON_AddStringToObject(w, "ssid", wifi.ssid);
    cJSON_AddStringToObject(w, "ip", wifi.ip);
    cJSON_AddBoolToObject(w, "saved", wifi.saved);
    cJSON_AddBoolToObject(w, "scanning", wifi.scanning);
    cJSON_AddNumberToObject(w, "scan_error", wifi.scan_error);
    cJSON *o = cJSON_AddObjectToObject(root, "ota");
    cJSON_AddNumberToObject(o, "phase", ota.phase);
    cJSON_AddNumberToObject(o, "received", ota.received);
    cJSON_AddNumberToObject(o, "total", ota.total);
    cJSON_AddBoolToObject(o, "busy", ota.busy);
    cJSON_AddStringToObject(o, "at32_version", ota.at32_version);
    cJSON_AddStringToObject(o, "esp32_version", ota.esp32_version);
    cJSON_AddStringToObject(o, "st32_version", ota.st32_version);
    cJSON_AddStringToObject(o, "at32_current", ota.at32_current);
    cJSON_AddStringToObject(o, "st32_current", ota.st32_current);
    cJSON_AddBoolToObject(o, "at32_checked", ota.at32_checked);
    cJSON_AddBoolToObject(o, "st32_checked", ota.st32_checked);
    cJSON_AddBoolToObject(o, "esp32_checked", ota.esp32_checked);
    portENTER_CRITICAL(&portal_lock); bool pending = ota_pending; portEXIT_CRITICAL(&portal_lock);
    cJSON_AddBoolToObject(o, "connecting", pending);
    cJSON *networks = cJSON_AddArrayToObject(root, "networks");
    wifi_ap_record_t *records = heap_caps_calloc(WIFI_CONNECT_SCAN_LIMIT, sizeof(*records), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (records) {
        unsigned count = WiFiConnectGetScan(records, WIFI_CONNECT_SCAN_LIMIT);
        for (unsigned i = 0; i < count; ++i) {
            char name[33]; memcpy(name, records[i].ssid, 32); name[32] = 0;
            if (!name[0]) continue;
            cJSON *n = cJSON_CreateObject();
            cJSON_AddStringToObject(n, "ssid", name);
            cJSON_AddNumberToObject(n, "rssi", records[i].rssi);
            cJSON_AddBoolToObject(n, "open", records[i].authmode == WIFI_AUTH_OPEN);
            cJSON_AddItemToArray(networks, n);
        }
        heap_caps_free(records);
    }
    return json_reply(req, root);
}
static esp_err_t start_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3; config.send_wait_timeout = 3;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t routes[] = {
        {.uri="/", .method=HTTP_GET, .handler=index_handler},
        {.uri="/api/status", .method=HTTP_GET, .handler=status_handler},
        {.uri="/api/connect", .method=HTTP_POST, .handler=connect_handler},
        {.uri="/api/scan", .method=HTTP_POST, .handler=scan_handler},
        {.uri="/api/ota", .method=HTTP_POST, .handler=ota_handler},
        {.uri="/api/ota/check", .method=HTTP_POST, .handler=ota_handler},
        {.uri="/*", .method=HTTP_GET, .handler=redirect_handler},
    };
    for (unsigned i = 0; i < sizeof(routes)/sizeof(routes[0]); ++i) {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) { httpd_stop(server); server = NULL; return err; }
    }
    return ESP_OK;
}
void WiFiPortalTask(void *arg)
{
    (void)arg;
    bool ap_requested = false;
    bool bluetooth_started = false;
    bool bluetooth_closed = false;
    int64_t bluetooth_idle_since = 0;
    bool announced = false;
    bool request_logged = false;
    int dns = -1;
    for (;;) {
        portENTER_CRITICAL(&portal_lock); bool start = requested; portEXIT_CRITICAL(&portal_lock);
        if (start && !ap_requested) {
            if (!request_logged) {
                ESP_LOGI("WiFiPortal", "PROVISION:ENTER boot button held 5s; starting AP and Bluetooth provisioning");
                request_logged = true;
            }
            if (!bluetooth_started) {
                BluetoothInit();
                nimble_port_freertos_init(bleprph_host_task);
                bluetooth_started = true;
                bluetooth_idle_since = esp_timer_get_time();
            }
            strlcpy(ap_name, DeviceName, sizeof(ap_name));
            if (WiFiConnectOpenAP(ap_name) == ESP_OK) ap_requested = true;
        }
        wifi_connect_status_t wifi; WiFiConnectGetStatus(&wifi);
        if (bluetooth_started && !bluetooth_closed && !BluetoothHasConnected()) {
            int64_t now = esp_timer_get_time();
            if (now - bluetooth_idle_since >= 120LL * 1000000LL) {
                /* Stop accepting connections before stopping the host. If a
                 * connection won the race, keep Bluetooth available to it. */
                int rc = ble_gap_adv_stop();
                if ((rc == 0 || rc == BLE_HS_EALREADY) && !BluetoothHasConnected()) {
                    rc = nimble_port_stop();
                    if (rc == 0) {
                        esp_err_t err = nimble_port_deinit();
                        bluetooth_closed = true;
                        ESP_LOGI("WiFiPortal", "PROVISION:BLE_IDLE_TIMEOUT closed after 120s without clients; deinit=%s", esp_err_to_name(err));
                    } else ESP_LOGW("WiFiPortal", "PROVISION:BLE_STOP_FAILED rc=%d", rc);
                }
            }
        }
        if (!wifi.ap_active && server) {
            httpd_stop(server);
            server = NULL;
        }
        if (!wifi.ap_active && dns >= 0) {
            close(dns);
            dns = -1;
        }
        if (wifi.ap_active && !server) {
            esp_err_t err = start_server();
            if (err != ESP_OK) ESP_LOGW("WiFiPortal", "PROVISION:HTTP_FAILED %s", esp_err_to_name(err));
            else ESP_LOGI("WiFiPortal", "PROVISION:HTTP_READY " WIFI_PORTAL_URL);
        }
        if (wifi.ap_active && dns < 0) {
            dns = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (dns >= 0) {
                struct sockaddr_in addr = {.sin_family=AF_INET, .sin_port=htons(53), .sin_addr.s_addr=inet_addr(WIFI_PORTAL_IP)};
                struct timeval timeout = {.tv_sec=0, .tv_usec=200000};
                setsockopt(dns, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                if (bind(dns, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
                    ESP_LOGW("WiFiPortal", "PROVISION:DNS_BIND_FAILED errno=%d", errno);
                    close(dns); dns=-1;
                } else ESP_LOGI("WiFiPortal", "PROVISION:DNS_READY " WIFI_PORTAL_IP);
            }
        }
        if (wifi.ap_error && !wifi.ap_active && ap_requested) {
            ESP_LOGE("WiFiPortal", "PROVISION:FAILED AP error=%s; UART console remains active", esp_err_to_name(wifi.ap_error));
            portENTER_CRITICAL(&portal_lock); requested = false; portEXIT_CRITICAL(&portal_lock);
            ap_requested = false;
        }
        if (server && dns >= 0 && !announced) {
            ESP_LOGI("WiFiPortal", "PROVISION:READY ssid=%s url=" WIFI_PORTAL_URL " UART console remains active", ap_name);
            MusicPlayProvisioningReady();
            announced = true;
        }
        portENTER_CRITICAL(&portal_lock);
        bool pending = ota_pending; int64_t until = ota_deadline;
        bool query_only = ota_query_only;
        portEXIT_CRITICAL(&portal_lock);
        if (pending) {
            ota_service_trigger_ret_t ret = wifi.phase == WIFI_CONNECT_CONNECTED ?
                (query_only ? OTAServiceTriggerQuery() : OTAServiceTriggerCheck()) : OTA_SERVICE_TRIGGER_NO_NETWORK;
            if (ret == OTA_SERVICE_TRIGGER_OK || ret == OTA_SERVICE_TRIGGER_BUSY || esp_timer_get_time() >= until) {
                portENTER_CRITICAL(&portal_lock); ota_pending = false; portEXIT_CRITICAL(&portal_lock);
                if (ret != OTA_SERVICE_TRIGGER_OK && ret != OTA_SERVICE_TRIGGER_BUSY) OTAServiceReportProgress(OTA_WEB_FAILED, 0, 0);
            }
        }
        if (dns >= 0) {
            uint8_t packet[512]; struct sockaddr_in source; socklen_t len = sizeof(source);
            int n = recvfrom(dns, packet, sizeof(packet), 0, (struct sockaddr *)&source, &len);
            if (n > 0) {
                size_t answer = portal_dns_reply(packet, n, sizeof(packet));
                if (answer) sendto(dns, packet, answer, 0, (struct sockaddr *)&source, len);
            }
        } else vTaskDelay(pdMS_TO_TICKS(200));
    }
}
