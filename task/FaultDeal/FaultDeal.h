#ifndef FAULTDEAL__H
#define FAULTDEAL__H

#define MAX_DETECTORS 10
#define NAME_LEN 20

#include <stdbool.h>

// ============ 1. 先定义枚举 ============
typedef enum {
    FAULT_NONE = 0,
    FAULT_WARNING = 1,
    FAULT_ERROR = 2,
} faultLevel_t;

// ============ 2. 再定义结构体 ============
typedef struct {
    int errCode;
    faultLevel_t faultLevel;
} faultResult_t;

// ============ 3. 再定义函数指针类型 ============
typedef faultResult_t (*detectFunc)(void);

// ============ 4. 再定义注册表条目 ============
typedef struct {
    char name[NAME_LEN];
    detectFunc func;
    bool active;
} detectEntry_t;

// ============ 5. 最后声明函数 ============
void FaultDealTask(void *pvParameters);
int FaultDetectRegister(const char *name, detectFunc func);
bool FaultDetectUnregister(int handle);
faultResult_t FaultDetectExecute(void);
void FaultDetectInit(void);
void FaultDetectDump(void);

#endif