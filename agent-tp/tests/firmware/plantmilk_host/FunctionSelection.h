#ifndef HOST_FUNCTION_SELECTION_H
#define HOST_FUNCTION_SELECTION_H
#include "freertos/event_groups.h"
typedef enum {SOYMILK_SEC, PRESSTOUFU_SEC, TOUFU_SEC, MILK_SEC, SELFCLEAN_SEC, BRINE_SEC, NOTASK_SEC} FuncSecType_t;
typedef enum {FUNCSEC_START, FUNCSEC_PAUSE, FUNCSEC_RESUME, FUNCSEC_STOP, FUNCSECORDER_NUM} FuncSecOrder_t;
extern EventGroupHandle_t FuncTypeGroup, FuncOrderGroup;
#define SelfClean (1U << SELFCLEAN_SEC)
#define SelfClean_ModeBit (1U << 4)
#define FuncStart_bit 1U
#define FuncStop_bit 8U
FuncSecType_t GetCurTaskState(void);
FuncSecOrder_t GetCurTaskOrder(void);
#endif
