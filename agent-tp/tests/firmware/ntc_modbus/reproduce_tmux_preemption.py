"""Model preemption of the adjacent AT32 production TMUX scanner; no hardware."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]
SLAVE = ROOT.parent / 'at32f415rbt7/code/app'
OUT = ROOT / 'build/test_ntc_modbus'
OUT.mkdir(parents=True, exist_ok=True)
source = (SLAVE / 'tmux1308.c').read_text()
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "tmux1308.h"
typedef unsigned adc_channel_select_type;
#define ADC_CHANNEL_13 13
#define ADC_CHANNEL_14 14
#define TRUE 1
#define FALSE 0
#define SET 1
#define tmux1308A0_GPIO_PORT 0
#define tmux1308A1_GPIO_PORT 1
#define tmux1308A2_GPIO_PORT 2
#define tmux1308A0_PIN 1
#define tmux1308A1_PIN 1
#define tmux1308A2_PIN 1
#define tmux1308IOData0_GPIO_PORT 0
#define tmux1308IOData0_PIN 0
#define CoreDebug_DEMCR_TRCENA_Msk 1
#define DWT_CTRL_CYCCNTENA_Msk 1
static struct { uint32_t DEMCR; } debug;
#define CoreDebug (&debug)
static struct dwt { uint32_t CTRL, CYCCNT; } clock_regs;
static struct dwt *fake_dwt(void) { clock_regs.CYCCNT+=2000; return &clock_regs; }
#define DWT fake_dwt()
static uint32_t system_core_clock=1000000;
static unsigned selected, inject, injected;
static void gpio_bits_write(unsigned port,unsigned pin,unsigned value) {
    (void)pin; selected=(selected & ~(1U<<port)) | (value<<port);
}
static unsigned gpio_input_data_bit_read(unsigned port,unsigned pin) {
    (void)port; (void)pin; return 0;
}
static uint16_t adc_read_channel(adc_channel_select_type channel) {
    if (inject && channel==14 && selected<4 && !(injected & (1U<<selected))) {
        injected |= 1U<<selected;
        /* A higher-priority debug capture selects another shared mux channel
           and returns without restoring the interrupted scanner's address. */
        tmux1308SetAddress(7);
    }
    static const uint16_t sensor_values[8]={1700,1800,1900,2000,2800,2900,3100,3000};
    return sensor_values[selected];
}
'''
checks = r'''
int main(void) {
    getTMUX1308Data();
    assert(tmux1308Data2Values[NTC1]==2000);
    assert(tmux1308Data2Values[NTC2]==1700);
    assert(tmux1308Data2Values[NTC3]==1800);
    inject=1;
    getTMUX1308Data();
    assert(tmux1308Data2Values[NTC1]==3000);
    assert(tmux1308Data2Values[NTC2]==3000);
    assert(tmux1308Data2Values[NTC3]==3000);
    inject=0;
    getTMUX1308Data();
    assert(tmux1308Data2Values[NTC1]==2000);
    assert(tmux1308Data2Values[NTC2]==1700);
    assert(tmux1308Data2Values[NTC3]==1800);
    puts("REPRODUCED: production scanner accepts other-channel ADC after modeled preemption; recovers on next scan");
}
'''
p = OUT / 'tmux_preemption.c'
p.write_text(prefix + source + checks)
binary = OUT / 'tmux_preemption'
subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(SLAVE),str(p),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
