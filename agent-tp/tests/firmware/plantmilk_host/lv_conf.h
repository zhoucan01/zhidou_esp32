#ifndef LV_CONF_H
#define LV_CONF_H
/* Firmware CONFIG_LV_* settings are force-included by the host runner.
 * Only host diagnostics/performance overlays differ. */
#undef LV_USE_PERF_MONITOR
#define LV_USE_PERF_MONITOR 0
#define LV_USE_FONT_COMPRESSED 1
#undef LV_USE_LOG
#define LV_USE_LOG 1
#undef LV_LOG_LEVEL
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#undef LV_LOG_PRINTF
#define LV_LOG_PRINTF 1
#undef LV_ASSERT_HANDLER_INCLUDE
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#undef LV_ASSERT_HANDLER
#define LV_ASSERT_HANDLER abort();
#undef LV_USE_ASSERT_OBJ
#define LV_USE_ASSERT_OBJ 1
#endif
