"""Validate new exact operating curves on the previous, untouched capture."""
import bisect,csv,json,re,statistics as st
from pathlib import Path
from datetime import datetime
from collections import defaultdict,Counter
ROOT=Path(__file__).resolve().parents[3]
REPORT=ROOT/'agent-tp/reports/ntc-recalibration-124212'
rows=list(csv.DictReader((ROOT/'agent-tp/data/captures/2026-11-04/20261104-115358-138914.csv').open(encoding='utf-8-sig')))
def ts(s):return datetime.fromisoformat(s).timestamp()
refs=sorted({ts(r['真实温度接收时间']):float(r['真实温度℃']) for r in rows if r['真实温度接收时间'] and r['真实温度℃']}.items()); times=[x[0] for x in refs]
curves=defaultdict(list)
for line in (ROOT/'function/ntcTemp/tables/NTC3/ntcTemp_machine3_operating_table.inc').read_text().splitlines():
 if line.lstrip().startswith('{'):
  v=list(map(float,re.findall(r'-?\d+(?:\.\d+)?',line)));curves[tuple(v[:9])].append(tuple(v[9:]))
config=json.loads((ROOT/'agent-tp/tools/soy_host/auto_heat_process2.json').read_text())['stages']
stop=None;last=None;stopped=False;errors=defaultdict(list);counts=Counter();seen=set()
for r in rows:
 t=ts(r['记录时间'])
 if r['事件来源']=='站3电机转速' and r['站3电机转速RPM']:
  zero=float(r['站3电机转速RPM'])==0
  if zero and (not stopped or last is None or t-last>1):stop=t
  if not zero:stop=None
  stopped=zero;last=t
 if r['事件来源']!='NTC' or r['模式'] not in ('2','3','4','5','6','7'):continue
 a_time=ts(r['NTC接收时间'])
 if a_time in seen:continue
 seen.add(a_time)
 if not stopped or stop is None or a_time-stop<.5 or last is None or not 0<=a_time-last<=1:continue
 i=bisect.bisect_left(times,a_time)
 if i==0 or i==len(times) or times[i]-times[i-1]>6:continue
 ref=refs[i-1][1]+(refs[i][1]-refs[i-1][1])*(a_time-times[i-1])/(times[i]-times[i-1])
 if ref<60:continue
 mode=int(r['模式']);s=config[mode-1]
 for ch in range(3):
  key=(ch,1,s['heater_gear'],s['heater_on_seconds'],s['heater_off_seconds'],s['motor_gear'],s['motor_on_seconds'],s['motor_off_seconds'],1150)
  c=curves[key];a=float(r[f'NTC{ch+1}_ADC']);counts[(mode,ch)]+=1
  j=bisect.bisect_left([x[0] for x in c],a);j=max(1,min(j,len(c)-1));x,y=c[j-1];u,v=c[j];p=y+(v-y)*(a-x)/(u-x)
  # Match production: flat boundary slopes cannot authorize extrapolation.
  if (a<c[0][0] and (c[0][1]<=c[1][1] or p>c[0][1]+5)) or (a>c[-1][0] and (c[-2][1]<=c[-1][1] or p<c[-1][1]-5)):continue
  errors[(mode,ch)].append(abs(p-ref))
result=[dict(mode=m,channel=c+1,samples=n,predicted=len(errors[(m,c)]),mae=st.mean(errors[(m,c)]) if errors[(m,c)] else None,max_abs=max(errors[(m,c)]) if errors[(m,c)] else None) for (m,c),n in sorted(counts.items())]
(REPORT/'previous_capture_validation.json').write_text(json.dumps(result,indent=2)+'\n')
for r in result:print(r)
