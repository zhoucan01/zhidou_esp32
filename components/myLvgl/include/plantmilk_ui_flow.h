#ifndef PLANTMILK_UI_FLOW_H
#define PLANTMILK_UI_FLOW_H

#include <stdbool.h>

/* 第二个功能的任务桥接入口；各页面的跳转实现位于 UI/PlantMilk 对应页面文件。 */
void PlantMilkUiConfigure(bool validation_mode, bool assume_sensors_present);
void PlantMilkUiEnter(void);
/* 将 UI 接入已有的奶或豆浆制作任务，不重新启动任务。 */
void PlantMilkUiFollowRunningFlow(void);
void PlantMilkUiHandleShortPress(void);
void PlantMilkUiHandleLongPress(void);
void PlantMilkUiTick(void);

#endif
