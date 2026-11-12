#include "UsartExchange.h"
#include "usart.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "DataCenter.h"
#include "ModbusMap.h"
#include "esp_log.h"
#include "SystemMoniter.h"
// 动力板 Modbus 控制。电机方向：1=正转，0=反转。
void SpeedHeatControl(SpeedHeatControl_t SpeedHeatControl,int gear,int dir)
{
    switch(SpeedHeatControl)
    {
        case HEATDOUBLE:
            setPowerHeater1((uint16_t)gear);
            setPowerHeater2((uint16_t)gear);
            break;
        case NOBUSHMOTER:
            setPowerMotor((uint16_t)gear, (uint8_t)(dir != 0));
            break;
        case HIGHHEAT1://下面加热
            setPowerHeater1((uint16_t)gear);
            break;
        case HIGHHEAT2://上面加热
            setPowerHeater2((uint16_t)gear);
            break;
        default:
            break;
    }
}
//无刷电机控制函数，正转为正数，反转为负数，0为停止
void NoBushRun(int gear)
{
    uint8_t dir = gear > 0 ? 1U : 0U;
    unsigned magnitude;

    ESP_LOGI("Usart", "NoBush: %d", gear);

    if (gear >= 15) {
        magnitude = 15U;
    } else if (gear <= -15) {
        magnitude = 15U;
    } else {
        magnitude = (unsigned)(gear < 0 ? -gear : gear);
    }
    SpeedHeatControl(NOBUSHMOTER, (int)magnitude, dir);
}
// 加热档位：0停止，1～30运行；越界值在此处收敛到协议范围。
void Heat1Run(int gear)
{
    gear = gear < 0 ? 0 : (gear > (int)MODBUS_POWER_HEATER_GEAR_MAX ? (int)MODBUS_POWER_HEATER_GEAR_MAX : gear);
    SpeedHeatControl(HIGHHEAT1,gear,0);
    setHeat1Power(gear);
    ESP_LOGI("Usart", "Heat1Run: %d", gear);
}

void Heat2Run(int gear)
{
    gear = gear < 0 ? 0 : (gear > (int)MODBUS_POWER_HEATER_GEAR_MAX ? (int)MODBUS_POWER_HEATER_GEAR_MAX : gear);
    setHeat2Power(gear);
    SpeedHeatControl(HIGHHEAT2,gear,0);
    ESP_LOGI("Usart", "Heat2Run: %d", gear);
}
