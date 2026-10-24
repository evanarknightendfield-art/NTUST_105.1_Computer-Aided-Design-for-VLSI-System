"""Small-record analysis only; run with taskset -c 16-19 from Lab3/."""
import csv,json,math,re,statistics
from collections import defaultdict,Counter
from pathlib import Path
R=Path('Testcases/results/CodexCapDP')
def rows(path):
    if not path.exists():return []
    return list(csv.DictReader(path.open()))
def numeric(row):
    out={}
    for k,v in row.items():
        try:out[k]=float(v)
        except (ValueError,TypeError):out[k]=v
    return out
def writecsv(name,data):
    if not data:return
    fields=list(dict.fromkeys(k for r in data for k in r))
    with (R/name).open('w') as f:
        w=csv.DictWriter(f,fields);w.writeheader();w.writerows(data)
def table(head,data):
    return '\n'.join(['| '+' | '.join(head)+' |','|'+'|'.join(['---']*len(head))+'|']+['| '+' | '.join(map(str,r))+' |' for r in data])
def fmt(v):return f'{v:,.3f}'
data={};md=['# 診斷數據表（自動產生）','數字保留來源執行；不混用不同 run 的位移與時間。']
chain_ref={r['bench']:numeric(r) for r in rows(Path('Testcases/results/ClaudeOpus55_v5/phase1/lg_chain/record.csv'))}
baseline=[]
for r in rows(R/'run_A_local_grade/record.csv'):
    r=numeric(r);c=chain_ref[r['bench']]
    r.update(max_vs_chain_pct=100*(r['max']/c['max']-1),total_vs_chain_pct=100*(r['disp']/c['disp']-1));baseline.append(r)
data['baseline_A']=baseline;writecsv('baseline_comparison.csv',baseline)
if baseline:
    md+=['\n## Run A：local_grade',table(['bench','verdict','秒','最大','總位移','最大 vs Chain %','總 vs Chain %'],[[r['bench'],r['verdict'],fmt(r['seconds']),fmt(r['max']),fmt(r['disp']),fmt(r['max_vs_chain_pct']),fmt(r['total_vs_chain_pct'])] for r in baseline])]
    data['baseline_A_summary']={'seconds':sum(r['seconds'] for r in baseline),'pass':sum(r['verdict']=='PASS' for r in baseline),'both':sum(r['max_vs_chain_pct']<=1e-7 and r['total_vs_chain_pct']<=.5+1e-7 for r in baseline)}
# Correct the legacy parser's [cap] classification using the recorded cap duration.
phases=[]
for r in rows(R/'run_B_ts/record.csv'):
    r=numeric(r);ls=[]
    for line in (R/'run_B_ts'/r['bench']/'stderr_ts.tsv').read_text().splitlines():
        t,l=line.split('\t',1);ls.append((float(t),l))
    cap=[(t,l) for t,l in ls if re.match(r'\[cap\] \d+ attempts',l)]
    cap_s=float(re.search(r'\s([\d.]+)s$',cap[-1][1]).group(1)) if cap else 0
    r['cap_s']=cap_s;r['chain_only_s']=r.get('t_chain',0)-cap_s
    r['cap_attempts']=int(re.search(r'\[cap\] (\d+) attempts',cap[-1][1]).group(1)) if cap else 0
    phases.append(r)
data['phases_B']=phases;writecsv('phases_corrected.csv',phases)
a_by={r['bench']:r for r in baseline}
ab=[dict(bench=r['bench'],A_max=a_by[r['bench']]['max'],B_max=r['max'],A_total=a_by[r['bench']]['disp'],B_total=r['disp'],max_change_pct=100*(r['max']/a_by[r['bench']]['max']-1),total_change_pct=100*(r['disp']/a_by[r['bench']]['disp']-1))for r in phases if r['bench']in a_by]
data['A_B']=ab;writecsv('run_variation.csv',ab)
if phases:
    md+=['\n## Run B：階段秒數',table(['bench','legalize','pareto+DP','chain','cap','Stage1','SA','polish','總秒'],[[r['bench'],fmt(r.get('t_legalize',0)),fmt(r.get('t_pareto',0)+r.get('t_windowdp',0)),fmt(r['chain_only_s']),fmt(r['cap_s']),fmt(r['t_stage1']),fmt(r.get('sa_seconds',0)),fmt(r.get('t_polish',0)),fmt(r['seconds'])]for r in phases])]
c_runs=[]
for f in sorted((R/'run_C').glob('h*/stdout.log')):
    record={'bench':f.parent.name}
    for line in f.read_text().splitlines():
        k,_,v=line.rpartition(':')
        if k in ['Total displacement (legal)','Maximum displacement (legal)','Total HPWL (legal)']:record[k]=float(v)
    log=(f.parent/'stderr.log').read_text();m=re.search(r'\[stage1\] done\s+([\d.]+)s',log)
    if m:record['stage1_s']=float(m.group(1))
    c_runs.append(record)
data['run_C']=c_runs;writecsv('run_C_record.csv',c_runs)
attempts=[];cap_summary=[]
for f in sorted((R/'run_C').glob('h*/diag_attempts.csv')):
    bench=f.parent.name;a=[dict(bench=bench,**numeric(r))for r in rows(f)];attempts+=a
    s={'bench':bench,'attempts':len(a),'probes':sum(r['kind']=='probe'for r in a),'full':sum(r['kind']=='full'for r in a),'unresolved':sum(not r['feasible']for r in a),'total_rejected':sum(r['reason']=='total_budget'for r in a),'accepted':sum(r['accepted']for r in a),'row_s':sum(r['row_s']for r in a),'site_s':sum(r['site_s']for r in a),'dp_s':sum(r['dp_s']for r in a)}
    text=(f.parent/'stderr.log').read_text()
    matches=re.findall(r'\[cap\] \d+ attempts[^\n]*?  ([\d.]+)s',text)
    if matches:s['cap_s']=float(matches[-1]);s['other_s']=s['cap_s']-s['row_s']-s['site_s']-s['dp_s']
    full=[r for r in a if r['kind']=='full'and r['feasible']]
    if full:s.update(raw_increase_min_pct=min(100*(r['raw_total']/r['start_total']-1)for r in full),raw_increase_max_pct=max(100*(r['raw_total']/r['start_total']-1)for r in full))
    cap_summary.append(s)
data['attempts_C']=attempts;data['cap_summary_C']=cap_summary;writecsv('cap_attempts.csv',attempts);writecsv('cap_summary.csv',cap_summary)
if cap_summary:
    md+=['\n## Run C：每次 cap 的時間分類',table(['bench','probe/full','unknown','總預算拒絕','接受','row s','site s','DP s','raw 增幅範圍 %'],[[r['bench'],f"{r['probes']}/{r['full']}",r['unresolved'],r['total_rejected'],int(r['accepted']),fmt(r['row_s']),fmt(r['site_s']),fmt(r['dp_s']),f"{r.get('raw_increase_min_pct',0):.2f}–{r.get('raw_increase_max_pct',0):.2f}"]for r in cap_summary])]
plateau=[]
for f in sorted((R/'plateau').glob('h*/*/result_plateau.csv')):
    plateau += [dict(bench=f.parent.parent.name,placement=f.parent.name,**numeric(r))for r in rows(f)]
data['plateau']=plateau;writecsv('plateau.csv',plateau)
if plateau:md+=['\n## 頂部密度',table(['bench','起點','降幅','違規 cells','segments','每段最多'],[[r['bench'],r['placement'],f"{100*r['fraction']:.1f}%",int(r['cells']),int(r['segments']),int(r['max_per_segment'])]for r in plateau])]
chains=[]
for f in sorted((R/'chain').glob('h*/result_chain.csv')):
    a=[numeric(r) for r in rows(f)]
    if len(a)!=2:continue
    a,b=a
    chains.append({'bench':f.parent.name,'base_total':a['base_total'],'s1_5':a['seconds'],'s4':b['seconds'],'total1_5':a['total'],'total4':b['total'],'delta':a['total']-b['total'],'delta_pct':100*(a['total']/b['total']-1),'max1_5':a['max'],'max4':b['max'],'legal':a['legal']and b['legal']})
data['chain']=chains;writecsv('chain_comparison.csv',chains)
if chains:md+=['\n## Chain 控制比較（不含 cap）',table(['bench','1.5s 實際','4s 實際','總 1.5','總 4','差','差 %'],[[r['bench'],fmt(r['s1_5']),fmt(r['s4']),fmt(r['total1_5']),fmt(r['total4']),fmt(r['delta']),fmt(r['delta_pct'])]for r in chains])]
dp=[];cell_summary=[];stages=[]
for family in ['local_dp','forensic']:
    for f in sorted((R/family).glob('h*/result_dp.csv')):
        a=[numeric(r)for r in rows(f)]
        if not a:continue
        full=a[0]
        for r in a:r.update(bench=f.parent.name,family=family,total_vs_full_pct=100*(r['total']/full['total']-1),time_saved_pct=100*(1-r['seconds']/full['seconds']))
        dp+=a
        a=[numeric(r)for r in rows(f.parent/'result_raw_cells.csv')]
        for label,predicate in [('all',lambda r:True),('violators',lambda r:r['violator']),('nonviolators',lambda r:not r['violator']),('same_segment',lambda r:r['old_segment']==r['new_segment']),('changed_segment',lambda r:r['old_segment']!=r['new_segment'])]:
            sub=[r for r in a if predicate(r)];deltas=[r['delta_disp']for r in sub];travel=[r['travel']for r in sub]
            cell_summary.append({'bench':f.parent.name,'family':family,'category':label,'cells':len(sub),'moved':sum(t>0 for t in travel),'delta':sum(deltas),'positive':sum(max(0,x)for x in deltas),'negative':sum(min(0,x)for x in deltas),'travel':sum(travel),'max_travel':max(travel,default=0)})
        # Keep the ten largest displacement increases as named, reviewable examples.
        writecsv(f'{family}_{f.parent.name}_top_cells.csv',sorted(a,key=lambda r:r['delta_disp'],reverse=True)[:10])
    for f in sorted((R/family).glob('h*/result*_stages.csv')):stages +=[dict(bench=f.parent.name,family=family,source=f.name,**numeric(r))for r in rows(f)]
data.update(dp=dp,cells=cell_summary,stages=stages);writecsv('local_dp_comparison.csv',dp);writecsv('cell_attribution.csv',cell_summary);writecsv('membership_stages.csv',stages)
if dp:md+=['\n## 相同 raw 起點的 DP',table(['組','bench','mode','秒','總','max','總 vs full %','省時 %','mask'],[[r['family'],r['bench'],r['mode'],fmt(r['seconds']),fmt(r['total']),fmt(r['max']),fmt(r['total_vs_full_pct']),fmt(r['time_saved_pct']),int(r['mask_cells'])]for r in dp])]
if cell_summary:md+=['\n## Raw 位移來源',table(['組','bench','類別','移動 cells','淨增','正增','負減','實際移動距離'],[[r['family'],r['bench'],r['category'],r['moved'],fmt(r['delta']),fmt(r['positive']),fmt(r['negative']),fmt(r['travel'])]for r in cell_summary])]
if stages:md+=['\n## Membership 的後處理重建（反事實座標）',table(['bench','階段','總','max','離開原段 cells','重建段數'],[[r['bench'],r['stage'],fmt(r['total']),fmt(r['max']),int(r['away']),int(r['dirty_segments'])]for r in stages])]
audits=[]
for family in ['local_dp','forensic']:
    for f in sorted((R/family).glob('h*/result_memo_audit.csv')):audits += [dict(bench=f.parent.name,family=family,**numeric(r))for r in rows(f)]
data['memo_audit']=audits;writecsv('memo_audit.csv',audits)
l2=[numeric(r)for r in rows(R/'L2/result_schedule.csv')];data['L2']=l2
if l2:md+=['\n## L2 單 seed',table(['排程','起點','SA 後','final','SA 秒','polish 秒','總秒','attempts'],[[r['schedule'],fmt(r['start_hpwl']),fmt(r['postsa_hpwl']),fmt(r['final_hpwl']),fmt(r['sa_s']),fmt(r['polish_s']),fmt(r['total_s']),int(r['sa_attempts'])]for r in l2])]
(R/'summary.json').write_text(json.dumps(data,indent=2))
(R/'tables.md').write_text('\n\n'.join(md)+'\n')
print(json.dumps({k:len(v)for k,v in data.items() if isinstance(v,list)}))
