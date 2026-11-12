#ifndef CLEAN_UI_FLOW_H
#define CLEAN_UI_FLOW_H

#include <stdbool.h>

void CleanUiConfigure(bool validation_mode, bool assume_sensors_present);
void CleanUiEnter(void);
/* 接管已有的 SelfClean 任务，并保留其选定的清洗模式。 */
void CleanUiFollowRunningFlow(void);
void CleanUiHandleShortPress(void);
void CleanUiHandleLongPress(void);
void CleanUiTick(void);

#endif
