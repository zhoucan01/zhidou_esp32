#include "UsartExchange.h"
#include "usart.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "DataCenter.h"
#include "esp_log.h"
#include "SystemMoniter.h"
//高压板通信协议 第1位总开关 第2，3位选择 第4-8位档位 2,3位 00两个加热 01下面加热 10上面加热 11电机

//串口数据编辑函数
void SpeedHeatControl(SpeedHeatControl_t SpeedHeatControl,int gear,int dir)
{
    uint8_t HighControlByte = 0x00;

    HighControlByte |= 1<<7;

    switch(SpeedHeatControl)
    {
        case HEATDOUBLE:
            // HighControlByte |= 0<<6;
            // HighControlByte |= 0<<5;
            break;
        case NOBUSHMOTER:
            HighControlByte |= 1<<6;
            HighControlByte |= 1<<5;
            HighControlByte |= dir<<4;
            break;
        case HIGHHEAT1://下面加热
            HighControlByte |= 1<<5;
            break;
        case HIGHHEAT2://上面加热
            HighControlByte |= 1<<6;
            break;
        default:
            break;
    }
    HighControlByte |= gear;
    ESP_LOGI("Usart","HighControlByte:%X",HighControlByte);
    setHighBroad(HighControlByte);
}
//无刷电机控制函数，正转为正数，反转为负数，0为停止
void NoBushRun(int gear)
{
    uint8_t dir=0;
    ESP_LOGI("Usart","NoBush：%d",gear);
    if(gear>=0)//正转
    {
        dir=1;
    }
    if(gear<0)//反转
    {
        dir=0;
        gear=-gear;
    }
    SpeedHeatControl(NOBUSHMOTER,gear,dir);
}
//加热控制函数，gear为档位，0为停止，1-31为档位，目前最大13档
void Heat1Run(int gear)
{
    //ESP_LOGI("Usart","Heat1");
    SpeedHeatControl(HIGHHEAT1,gear,0);
    setHeat1Power(gear);
    ESP_LOGI("Usart","Heat1Run%d",gear);
}
//加热控制函数，gear为档位，0为停止，1-31为档位，目前最大13档
void Heat2Run(int gear)
{
  //  ESP_LOGI("Usart","Heat2");
    setHeat2Power(gear);
    SpeedHeatControl(HIGHHEAT2,gear,0);
    ESP_LOGI("Usart","Heat2Run%d",gear);
}