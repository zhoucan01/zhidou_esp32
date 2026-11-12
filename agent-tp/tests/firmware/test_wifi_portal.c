#include <assert.h>
#include <stdio.h>
#include "portal_protocol.h"
#include "boot_hold.h"
#include "wifi_credentials.h"

static size_t question(uint8_t *packet, unsigned type)
{
    const uint8_t query[] = {0x12,0x34,1,0,0,1,0,0,0,0,0,0, 3,'w','w','w',0,0,1,0,1};
    memcpy(packet, query, sizeof(query));
    packet[18] = type;
    return sizeof(query);
}
int main(void)
{
    uint8_t packet[512];
    size_t count = question(packet, 1);
    assert(portal_dns_reply(packet, count, sizeof(packet)) == count + 16);
    assert(packet[0] == 0x12 && packet[1] == 0x34 && packet[7] == 1);
    assert(!memcmp(packet + count + 12, (uint8_t[]){172,27,231,1}, 4));
    question(packet, 28);
    assert(portal_dns_reply(packet, count, sizeof(packet)) == count && packet[7] == 0);
    question(packet, 1);
    assert(portal_dns_reply(packet, count, count + 15) == 0);
    for (size_t size = 0; size < count; ++size) {
        question(packet, 1);
        assert(portal_dns_reply(packet, size, sizeof(packet)) == 0);
    }
    question(packet, 1); packet[12] = 0xc0;
    assert(portal_dns_reply(packet, count, sizeof(packet)) == 0);
    question(packet, 1); packet[2] = 0x81;
    assert(portal_dns_reply(packet, count, sizeof(packet)) == 0);
    question(packet, 1); packet[5] = 2;
    assert(portal_dns_reply(packet, count, sizeof(packet)) == 0);
    assert(boot_hold_eligible(0) && boot_hold_eligible(25000000));
    assert(!boot_hold_eligible(-1) && !boot_hold_eligible(25000001));
    assert(!boot_hold_ready(1000000, 5999999));
    assert(boot_hold_ready(1000000, 6000000));
    assert(boot_hold_ready(25000000, 30000000));
    assert(!boot_hold_ready(25000000, 30000001));
    assert(wifi_credentials_valid("Office", "12345678"));
    assert(wifi_credentials_valid("开放网络", ""));
    assert(!wifi_credentials_valid("", "12345678"));
    assert(!wifi_credentials_valid("Office", "1234567"));
    assert(!wifi_credentials_valid(NULL, "12345678"));
    char ssid[34], password[66];
    memset(ssid, 'a', 33); ssid[32]=0;
    assert(wifi_credentials_valid(ssid, "12345678"));
    ssid[32]='a'; ssid[33]=0;
    assert(!wifi_credentials_valid(ssid, "12345678"));
    memset(password, 'f', 65); password[64]=0;
    assert(wifi_credentials_valid("Office", password));
    password[0]='g'; assert(!wifi_credentials_valid("Office", password));
    password[63]=0; assert(wifi_credentials_valid("Office", password));
    password[0]='f'; password[63]='f'; password[64]='f'; password[65]=0;
    assert(!wifi_credentials_valid("Office", password));
    puts("WiFi portal DNS and boot-hold boundaries: PASS");
    return 0;
}
