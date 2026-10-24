#!/usr/bin/env python3
"""Read completed measurements only; produce provenance-aware delivery tables."""
import csv
import json
import math
import re
import statistics as st
from pathlib import Path

R = Path(__file__).resolve().parents[1]
F = R / 'final'
OLD = R.parent / 'ClaudeOpus55_v5'


def read(path):
    with path.open() as f:
        return list(csv.DictReader(f))


def dump_csv(name, rows):
    with (F / name).open('w') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)


def gm(xs):
    return math.exp(st.mean(math.log(x) for x in xs))


lines = ['# 最終驗證補充：計時、協定與四版本新 seed', '',
         '只讀取已完成的紀錄；沒有補跑或用 edge 調參。', '',
         '## cap 嘗試成本', '',
         '舊版是第一階段 run C 儀器化執行，新版是 phase2/full_v5（凍結演算法，最後只有 help 字串修正）。',
         '兩次的目標 D、起點與候選數不同，這是搜尋成本觀察，不能視為同一候選的加速比。',
         '舊版 full 不含 feasibility probe；新版沒有 probe。表中平均只計找到可行解的完整候選，時間為修復＋DP；',
         '新版 DP 欄也包含候選的短 chain 回收與統計。remaining 是局部修復後、fallback 前的違規數。', '',
         '| bench | 舊 probe/full | 新嘗試/可行/接受 | 舊 full 平均秒 | 新可行平均秒 | 舊 DP 平均秒 | 新 DP 平均秒 | 舊 cap 秒 | 新 cap 秒 |',
         '|---|---:|---:|---:|---:|---:|---:|---:|---:|']
old = read(R / 'cap_attempts.csv')
attempts, timing = [], []
pat = re.compile(r'\[cap-local\] D ([\d.]+) method (\d+) repair ([\d.]+) DP ([\d.]+) windows (\d+) remaining (\d+) raw ([\d.]+) total ([\d.]+) max ([\d.]+) (\w+)')
for b in ['h01', 'h04', 'h06', 'h07', 'h08']:
    text = (R / 'phase2/full_v5' / b / 'stderr_ts.tsv').read_text()
    records = []
    for i, m in enumerate(pat.finditer(text), 1):
        r = dict(bench=b, attempt=i, D=float(m[1]), method=int(m[2]), repair_s=float(m[3]), dp_s=float(m[4]),
                 windows=int(m[5]), remaining_before_fallback=int(m[6]), raw_total=float(m[7]),
                 total=float(m[8]), max=float(m[9]), reason=m[10])
        records.append(r)
    attempts.extend(records)
    feasible = [r for r in records if r['reason'] != 'unresolved']
    old_b = [r for r in old if r['bench'] == b]
    full = [r for r in old_b if r['kind'] == 'full']
    def cap_seconds(text):
        return float(re.search(r'\[cap\] \d+ attempts.*?([\d.]+)s\s*$', text, re.M)[1])
    t = dict(bench=b, old_probe=sum(r['kind'] == 'probe' for r in old_b), old_full=len(full),
             new_attempts=len(records), new_feasible=len(feasible), new_accepted=sum(r['reason']=='accepted' for r in records),
             old_candidate_mean=st.mean(sum(float(r[k]) for k in ['row_s','site_s','dp_s']) for r in full),
             new_candidate_mean=st.mean(r['repair_s']+r['dp_s'] for r in feasible),
             old_dp_mean=st.mean(float(r['dp_s']) for r in full), new_dp_mean=st.mean(r['dp_s'] for r in feasible),
             old_cap_seconds=cap_seconds((R/'run_C'/b/'stderr.log').read_text()), new_cap_seconds=cap_seconds(text))
    timing.append(t)
    lines.append(f"| {b} | {t['old_probe']}/{t['old_full']} | {t['new_attempts']}/{t['new_feasible']}/{t['new_accepted']} | {t['old_candidate_mean']:.4f} | {t['new_candidate_mean']:.4f} | {t['old_dp_mean']:.4f} | {t['new_dp_mean']:.4f} | {t['old_cap_seconds']:.2f} | {t['new_cap_seconds']:.2f} |")
dump_csv('cap_attempts.csv', attempts)
dump_csv('cap_timing.csv', timing)

summary = json.loads((F/'summary.json').read_text())
over = [dict(**r, excess=r['seconds']-r['budget']) for r in summary['extra'] if r['seconds'] > r['budget']]
lines += ['', '## 額外階段時間', '',
          f"27 筆有 {27-len(over)}/27 在指定預算內；主要 testcases＋holdout 11/11 在內。", '',
          '| bench | 秒 | 預算 | 超出秒 |', '|---|---:|---:|---:|']
for r in over:
    lines.append(f"| {r['bench']} | {r['seconds']:.6f} | {r['budget']:.1f} | {r['excess']:.6f} |")
lines += ['', 'e07 timestamp 最後一個 unresolved 嘗試：repair 1.822982 秒、DP 0 秒；整體 extra 6.245429 秒。',
          '可定位到修復呼叫超時，未量出其內部各子步驟時間；deadline 檢查是協作式，預留開銷仍不能保證硬上限。',
          '來源：[extra_times.csv](extra_times.csv)、[e07 log](ts_new_stage1/e07_tworows/stderr_ts.tsv)。']

protocol, per_bench, fixed_mismatch = {}, [], []
for mode in ['fixed','equal','default']:
    data = read(F/f'hpwl_{mode}/record.csv')
    by = {}
    for r in data:
        by.setdefault((r['group'],r['bench']), {}).setdefault(r['version'], {})[int(r['seed'])] = r
    protocol[mode] = dict(rows=len(data), passed=sum(r['verdict']=='PASS' for r in data), groups={})
    for g in ['holdout','edge']:
        protocol[mode]['groups'][g] = {}
        for v in sorted(set(r['version'] for r in data)):
            rs = [r for r in data if r['version']==v and r['group']==g]
            info = dict(rows=len(rs), charged_mean=st.mean(float(r['charged_seconds']) for r in rs),
                        sa_mean=st.mean(float(r['sa_seconds']) for r in rs), attempts_mean=st.mean(float(r['attempts']) for r in rs))
            if mode == 'equal':
                info.update(budget_mean=st.mean(float(r['budget']) for r in rs),
                            max_excess=max(float(r['charged_seconds'])-float(r['budget']) for r in rs))
            protocol[mode]['groups'][g][v] = info
    for (g,b), versions in by.items():
        n = versions['CodexCapDP']
        base = versions['ClaudeOpus55_v4']
        row = dict(mode=mode,group=g,bench=b,new_mean=st.mean(float(r['hpwl']) for r in n.values()),
                   v4_mean=st.mean(float(r['hpwl']) for r in base.values()))
        row['new_over_v4'] = row['new_mean']/row['v4_mean']
        prev = versions.get('ClaudeOpus55_v5')
        row['v5_mean'] = st.mean(float(r['hpwl']) for r in prev.values()) if prev else ''
        row['new_over_v5'] = row['new_mean']/row['v5_mean'] if prev else ''
        per_bench.append(row)
        if mode == 'fixed':
            for seed, r in n.items():
                if any(r[k] != prev[seed][k] for k in ['hpwl','attempts']):
                    fixed_mismatch.append(dict(bench=b, seed=seed))
dump_csv('hpwl_per_bench.csv', per_bench)
lines += ['', '## HPWL 協定核對', '',
          f"fixed：新版與 v5 的 24×3 筆 HPWL/attempts 不同者 {len(fixed_mismatch)} 筆；fixed 沒有測 Stage 1 或 polish 改動。", '',
          'equal 是相同總時間上限，實際可提早完成；Stage 1 快取成本每個 seed 計入一次。',
          'v4 與新版本次配對執行；v5 equal 只用歷史目標，不宣稱是同一輪配對。fixed/default 的 v4/v5 參考紀錄沿用歷史資料，來源 hash 在 reuse.json。', '',
          '| equal 分組 | 版本 | 平均總預算秒 | 平均計費秒 | 最大超預算秒 |', '|---|---|---:|---:|---:|']
for g, vs in protocol['equal']['groups'].items():
    for v, r in vs.items():
        lines.append(f"| {g} | {v} | {r['budget_mean']:.3f} | {r['charged_mean']:.3f} | {max(0,r['max_excess']):.3f} |")
lines += ['', '預設參數下，以下逐筆三 seed 平均比 v5 高；分組驗收只有 holdout 未達標，這不是額外的逐筆 HPWL 驗收條件。', '',
          '| 分組 | bench | 新版 ÷ v5 − 1 |', '|---|---|---:|']
for r in per_bench:
    if r['mode']=='default' and r['new_over_v5']>1+1e-12:
        lines.append(f"| {r['group']} | {r['bench']} | {(r['new_over_v5']-1)*100:+.4f}% |")

versions = ['ClaudeOpus55_v4','CodexChainDP','ClaudeOpus55_v5','CodexCapDP']
seed = {v:{r['bench']:r for r in read(F/f'seed4_{v}/record.csv')} for v in versions}
seed_stats = {}
lines += ['', '## seed4 四版本完整比較', '',
          'seed=20261022，每版一次 local_grade 預設 seed 1；以下比值以本次 ChainDP 為分母。',
          '位移幾何平均排除共同的兩筆零位移，n=14；HPWL n=16，不能當成三 seed 協定。', '',
          '| 版本 | PASS | 最大 ÷ Chain | 總 ÷ Chain | HPWL ÷ Chain | 總秒數 |', '|---|---:|---:|---:|---:|---:|']
for v, rs in seed.items():
    s = dict(passed=sum(r['verdict']=='PASS' for r in rs.values()), seconds=sum(float(r['seconds']) for r in rs.values()))
    for key in ['max','disp','hpwl_sa']:
        s[key+'_over_chain'] = gm([float(r[key])/float(seed['CodexChainDP'][b][key]) for b,r in rs.items() if float(seed['CodexChainDP'][b][key])>0])
    seed_stats[v] = s
    lines.append(f"| {v} | {s['passed']}/16 | {s['max_over_chain']:.6f} | {s['disp_over_chain']:.6f} | {s['hpwl_sa_over_chain']:.6f} | {s['seconds']:.2f} |")
lines += ['', '各格依序為 **最大位移 / 總位移 / HPWL / 秒**。', '',
          '| bench | v4 | ChainDP | v5 | CodexCapDP |', '|---|---:|---:|---:|---:|']
for b in seed['CodexCapDP']:
    cells = []
    for v in versions:
        r = seed[v][b]
        cells.append(' / '.join(f'{float(r[k]):.4f}' for k in ['max','disp','hpwl_sa','seconds']))
    lines.append('| '+b+' | '+' | '.join(cells)+' |')

large = {}
lines += ['', '## 大設計歷史比較', '', '兩輪都是單 seed，合法化的 90 秒預算會改變起點，不能把差異全歸因於 SA/polish 排程。', '',
          '| bench | 新秒 / v5秒 | 新 HPWL / v5 HPWL | HPWL 變化 |', '|---|---:|---:|---:|']
for b in ['L1','L2']:
    n = read(F/f'lg_new_{b}/record.csv')[0]
    p = read(OLD/f'final/lg_v5_{b}/record.csv')[0]
    large[b] = dict(new=n, historical_v5=p, hpwl_change=float(n['hpwl_sa'])/float(p['hpwl_sa'])-1)
    lines.append(f"| {b} | {n['seconds']} / {p['seconds']} | {n['hpwl_sa']} / {p['hpwl_sa']} | {large[b]['hpwl_change']*100:+.4f}% |")

(F/'details.md').write_text('\n'.join(lines)+'\n')
(F/'details.json').write_text(json.dumps(dict(cap=timing,extra_overruns=over,hpwl_protocol=protocol,
    fixed_v5_mismatches=fixed_mismatch,seed4=seed_stats,large=large),ensure_ascii=False,indent=2)+'\n')
print('\n'.join(lines[:18]))
print(json.dumps(dict(extra_overruns=over,fixed_mismatches=fixed_mismatch,seed4=seed_stats),ensure_ascii=False,indent=2))
