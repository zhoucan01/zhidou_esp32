#include "WiFiConnect.h"
#include "wifi_credentials.h"
#include "portal_network.h"
#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "dhcpserver/dhcpserver.h"

typedef enum { CMD_CONNECT, CMD_AP, CMD_SCAN, CMD_STOP, CMD_RESUME, CMD_FORGET } command_type_t;
typedef struct { command_type_t type; char ssid[33]; char password[65]; } command_t;
static QueueHandle_t commands;
static EventGroupHandle_t driver_events;
static TaskHandle_t worker;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_connect_status_t status;
static wifi_ap_record_t scan_records[WIFI_CONNECT_SCAN_LIMIT];
static volatile bool scan_done, got_ip;
static bool scan_running, ap_started, stopped, dirty_credentials, connected_once;
static esp_netif_t *ap_netif;
static int64_t ap_idle_since;
static atomic_bool ap_client_connected_once;
#define PROVISION_IDLE_TIMEOUT_US (120LL * 1000000LL)
static bool retry_waiting;
static char target_ssid[33], target_password[65];
static int64_t deadline;
static uint32_t retries;
static int64_t attempt_started;
static bool associated, disconnect_requested; /* protected by lock */
extern EventGroupHandle_t WiFiEventGroup;

/* Wi-Fi event reasons are not esp_err_t values: keep the two namespaces
 * separate (e.g. 202=AUTH_FAIL, 263=ESP_ERR_TIMEOUT). */
static const char *disconnect_reason_name(unsigned reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_LEAVE: return "AUTH_LEAVE";
    case WIFI_REASON_ASSOC_EXPIRE: return "ASSOC_EXPIRE";
    case WIFI_REASON_ASSOC_LEAVE: return "ASSOC_LEAVE";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_802_1X_AUTH_FAILED: return "802_1X_AUTH_FAILED";
    case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
    case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_CONNECTION_FAIL: return "CONNECTION_FAIL";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY: return "NO_COMPATIBLE_SECURITY";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return "AUTHMODE_BELOW_THRESHOLD";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return "RSSI_BELOW_THRESHOLD";
    default: return "OTHER_WIFI_REASON";
    }
}

static bool load_credentials(void)
{
    nvs_handle_t nvs;
    if (nvs_open("NVSWifi", NVS_READONLY, &nvs) != ESP_OK) return false;
    size_t a = sizeof(target_ssid), b = sizeof(target_password);
    esp_err_t err = nvs_get_str(nvs, "USRWifissid", target_ssid, &a);
    if (nvs_get_str(nvs, "USRWifiPassword", target_password, &b) != ESP_OK) target_password[0] = 0;
    nvs_close(nvs);
    return err == ESP_OK && target_ssid[0];
}
static bool save_credentials(void)
{
    nvs_handle_t nvs;
    if (nvs_open("NVSWifi", NVS_READWRITE, &nvs) != ESP_OK) return false;
    esp_err_t err = nvs_set_str(nvs, "USRWifissid", target_ssid);
    if (err == ESP_OK) err = nvs_set_str(nvs, "USRWifiPassword", target_password);
    if (err == ESP_OK) err = nvs_set_i8(nvs, "WifiInfo", 1);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err == ESP_OK;
}
static void set_state(wifi_connect_phase_t phase, int reason)
{
    portENTER_CRITICAL(&lock);
    status.phase = phase; status.reason = reason;
    portEXIT_CRITICAL(&lock);
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED)
        atomic_store(&ap_client_connected_once, true);
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) scan_done = true;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *event = data;
        portENTER_CRITICAL(&lock); associated = true; portEXIT_CRITICAL(&lock);
        if (event) ESP_LOGI("WiFiConnect", "CONNECT:ASSOCIATED ssid=%.*s channel=%u auth=%u; waiting for DHCP IP",
                           (int)event->ssid_len, (const char *)event->ssid,
                           (unsigned)event->channel, (unsigned)event->authmode);
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = data;
        portENTER_CRITICAL(&lock);
        bool intentional = disconnect_requested;
        associated = false;
        portEXIT_CRITICAL(&lock);
        ESP_LOGW("WiFiConnect", "CONNECT:DISCONNECTED reason=%u (%s) local_request=%u",
                 (unsigned)(event ? event->reason : 0),
                 disconnect_reason_name(event ? event->reason : 0), (unsigned)intentional);
        got_ip = false;
        set_state(WIFI_CONNECT_FAILED, event ? event->reason : 0);
        if (WiFiEventGroup) xEventGroupClearBits(WiFiEventGroup, 1 << 1);
        if (driver_events) xEventGroupSetBits(driver_events, 1);
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        portENTER_CRITICAL(&lock);
        status.phase = WIFI_CONNECT_CONNECTED; status.reason = 0;
        snprintf(status.ip, sizeof(status.ip), IPSTR, IP2STR(&event->ip_info.ip));
        portEXIT_CRITICAL(&lock);
        got_ip = true;
        ESP_LOGI("WiFiConnect", "CONNECT:GOT_IP ip=" IPSTR " mask=" IPSTR " gateway=" IPSTR,
                 IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.netmask), IP2STR(&event->ip_info.gw));
        if (WiFiEventGroup) xEventGroupSetBits(WiFiEventGroup, 1 << 1);
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)
        ESP_LOGW("WiFiConnect", "CONNECT:LOST_IP DHCP address lost");
}
static esp_err_t disconnect_sta(void)
{
    /* Drain the previous association's disconnect event before starting a
     * new attempt, so an old event cannot overwrite the new result. */
    xEventGroupClearBits(driver_events, 1);
    portENTER_CRITICAL(&lock); disconnect_requested = true; portEXIT_CRITICAL(&lock);
    esp_err_t err = esp_wifi_disconnect();
    ESP_LOGI("WiFiConnect", "CONNECT:DISCONNECT_PREVIOUS result=%s (0x%x)", esp_err_to_name(err), (unsigned)err);
    if (err == ESP_OK) {
        if (!(xEventGroupWaitBits(driver_events, 1, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000)) & 1)) {
            /* An already-disconnected STA can return ESP_OK without a new
             * DISCONNECTED event. Confirm with the driver instead of blocking
             * every subsequent provisioning attempt on a nonexistent event. */
            wifi_ap_record_t ap;
            esp_err_t state = esp_wifi_sta_get_ap_info(&ap);
            ESP_LOGI("WiFiConnect", "CONNECT:DISCONNECT_VERIFY result=%s (0x%x)",
                     esp_err_to_name(state), (unsigned)state);
            if (state == ESP_ERR_WIFI_NOT_CONNECT) {
                portENTER_CRITICAL(&lock); associated = false; disconnect_requested = false; portEXIT_CRITICAL(&lock);
                ESP_LOGI("WiFiConnect", "CONNECT:DISCONNECT_CONFIRMED already disconnected; continuing new connection");
                return ESP_OK;
            }
            portENTER_CRITICAL(&lock); disconnect_requested = false; portEXIT_CRITICAL(&lock);
            ESP_LOGW("WiFiConnect", "CONNECT:TIMEOUT stage=disconnect_previous wait_ms=1000 error=263 (ESP_ERR_TIMEOUT)");
            set_state(WIFI_CONNECT_FAILED, ESP_ERR_TIMEOUT);
            deadline = esp_timer_get_time() + WIFI_CONNECT_TIMEOUT_MS * 1000LL;
            return ESP_ERR_TIMEOUT;
        }
    }
    portENTER_CRITICAL(&lock); disconnect_requested = false; portEXIT_CRITICAL(&lock);
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) {
        set_state(WIFI_CONNECT_FAILED, err);
        deadline = esp_timer_get_time() + WIFI_CONNECT_TIMEOUT_MS * 1000LL;
        return err;
    }
    return ESP_OK;
}
static esp_err_t connect_target(void)
{
    attempt_started = esp_timer_get_time();
    ESP_LOGI("WiFiConnect", "CONNECT:CREDENTIALS ssid=[%s] password=[%s] password_len=%u",
             target_ssid, target_password, (unsigned)strlen(target_password));
    ESP_LOGI("WiFiConnect", "CONNECT:BEGIN ssid=%s security=%s retry=%lu ap_active=%u timeout_ms=%u",
             target_ssid, target_password[0] ? "WPA2-or-higher" : "OPEN",
             (unsigned long)retries, (unsigned)ap_started, (unsigned)WIFI_CONNECT_TIMEOUT_MS);
    retry_waiting = false;
    got_ip = false;
    if (WiFiEventGroup) xEventGroupClearBits(WiFiEventGroup, 1 << 1);
    if (scan_running) {
        esp_err_t scan_err = esp_wifi_scan_stop();
        ESP_LOGI("WiFiConnect", "CONNECT:STOP_SCAN result=%s", esp_err_to_name(scan_err));
        esp_wifi_clear_ap_list();
        scan_running = false; scan_done = false;
    }
    esp_err_t disconnect_result = disconnect_sta();
    if (disconnect_result != ESP_OK) return disconnect_result;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, target_ssid, strlen(target_ssid));
    memcpy(config.sta.password, target_password, strlen(target_password));
    config.sta.threshold.authmode = target_password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    ESP_LOGI("WiFiConnect", "CONNECT:SET_CONFIG result=%s (0x%x) auth_threshold=%u",
             esp_err_to_name(err), (unsigned)err, (unsigned)config.sta.threshold.authmode);
    portENTER_CRITICAL(&lock);
    strlcpy(status.ssid, target_ssid, sizeof(status.ssid)); status.ip[0] = 0; status.scanning = false;
    portEXIT_CRITICAL(&lock);
    if (err == ESP_OK) {
        err = esp_wifi_connect();
        ESP_LOGI("WiFiConnect", "CONNECT:DRIVER_START result=%s (0x%x)", esp_err_to_name(err), (unsigned)err);
    }
    set_state(err == ESP_OK ? WIFI_CONNECT_CONNECTING : WIFI_CONNECT_FAILED, err);
    deadline = esp_timer_get_time() + WIFI_CONNECT_TIMEOUT_MS * 1000LL;
    return err;
}
static void start_scan(void)
{
    if (scan_running) return;
    wifi_scan_config_t config = {.show_hidden = true};
    esp_err_t err = esp_wifi_scan_start(&config, false);
    portENTER_CRITICAL(&lock);
    status.scanning = err == ESP_OK; status.scan_error = err;
    portEXIT_CRITICAL(&lock);
    scan_running = err == ESP_OK;
}
static void finish_scan(void)
{
    scan_done = false;
    if (!scan_running) return;
    uint16_t count = WIFI_CONNECT_SCAN_LIMIT;
    /* The old on-stack array used a 1456-byte function frame on a 4 KiB
     * task, before SDK call depth. Keep scan work off the task's stack. */
    wifi_ap_record_t *records = heap_caps_calloc(count, sizeof(*records), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_err_t err = records ? esp_wifi_scan_get_ap_records(&count, records) : ESP_ERR_NO_MEM;
    if (err != ESP_OK) esp_wifi_clear_ap_list();
    portENTER_CRITICAL(&lock);
    if (err == ESP_OK) memcpy(scan_records, records, count * sizeof(records[0]));
    status.scan_count = err == ESP_OK ? count : 0; status.scan_error = err; status.scanning = false;
    portEXIT_CRITICAL(&lock);
    heap_caps_free(records);
    scan_running = false;
    ESP_LOGI("WiFiConnect", "PROVISION:SCAN_DONE count=%u result=%s stack_free=%u bytes",
             (unsigned)(err == ESP_OK ? count : 0), esp_err_to_name(err),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static esp_err_t open_ap(const char *name)
{
    esp_err_t err = ESP_OK;
    const char *stage = "create AP netif";
    ESP_LOGI("WiFiConnect", "PROVISION:AP_START ssid=%s free_internal=%u stack_free=%u",
             name, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_AP();
    esp_netif_t *netif = esp_netif_new(&netif_config);
    if (!netif) { err = ESP_ERR_NO_MEM; goto failed; }
#define AP_STEP(label, call) do { stage = label; ESP_LOGI("WiFiConnect", "PROVISION:AP_STEP %s", stage); err = (call); if (err != ESP_OK) goto failed; } while (0)
    AP_STEP("attach AP netif", esp_netif_attach_wifi_ap(netif));
    AP_STEP("register AP handlers", esp_wifi_set_default_wifi_ap_handlers());
    stage = "stop DHCP";
    err = esp_netif_dhcps_stop(netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) goto failed;
    esp_netif_ip_info_t ip = {0};
    ip.ip.addr = esp_ip4addr_aton(WIFI_PORTAL_IP);
    ip.gw = ip.ip;
    ip.netmask.addr = esp_ip4addr_aton(WIFI_PORTAL_NETMASK);
    AP_STEP("set AP address", esp_netif_set_ip_info(netif, &ip));
    esp_netif_dns_info_t dns = {.ip.type = ESP_IPADDR_TYPE_V4};
    dns.ip.u_addr.ip4.addr = ip.ip.addr;
    AP_STEP("set AP DNS", esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns));
    dhcps_offer_t offer = OFFER_DNS;
    AP_STEP("offer DNS via DHCP", esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer)));
    AP_STEP("arm DHCP", esp_netif_dhcps_start(netif));
    AP_STEP("enable APSTA", esp_wifi_set_mode(WIFI_MODE_APSTA));
    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, name, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(name);
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    AP_STEP("configure AP", esp_wifi_set_config(WIFI_IF_AP, &ap));
    ap_started = true;
    ap_netif = netif;
    ap_idle_since = esp_timer_get_time();
    portENTER_CRITICAL(&lock); status.ap_active = true; status.ap_error = 0; portEXIT_CRITICAL(&lock);
    ESP_LOGI("WiFiConnect", "PROVISION:AP_READY ssid=%s url=" WIFI_PORTAL_URL " UART unchanged", name);
    return ESP_OK;
failed:
    ESP_LOGE("WiFiConnect", "PROVISION:AP_FAILED step=%s error=%s; keeping UART/tasks running", stage, esp_err_to_name(err));
    if (netif) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_netif_destroy_default_wifi(netif);
    }
    portENTER_CRITICAL(&lock); status.ap_error = err; status.ap_active = false; portEXIT_CRITICAL(&lock);
    return err;
#undef AP_STEP
}
void WiFiConnectTask(void *arg)
{
    (void)arg;
    worker = xTaskGetCurrentTaskHandle();
    driver_events = xEventGroupCreate();
    ESP_ERROR_CHECK(driver_events ? ESP_OK : ESP_ERR_NO_MEM);
    commands = xQueueCreate(3, sizeof(command_t));
    ESP_ERROR_CHECK(commands ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    bool loaded = load_credentials();
    portENTER_CRITICAL(&lock); status.saved = loaded; portEXIT_CRITICAL(&lock);
#ifdef WIFI_CONNECT_TEST
    if (!loaded) {
        strlcpy(target_ssid, WIFI_CONNECT_SSID, sizeof(target_ssid));
        strlcpy(target_password, WIFI_CONNECT_PASSWORD, sizeof(target_password));
    }
#endif
    ESP_LOGI("WiFiConnect", "CONNECT:BOOT credentials=%s", loaded ? "saved NVS" : "factory fallback");
    ESP_ERROR_CHECK(esp_wifi_start());
#ifdef WIFI_CONNECT_TEST
    if (target_ssid[0]) connect_target();
#endif
    for (;;) {
        command_t c;
        if (xQueueReceive(commands, &c, pdMS_TO_TICKS(200)) == pdTRUE) {
            switch (c.type) {
            case CMD_CONNECT:
                strlcpy(target_ssid, c.ssid, sizeof(target_ssid));
                strlcpy(target_password, c.password, sizeof(target_password));
                dirty_credentials = true; stopped = false; retries = 0; connected_once = false;
                connect_target(); break;
            case CMD_AP:
                if (!ap_started && open_ap(c.ssid) != ESP_OK) break;
                if (!got_ip) { stopped = true; (void)disconnect_sta(); }
                start_scan(); break;
            case CMD_SCAN: start_scan(); break;
            case CMD_STOP: stopped = true; esp_wifi_disconnect(); break;
            case CMD_RESUME: stopped = false; connect_target(); break;
            case CMD_FORGET:
                stopped = true; target_ssid[0] = target_password[0] = 0;
                dirty_credentials = false; esp_wifi_disconnect(); save_credentials(); break;
            }
        }
        if (ap_started && !atomic_load(&ap_client_connected_once)) {
            wifi_sta_list_t clients = {0};
            int64_t now = esp_timer_get_time();
            /* Only hotspot clients count; STA's router connection is unrelated. */
            if (esp_wifi_ap_get_sta_list(&clients) == ESP_OK) {
                if (clients.num) atomic_store(&ap_client_connected_once, true);
                else if (now - ap_idle_since >= PROVISION_IDLE_TIMEOUT_US &&
                         !atomic_load(&ap_client_connected_once)) {
                    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
                    if (err == ESP_OK) {
                        ap_started = false;
                        esp_netif_destroy_default_wifi(ap_netif);
                        ap_netif = NULL;
                        portENTER_CRITICAL(&lock);
                        status.ap_active = false;
                        portEXIT_CRITICAL(&lock);
                        ESP_LOGI("WiFiConnect", "PROVISION:AP_IDLE_TIMEOUT closed after 120s without clients");
                    } else ESP_LOGW("WiFiConnect", "PROVISION:AP_CLOSE_FAILED %s", esp_err_to_name(err));
                }
            }
        }
        if (scan_done) finish_scan();
        if (got_ip) connected_once = true;
        if (got_ip && dirty_credentials) {
            wifi_ap_record_t current;
            if (esp_wifi_sta_get_ap_info(&current) == ESP_OK && !strncmp((char *)current.ssid, target_ssid, 32)) {
                bool saved = save_credentials();
                ESP_LOGI("WiFiConnect", "CONNECT:SAVE_CREDENTIALS result=%s", saved ? "OK" : "FAILED (NVS)");
                portENTER_CRITICAL(&lock); status.saved = saved; portEXIT_CRITICAL(&lock);
                dirty_credentials = false;
            }
        }
        if (!got_ip && !stopped && !scan_running && esp_timer_get_time() >= deadline) {
            if (retry_waiting) connect_target();
            else {
                wifi_connect_status_t current;
                WiFiConnectGetStatus(&current);
                portENTER_CRITICAL(&lock); bool has_association = associated; portEXIT_CRITICAL(&lock);
                ESP_LOGW("WiFiConnect", "CONNECT:TIMEOUT stage=%s elapsed_ms=%lu last_reason=%d; %s",
                         has_association ? "dhcp_wait" : "association_or_auth",
                         (unsigned long)((esp_timer_get_time() - attempt_started) / 1000), current.reason,
                         ap_started && !connected_once ? "waiting for user retry" : "will retry automatically");
                set_state(WIFI_CONNECT_FAILED, current.reason ? current.reason : ESP_ERR_TIMEOUT);
                if (ap_started && !connected_once) stopped = true;
                else {
                    ++retries;
                    esp_wifi_disconnect();
                    retry_waiting = true;
                    deadline = esp_timer_get_time() + WIFI_CONNECT_RETRY_DELAY_MS * 1000LL;
                }
            }
        }
    }
}
static esp_err_t submit(command_t *c)
{
    if (!commands) return ESP_ERR_INVALID_STATE;
    return xQueueSend(commands, c, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
esp_err_t WiFiConnectConfigure(const char *ssid, const char *password)
{
    if (!wifi_credentials_valid(ssid, password)) {
        ESP_LOGW("WiFiConnect", "CONNECT:REQUEST_REJECTED invalid SSID/password format (password omitted)");
        return ESP_ERR_INVALID_ARG;
    }
    command_t c = {.type=CMD_CONNECT};
    strlcpy(c.ssid, ssid, sizeof(c.ssid)); strlcpy(c.password, password, sizeof(c.password));
    esp_err_t err = submit(&c);
    ESP_LOGI("WiFiConnect", "CONNECT:REQUEST ssid=%s queue_result=%s (0x%x)", ssid, esp_err_to_name(err), (unsigned)err);
    if (err == ESP_OK) {
        portENTER_CRITICAL(&lock);
        status.phase = WIFI_CONNECT_CONNECTING; status.reason = 0; status.saved = false;
        strlcpy(status.ssid, ssid, sizeof(status.ssid));
        portEXIT_CRITICAL(&lock);
    }
    return err;
}
esp_err_t WiFiConnectOpenAP(const char *name)
{
    if (!name || !name[0] || strlen(name) > 31) return ESP_ERR_INVALID_ARG;
    command_t c = {.type=CMD_AP}; strlcpy(c.ssid, name, sizeof(c.ssid)); return submit(&c);
}
esp_err_t WiFiConnectScan(void) { command_t c = {.type=CMD_SCAN}; return submit(&c); }
void WiFiConnectGetStatus(wifi_connect_status_t *out)
{ portENTER_CRITICAL(&lock); *out = status; portEXIT_CRITICAL(&lock); }
unsigned WiFiConnectGetScan(wifi_ap_record_t *out, unsigned capacity)
{
    portENTER_CRITICAL(&lock);
    unsigned count = status.scan_count < capacity ? status.scan_count : capacity;
    memcpy(out, scan_records, count * sizeof(*out));
    portEXIT_CRITICAL(&lock); return count;
}
bool WiFiConnectIsConnected(void) { return got_ip; }
uint32_t WiFiConnectGetRetryCount(void) { return retries; }
int8_t WiFiConnectGetRssi(void)
{ wifi_ap_record_t ap; return got_ip && esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0; }
void WiFiConnectStop(void) { command_t c = {.type=CMD_STOP}; (void)submit(&c); }
void WiFiConnectResume(void) { command_t c = {.type=CMD_RESUME}; (void)submit(&c); }
void WiFiConnectForget(void) { command_t c = {.type=CMD_FORGET}; (void)submit(&c); }
esp_err_t WiFiConnectStart(void)
{
    if (worker) return ESP_OK;
    return xTaskCreate(WiFiConnectTask, "WiFiConnect", WIFI_CONNECT_TASK_STACK_SIZE,
                       NULL, WIFI_CONNECT_TASK_PRIORITY, &worker) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
