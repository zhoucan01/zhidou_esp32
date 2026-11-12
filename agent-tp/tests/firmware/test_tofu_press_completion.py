"""Run the real press/hold code with virtual time; no real motors or heating.

python -B agent-tp/tests/firmware/test_tofu_press_completion.py --cc <host cc>
Generated C and executable stay in build/ccache/tofu-press-completion.
"""
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/ccache/tofu-press-completion'


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
enum {riseLowPos, riseHighPos, pressHighPos};
static struct { int32_t pressRightEncoderPos; } g_DataPool;
static uint64_t now, hold_started_at, last_stop_at;
static bool hold_started;
static unsigned downward_commands, timed_up, timed_down, stops, current_gear;
static void vTaskDelay(uint32_t ticks) { now += ticks; }
static bool getSensorData(unsigned sensor) {
    assert(sensor == riseLowPos); /* No automatic final high-limit wait. */
    return true;
}
static void riseMoterSetCurrentGear(uint8_t gear) {
    assert(gear == 1); current_gear = gear;
}
static void riseMoterDown(uint16_t speed) { assert(speed == 45); }
static void riseMoterStop(void) {}
static void riseMoterTimedUpAndHold(void) { ++timed_up; }
static void riseMoterTimedDownAndHold(void) { ++timed_down; }
static void riseMoterUp(uint16_t speed) {
    (void)speed; assert(!"Final rise must wait for Collect Pulp");
}
static void pressMoterUp(uint16_t speed) {
    (void)speed; assert(!"Press head must wait for Collect Pulp");
}
static void Play_Music(void) { assert(!"Final melody belongs to Collect Pulp"); }
static void pressMoterStop(void) { ++stops; last_stop_at = now; }
static void pressMoterDown(uint16_t speed) {
    if (speed == 800) {
        ++downward_commands;
        g_DataPool.pressRightEncoderPos = downward_commands == 1 ? 20000 : 53000;
    } else if (speed == 8) {
        assert(!hold_started);
        hold_started = true;
        hold_started_at = now;
        g_DataPool.pressRightEncoderPos = 74000;
    } else {
        assert(speed == 0);
    }
}
'''

MAIN = r'''
int main(void) {
    assert(PRESS_HOLD_MS == 600000U);
    assert(TouFuPress());
    assert(hold_started && now - hold_started_at == 600000U);
    assert(downward_commands == 2 && timed_up == 1 && timed_down == 1);
    assert(stops >= 3 && last_stop_at == now);
    assert(current_gear == 1);
    puts("PASS: actual TouFuPress contact/drain/form/fast/slow stages run;");
    puts("hold lasts exactly 600000 ms, final press stop occurs, no final rise/head-up/music/low-gear command");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=shutil.which('gcc'))
    args = parser.parse_args()
    if not args.cc:
        parser.error('Provide --cc')
    OUT.mkdir(parents=True, exist_ok=True)
    source = (ROOT / 'function/Press/Press.c').read_text(encoding='utf-8')
    defines = '\n'.join(line for line in source.splitlines() if line.startswith('#define '))
    signatures = ['void pressFastStage(', 'static void pressGoToContact(',
                  'static void riseGoToBottom(', 'bool TouFuPress(']
    harness = PREFIX + defines + '\n' + '\n'.join(function(source, s) for s in signatures) + MAIN
    path = OUT / 'press_completion.c'
    path.write_text(harness, encoding='utf-8')
    exe = OUT / 'press_completion.exe'
    subprocess.run([args.cc, '-std=c11', '-O0', '-Wall', '-Wextra',
                    '-Wno-unused-function', str(path), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
    (OUT / 'run.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    print(result.stdout, end='')


if __name__ == '__main__':
    main()
