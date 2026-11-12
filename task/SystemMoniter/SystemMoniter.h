#ifndef SYSMONITIER__H
#define SYSMONITIER__H

void SystemMonitorTask(void *pvParameters);
#ifndef SYSTEM_MEMORY_LOG_ENABLE
#define SYSTEM_MEMORY_LOG_ENABLE 0
#endif

#define SYSTEM_MEMORY_LOG_INTERVAL_MS 60000U
void setHeat1Power(int heat1Gear);
void setHeat2Power(int heat2Gear);
float getHeatPower(int HeatNum);

#endif
