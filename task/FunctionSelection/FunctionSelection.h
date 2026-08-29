#ifndef __FUNCTIONSELECTION_H
#define __FUNCTIONSELECTION_H

#include <stdbool.h>
#include "SoyMilkMake.h"

#include "freertos/event_groups.h"

extern EventGroupHandle_t FuncTypeGroup;
extern EventGroupHandle_t FuncOrderGroup;

#define Function_ReservationBit (1<<0) // 预约功能标志位



//豆浆制作任务事件位定义
#define SoyMilkMake_StartBit    (1<<0)//任务开始标志位(包含手动停止后的启动和上电启动，提前终止后的启动)
#define SoyMilkMake_PauseBit    (1<<1)//任务暂停标志位
#define SoyMilkMake_ResumeBit   (1<<2)//任务恢复标志位
#define SoyMilkMake_EndBit      (1<<3)//任务结束标志位
#define SoyMilkMake_MassConcBit (1<<4)//豆浆浓淡选择标志位
#define SoyMilkMake_CleanBit    (1<<5)//豆浆自清洁标志位
#define SoyMilkMake_KeepWarmBit (1<<6)//豆浆保温标志位 
#define SoyMilkMake_DecTempBit  (1<<7)//豆浆快速降温标志位
//压制功能任务事件位定义
#define Press_StartBit          (1<<0)//压制任务开始标志位
#define Press_PauseBit          (1<<1)//压制任务暂停标志位
#define Press_ResumeBit         (1<<2)//压制任务恢复标志位
#define Press_EndBit            (1<<3)//压制任务结束标志位
//植物奶功能任务事件位定义
#define PlantMilk_StartBit      (1<<0)//植物奶任务开始标志位
#define PlantMilk_PauseBit      (1<<1)//植物奶任务暂停标志位
#define PlantMilk_ResumeBit     (1<<2)//植物奶任务恢复标志位
#define PlantMilk_EndBit        (1<<3)//植物奶任务结束标志位
#define PlantMilk_HeatBit       (1<<4)//植物奶加热标志位
#define PlantMilk_CleanBit      (1<<5)//植物奶自清洁标志位
//自清洁功能任务事件位定义
#define SelfClean_StartBit      (1<<0)//自清洁任务开始标志位
#define SelfClean_EndBit        (1<<1)//自清洁任务结束标志位
#define SelfClean_PauseBit      (1<<2)//自清洁任务暂停标志位
#define SelfClean_ResumeBit     (1<<3)//自清洁任务恢复标志位
#define SelfClean_ModeBit       (1<<4)//自清洁模式选择标志位    

//功能种类
#define SoyMilk_bit             (1<<0)//豆浆  1
#define PRESS_bit               (1<<1)//压制  2
#define Milk_bit                (1<<2)//植物奶4
#define TouFu_bit               (1<<3)//豆腐  8
#define SelfClean               (1<<4)//自清洁16
#define TouFuBrain              (1<<5)//豆腐脑32

//功能控制
#define FuncStart_bit           (1<<0)//开始1
#define FuncPause_bit           (1<<1)//暂停2
#define FuncResume_bit          (1<<2)//恢复4
#define FuncStop_bit            (1<<3)//停止8


typedef struct{
    float CurrentValue;
    int TargetValue;
    int CurrentNum;
    int TargetNum;
    bool isRunning;//用于判断区分是否运行后，开始任务，还是初次开启
    bool isEnd;
}TaskMoniterHandle_t;

typedef enum {
    SOYMILK_SEC=0,          //功能：豆浆
    PRESSTOUFU_SEC,         //压制
    TOUFU_SEC,              //豆腐
    MILK_SEC,               //植物奶
    SELFCLEAN_SEC,          //自清洗
    TOUFUBRAIN_SEC,         //豆腐脑
    NOTASK_SEC,             //无任务
    FUNCSEC_NUM             //功能数量
}FuncSecType_t;

typedef enum{
    FUNCSEC_START,
    FUNCSEC_PAUSE,
    FUNCSEC_RESUME,
    FUNCSEC_STOP,
    FUNCSECORDER_NUM
}FuncSecOrder_t;


void FuncMoniterTask(void *pvParameters);
FuncSecType_t GetCurTaskState(void);


#endif
