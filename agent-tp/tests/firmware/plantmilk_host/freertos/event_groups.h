#ifndef HOST_EVENT_GROUPS_H
#define HOST_EVENT_GROUPS_H
#include <stdint.h>
typedef void *EventGroupHandle_t;
typedef uint32_t EventBits_t;
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupGetBits(EventGroupHandle_t group);
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits);
#endif
