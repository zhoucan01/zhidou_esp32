/* Tests the production Task body, with only its C++ method qualifier removed. */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
enum { pdPASS=1, BUZZER_PLAY_QUICK, BUZZER_PLAY_FULL };
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGD(...) ((void)0)
typedef struct { uint32_t frequency, duration_ms; int mode; } message_t;
static message_t messages[4];
static size_t count, cursor, wait_count, delay_count, tone_count;
static TickType_t waits[10], delays[10];
static int queue_handle_, timed_out;
static jmp_buf done;
static BaseType_t xQueueReceive(int queue, message_t *out, TickType_t wait)
{
    (void)queue;
    waits[wait_count++]=wait;
    if (cursor<count) { *out=messages[cursor++]; return pdPASS; }
    if (!timed_out && wait!=portMAX_DELAY) { timed_out=1; return 0; }
    longjmp(done, 1);
}
static void vTaskDelay(TickType_t ticks) { delays[delay_count++]=ticks; }
static void Start(uint32_t freq) { (void)freq; ++tone_count; }
static void Stop(void) {}
#include "buzzer_task.inc"
static void run(const message_t *input, size_t n)
{
    memcpy(messages,input,n*sizeof(*input)); count=n;
    cursor=wait_count=delay_count=tone_count=0; timed_out=0;
    if (setjmp(done)==0) Task();
}
int main(void)
{
    run((message_t[]){{262,100,BUZZER_PLAY_QUICK},{330,200,BUZZER_PLAY_QUICK}},2);
    assert(!delay_count && wait_count==4);
    assert(waits[0]==portMAX_DELAY && waits[1]==100 && waits[2]==200 && waits[3]==portMAX_DELAY);
    run((message_t[]){{523,240,BUZZER_PLAY_FULL},{659,300,BUZZER_PLAY_FULL}},2);
    assert(delay_count==4 && delays[0]==216 && delays[1]==24 && delays[2]==270 && delays[3]==30);
    assert(wait_count==3 && waits[1]==portMAX_DELAY && waits[2]==portMAX_DELAY);
    run((message_t[]){{523,240,BUZZER_PLAY_FULL},{330,100,BUZZER_PLAY_QUICK}},2);
    assert(delay_count==2 && waits[2]==100); /* Mode does not leak. */
    run((message_t[]){{0,100,BUZZER_PLAY_FULL}},1);
    assert(!tone_count && delay_count==1 && delays[0]==100);
    puts("buzzer mode regression: PASS (quick/full/mixed/rest)");
}
