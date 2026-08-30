#include <stdio.h>
#include <stdint.h>
#include "Beep.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Music.h"

const Note_t song_star[] = {
    {NOTE_C4, 500}, {NOTE_C4, 500}, // 1 1
    {NOTE_G4, 500}, {NOTE_G4, 500}, // 5 5
    {NOTE_A4, 500}, {NOTE_A4, 500}, // 6 6
    {NOTE_G4, 1000},                // 5 - (长音)
    
    {NOTE_F4, 500}, {NOTE_F4, 500}, // 4 4
    {NOTE_E4, 500}, {NOTE_E4, 500}, // 3 3
    {NOTE_D4, 500}, {NOTE_D4, 500}, // 2 2
    {NOTE_C4, 1000},                // 1 -
    
    {0, 0} // 结束标�??
};

void Play_Music(void) {
    // int i = 0;
    // while (song_star[i].MC_freq != 0 || song_star[i].MC_duration != 0) { // 直到遇到结束标�??
        
    //      uint32_t freq = song_star[i].MC_freq;
    //      uint32_t duration = song_star[i].MC_duration;

    //     if (freq == NOTE_REST) {
    //         // 如果�?休�?��?�，�?延时，不发声
    //         // 注意：你�? Buzzer_Beep �?能不�?�? freq=0，所以直接延�?
    //         vTaskDelay(duration / portTICK_PERIOD_MS); 
    //     } else {
    //         // �?放音�?
    //         // 技巧：为了声音好听，实际播放时长可以比谱面�?一点点，留出空�?
    //         Buzzer_Beep(freq, duration * 0.9); 
            
    //         // 音�?�之间的停顿（休�?），避免粘连
    //         // 比�?�留�? 10%-15% 的时间作为间�?
    //         vTaskDelay((duration * 0.15) / portTICK_PERIOD_MS);
    //     }
    //     i++;
    // }
}

