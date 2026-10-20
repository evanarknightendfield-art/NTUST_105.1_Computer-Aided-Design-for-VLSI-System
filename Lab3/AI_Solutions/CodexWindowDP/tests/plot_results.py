#!/usr/bin/env python3
"""Export comparison and SA trace figures from actual recorded measurements."""
import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
RES=ROOT.parents[1]/'Testcases/results'/ROOT.name   # Lab3 convention: run outputs live in Testcases/results
DEST=RES/'figures'
DEST.mkdir(parents=True,exist_ok=True)
rows=list(csv.DictReader((RES/'comparison/records.csv').open()))
assert len(rows)==66 and all(r['verdict']=='PASS' for r in rows)
data={(r['solution'],r['bench']):r for r in rows}
benches=['toy','ibm01','ibm05']+[f'h{i:02d}' for i in range(1,9)]
fig,axes=plt.subplots(1,3,figsize=(13,4.6),layout='constrained')
for ax,key,title in zip(axes,['disp','hpwl_final','seconds'],['Total displacement','Final HPWL','Elapsed time']):
    changes=[100*(float(data['CodexWindowDP',b][key])/float(data['ClaudeOpus55_v3',b][key])-1) for b in benches]
    ax.barh(benches,changes,color=['#277d9b' if x<=0 else '#cc6b49' for x in changes])
    ax.axvline(0,color='#303b48',lw=0.8)
    ax.invert_yaxis();ax.set_title(title,weight='bold');ax.set_xlabel('Change vs v3 (%) — lower is better')
    ax.grid(axis='x',alpha=0.2);ax.set_axisbelow(True)
fig.suptitle('CodexWindowDP vs Claude v3 | same SA attempt budget | seed 1',weight='bold')
fig.savefig(DEST/'comparison.png',dpi=180)
plt.close(fig)

fig,axes=plt.subplots(2,1,figsize=(9,6),sharex=True,layout='constrained',gridspec_kw={'height_ratios':[2,1]})
for label,color in [('ClaudeOpus55_v3','#7d8590'),('CodexWindowDP','#277d9b')]:
    trace=list(csv.DictReader((RES/f'comparison/{label}/ibm01_s1/trace.csv').open()))
    it=np.array([float(r['iteration']) for r in trace])/1e6
    axes[0].plot(it,[float(r['current'])/1e6 for r in trace],color=color,lw=0.7,alpha=0.5,label=label+' current')
    axes[0].plot(it,[float(r['best'])/1e6 for r in trace],color=color,lw=1.5,label=label+' best')
    axes[1].semilogy(it,[float(r['temperature']) for r in trace],color=color,label=label)
    # The final point includes deterministic post-SA refinement, shown as a
    # separate marker rather than pretending it consumed more SA iterations.
    final=float(data[label,'ibm01']['hpwl_final'])/1e6
    axes[0].scatter([it[-1]],[final],marker='D',s=24,color=color,zorder=5)
axes[0].set(title='ibm01: actual SA trajectory (diamond = final Stage 2 result)',ylabel='HPWL (millions)')
axes[0].legend(fontsize=8);axes[1].set(xlabel='Attempted SA moves (millions)',ylabel='Temperature')
for ax in axes:ax.grid(alpha=0.2)
fig.savefig(DEST/'sa_ibm01.png',dpi=180)
print('wrote',DEST)
