"""Measure snapshot ADC jumps and inject their amplitudes into production filters.

The CSVs are ~1 Hz snapshots, not 50 Hz firmware traces. The labeled injection
is a synthetic stress test, never a reconstruction of the missing samples.
"""
import argparse
import csv
import ctypes
from datetime import datetime
import hashlib
import json
from pathlib import Path
from statistics import median
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def library(source, name, timed):
    directory = ROOT / "build/adc_noise_analysis"
    directory.mkdir(parents=True, exist_ok=True)
    wrapper = directory / f"{name}.c"
    binary = directory / f"{name}.so"
    call = "update_adc_window(channel, adc, timestamp)" if timed else "update_adc_window(channel, adc)"
    wrapper.write_text(f'''#include "{source.as_posix()}"
struct DataPool g_DataPool;
static int64_t clock_us;
int64_t esp_timer_get_time(void) {{ return clock_us; }}
void reset_filter(void) {{ ntcTempResetFilter(); }}
uint32_t step(unsigned channel, uint32_t adc, int64_t timestamp) {{
 clock_us = timestamp;
 return {call};
}}
''')
    subprocess.run(["gcc", "-shared", "-fPIC", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-DMACHINE=3", "-DNTC_TEMP_LOG_ENABLE=0", "-I" + str(ROOT / "function/ntcTemp"),
                    "-I" + str(ROOT / "task/DataCenter"),
                    "-I" + str(ROOT / "agent-tp/tests/firmware/ntc_host"),
                    str(wrapper), "-lm", "-o", str(binary)], check=True)
    result = ctypes.CDLL(str(binary))
    result.step.argtypes = [ctypes.c_uint, ctypes.c_uint32, ctypes.c_int64]
    result.step.restype = ctypes.c_uint32
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, nargs="?", default=ROOT / "agent-tp/data/testdata/ntcdata")
    parser.add_argument("--baseline", type=Path, required=True, help="saved pre-change ntcTemp.c")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "agent-tp/reports/ntc/2026-10-28_ADC去噪验证")
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    old = library(args.baseline.resolve(), "old", False)
    new = library(ROOT / "function/ntcTemp/ntcTemp.c", "new", True)
    records = []
    provenance = []
    for path in sorted(args.directory.rglob("*.csv")):
        raw = []
        seen = set()
        with path.open(encoding="utf-8-sig", newline="") as stream:
            for line, row in enumerate(csv.DictReader(stream), 2):
                stamp = row.get("NTC接收时间", "")
                if row.get("事件来源") != "NTC" or not stamp or stamp in seen:
                    continue
                values = [row.get(f"NTC{ch}_ADC", "") for ch in (1, 2, 3)]
                if not all(values):
                    continue
                seen.add(stamp)
                raw.append((datetime.fromisoformat(stamp).timestamp(),
                            [int(float(v)) for v in values], line))
        raw.sort()
        provenance.append(dict(file=str(path.relative_to(ROOT)), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
        for ch in range(3):
            jumps = [(b[1][ch] - a[1][ch], b[0] - a[0], b[2])
                     for a, b in zip(raw, raw[1:]) if abs(b[1][ch] - a[1][ch]) > 200]
            short = [j for j in jumps if 0 < j[1] <= 1.5]
            old_bad = new_bad = 0
            for delta, gap, line in short:
                # Repeat each measured signed amplitude in 7-bad / 1-good bursts.
                # Truth is deliberately fixed at 2000 ADC for this injection.
                value = 2000 + delta
                if not 0 < value < 4095:
                    continue
                errors = []
                for module in (old, new):
                    module.reset_filter()
                    now = 1000000
                    for _ in range(20):
                        module.step(0, 2000, now)
                        now += 20000
                    largest = 0
                    for cycle in range(5):
                        for _ in range(7):
                            output = module.step(0, value, now)
                            largest = max(largest, abs(output - 2000))
                            now += 20000
                        output = module.step(0, 2000, now)
                        largest = max(largest, abs(output - 2000))
                        now += 20000
                    errors.append(largest)
                old_bad += errors[0] > 200
                new_bad += errors[1] > 200
            entry = dict(file=path.name, channel=ch + 1, samples=len(raw),
                median_interval_s=round(median([b[0] - a[0] for a, b in zip(raw, raw[1:])]), 3),
                jumps_over200=len(jumps), jumps_within1p5s=len(short),
                up=sum(j[0] > 0 for j in jumps), down=sum(j[0] < 0 for j in jumps),
                max_jump=max([abs(j[0]) for j in jumps], default=0),
                max_short_jump=max([abs(j[0]) for j in short], default=0),
                injected_old_errors_over200=old_bad, injected_new_errors_over200=new_bad)
            records.append(entry)
            print(entry)
    with (args.out_dir / "ADC跳变统计.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)
    (args.out_dir / "输入与方法.json").write_text(json.dumps(dict(
        provenance=provenance, baseline_sha256=hashlib.sha256(args.baseline.read_bytes()).hexdigest(),
        source_sha256=hashlib.sha256((ROOT / "function/ntcTemp/ntcTemp.c").read_bytes()).hexdigest(),
        method="Snapshot jump statistics plus labeled 50 Hz synthetic 7-bad/1-good bursts; not firmware replay.",
        records=records), ensure_ascii=False, indent=2))
    print("Totals:", {key: sum(r[key] for r in records) for key in (
        "samples", "jumps_over200", "jumps_within1p5s", "up", "down",
        "injected_old_errors_over200", "injected_new_errors_over200")})


if __name__ == "__main__":
    main()
