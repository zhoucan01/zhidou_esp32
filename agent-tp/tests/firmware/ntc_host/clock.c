/* Default clock for legacy conversion tests. Timing regressions supply their
 * own controlled clock instead of linking this file. */
#include "esp_timer.h"
int64_t esp_timer_get_time(void)
{
    static int64_t now_us;
    now_us += 20000;
    return now_us;
}
