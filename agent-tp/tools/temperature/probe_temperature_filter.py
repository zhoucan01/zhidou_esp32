"""Probe the real trend filter using labeled synthetic inputs; no firmware edits.

This isolates machine3_trend_filter, not the ADC gate, table, median or final
DataCenter filter. C source/executable stay in build; delivered CSV in agent-tp.
"""
import argparse
import csv
from io import StringIO
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
HARNESS = r'''
#include <stdio.h>
#include "ntcTemp.c"
struct DataPool g_DataPool;

int main(void)
{
    const char *names[] = {"single_spike", "persistent_bias", "real_change_after_gap",
                           "ramp_0.2C_per_s", "ramp_2C_per_s"};
    puts("scenario,elapsed_s,input_C,synthetic_truth_C,output_C");
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        ntcTempResetFilter();
        for (unsigned i = 0; i < 100; ++i)
            (void)machine3_trend_filter(0, 85.0f, (int64_t)i * 20000);
        for (unsigned i = 0; i < 125; ++i) {
            float truth = 85.0f;
            float input = truth;
            float elapsed = .02f * (float)(i + 1);
            if (scenario == 0 && i == 10) input += 4.0f;
            if (scenario == 1 || scenario == 2) {
                elapsed += 3.0f; /* Three-second gap before the first new point. */
                input = 87.0f;
                if (scenario == 2) truth = input;
            }
            if (scenario == 3 || scenario == 4) {
                truth += (scenario == 3 ? .2f : 2.0f) * elapsed;
                input = truth;
            }
            float output = machine3_trend_filter(0, input,
                2000000 + (int64_t)(elapsed * 1000000.0f));
            printf("%s,%.3f,%.4f,%.4f,%.4f\n", names[scenario], elapsed,
                   input, truth, output);
        }
    }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "agent-tp/reports/temperature/2026-10-28_温度提取流程探针.csv")
    args = parser.parse_args()
    build = ROOT / "build/temperature_flow_probe"
    build.mkdir(parents=True, exist_ok=True)
    source = build / "probe.c"
    executable = build / "probe"
    source.write_text(HARNESS)
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-DMACHINE=3", "-DNTC_TEMP_LOG_ENABLE=0",
                    "-I", str(ROOT / "function/ntcTemp"),
                    "-I", str(ROOT / "agent-tp/tests/firmware/ntc_host"),
                    "-I", str(ROOT / "task/DataCenter"),
                    str(source), str(ROOT / "agent-tp/tests/firmware/ntc_host/clock.c"),
                    "-lm", "-o", str(executable)], check=True)
    output = subprocess.run([str(executable)], capture_output=True, text=True, check=True).stdout
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output)
    rows = list(csv.DictReader(StringIO(output)))
    for name in dict.fromkeys(r["scenario"] for r in rows):
        points = [r for r in rows if r["scenario"] == name]
        errors = [abs(float(r["output_C"]) - float(r["synthetic_truth_C"])) for r in points]
        print(f"{name}: n={len(points)}, truth_end={points[-1]['synthetic_truth_C']}, "
              f"output_end={points[-1]['output_C']}, MAE={sum(errors)/len(errors):.4f} C")
    print(f"Saved: {args.output}")


if __name__ == "__main__":
    main()
