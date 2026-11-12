"""Generate NTC1/NTC2 lookup data from paired temperature/Modbus logs."""
import argparse
import csv
import math
import re
from pathlib import Path
from collections import defaultdict
from statistics import mean, median
from bisect import bisect_right


def read_pair(csv_path, txt_path, resolution, channel=2, return_timestamps=False):
    text = txt_path.read_text(encoding="utf-8", errors="replace")
    temperatures = defaultdict(list)
    invalid = 0
    for stamp, value in re.findall(r"\[(\d{2}:\d{2}:\d{2}\.\d+)\].*?=\s*(-?\d+(?:\.\d+)?)", text):
        value = float(value)
        if 0 <= value <= 150:
            temperatures[stamp if resolution == "millisecond" else stamp[:8]].append(value)
        else:
            invalid += 1
    with csv_path.open(encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.DictReader(stream))
    column = next(k for k in rows[0] if k.startswith(f"站2_NTC{channel} ADC") and not k.endswith("_HEX"))
    adc_by_time = defaultdict(list)
    seen = set()
    for row in rows:
        stamp = row["站2_最近接收时间"]
        if not stamp or stamp in seen or not row[column]:
            continue
        seen.add(stamp)
        match = re.fullmatch(r"\s*(\d+)\s*\(ADC\)\s*", row[column])
        if match:
            adc = int(match[1])
            if adc > 0:
                key = stamp.split()[-1]
                adc_by_time[key if resolution == "millisecond" else key[:8]].append(adc)
    records = [(stamp, mean(values), mean(temperatures[stamp]))
               for stamp, values in sorted(adc_by_time.items()) if stamp in temperatures]
    samples = [(a, t) for _, a, t in records]
    print(f"{csv_path.name}: valid TXT={sum(map(len, temperatures.values()))}, invalid TXT={invalid}, paired timestamps={len(samples)}")
    return records if return_timestamps else samples


def make_table(samples):
    bins = defaultdict(list)
    for adc, temp in samples:
        bins[int(adc) // 32].append((adc, temp))
    points = [(mean(a for a, _ in values), median(t for _, t in values), len(values))
              for _, values in sorted(bins.items())]
    # Weighted pool-adjacent-violators: temperature decreases as ADC increases.
    blocks = []
    for i, (_, temp, weight) in enumerate(points):
        blocks.append([i, i, temp * weight, weight])
        while len(blocks) > 1 and blocks[-2][2] / blocks[-2][3] < blocks[-1][2] / blocks[-1][3]:
            right = blocks.pop()
            left = blocks.pop()
            blocks.append([left[0], right[1], left[2] + right[2], left[3] + right[3]])
    fitted = [0.0] * len(points)
    for start, end, total, weight in blocks:
        for i in range(start, end + 1):
            fitted[i] = total / weight
    table = [(round(a, 3), round(t, 3)) for (a, _, _), t in zip(points, fitted)]
    if len(table) < 2:
        raise ValueError("At least two ADC bins are required; timestamp matches are insufficient")
    return table


def predict(table, adc):
    i = bisect_right([a for a, _ in table], adc)
    if i == 0:
        return table[0][1]
    if i == len(table):
        return table[-1][1]
    a, t = table[i - 1]
    b, u = table[i]
    return t + (u - t) * (adc - a) / (b - a)


def filter_and_fit(samples, max_error=20.0):
    """Refit after removing residuals > threshold until retained set stabilizes.

    Reference temperatures are required: this is offline calibration filtering,
    not a runtime sensor fault detector. Removed samples are never reintroduced.
    """
    retained = list(range(len(samples)))
    passes = 0
    while True:
        table = make_table([samples[i] for i in retained])
        keep = [i for i in retained if abs(predict(table, samples[i][0]) - samples[i][1]) <= max_error]
        if keep == retained:
            return table, retained, passes
        retained = keep
        passes += 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--channel", type=int, choices=(1, 2), default=2)
    parser.add_argument("--groups", type=int, nargs="+", default=[1, 2], help="Paired CSV/TXT group numbers")
    parser.add_argument("--resolution", choices=("second", "millisecond"), default="second")
    parser.add_argument("--write", action="store_true", help="Write the selected table under tables/NTC2")
    parser.add_argument("--output", help="Output table filename under tables/NTC2 (requires --write)")
    parser.add_argument("--max-error", type=float, help="Remove calibration residuals above this Celsius threshold and refit")
    args = parser.parse_args()
    groups = []
    for number in args.groups:
        groups.append(read_pair(args.directory / f"{number}.csv", args.directory / f"{number}.txt", args.resolution, args.channel))
    samples = [s for group in groups for s in group]
    table = make_table(samples)
    if args.max_error is not None:
        if not math.isfinite(args.max_error) or args.max_error <= 0:
            parser.error("--max-error must be finite and positive")
        table, retained, passes = filter_and_fit(samples, args.max_error)
        print(f"Removed {len(samples)-len(retained)}/{len(samples)} samples in {passes} passes (absolute error > {args.max_error} C)")
        keep = set(retained)
        offset = 0
        cleaned = []
        for group in groups:
            cleaned.append([s for i, s in enumerate(group, offset) if i in keep])
            offset += len(group)
        groups = cleaned
    print(f"Table: {len(table)} points, ADC {table[0][0]}..{table[-1][0]}, temperature {table[-1][1]}..{table[0][1]} C")
    for number, group in zip(args.groups, groups):
        if not group:
            print(f"Group {number}: no matched samples; excluded from calibration")
            continue
        errors = sorted(abs(predict(table, a) - t) for a, t in group)
        print(f"Group {number}: MAE={mean(errors):.3f} C, RMSE={math.sqrt(mean(e*e for e in errors)):.3f} C, P95={errors[int((len(errors)-1)*.95)]:.3f} C, max={max(errors):.3f} C")
    if args.write:
        lines = ["/* Generated by calibrate.py; ADC ascending, temperature non-increasing. */"]
        lines += [f"    {{{a:.3f}f, {t:.3f}f}}," for a, t in table]
        filename = "ntcTemp_table.inc" if args.channel == 2 else "ntcTemp_ntc1_table.inc"
        if args.output:
            if Path(args.output).name != args.output:
                parser.error("--output must be a filename without directories")
            filename = args.output
        output = Path(__file__).resolve().parents[2] / "tables" / "NTC2" / filename
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text("\n".join(lines) + "\n", encoding="utf-8")
