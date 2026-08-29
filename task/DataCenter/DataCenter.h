#ifndef DATACENTER__H
#define DATACENTER__H

#include "stdint.h"
#include <stdbool.h>

typedef enum{
    dpSwitchValve=0,
    dpPreeMoter,
    dpClearWaterPump,
    dpDrinePump,
    dpHosePump,
    dpRiseMoter,
    dpBreakMoter,
    dpMoterCount
}moterDate;

typedef enum{
    Valve,
    pressMoter,
    clearWaterPump,
    drinePump,
    hosePump,
    riseMoter,
    breakMoter,
    moterNum
}moterState_t;

typedef enum {
    riseHighPos,
    riseLowPos,
    pressHighPos,
    pressLowPos,
    pressHeadAvail,
    waterBoxAvail,
    waterBoxHighPos,
    waterBoxLowPos,
    wasteBoxAvail,
    valveOnPos,
    valveOffPos,
    brineBoxAvail,
    overFlow,
    lidAvail,
    filerAvail,
    moldedBoxAvail,
    sensorNum
}sensorState_t;

extern bool sensorState[sensorNum];
extern bool moterState[moterNum];
extern char version[3][4];

extern struct DataPool {
    float temperature;
    int   speed;
    float current;
    float turns;
    float voltage;
} g_DataPool;

void DataCenterTask(void *pvParameters);

// 内联函数
inline bool getSensorData(sensorState_t wanndata){
    return sensorState[wanndata];
}

inline bool getMoterData(moterState_t wanndata){
    (void)wanndata;
    return moterState[wanndata];
}

inline float getTurns(void){
    return g_DataPool.turns;
}

inline float getTemp(void){
    return g_DataPool.temperature;
}

// 水泵控制函数
void waterPumpOn(uint8_t waterPumpSpeed);
void waterPumpOff(void);

// 点卤泵控制函数
void brinePumpOn(void);
void brinePumpOff(void);

// 蠕动泵控制函数
void hosePumpOn(uint8_t hosePumpSpeed);
void hosePumpOff(void);

// 压制电机控制函数
void pressMoterDown(uint8_t pressMoterSpeed);
void pressMoterUp  (uint8_t pressMoterSpeed);
void pressMoterStop(void);

// 抬升电机控制函数
void riseMoterDown(uint16_t riseMoterSpeed);
void riseMoterUp(uint16_t riseMoterSpeed);
void riseMoterStop(void);

// WS2812 LED控制函数
void ws2812Coler(uint8_t lednum, uint8_t rColer, uint8_t gColer, uint8_t bColer);

// 高压板控制函数
void setHighBroad(uint8_t highBroadValue);

// 阀门控制函数
void valveOn(void);
void valveOff(void);

// LED灯控制函数
void ledOn(void);
void ledOff(void);

char (*getVersion(void))[4];

void braineAct(void);

#endif
