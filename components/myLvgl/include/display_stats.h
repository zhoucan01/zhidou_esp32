#ifndef DISPLAY_STATS_H
#define DISPLAY_STATS_H

#include <stdint.h>

typedef struct {
    uint32_t frames;
    uint32_t average_us;
    uint32_t maximum_us;
} display_stats_t;

/* Initialize before enabling display callbacks. No allocations or logging. */
void display_stats_init(void);
/* Called only for the final successful DMA completion of a refresh, in ISR. */
void display_stats_record_frame(uint32_t duration_us);
/* Thread-safe snapshot of the last completed one-second window. */
void display_stats_get(display_stats_t *stats);

#endif
