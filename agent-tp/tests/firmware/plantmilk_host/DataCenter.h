#ifndef HOST_DATA_CENTER_H
#define HOST_DATA_CENTER_H
#include <stdbool.h>
#include "../../task/DataCenter/SensorSnapshot.h"
typedef enum {riseHighPos, riseLowPos, pressHighPos, pressLowPos, pressHeadAvail,
              waterBoxAvail, waterBoxHighPos, waterBoxLowPos, wasteBoxAvail,
              valveOnPos, valveOffPos, brineBoxAvail, overFlow, lidAvail,
              filerAvail, moldedBoxAvail, sensorNum} sensorState_t;
extern bool sensorState[sensorNum];
static inline bool getSensorData(sensorState_t sensor) { return sensorState[sensor]; }
void TestSensorSnapshotReset(void);
void TestSensorSnapshotUpdate(void);
void riseMoterUp(uint16_t speed);
void riseMoterStop(void);
void pressMoterUp(uint16_t speed);
void pressMoterStop(void);
void riseMoterSetCurrentGear(uint8_t gear);
#endif
