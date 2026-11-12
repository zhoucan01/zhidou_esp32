#pragma once
/* Single source for DHCP, DNS replies, HTTP redirects and Origin checks. */
#define WIFI_PORTAL_IP "172.27.231.1"
#define WIFI_PORTAL_NETMASK "255.255.255.0"
#define WIFI_PORTAL_IP_BYTES 172,27,231,1
#define WIFI_PORTAL_URL "http://" WIFI_PORTAL_IP "/"
#define WIFI_PORTAL_ORIGIN "http://" WIFI_PORTAL_IP
