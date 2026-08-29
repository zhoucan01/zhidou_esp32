#ifndef MYLVGL__H
#define MYLVGL__H

typedef enum {
    MODE_FLAG_NONE = 0,
    MODE_FLAG_CLEAN,
    MODE_FLAG_PLANT_MILK,
    MODE_FLAG_TOFU,
    MODE_FLAG_CLEAN_DEEP,
    MODE_FLAG_CLEAN_QUICK
} mode_flag_t;

extern volatile mode_flag_t currentModeFlag;

void lvUIshowTask(void *arg);

#endif
