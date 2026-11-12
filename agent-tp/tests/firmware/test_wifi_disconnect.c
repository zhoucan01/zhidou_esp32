/* Compile against the actual disconnect_sta body extracted into build/. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef int esp_err_t;
typedef struct { unsigned char unused; } wifi_ap_record_t;
enum { ESP_OK=0, ESP_ERR_TIMEOUT=263, ESP_ERR_WIFI_NOT_CONNECT=0x300f,
       ESP_ERR_WIFI_CONN=0x3007, WIFI_CONNECT_FAILED=3, WIFI_CONNECT_TIMEOUT_MS=15000,
       pdTRUE=1, pdFALSE=0 };
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
static int driver_events;
static bool associated, disconnect_requested;
static int64_t deadline;
static int disconnect_result, ap_result, event_bits, queries, waits, failure;
static void xEventGroupClearBits(int group, int bits) { (void)group; (void)bits; }
static int esp_wifi_disconnect(void) { return disconnect_result; }
static int xEventGroupWaitBits(int group, int bits, int clear, int all, int ticks)
{ (void)group; (void)bits; (void)clear; (void)all; assert(ticks==1000); ++waits; return event_bits; }
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) { (void)ap; ++queries; return ap_result; }
static void set_state(int phase, int reason) { assert(phase==WIFI_CONNECT_FAILED); failure=reason; }
static int64_t esp_timer_get_time(void) { return 123000; }
#include "wifi_disconnect.inc"
static void reset(void)
{
    disconnect_result=ESP_OK; ap_result=ESP_ERR_WIFI_NOT_CONNECT;
    event_bits=queries=waits=failure=0; associated=true; disconnect_requested=false; deadline=0;
}
int main(void)
{
    reset(); /* Regression: disconnect OK, no event, driver already idle. */
    assert(disconnect_sta()==ESP_OK && queries==1 && waits==1);
    assert(!associated && !disconnect_requested && !failure);
    reset(); event_bits=1;
    assert(disconnect_sta()==ESP_OK && queries==0 && waits==1 && !disconnect_requested);
    reset(); ap_result=ESP_OK; /* Still associated: must not start another connection. */
    assert(disconnect_sta()==ESP_ERR_TIMEOUT && failure==ESP_ERR_TIMEOUT && !disconnect_requested);
    reset(); ap_result=ESP_ERR_WIFI_CONN; /* Unknown state must not count as disconnected. */
    assert(disconnect_sta()==ESP_ERR_TIMEOUT && failure==ESP_ERR_TIMEOUT);
    reset(); disconnect_result=ESP_ERR_WIFI_CONN;
    assert(disconnect_sta()==ESP_ERR_WIFI_CONN && failure==ESP_ERR_WIFI_CONN && !waits && !disconnect_requested);
    reset(); disconnect_result=ESP_ERR_WIFI_NOT_CONNECT;
    assert(disconnect_sta()==ESP_OK && !waits && !disconnect_requested);
    puts("wifi disconnect regression: PASS (6 cases)");
}
