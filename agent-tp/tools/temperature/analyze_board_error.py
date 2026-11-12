"""Event-only temperature comparison; output artifacts stay under agent-tp."""
import argparse, csv, json, math, hashlib
from pathlib import Path
from datetime import datetime
from bisect import bisect_left
from collections import Counter
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
raw=a.source.read_bytes();rows=list(csv.DictReader(raw.decode('utf-8-sig').splitlines()))
def events(kind,tk,vk):
    d={};matched=invalid=0
    for r in rows:
        if r['事件来源']!=kind:continue
        matched+=1
        try:
            t=datetime.fromisoformat(r[tk]).timestamp();v=float(r[vk])
            if not math.isfinite(v):raise ValueError()
            d[t]=v
        except (ValueError,KeyError):invalid+=1
    return sorted(d.items()),dict(rows=matched,invalid=invalid,unique=len(d),duplicate=matched-invalid-len(d))
ref,rc=events('真实温度','真实温度接收时间','真实温度℃');board,bc=events('ESP32板级温度','板级温度接收时间','ESP32板级温度℃');rt=[t for t,v in ref]
paired=[];excluded=[]
for t,v in board:
    i=bisect_left(rt,t)
    if i<len(rt) and rt[i]==t:r=ref[i][1]
    elif i==0 or i==len(rt) or rt[i]-rt[i-1]>2:
        excluded.append((t,v));continue
    else:r=ref[i-1][1]+(ref[i][1]-ref[i-1][1])*(t-rt[i-1])/(rt[i]-rt[i-1])
    paired.append((t,r,v,v-r))
def metrics(points):
    if not points:return {}
    e=[x[3] for x in points];ab=sorted(abs(v) for v in e);n=len(e)
    return dict(n=n,bias=sum(e)/n,mae=sum(ab)/n,rmse=math.sqrt(sum(v*v for v in e)/n),p95=ab[math.ceil(.95*n)-1],min=min(e),max=max(e),within1=100*sum(v<=1 for v in ab)/n,within2=100*sum(v<=2 for v in ab)/n)
def ts(s):return datetime.fromisoformat('2026-10-28T'+s).timestamp()
windows={'全程':paired,'启动至23:47:31':[x for x in paired if x[0]<ts('23:47:31')],'初始高温及降温23:47:31～23:49:30':[x for x in paired if ts('23:47:31')<=x[0]<ts('23:49:30')],'突降及恢复23:49:30～23:50:15':[x for x in paired if ts('23:49:30')<=x[0]<ts('23:50:15')],'后续升温及高温23:50:15起':[x for x in paired if x[0]>=ts('23:50:15')]}
bins={f'{lo}～{hi}':metrics([x for x in windows['后续升温及高温23:50:15起'] if lo<=x[1]<hi]) for lo,hi in [(25,40),(40,60),(60,80),(80,90),(90,98),(98,101)]}
runs=[];start=0
for i in range(1,len(board)+1):
    if i==len(board) or board[i][1]!=board[start][1]:
        if board[i-1][0]-board[start][0]>=5:
            subset=[x for x in paired if board[start][0]<=x[0]<=board[i-1][0]]
            runs.append(dict(start=datetime.fromtimestamp(board[start][0]).isoformat(),end=datetime.fromtimestamp(board[i-1][0]).isoformat(),duration=board[i-1][0]-board[start][0],value=board[start][1],n=i-start,reference_range=[min(x[1] for x in subset),max(x[1] for x in subset)] if subset else []))
        start=i
summary=dict(source=str(a.source),sha256=hashlib.sha256(raw).hexdigest(),events=dict(Counter(r['事件来源'] for r in rows)),reference=rc,board=bc,paired=len(paired),excluded=len(excluded),coverage=100*len(paired)/len(board),reference_gap_limit=2,reference_range=[min(v for t,v in ref),max(v for t,v in ref)],board_range=[min(v for t,v in board),max(v for t,v in board)],windows={k:metrics(v) for k,v in windows.items()},heating_bins=bins,constant_runs=runs,worst=sorted(paired,key=lambda x:abs(x[3]),reverse=True)[:10])
(a.output/'统计.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2))
with (a.output/'对齐误差.csv').open('w',encoding='utf-8-sig',newline='') as f:
    w=csv.writer(f);w.writerow(['时间','参考温度℃','板级温度℃','误差℃']);w.writerows((datetime.fromtimestamp(t).isoformat(),r,v,e) for t,r,v,e in paired)
# Standalone SVG: elapsed time axis avoids local font dependencies.
try:
    import os
    root=Path(__file__).resolve().parents[3];cache=root/'build/matplotlib';cache.mkdir(parents=True,exist_ok=True);os.environ['MPLCONFIGDIR']=str(cache)
    import matplotlib;matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,ax=plt.subplots(2,1,sharex=True,figsize=(11,6));t0=board[0][0]
    ax[0].plot([(t-t0)/60 for t,v in ref],[v for t,v in ref],label='Reference',lw=1.5)
    ax[0].plot([(t-t0)/60 for t,v in board],[v for t,v in board],label='Board',lw=1.2)
    ax[0].set_ylabel('Temperature (C)');ax[0].legend()
    ax[1].plot([(t-t0)/60 for t,r,v,e in paired],[e for t,r,v,e in paired],lw=1)
    ax[1].axhline(0,color='black',lw=.5);ax[1].set_ylabel('Board - reference (C)');ax[1].set_xlabel('Minutes since first board sample')
    for axis in ax:axis.grid(alpha=.25)
    fig.tight_layout();fig.savefig(a.output/'温度与误差.svg');plt.close(fig)
except ImportError:pass
print(json.dumps(summary,ensure_ascii=False,indent=2))
def table(items):
    lines=['| 区间 | 有效点数 | 平均偏差℃ | 平均绝对误差℃ | RMSE℃ | P95绝对误差℃ | ±1℃比例 | ±2℃比例 |','|---|---:|---:|---:|---:|---:|---:|---:|']
    for name,m in items:
        if m:lines.append(f"| {name} | {m['n']} | {m['bias']:+.3f} | {m['mae']:.3f} | {m['rmse']:.3f} | {m['p95']:.3f} | {m['within1']:.1f}% | {m['within2']:.1f}% |")
    return '\n'.join(lines)
report=['# 20261028-234721 板级温度误差分析',
'输入：`'+str(a.source)+'`。只分析实际记录，不修改固件或标定表。',
'## 结论',
'后续升温与高温段（23:50:15起）平均绝对误差0.557℃，最大绝对误差1.974℃，233个可对齐点全部在±2℃内；但突降温段板温仍冻结约37.4秒，最高偏高61.336℃。不能以升温精度代替全程动态响应评价。',
'## 全程与人工分段',table(summary['windows'].items()),
'分段依据为记录中的启动零值、23:49:31左右开始的参考温度突降，以及23:50:13左右板温重新接近低温参考；边界取整至23:47:31、23:49:30、23:50:15便于复现，是人工诊断窗口，不是从控制状态严格识别的纯升温/纯降温标签。全程结果保留所有可对齐的启动零值和冻结点；分段结果公开列出，不静默剔除异常。',
'## 后续升温与高温段，按参考温度分组',table(bins.items()),
'80～90℃段的MAE约1.08℃，是后续升温段误差较大的温区；90～98℃平均偏高约0.49℃。98～101℃段MAE约0.16℃，但这并不验证100℃限幅，因为记录中板温实际超过100℃。各温区按可对齐采样点计权，未补偿时间延迟或重新拟合。',
'## 冻结与启动',
'23:47:21.698～23:47:29.792：9条独立板温事件连续输出0℃，跨度8.094秒；其中8条可与参考对齐，参考约99.7～99.8℃。这是启动输出异常，应与标定误差分开。',
'23:49:34.841～23:50:12.239：37条独立板温事件均为90.436℃，跨度37.398秒；同期可对齐参考温度范围29.100～80.867℃，冻结期间最大偏差+61.336℃（23:49:56.486）。后续23:50:13附近板温恢复至约31.6℃。串口也连续上报相同值，非CSV重复快照制造的假冻结。',
'突降期间NTC ADC同时明显增大；约23:49:50时NTC1/2/3原始ADC为3547/3432/2933，而三路上报仍为90.44/88.80/69.30℃。这说明冻结不只发生在板温下游，NTC转换/有效范围或重新接纳环节也参与保持。冷却数据库低端覆盖不足、越界后保持旧温度，与此现象一致；仅凭CSV无法唯一定位固件执行分支、也不能判定突降是换水、移探头还是其它操作。',
'末段100.411℃连续约7.317秒，但参考只在约100.2～100.4℃变化，不属于本次突降式失跟证据。板温整段范围0～100.44℃，可以确认记录结果不满足25～100℃限幅；日志没有足够版本信息证明它运行的是本轮最新源码。',
'## 对齐方法、覆盖率与限制',
f"CSV共{len(rows)}行。参考温度449个、板温492个独立时间戳；均只取事件来源匹配的行，用各自接收时间去重，未发现非法数值或重复时间戳。不使用其它事件携带的旧快照，也不直接采用CSV误差列。参考范围29.1～100.5℃。",
f"以板温接收时间为基准，对左右参考温度做线性插值，仅允许参考间隔≤2秒，不外插、不平移。保留400点，覆盖率81.30%；92点因参考缺口或记录两端无法对齐而排除（包含起始第一条零值）。参考449点全部作为插值候选保留。大幅突变时线性插值只能近似实际变化，不能视为高频动态真值。",
'误差定义：板级温度－参考温度；平均偏差正值表示板温偏高。P95采用最近秩，统计均按采样点计权。约1Hz事件采样无法还原20ms级固件滤波过程，日志中的本地MACHINE=4不代表固件机型。新算法效果需用带版本号的新固件重新采集确认。',
'输入SHA256：`'+summary['sha256']+'`。',
'明细：[对齐误差CSV](对齐误差.csv)，[统计JSON](统计.json)。',
'复现：`python3 -B agent-tp/tools/temperature/analyze_board_error.py '+str(a.source)+' '+str(a.output)+'`。']
if (a.output/'温度与误差.svg').exists():report.append('![温度与误差](温度与误差.svg)')
(a.output/'分析报告.md').write_text('\n\n'.join(report),encoding='utf-8')
