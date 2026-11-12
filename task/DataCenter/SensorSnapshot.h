#ifndef SENSOR_SNAPSHOT_H
#define SENSOR_SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>

/* 站号 2 超过此时间未成功更新，UI 不再使用旧传感器数据。 */
#define DATACENTER_SENSOR_TIMEOUT_MS 1000U

/* 原子读取站号 2 的原始传感器位图（0 = 在位）。
 * 未收到有效数据或数据超时返回 false，并将位图置为全 1。
 * 只读取本地快照，不发起通信，不等待下位机。 */
bool DataCenterGetSensorSnapshot(uint16_t *raw_inputs);

#endif
