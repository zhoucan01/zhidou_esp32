#ifndef __MUSIC_H
#define __MUSIC_H

#include "Beep.h"


// 单位：Hz。这是最常用的 C 大调音阶
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523 // 高音 Do
#define NOTE_G3  196 // 低音 Sol

// 特殊定义：休止符（不发声）
#define NOTE_REST 0

// 我们用一个结构体数组来存歌，这样更清晰
typedef struct {
    uint32_t MC_freq;    // 频率
    uint32_t MC_duration;// 时长 (毫秒)
} Note_t;



void Play_Music(void);

#endif