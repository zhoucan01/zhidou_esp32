"""Fit the machine-3 NTC database from soy-host CSV and serial logs."""
import argparse
import csv
import json
import math
import re
from bisect import bisect_left, bisect_right
from collections import defaultdict
from datetime import datetime
from pathlib import Path
from statistics import mean

from calibrate import make_table, predict

CHANNEL_COLUMNS = ("NTC1_ADC", "NTC2_ADC", "NTC3_ADC")
MAX_REFERENCE_GAP_SECONDS = 2.0
MOTOR_STOP_SETTLE_SECONDS = 0.5
DEFAULT_WATER_VOLUME_ML = 1150
COOLDOWN_PROFILE = (0, 0.0, 0.0, 1, 3.0, 1.5)
SERIAL_FRAME = re.compile(
    r"^(\S+).*?RX command MODBUS-RX:\[([0-9A-Fa-f ]+)\]")


def parse_time(value):
    return datetime.fromisoformat(value)


def read_motor_events(serial_path):
    events = []
    for line in serial_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = SERIAL_FRAME.search(line)
        if not match:
            continue
        try:
            frame = bytes.fromhex(match.group(2))
        except ValueError:
            continue
        if (len(frame) == 8 and frame[0] == 0x03 and frame[1] == 0x06 and
                frame[2:4] == b"\x00\x04"):
            events.append((parse_time(match.group(1)),
                           int.from_bytes(frame[4:6], "big")))
    if not events:
        raise ValueError("No station-3 motor register confirmations in serial log")
    return events


def interpolate_reference(references, reference_times, stamp):
    index = bisect_left(reference_times, stamp)
    if index < len(references) and references[index][0] == stamp:
        return references[index][1]
    if index == 0 or index == len(references):
        return None
    before, after = references[index - 1], references[index]
    gap = (after[0] - before[0]).total_seconds()
    if gap <= 0 or gap > MAX_REFERENCE_GAP_SECONDS:
        return None
    fraction = (stamp - before[0]).total_seconds() / gap
    return before[1] + fraction * (after[1] - before[1])


def stage_profile(stage):
    return (
        int(stage["heater_gear"]),
        float(stage["heater_on_seconds"]),
        float(stage["heater_off_seconds"]),
        int(stage["motor_gear"]),
        float(stage["motor_on_seconds"]),
        float(stage["motor_off_seconds"]),
    )


def read_database_samples(csv_path, serial_path, config,
                          cooling_start_line=None):
    motor_events = read_motor_events(serial_path)
    motor_times = [event[0] for event in motor_events]
    with csv_path.open(encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.DictReader(stream))
    references = sorted(
        (parse_time(row["真实温度接收时间"]), float(row["真实温度℃"]))
        for row in rows
        if row.get("事件来源") == "真实温度" and row.get("真实温度℃")
    )
    if not references:
        raise ValueError("CSV contains no reference temperature samples")
    reference_times = [item[0] for item in references]

    samples = defaultdict(lambda: defaultdict(list))
    stats = defaultdict(int)
    was_heating = False
    motor_index = 0
    motor_gear = 0
    motor_stopped_since = None
    for line_number, row in enumerate(rows, start=2):
        if row.get("事件来源") != "NTC" or any(
                not row.get(column) for column in CHANNEL_COLUMNS):
            continue
        stats["ntc_rows"] += 1
        stamp = parse_time(row["NTC接收时间"])
        try:
            mode = int(row["模式"])
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError("CSV NTC rows require an integer 模式 field") from error
        if mode < 0 or mode > len(config["stages"]):
            raise ValueError(f"Unknown automatic heating mode {mode}")
        if cooling_start_line is not None:
            if mode != 0:
                continue
            if line_number < cooling_start_line:
                stats["cooling_before_start_line"] += 1
                continue
            was_heating = True
        was_heating = was_heating or mode > 0
        if mode == 0 and not was_heating:
            stats["before_first_heating"] += 1
            continue
        stats["eligible_profile_rows"] += 1

        while motor_index < len(motor_events) and motor_times[motor_index] <= stamp:
            event_time, new_gear = motor_events[motor_index]
            if new_gear != motor_gear:
                motor_gear = new_gear
                motor_stopped_since = event_time if motor_gear == 0 else None
            motor_index += 1
        if motor_gear != 0 or motor_stopped_since is None or (
                stamp - motor_stopped_since).total_seconds() < MOTOR_STOP_SETTLE_SECONDS:
            stats["rejected_motor_not_stopped_500ms"] += 1
            continue
        stats["motor_stopped_500ms"] += 1

        temperature = interpolate_reference(references, reference_times, stamp)
        if temperature is None:
            stats["rejected_reference_alignment"] += 1
            continue
        if mode:
            profile = stage_profile(config["stages"][mode - 1])
            phase = 1
        else:
            profile = COOLDOWN_PROFILE
            phase = 0
        for channel, column in enumerate(CHANNEL_COLUMNS):
            adc = int(row[column])
            if adc > 0:
                samples[(phase, *profile)][channel].append((adc, temperature))
        stats["accepted_samples"] += 1
    return samples, stats


def emit_database(samples, output=None, merge_existing=None):
    lines = [
        f"/* Generated by calibrate_machine3_database.py; motor stopped >= "
        f"{MOTOR_STOP_SETTLE_SECONDS * 1000:.0f} ms; sorted by query key and ADC; water_volume_ml before ADC/temperature. */"
    ]
    fitted_groups = 0
    generated_entries = []
    for (phase, gear, heat_on, heat_off, motor_gear,
         motor_on, motor_off), channels in sorted(samples.items()):
        for channel in sorted(channels):
            records = channels[channel]
            if len(records) < 10:
                raise ValueError(
                    f"Insufficient samples for channel {channel + 1}, "
                    f"profile {(phase, gear, heat_on, heat_off, motor_gear, motor_on, motor_off)}: "
                    f"{len(records)}")
            table = make_table(records)
            held_out = [sample for index, sample in enumerate(records) if index % 5 == 0]
            training = [sample for index, sample in enumerate(records) if index % 5 != 0]
            validation_table = make_table(training)
            errors = [predict(validation_table, adc) - temp
                      for adc, temp in held_out]
            mae = mean(abs(error) for error in errors)
            rmse = math.sqrt(mean(error * error for error in errors))
            print(
                f"{'heat' if phase else 'cool'} NTC{channel + 1} "
                f"gear={gear} heat={heat_on:g}/{heat_off:g}s "
                f"motor={motor_gear} {motor_on:g}/{motor_off:g}s "
                f"samples={len(records)} nodes={len(table)} "
                f"ADC={table[0][0]:.0f}..{table[-1][0]:.0f} "
                f"holdout MAE={mae:.3f} RMSE={rmse:.3f} C")
            for adc, temperature in table:
                generated_entries.append((
                    (channel, phase, gear, heat_on, heat_off, motor_gear,
                     motor_on, motor_off, adc),
                    f"    {{{channel}U, {phase}U, {gear}U, "
                    f"{heat_on:.3f}f, {heat_off:.3f}f, {motor_gear}, "
                    f"{motor_on:.3f}f, {motor_off:.3f}f, {DEFAULT_WATER_VOLUME_ML}U, "
                    f"{{{adc:.3f}f, {temperature:.3f}f}}}},"))
            fitted_groups += 1
    if not fitted_groups:
        raise ValueError("No temperature database groups were fitted")
    if merge_existing is not None:
        profile_pattern = re.compile(
            r"^\s*\{(\d+)U, (\d+)U, (\d+)U, "
            r"([\d.]+)f, ([\d.]+)f, (-?\d+), "
            r"([\d.]+)f, ([\d.]+)f, (\d+)U, \{([\d.]+)f, ([\d.]+)f\}\},$")
        replaced_profiles = set(samples)
        merged_entries = []
        for line in merge_existing.read_text(encoding="utf-8").splitlines():
            match = profile_pattern.match(line)
            if not match:
                continue
            channel, phase, gear, heat_on, heat_off, motor_gear, motor_on, motor_off, water_volume_ml, adc, _ = match.groups()
            profile = (int(phase), int(gear), float(heat_on), float(heat_off),
                       int(motor_gear), float(motor_on), float(motor_off))
            if profile not in replaced_profiles:
                merged_entries.append(((int(channel),) + profile +
                                       (float(adc),), line))
        merged_entries.extend(generated_entries)
        merged_entries.sort(key=lambda entry: entry[0])
        lines.extend(line for _, line in merged_entries)
    else:
        lines.extend(line for _, line in generated_entries)

    if output is not None:
        output.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"Wrote {fitted_groups} channel/profile groups to {output}")
    else:
        print(f"Fitted {fitted_groups} channel/profile groups (use --write to save)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--serial", type=Path, required=True)
    parser.add_argument("--config", type=Path,
                        default=Path(__file__).resolve().parents[4] / "agent-tp" /
                        "tools" / "soy_host" / "auto_heat_config.json")
    parser.add_argument("--write", action="store_true",
                        help="write the fitted C database include")
    parser.add_argument("--cooling-start-line", type=int,
                        help="ignore cooling samples before this 1-based CSV line")
    parser.add_argument("--merge-existing", action="store_true",
                        help="replace only fitted profiles and preserve other table profiles")
    args = parser.parse_args()
    try:
        config = json.loads(args.config.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        parser.error(f"cannot read config: {error}")
    if not isinstance(config, dict) or not isinstance(config.get("stages"), list):
        parser.error("config must contain a stages array")
    samples, stats = read_database_samples(
        args.csv, args.serial, config, args.cooling_start_line)
    print(
        "Sample filter: "
        f"NTC rows={stats['ntc_rows']}, "
        f"before first heating={stats['before_first_heating']}, "
        f"eligible={stats['eligible_profile_rows']}, "
        f"cooling before start line={stats['cooling_before_start_line']}, "
        f"rejected before 500 ms stop={stats['rejected_motor_not_stopped_500ms']}, "
        f"motor stopped >=500 ms={stats['motor_stopped_500ms']}, "
        f"reference unmatched={stats['rejected_reference_alignment']}, "
        f"accepted={stats['accepted_samples']}"
    )
    table_path = (Path(__file__).resolve().parents[2] / "tables" / "NTC3" /
                  "ntcTemp_machine3_operating_table.inc")
    output = table_path if args.write else None
    merge_existing = table_path if args.merge_existing else None
    emit_database(samples, output, merge_existing)


if __name__ == "__main__":
    main()
