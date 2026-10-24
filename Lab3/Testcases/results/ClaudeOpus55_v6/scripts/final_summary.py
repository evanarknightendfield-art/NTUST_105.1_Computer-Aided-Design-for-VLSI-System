#!/usr/bin/env python3
"""final_summary.py — final/summary.md + summary.json from phase2_final.py outputs (reads only).

Criterion per bench (the task's): max <= ChainDP max and total <= ChainDP total x 1.005, ChainDP = the same
final session's run. HPWL: per bench the mean of 3 seeds, ratio to v4's mean, geometric mean over the group;
range = the group geometric mean of each seed alone (min / max), not a confidence interval.
"""
import csv, json, math, re, statistics as st
from pathlib import Path

R = Path(__file__).resolve().parents[1]
F = R / 'final'
VERS = ['ClaudeOpus55_v6', 'CodexCapDP', 'ClaudeOpus55_v5', 'CodexChainDP']
SHORT = {'ClaudeOpus55_v6': 'v6', 'CodexCapDP': 'CapDP', 'ClaudeOpus55_v5': 'v5', 'CodexChainDP': 'ChainDP', 'ClaudeOpus55_v4': 'v4'}


def load(*paths):
    out = {}
    for p in paths:
        if Path(p).exists():
            for r in csv.DictReader(open(p)):
                out[r['bench']] = r
    return out


def gm(xs):
    xs = [x for x in xs if x and x > 0]
    return math.exp(sum(map(math.log, xs)) / len(xs)) if xs else float('nan')


def disp_section(title, recs, ref_name, lines, js, key):
    ref = recs[ref_name]
    lines += [f'## {title}', '', f'參考 = 同一輪的 {SHORT[ref_name]}。雙條件：最大 ≤ 參考、總位移 ≤ 參考 × 1.005。', '',
              '| 版本 | PASS | 最大達標 | 總位移達標 | 雙條件 | 最大 GM | 總 GM | HPWL GM | 秒合計 |', '|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    fails = {}
    for v, rec in recs.items():
        if v == ref_name:
            continue
        n = okm = okt = both = 0
        mr, tr, hr, secs, f = [], [], [], 0.0, []
        for b, r0 in ref.items():
            if b not in rec:
                continue
            r = rec[b]
            n += 1
            m, m0, t, t0 = float(r['max']), float(r0['max']), float(r['disp']), float(r0['disp'])
            a, c = m <= m0 + 1e-9, t <= t0 * 1.005 + 1e-9
            okm += a; okt += c; both += a and c
            secs += float(r['seconds'])
            if m0 > 0: mr.append(m / m0)
            if t0 > 0: tr.append(t / t0)
            hr.append(float(r['hpwl_sa']) / float(r0['hpwl_sa']))
            if not (a and c):
                f.append(dict(bench=b, max=m, ref_max=m0, max_pct=(m / m0 - 1) * 100 if m0 else 0, total=t, ref_total=t0,
                              total_pct=(t / t0 - 1) * 100 if t0 else 0, total_over=max(0.0, t - 1.005 * t0)))
        passes = sum(rec[b]['verdict'] == 'PASS' for b in ref if b in rec)
        lines.append(f'| {SHORT[v]} | {passes}/{n} | {okm}/{n} | {okt}/{n} | {both}/{n} | {gm(mr):.5f} | {gm(tr):.5f} | {gm(hr):.5f} | {secs:.1f} |')
        js[key + '_' + v] = dict(pass_=passes, n=n, ok_max=okm, ok_total=okt, both=both, max_gm=gm(mr), total_gm=gm(tr), hpwl_gm=gm(hr), seconds=secs, fails=f)
        fails[v] = f
    lines.append('')
    for v, f in fails.items():
        if not f:
            continue
        lines += [f'{SHORT[v]} 未達標：', '', '| bench | 最大 | 參考最大 | 最大差 | 總位移 | 參考總 | 總差 | 總超過 ×1.005 |', '|---|---:|---:|---:|---:|---:|---:|---:|']
        lines += [f"| {x['bench']} | {x['max']:.4f} | {x['ref_max']:.4f} | {x['max_pct']:+.3f}% | {x['total']:.2f} | {x['ref_total']:.2f} | "
                  f"{x['total_pct']:+.3f}% | {x['total_over']:.3f} |" for x in f]
        lines.append('')


def extra_times(lines, js):
    lines += ['## 額外步驟的時間（ts --no-sa，holdout + edge；testcases 見 extra_testcases）', '',
              '| 版本 | 筆數 | 超出預算 | 最大超出秒 |', '|---|---:|---:|---|']
    for v in ['ClaudeOpus55_v6', 'CodexCapDP']:
        over, n, worst = [], 0, (None, -1e9)
        for d in [F / f'ts_{v}', F / f'extra_testcases_{v}']:
            if not d.exists():
                continue
            for p in sorted(d.glob('*/stderr_ts.tsv')):
                for ln in open(p):
                    m = re.search(r'\[extra\] budget ([\d.]+) used ([\d.]+)', ln)
                    if m:
                        n += 1
                        o = float(m.group(2)) - float(m.group(1))
                        if o > 0: over.append((p.parent.name, o))
                        if o > worst[1]: worst = (p.parent.name, o)
        lines.append(f"| {SHORT[v]} | {n} | {len(over)} {' '.join(f'{b} +{o:.3f}s' for b, o in over)} | {worst[0]} {worst[1]:+.3f} |")
        js['extra_' + v] = dict(n=n, over=over, worst=worst)
    lines.append('')


def large(lines, js):
    lines += ['## 大設計（local_grade，各一次 seed 1）', '', '| bench | 版本 | PASS | 秒 | 上限 | 最大 | 總 | HPWL | HPWL ÷ v5 |', '|---|---|---|---:|---:|---:|---:|---:|---:|']
    for large_, lim in [('L1', 240), ('L2', 600)]:
        v5 = load(F / f'lg_ClaudeOpus55_v5_{large_}/record.csv').get(large_)
        for v in ['ClaudeOpus55_v6', 'CodexCapDP', 'ClaudeOpus55_v5']:
            r = load(F / f'lg_{v}_{large_}/record.csv').get(large_)
            if not r:
                continue
            q = float(r['hpwl_sa']) / float(v5['hpwl_sa']) if v5 else float('nan')
            lines.append(f"| {large_} | {SHORT[v]} | {r['verdict']} | {float(r['seconds']):.1f} | {lim} | {float(r['max']):.3f} | "
                         f"{float(r['disp']):.1f} | {float(r['hpwl_sa']):.6e} | {q:.4f} |")
            js[f'{large_}_{v}'] = r
    lines.append('')


def hpwl(lines, js):
    lines += ['## HPWL（3 seeds，÷ 同一輪 v4）', '',
              '每筆三 seed 平均之比的幾何平均；括號 = 每個 seed 單獨的分組幾何平均 min–max（不是信賴區間）。', '',
              '| 協定 | 分組 | v6 ÷ v4 | CapDP ÷ v4 | v6 ÷ CapDP |', '|---|---|---|---|---|']
    for mode in ['fixed', 'equal', 'default']:
        p = F / f'hpwl_{mode}' / 'record.csv'
        if not p.exists():
            continue
        rows = [r for r in csv.DictReader(open(p)) if r['verdict'] == 'PASS']
        by = {}
        for r in rows:
            by.setdefault((r['group'], r['bench']), {}).setdefault(r['version'], {})[int(r['seed'])] = float(r['hpwl'])
        for group in ['holdout', 'edge']:
            def ratio(a, b):
                per, seeds = [], {1: [], 2: [], 3: []}
                for (g, bench), d in by.items():
                    if g != group or a not in d or b not in d or len(d[a]) < 3 or len(d[b]) < 3:
                        continue
                    per.append(st.mean(d[a].values()) / st.mean(d[b].values()))
                    for s in seeds:
                        seeds[s].append(d[a][s] / d[b][s])
                rs = [gm(x) for x in seeds.values()]
                return gm(per), min(rs), max(rs), len(per)
            cells, rec = [], {}
            for a, b in [('ClaudeOpus55_v6', 'ClaudeOpus55_v4'), ('CodexCapDP', 'ClaudeOpus55_v4'), ('ClaudeOpus55_v6', 'CodexCapDP')]:
                g, lo, hi, n = ratio(a, b)
                cells.append(f'{g:.5f}（{lo:.5f}–{hi:.5f}，{n} 筆）')
                rec[f'{SHORT[a]}/{SHORT[b]}'] = dict(gm=g, lo=lo, hi=hi, n=n)
            lines.append(f'| {mode} | {group} | ' + ' | '.join(cells) + ' |')
            js[f'hpwl_{mode}_{group}'] = rec
    lines.append('')


def main():
    lines = ['# ClaudeOpus55_v6 最終驗證', '',
             '同一段時間、同一台機器配對重跑（phase2_final.py）；凍結 manifest 見 ../freeze.json。逐筆 record 在各子資料夾。', '']
    js = {}
    recs = {v: load(F / f'lg_{v}/record.csv', F / f'edge_{v}/record.csv') for v in VERS}
    disp_section('位移：testcases + holdout + edge（27 筆）', recs, 'CodexChainDP', lines, js, 'd27')
    seed = {v: load(F / f'seed5_{v}/record.csv') for v in VERS + ['ClaudeOpus55_v4']}
    seed = {v: r for v, r in seed.items() if r}
    if 'CodexChainDP' in seed:
        disp_section('凍結後新 seed 20261023（edge_seed5，16 筆，一次）', seed, 'CodexChainDP', lines, js, 'seed5')
    extra_times(lines, js)
    large(lines, js)
    hpwl(lines, js)
    (F / 'summary.md').write_text('\n'.join(lines) + '\n')
    (F / 'summary.json').write_text(json.dumps(js, indent=1, default=str) + '\n')


if __name__ == '__main__':
    main()
