#include <stdio.h>
#include <stdint.h>
#include "Beep.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Music.h"

/* 小星星：普通快速播放音乐。 */
const Note_t song_star[] = {
    {NOTE_C4, 500}, {NOTE_C4, 500}, // 1 1
    {NOTE_G4, 500}, {NOTE_G4, 500}, // 5 5
    {NOTE_A4, 500}, {NOTE_A4, 500}, // 6 6
    {NOTE_G4, 1000},                // 5 - (长音)
    
    {NOTE_F4, 500}, {NOTE_F4, 500}, // 4 4
    {NOTE_E4, 500}, {NOTE_E4, 500}, // 3 3
    {NOTE_D4, 500}, {NOTE_D4, 500}, // 2 2
    {NOTE_C4, 1000},                // 1 -
    
    {0, 0} // 结束标记
};

/* 配网就绪：独立曲谱，完整播放，共 2.70 秒。 */
static const Note_t song_provisioning_ready[] = {
    {NOTE_C5, 240}, {NOTE_E5, 240}, {NOTE_G5, 420}, {NOTE_E5, 300},
    {NOTE_D5, 240}, {NOTE_B4, 240}, {NOTE_D5, 300}, {NOTE_C5, 720},
};

void MusicPlayProvisioningReady(void)
{
    for (unsigned i = 0; i < sizeof(song_provisioning_ready) / sizeof(song_provisioning_ready[0]); ++i)
        Buzzer_BeepWithMode(song_provisioning_ready[i].MC_freq, song_provisioning_ready[i].MC_duration,
                            BUZZER_PLAY_FULL);
}

void Play_Music(void) {
    int i = 0;
    while (song_star[i].MC_freq != 0 || song_star[i].MC_duration != 0) { // 直到遇到结束标记
        
         uint32_t freq = song_star[i].MC_freq;
         uint32_t duration = song_star[i].MC_duration;

        if (freq == NOTE_REST) {
            // 如果是休止符，只延时，不发声
            // 注意：你的 Buzzer_Beep 可能不支持 freq=0，所以直接延时
            vTaskDelay(duration / portTICK_PERIOD_MS); 
        } else {
            // 播放音符
            // 技巧：为了声音好听，实际播放时长可以比谱面短一点点，留出空隙
            Buzzer_Beep(freq, duration * 0.9); 
            
            // 音符之间的停顿（休止），避免粘连
            // 比如留出 10%-15% 的时间作为间隔
            vTaskDelay((duration * 0.15) / portTICK_PERIOD_MS);
        }
        i++;
    }
}

