"""Calibrate machine 2 from an acknowledged heating session's host logs."""
import argparse
import csv
import re
from datetime import datetime
from pathlib import Path
from statistics import mean
from calibrate import filter_and_fit, predict


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    log = args.csv.with_name(args.csv.stem + '-serial.txt').read_text(encoding='utf-8')
    starts = re.findall(r'^(\S+) RX command NB\+SOYMODE=1,1 OK', log, re.M)
    if not starts: raise ValueError('No acknowledged heating start')
    start = datetime.fromisoformat(starts[0])
    stops = [datetime.fromisoformat(s) for s in re.findall(r'^(\S+) TX NB\+SOYMODE=0,0', log, re.M) if datetime.fromisoformat(s) > start]
    if not stops: raise ValueError('No stop command after heating start')
    end = min(stops) # exclude stop command onwards, including response delay
    with args.csv.open(encoding='utf-8-sig', newline='') as stream:
        rows = list(csv.DictReader(stream))
    samples = []
    seen = set()
    for r in rows:
        if r['事件来源'] != 'NTC' or not r['真实温度℃'] or not r['NTC2_ADC']: continue
        time = datetime.fromisoformat(r['记录时间'])
        if not start <= time < end: continue
        real_time = datetime.fromisoformat(r['真实温度接收时间'])
        ntc_time = datetime.fromisoformat(r['NTC接收时间'])
        temp, adc = float(r['真实温度℃']), int(r['NTC2_ADC'])
        if not 0 <= temp <= 150 or adc <= 0 or abs((ntc_time-real_time).total_seconds()) > 1: continue
        if r['NTC接收时间'] in seen: continue
        seen.add(r['NTC接收时间']); samples.append((adc, temp))
    table, retained, passes = filter_and_fit(samples, 20)
    print(f'Heating: {start} to {end}; samples={len(samples)}, retained={len(retained)}, points={len(table)}')
    print(f'ADC={table[0][0]}..{table[-1][0]}, fitted temperature={table[-1][1]}..{table[0][1]}')
    errors=[]
    for i in retained:
        a,t=samples[i]; p=predict(table,a)
        p=98 if 97.9 <= p < 98.0 else p
        errors.append(abs(p-t))
    print(f'Runtime calibration residual MAE={mean(errors):.3f}, max={max(errors):.3f}; removed={len(samples)-len(retained)}')
    if args.write:
        lines=['/* Machine 2: host session 20261014-202806-675654; heating only. */']
        lines += [f'    {{{a:.3f}f, {t:.3f}f}},' for a,t in table]
        output = Path(__file__).resolve().parents[2] / 'tables' / 'NTC2' / 'ntcTemp_machine2_table.inc'
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text('\n'.join(lines)+'\n',encoding='utf-8')


if __name__=='__main__': main()
