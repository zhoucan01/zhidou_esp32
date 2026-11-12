#ifndef MYLVGL__H
#define MYLVGL__H

#include <stdbool.h>
#include <stdint.h>

/* 三个功能共用的 UI 模式开关；修改后需要重新编译固件。
 * 0：读取硬件传感器并启动实际工艺任务。
 * 1：豆腐、植物奶、清洗均模拟传感器与流程，不从 UI 下发硬件命令。 */
#ifndef MYLVGL_UI_VALIDATION_MODE
#define MYLVGL_UI_VALIDATION_MODE 0
#endif

#if MYLVGL_UI_VALIDATION_MODE != 0 && MYLVGL_UI_VALIDATION_MODE != 1
#error "MYLVGL_UI_VALIDATION_MODE must be 0 or 1"
#endif

#define MYLVGL_VALIDATION_MAKING_MS   5000U
#define MYLVGL_VALIDATION_STOPPING_MS 2000U

/* 查询公共开关；初始化三个 UI 功能的流程接口（UI 启动时调用一次）。 */
bool UiValidationModeEnabled(void);
void UiFlowConfigure(void);

/* Runtime LVGL refresh rate used by the station-1 Modbus register. */
bool DisplayFrameRateSet(uint16_t fps);
uint16_t DisplayFrameRateGet(void);

/* 预留输入，待硬件完成凝固剂低液位传感器映射后使用。 */
void MachineCheckSetCoagulantLowLevel(bool detected);

/* 提供压制通道的逻辑状态：true 表示已安装，false 表示未安装。
 * 将 myLvgl.c 中的 MYLVGL_ASSUME_PRESS_CHANNEL_PRESENT 设为 0 后使用此输入。 */
void PressPartsCheckSetChannelPresent(bool detected);

void lvUIshowTask(void *arg);

#endif
