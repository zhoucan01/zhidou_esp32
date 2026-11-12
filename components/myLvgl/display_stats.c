#include "display_stats.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define STATS_WINDOW_US 1000000LL

static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t window_start_us;
static uint32_t current_frames;
static uint64_t current_total_us;
static uint32_t current_maximum_us;
static uint32_t completed_frames;
static uint64_t completed_total_us;
static uint32_t completed_maximum_us;

/* Caller holds stats_lock. Empty windows must replace stale active results. */
static void advance_window(int64_t now_us)
{
    int64_t elapsed = now_us - window_start_us;
    if (elapsed < STATS_WINDOW_US) return;

    if (elapsed < 2 * STATS_WINDOW_US) {
        completed_frames = current_frames;
        completed_total_us = current_total_us;
        completed_maximum_us = current_maximum_us;
    } else {
        completed_frames = 0;
        completed_total_us = 0;
        completed_maximum_us = 0;
    }
    current_frames = 0;
    current_total_us = 0;
    current_maximum_us = 0;
    window_start_us = now_us - elapsed % STATS_WINDOW_US;
}

void display_stats_init(void)
{
    portENTER_CRITICAL(&stats_lock);
    window_start_us = esp_timer_get_time();
    current_frames = completed_frames = 0;
    current_total_us = completed_total_us = 0;
    current_maximum_us = completed_maximum_us = 0;
    portEXIT_CRITICAL(&stats_lock);
}

void display_stats_record_frame(uint32_t duration_us)
{
    portENTER_CRITICAL_ISR(&stats_lock);
    advance_window(esp_timer_get_time());
    ++current_frames;
    current_total_us += duration_us;
    if (duration_us > current_maximum_us) current_maximum_us = duration_us;
    portEXIT_CRITICAL_ISR(&stats_lock);
}

void display_stats_get(display_stats_t *stats)
{
    uint64_t total_us;
    portENTER_CRITICAL(&stats_lock);
    advance_window(esp_timer_get_time());
    stats->frames = completed_frames;
    stats->maximum_us = completed_maximum_us;
    total_us = completed_total_us;
    portEXIT_CRITICAL(&stats_lock);
    stats->average_us = stats->frames ? (uint32_t)(total_us / stats->frames) : 0;
}
