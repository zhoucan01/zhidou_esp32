#ifndef SELFCLEAN_H
#define SELFCLEAN_H

void SelfCleanTask(void *pvParameters);
void LightSelfClean(void);
void DeepSelfClean(void);
void SelfCleanPause(void);
void SelfCleanResume(void);
void SelfCleanEnd(void);
#endif