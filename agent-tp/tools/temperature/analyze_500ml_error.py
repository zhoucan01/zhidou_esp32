"""Evaluate logged firmware temperatures against reference; no production table edits."""
import csv, json, math, hashlib, io
from pathlib import Path
from bisect import bisect_left
from datetime import datetime
from collections import Counter
ROOT=Path(__file__).resolve().parents[3]
SRC=ROOT/'agent-tp/data/captures/2026-10-28/20261028-191004-384169.csv'
OUT=ROOT/'agent-tp/reports/ntc/2026-10-28_500mL沿用1150mL参数'
OUT.mkdir(parents=True,exist_ok=True)
raw=SRC.read_bytes()
rows=list(csv.DictReader(io.StringIO(raw.decode('utf-8-sig'))))
def events(event,timekey,valuekeys):
    found={}
    for r in rows:
        if r['事件来源']!=event or not r[timekey]:continue
        try:v=[float(r[k]) for k in valuekeys]
        except ValueError:continue
        if all(math.isfinite(x) for x in v):found[datetime.fromisoformat(r[timekey]).timestamp()]=v
    return sorted(found.items())
refs=events('真实温度','真实温度接收时间',['真实温度℃']);times=[t for t,v in refs]
def reference(t,limit=2):
    i=bisect_left(times,t)
    if i<len(times) and times[i]==t:return refs[i][1][0]
    if i==0 or i==len(times) or times[i]-times[i-1]>limit:return None
    a,b=refs[i-1][1][0],refs[i][1][0]
    return a+(b-a)*(t-times[i-1])/(times[i]-times[i-1])
def metric(points):
    if not points:return {}
    e=[p[3] for p in points];a=sorted(abs(x) for x in e)
    return dict(n=len(e),bias=sum(e)/len(e),mae=sum(a)/len(a),rmse=math.sqrt(sum(x*x for x in e)/len(e)),p95=a[math.ceil(.95*len(a))-1],min=min(e),max=max(e),maxabs=max(a),within1=sum(x<=1 for x in a)/len(a)*100,within2=sum(x<=2 for x in a)/len(a)*100)
series={}; summary={}
for name,event,tk,vk in [('主温度','ESP32板级温度','板级温度接收时间',['ESP32板级温度℃']),('三路','NTC查表温度','NTC计算温度接收时间',[f'NTC{i}查表温度℃' for i in (1,2,3)])]:
    data=events(event,tk,vk)
    for j in range(len(vk)):
        label=name if name!='三路' else f'NTC{j+1}'
        points=[]
        for t,values in data:
            ref=reference(t)
            if ref is not None:points.append((t,ref,values[j],values[j]-ref))
        series[label]=points
        summary[label]={'total':len(data),'excluded':len(data)-len(points),'all':metric(points),'bins':{f'{lo}～{hi}':metric([p for p in points if lo<=p[1]<hi]) for lo,hi in [(60,65),(65,70),(70,75),(75,80),(80,85),(85,90),(90,95),(95,100)]}}
with (OUT/'对齐误差.csv').open('w',encoding='utf-8-sig',newline='') as f:
    w=csv.writer(f);w.writerow(['通道','时间','参考℃','测温℃','误差℃'])
    for label,points in series.items():
        for t,ref,value,error in points:w.writerow([label,datetime.fromtimestamp(t).isoformat(),ref,value,error])
provenance={'file':str(SRC.relative_to(ROOT)),'sha256':hashlib.sha256(raw).hexdigest(),'rows':len(rows),'events':dict(Counter(r['事件来源'] for r in rows)),'reference_count':len(refs),'reference_range':[min(v[0] for t,v in refs),max(v[0] for t,v in refs)],'max_reference_gap_seconds':2,'summary':summary}
(OUT/'统计.json').write_text(json.dumps(provenance,ensure_ascii=False,indent=2))
for label, points in series.items():
    summary[label]['core70to95']=metric([p for p in points if 70<=p[1]<95])
    summary[label]['last'] = points[-1]
(OUT/'统计.json').write_text(json.dumps(provenance,ensure_ascii=False,indent=2))
def table(labels,which):
    result=['| 通道 | 样本数 | 平均偏差℃ | MAE℃ | P95绝对误差℃ | 最大绝对误差℃ | ±1℃占比 |',
            '|---|---:|---:|---:|---:|---:|---:|']
    for label in labels:
        m=summary[label][which]
        result.append(f"| {label} | {m['n']} | {m['bias']:+.3f} | {m['mae']:.3f} | {m['p95']:.3f} | {m['maxabs']:.3f} | {m['within1']:.1f}% |")
    return '\n'.join(result)
report=['# 500 mL水量沿用1150 mL参数的误差分析',
    '输入：`'+str(SRC.relative_to(ROOT))+'`。水量500 mL、沿用1150 mL参数由用户确认；未从日志字段独立识别水量。',
    '## 结论',
    '本次70～95℃段NTC1和NTC2整体误差较小，NTC3偏高更明显。不能据此认定1150 mL参数在500 mL全温区均适用：低温端三路先后冻结在约69℃，末段误差超过6℃。冻结与数据库下边界及5℃外插限制一致，应与水量造成的映射偏差分开判断。',
    '## 完整记录（含启动和低温冻结）',table(list(series),'all'),
    '## 70～95℃区间（按参考温度筛选）',table(list(series),'core70to95'),
    '这是同一批数据的区间统计，不是独立验证。高温启动瞬态和低温冻结都保留在全程统计中。',
    '## 三路分温区平均偏差 / MAE（℃）',
    '| 参考温区℃ | 样本数（每通道） | NTC1 | NTC2 | NTC3 |','|---|---:|---:|---:|---:|']
for band,m in summary['NTC1']['bins'].items():
    if not m:continue
    cells=[f"{summary[label]['bins'][band]['bias']:+.3f} / {summary[label]['bins'][band]['mae']:.3f}" for label in ('NTC1','NTC2','NTC3')]
    report.append(f"| {band} | {m['n']} | "+' | '.join(cells)+' |')
report += ['## 冻结原因与水量影响的区别',
    '日志开始参考水温98.1℃，随后短暂升至100.1℃再下降；不是纯粹从第一点开始连续降温。起始瞬态中NTC1最大偏低约10℃，本次不将其直接归结为水量差。',
    '约19:49后NTC1维持69.18℃，NTC2/3稍后分别维持69.08℃、69.30℃，同时原始ADC继续上升、参考温度继续下降。当前数据库冷却组的最低节点约74℃，允许再外插5℃，边界约69℃；越界后代码保持最近有效温度。这是测温覆盖不足导致的冻结，不是实际恒温。',
    '70～95℃区间中NTC1在70～75℃偏低、80～85℃略偏高；NTC3在90～95℃偏高约1.18℃。三探头高度和浸没深度不同，减水后热容量、冷却速度、分层及相对液面位置都可能变化，但本记录缺少同工况1150 mL对照、探头浸没信息与独立复测，不能将这些误差全量分摊为水量的因果影响，也不应直接按500/1150缩放补偿。',
    '建议先补足500 mL降温表到目标最低温区，保留范围失效诊断；用独立批次验证500 mL表。升温误差需单独分析升温记录，本报告不外推升温精度。',
    '## 对齐方法与限制',
    f"CSV共{len(rows)}行；参考温度{len(refs)}个独立接收时间。只取各通道自身事件及自身接收时间，按时间戳去重；参考温度线性插值仅允许相邻参考间隔≤2秒，不外插、不平移时间、不使用已记录误差列。误差定义为测温减参考，P95采用最近秩。",
    '; '.join(f"{label}：{m['total']}点中保留{m['all']['n']}点，剔除{m['excluded']}点（参考缺口或两端无法对齐）" for label,m in summary.items())+'。',
    '各温区按采样点计权，并非等温区权重；高温参考缺口较多。CSV的本地MACHINE=4是上位机本地ADC拟合选择，不作为固件机型；本次使用固件上报的三路温度和主温度，不使用本地拟合列。',
    '输入SHA256：`'+provenance['sha256']+'`。详细数据见[统计JSON](统计.json)和[对齐误差CSV](对齐误差.csv)。',
    '复现：`python3 -B agent-tp/tools/temperature/analyze_500ml_error.py`。未修改固件或标定表。']
(OUT/'分析报告.md').write_text('\n\n'.join(report).replace('|\n\n|', '|\n|'),encoding='utf-8')
for label in series:print(label,summary[label]['core70to95'])
print('report:',OUT/'分析报告.md')
