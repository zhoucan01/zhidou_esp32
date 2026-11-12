#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
mkdir -p build/test_ota_web_versions
cat > build/test_ota_web_versions/test.c <<'C'
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "OTAService.h"
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define MODBUS_SLAVE_ACTUATOR 2
#define MODBUS_SLAVE_POWER 3
#define MB_ACT_INPUT_VERSION_HIGH 11
#define MB_POWER_INPUT_VERSION_HIGH 11
static bool valid = true;
static bool ModbusCopyCachedInputRegisters(uint8_t slave, uint16_t address,
                                           uint16_t count, uint16_t *out)
{
    assert(address == 11 && count == 2);
    out[0] = slave == 2 ? 0x0102 : 0x03ff;
    out[1] = slave == 2 ? 0x0304 : 0xfe80;
    return valid;
}
static size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t length = strlen(src);
    if (size) { size_t n = length < size - 1 ? length : size - 1;
        memcpy(dst, src, n); dst[n] = 0; }
    return length;
}
C
# Compile the actual production reporting and snapshot functions.
sed -n '/^static portMUX_TYPE progress_lock/,/^\/\* 外部触发标志/{ /^\/\* 外部触发标志/!p; }' task/OTAService/OTAService.c >> build/test_ota_web_versions/test.c
cat >> build/test_ota_web_versions/test.c <<'C'
int main(void)
{
    ota_web_status_t out;
    OTAServiceGetProgress(&out);
    assert(!out.at32_checked && !out.st32_checked && !out.esp32_checked);
    assert(!strcmp(out.at32_current, "1.2.3.4"));
    assert(!strcmp(out.st32_current, "3.255.254.128"));
    OTAServiceReportVersion(OTA_FW_TARGET_AT32, "1.2.3.5", true);
    OTAServiceReportVersion(OTA_FW_TARGET_ST32, "3.255.254.128", false);
    OTAServiceReportVersion(OTA_FW_TARGET_ESP32, "0.0.2.0", false);
    OTAServiceGetProgress(&out);
    assert(!strcmp(out.at32_version, "1.2.3.5"));
    assert(!strcmp(out.st32_version, "3.255.254.128"));
    assert(!strcmp(out.esp32_version, "0.0.2.0"));
    assert(out.at32_checked && out.st32_checked && out.esp32_checked);
    assert(out.update_available);
    valid = false;
    OTAServiceGetProgress(&out);
    assert(!out.at32_current[0] && !out.st32_current[0]);
    memset(&web_progress, 0, sizeof(web_progress));
    OTAServiceReportVersion(OTA_FW_TARGET_ST32, "0.0.3.0", false);
    OTAServiceGetProgress(&out);
    assert(out.st32_checked && !out.at32_checked && !out.esp32_checked);
    assert(!out.update_available);
    puts("PASS: three independent OTA targets, cached versions, unknown and no-update states");
}
C
cc -std=c99 -Wall -Wextra -Werror -I task/OTAService build/test_ota_web_versions/test.c -o build/test_ota_web_versions/test
build/test_ota_web_versions/test
