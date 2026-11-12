#ifndef TOFU_MAKE_H
#define TOFU_MAKE_H


void TouFuMakeTask(void* pvParameters);

void TouFuPause(void);
void TouFuEnd(void);
void TouFuResume(void);

void braineAct(void);

int getTouFuTaskStatus(void);

#endif // TOFU_MAKE_H