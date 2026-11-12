"""Fit an NTC2 ADC x heater-gear lookup table from paired logs."""

import argparse
import csv
import math
import re
from bisect import bisect_right
from collections import defaultdict
from pathlib import Path
from statistics import mean, median


GEAR_ANCHORS = (0, 9, 10, 19)
REFERENCE_PATTERN = re.compile(
    r"\[(\d{2}:\d{2}:\d{2}\.\d+)\].*?=\s*(-?\d+(?:\.\d+)?)"
)


def read_reference(path):
    temperatures = defaultdict(list)
    text = path.read_text(encoding="utf-8", errors="replace")
    for stamp, value in REFERENCE_PATTERN.findall(text):
        temperature = float(value)
        if 0 <= temperature <= 150:
            temperatures[stamp[:8]].append(temperature)
    return {stamp: mean(values) for stamp, values in temperatures.items()}


def gear_anchor(gear):
    return 9 if gear in (7, 8, 9) else gear


def read_samples(path, reference, date, start_time, end_time):
    by_second = defaultdict(list)
    with path.open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            stamp = row.get("timestamp", "")
            if not stamp.startswith(date + " "):
                continue
            clock = stamp.split()[1]
            second = clock[:8]
            if not start_time <= second <= end_time or second not in reference:
                continue
            gear_value = row.get("effective_gear", "")
            if not gear_value:
                continue
            adc = int(row["ntc2_adc"])
            if adc == 0:
                continue
            gear = int(gear_value)
            by_second[(second, gear_anchor(gear), gear)].append(adc)

    samples = []
    for (second, anchor, actual_gear), adcs in sorted(by_second.items()):
        samples.append((mean(adcs), reference[second], anchor, actual_gear, second))
    return samples


def make_table(samples, bin_width):
    bins = defaultdict(list)
    for adc, temperature, *_ in samples:
        bins[int(adc) // bin_width].append((adc, temperature))
    nodes = [
        (mean(adc for adc, _ in values), median(temp for _, temp in values), len(values))
        for _, values in sorted(bins.items())
    ]
    if len(nodes) < 2:
        raise ValueError("Each gear anchor needs samples in at least two ADC bins")

    # Enforce the thermistor's non-increasing temperature as ADC rises.
    blocks = []
    for index, (_, temperature, weight) in enumerate(nodes):
        blocks.append([index, index, temperature * weight, weight])
        while (len(blocks) > 1 and
               blocks[-2][2] / blocks[-2][3] < blocks[-1][2] / blocks[-1][3]):
            right = blocks.pop()
            left = blocks.pop()
            blocks.append([
                left[0], right[1], left[2] + right[2], left[3] + right[3]
            ])

    fitted = [0.0] * len(nodes)
    for first, last, total, weight in blocks:
        for index in range(first, last + 1):
            fitted[index] = total / weight
    return [(nodes[index][0], fitted[index]) for index in range(len(nodes))]


def lookup(table, adc):
    index = bisect_right([point[0] for point in table], adc)
    if index == 0:
        return table[0][1]
    if index == len(table):
        return table[-1][1]
    low_adc, low_temp = table[index - 1]
    high_adc, high_temp = table[index]
    fraction = (adc - low_adc) / (high_adc - low_adc)
    return low_temp + fraction * (high_temp - low_temp)


def predict(tables, adc, gear):
    if gear <= GEAR_ANCHORS[0]:
        return lookup(tables[GEAR_ANCHORS[0]], adc)
    if gear >= GEAR_ANCHORS[-1]:
        return lookup(tables[GEAR_ANCHORS[-1]], adc)
    for lower, upper in zip(GEAR_ANCHORS, GEAR_ANCHORS[1:]):
        if lower <= gear <= upper:
            fraction = (gear - lower) / (upper - lower)
            low_temp = lookup(tables[lower], adc)
            high_temp = lookup(tables[upper], adc)
            return low_temp + fraction * (high_temp - low_temp)
    raise ValueError(f"Unsupported gear: {gear}")


def errors_for(samples, tables):
    return [predict(tables, adc, gear) - temperature
            for adc, temperature, _, gear, _ in samples]


def print_metrics(label, errors):
    absolute = sorted(abs(error) for error in errors)
    print(
        f"{label}: N={len(errors)}, MAE={mean(absolute):.3f} C, "
        f"RMSE={math.sqrt(mean(error * error for error in errors)):.3f} C, "
        f"P95={absolute[int((len(absolute) - 1) * 0.95)]:.3f} C, "
        f"max={max(absolute):.3f} C"
    )


def validate(samples, bin_width):
    tables = {}
    held_out = []
    by_anchor = defaultdict(list)
    for sample in samples:
        by_anchor[sample[2]].append(sample)
    for anchor in GEAR_ANCHORS:
        points = sorted(by_anchor[anchor], key=lambda sample: sample[4])
        training = [sample for index, sample in enumerate(points) if index % 5 != 0]
        validation = [sample for index, sample in enumerate(points) if index % 5 == 0]
        tables[anchor] = make_table(training, bin_width)
        held_out.extend(validation)
    print_metrics("时间顺序每5点留1点验证", errors_for(held_out, tables))
    for anchor in GEAR_ANCHORS:
        group = [sample for sample in held_out if sample[2] == anchor]
        print_metrics(f"  档位锚点 {anchor}", errors_for(group, tables))


def write_table(path, samples, bin_width):
    by_anchor = defaultdict(list)
    for sample in samples:
        by_anchor[sample[2]].append(sample)
    tables = {
        anchor: make_table(by_anchor[anchor], bin_width)
        for anchor in GEAR_ANCHORS
    }
    adc_values = [sample[0] for sample in samples]
    lines = [
        "/* Generated by calibrate_2d.py; each row is ADC ascending, temperature non-increasing. */",
        f"#define NTC2_CALIBRATED_ADC_MIN {min(adc_values):.3f}f",
        f"#define NTC2_CALIBRATED_ADC_MAX {max(adc_values):.3f}f",
    ]
    for anchor in GEAR_ANCHORS:
        lines.append(f"static const NtcTempPoint ntc2_gear_{anchor}_table[] = {{")
        lines.extend(f"    {{{adc:.3f}f, {temp:.3f}f}}," for adc, temp in tables[anchor])
        lines.append("};")
    lines.append("static const NtcTempGearCurve ntc2_gear_tables[] = {")
    for anchor in GEAR_ANCHORS:
        points = by_anchor[anchor]
        lines.append(
            f"    {{{anchor}, ntc2_gear_{anchor}_table, "
            f"sizeof(ntc2_gear_{anchor}_table) / sizeof(ntc2_gear_{anchor}_table[0]), "
            f"{min(sample[0] for sample in points):.3f}f, "
            f"{max(sample[0] for sample in points):.3f}f}},"
        )
    lines.append("};")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {sum(len(table) for table in tables.values())} nodes to {path}")
    for anchor, table in tables.items():
        points = by_anchor[anchor]
        print(
            f"  gear {anchor}: n={len(points)}, nodes={len(table)}, "
            f"ADC={min(s[0] for s in points):.1f}..{max(s[0] for s in points):.1f}, "
            f"T={min(s[1] for s in points):.1f}..{max(s[1] for s in points):.1f} C"
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adc-csv", type=Path,
                        default=Path("agent-tp/data/testdata/ntcdata/261014/231906_ntc2_adc.csv"))
    parser.add_argument("--reference-txt", type=Path,
                        default=Path("agent-tp/data/testdata/ntcdata/261014/SaveWindows2026_10_14_21-25-47.TXT"))
    parser.add_argument("--date", default="2026-10-14")
    parser.add_argument("--start-time", default="20:12:00")
    parser.add_argument("--end-time", default="21:17:15")
    parser.add_argument("--bin-width", type=int, default=48)
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[2] / "tables" / "NTC2" /
                        "ntcTemp_2d_table.inc")
    parser.add_argument("--write", action="store_true", help="write the generated C table")
    args = parser.parse_args()
    if args.bin_width <= 0:
        parser.error("--bin-width must be positive")

    reference = read_reference(args.reference_txt)
    samples = read_samples(args.adc_csv, reference, args.date, args.start_time, args.end_time)
    if not samples:
        parser.error("no same-second ADC/reference samples in the selected date/time window")
    print(f"Paired seconds: {len(samples)}; window: {args.date} {args.start_time}..{args.end_time}")
    validate(samples, args.bin_width)
    if args.write:
        write_table(args.output, samples, args.bin_width)


if __name__ == "__main__":
    main()