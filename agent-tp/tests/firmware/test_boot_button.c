#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "boot_hold.h"
typedef unsigned TickType_t;
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY UINT32_MAX
#define BUTTON_DEBOUNCE_US 10000
#define BUTTON_LONG_PRESS_US 2000000
static int encoder_lock;
static int button_level=1, stable_button_level=1;
static int64_t button_edge_us, press_started_us, now_us;
static bool long_press_sent, provisioning_press, isPress, isLongPress;
static unsigned opened;
static int64_t esp_timer_get_time(void) { return now_us; }
static void open_portal(void) { ++opened; }
static void (*boot_hold_callback)(void) = open_portal;
#include "encoder_button.inc"
static void reset(void)
{
    button_level=stable_button_level=1;
    button_edge_us=press_started_us=now_us=0;
    long_press_sent=provisioning_press=isPress=isLongPress=false;
    opened=0;
}
static void edge(int level, int64_t time)
{
    button_level=level; button_edge_us=time; now_us=time+10000;
    (void)process_button();
}
int main(void)
{
    reset(); edge(0,2000000);
    now_us=4000000; (void)process_button(); assert(!isLongPress && !opened);
    now_us=7000000; (void)process_button(); assert(opened==1 && !isLongPress && !isPress);
    now_us=8000000; (void)process_button(); edge(1,8100000); assert(opened==1 && !isPress && !isLongPress);
    reset(); edge(0,1000000); edge(1,1100000); assert(isPress && !opened);
    reset(); edge(0,1000000); edge(1,4000000); assert(isLongPress && !opened);
    reset(); edge(0,31000000); now_us=33000000; (void)process_button(); assert(isLongPress && !opened);
    reset(); edge(0,25000000); now_us=30000000; (void)process_button(); assert(opened==1 && !isLongPress);
    reset(); edge(0,26000000); now_us=28000000; (void)process_button(); assert(isLongPress && !opened);
    puts("Boot provisioning gesture and normal buttons: PASS");
    return 0;
}
