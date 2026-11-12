"""Event-deduplicated board/reference error statistics; no third-party packages."""
import bisect,csv,statistics as st
from collections import Counter,defaultdict
from datetime import datetime
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
CAP=ROOT/'agent-tp/data/captures/2026-11-04/20261104-132238-073306.csv'
OUT=ROOT/'agent-tp/reports/temperature-error-132238'
OUT.mkdir(parents=True,exist_ok=True)
rows=list(csv.DictReader(CAP.open(encoding='utf-8-sig')))
def ts(s):return datetime.fromisoformat(s).timestamp()
refs=sorted({ts(r['真实温度接收时间']):float(r['真实温度℃']) for r in rows if r['真实温度接收时间'] and r['真实温度℃']}.items());rt=[x[0] for x in refs]
def ref_at(t):
 i=bisect.bisect_left(rt,t)
 if i<len(rt) and rt[i]==t:return refs[i][1]
 if i==0 or i==len(rt) or rt[i]-rt[i-1]>6:return None
 return refs[i-1][1]+(refs[i][1]-refs[i-1][1])*(t-rt[i-1])/(rt[i]-rt[i-1])
samples=[];seen=set();counts=Counter();snapshot=[]
for r in rows:
 if r['事件来源']!='ESP32板级温度' or not r['板级温度接收时间'] or not r['ESP32板级温度℃']:continue
 t=ts(r['板级温度接收时间'])
 if t in seen:counts['重复板级事件']+=1;continue
 seen.add(t);counts['板级事件']+=1
 ref=ref_at(t)
 if ref is None:counts['无≤6秒参考插值区间']+=1;continue
 board=float(r['ESP32板级温度℃']);mode=int(r['模式'])
 samples.append(dict(time=r['板级温度接收时间'],timestamp=t,real=ref,board=board,error=board-ref,mode=mode))
 if r['真实温度℃'] and r['真实温度接收时间'] and 0<=t-ts(r['真实温度接收时间'])<=2:
  snapshot.append((float(r['真实温度℃']),board-float(r['真实温度℃']),mode))
def stats(s):
 e=[x['error'] for x in s];a=sorted(map(abs,e));n=len(e)
 if not n:return None
 return dict(n=n,bias=st.mean(e),mae=st.mean(a),rmse=(st.mean(x*x for x in e))**.5,max_abs=max(a),p95=a[min(n-1,int(.95*(n-1)))],within1=sum(x<=1 for x in a)/n,within2=sum(x<=2 for x in a)/n,min_error=min(e),max_error=max(e))
def fmt(s):
 v=stats(s)
 if v is None:return '|无样本|'
 return f"{v['n']}|{v['bias']:.3f}|{v['mae']:.3f}|{v['rmse']:.3f}|{v['max_abs']:.3f}|{v['within1']:.1%}|{v['within2']:.1%}|"
active=[x for x in samples if x['mode'] in range(1,8)]
hi=[x for x in active if x['real']>=60]
usable=[x for x in hi if 25<=x['board']<=100]
bins=[(60,80),(80,85),(85,90),(90,93),(93,95),(95,98),(98,100),(100,float('inf'))]
text=['# 本轮板级温度与真实温度误差','',f'输入：`{CAP.name}`。温差=板级−真实，正值表示板级偏高。','',f'原始CSV {len(rows)}行；板级独立事件{len(seen)}次；排除计数：{dict(counts)}。参考温度共{len(refs)}个独立接收时间。',
'','按板级温度接收时间去重，参考温度仅在相邻接收间隔≤6秒时线性插值，不做区间外补值。按有效事件等权统计，不按CSV重复快照计数，不按时长加权。升温取模式1～7；模式0单独列出，未混入精度结论。原始ADC和板级异常值不按误差大小剔除。',
'','|范围|样本数|平均偏差℃|MAE℃|RMSE℃|最大绝对误差℃|≤1℃|≤2℃|','|---|---:|---:|---:|---:|---:|---:|---:|']
for label,s in [('全部升温',active),('升温且真实≥60℃（含0℃异常）',hi),('升温且真实≥60℃、板级有效25～100℃',usable),('模式0', [x for x in samples if x['mode']==0])]:text.append('|'+label+'|'+fmt(s))
for lo,up in bins:
 s=[x for x in active if lo<=x['real']<up]
 text.append(f'|升温[{lo},{up})℃|'+fmt(s))
text+=['','## 板级输出有效时的分段精度','',
 '额外条件为板级25～100℃。这一表显式排除0℃占位异常，不替代前述全量统计。',
 '|真实温区℃|样本数|平均偏差℃|MAE℃|RMSE℃|最大绝对误差℃|≤1℃|≤2℃|',
 '|---|---:|---:|---:|---:|---:|---:|---:|']
for lo,up in bins:
 s=[x for x in usable if lo<=x['real']<up]
 if s:text.append(f'|[{lo},{up})|'+fmt(s))
cool=[x for x in samples if x['mode']==0 and x['timestamp']>=ts('2026-11-04T13:46:57.294')]
text+=['','## 降温（按串口100℃降温入口划分）','',
 '|范围|样本数|平均偏差℃|MAE℃|RMSE℃|最大绝对误差℃|≤1℃|≤2℃|',
 '|---|---:|---:|---:|---:|---:|---:|---:|','|降温模式0|'+fmt(cool)]
text+=['','## 按模式','', '|模式|样本数|平均偏差℃|MAE℃|RMSE℃|最大绝对误差℃|≤1℃|≤2℃|','|---|---:|---:|---:|---:|---:|---:|---:|']
for m in sorted(set(x['mode'] for x in samples)):text.append(f'|{m}|'+fmt([x for x in samples if x['mode']==m]))
text+=['','## 60℃以上升温最大误差事件','', '|接收时间|模式|真实℃|板级℃|温差℃|','|---|---:|---:|---:|---:|']
for x in sorted(hi,key=lambda x:abs(x['error']),reverse=True)[:10]:text.append(f"|{x['time']}|{x['mode']}|{x['real']:.3f}|{x['board']:.3f}|{x['error']:+.3f}|")
# Longest unchanged board run, using real elapsed time and <=3s adjacent gap.
runs=[];run=[]
for x in hi:
 if run and (x['board']!=run[-1]['board'] or x['timestamp']-run[-1]['timestamp']>3):runs.append(run);run=[]
 run.append(x)
if run:runs.append(run)
runs.sort(key=lambda r:r[-1]['timestamp']-r[0]['timestamp'],reverse=True)
text+=['','## 数据范围与重复输出检查','',f"工艺标识：{sorted(set(r['工艺'] for r in rows))}；参考温度范围{min(v for t,v in refs):.2f}～{max(v for t,v in refs):.2f}℃。",f"升温事件真实温度最高{max(x['real'] for x in active):.3f}℃。"]
for r in runs[:3]:
 text.append(f"- 60℃以上最长重复输出之一：{r[0]['time']}～{r[-1]['time']}，{r[-1]['timestamp']-r[0]['timestamp']:.1f}秒，板级{r[0]['board']:.3f}℃，参考{r[0]['real']:.3f}→{r[-1]['real']:.3f}℃。")
snap=[dict(error=e) for r,e,m in snapshot if r>=60 and m in range(1,8)]
text+=['',f'敏感性对照：不插值而使用接收年龄≤2秒的最近参考值，60℃以上升温统计为{stats(snap)}。',
'','本分析仅比较板级输出与外部参考，CSV不能证明所烧录固件版本、每次内部滤波拒绝原因或真实液体温度空间一致性；两路测点响应延迟也可能影响瞬时误差。未修改数据库。']
(OUT/'README.md').write_text('\n'.join(text)+'\n')
print('\n'.join(text))
with (OUT/'README.md').open('a') as report:
 report.write('''

## 结论与异常说明

本轮为工艺1，末段8档、电机开2秒停6秒，不能当作7档工艺2/3的实机精度验证。正常输出时60℃以上平均绝对误差0.294℃，96.1%在±1℃内，全部在±2℃内；98～100℃也有有效数据，未见此前94℃附近长期不更新的特征。

60℃以上升温1193次比较中有51次板级0℃，占4.3%。13:27:00.567首次恢复约66.56℃，参考约66.7℃。这不是小幅标定偏差，应另查低温启动、滤波接纳及未建立有效温度时的输出。串口显示功率校准13:23:54.959已完成，所以不能将整段0℃归入功率校准期间。CSV无法证明具体拒绝分支或烧录版本。

串口确认13:46:57.294真实温度达到100℃进入降温，14:07:45.597降至85℃结束。终止前已对齐升温板级事件最高参考99.901℃，没有100℃以上升温样本不表示未达到100℃。板级重复100℃还受软件上限影响，不能单凭重复值判定冻结。

建议优先定位启动0℃异常；正常测温区间暂没有证据要求再次整体改写温度数据库。本次未修改数据库，未产生新增测试或标定过程数据文件，仅保存报告与复现脚本。
''')
