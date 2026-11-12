#ifndef PRESS_H
#define PRESS_H

#include <stdbool.h>

void PressTask(void* pvParameters);

// void Press(int TargetPusssure,int TargetTime);
void PressStageSlow(int iRunTime);
void PressStageUp(void);
bool TouFuPress(void);
void pressEnd(void);
void pressResume(void);
void pressPause(void);


#endif // TOFU_MAKE_H