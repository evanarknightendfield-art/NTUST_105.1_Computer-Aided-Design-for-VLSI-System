#!/usr/bin/env python3
"""final_report_v7.py — final/summary.md + summary.json from final_validate_v7.py outputs (reads only).
(v6's final_report.py + the 11-case table ÷ v1, v6 on the new seed, L1/L2 seeds 1-3.)

usage: final_report_v7.py [--new NAME] [--base CodexParallelChain] [--out PREFIX]
Criterion per bench: max <= ChainDP max and total <= ChainDP total x 1.005 (ChainDP of the same session).
HPWL: per bench the mean of 3 seeds, ratio to v4's mean, geometric mean over the group; range = each seed's
group geometric mean (min / max), not a confidence interval. Prints only the headline lines.
"""
import argparse, csv, json, math, re, statistics as st
from pathlib import Path

R = Path(__file__).resolve().parents[1]
F = R / 'final'
ap = argparse.ArgumentParser()
ap.add_argument('--new', default=R.name)
ap.add_argument('--base', default='CodexParallelChain')
ap.add_argument('--out', default=str(F / 'summary'), help='writes OUT.md and OUT.json')
a = ap.parse_args()
NEW, BASE, REF, V4, V6, V1 = a.new, a.base, 'CodexChainDP', 'ClaudeOpus55_v4', 'ClaudeOpus55_v6', 'ClaudeOpus55'


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


def disp(title, recs, lines, js, key):
    ref = recs[REF]
    lines += [f'## {title}', '', '參考 = 同一輪的 ChainDP。雙條件：最大 ≤ 參考、總位移 ≤ 參考 × 1.005。', '',
              '| 版本 | PASS | 最大達標 | 總位移達標 | 雙條件 | 最大 GM | 總 GM | HPWL GM | 秒合計 |', '|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    fails = {}
    for v, rec in recs.items():
        if v == REF or not rec:
            continue
        n = okm = okt = both = 0
        mr, tr, hr, secs, f = [], [], [], 0.0, []
        for b, r0 in ref.items():
            if b not in rec:
                continue
            r = rec[b]
            n += 1
            m, m0, t, t0 = float(r['max']), float(r0['max']), float(r['disp']), float(r0['disp'])
            x, y = m <= m0 + 1e-9, t <= t0 * 1.005 + 1e-9
            okm += x; okt += y; both += x and y
            secs += float(r['seconds'])
            if m0 > 0: mr.append(m / m0)
            if t0 > 0: tr.append(t / t0)
            hr.append(float(r['hpwl_sa']) / float(r0['hpwl_sa']))
            if not (x and y):
                f.append(dict(bench=b, max=m, ref_max=m0, max_pct=(m / m0 - 1) * 100 if m0 else 0, total=t, ref_total=t0,
                              total_pct=(t / t0 - 1) * 100 if t0 else 0, total_over=max(0.0, t - 1.005 * t0)))
        passes = sum(rec[b]['verdict'] == 'PASS' for b in ref if b in rec)
        lines.append(f'| {v} | {passes}/{n} | {okm}/{n} | {okt}/{n} | {both}/{n} | {gm(mr):.5f} | {gm(tr):.5f} | {gm(hr):.5f} | {secs:.1f} |')
        js[f'{key}_{v}'] = dict(pass_=passes, n=n, ok_max=okm, ok_total=okt, both=both, max_gm=gm(mr), total_gm=gm(tr),
                                hpwl_gm=gm(hr), seconds=secs, fails=f)
        fails[v] = f
    lines.append('')
    for v, f in fails.items():
        if f:
            lines += [f'{v} 未達標：', '', '| bench | 最大 | 參考最大 | 最大差 | 總位移 | 參考總 | 總差 | 總超過 ×1.005 |', '|---|---:|---:|---:|---:|---:|---:|---:|']
            lines += [f"| {x['bench']} | {x['max']:.4f} | {x['ref_max']:.4f} | {x['max_pct']:+.3f}% | {x['total']:.2f} | "
                      f"{x['ref_total']:.2f} | {x['total_pct']:+.3f}% | {x['total_over']:.3f} |" for x in f]
            lines.append('')


def extra(lines, js):
    lines += ['## 額外步驟的時間（ts --no-sa：holdout + edge + testcases）', '', '| 版本 | 筆數 | 超出預算 | 最大超出秒 |', '|---|---:|---|---|']
    for v in [NEW, BASE]:
        over, n, worst = [], 0, (None, -1e9)
        for d in [F / f'ts_{v}', F / f'extra_testcases_{v}']:
            for p in sorted(d.glob('*/stderr_ts.tsv')):
                for ln in open(p):
                    m = re.search(r'\[extra\] budget ([\d.]+) used ([\d.]+)', ln)
                    if m:
                        n += 1
                        o = float(m.group(2)) - float(m.group(1))
                        if o > 0: over.append((p.parent.name, round(o, 3)))
                        if o > worst[1]: worst = (p.parent.name, o)
        lines.append(f"| {v} | {n} | {len(over)} {over} | {worst[0]} {worst[1]:+.3f} |")
        js['extra_' + v] = dict(n=n, over=over, worst=worst)
    lines.append('')


def large(lines, js):
    lines += ['## 大設計（SA seed 1、2、3 各一次；Stage 1 與 seed 無關，但每次重跑）', '',
              '| bench | 版本 | PASS | 秒（平均，範圍） | 最大（平均，範圍） | 總位移（平均，範圍） | HPWL（平均，範圍） |', '|---|---|---|---|---|---|---|']
    for lg in ['L1', 'L2']:
        got = {}
        for v in [NEW, BASE]:
            rs = [load(F / f'lg_{v}_{lg}_s{k}/record.csv').get(lg) for k in [1, 2, 3]]
            rs = [r for r in rs if r]
            if not rs:
                continue
            got[v] = rs
            cell = lambda key, fmt: (f"{st.mean(float(r[key]) for r in rs):{fmt}}（{min(float(r[key]) for r in rs):{fmt}}–"
                                     f"{max(float(r[key]) for r in rs):{fmt}}）")
            lines.append(f"| {lg} | {v} | {sum(r['verdict'] == 'PASS' for r in rs)}/{len(rs)} | {cell('seconds', '.1f')} | {cell('max', '.3f')} | "
                         f"{cell('disp', '.6e')} | {cell('hpwl_sa', '.6e')} |")
            js[f'{lg}_{v}'] = rs
        if NEW in got and BASE in got and len(got[NEW]) == len(got[BASE]):
            for key, name in [('disp', '總位移'), ('max', '最大'), ('hpwl_sa', 'HPWL')]:
                rr = [float(x[key]) / float(y[key]) for x, y in zip(got[NEW], got[BASE])]
                lines.append(f"| {lg} | {NEW} ÷ {BASE} {name} | | 各 seed：{' / '.join(f'{x:.4f}' for x in rr)} | 平均 {st.mean(rr):.4f} | | |")
                js[f'{lg}_ratio_{key}'] = rr
    lines.append('')


def main11(lines, js):
    v1 = load(F / f'lg_{V1}/record.csv')
    if not v1:
        return
    lines += ['## 主要 11 筆（testcases + holdout），預設參數，÷ 同輪 v1 的幾何平均', '',
              '| 版本 | 總位移 | 最大位移 | HPWL | 11 筆秒數 |', '|---|---:|---:|---:|---:|']
    for v in [NEW, BASE, REF, V1]:
        rec = load(F / f'lg_{v}/record.csv')
        tr, mr, hr = [], [], []
        for b, r0 in v1.items():
            r = rec.get(b)
            if not r:
                continue
            if float(r0['disp']) > 0: tr.append(float(r['disp']) / float(r0['disp']))
            if float(r0['max']) > 0: mr.append(float(r['max']) / float(r0['max']))
            hr.append(float(r['hpwl_sa']) / float(r0['hpwl_sa']))
        secs = sum(float(rec[b]['seconds']) for b in v1 if b in rec)
        lines.append(f'| {v} | {gm(tr):.4f} | {gm(mr):.4f} | {gm(hr):.4f} | {secs:.0f} |')
        js[f'main11_{v}'] = dict(total=gm(tr), max=gm(mr), hpwl=gm(hr), seconds=secs, n=len(hr))
    lines.append('')


def hpwl(lines, js):
    lines += ['## HPWL（3 seeds，÷ 同輪 v4）', '', f'| 協定 | 分組 | {NEW} ÷ v4 | {BASE} ÷ v4 | {NEW} ÷ {BASE} |', '|---|---|---|---|---|']
    for mode in ['fixed', 'equal', 'default']:
        p = F / f'hpwl_{mode}' / 'record.csv'
        if not p.exists():
            continue
        by = {}
        for r in csv.DictReader(open(p)):
            if r['verdict'] == 'PASS':
                by.setdefault((r['group'], r['bench']), {}).setdefault(r['version'], {})[int(r['seed'])] = float(r['hpwl'])
        for group in ['holdout', 'edge']:
            cells, rec = [], {}
            for x, y in [(NEW, V4), (BASE, V4), (NEW, BASE)]:
                per, seeds = [], {1: [], 2: [], 3: []}
                for (g, _), d in by.items():
                    if g == group and x in d and y in d and len(d[x]) == 3 and len(d[y]) == 3:
                        per.append(st.mean(d[x].values()) / st.mean(d[y].values()))
                        for s in seeds:
                            seeds[s].append(d[x][s] / d[y][s])
                rs = [gm(s) for s in seeds.values()]
                cells.append(f'{gm(per):.5f}（{min(rs):.5f}–{max(rs):.5f}，{len(per)} 筆）')
                rec[f'{x}/{y}'] = dict(gm=gm(per), lo=min(rs), hi=max(rs), n=len(per))
            lines.append(f'| {mode} | {group} | ' + ' | '.join(cells) + ' |')
            js[f'hpwl_{mode}_{group}'] = rec
    lines.append('')


lines = [f'# {NEW} 最終驗證', '', f'同一段時間配對重跑（final_validate.py）；對照 {BASE}、參考 ChainDP。', '']
js = {}
disp('位移：testcases + holdout + edge（27 筆）', {v: load(F / f'lg_{v}/record.csv', F / f'edge_{v}/record.csv') for v in [NEW, BASE, REF]}, lines, js, 'd27')
seed = {v: load(F / f'seed_{v}/record.csv') for v in [NEW, BASE, V6, V4, REF]}
if seed[REF]:
    disp('凍結後新 seed', seed, lines, js, 'seed')
main11(lines, js)
extra(lines, js)
large(lines, js)
hpwl(lines, js)
Path(a.out + '.md').write_text('\n'.join(lines) + '\n')
Path(a.out + '.json').write_text(json.dumps(js, indent=1, default=str) + '\n')
for k in ['d27_' + NEW, 'd27_' + BASE, 'seed_' + NEW]:
    if k in js:
        print(k, {x: js[k][x] for x in ['pass_', 'n', 'both', 'max_gm', 'total_gm']})
print('寫好', a.out + '.md')
