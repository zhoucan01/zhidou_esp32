#include "SoyMilkMake.h"
#include "ToufuMake.h"
#include "Milk.h"
#include "SelfClean.h"
#include "brine.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "FunctionSelection.h"
#include "strings.h"
#include <string.h>

#include "esp_log.h"

#include "press.h"

static volatile FuncSecType_t CurrentFunc = NOTASK_SEC;
static volatile FuncSecOrder_t CurrentOrder = FUNCSEC_STOP;

extern TaskMoniterHandle_t SoyMilkMakeMoniter;
extern TaskMoniterHandle_t TouFuMoniter;

// 外部任务句柄（确保在某处已定义为 NULL）
extern TaskHandle_t SoyMilkMakeHandle;
extern TaskHandle_t TouFuHandle;
extern TaskHandle_t MilkHandle;
extern TaskHandle_t SelfCleanHandle;
extern TaskHandle_t PressHandle;
extern TaskHandle_t brineHandle;
// 事件组句柄
EventGroupHandle_t FunctionSelectionEventGroup;
EventGroupHandle_t TouFuEventGroup;
EventGroupHandle_t SoyMilkEventGroup;
EventGroupHandle_t PressEventGroup;
EventGroupHandle_t PlantMilkEventGroup;
EventGroupHandle_t SelfCleanEventGroup;
EventGroupHandle_t TouFuEventGroup;

EventGroupHandle_t FuncTypeGroup;
EventGroupHandle_t FuncOrderGroup;

FuncSecType_t  FuncSecType;
FuncSecOrder_t FuncSecOrder;

typedef void (*Funcvoid)(void *);

typedef struct{
    Funcvoid               FuncSecP1;
    char                   *FuncSecP2;
    configSTACK_DEPTH_TYPE FuncSecP3;
    void                   *FuncSecP4;
    UBaseType_t            FuncSecP5;
    TaskHandle_t           *FuncSecP6;
}FuncSecNum_t;

typedef void (*FuncPrt)(FuncSecNum_t *);

#define FunWRAP_0(Func) static void wrapper_##Func(void *a) { (void)a; Func(); }
#define FunWRAP_1(Func) static void wrapper_##Func(FuncSecNum_t *a) { if(a && a->FuncSecP6 && *a->FuncSecP6) {Func(*a->FuncSecP6);} }

#define NUM_FUNC_TYPES 6   // 对应 Typebits 的最大位数
#define NUM_ORDER_TYPES 4  // 对应 Orderbits 的最大位数

FunWRAP_0(SoyMilkPause)
FunWRAP_0(SoyMilkResume)
FunWRAP_0(SoyMilkEnd)

FunWRAP_0(TouFuPause)
FunWRAP_0(TouFuResume)
FunWRAP_0(TouFuEnd)

FunWRAP_0(pressPause)
FunWRAP_0(pressResume)
FunWRAP_0(pressEnd)

FunWRAP_0(PlantMilkPause)
FunWRAP_0(PlantMilkResume)
FunWRAP_0(PlantMilkEnd)

FunWRAP_0(SelfCleanPause)
FunWRAP_0(SelfCleanResume)
FunWRAP_0(SelfCleanEnd)

FunWRAP_0(brinePause)
FunWRAP_0(brineResume)
FunWRAP_0(brineEnd)

FunWRAP_1(vTaskSuspend)
FunWRAP_1(vTaskResume)
/* vTaskDelete() does not clear the caller's saved task handle. Clear it
 * after stopping so a repeated stop cannot use a stale FreeRTOS handle. */
static void wrapper_vTaskDelete(FuncSecNum_t *a)
{
    if (a == NULL || a->FuncSecP6 == NULL || *a->FuncSecP6 == NULL) {
        return;
    }

    TaskHandle_t task_handle = *a->FuncSecP6;
    vTaskDelete(task_handle);
    *a->FuncSecP6 = NULL;
}

/* A running function owns its handle. Do not create a duplicate task that
 * could control the same actuators or overwrite the original handle. */
static void wrapper_xTaskCreate(FuncSecNum_t *a)
{
    if (a == NULL || a->FuncSecP1 == NULL || a->FuncSecP6 == NULL) {
        ESP_LOGE("FuncEvent", "Invalid task start request");
        return;
    }
    if (*a->FuncSecP6 != NULL) {
        ESP_LOGW("FuncEvent", "%s is already running; ignore duplicate start",
                 a->FuncSecP2);
        return;
    }
    if (xTaskCreate(a->FuncSecP1, a->FuncSecP2, a->FuncSecP3,
                    a->FuncSecP4, a->FuncSecP5, a->FuncSecP6) != pdPASS) {
        ESP_LOGE("FuncEvent", "Failed to create task %s", a->FuncSecP2);
        *a->FuncSecP6 = NULL;
    }
}

static FuncPrt FuncSeccon[FUNCSECORDER_NUM] = {
        [FUNCSEC_START]   = wrapper_xTaskCreate,
        [FUNCSEC_PAUSE]   = wrapper_vTaskSuspend,
        [FUNCSEC_RESUME]  = wrapper_vTaskResume,
        [FUNCSEC_STOP]    = wrapper_vTaskDelete,
};


static FuncSecNum_t Func[FUNCSEC_NUM] __attribute__((used)) ={

    [SOYMILK_SEC]   ={.FuncSecP1=SoyMilkMake_Task,.FuncSecP2="SoyMilkMake_Task",.FuncSecP3=4096,.FuncSecP4=NULL,.FuncSecP5=13,.FuncSecP6=&SoyMilkMakeHandle},
    [PRESSTOUFU_SEC]={.FuncSecP1=PressTask,       .FuncSecP2="PressTask",       .FuncSecP3=4096,.FuncSecP4=NULL,.FuncSecP5=13,.FuncSecP6=&PressHandle      },
    [TOUFU_SEC]     ={.FuncSecP1=TouFuMakeTask,   .FuncSecP2="TouFuTask",       .FuncSecP3=4096,.FuncSecP4=NULL,.FuncSecP5=13,.FuncSecP6=&TouFuHandle      },
    [MILK_SEC]      ={.FuncSecP1=MilkTask,        .FuncSecP2="Milk_Task",       .FuncSecP3=4096,.FuncSecP4=NULL,.FuncSecP5=13,.FuncSecP6=&MilkHandle       },
    [SELFCLEAN_SEC] ={.FuncSecP1=SelfCleanTask,   .FuncSecP2="SelfClean_Task",  .FuncSecP3=4096,.FuncSecP4=NULL,.FuncSecP5=13,.FuncSecP6=&SelfCleanHandle  },
    [BRINE_SEC]     ={.FuncSecP1=brineTask,       .FuncSecP2="brine_Task",      .FuncSecP3=4096,.FuncSecP4=NULL,.FuncSecP5=13,.FuncSecP6=&brineHandle      },
};

static Funcvoid FuncPre[FUNCSEC_NUM][FUNCSECORDER_NUM] __attribute__((used)) ={
    [SOYMILK_SEC]   ={ [FUNCSEC_START]=NULL,[FUNCSEC_PAUSE]=wrapper_SoyMilkPause,  [FUNCSEC_RESUME]=wrapper_SoyMilkResume,  [FUNCSEC_STOP]=wrapper_SoyMilkEnd,  },
    [PRESSTOUFU_SEC]={ [FUNCSEC_START]=NULL,[FUNCSEC_PAUSE]=wrapper_pressPause,    [FUNCSEC_RESUME]=wrapper_pressResume,    [FUNCSEC_STOP]=wrapper_pressEnd,    },
    [TOUFU_SEC]     ={ [FUNCSEC_START]=NULL,[FUNCSEC_PAUSE]=wrapper_TouFuPause,    [FUNCSEC_RESUME]=wrapper_TouFuResume,    [FUNCSEC_STOP]=wrapper_TouFuEnd,    },
    [MILK_SEC]      ={ [FUNCSEC_START]=NULL,[FUNCSEC_PAUSE]=wrapper_PlantMilkPause,[FUNCSEC_RESUME]=wrapper_PlantMilkResume,[FUNCSEC_STOP]=wrapper_PlantMilkEnd,},
    [SELFCLEAN_SEC] ={ [FUNCSEC_START]=NULL,[FUNCSEC_PAUSE]=wrapper_SelfCleanPause,[FUNCSEC_RESUME]=wrapper_SelfCleanResume,[FUNCSEC_STOP]=wrapper_SelfCleanEnd,},
    [BRINE_SEC]     ={ [FUNCSEC_START]=NULL,[FUNCSEC_PAUSE]=wrapper_brinePause,    [FUNCSEC_RESUME]=wrapper_brineResume,    [FUNCSEC_STOP]=wrapper_brineEnd,    },
};

static int FuncEvent(EventBits_t Typebits, EventBits_t Orderbits)
{
    if (Typebits == 0 || Orderbits == 0) {
        return 0;
    }

    int typeIdx  = ffs(Typebits) - 1;
    int orderIdx = ffs(Orderbits) - 1;
    ESP_LOGI("FuncEvent", "typeIdx=%d, orderIdx=%d", typeIdx, orderIdx);
    ESP_LOGI("FuncEvent", "Func[%d].FuncSecP1 = %p", typeIdx, Func[typeIdx].FuncSecP1);
    ESP_LOGI("FuncEvent", "Func[%d].FuncSecP2 = %s", typeIdx, Func[typeIdx].FuncSecP2);
    if (typeIdx >= NUM_FUNC_TYPES || orderIdx >= NUM_ORDER_TYPES) {
        ESP_LOGE("func", "Index out of bounds! Type=%d (0x%" PRIx32 "), Order=%d (0x%" PRIx32 ")", 
                 typeIdx, (uint32_t)Typebits, orderIdx, (uint32_t)Orderbits);
        
        xEventGroupClearBits(FuncTypeGroup, Typebits);
        xEventGroupClearBits(FuncOrderGroup, Orderbits);
        return -1;
    }
    if (FuncPre[typeIdx][orderIdx] != NULL) {
        FuncPre[typeIdx][orderIdx](NULL);
    } else {
        ESP_LOGD("func", "No pre-action defined for [%d][%d] yet.", typeIdx, orderIdx);
    }
    // 执行第二阶段 (这部分顺序是对的)
    if (FuncSeccon[orderIdx] != NULL) {
        FuncSeccon[orderIdx](&Func[typeIdx]);
    }
    if (orderIdx == FUNCSEC_STOP) {
        if (CurrentFunc == (FuncSecType_t)typeIdx) {
            CurrentFunc = NOTASK_SEC;
            CurrentOrder = FUNCSEC_STOP;
        }
    } else {
        CurrentFunc = (FuncSecType_t)typeIdx;
        CurrentOrder = (FuncSecOrder_t)orderIdx;
    }
    ESP_LOGI("FuncCurrent","Current Func:%d",CurrentFunc);
    // 清除事件位
    xEventGroupClearBits(FuncTypeGroup, Typebits); 
    xEventGroupClearBits(FuncOrderGroup, Orderbits); 
    return 1; 
}

void FuncMoniterTask(void *pvParameters)
{
        // 创建事件组
    SoyMilkEventGroup = xEventGroupCreate();
    PressEventGroup = xEventGroupCreate();
    PlantMilkEventGroup = xEventGroupCreate();
    SelfCleanEventGroup = xEventGroupCreate();
    FunctionSelectionEventGroup = xEventGroupCreate();
    FuncTypeGroup = xEventGroupCreate();
    FuncOrderGroup = xEventGroupCreate();
    // 安全检查：确保事件组创建成功（可选）
    if (!SoyMilkEventGroup || !PressEventGroup || 
        !PlantMilkEventGroup || !SelfCleanEventGroup) {
        // 处理错误（如重启）
        while (1) vTaskDelay(1000);
    }
    EventBits_t Typebits;
    EventBits_t Orderbits;
    while(1)
    {
        Typebits=xEventGroupGetBits(FuncTypeGroup);
        Orderbits=xEventGroupGetBits(FuncOrderGroup);
        FuncEvent(Typebits,Orderbits);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

FuncSecType_t GetCurTaskState(void)
{
    return CurrentFunc;
}

FuncSecOrder_t GetCurTaskOrder(void)
{
    return CurrentOrder;
}

void FunctionSelectionFlowCompleted(FuncSecType_t flow)
{
    if (CurrentFunc == flow) {
        CurrentFunc = NOTASK_SEC;
        CurrentOrder = FUNCSEC_STOP;
    }
}
