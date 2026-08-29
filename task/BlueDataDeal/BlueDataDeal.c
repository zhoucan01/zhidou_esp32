#include "BlueDataDeal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "DataCenter.h"
#include "FunctionSelection.h"
#include "UsartExchange.h"

static const char *TAG = "BlueDataDeal";

extern EventGroupHandle_t FuncTypeGroup;
extern EventGroupHandle_t FuncOrderGroup;

typedef struct {
    int P1BLE;
    int P2BLE;
    int P3BLE;
    int P4BLE;
} BlePar_t;

typedef void (*BleCont)(BlePar_t *);

#define WRAP_0(F) static void wrapper_##F(BlePar_t *a) { (void)a; F(); }
#define WRAP_1(F) static void wrapper_##F(BlePar_t *a) { F(a ? a->P1BLE : 0); }
#define WRAP_4(F) static void wrapper_##F(BlePar_t *a) { \
    if (a) F(a->P1BLE, a->P2BLE, a->P3BLE, a->P4BLE); \
}

typedef enum {
    WATERPUMP_OFF = 1,
    WATERPUMP_ON,
    MILKPUMP_ON,
    MILKPUMP_OFF,
    PONDERPUMP_ON,
    PONDERPUMP_OFF,
    VALVE_ON,
    VALVE_OFF,
    LED_ON,
    LED_OFF,

    PRESS_MOTOR_DOWN = 15,
    PRESS_MOTOR_UP,
    NOBUSHRUN_ONEVAL,
    HEAT1RUN_ONEVAL,
    HEAT2RUN_ONEVAL,
    PRESS_MOTOR_STOP,
    RISE_MOTOR_DOWN,
    RISE_MOTOR_UP,
    RISE_MOTOR_STOP,
    WS2812_COLOR,
    braine_Act,
    
    IOCONTROL_NUM = 80,
    SOYMILK_BLE,
    PRESSTOUFU_BLE,
    TOUFU_BLE,
    MILK_BLE,
    SELFCLEAN_BLE,
    TOUFUBRAIN_BLE,
    FUNCCONTROL_NUM = 90,
} IoControlCmd_t;

WRAP_0(waterPumpOff)
WRAP_1(waterPumpOn)
WRAP_1(hosePumpOn)
WRAP_0(hosePumpOff)
WRAP_0(brinePumpOn)
WRAP_0(brinePumpOff)
WRAP_0(valveOn)
WRAP_0(valveOff)
WRAP_0(ledOn)
WRAP_0(ledOff)
WRAP_1(pressMoterDown)
WRAP_1(pressMoterUp)
WRAP_1(NoBushRun)
WRAP_1(Heat1Run)
WRAP_1(Heat2Run)
WRAP_0(pressMoterStop)
WRAP_1(riseMoterDown)
WRAP_1(riseMoterUp)
WRAP_0(riseMoterStop)
WRAP_4(ws2812Coler)
WRAP_0(braineAct)

static BleCont ActionTable[IOCONTROL_NUM + 1] = {
    [WATERPUMP_OFF] = wrapper_waterPumpOff,
    [WATERPUMP_ON] = wrapper_waterPumpOn,
    [MILKPUMP_ON] = wrapper_hosePumpOn,
    [MILKPUMP_OFF] = wrapper_hosePumpOff,
    [PONDERPUMP_ON] = wrapper_brinePumpOn,
    [PONDERPUMP_OFF] = wrapper_brinePumpOff,
    [VALVE_ON] = wrapper_valveOn,
    [VALVE_OFF] = wrapper_valveOff,
    [LED_ON] = wrapper_ledOn,
    [LED_OFF] = wrapper_ledOff,
    [PRESS_MOTOR_DOWN] = wrapper_pressMoterDown,
    [PRESS_MOTOR_UP] = wrapper_pressMoterUp,
    [NOBUSHRUN_ONEVAL] = wrapper_NoBushRun,
    [HEAT1RUN_ONEVAL] = wrapper_Heat1Run,
    [HEAT2RUN_ONEVAL] = wrapper_Heat2Run,
    [PRESS_MOTOR_STOP] = wrapper_pressMoterStop,
    [RISE_MOTOR_DOWN] = wrapper_riseMoterDown,
    [RISE_MOTOR_UP] = wrapper_riseMoterUp,
    [RISE_MOTOR_STOP] = wrapper_riseMoterStop,
    [WS2812_COLOR] = wrapper_ws2812Coler,
    [braine_Act] = wrapper_braineAct,
};

QueueHandle_t BlueDataQueue = NULL;

static void DataPreDeal(const uint8_t *PreData, size_t DataLen, int *DealData)
{
    for (size_t i = 0; i < 7; ++i) {
        DealData[i] = i < DataLen ? PreData[i] : 0;
    }

    if (PreData[0] == NOBUSHRUN_ONEVAL) {
        DealData[1] -= 15;
    }
}

void BlueDataDealTask(void *pvParameters)
{
    (void)pvParameters;
    BlueDataMessage_t message;
    int DealData[20] = {0};
    BlePar_t BlePar = {0};

    BlueDataQueue = xQueueCreate(5, sizeof(message));
    if (BlueDataQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create Bluetooth data queue");
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        if (xQueueReceive(BlueDataQueue, &message, portMAX_DELAY) == pdTRUE) {
            if (message.length == 0 || message.length > BLUE_DATA_MAX_LEN) {
                ESP_LOGW(TAG, "Invalid Bluetooth message length: %u",
                         (unsigned)message.length);
                continue;
            }
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, message.data, message.length,
                                     ESP_LOG_INFO);

            uint8_t cmdIndex = message.data[0];
            if (cmdIndex >= FUNCCONTROL_NUM) {
                ESP_LOGE(TAG, "Invalid index: 0x%02x, max: 0x%02x",
                         cmdIndex, FUNCCONTROL_NUM - 1);
                continue;
            }

            if (cmdIndex > IOCONTROL_NUM) {
                if (cmdIndex > TOUFUBRAIN_BLE || message.length < 2) {
                    ESP_LOGE(TAG, "Invalid function command: 0x%02x", cmdIndex);
                    continue;
                }

                uint8_t funcIdx = cmdIndex - IOCONTROL_NUM - 1;
                uint8_t orderBit = message.data[1];
                if (orderBit != FuncStart_bit && orderBit != FuncPause_bit &&
                    orderBit != FuncResume_bit && orderBit != FuncStop_bit) {
                    ESP_LOGE(TAG, "Invalid function order: 0x%02x", orderBit);
                    continue;
                }
                if (FuncTypeGroup == NULL || FuncOrderGroup == NULL) {
                    ESP_LOGW(TAG, "Function dispatcher is not ready");
                    continue;
                }

                xEventGroupSetBits(FuncTypeGroup, BIT(funcIdx));
                xEventGroupSetBits(FuncOrderGroup, orderBit);
                ESP_LOGI(TAG, "Function: index=%u order=0x%02x",
                         funcIdx, orderBit);
                continue;
            }

            if (ActionTable[cmdIndex] != NULL) {
                DataPreDeal(message.data, message.length, DealData);
                BlePar.P1BLE = DealData[1];
                BlePar.P2BLE = DealData[2];
                BlePar.P3BLE = DealData[3];
                BlePar.P4BLE = DealData[4];
                ActionTable[cmdIndex](&BlePar);
            } else {
                ESP_LOGW(TAG, "Unsupported I/O command: 0x%02x", cmdIndex);
            }
        }
    }
}

// #include "BlueDataDeal.h"
// #include "Bluetooch.h"

// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "freertos/event_groups.h"
// #include "freertos/queue.h"

// #include "esp_log.h"
// #include "sys.h"

// #include "UsartExchange.h"
// #include "DataCenter.h"

// static const char *TAG = "BlueDataDeal";

// extern EventGroupHandle_t FuncTypeGroup;
// extern EventGroupHandle_t FuncOrderGroup;
// void DataPreDeal(char* PreData,int* DealDate);


// typedef struct{
//     int P1BLE;
//     int P2BLE; 
//     int P3BLE;
// }BlePar_t;

// #define WRAP_0(F) static void wrapper_##F(BlePar_t *a) { F(); }
// #define WRAP_1(F) static void wrapper_##F(BlePar_t *a) { if(a) F(a->P1BLE); else F(0); }
// #define WRAP_2(F) static void wrapper_##F(BlePar_t *a) { if(a) F(a->P1BLE, a->P2BLE); else F(0, 0); }
// #define WRAP_3(F) static void wrapper_##F(BlePar_t *a) { if(a) F(a->P1BLE, a->P2BLE,a->P3BLE); else F(0, 0,0); }
// //#define WRAP_4(F) static void wrapper_##F(BlePar_t *a) {F(param->para[0],param->para[1],param->para[2],param->para[3]); \}
// //IOControl功能数组0-14对应无参函数 15-45对应一个参数函数 46-65对应两个参数函数 66-80对应三个参数
// //FuncControl功能数组0-20开启对应的功能，功能里会有自己的参数数组，这个仅作索引
// typedef enum {
//     //无参函数
//     // MOTOR1_STOP=1,          //电机1停止            Motor1Off();
//     // MOTOR1_CLOCK,           //电机1正转            Motor1clockwise();
//     // MOTOR1_ANTICLOCK,       //电机1反转            Motor1Clockcounterclockwise();
//     WATERPUMP_OFF=1,           //清水泵开             waterPumpOn()
//     WATERPUMP_ON,          //清水泵关             waterPumpOff()
//     MilkPump_On,            //抽豆浆泵开           MilkPumpOn()
//     MilkPump_Off,           //抽豆浆泵关           hosePumpOff()
//     PonderPump_On,          //抽点卤剂泵开         PonderPumpOn()
//     PonderPump_Off,         //抽点卤剂泵关         brinePumpOff()
//     Valve_On,               //出浆阀开              ValveOn()
//     Valve_Off,              //出浆阀关              ValveOff()
//     //一个参数函数
//     PUSHMOTER_ONEVAL=15,    //一个变量，上下停切换  PushMoterSimpleControl(StepMotorMove_t MotorMove);15
//     PRESSMOTOR_ONEVAL,      //一个变量，上下停切换  PressMoterSimpleControl(StepMotorMove_t MotorMove);16
//     NOBUSHRUN_ONEVAL,       //一个变量，档位切换    NoBushRun(int gear)
//     HEAT1RUN_ONEVAL,        //一个变量，档位切换    Heat1Run(int gear)
//     HEAT2RUN_ONEVAL,        //一个变量，档位切换    Heat2Run(int gear)
//     //两个参数函数
//     MUSIC_TWOVAL=45,        //两个参数，音乐选择，开关切换
    

//     //三个参数函数
//     STEPMOTERRUNTURN_THREEVAL=66,   //步进电机转动，三个参数：电机选择，转数，速度 void Motor_Run_Logic(StepMotorType_t type, int turns, uint32_t speed);
//     HEATMOTER_THREEEVAL,            //加热破壁控制，三个参数，设备选择，开关，档位 SpeedHeatControl(SpeedHeatControl_t SpeedHeatControl,bool SpeedHeatState,int gear)
//     IOCONTROL_NUM=80,//外设控制总数
//     //功能索引
//     SOYMILK_BLE,            //功能：豆浆
//     PRESSTOUFU_BLE,         //压制
//     TOUFU_BLE,              //豆腐
//     MILK_BLE,               //植物奶
//     SELFCLEAN_BLE,          //自清洗
//     TOUFUBRAIN_BLE,         //豆腐脑
//     FUNCCONTROL_NUM=90,        //功能数量
// }IoControlCmd_t;



// typedef enum{
//     FUNC_START,
//     FUNC_PAUSE,
//     FUNC_RESUME,
//     FUNC_STOP,
//     FUNCORDER_NUM
// }FuncOrderCmd_t;



// //定义函数类型的结构体
// typedef void (*BleCont)(BlePar_t *);


// //外设实例化
// // WRAP_0(Motor1Off)
// // WRAP_0(Motor1clockwise)
// // WRAP_0(Motor1Clockcounterclockwise)

// WRAP_0(waterPumpOff)

// WRAP_0(hosePumpOff)
// WRAP_0(brinePumpOn)
// WRAP_0(brinePumpOff)
// WRAP_0(valueOn)
// WRAP_0(valueOff)
// WRAP_0(ledOn)
// WRAP_0(ledOff)
// WRAP_0(ledOn)
// WRAP_0(ledOff)

// WRAP_1(hosePumpOn)
// WRAP_1(waterPumpOn)
// WRAP_1(PushMoterSimpleControl)
// WRAP_1(PressMoterSimpleControl)

// WRAP_1(NoBushRun)
// WRAP_1(Heat1Run)
// WRAP_1(Heat2Run)


// // WRAP_3(SpeedHeatControl)

// //功能实例化



// //表分配
// BleCont ActionTable[IOCONTROL_NUM]={
//     // [MOTOR1_STOP]=wrapper_Motor1Off,
//     // [MOTOR1_CLOCK] = wrapper_Motor1clockwise,
//     // [MOTOR1_ANTICLOCK] = wrapper_Motor1Clockcounterclockwise,
//     [WATERPUMP_ON]=wrapper_brinePumpOff,
//     [WATERPUMP_OFF]=wrapper_PonderPumpOn,
//     [MilkPump_On]=wrapper_waterPumpOn,
//     [MilkPump_Off]=wrapper_waterPumpOff,
//     [PonderPump_On]=wrapper_MilkPumpOn,
//     [PonderPump_Off]=wrapper_hosePumpOff,
//     [Valve_On]=wrapper_valveOn,
//     [Valve_Off]=wrapper_valveOff,

//     [PUSHMOTER_ONEVAL]=wrapper_PressMoterSimpleControl,
//     [PRESSMOTOR_ONEVAL]=wrapper_PushMoterSimpleControl,
//     [NOBUSHRUN_ONEVAL]=wrapper_NoBushRun,
//     [HEAT1RUN_ONEVAL]=wrapper_Heat1Run,
//     [HEAT2RUN_ONEVAL]=wrapper_Heat2Run,

    
//    // [HEATMOTER_THREEEVAL]=wrapper_SpeedHeatControl
// };

// //第一位检查功能和外设 0-80给外设 81以后给功能 
// //第二位参数细化 功能分别别是时间组的不同位
// QueueHandle_t BlueDataQueue;
// static char BlueData[20];
// void BlueDataDealTask(void *pvParameters)
// {
//     BlueDataQueue = xQueueCreate(5,sizeof(BlueData));
//     BaseType_t QueueState;
//     BlePar_t BlePar;
//     int DealDate[20];
//     ValveTimerInit();
//     while(1)
//     {
//         // 1. 接收数据并检查是否成功
//         QueueState = xQueueReceive(BlueDataQueue, BlueData, portMAX_DELAY);
//         if (QueueState == false) {
//             continue;
//         }
//         ESP_LOG_BUFFER_HEX_LEVEL(TAG, BlueData, 4, ESP_LOG_INFO);
        
//         // 2. 索引范围检查
//         uint8_t cmdIndex = BlueData[0];
        
//         if (cmdIndex >= FUNCCONTROL_NUM) {
//             ESP_LOGE(TAG, "Invalid index: %x,max:%x", cmdIndex, FUNCCONTROL_NUM);
//             continue; 
//         }
        
//         // 功能索引 (81-89)
//         if ((cmdIndex < FUNCCONTROL_NUM) && (cmdIndex > IOCONTROL_NUM)) {
//             uint8_t funcIdx = BlueData[0] - IOCONTROL_NUM - 1;  // 0x51→0, 0x52→1, ...
//             uint8_t orderBit = BlueData[1];  // 1,2,4,8 (位掩码格式)
            
//             // 将位掩码转换成索引: 1→0, 2→1, 4→2, 8→3
//             uint8_t orderIdx;
//             if (orderBit == 1) orderIdx = 0;       // START
//             else if (orderBit == 2) orderIdx = 1;  // PAUSE
//             else if (orderBit == 4) orderIdx = 2;  // RESUME
//             else if (orderBit == 8) orderIdx = 3;  // STOP
//             else orderIdx = orderBit;              // 保底
            
//             // 使用位移操作设置正确的位
//             xEventGroupSetBits(FuncTypeGroup, 1 << funcIdx);
//             xEventGroupSetBits(FuncOrderGroup, 1 << orderIdx);
            
//             ESP_LOGI("功能", "功能: funcIdx=%d, orderBit=0x%02X → orderIdx=%d", 
//                     funcIdx, orderBit, orderIdx);
//             continue;
//         }
        
//         // 外设索引 (0-80)
//         if (ActionTable[cmdIndex] != NULL) {
//             DataPreDeal(BlueData, DealDate);
//             BlePar.P1BLE = DealDate[1];
//             BlePar.P2BLE = DealDate[2];
//             BlePar.P3BLE = DealDate[3];
                
//             ActionTable[cmdIndex](&BlePar);
//         }
//     }
// }

// void DataPreDeal(char* PreData,int* DealDate)
// {

//     DealDate[0] = PreData[0];
//     DealDate[1] = PreData[1];
//     DealDate[2] = PreData[2];
//     DealDate[3] = PreData[3];
//     DealDate[4] = PreData[4];
//     DealDate[5] = PreData[5];
//     DealDate[6] = PreData[6];
//     if(PreData[0]==0x42)
//     {
//         if(PreData[1]==1)
//         {
//             PreData[1]=2;
//         }
//         if(PreData[1]==2)
//         {
//             PreData[1]=1;
//         }
//         if(PreData[2]<=0x13)
//         {
//             DealDate[2]= (int)(PreData[2]+1)*(-1);
//         }
//        else if(PreData[2]>0x13)
//        {
//             DealDate[2]= (int)(PreData[2]-19);
//        }
//         switch(PreData[3])
//         {
//             case 0x00:  DealDate[3]=5000;break;
//             case 0x01:  DealDate[3]=100;break;
//             case 0x02:  DealDate[3]=200;break;
//             case 0x03:  DealDate[3]=500;break;
//             case 0x04:  DealDate[3]=1000;break;
//             case 0x05:  DealDate[3]=1500;break;
//             case 0x06:  DealDate[3]=2000;break;
//             case 0x07:  DealDate[3]=5000;break;
//         }
//     }
//     if(PreData[0]==0x11)
//     {
//         DealDate[1]=DealDate[1]-15;
//     }
//   //  ESP_LOGI("predate","OVER");
// }
