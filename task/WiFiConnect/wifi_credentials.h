#pragma once
#include <stdbool.h>
#include <string.h>
/* IEEE SSID limit is in bytes; a 64-byte WPA PSK must be hexadecimal. */
static inline bool wifi_credentials_valid(const char *ssid, const char *password)
{
    if (!ssid || !password || !ssid[0] || strlen(ssid) > 32 || strlen(password) > 64 || (password[0] && strlen(password) < 8)) return false;
    if (strlen(password) == 64) {
        for (unsigned i = 0; i < 64; ++i) {
            char digit = password[i];
            if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f') || (digit >= 'A' && digit <= 'F'))) return false;
        }
    }
    return true;
}
