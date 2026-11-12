#ifndef DATACENTER__H
#define DATACENTER__H

#include "stdint.h"
#include <stdbool.h>
#include <stddef.h>
#include "ntcTemp.h"
#include "SensorSnapshot.h"

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
    riseHighPos,//抬升合并高位（bit0）
    riseLowPos,//抬升合并低位（bit1，编码器下行到位）
    pressHighPos,//压制高位
    pressLowPos,//压制低位
    pressHeadAvail,//锤头
    waterBoxAvail,//水箱
    waterBoxHighPos,//水箱高位
    waterBoxLowPos,//水箱低位
    wasteBoxAvail,//水箱在位
    valveOnPos,//阀开位
    valveOffPos,//阀关位
    brineBoxAvail,//点卤盒
    overFlow,//溢流
    lidAvail,//盖子
    riseLeftHighPos,//抬升左高实际状态（bit14，PF6）
    riseRightHighPos,//抬升右高实际状态（bit15，PA15）
    brineLowPos,//点卤盒低水位（站2离散输入 0x0010）
    modeAvail,//成型通道在位（站2离散输入 0x0011）
    sensorNum
}sensorState_t;

extern bool sensorState[sensorNum];
extern bool moterState[moterNum];
extern char version[3][4];

extern struct DataPool {
    float temperature;       /* Filtered primary temperature from ntcTemp */
    uint32_t ntc1Raw;       /* NTC1 raw ADC value from snapshot 0x0000-0x0001 */
    uint32_t ntc2Raw;       /* NTC2 raw ADC value (not converted) */
    uint32_t ntc2FilteredRaw; /* NTC2 ADC accepted for temperature lookup */
    uint32_t ntc3Raw;       /* NTC3 raw ADC value (not converted) */
    uint32_t ntcBackRaw;    /* NTCBack raw ADC value (not converted) */
    int   speed;
    float current;
    float turns;
    float voltage;
    uint16_t statusWord;
    uint16_t faultCode;
    uint16_t actuatorCoilBitmap;
    uint16_t actuatorDiscreteBitmapHigh;
    uint16_t waterPumpCommand;
    uint16_t hosePumpCommand;
    int16_t pressMotorCommand;
    int16_t riseMotorCommand;
    uint8_t legacyQueueDepth;
    uint8_t modbusQueueDepth;
    uint32_t modbusQueueOverflowCount;
    uint32_t modbusCrcErrorCount;
    uint32_t modbusFrameErrorCount;
    uint32_t legacyQueueOverflowCount;
    uint8_t tmuxD0Bitmap;
    uint8_t tmuxD1Bitmap;
    uint16_t tmuxAdcRaw[8];
    uint16_t currentAdcRaw;
    uint16_t powerMotorRpm;
    uint16_t powerMotorState;
    uint16_t powerMotorPhaseCurrentMa;
    bool powerEnabled;
    bool powerHeater1Enabled;
    bool powerHeater2Enabled;
    uint16_t powerHeater1Gear;
    uint16_t powerHeater2Gear;
    int16_t powerMotorGear;
    uint16_t powerPc2AdcMedian; /* Station 3 PC2/ADC2_IN8 median, input 0x000E. */
    uint32_t powerMeterVoltageMv;
    uint32_t powerMeterCurrentMa;
    uint32_t powerMeterActivePowerMw;
    bool powerMeterSampleValid;
    uint32_t powerMeterFrequencyMilliHz;
    uint16_t powerCycleThreshold;
    uint16_t powerCycleTotal;
    uint16_t powerCalibrationStatus;

    /* Station 2 V2 appended region (0x0028~0x004F). Standard-only slaves
     * leave these zero; V2 main board reports dual rise motor currents,
     * encoder positions, multi-channel currents and extra NTC temperatures. */
    float busAmpere;
    float ledAmpere;
    float brakeAmpere;
    float strainGageAmpere;
    float riseRightCurrentB;
    float riseRightCurrentA;
    float riseLeftCurrentB;
    float riseLeftCurrentA;
    uint16_t riseCurrentMagnitude;
    uint16_t pressCurrentMagnitude;
    uint16_t resetKeyLevel;
    int32_t riseLeftEncoderPos;
    int32_t riseRightEncoderPos;
    int32_t pressRightEncoderPos;
    uint32_t waterFlowPulses;
    uint32_t brineFlowPulses;
    uint16_t windingAZero;
    uint16_t windingBZero;
    uint16_t adcReady;
    uint16_t adcSampleCount;
} g_DataPool;

void DataCenterTask(void *pvParameters);

/* Last accepted station-3 targets, independent of live feedback. Read-only
 * outside DataCenter; signed motor gear preserves direction. */
extern volatile uint16_t g_TargetHeater1Gear;
extern volatile uint16_t g_TargetHeater2Gear;
extern volatile int16_t g_TargetPowerMotorGear;
void PowerRecoveryTask(void *pvParameters);

/* Queue station 3's full self-calibration sequence. The result/status is
 * refreshed by the normal station 3 polling task. */
/* Queue the station 3 total-enable write followed by the calibration trigger. */
bool setPowerCalibration(void);
uint16_t getPowerCalibrationStatus(void);
/* timeout_ms is the maximum gap without a fresh status=1 poll; each such
 * response renews the wait. Callers currently use 60 seconds. */
bool runPowerCalibration(uint32_t timeout_ms);

/* Execute one complete Modbus RTU frame on slave 2/3's dedicated UART.
 * This call blocks until DataCenterTask returns a response. */
bool ModbusGatewayTransaction(const uint8_t *request, size_t request_length,
                              uint8_t *response, size_t response_capacity,
                              size_t *response_length);

/* Copy input registers from the latest background-poll snapshot. This reads
 * ESP32 RAM only and never sends a request to either downstream UART. */
bool ModbusCopyCachedInputRegisters(uint8_t slave, uint16_t address,
                                    uint16_t quantity, uint16_t *registers);

/* True once the background poll has read slave 2 (AT32) at least once.
 * Process flows use this as an online gate: while it is false, every slave-2
 * command is dropped and every slave-2 derived value (temperature, encoders,
 * limit bits) is stale. */
bool ModbusActuatorOnline(void);

/* True once the background poll has read slave 3 (power board / ST32) at
 * least once. Same semantics as ModbusActuatorOnline(), used by the OTA IAP
 * flow to gate ST32 version verification on a fresh register snapshot. */
bool ModbusPowerOnline(void);

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

inline uint32_t getNtc1Raw(void){
    return g_DataPool.ntc1Raw;
}

inline uint32_t getNtc2Raw(void){
    return g_DataPool.ntc2Raw;
}

inline uint32_t getNtc3Raw(void){
    return g_DataPool.ntc3Raw;
}

inline uint32_t getNtcBackRaw(void){
    return g_DataPool.ntcBackRaw;
}

/* 最近5秒内至少3次新鲜主温度 >=98℃，每次结算至少间隔1秒。
 * 不要求连续命中；缺失采样不补算，过期命中自动失效。 */
bool isTemperatureOver98Confirmed(void);

// 水泵控制函数
void waterPumpOn(uint16_t waterPumpSpeed);
void waterPumpOff(void);

// 点卤泵控制函数
void brinePumpOn(void);
void brinePumpOff(void);

// 蠕动泵控制函数
void hosePumpOn(uint8_t hosePumpSpeed);
void hosePumpSetSignedSpeed(int16_t signedSpeed);
void hosePumpOff(void);

// 压制电机控制函数
void pressMoterDown(uint16_t pressMoterSpeed);
void pressMoterUp  (uint16_t pressMoterSpeed);
void pressMoterStop(void);

// 抬升电机控制函数
void riseMoterDown(uint16_t riseMoterSpeed);
void riseMoterUp(uint16_t riseMoterSpeed);
void riseMoterStop(void);
void riseMoterTimedDownAndHold(void);
void riseMoterTimedUpAndHold(void);
/* 抬升电流档：0=低档（普通上/下行、上位机手动点动），非 0=高档（压制流程带载）。 */
void riseMoterSetCurrentGear(uint8_t loadedGear);

// WS2812 LED控制函数
void ws2812Coler(uint8_t lednum, uint8_t rColer, uint8_t gColer, uint8_t bColer);

// 高压板控制函数
void setHighBroad(uint8_t highBroadValue);
void setPowerEnabled(bool enabled);
void setPowerMotor(uint16_t gear, uint8_t direction);
bool DataCenterSetPowerMotorDirectionMode(uint8_t mode);
uint8_t DataCenterGetPowerMotorDirectionMode(void);
void setPowerHeater1(uint16_t gear);
void setPowerHeater2(uint16_t gear);

/* Accepted nonzero heater commands (API and gateway), not feedback samples. */
uint32_t DataCenterHeatingCommandSequence(void);
/* Queue both protection stops only if no newer heating request has arrived.
 * Stops are also checked again at execution, so stale stops cannot override
 * a later restart. False means queue full or sequence changed; caller retries. */
bool DataCenterStopHeatersIfUnchanged(uint32_t sequence);

// 阀门控制函数
void valveOn(void);
void valveOff(void);

// LED灯控制函数
void ledOn(void);
void ledOff(void);

char (*getVersion(void))[4];

void braineAct(void);

/* Modbus acquisition test mode. When active, DataCenterTask stops
 * automatic polling and reads the selected slave's input registers
 * as fast as possible, counting success/failure outcomes. */
typedef struct {
    volatile bool active;
    volatile bool aggressive;  /* short timeouts for fast failure on dead slaves */
    volatile uint8_t slave;
    volatile uint64_t attempts;  /* uint64: survives ~584000 years at 1 MHz */
    volatile uint64_t ok;
    volatile uint64_t timeout;
    volatile uint64_t crc;
    volatile uint64_t invalid;
    volatile uint64_t total_us;  /* uint64: no wrap at 1e7 iterations x 2ms */
    volatile uint32_t min_us;    /* single-iteration, uint32 is enough */
    volatile uint32_t max_us;
} modbus_test_stats_t;

extern modbus_test_stats_t g_ModbusTestStats;

/* Start acquisition test on the given slave. When aggressive is true,
 * short read/byte timeouts are applied so a dead slave fails fast and
 * the loop achieves higher throughput at the cost of misclassifying
 * slow-but-alive slaves as timeouts. */
void ModbusTestStart(uint8_t slave, bool aggressive);
void ModbusTestStop(void);
void ModbusTestPrintStats(void);

/* Schedule an automatic pressure test from DataCenterTask boot. The task
 * waits delay_ms after startup, then calls ModbusTestStart(slave, aggressive).
 * slave=0 or delay_ms=0 cancels any pending schedule. This is the runtime
 * hook used by main.c's MODBUS_TEST_DEFAULT_* macros. */
void DataCenterScheduleDefaultTest(uint8_t slave, bool aggressive,
                                   uint32_t delay_ms);

#endif
