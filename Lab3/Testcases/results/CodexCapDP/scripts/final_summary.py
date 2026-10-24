#!/usr/bin/env python3
"""Summarize measured final results; never fill missing results with estimates."""
import csv, json, math, re, statistics as st
from pathlib import Path

R = Path(__file__).resolve().parents[1]
F = R/'final'
OLD = R.parent/'ClaudeOpus55_v5'

def rd(p):
    with Path(p).open() as f: return {r['bench']: r for r in csv.DictReader(f)}

def gm(xs):
    return math.exp(st.mean(math.log(x) for x in xs))

def compare(new, chain):
    rows = []
    for b, n in new.items():
        c = chain[b]
        mx, total, cm, ct = (float(n['max']), float(n['disp']), float(c['max']), float(c['disp']))
        rows.append(dict(bench=b, verdict=n['verdict'], seconds=float(n['seconds']), max=mx, total=total,
            hpwl=float(n['hpwl_sa']), chain_max=cm, chain_total=ct,
            max_ok=mx <= cm+1e-7, total_ok=total <= ct*1.005+1e-5,
            max_gap=max(0,mx-cm), total_gap=max(0,total-ct*1.005),
            max_ratio=mx/cm if cm else 1, total_ratio=total/ct if ct else 1))
    return rows

new = rd(F/'lg_new/record.csv')
new.update(rd(R.parent/'edge/CodexCapDP/record.csv'))
chain = rd(OLD/'phase1/lg_chain/record.csv')
rows = compare(new, chain)
v5 = rd(OLD/'final/lg_v5/record.csv')
v5.update(rd(R.parent/'edge/ClaudeOpus55_v5/record.csv'))
lines = ['# CodexCapDP 最終驗證', '',
    'Stage 1 以本次 local_grade 預設執行為準；比較基準是 v5 phase1/lg_chain/record.csv。',
    '牆鐘預算會改變搜尋軌跡；調參快取與 HPWL 的 Stage 1 快取是另外標記的執行，不混成一次結果。', '',
    'cap 成本、四版本 seed4 全表、實際計費時間與大設計歷史比較見 [details.md](details.md)。', '',
    '| 分組 | bench | 最大位移 | ChainDP 最大 | 總位移 | ChainDP 總 | 最大比值 | 總比值 | 兩條件 | 秒 |',
    '|---|---|---:|---:|---:|---:|---:|---:|:---:|---:|']
groups = {}
for r in rows:
    b = r['bench']; g = 'holdout' if b.startswith('h') else 'edge' if b.startswith('e') else 'testcases'
    r['group'] = g
    groups.setdefault(g, []).append(r)
    lines.append(f"| {g} | {b} | {r['max']:.6f} | {r['chain_max']:.6f} | {r['total']:.4f} | {r['chain_total']:.4f} | {r['max_ratio']:.6f} | {r['total_ratio']:.6f} | {'✓' if r['max_ok'] and r['total_ok'] else '未達標'} | {r['seconds']:.2f} |")
stats = {}
for g, rs in [*groups.items(), ('all', rows)]:
    stats[g] = dict(cases=len(rs), passed=sum(r['verdict']=='PASS' for r in rs),
                    max_ok=sum(r['max_ok'] for r in rs), total_ok=sum(r['total_ok'] for r in rs),
                    both=sum(r['max_ok'] and r['total_ok'] for r in rs), seconds=sum(r['seconds'] for r in rs))
lines += ['', '## 未達位移目標的逐筆差距', '',
          '| bench | 最大超過 ChainDP | 最大超過 % | 總位移超過 ChainDP×1.005 | 總 ÷ ChainDP |',
          '|---|---:|---:|---:|---:|']
failures = [r for r in rows if not (r['max_ok'] and r['total_ok'])]
for r in failures:
    lines.append(f"| {r['bench']} | {r['max_gap']:.6f} | {max(0,r['max_ratio']-1)*100:.4f}% | {r['total_gap']:.6f} | {r['total_ratio']:.6f} |")

large = {}
lines += ['', '## 大設計', '', '| bench | PASS | 秒 | 上限 | 最大 | 總 | HPWL |', '|---|---|---:|---:|---:|---:|---:|']
for name, limit in [('L1',240), ('L2',600)]:
    r = rd(F/f'lg_new_{name}/record.csv')[name]
    large[name] = dict(**r, time_limit=limit, time_ok=float(r['seconds'])<=limit)
    lines.append(f"| {name} | {r['verdict']} | {r['seconds']} | {limit} | {r['max']} | {r['disp']} | {r['hpwl_sa']} |")

hpwl = {}
thresholds = {'equal':{'holdout':.9907,'edge':.9876}, 'default':{'holdout':.9919,'edge':.9938}}
for mode in ['fixed','equal','default']:
    with (F/f'hpwl_{mode}/record.csv').open() as f: data = list(csv.DictReader(f))
    hpwl[mode] = {}
    lines += ['', f'## HPWL：{mode}', '',
              '每筆 mean/min/max 見同目錄的 hpwl_'+mode+'/summary.md。下表平均採每筆三個 seed 平均之比的幾何平均；範圍是三個 seed 各自分組比值的最小／最大，非信賴區間。', '',
              '| 分組 | 新版 ÷ v4 | 三 seed 範圍 | v5 目標 | 達標 |', '|---|---:|---:|---:|:---:|']
    for g in ['holdout','edge']:
        by = {}
        for r in data:
            if r['group']==g and r['version'] in ['ClaudeOpus55_v4','CodexCapDP']:
                if r['verdict']!='PASS': raise RuntimeError('HPWL run failed: '+str(r))
                by.setdefault(r['bench'], {}).setdefault(r['version'], {})[int(r['seed'])] = float(r['hpwl'])
        ratios, seed_ratios = [], {s:[] for s in [1,2,3]}
        for b, v in by.items():
            a, n = v['ClaudeOpus55_v4'], v['CodexCapDP']
            if set(a)!=set(n) or set(n)!={1,2,3}: raise RuntimeError('Missing seed: '+b)
            ratios.append(st.mean(n.values())/st.mean(a.values()))
            for seed in seed_ratios: seed_ratios[seed].append(n[seed]/a[seed])
        ratio = gm(ratios); seed_values = [gm(x) for x in seed_ratios.values()]
        threshold = thresholds.get(mode,{}).get(g)
        hpwl[mode][g] = dict(ratio=ratio, seed_min=min(seed_values), seed_max=max(seed_values),
                           benches=len(by), threshold=threshold, passed=None if threshold is None else ratio<=threshold)
        lines.append(f"| {g} | {ratio:.6f} | [{min(seed_values):.6f}, {max(seed_values):.6f}] | {threshold if threshold else '—'} | {'—' if threshold is None else '✓' if ratio<=threshold else '未達標'} |")

seed = {v:rd(F/f'seed4_{v}/record.csv') for v in ['ClaudeOpus55_v4','CodexChainDP','ClaudeOpus55_v5','CodexCapDP']}
seed_rows = compare(seed['CodexCapDP'],seed['CodexChainDP'])
lines += ['', '## 凍結後的新 seed4', '',
          'seed=20261022；四版本都用 local_grade。以下列新版相對本次 ChainDP 的位移差距，完整四版本數字在各 seed4_*/record.csv。', '',
          '| bench | 新版最大 | 新版總 | 最大比值 | 總比值 | 最大超額 | 總超額（對1.005） |',
          '|---|---:|---:|---:|---:|---:|---:|']
for r in seed_rows:
    lines.append(f"| {r['bench']} | {r['max']:.6f} | {r['total']:.4f} | {r['max_ratio']:.6f} | {r['total_ratio']:.6f} | {r['max_gap']:.6f} | {r['total_gap']:.4f} |")
seed_pass = {v:sum(r['verdict']=='PASS' for r in rs.values()) for v,rs in seed.items()}

e08 = dict(new_total=float(new['e08_fragmented']['disp']), v5_total=float(v5['e08_fragmented']['disp']),
           chain_total=float(chain['e08_fragmented']['disp']))
e08['vs_v5'] = e08['new_total']/e08['v5_total']-1
e08['vs_chain'] = e08['new_total']/e08['chain_total']-1
lines += ['', '## e08 總位移與時間', '',
          f"本次總位移 {e08['new_total']:.4f}，相對歷史 v5 {e08['vs_v5']*100:+.4f}%，相對 ChainDP {e08['vs_chain']*100:+.4f}%。",
          '這是完整流程的差異，沒有把它當成單獨 chain 消融的因果結果；未用 edge 調參。']

extra = []
for d in [R/'phase2/full_v5', F/'ts_new_stage1']:
    for log in sorted(d.glob('*/stderr_ts.tsv')):
        if any(r['bench']==log.parent.name for r in extra): continue
        match = re.search(r'\[extra\] budget ([0-9.]+) used ([0-9.]+)',log.read_text())
        if match: extra.append(dict(bench=log.parent.name, budget=float(match[1]), seconds=float(match[2]), source=str(log)))
overruns = [r for r in extra if r['seconds'] > r['budget']]
lines += ['', '額外時間逐筆記錄在 extra_times.csv；來源是同一凍結演算法的 timestamp Stage 1 執行，與 local_grade 是不同執行。',
          f'{len(extra)-len(overruns)}/{len(extra)} 在預算內；超時如下：', '']
for r in overruns:
    lines.append(f"- {r['bench']}：{r['seconds']:.6f} 秒／{r['budget']:.1f} 秒，超出 {r['seconds']-r['budget']:.6f} 秒。")
with (F/'extra_times.csv').open('w') as f:
    w=csv.DictWriter(f,fieldnames=['bench','budget','seconds','source']);w.writeheader();w.writerows(extra)
summary = dict(groups=stats, rows=rows, failures=failures, large=large, hpwl=hpwl, e08=e08,
               seed4_rows=seed_rows, seed4_pass=seed_pass, extra=extra)
(F/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
(F/'summary.md').write_text('\n'.join(lines)+'\n')
print(json.dumps(dict(groups=stats,hpwl=hpwl,e08=e08,seed4_pass=seed_pass),ensure_ascii=False,indent=2))
