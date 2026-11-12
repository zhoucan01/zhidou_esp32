#pragma once
#include <stdbool.h>
#include <stdint.h>
#define BOOT_PROVISION_WINDOW_US 30000000LL
#define BOOT_PROVISION_HOLD_US 5000000LL
static inline bool boot_hold_eligible(int64_t pressed_us)
{
    return pressed_us >= 0 && pressed_us <= BOOT_PROVISION_WINDOW_US - BOOT_PROVISION_HOLD_US;
}
static inline bool boot_hold_ready(int64_t pressed_us, int64_t now_us)
{
    return boot_hold_eligible(pressed_us) && now_us >= pressed_us + BOOT_PROVISION_HOLD_US && now_us <= BOOT_PROVISION_WINDOW_US;
}
