"""Reproduce NTC comparison, plots and a standalone HTML report."""
import argparse
import base64
import html
from datetime import datetime
from pathlib import Path
from statistics import mean
import math

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "calibration"))

from calibrate import read_pair, make_table, filter_and_fit, predict


def metrics(errors):
    absolute = sorted(abs(e) for e in errors)
    return (len(errors), mean(absolute), math.sqrt(mean(e*e for e in errors)),
            mean(errors), absolute[int((len(absolute)-1)*.95)], max(absolute),
            100*sum(e <= 1 for e in absolute)/len(absolute))


def metric_row(label, errors):
    n, mae, rmse, bias, p95, maximum, within = metrics(errors)
    return f"<tr><td>{html.escape(label)}</td><td>{n}</td>" + "".join(f"<td>{v:.3f}</td>" for v in (mae, rmse, bias, p95, maximum)) + f"<td>{within:.2f}%</td></tr>"


def metric_table(rows):
    return "<table><thead><tr><th>比较对象</th><th>样本数</th><th>MAE ℃</th><th>RMSE ℃</th><th>平均有符号误差 ℃</th><th>P95 ℃</th><th>最大绝对误差 ℃</th><th>误差≤1℃比例</th></tr></thead><tbody>" + "".join(rows) + "</tbody></table>"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    output = Path(__file__).resolve().parents[4] / "agent-tp/reports/temperature"
    output.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({"font.sans-serif": ["Microsoft YaHei", "SimHei", "DejaVu Sans"], "axes.unicode_minus": False})
    groups = []
    for number in (1, 2):
        channels = [read_pair(args.directory/f"{number}.csv", args.directory/f"{number}.txt", "second", c, True) for c in (1, 2)]
        maps = [{stamp: (adc, temp) for stamp, adc, temp in records} for records in channels]
        common = sorted(maps[0].keys() & maps[1].keys())
        assert all(maps[0][t][1] == maps[1][t][1] for t in common)
        groups.append([(t, maps[0][t][0], maps[1][t][0], maps[0][t][1]) for t in common])
    all_records = [r for g in groups for r in g]
    samples = [[(r[c], r[3]) for r in all_records] for c in (1, 2)]
    raw_tables = [make_table(s) for s in samples]
    fits = [filter_and_fit(s, 20) for s in samples]
    tables = [f[0] for f in fits]
    keeps = [set(f[1]) for f in fits]
    shared = keeps[0] & keeps[1]
    errors = [[predict(tables[c-1], r[c])-r[3] for r in all_records] for c in (1, 2)]
    raw_errors = [[predict(raw_tables[c-1], r[c])-r[3] for r in all_records] for c in (1, 2)]
    raw_rows, clean_rows, common_rows, cv_rows = [], [], [], []
    figures = []
    offset = 0
    for number, group in enumerate(groups, 1):
        indices = list(range(offset, offset+len(group)))
        for c in (0, 1):
            label = f"组{number} NTC{c+1}"
            raw_rows.append(metric_row(label, [raw_errors[c][i] for i in indices]))
            clean_rows.append(metric_row(label, [errors[c][i] for i in indices if i in keeps[c]]))
            common_rows.append(metric_row(label, [errors[c][i] for i in indices if i in shared]))
        x = [datetime.strptime(r[0], "%H:%M:%S") for r in group]
        fig, axes = plt.subplots(2, 1, figsize=(13, 8), sharex=True, gridspec_kw={"height_ratios": [2, 1]})
        axes[0].plot(x, [r[3] for r in group], color="#151b26", linewidth=2, label="实际温度（TXT）")
        for c, color in ((0, "#d85c24"), (1, "#087fb4")):
            predictions = [predict(tables[c], r[c+1]) for r in group]
            axes[0].plot(x, [p if i in keeps[c] else float("nan") for p, i in zip(predictions, indices)], color=color, linewidth=1.3, label=f"NTC{c+1}预测温度（过滤后重拟合）")
            axes[1].plot(x, [errors[c][i] if i in keeps[c] else float("nan") for i in indices], color=color, linewidth=1, label=f"NTC{c+1}预测－实际")
            removed = [(time, p, i) for time, p, i in zip(x, predictions, indices) if i not in keeps[c]]
            if removed:
                axes[0].scatter([v[0] for v in removed], [v[1] for v in removed], s=10, marker="x", color=color, alpha=.5, label=f"NTC{c+1}已剔除点（展示重拟合预测，不计入过滤后指标）")
        axes[1].axhline(0, color="black", linewidth=.7)
        axes[0].set_ylabel("温度（℃）")
        axes[1].set_ylabel("有符号误差（℃）")
        axes[1].set_xlabel("同一秒时间戳")
        axes[1].xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S"))
        for ax in axes:
            ax.grid(alpha=.2)
            ax.legend(fontsize=8, loc="best")
        axes[0].set_title(f"第{number}组：实际温度与 NTC1 / NTC2 预测温度对比（{len(group)} 个共同时间点）")
        fig.tight_layout()
        path = output/f"group{number}_temperature_comparison.png"
        fig.savefig(path, dpi=180)
        plt.close(fig)
        figures.append(f'<figure><img src="data:image/png;base64,{base64.b64encode(path.read_bytes()).decode()}"><figcaption>组{number}：全部共同时间点。折线断点为被剔除的样本；叉号保留其重拟合预测位置，避免隐藏异常。</figcaption></figure>')
        offset += len(group)
    for train, test in ((0, 1), (1, 0)):
        for channel in (1, 2):
            training = [(r[channel], r[3]) for r in groups[train]]
            table, retained, passes = filter_and_fit(training, 20)
            testing = [predict(table, r[channel])-r[3] for r in groups[test]]
            outside = sum(not table[0][0] <= r[channel] <= table[-1][0] for r in groups[test])
            cv_rows.append(metric_row(f"组{train+1}训练→组{test+1}验证 NTC{channel}（训练剔除{len(training)-len(retained)}；验证超表范围{outside}）", testing))
    m1 = metrics([errors[0][i] for i in sorted(shared)])
    m2 = metrics([errors[1][i] for i in sorted(shared)])
    reduction = 100*(1-m2[1]/m1[1])
    report = f"""<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>NTC1 与 NTC2 温度标定对比报告</title>
<style>body{{font-family:'Microsoft YaHei',sans-serif;max-width:1150px;margin:36px auto;padding:0 24px;color:#18232f;line-height:1.65}}h1,h2{{color:#163d56}}table{{border-collapse:collapse;width:100%;font-size:13px;margin:20px 0}}td,th{{padding:9px;border:1px solid #d9e1e6;text-align:right}}td:first-child,th:first-child{{text-align:left}}th{{background:#edf4f8}}img{{width:100%}}figure{{margin:24px 0}}figcaption{{font-size:13px;color:#596773}}.result{{background:#edf7fb;padding:18px;border-left:4px solid #087fb4}}@media print{{body{{margin:0;max-width:none}}figure,table{{break-inside:avoid}}}}</style>
<h1>NTC1 与 NTC2 温度标定对比报告</h1><p>日期：2026-10-11。数据来源：1.csv / 1.txt、2.csv / 2.txt。</p>
<div class="result"><strong>结论：在本次两组记录的实测温区内，采用相同的 ADC 单变量查表方法，NTC2 比 NTC1 更适合估算 TXT 所测的真实温度。</strong>
<p>对两路都保留的 {len(shared)} 个相同时间点，NTC1 平均绝对误差 {m1[1]:.3f}℃，NTC2 为 {m2[1]:.3f}℃，降低 {reduction:.1f}%。NTC2 无需剔除任何超过 20℃ 的点；NTC1 共剔除 {len(all_records)-len(keeps[0])} 个点，占 {100*(len(all_records)-len(keeps[0]))/len(all_records):.2f}%。</p></div>
<h2>数据范围与统一处理规则</h2><p>TXT 只保留 0≤T≤150℃ 的真实温度，共剔除 789 条无效读数。实际有效温度范围为 38.5～98.2℃。
CSV 读取站2 NTC1（0x0000～0x0001）和 NTC2（0x0041～0x0042）原始 ADC，以站2最近接收时间和 TXT 按时分秒配对，重复快照去重，同秒读数取平均。
两路共同配对样本为组1 {len(groups[0])} 个、组2 {len(groups[1])} 个，总计 {len(all_records)} 个。</p>
<p>两路均按 32 ADC 宽度分箱，温度取中位数，并以样本数加权做单调递减拟合，运行时线性插值。异常定义为 |预测温度－真实温度|&gt;20℃，恰好 20℃ 保留。
先用全部有效配对点拟合，再剔除超过阈值的点并重新拟合，迭代至剩余点均满足阈值。本次 NTC1 过滤 {fits[0][2]} 轮，NTC2 过滤 {fits[1][2]} 轮。
该规则需要 TXT 参考温度，是离线标定规则，设备运行时仅靠 ADC 无法据此自动判断 20℃ 误差。</p>
<h2>过滤前：全部有效配对数据</h2>{metric_table(raw_rows)}
<h2>过滤后：各路保留样本上的拟合误差</h2>{metric_table(clean_rows)}
<p>两路保留数量不同，因此进一步使用共同保留样本比较，避免删点数量不同影响结论。</p>
<h2>公平比较：相同时间点、共同保留样本</h2>{metric_table(common_rows)}
<h2>温度与误差折线图</h2>{''.join(figures)}
<h2>跨组验证：仅用训练组过滤和建表，另一组全部样本验证</h2>
<p>以下验证组未参与训练、未按其预测误差删除样本。表范围之外的 ADC 按组件规则返回端点温度，也计入误差。
这可检查两组记录间的泛化表现，避免只凭建表数据回代下结论。</p>{metric_table(cv_rows)}
<h2>指标含义与结论边界</h2><p>MAE 为绝对误差平均值；RMSE 更强调大误差；平均有符号误差为预测减真实，反映偏高或偏低；P95 取排序后第 floor((n−1)×0.95) 个绝对误差。
除跨组验证表外，指标均为建表数据回代误差。温度曲线差异说明 NTC2 的 ADC 与本次参考温度关联更稳定，不能仅凭这些数据确定 NTC1 偏差的硬件或热学原因。</p>
<p>建议在同类设备、相近传感器安装位置及本次实测温区内优先采用 NTC2。结论未覆盖 0～38.5℃、98.2～150℃，也未验证其他设备或其他运行条件。
NTC1 固定补偿 6℃ 无法消除温度曲线随时刻变化的偏差。</p>
<h2>复现</h2><pre>python function/ntcTemp/scripts/analysis/comparison_report.py C:/Users/29546/Desktop</pre>
<p>原始日志保持不变。更新后的 NTC1 查表已应用 &gt;20℃ 异常过滤，NTC2 表不变。</p></html>"""
    target = output/"NTC1_NTC2温度对比报告.html"
    target.write_text(report, encoding="utf-8")
    print(f"Common retained: {len(shared)}; NTC1 MAE={m1[1]:.3f}, NTC2 MAE={m2[1]:.3f}, reduction={reduction:.1f}%")
    for row in cv_rows:
        print(row)
    print(target)


if __name__ == "__main__":
    main()
