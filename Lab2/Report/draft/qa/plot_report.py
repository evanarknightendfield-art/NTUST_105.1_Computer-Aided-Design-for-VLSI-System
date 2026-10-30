from pathlib import Path
import sys, os
os.environ['MPLCONFIGDIR']='/tmp/lab2-report-mpl'
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager
ROOT=Path('/home/evan/Desktop/NTUST/105_1/Class/Computer_Aided_Design_for_VLSI_System/Lab2')
OUT=ROOT/'M11502102_report/draft'; QA=OUT/'qa'
sys.path.insert(0,str(ROOT/'grader/grader'))
import verifier

# Plot data comes directly from the saved trees; every sink is shown.
font_manager.fontManager.addfont('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc')
plt.rcParams.update({'font.family':'Noto Sans CJK JP','font.size':9,'axes.unicode_minus':False,'axes.spines.top':False,'axes.spines.right':False})
fig,ax=plt.subplots(figsize=(6.8,1.5))
colors=['#2A78D6','#C85327']
for idx,tag in enumerate(['v1','v5']):
 inp=verifier.parse_input(ROOT/'testcases/input004.cbi')
 bufs,_,levels,_=verifier.parse_output(ROOT/f'M11502102_report/results/testcases/{tag}/output004.cbi')
 coords={'SRC':inp[3],**inp[4],**{b:xy for b,(_,xy) in bufs.items()}}
 arrivals={'SRC':0}
 for lev,groups in sorted(levels):
  for p,kids in groups.items():
   for ch in kids: arrivals[ch]=arrivals[p]+verifier.manhattan(coords[p],coords[ch])
 ts=sorted(arrivals[s] for s in inp[4]); y=1-idx
 ax.hlines(y,min(ts),max(ts),color=colors[idx],linewidth=2)
 ax.scatter(ts,[y+((i%7)-3)*0.035 for i in range(len(ts))],s=10,color=colors[idx],alpha=.8,zorder=3)
 ax.text(max(ts)+12,y,f'{min(ts)}–{max(ts)}',va='center',fontsize=9,color=colors[idx])
ax.set_yticks([1,0],['v1  基礎建構','v5  最終結果'])
ax.set_xlim(0,830);ax.set_ylim(-.42,1.42)
ax.set_xlabel('Sink 到達時間（Manhattan 路徑長度）',labelpad=3)
ax.set_xticks([0,200,400,600,800]);ax.grid(axis='x',color='#dddddd',linewidth=.6)
ax.spines['left'].set_visible(False);ax.tick_params(axis='y',length=0)
fig.subplots_adjust(left=.17,right=.99,bottom=.32,top=.94)
fig.savefig(QA/'arrival_comparison.png',dpi=260,facecolor='white')
plt.close(fig)

