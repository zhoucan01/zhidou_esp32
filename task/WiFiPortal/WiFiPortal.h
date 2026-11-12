#pragma once
/* Within the first 30s, the encoder's 5s hold requests AP and Bluetooth.
 * Called in task context; radio startup is handled by WiFiPortalTask. */
void WiFiPortalRequestStart(void);
void WiFiPortalTask(void *arg);
