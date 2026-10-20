#!/usr/bin/env python3
"""Build small CSV/Markdown summaries from record.csv only (no placement scans)."""
import csv,json,math,statistics
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];LAB=ROOT.parents[1];OUT=LAB/'Testcases/results/CodexChainDP'

def read(path):return list(csv.DictReader(path.open())) if path.exists() else []
def gm(xs):return math.exp(statistics.mean(math.log(x) for x in xs)) if xs else None
def csvout(path,rows):
    if not rows:return
    with path.open('w') as f:w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)

def main():
    baseline={r['bench']:r for r in read(OUT/'baseline_default/record.csv')}
    current={r['bench']:r for folder in ['default','edge_CodexChainDP'] for r in read(OUT/folder/'record.csv')}
    rows=[];summary={}
    for b,c in current.items():
        if b not in baseline:continue
        v=baseline[b];group='edge' if b.startswith('e') else 'holdout' if b.startswith('h') else 'testcases'
        r=dict(bench=b,group=group,v4_pass=v['verdict'],chain_pass=c['verdict'])
        for metric in ['disp','max','hpwl_sa','seconds']:
            old,new=float(v[metric]),float(c[metric]);r['v4_'+metric]=old;r['chain_'+metric]=new
            r[metric+'_ratio']=new/old if old else (1 if not new else math.inf)
        rows.append(r)
    csvout(OUT/'default_comparison.csv',rows)
    for group in ['all','testcases','holdout','edge']:
        rr=[r for r in rows if group=='all' or r['group']==group]
        if not rr:continue
        summary[group]=dict(cases=len(rr),all_pass=all(r['v4_pass']==r['chain_pass']=='PASS' for r in rr),max_regressions=[r['bench'] for r in rr if r['chain_max']>r['v4_max']+1e-6])
        for metric in ['disp','max','hpwl_sa']:
            summary[group][metric+'_geomean']=gm([r[metric+'_ratio'] for r in rr if r['v4_'+metric]>0])
        summary[group]['v4_seconds']=sum(r['v4_seconds'] for r in rr);summary[group]['chain_seconds']=sum(r['chain_seconds'] for r in rr)
    for name,folders in [('fixed_work',['fixed_work_holdout','fixed_work_edge']),('equal_total_time',['equal_total_time']),('equal_time_default',['equal_time_default'])]:
        records=[r for folder in folders for r in read(OUT/folder/'record.csv')]
        stats=[]
        for b in sorted({r['bench'] for r in records}):
            selected=[r for r in records if r['bench']==b]
            values={v:[float(r['hpwl']) for r in selected if r['version']==v and r['verdict']=='PASS'] for v in ['ClaudeOpus55_v4','CodexChainDP']}
            if any(len(x)!=3 for x in values.values()):continue
            row={'bench':b,'group':selected[0]['group']}
            for prefix,v in [('v4','ClaudeOpus55_v4'),('chain','CodexChainDP')]:
                row[prefix+'_mean']=statistics.mean(values[v]);row[prefix+'_min']=min(values[v]);row[prefix+'_max']=max(values[v])
                timekey='charged_seconds' if name!='fixed_work' else 'stage2_seconds'
                row[prefix+'_seconds_mean']=statistics.mean(float(r[timekey]) for r in selected if r['version']==v)
            row['ratio']=row['chain_mean']/row['v4_mean']
            if name!='fixed_work':row['budget']=float(selected[0]['budget'])
            stats.append(row)
        csvout(OUT/(name+'_summary.csv'),stats)
        summary[name]={}
        for group in ['holdout','edge']:
            rr=[r for r in stats if r['group']==group]
            if not rr:continue
            summary[name][group]={'cases':len(rr),'hpwl_geomean':gm([r['ratio'] for r in rr]),'mean_ratio':statistics.mean(r['ratio'] for r in rr)}
        label='charged seconds' if name!='fixed_work' else 'Stage 2 seconds'
        extra=' 共同總預算秒數 |' if name!='fixed_work' else ''
        lines=['| bench | v4 mean [min, max] | ChainDP mean [min, max] | ratio | v4 / Chain '+label+' |'+extra,'|---|---:|---:|---:|---:|'+('---:|' if extra else '')]
        for r in stats:
            line=f"| {r['bench']} | {r['v4_mean']:,.3f} [{r['v4_min']:,.3f}, {r['v4_max']:,.3f}] | {r['chain_mean']:,.3f} [{r['chain_min']:,.3f}, {r['chain_max']:,.3f}] | {r['ratio']:.6f} | {r['v4_seconds_mean']:.3f} / {r['chain_seconds_mean']:.3f} |"
            if extra:line+=f" {r['budget']:.3f} |"
            lines.append(line)
        (OUT/(name+'_table.md')).write_text('\n'.join(lines)+'\n')
    summary['L1']=read(OUT/'large_L1/record.csv')
    summary['fresh_edge']=read(OUT/'edge_seed2/record.csv')
    fresh_base={r['bench']:r for r in read(OUT/'baseline_seed2/record.csv')}
    fresh_pairs=[]
    for c in summary['fresh_edge']:
        if c['bench'] not in fresh_base:continue
        v=fresh_base[c['bench']];r=dict(bench=c['bench'],v4_pass=v['verdict'],chain_pass=c['verdict'])
        for metric in ['disp','max','hpwl_sa','seconds']:
            old,new=float(v[metric]),float(c[metric]);r['v4_'+metric]=old;r['chain_'+metric]=new
            r[metric+'_ratio']=new/old if old else (1 if not new else math.inf)
        fresh_pairs.append(r)
    csvout(OUT/'fresh_comparison.csv',fresh_pairs)
    if fresh_pairs:
        summary['fresh_comparison']=dict(cases=len(fresh_pairs),
            all_pass=all(r['v4_pass']==r['chain_pass']=='PASS' for r in fresh_pairs),
            max_geomean=gm([r['max_ratio'] for r in fresh_pairs if r['v4_max']>0]),
            disp_geomean=gm([r['disp_ratio'] for r in fresh_pairs if r['v4_disp']>0]),
            max_regressions=[r['bench'] for r in fresh_pairs if r['chain_max']>r['v4_max']+1e-6])
    (OUT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))

if __name__=='__main__':main()
