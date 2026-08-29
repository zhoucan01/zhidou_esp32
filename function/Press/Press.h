#ifndef PRESS_H
#define PRESS_H

void PressTask(void* pvParameters);

// void Press(int TargetPusssure,int TargetTime);
void PressStageSlow(int iRunTime);
void PressStageUp(void);
void TouFuPress(void);
#endif // TOFU_MAKE_H