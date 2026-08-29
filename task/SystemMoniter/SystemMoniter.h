#ifndef SYSMONITIER__H
#define SYSMONITIER__H

void SystemMonitorTask(void *pvParameters);
void setHeat1Power(int heat1Gear);
void setHeat2Power(int heat2Gear);
float getHeatPower(int HeatNum);

#endif
