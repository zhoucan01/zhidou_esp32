"""Audit actual production selection against fresh >=60 C capture samples."""
import csv
import json
import subprocess
import sys
from collections import defaultdict, Counter
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/ntc-stage-audit'
OUT.mkdir(parents=True, exist_ok=True)
CAPTURE = ROOT / 'agent-tp/data/captures/2026-11-04/20261104-115358-138914.csv'
REPORT = ROOT / 'agent-tp/reports/ntc-stage-audit'
if len(sys.argv) > 1:
 CAPTURE = Path(sys.argv[1])
if len(sys.argv) > 2:
 REPORT = Path(sys.argv[2])
REPORT.mkdir(parents=True, exist_ok=True)
SOURCE = r'''
#include <stdio.h>
#include <math.h>
#include "ntcTemp.c"
struct DataPool g_DataPool;
int64_t esp_timer_get_time(void) { return 1000000; }
int main(void) {
 unsigned mode, gear, a[3]; float real, board;
 while (scanf("%u %u %f %f %u %u %u", &mode, &gear, &real, &board, &a[0], &a[1], &a[2]) == 7) {
  g_DataPool.powerEnabled=true; g_DataPool.powerHeater1Enabled=true;
  g_DataPool.powerHeater1Gear=gear; g_DataPool.powerMotorRpm=0;
  for(unsigned scenario=0;scenario<2;++scenario) for(unsigned ch=0;ch<3;++ch) {
   machine3_temperature_valid[ch]=true;
   machine3_last_valid_temperatures[ch]=scenario ? board : real;
   NtcTempMachine3OperatingConditions c=machine3_default_operating_conditions(true,ch);
   size_t n=sizeof(machine3_operating_table)/sizeof(machine3_operating_table[0]);
   const NtcMachine3OperatingPoint *p=machine3_nearest_covered_profile(machine3_operating_table,n,ch,1,&c,a[ch]);
   int covered=p!=NULL;
   if(!p) p=machine3_nearest_profile(machine3_operating_table,n,ch,1,&c);
   float t=machine3_channel_temperature(ch,a[ch],true,&c);
   printf("%u,%u,%u,%u,%u,%.0f,%.0f,%d,%.4f,%.4f\n",mode,scenario,ch,gear,p?p->heater_gear:0,p?p->heater_on_seconds:-1,p?p->heater_off_seconds:-1,covered,t,real);
  }
 }
}
'''
(OUT/'audit.c').write_text(SOURCE)
subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-DMACHINE=3','-DNTC_TEMP_LOG_ENABLE=0',
 '-I',str(ROOT/'function/ntcTemp'),'-I',str(ROOT/'task/DataCenter'),
 '-I',str(ROOT/'agent-tp/tests/firmware/ntc_host'),str(OUT/'audit.c'),'-lm','-o',str(OUT/'audit')],check=True)
stages=json.loads((ROOT/'agent-tp/tools/soy_host/auto_heat_process2.json').read_text())['stages']
lines=[]; rejected=Counter(); modes=Counter()
with CAPTURE.open(encoding='utf-8-sig') as stream:
 for r in csv.DictReader(stream):
  if r['事件来源']!='NTC': continue
  if not r['真实温度℃'] or float(r['真实温度℃'])<60: continue
  if r['模式'] not in ('2','3','4','5','6','7'):
   rejected['非升温段（模式0）']+=1; continue
  age=(datetime.fromisoformat(r['记录时间'])-datetime.fromisoformat(r['真实温度接收时间'])).total_seconds()
  if not 0<=age<=2: rejected['参考温度超过2秒（保留选表检查，不用于精度）']+=1
  m=int(r['模式']); modes[m]+=1
  lines.append(' '.join(map(str,[m, stages[m-1]['heater_gear'],r['真实温度℃'],r['ESP32板级温度℃'] or r['真实温度℃'],r['NTC1_ADC'],r['NTC2_ADC'],r['NTC3_ADC']])))
result=subprocess.run([str(OUT/'audit')],input='\n'.join(lines),text=True,capture_output=True,check=True)
(REPORT/'selection.csv').write_text('mode,scenario,channel,requested_gear,selected_gear,on_seconds,off_seconds,covered,converted,reference\n'+result.stdout)
stats=defaultdict(Counter)
import math
for row in csv.reader(result.stdout.splitlines()):
 mode,scenario,ch,gear,selected,on,off,covered,t,real=row
 s=stats[(mode,scenario,ch)]; s['count']+=1
 s['finite']+=math.isfinite(float(t)); s['covered']+=int(covered)
 s[f'{selected}档 {on}/{off}秒']+=1
print('样本数:',dict(modes),'剔除:',dict(rejected))
for key,s in sorted(stats.items()):print(key,dict(s))
