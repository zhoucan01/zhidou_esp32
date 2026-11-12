"""Append measured profiles without changing any existing calibration row."""
import argparse,bisect,csv,json,math,re,statistics as st
from collections import Counter,defaultdict,deque
from datetime import datetime
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--adc-only', action='store_true', help='Use latest recorded valid reference without age or motor-feedback gates')
args=parser.parse_args()
DB=ROOT/'function/ntcTemp/tables/NTC3/ntcTemp_machine3_operating_table.inc'
REPORT=ROOT/('agent-tp/reports/ntc-append-164228-stable' if args.adc_only else 'agent-tp/reports/ntc-append-164228')
REPORT.mkdir(parents=True,exist_ok=True)
rows=list(csv.DictReader((ROOT/'agent-tp/data/captures/2026-11-04/20261104-164228-160405.csv').open(encoding='utf-8-sig')))
def ts(s):return datetime.fromisoformat(s).timestamp()
refs=sorted({ts(r['真实温度接收时间']):float(r['真实温度℃']) for r in rows if r['真实温度接收时间'] and r['真实温度℃']}.items());rt=[x[0] for x in refs]
def ref_at(t):
 i=bisect.bisect_left(rt,t)
 if i<len(rt) and rt[i]==t:return refs[i][1]
 if i==0 or i==len(rt) or rt[i]-rt[i-1]>6:return None
 return refs[i-1][1]+(refs[i][1]-refs[i-1][1])*(t-rt[i-1])/(rt[i]-rt[i-1])
old=DB.read_text();groups=defaultdict(list)
for line in old.splitlines():
 if line.lstrip().startswith('{'):
  v=list(map(float,re.findall(r'-?\d+(?:\.\d+)?',line)))
  groups[tuple(v[:9])].append((v[9],v[10],line))
config=json.loads((ROOT/'agent-tp/tools/soy_host/auto_heat_process4.json').read_text())['stages']
windows=[deque(maxlen=8) for _ in range(3)];last_mode=None;seen=set();candidates=defaultdict(list);counts=Counter();rpm_time=None
reference_ages=[]
for r in rows:
 t=ts(r['记录时间'])
 if r['事件来源']=='站3电机转速' and r['站3电机转速RPM']:rpm_time=t
 if r['事件来源']!='NTC':continue
 if r['模式'] not in ('1','2','3'):
  last_mode=None
  for w in windows:w.clear()
  continue
 mode=int(r['模式']);at=ts(r['NTC接收时间'])
 if at in seen:continue
 seen.add(at);counts['heating_events']+=1
 if mode!=last_mode:
  for w in windows:w.clear()
 last_mode=mode
 ref=(float(r['真实温度℃']) if r['真实温度℃'] else None) if args.adc_only else ref_at(at)
 if ref is not None and (not math.isfinite(ref) or not -50<=ref<=150):ref=None
 for ch,w in enumerate(windows):
  a=float(r[f'NTC{ch+1}_ADC'])
  if not 0<a<4095 or (w and (at-w[-1][0]>2 or at<=w[-1][0] or abs(a-w[-1][1])>200)):
   w.clear();counts['ADC_reset']+=1
  if not 0<a<4095:continue
  w.append((at,a))
  if len(w)<8 or at-w[0][0]<.14:counts['insufficient_trend']+=1;continue
  x=[p[0]-w[0][0] for p in w];y=[p[1] for p in w];xm=st.mean(x);ym=st.mean(y)
  slope=sum((u-xm)*(v-ym) for u,v in zip(x,y))/sum((u-xm)**2 for u in x)
  residual=max(abs(v-(ym+slope*(u-xm))) for u,v in zip(x,y))
  if residual>40:counts['trend_residual_over40']+=1;continue
  if ref is None:counts['reference_unavailable']+=1;continue
  if not args.adc_only and (rpm_time is None or not 0<=at-rpm_time<=1):counts['motor_feedback_stale']+=1;continue
  s=config[mode-1];key=(ch,1,s['heater_gear'],s['heater_on_seconds'],s['heater_off_seconds'],s['motor_gear'],s['motor_on_seconds'],s['motor_off_seconds'],1150)
  candidates[key].append((a,ref));counts['accepted_channel_samples']+=1
  if args.adc_only and r['真实温度接收时间']:
   age=at-ts(r['真实温度接收时间']);reference_ages.append(age)
   if age>6:counts['accepted_reference_age_over6s']+=1
# Medians in 16-ADC bins; pool adjacent violating blocks for monotone curves.
added=[];summaries=[]
for key,values in sorted(candidates.items()):
 bins=defaultdict(list)
 for a,t in values:bins[int(a//16)].append((a,t))
 blocks=[]
 for _,v in sorted(bins.items()):
  a=st.median(x[0] for x in v);t=st.median(x[1] for x in v);weight=len(v)
  blocks.append([weight*t,weight,[a]])
  while len(blocks)>1 and blocks[-2][0]/blocks[-2][1]<blocks[-1][0]/blocks[-1][1]:
   b=blocks.pop();p=blocks[-1];p[0]+=b[0];p[1]+=b[1];p[2]+=b[2]
 nodes=[(round(a,3),round(b[0]/b[1],3)) for b in blocks for a in b[2]]
 existing=sorted(groups[key]);accepted=[];skipped=Counter()
 for a,t in nodes:
  # Preserve the entire measured span of an existing profile, not merely exact nodes.
  if existing and existing[0][0]<=a<=existing[-1][0]:skipped['old_span']+=1;continue
  combined=sorted([(x,y) for x,y,*_ in existing]+accepted)
  i=bisect.bisect_left([p[0] for p in combined],a)
  if (i and combined[i-1][1]<t) or (i<len(combined) and t<combined[i][1]):
   skipped['monotonic_conflict']+=1;continue
  accepted.append((a,t))
 if not existing and len(accepted)<2:
  skipped['insufficient_new_nodes']+=len(accepted);accepted=[]
 for a,t in accepted:
  ch,phase,gear,on,off,motor,mon,moff,water=key
  added.append((key,a,f'    {{{ch}U, 1U, {gear}U, {on:.3f}f, {off:.3f}f, {motor}, {mon:.3f}f, {moff:.3f}f, {water}U, {{{a:.3f}f, {t:.3f}f}}}},'))
 summaries.append(dict(key=key,samples=len(values),nodes_added=len(accepted),adc_range=[min(a for a,t in values),max(a for a,t in values)],temp_range=[min(t for a,t in values),max(t for a,t in values)],skipped=dict(skipped)))
# Keep every old line verbatim, sort whole profile groups so production scans remain valid.
combined=[(key,a,line) for key,v in groups.items() for a,t,line in v]+added
result='/* Legacy points retained; append-only measured groups: agent-tp/tools/ntc-append/append_164228.py. */\n'+'\n'.join(line for key,a,line in sorted(combined,key=lambda x:(x[0],x[1])))+'\n'
for key,v in groups.items():
 for a,t,line in v:assert line in result
DB.write_text(result)
report=['# 工艺4温度数据库补充（164228批次）','',f'筛选计数：{dict(counts)}。新增节点合计{len(added)}。','',
'所有原有数据库行逐行断言保留。冲突按通道、加热/降温、功率、电机档位、各开停时间、水量组成的完整工况键判断；旧工况已有ADC覆盖范围整体不改，新增点破坏单调关系也不添加。不同功率或节奏的相同ADC可以各自存在，不是同一标定曲线的冲突。','',
'本批工艺4，14档常开至95℃，8档常开至100℃，电机1档开3秒停0.5秒；水量沿用项目1150mL默认，CSV不独立证明实际水量。只补充升温，未补充模式0的降温。','',
'按NTC接收时间去重，模式边界清空窗口；每通道需8个连续ADC、相邻间隔≤2秒、步变≤200、持续≥140ms，线性趋势最大残差≤40 ADC。RPM反馈年龄≤1秒。采用同通道稳定趋势而非停稳500ms规则（本批0.5秒停机几乎无此类样本）。参考仅在前后接收间隔≤6秒内插值，不外推；失效期间不以旧参考值标定。16 ADC分箱中位数加权保序。','',
'这不是独立精度验证。短停机/电机运行中稳定ADC仍可能存在系统性偏移；本次没有用拟合残差宣称实机精度。新增14档已补充默认工况模板对应该节奏，8档因其他工艺节奏不同保留原模板，新增8档组仍参与按工况距离与ADC覆盖的回退选择。','',
'未输出或提交测试/标定中间数据，仅保留工具和本结论。','']
if args.adc_only:
 report=[line.replace('RPM反馈年龄≤1秒。','不限制电机反馈年龄。').replace(
  '参考仅在前后接收间隔≤6秒内插值，不外推；失效期间不以旧参考值标定。',
  '按用户要求使用CSV当行最近有效真实温度，不作参考接收年龄筛选，不插值；原始-600等无效参考不会作为温度标签。') for line in report]
 report+=['用户指定仅以ADC连续稳定性作为采样筛选，旧点及单调冲突保护仍保留。缺少任何有限有效参考的行不能形成ADC—温度点。',
          f'接受的通道样本参考年龄最大{max(reference_ages, default=0):.3f}秒；年龄超过6秒的样本数{counts["accepted_reference_age_over6s"]}（三通道分别计数）。稳定ADC不等于参考温度同步，新增点需要实机验证。','']
for s in summaries:report.append(str(s))
(REPORT/'README.md').write_text('\n'.join(report)+'\n')
print('\n'.join(report))
