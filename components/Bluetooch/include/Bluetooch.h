#ifndef __BLUETOOCH__H
#define __BLUETOOCH__H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ESP APIs */
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

/* FreeRTOS APIs */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/queue.h"

/* NimBLE stack APIs */
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"

/* NimBLE GAP APIs */
#include "host/ble_gap.h"

#include "services/gap/ble_svc_gap.h"
#include "esp_nimble_hci.h"       // 新增

#define BLE_GAP_APPEARANCE_GENERIC_TAG 0x0200
#define BLE_GAP_URI_PREFIX_HTTPS 0x17
#define BLE_GAP_LE_ROLE_PERIPHERAL 0x00

void BluetoothInit(void);
void format_addr(char *addr_str, uint8_t addr[]);


void SendMessage(uint8_t *data, size_t len,uint16_t channal);
void bleprph_host_task(void *param);
bool BlueToochState(void);

#endif
