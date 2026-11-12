"""Run the production task startup gates and failure shutdowns on the host."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/test_task_start_calibration"


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
enum {TOUFU_SEC=1, MILK_SEC, SELFCLEAN_SEC};
static bool calibration_ok, isRun, faultPaused;
static unsigned calls, deleted, completed, business, shutdown;
static void *TouFuHandle, *MilkHandle, *SelfCleanHandle;
static struct { bool isRunning, isEnd; } TouFuMoniter;
static void *xTaskGetCurrentTaskHandle(void) { return (void *)1; }
static bool runPowerCalibration(uint32_t timeout) {
    assert(timeout == 60000U && shutdown == 0 && business == 0);
    ++calls; return calibration_ok;
}
static void Heat1Run(int v) { assert(v == 0); shutdown |= 1; }
static void Heat2Run(int v) { assert(v == 0); shutdown |= 2; }
static void NoBushRun(int v) { assert(v == 0); shutdown |= 4; }
static void setHighBroad(int v) { assert(v == 0); shutdown |= 8; }
static void hosePumpOff(void) { shutdown |= 16; }
static void waterPumpOff(void) { shutdown |= 32; }
static void brinePumpOff(void) { shutdown |= 64; }
static void pressMoterStop(void) { shutdown |= 128; }
static void riseMoterStop(void) { shutdown |= 256; }
static void milkUnregisterFault(void) { shutdown |= 512; }
static void FunctionSelectionFlowCompleted(unsigned flow) { completed = flow; }
static void vTaskDelete(void *task) { assert(task == NULL); ++deleted; }
'''


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    cases = [
        ("ToufuMake", "TouFuMakeTask", "TouFuEnd", "while(1)", "TouFuHandle", "TOUFU_SEC", 511),
        ("Milk", "MilkTask", "PlantMilkEnd", "isRun = true;", "MilkHandle", "MILK_SEC", 639),
        ("SelfClean", "SelfCleanTask", "SelfCleanEnd", "while(1)", "SelfCleanHandle", "SELFCLEAN_SEC", 127),
    ]
    for module, task, stop, marker, handle, flow, mask in cases:
        source = (ROOT / f"function/{module}/{module}.c").read_text()
        task_source = function(source, f"void {task}(")
        # Only replace the lengthy production process after its startup gate.
        gate = task_source[:task_source.index(marker)] + "++business;\n}"
        harness = PREFIX + function(source, f"void {stop}(") + "\n" + gate
        harness += f'''
int main(void) {{
    (void)xTaskGetCurrentTaskHandle;
    (void)pressMoterStop; (void)riseMoterStop; (void)milkUnregisterFault;
    (void)TouFuHandle; (void)MilkHandle; (void)SelfCleanHandle;
    (void)TouFuMoniter; (void)isRun; (void)faultPaused;
    {handle} = (void *)1;
    {task}(NULL);
    assert(calls == 1 && deleted == 1 && completed == {flow});
    assert(shutdown == {mask} && business == 0 && {handle} == NULL);
    calibration_ok = true;
    calls = deleted = completed = shutdown = 0;
    {handle} = (void *)1;
    {task}(NULL);
    assert(calls == 1 && business == 1 && deleted == 0);
    assert(completed == 0 && shutdown == 0 && {handle} != NULL);
}}
'''
        path = OUT / f"{module}.c"
        path.write_text(harness)
        binary = OUT / module
        subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", str(path), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print(f"PASS: {task} calibrates once before work; failure stops outputs and clears task/flow")


if __name__ == "__main__":
    main()
