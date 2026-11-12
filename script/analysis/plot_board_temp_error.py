# -*- coding: utf-8 -*-
"""绘制 真实温度/板级温度 趋势与误差曲线 (在 Windows Python 下运行, 需 matplotlib)"""
import csv
import math
from datetime import datetime
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager

CSV_PATH = r"D:\workspace\Code\Keil\NULLVE_V2\ESP32S3\testdata\ntcdata\20261027-125905-853024.csv"
OUT_PNG = r"D:\workspace\Code\Keil\NULLVE_V2\ESP32S3\output\board_temp_error_analysis.png"

HEAT_START = 400
COOL_START = 10161

# 中文字体
for f in ["Microsoft YaHei", "SimHei", "DengXian"]:
    if any(f.lower() in ff.name.lower() for ff in font_manager.fontManager.ttflist):
        plt.rcParams["font.family"] = f
        break
plt.rcParams["axes.unicode_minus"] = False

rows = []
with open(CSV_PATH, "r", encoding="utf-8-sig") as fp:
    reader = csv.reader(fp)
    next(reader)
    for lineno, r in enumerate(reader, start=2):  # 表头在第1行
        if r and len(r) >= 8 and r[1].strip() in ("真实温度", "ESP32板级温度"):
            try:
                real = float(r[2]); board = float(r[4])
                ts = datetime.fromisoformat(r[5].strip())
            except (ValueError, TypeError, Exception):
                continue
            rows.append((ts, real, board, float(r[6]) if r[6] else board - real,
                         lineno))

ts = [x[0] for x in rows]
real = [x[1] for x in rows]
board = [x[2] for x in rows]
err = [x[2] - x[1] for x in rows]
# 行号近似: 用索引映射
line_no = [x[4] for x in rows]

i_heat = next(i for i, ln in enumerate(line_no) if ln >= HEAT_START)
i_cool = next(i for i, ln in enumerate(line_no) if ln >= COOL_START)

fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 9), sharex=True,
                               gridspec_kw={"height_ratios": [2, 1.4]})

ax1.plot(ts, real, "-", color="tab:red", lw=1.2, label="真实温度(标准源)")
ax1.plot(ts, board, "-", color="tab:blue", lw=0.8, alpha=0.8, label="ESP32板级温度")
ax1.axvline(ts[i_cool], color="green", ls="--", lw=1.2, label="降温段起点 L10161 (13:25:47)")
ax1.axvline(ts[i_heat], color="orange", ls="--", lw=1.0, label="升温段起点 L400 (13:00:09)")
ax1.set_ylabel("温度 (℃)")
ax1.set_title("真实温度 vs ESP32板级温度 (2026-10-27 NTC测试)")
ax1.legend(loc="lower right", fontsize=9)
ax1.grid(alpha=0.3)

ax2.plot(ts, err, "-", color="tab:purple", lw=0.8)
ax2.axhline(0, color="k", lw=0.8)
ax2.axvline(ts[i_cool], color="green", ls="--", lw=1.2)
ax2.axvline(ts[i_heat], color="orange", ls="--", lw=1.0)
ax2.set_ylabel("误差 = 板级 - 真实 (℃)")
ax2.set_xlabel("时间")
ax2.set_title("板级温度误差曲线 (升温段 |err|≤2.3℃; 降温段高温区滞后达 -5.8℃)")
ax2.grid(alpha=0.3)

# 标注极值点
imax = max(range(len(err)), key=lambda i: err[i])
imin = min(range(len(err)), key=lambda i: err[i])
ax2.annotate(f"+{err[imax]:.2f}℃\nL{line_no[imax]}", xy=(ts[imax], err[imax]),
             xytext=(10, 10), textcoords="offset points", fontsize=8,
             color="darkred", arrowprops=dict(arrowstyle="->", color="darkred"))
ax2.annotate(f"{err[imin]:.2f}℃\nL{line_no[imin]}", xy=(ts[imin], err[imin]),
             xytext=(10, -20), textcoords="offset points", fontsize=8,
             color="darkblue", arrowprops=dict(arrowstyle="->", color="darkblue"))

plt.tight_layout()
plt.savefig(OUT_PNG, dpi=130)
print("saved:", OUT_PNG)
