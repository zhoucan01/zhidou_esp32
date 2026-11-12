#ifndef MYENCODER__H
#define MYENCODER__H

/* 硬件方向校准：当前安装的编码器在物理正转时报告 -1，
 * 反转时报告 +1。
 * UI 统一规则：所有页面和数值编辑中，正转选择逻辑上的下一项或下方项，
 * 反转选择上一项或上方项。 */
#define MY_ENCODER_FORWARD_DIRECTION (-1)

#if MY_ENCODER_FORWARD_DIRECTION != 1 && \
    MY_ENCODER_FORWARD_DIRECTION != -1
#error "MY_ENCODER_FORWARD_DIRECTION must be either 1 or -1"
#endif

#define MY_ENCODER_REVERSE_DIRECTION (-MY_ENCODER_FORWARD_DIRECTION)

typedef struct {
    /* Boot-only 5 s gesture; invoked in task context, never in the ISR. */
    void (*boot_hold_callback)(void);
} my_encoder_task_config_t;

/* Optional boot gesture configuration; NULL keeps normal button handling. */
void enctest(void *arg);

#endif
