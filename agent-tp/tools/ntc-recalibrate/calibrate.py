"""Recalibrate only measured >=60 C heating groups; retain other database rows."""
import bisect
import csv
import json
import math
import re
import statistics as st
from collections import Counter, defaultdict
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
REPORT = ROOT/'agent-tp/reports/ntc-recalibration-124212'
REPORT.mkdir(parents=True, exist_ok=True)
DB = ROOT/'function/ntcTemp/tables/NTC3/ntcTemp_machine3_operating_table.inc'
SOURCE = ROOT/'agent-tp/data/captures/2026-11-04/20261104-124212-441968.csv'
rows = list(csv.DictReader(SOURCE.open(encoding='utf-8-sig')))
def timestamp(s): return datetime.fromisoformat(s).timestamp()
refs = sorted({timestamp(r['真实温度接收时间']):float(r['真实温度℃']) for r in rows if r['真实温度接收时间'] and r['真实温度℃']}.items())
rt = [x[0] for x in refs]
def reference(t):
 i=bisect.bisect_left(rt,t)
 if i<len(rt) and rt[i]==t: return refs[i][1]
 if i==0 or i==len(rt) or rt[i]-rt[i-1]>6: return None
 return refs[i-1][1]+(refs[i][1]-refs[i-1][1])*(t-rt[i-1])/(rt[i]-rt[i-1])
config=json.loads((ROOT/'agent-tp/tools/soy_host/auto_heat_process3.json').read_text())['stages']
samples=defaultdict(list); counts=Counter(); stop=None; last_motor=None; stopped=False; block=0
seen=set()
for rowno,r in enumerate(rows,2):
 t=timestamp(r['记录时间'])
 if r['事件来源']=='站3电机转速' and r['站3电机转速RPM']:
  fresh=last_motor is not None and t-last_motor<=1
  zero=float(r['站3电机转速RPM'])==0
  if zero and (not stopped or not fresh): stop=t; block+=1
  if not zero: stop=None
  stopped=zero;last_motor=t
 if r['事件来源']!='NTC' or r['模式'] not in ('2','3','4','5','6','7'):continue
 counts['NTC heating events']+=1
 at=timestamp(r['NTC接收时间'])
 if at in seen:counts['duplicate ADC event']+=1;continue
 seen.add(at)
 if not stopped or stop is None or at-stop<.5 or last_motor is None or not 0<=at-last_motor<=1:
  counts['motor not settled/fresh']+=1;continue
 ref=reference(at)
 if ref is None:counts['reference interpolation unavailable (>6s bracket or endpoints)']+=1;continue
 if ref<60:counts['below60']+=1;continue
 mode=int(r['模式']); counts['accepted events']+=1
 for ch in range(3):
  adc=float(r[f'NTC{ch+1}_ADC'])
  if not 0<adc<4095: counts['invalid ADC channel']+=1;continue
  samples[(mode,ch)].append((adc,ref,block,rowno))
# Bin at 16 ADC counts, median suppresses within-stop scatter; isotonic PAVA
# enforces the physical non-increasing temperature vs ADC relation.
def fit(s):
 bins=defaultdict(list)
 for a,t,*_ in s:bins[int(a//16)].append((a,t))
 nodes=[(st.median(a for a,t in v),st.median(t for a,t in v),len(v)) for _,v in sorted(bins.items())]
 blocks=[]
 for a,t,w in nodes:
  blocks.append([t*w,w,[(a,w)]])
  while len(blocks)>1 and blocks[-2][0]/blocks[-2][1]<blocks[-1][0]/blocks[-1][1]:
   b=blocks.pop();p=blocks[-1];p[0]+=b[0];p[1]+=b[1];p[2]+=b[2]
 return [(a,b[0]/b[1]) for b in blocks for a,w in b[2]]
def predict(curve,a):
 if len(curve)<2:return None
 i=bisect.bisect_left([x[0] for x in curve],a)
 if i==0:i=1
 if i==len(curve):i-=1
 x,y=curve[i-1];u,v=curve[i]
 t=y+(v-y)*(a-x)/(u-x)
 if a<curve[0][0] and (curve[0][1]<=curve[1][1] or t>curve[0][1]+5):return None
 if a>curve[-1][0] and (curve[-2][1]<=curve[-1][1] or t<curve[-1][1]-5):return None
 return t
old=DB.read_text();backup=REPORT/'operating_table_before.inc'
if not backup.exists():backup.write_text(old)
parsed=[]
for line in old.splitlines():
 if line.lstrip().startswith('{'):
  v=re.findall(r'-?\d+(?:\.\d+)?',line);parsed.append((tuple(map(float,v[:9])),line))
metrics=[];replacements={}
for (mode,ch),s in sorted(samples.items()):
 stage=config[mode-1];key=(ch,1,stage['heater_gear'],stage['heater_on_seconds'],stage['heater_off_seconds'],stage['motor_gear'],stage['motor_on_seconds'],stage['motor_off_seconds'],1150)
 train=[x for x in s if x[2]%3!=0];test=[x for x in s if x[2]%3==0]
 curve=fit(train);errors=[]
 for a,t,*_ in test:
  p=predict(curve,a)
  if p is not None:errors.append(abs(p-t))
 full=fit(s)
 if len(full)<2:raise RuntimeError(f'insufficient nodes {mode,ch}')
 replacements[key]=[f'    {{{ch}U, 1U, {key[2]}U, {key[3]:.3f}f, {key[4]:.3f}f, {key[5]}, {key[6]:.3f}f, {key[7]:.3f}f, 1150U, {{{a:.3f}f, {t:.3f}f}}}},' for a,t in full]
 metrics.append(dict(mode=mode,channel=ch+1,samples=len(s),nodes=len(full),adc_min=min(x[0] for x in s),adc_max=max(x[0] for x in s),temperature_min=min(x[1] for x in s),temperature_max=max(x[1] for x in s),heldout=len(test),predicted=len(errors),mae=st.mean(errors) if errors else None,max_abs=max(errors) if errors else None))
output=defaultdict(list)
for k,line in parsed:
 if k not in replacements:output[k].append(line)
output.update(replacements)
with (REPORT/'calibration_samples.csv').open('w', newline='') as stream:
 writer=csv.writer(stream, lineterminator='\n')
 writer.writerow(('mode','channel','adc','reference_interpolated','stop_block','source_csv_line','validation_split'))
 for (mode,ch),values in sorted(samples.items()):
  for a,t,b,line in values:
   writer.writerow((mode,ch+1,a,t,b,line,'holdout' if b%3==0 else 'train'))
DB.write_text('/* Operating-condition calibration: legacy groups retained; latest measured motor cycles calibrated by agent-tp/tools/ntc-recalibrate/calibrate.py. */\n'+'\n'.join(line for key in sorted(output) for line in output[key])+'\n')
(REPORT/'metrics.json').write_text(json.dumps(dict(counts=counts,groups=metrics),ensure_ascii=False,indent=2)+'\n')
print(dict(counts))
for m in metrics:print(m)
