"""Analyze an event-based soy-host capture without changing calibration tables.

Dependencies: matplotlib for plots; optionally install under build/analysis_deps
and run with PYTHONPATH=build/analysis_deps. Intermediate results stay in build/.
"""
import argparse
from bisect import bisect_left, bisect_right
from collections import Counter, defaultdict
import csv
from datetime import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
from statistics import mean, median, pstdev
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "function/ntcTemp/scripts/calibration"))
from calibrate import make_table, predict


def number(s):
    try:
        value = float(s)
        return value if math.isfinite(value) else None
    except (TypeError, ValueError):
        return None


def quantile(values, q):
    a = sorted(values)
    x = (len(a) - 1) * q
    lo = int(x)
    return a[lo] + (a[min(lo + 1, len(a) - 1)] - a[lo]) * (x - lo)


def metrics(errors):
    if not errors:
        return {"n": 0}
    absolute = [abs(e) for e in errors]
    return dict(n=len(errors), bias=mean(errors), mae=mean(absolute),
                rmse=math.sqrt(mean(e * e for e in errors)),
                p95=quantile(absolute, .95), maximum=max(absolute),
                minimum=min(errors), maximum_signed=max(errors), sd=pstdev(errors),
                within05=100 * mean(abs(e) <= .5 for e in errors),
                within1=100 * mean(abs(e) <= 1 for e in errors))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    rows = list(csv.DictReader(args.csv.open(encoding="utf-8-sig")))
    for line, row in enumerate(rows, 2):
        row["line"] = line
    def stamp(row, key):
        return datetime.fromisoformat(row[key]).timestamp()
    refs = sorted({stamp(r, "真实温度接收时间"): number(r["真实温度℃"])
                   for r in rows if r["事件来源"] == "真实温度" and
                   number(r["真实温度℃"]) is not None and
                   0 <= number(r["真实温度℃"]) <= 150}.items())
    ref_times = [t for t, _ in refs]
    start = stamp(rows[0], "记录时间")
    heat_start = next(stamp(r, "记录时间") for r in rows if int(r["模式"]) > 0)
    last_heating = max(stamp(r, "记录时间") for r in rows if int(r["模式"]) > 0)
    cool_start = next(stamp(r, "记录时间") for r in rows
                      if stamp(r, "记录时间") > last_heating)

    def reference(t, max_gap=2):
        i = bisect_left(ref_times, t)
        if i < len(refs) and ref_times[i] == t:
            return refs[i][1]
        if i == 0 or i == len(refs):
            return None
        a, b = refs[i - 1], refs[i]
        if b[0] - a[0] > max_gap:
            return None
        return a[1] + (b[1] - a[1]) * (t - a[0]) / (b[0] - a[0])

    def phase(t):
        return "startup" if t < heat_start else "heat" if t < cool_start else "cool"

    series = defaultdict(list)
    for name, event, col, clock in [
        ("board", "ESP32板级温度", "ESP32板级温度℃", "板级温度接收时间"),
        *[(f"NTC{ch}", "NTC查表温度", f"NTC{ch}查表温度℃", "NTC计算温度接收时间")
          for ch in (1, 2, 3)],
    ]:
        seen = set()
        for r in rows:
            value = number(r[col])
            if r["事件来源"] != event or value is None or not r[clock]:
                continue
            t = stamp(r, clock)
            if t in seen:
                continue
            seen.add(t)
            series[name].append(dict(t=t, value=value, ref=reference(t),
                                     phase=phase(t), mode=int(r["模式"]), line=r["line"]))
        series[name].sort(key=lambda p: p["t"])

    groups = {}
    for name, points in series.items():
        sets = {
            "heat_all": lambda p: p["phase"] == "heat",
            "heat_60plus": lambda p: p["phase"] == "heat" and p["ref"] >= 60,
            "heat_80plus": lambda p: p["phase"] == "heat" and p["ref"] >= 80,
            "heat_85plus": lambda p: p["phase"] == "heat" and p["ref"] >= 85,
            "cool_all": lambda p: p["phase"] == "cool",
            "cool_after120s": lambda p: p["t"] >= cool_start + 120,
        }
        groups[name] = {key: metrics([p["value"] - p["ref"] for p in points
                         if p["ref"] is not None and p["value"] > 0 and fn(p)])
                        for key, fn in sets.items()}
    by_mode = {str(mode): metrics([p["value"] - p["ref"] for p in series["board"]
                if p["ref"] is not None and p["mode"] == mode and p["phase"] == "heat"])
               for mode in range(1, 8)}
    bands = {}
    for ph in ("heat", "cool"):
        for low, high in [(35, 60), (60, 80), (80, 85), (85, 90), (90, 95), (95, 101)]:
            bands[f"{ph}_{low}_{high}"] = metrics([p["value"] - p["ref"]
                for p in series["board"] if p["ref"] is not None and
                p["phase"] == ph and low <= p["ref"] < high])

    # A residual's local linear trend is removed to distinguish drift from jitter.
    stability = {}
    for ph in ("heat", "cool"):
        pts = [p for p in series["board"] if p["phase"] == ph and
               p["ref"] is not None and p["value"] > 0]
        windows = defaultdict(list)
        for p in pts:
            windows[int((p["t"] - start) // 30)].append(p)
        deviations = []
        for win in windows.values():
            if len(win) < 10 or win[-1]["t"] - win[0]["t"] < 15:
                continue
            xs = [p["t"] - win[0]["t"] for p in win]
            ys = [p["value"] - p["ref"] for p in win]
            mx, my = mean(xs), mean(ys)
            slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx)**2 for x in xs)
            deviations.append(pstdev([y - my - slope * (x - mx) for x, y in zip(xs, ys)]))
        # Consecutive near-1-second jumps, and flat-value runs.
        observed = [p for p in series["board"] if p["phase"] == ph and p["value"] > 0]
        steps = [abs(b["value"] - a["value"]) for a, b in zip(observed, observed[1:])
                 if .5 <= b["t"] - a["t"] <= 1.5]
        holds = []
        run = []
        for p in [p for p in series["board"] if p["phase"] == ph]:
            if run and (abs(p["value"] - run[-1]["value"]) > .001 or
                        p["t"] - run[-1]["t"] > 2):
                if len(run) > 1:
                    holds.append(run)
                run = []
            run.append(p)
        if len(run) > 1:
            holds.append(run)
        longest = max(holds, key=lambda r: r[-1]["t"] - r[0]["t"], default=[])
        stability[ph] = dict(windows=len(deviations),
            jitter_sd_median=median(deviations) if deviations else None,
            jitter_sd_p95=quantile(deviations, .95) if deviations else None,
            steps_n=len(steps), step_p95=quantile(steps, .95) if steps else None,
            step_max=max(steps, default=None), steps_over1=sum(s > 1 for s in steps),
            holds_over10s=sum(r[-1]["t"] - r[0]["t"] >= 10 for r in holds),
            longest_hold=dict(seconds=longest[-1]["t"] - longest[0]["t"],
                start=datetime.fromtimestamp(longest[0]["t"]).isoformat(),
                value=longest[0]["value"],
                reference_change=(reference(longest[-1]["t"], 10) -
                                  reference(longest[0]["t"], 10))
                if reference(longest[-1]["t"], 10) is not None and
                   reference(longest[0]["t"], 10) is not None else None) if longest else None)

    rpms = sorted((stamp(r, "站3快照接收时间"), float(r["站3电机转速RPM"]))
                  for r in rows if r["事件来源"] == "站3电机转速" and r["站3快照接收时间"]
                  and number(r["站3电机转速RPM"]) is not None)
    rpm_times = [t for t, _ in rpms]
    stop_start = None
    stop_durations = []
    previous = None
    for t, rpm in rpms:
        if rpm != 0 or previous is None or t - previous > .5:
            stop_start = t if rpm == 0 else None
        elif stop_start is None:
            stop_start = t
        stop_durations.append(t - stop_start if stop_start is not None else -1)
        previous = t

    raw_groups = defaultdict(list)
    raw_counts = Counter()
    for r in rows:
        if r["事件来源"] != "NTC":
            continue
        t = stamp(r, "NTC接收时间")
        if phase(t) == "startup":
            continue
        raw_counts["total"] += 1
        i = bisect_right(rpm_times, t) - 1
        if i < 0 or t - rpm_times[i] > .5 or rpms[i][1] != 0 or stop_durations[i] < .5:
            raw_counts["motor_rejected"] += 1
            continue
        ref = reference(t)
        if ref is None:
            raw_counts["reference_rejected"] += 1
            continue
        raw_counts["accepted"] += 1
        for ch in (1, 2, 3):
            adc = number(r[f"NTC{ch}_ADC"])
            if adc is not None and 0 < adc < 4095:
                raw_groups[(phase(t), ch)].append((t, adc, ref))
    fits = {}
    for (ph, ch), records in raw_groups.items():
        samples = [(a, temp) for t, a, temp in records]
        try:
            table = make_table(samples)
        except ValueError:
            continue
        result = dict(n=len(samples), nodes=len(table), adc_min=table[0][0], adc_max=table[-1][0],
                      resubstitution=metrics([predict(table, a) - temp for a, temp in samples]))
        interleaved = []
        blocked = []
        for fold in range(5):
            train_i = [(a, temp) for i, (a, temp) in enumerate(samples) if i % 5 != fold]
            train_b = [(a, temp) for i, (a, temp) in enumerate(samples)
                       if min(4, i * 5 // len(samples)) != fold]
            for training, target, select in [
                (train_i, interleaved, lambda i: i % 5 == fold),
                (train_b, blocked, lambda i: min(4, i * 5 // len(samples)) == fold),
            ]:
                try:
                    fitted = make_table(training)
                except ValueError:
                    continue
                target.extend(predict(fitted, a) - temp for i, (a, temp) in enumerate(samples)
                              if select(i) and fitted[0][0] <= a <= fitted[-1][0])
        result["interleaved"] = metrics(interleaved)
        result["blocked"] = metrics(blocked)
        fits[f"{ph}_NTC{ch}"] = result

    serial = args.csv.with_name(args.csv.stem + "-serial.txt")
    recovery = []
    controls = Counter()
    if serial.exists():
        for line in serial.read_text(encoding="utf-8").splitlines():
            if "Recover station 3" in line:
                match = re.search(r'address=0x([0-9A-Fa-f]+) target=(-?\d+)', line)
                recovery.append(dict(t=line[:23], address=match[1] if match else "?",
                                     target=int(match[2]) if match else None))
            match = re.search(r' TX MODBUS-TX:\[03 06 ([0-9A-Fa-f ]+)\]', line)
            if match:
                data = bytes.fromhex(match[1])
                controls[(int.from_bytes(data[:2], "big"), int.from_bytes(data[2:4], "big"))] += 1
    sensitivity = {}
    for max_gap in (2, 5, 10):
        sensitivity[str(max_gap)] = {}
        for ph in ("heat", "cool"):
            errors = []
            for p in series["board"]:
                ref = reference(p["t"], max_gap)
                if p["phase"] == ph and ref is not None and p["value"] > 0:
                    errors.append(p["value"] - ref)
            sensitivity[str(max_gap)][ph] = metrics(errors)
    result = dict(source=str(args.csv), sha256=hashlib.sha256(args.csv.read_bytes()).hexdigest(),
        rows=len(rows), events=dict(Counter(r["事件来源"] for r in rows)),
        start=rows[0]["记录时间"], end=rows[-1]["记录时间"],
        heat_start=datetime.fromtimestamp(heat_start).isoformat(),
        cool_start=datetime.fromtimestamp(cool_start).isoformat(),
        reference_gaps_over2=sum(b - a > 2 for a, b in zip(ref_times, ref_times[1:])),
        reference_max_gap=max(b - a for a, b in zip(ref_times, ref_times[1:])),
        groups=groups, modes=by_mode, bands=bands, stability=stability,
        fit_selection=dict(raw_counts), fits=fits, recovery=recovery,
        commands={f"{a:04X}:{v:04X}": n for (a, v), n in controls.items()},
        alignment_sensitivity=sensitivity)
    summary = args.out_dir / (args.csv.stem + "_summary.json")
    summary.write_text(json.dumps(result, ensure_ascii=False, indent=2))
    with (args.out_dir / (args.csv.stem + "_aligned.csv")).open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["source", "time", "phase", "mode", "temperature", "reference", "error", "original_line"])
        for name, points in series.items():
            for p in points:
                writer.writerow([name, datetime.fromtimestamp(p["t"]).isoformat(), p["phase"], p["mode"],
                    p["value"], p["ref"], p["value"] - p["ref"] if p["ref"] is not None else "", p["line"]])

    os.environ.setdefault("MPLCONFIGDIR", str(ROOT / "build" / "matplotlib"))
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(3, 1, figsize=(13, 10), sharex=True, constrained_layout=True)
    axes[0].plot([(t-start)/60 for t, v in refs], [v for t, v in refs], color="black", label="Reference", lw=1.4)
    for name, color in [("board", "tab:blue"), ("NTC2", "tab:orange"), ("NTC3", "tab:green")]:
        pts = [p for p in series[name] if p["value"] > 0]
        axes[0].plot([(p["t"]-start)/60 for p in pts], [p["value"] for p in pts], label=name, color=color, lw=.8)
        aligned = [p for p in pts if p["ref"] is not None]
        # Dots intentionally do not connect across missing-reference intervals.
        axes[1].plot([(p["t"]-start)/60 for p in aligned],
                     [p["value"]-p["ref"] for p in aligned], '.', ms=1.5, label=name, color=color)
    axes[1].axhline(0, color="black", lw=.7)
    axes[1].axhspan(-1, 1, color="green", alpha=.1)
    axes[2].plot([(t-start)/60 for t, rpm in rpms], [rpm for t, rpm in rpms], lw=.5, color="tab:purple")
    for ax in axes:
        ax.axvline((heat_start-start)/60, color="red", ls="--", lw=.8)
        ax.axvline((cool_start-start)/60, color="navy", ls="--", lw=.8)
        ax.grid(alpha=.25)
    axes[0].set_ylabel("Temperature (C)")
    axes[1].set_ylabel("Error vs reference (C)")
    axes[2].set_ylabel("Station 3 RPM")
    axes[2].set_xlabel("Minutes since capture start")
    axes[0].legend(ncol=4)
    axes[1].legend(ncol=3)
    fig.suptitle(f"{args.csv.stem}: recorded temperatures, aligned error and motor RPM")
    fig.savefig(args.out_dir / (args.csv.stem + "_temperature.png"), dpi=160)
    plt.close(fig)
    fig, axes = plt.subplots(1, 3, figsize=(15, 4.5), constrained_layout=True)
    for ch, ax in enumerate(axes, 1):
        for ph, color in [("heat", "tab:red"), ("cool", "tab:blue")]:
            records = raw_groups.get((ph, ch), [])
            if not records:
                continue
            ax.scatter([a for t, a, temp in records], [temp for t, a, temp in records],
                       s=4, alpha=.4, color=color, label=ph + " samples")
            table = make_table([(a, temp) for t, a, temp in records])
            ax.plot([a for a, temp in table], [temp for a, temp in table],
                    color=color, lw=1.5, label=ph + " PAVA fit")
        ax.set_title(f"NTC{ch}: stopped-motor raw samples")
        ax.set_xlabel("Raw ADC")
        ax.set_ylabel("Reference temperature (C)")
        ax.grid(alpha=.25)
        ax.legend(fontsize=8)
    fig.savefig(args.out_dir / (args.csv.stem + "_fit.png"), dpi=160)
    plt.close(fig)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
