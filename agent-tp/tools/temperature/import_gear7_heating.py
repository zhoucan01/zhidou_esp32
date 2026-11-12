"""Import measured gear-7 heating profiles, preserving other database groups."""
import argparse
import csv
import json
import re
import sys
from bisect import bisect_right
from collections import Counter, defaultdict
from datetime import datetime
from pathlib import Path
from statistics import mean

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'function/ntcTemp/scripts/calibration'))
sys.path.insert(0, str(ROOT / 'agent-tp/tools/soy_host'))
from calibrate import make_table, predict
from calibrate_machine3_database import interpolate_reference, stage_profile
from telemetry import crc16

TABLE = ROOT / 'function/ntcTemp/tables/NTC3/ntcTemp_machine3_operating_table.inc'
ENTRY = re.compile(r'^\s*\{(\d+)U, (\d+)U, (\d+)U, ([\d.]+)f, ([\d.]+)f, (-?\d+), ([\d.]+)f, ([\d.]+)f, (\d+)U, \{([\d.]+)f, ([\d.]+)f\}\},$')


def run(args):
    config = json.loads(args.config.read_text())
    events, rpms = [], []
    for line in args.serial.read_text(encoding='utf-8').splitlines():
        match = re.search(r'^(\S+).*?RX command MODBUS-RX:\[([0-9A-Fa-f ]+)\]', line)
        if not match:
            continue
        frame = bytes.fromhex(match[2])
        if len(frame) < 5 or crc16(frame[:-2]) != int.from_bytes(frame[-2:], 'little'):
            continue
        stamp = datetime.fromisoformat(match[1])
        if len(frame) == 8 and frame[:2] == bytes((3, 6)):
            events.append((stamp, int.from_bytes(frame[2:4], 'big'),
                           int.from_bytes(frame[4:6], 'big')))
        elif len(frame) == 53 and frame[:3] == bytes((3, 4, 48)):
            rpms.append((stamp, int.from_bytes(frame[3:5], 'big')))
    rpm_times = [x[0] for x in rpms]
    first7 = next(t for t, address, value in events if address == 2 and value == 7)
    with args.csv.open(encoding='utf-8-sig', newline='') as stream:
        rows = list(csv.DictReader(stream))
    references = sorted({(datetime.fromisoformat(row['真实温度接收时间']), float(row['真实温度℃']))
                         for row in rows if row['事件来源'] == '真实温度' and row['真实温度℃']})
    reference_times = [x[0] for x in references]
    samples, accepted = defaultdict(lambda: defaultdict(list)), []
    counts, seen = Counter(), set()
    event_index, motor, stopped, enabled, last_heat = 0, None, None, 0, None
    for row in rows:
        if row['事件来源'] != 'NTC':
            continue
        stamp = datetime.fromisoformat(row['NTC接收时间'])
        if stamp in seen:
            counts['duplicate_ntc'] += 1
            continue
        seen.add(stamp)
        counts['ntc_total'] += 1
        while event_index < len(events) and events[event_index][0] <= stamp:
            time, address, value = events[event_index]
            if address == 0:
                enabled = value
                if not value:
                    last_heat = None
                    motor, stopped = 0, time
            elif address == 2 and value:
                last_heat = value
            elif address == 4:
                if motor != value:
                    stopped = time if value == 0 else None
                motor = value
            event_index += 1
        mode = int(row['模式'])
        if stamp < first7 or not 1 <= mode <= len(config['stages']):
            counts['outside_heating_stage'] += 1
            continue
        profile = stage_profile(config['stages'][mode - 1])
        if profile[0] != 7 or not enabled or last_heat != 7:
            counts['not_gear7'] += 1
            continue
        counts['gear7_candidates'] += 1
        if motor != 0 or stopped is None or (stamp - stopped).total_seconds() < .5:
            counts['motor_not_settled'] += 1
            continue
        index = bisect_right(rpm_times, stamp) - 1
        if index < 0 or (stamp - rpms[index][0]).total_seconds() > .5 or rpms[index][1] != 0:
            counts['rpm_nonzero_or_stale'] += 1
            continue
        temp = interpolate_reference(references, reference_times, stamp)
        if temp is None:
            counts['reference_gap_over_2s'] += 1
            continue
        if temp < 90:
            counts['below_90c'] += 1
            continue
        adcs = [int(row[f'NTC{i}_ADC']) for i in (1, 2, 3)]
        if any(not 0 < adc < 4095 for adc in adcs):
            counts['invalid_adc'] += 1
            continue
        counts['accepted'] += 1
        accepted.append([stamp.isoformat(), mode, *profile, *adcs, temp])
        for channel, adc in enumerate(adcs):
            samples[(1, *profile)][channel].append((adc, temp))
    entries, metrics, replaced = [], [], set()
    for profile, channels in sorted(samples.items()):
        phase, gear, heat_on, heat_off, motor_gear, motor_on, motor_off = profile
        for channel, records in sorted(channels.items()):
            if len(records) < 10:
                raise ValueError(f'Insufficient samples: {profile}, channel {channel}, n={len(records)}')
            table = make_table(records)
            validation = make_table([x for i, x in enumerate(records) if i % 5])
            errors = [abs(predict(validation, adc) - temp)
                      for i, (adc, temp) in enumerate(records) if i % 5 == 0]
            key = (channel, *profile, args.water_ml)
            replaced.add(key)
            for adc, temp in table:
                line = (f'    {{{channel}U, {phase}U, {gear}U, {heat_on:.3f}f, {heat_off:.3f}f, '
                        f'{motor_gear}, {motor_on:.3f}f, {motor_off:.3f}f, {args.water_ml}U, '
                        f'{{{adc:.3f}f, {temp:.3f}f}}}},')
                entries.append(((*key, adc), line))
            metrics.append(dict(channel=channel + 1, profile=profile, samples=len(records),
                                nodes=len(table), adc_range=[table[0][0], table[-1][0]],
                                temperature_range=[min(t for _, t in records), max(t for _, t in records)],
                                holdout_mae=mean(errors), holdout_max=max(errors)))
    if not metrics:
        raise ValueError('No eligible gear-7 heating samples')
    before = TABLE.read_text()
    preserved = 0
    for line in before.splitlines():
        match = ENTRY.match(line)
        if not match:
            if line.strip() and not line.startswith('/*'):
                raise ValueError(f'Unrecognized database row: {line}')
            continue
        values = [float(v) for v in match.groups()]
        key = tuple(values[:9])
        if key not in replaced:
            entries.append(((*key, values[9]), line))
            preserved += 1
    entries.sort(key=lambda item: item[0])
    args.output.mkdir(parents=True, exist_ok=True)
    candidate = args.output / 'gear7_heating_table.inc'
    candidate.write_text(before.splitlines()[0] + '\n' + '\n'.join(line for _, line in entries) + '\n')
    with (args.output / 'accepted_samples.csv').open('w', newline='', encoding='utf-8-sig') as stream:
        writer = csv.writer(stream)
        writer.writerow(['NTC接收时间', '模式', '加热档位', '加热开秒', '加热停秒', '电机档位',
                         '电机开秒', '电机停秒', 'NTC1_ADC', 'NTC2_ADC', 'NTC3_ADC', '对齐真实温度℃'])
        writer.writerows(accepted)
    result = dict(csv=str(args.csv), serial=str(args.serial), water_ml=args.water_ml,
                  first_gear7_ack=first7.isoformat(), counts=dict(counts), metrics=metrics,
                  preserved_nodes=preserved, added_nodes=sum(m['nodes'] for m in metrics))
    (args.output / 'metrics.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    if args.write:
        TABLE.write_text(candidate.read_text())
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--serial', type=Path, required=True)
    parser.add_argument('--water-ml', type=int, required=True)
    parser.add_argument('--config', type=Path, default=ROOT / 'agent-tp/tools/soy_host/auto_heat_process2.json')
    parser.add_argument('--output', type=Path, default=ROOT / 'agent-tp/reports/ntc/2026-10-29_gear7_heating')
    parser.add_argument('--write', action='store_true')
    run(parser.parse_args())
