#include "SystemMoniter.h"

#include "ToufuMake.h"
#include "UsartExchange.h"
#include "FunctionSelection.h"

#include "esp_log.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"

#include "Bluetooch.h"
#include "DataCenter.h"

EventGroupHandle_t SystemGroup;
extern uint16_t DatafdTempServerHandle;
extern uint16_t SysMoniterServerHandle;


const float Heat1Power[] = {
    0.0f, 24.f, 47.f, 97.f, 113.f, 126.f, 148.f, 195.f, 234.f, 250.f, 280.f, 300.f, 313.f, 318.f,
    // Gears 14-30, power in watts.
    600.f, 650.f, 700.f, 750.f, 800.f, 850.f, 900.f, 950.f, 1000.f,
    1050.f, 1100.f, 1150.f, 1200.f, 1250.f, 1300.f, 1300.f, 1300.f
};

const float Heat2Power[] = {
    0.0f, 58.f, 113.f, 238.f, 277.f, 313.f, 366.f, 484.f, 586.f, 628.f, 710.f, 800.f, 800.f, 800.f,
    // Gears 14-30, power in watts.
    600.f, 650.f, 700.f, 750.f, 800.f, 850.f, 900.f, 950.f, 1000.f,
    1050.f, 1100.f, 1150.f, 1200.f, 1250.f, 1300.f, 1300.f, 1300.f
};

static float currentHeatPower[2];

#if SYSTEM_MEMORY_LOG_ENABLE
/* Capability pools overlap (DMA is part of internal RAM); do not add them.
 * No extra task or heap allocation is needed for these diagnostics. */
static void SystemMemoryLog(const char *stage)
{
    /* Called only by MachineControl. */
    static size_t previous_internal;
    static bool have_previous;
    const size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (have_previous) {
        ESP_LOGI("RAM", "%s internal net allocated since previous sample=%ld bytes",
                 stage, (long)previous_internal - (long)internal);
    }
    previous_internal = internal;
    have_previous = true;
    const uint32_t caps[] = {
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA,
    };
    const char *names[] = {"internal8", "psram8", "internalDMA"};
    for (unsigned i = 0; i < sizeof(caps) / sizeof(caps[0]); ++i) {
        multi_heap_info_t info;
        heap_caps_get_info(&info, caps[i]);
        ESP_LOGI("RAM", "%s %s bytes: free=%u used=%u min=%u largest=%u",
                 stage, names[i], (unsigned)info.total_free_bytes,
                 (unsigned)info.total_allocated_bytes,
                 (unsigned)info.minimum_free_bytes,
                 (unsigned)info.largest_free_block);
    }
}

#if CONFIG_FREERTOS_USE_TRACE_FACILITY
enum { MAX_DIAGNOSTIC_TASKS = 48, HEAP_DETAIL_TOP = 12, HEAP_DETAIL_BINS = 10,
       HEAP_DETAIL_REGIONS = 32 };
typedef struct {
    TaskStatus_t status[MAX_DIAGNOSTIC_TASKS];
    char names[MAX_DIAGNOSTIC_TASKS][configMAX_TASK_NAME_LEN];
    UBaseType_t count;
    size_t stack_bytes[MAX_DIAGNOSTIC_TASKS];
    size_t tcb_bytes[MAX_DIAGNOSTIC_TASKS];
    size_t used_bytes, other_bytes, other_blocks;
    size_t backing_bytes;
    unsigned region_count;
    bool regions_overflow;
    walker_heap_into_t regions[HEAP_DETAIL_REGIONS];
    size_t bin_bytes[HEAP_DETAIL_BINS], bin_count[HEAP_DETAIL_BINS];
    struct { void *ptr; size_t size; } top[HEAP_DETAIL_TOP];
} task_snapshot_t;

static bool SystemHeapRegionVisit(walker_heap_into_t heap,
                                  walker_block_info_t block, void *context)
{
    (void)block;
    task_snapshot_t *snapshot = context;
    for (unsigned i = 0; i < snapshot->region_count; ++i)
        if (snapshot->regions[i].start == heap.start) return true;
    if (snapshot->region_count == HEAP_DETAIL_REGIONS) {
        snapshot->regions_overflow = true;
        return true;
    }
    snapshot->regions[snapshot->region_count++] = heap;
    return true;
}

/* Walk callback: arithmetic only. Never log, allocate, or dereference a
 * captured task pointer while the allocator is traversing its blocks. */
static bool SystemHeapBlockVisit(walker_heap_into_t heap,
                                 walker_block_info_t block, void *context)
{
    if (!block.used || block.size == 0) return true;
    task_snapshot_t *snapshot = context;
    const uintptr_t start = (uintptr_t)block.ptr;
    /* IDF reserves an internal DMA pool by allocating from a parent heap,
     * then registering that same allocation as another heap. Count its child
     * allocations, not the parent backing block a second time. */
    for (unsigned i = 0; i < snapshot->region_count; ++i) {
        const uintptr_t child_start = (uintptr_t)snapshot->regions[i].start;
        const uintptr_t child_end = (uintptr_t)snapshot->regions[i].end;
        if (snapshot->regions[i].start != heap.start && child_start >= start &&
            child_end >= child_start && child_end - start < block.size) {
            snapshot->backing_bytes += block.size;
            return true;
        }
    }
    snapshot->used_bytes += block.size;
    for (UBaseType_t i = 0; i < snapshot->count; ++i) {
        const uintptr_t stack = (uintptr_t)snapshot->status[i].pxStackBase;
        const uintptr_t task = (uintptr_t)snapshot->status[i].xHandle;
        if (stack >= start && stack - start < block.size) {
            snapshot->stack_bytes[i] += block.size;
            return true;
        }
        if (task >= start && task - start < block.size) {
            snapshot->tcb_bytes[i] += block.size;
            return true;
        }
    }
    snapshot->other_bytes += block.size;
    ++snapshot->other_blocks;
    unsigned bin = 0;
    size_t limit = 64;
    while (bin + 1 < HEAP_DETAIL_BINS && block.size > limit) {
        ++bin;
        limit *= 2;
    }
    snapshot->bin_bytes[bin] += block.size;
    ++snapshot->bin_count[bin];
    for (unsigned i = 0; i < HEAP_DETAIL_TOP; ++i) {
        if (block.size > snapshot->top[i].size) {
            for (unsigned j = HEAP_DETAIL_TOP - 1; j > i; --j)
                snapshot->top[j] = snapshot->top[j - 1];
            snapshot->top[i].ptr = block.ptr;
            snapshot->top[i].size = block.size;
            break;
        }
    }
    return true;
}

static void SystemHeapDetailsLog(task_snapshot_t *snapshot)
{
    heap_caps_walk(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
                   SystemHeapRegionVisit, snapshot);
    for (unsigned i = 0; i < snapshot->region_count; ++i) {
        ESP_LOGI("RAM-DETAIL", "heap region[%u] start=%p end=%p", i,
                 (void *)snapshot->regions[i].start, (void *)snapshot->regions[i].end);
    }
    if (snapshot->regions_overflow) {
        ESP_LOGW("RAM-DETAIL", "Too many heap regions; backing exclusion may be incomplete");
    }
    heap_caps_walk(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
                   SystemHeapBlockVisit, snapshot);
    size_t stacks = 0, tcbs = 0;
    for (UBaseType_t i = 0; i < snapshot->count; ++i) {
        stacks += snapshot->stack_bytes[i];
        tcbs += snapshot->tcb_bytes[i];
        ESP_LOGI("RAM-DETAIL", "task=%s matched_stack_block=%u matched_tcb_block=%u bytes",
                 snapshot->names[i], (unsigned)snapshot->stack_bytes[i],
                 (unsigned)snapshot->tcb_bytes[i]);
    }
    ESP_LOGI("RAM-DETAIL", "internal walk bytes: used=%u stacks=%u tcbs=%u other=%u other_blocks=%u",
             (unsigned)snapshot->used_bytes, (unsigned)stacks, (unsigned)tcbs,
             (unsigned)snapshot->other_bytes, (unsigned)snapshot->other_blocks);
    ESP_LOGI("RAM-DETAIL", "internal raw_used=%u excluded_nested_heap_backing=%u bytes (not business buffers)",
             (unsigned)(snapshot->used_bytes + snapshot->backing_bytes),
             (unsigned)snapshot->backing_bytes);
    size_t limit = 64;
    for (unsigned i = 0; i < HEAP_DETAIL_BINS; ++i, limit *= 2) {
        if (snapshot->bin_count[i]) {
            ESP_LOGI("RAM-DETAIL", "other size_bin=%u range=%u..%u count=%u bytes=%u",
                     i, i == 0 ? 1U : (unsigned)(limit / 2 + 1),
                     (unsigned)(i + 1 == HEAP_DETAIL_BINS ? UINT32_MAX : limit),
                     (unsigned)snapshot->bin_count[i], (unsigned)snapshot->bin_bytes[i]);
        }
    }
    for (unsigned i = 0; i < HEAP_DETAIL_TOP; ++i) {
        if (snapshot->top[i].size) {
            ESP_LOGI("RAM-DETAIL", "other largest[%u] ptr=%p bytes=%u",
                     i, snapshot->top[i].ptr, (unsigned)snapshot->top[i].size);
        }
    }
    ESP_LOGI("RAM-DETAIL", "Address matches are sampled, not allocation callers; 0 can mean PSRAM/static/unmatched. Concurrent changes possible.");
}

static void SystemTaskMemoryLog(void)
{
    /* Avoid consuming scarce internal RAM or the monitor task's stack. */
    task_snapshot_t *snapshot = heap_caps_calloc(1, sizeof(*snapshot),
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (snapshot == NULL) {
        ESP_LOGW("RAM", "Cannot allocate task snapshot in PSRAM");
        return;
    }
    vTaskSuspendAll();
    UBaseType_t count = uxTaskGetSystemState(snapshot->status, MAX_DIAGNOSTIC_TASKS, NULL);
    for (UBaseType_t i = 0; i < count; ++i) {
        /* Copy names before logging; do not retain task handles or names
         * across diagnostic intervals. */
        strncpy(snapshot->names[i], snapshot->status[i].pcTaskName,
                configMAX_TASK_NAME_LEN - 1);
        snapshot->names[i][configMAX_TASK_NAME_LEN - 1] = '\0';
    }
    xTaskResumeAll();
    if (count == 0) {
        ESP_LOGW("RAM", "Task snapshot capacity exceeded (%u)", MAX_DIAGNOSTIC_TASKS);
    }
    snapshot->count = count;
    if (count != 0) {
        SystemHeapDetailsLog(snapshot);
    }
    for (UBaseType_t i = 0; i < count; ++i) {
        ESP_LOGI("RAM", "task=%s stack_base=%p stack_min_free=%u bytes priority=%u",
                 snapshot->names[i], (void *)snapshot->status[i].pxStackBase,
                 (unsigned)snapshot->status[i].usStackHighWaterMark,
                 (unsigned)snapshot->status[i].uxCurrentPriority);
    }
    heap_caps_free(snapshot);
}
#endif
#endif /* SYSTEM_MEMORY_LOG_ENABLE */

//管理OTA，时间查询，固件查询,任务状态查询（做豆浆：抽水、破壁加热、保温、排浆；做豆腐：抽水、破壁、加热、保温、排浆、点卤、蹲脑、压制、抬升；自清洁：进水、搅拌加热、防水、重复；植物奶：加水、破壁、（加热）、出浆）,设备状态
void SystemMonitorTask(void *pvParameters)
{
    static int workStatus[7]={0x66,0x56,0x00,0x00,0x00,0x65,0x65};
    SystemGroup = xEventGroupCreate();
#if SYSTEM_MEMORY_LOG_ENABLE
    TickType_t last_memory_log = xTaskGetTickCount();
#endif
    while(1)
    {
        // 方式1：发送单个数值
        workStatus[2] = getTouFuTaskStatus();
        SendMessage((uint8_t*)&workStatus, sizeof(workStatus), SysMoniterServerHandle);
        vTaskDelay(pdMS_TO_TICKS(1000));
#if SYSTEM_MEMORY_LOG_ENABLE
        TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - last_memory_log) >= pdMS_TO_TICKS(SYSTEM_MEMORY_LOG_INTERVAL_MS)) {
            last_memory_log = now;
            SystemMemoryLog("runtime");
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
            SystemTaskMemoryLog();
#endif
        }
#endif
        // ESP_LOGI(TAG,"turns:%f",getTurns());       
    }
}

void setHeat1Power(int heat1Gear)
{
    // Clamp the index to the supported power table range.
    const int maxCalibratedGear = (int)(sizeof(Heat1Power) / sizeof(Heat1Power[0])) - 1;
    heat1Gear = heat1Gear < 0 ? 0 : (heat1Gear > maxCalibratedGear ? maxCalibratedGear : heat1Gear);
    currentHeatPower[0]=Heat1Power[heat1Gear];
}
void setHeat2Power(int hrat2Gear)
{
    // Clamp the index to the supported power table range.
    const int maxCalibratedGear = (int)(sizeof(Heat2Power) / sizeof(Heat2Power[0])) - 1;
    hrat2Gear = hrat2Gear < 0 ? 0 : (hrat2Gear > maxCalibratedGear ? maxCalibratedGear : hrat2Gear);
    currentHeatPower[1]=Heat2Power[hrat2Gear];
}

float getHeatPower(int HeatNum)
{
    return currentHeatPower[HeatNum];
}
