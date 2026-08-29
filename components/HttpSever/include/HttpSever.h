#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdint.h>
#include "esp_http_server.h"

extern uint8_t arr[16];

httpd_handle_t start_webserver(void);

#endif