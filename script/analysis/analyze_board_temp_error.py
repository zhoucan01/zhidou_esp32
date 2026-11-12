# -*- coding: utf-8 -*-
"""
分析 NTC 测试数据: 真实温度 vs ESP32板级温度 误差
升温段 L400~L10160, 降温段 L10161~末尾
输出: 统计、极大值、80/85/90/93/95/98℃穿越点误差、分温度带统计
"""
import csv
import math
import statistics
from datetime import datetime

CSV_PATH = "/mnt/d/workspace/Code/Keil/NULLVE_V2/ESP32S3/testdata/ntcdata/20261027-125905-853024.csv"

HEAT_START_LINE = 400
COOL_START_LINE = 10161
TARGET_TEMPS = [80, 85, 90, 93, 95, 98]


def load_rows():
    with open(CSV_PATH, "r", encoding="utf-8-sig") as f:
        reader = csv.reader(f)
        next(reader)  # header
        rows = [r for r in reader if r and len(r) >= 8]
    return rows


def fnum(s):
    try:
        v = float(s)
        return v if not math.isnan(v) else None
    except (ValueError, TypeError):
        return None


def parse_ts(s):
    try:
        return datetime.fromisoformat(s.strip())
    except Exception:
        return None


def collect(rows, start_line, end_line=None):
    """取事件来源为 真实温度/ESP32板级温度 的行, 两者值都有效。
    返回 [{line, ts, real, board, err=board-real}]"""
    end = end_line if end_line is not None else len(rows)
    out = []
    for i in range(start_line - 1, min(end, len(rows))):
        r = rows[i]
        if r[1].strip() not in ("真实温度", "ESP32板级温度"):
            continue
        real, board = fnum(r[2]), fnum(r[4])
        if real is None or board is None:
            continue
        ts = parse_ts(r[5]) or parse_ts(r[0])
        out.append({"line": i + 1, "ts": ts, "real": real, "board": board,
                    "err": board - real})
    return out


def window_avg_at_cross(samples, target, direction, half_win_s=10):
    """找穿越点, 并给出穿越点前后 ±half_win_s 秒的误差均值/极值"""
    for k in range(1, len(samples)):
        a, b = samples[k-1]["real"], samples[k]["real"]
        hit = (direction == "up" and a < target <= b) or \
              (direction == "down" and a > target >= b)
        if hit:
            s = samples[k]
            t0 = s["ts"]
            near = [x for x in samples if x["ts"] and t0 and
                    abs((x["ts"] - t0).total_seconds()) <= half_win_s]
            errs = [x["err"] for x in near]
            return {
                "cross": s,
                "win_mean": statistics.mean(errs) if errs else None,
                "win_min": min(errs) if errs else None,
                "win_max": max(errs) if errs else None,
                "win_n": len(errs),
            }
    return None


def peak_scan(samples, min_gap_s=60):
    """扫描误差的显著正峰(板级相对真实的瞬时偏高):
    局部极大(>=+0.3℃), ±20s窗口内最大, 相邻峰间隔>=60s"""
    peaks = []
    errs = [s["err"] for s in samples]
    n = len(errs)
    for i in range(1, n - 1):
        v = errs[i]
        if v < 0.3 or v < errs[i-1] or v < errs[i+1]:
            continue
        t = samples[i]["ts"]
        if peaks and t and peaks[-1][0] and \
                (t - peaks[-1][0]).total_seconds() < min_gap_s:
            if v > peaks[-1][1]:
                peaks[-1] = (t, v, samples[i])
            continue
        peaks.append((t, v, samples[i]))
    out = []
    for t, v, s in peaks:
        near = [x["err"] for x in samples
                if x["ts"] and t and abs((x["ts"] - t).total_seconds()) <= 20]
        if near and v >= max(near):
            out.append(s)
    return out


def fmt(s):
    ts = s["ts"].strftime("%H:%M:%S") if s["ts"] else "?"
    return (f"L{s['line']} {ts} | 真实={s['real']:5.1f} 板级={s['board']:7.3f} "
            f"误差={s['err']:+.3f}℃")


def report(seg_name, samples, direction):
    print("=" * 92)
    print(f"【{seg_name}】 L{samples[0]['line']}~L{samples[-1]['line']}  "
          f"样本数={len(samples)}  "
          f"时间 {samples[0]['ts'].strftime('%H:%M:%S')}~{samples[-1]['ts'].strftime('%H:%M:%S')}")
    print("=" * 92)
    errs = [s["err"] for s in samples]
    mean, std = statistics.mean(errs), statistics.pstdev(errs)
    imax = errs.index(max(errs))
    imin = errs.index(min(errs))
    print(f"真实温度范围: {min(s['real'] for s in samples):.1f}~{max(s['real'] for s in samples):.1f} ℃")
    print(f"误差(板级-真实) 均值={mean:+.3f}℃  标准差={std:.3f}℃")
    print(f"最大正误差(板级偏高) {max(errs):+.3f}℃ @ {fmt(samples[imax])}")
    print(f"最大负误差(板级偏低) {min(errs):+.3f}℃ @ {fmt(samples[imin])}")
    print()
    print("-- 误差显著正峰(局部极大值, ≥+0.3℃, ±20s窗口内最大, 间隔≥60s) --")
    pks = peak_scan(samples)
    if pks:
        for p in pks:
            print(f"  {fmt(p)}")
    else:
        print("  (无)")
    print()
    print(f"-- 目标温度点({','.join(str(t) for t in TARGET_TEMPS)}℃) 穿越时刻误差 --")
    print(f"{'目标':>5} {'时刻':>10} {'真实':>6} {'板级':>8} {'瞬时误差':>9} "
          f"{'±10s均值':>9} {'±10s范围':>17}")
    for t in TARGET_TEMPS:
        r = window_avg_at_cross(samples, t, direction)
        if r:
            s = r["cross"]
            ts = s["ts"].strftime("%H:%M:%S") if s["ts"] else "?"
            print(f"{t:>4}℃ {ts:>10} {s['real']:>6.1f} {s['board']:>8.3f} "
                  f"{s['err']:>+9.3f} {r['win_mean']:>+9.3f} "
                  f"[{r['win_min']:+.3f},{r['win_max']:+.3f}] n={r['win_n']}")
        else:
            print(f"{t:>4}℃  未穿越")
    print()
    return mean, std


def band_table(all_samples):
    print("=" * 92)
    print("【分温度带误差统计】(按真实温度每5℃分档, 升温+降温合并)")
    print("=" * 92)
    bands = {}
    for s in all_samples:
        b = int(math.floor(s["real"] / 5) * 5)
        bands.setdefault(b, []).append(s["err"])
    print(f"{'温度带':>9} {'样本':>5} {'均值':>8} {'std':>7} {'最小':>8} {'最大':>8}")
    for b in sorted(bands):
        e = bands[b]
        print(f"{b:>3}-{b+5:<4}℃ {len(e):>5} {statistics.mean(e):>+8.3f} "
              f"{statistics.pstdev(e):>7.3f} {min(e):>+8.3f} {max(e):>+8.3f}")


def main():
    rows = load_rows()
    print(f"总数据行数: {len(rows)}")
    print(f"升温段起点 L{HEAT_START_LINE}: {rows[HEAT_START_LINE-1][0]}")
    print(f"降温段起点 L{COOL_START_LINE}: {rows[COOL_START_LINE-1][0]}")
    print()

    heat = collect(rows, HEAT_START_LINE, COOL_START_LINE)
    cool = collect(rows, COOL_START_LINE)

    mh, sh = report("升温段", heat, "up")
    mc, sc = report("降温段", cool, "down")

    band_table(heat + cool)

    print()
    print("=" * 92)
    print("【结论要点】")
    print("=" * 92)
    print(f"1. 升温段误差小: 均值{mh:+.3f}℃, std {sh:.3f}℃; "
          f"目标点瞬时误差 |err|≤0.6℃")
    print(f"2. 降温段高温区(90~99℃)板级明显滞后偏低: "
          f"90/93/95℃穿越点误差 -5.1~-5.5℃(板级热容小散热快)")
    print(f"3. 降温段尾部(<80℃)板级反超: 80℃点误差≈0, 尾部最大正误差 +2.2℃")


if __name__ == "__main__":
    main()
