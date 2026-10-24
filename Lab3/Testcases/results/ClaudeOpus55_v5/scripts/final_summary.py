#!/usr/bin/env python3
"""final_summary.py — tables for the v5 README / COMPARISON.md from the final runs.

Stage 1 (max / total displacement) per bench: v4 (phase1/ts_ClaudeOpus55_v4, default run),
ChainDP (phase1/lg_chain, local_grade), v5 (final/lg_v5 + Testcases/results/edge/ClaudeOpus55_v5, local_grade).
Acceptance per bench: v5 max <= ChainDP max, and v5 total <= ChainDP total x 1.005.
writes final/summary.md, final/summary.json
"""
import csv, json, math, statistics as st
from pathlib import Path

R = Path(__file__).resolve().parents[1]
F = R / 'final'
RES = R.parent


def rd(p):
    p = Path(p)
    return {r['bench']: r for r in csv.DictReader(open(p))} if p.exists() else {}


def gm(xs):
    xs = [x for x in xs if x and x > 0]
    return math.exp(st.mean(math.log(x) for x in xs)) if xs else float('nan')


def main():
    out, js = [], {}
    v4 = rd(R / 'phase1' / 'ts_ClaudeOpus55_v4' / 'record.csv')
    ch = rd(R / 'phase1' / 'lg_chain' / 'record.csv')
    v5 = rd(F / 'lg_v5' / 'record.csv')
    v5.update(rd(RES / 'edge' / 'ClaudeOpus55_v5' / 'record.csv'))
    groups = [('testcases', ['ibm01', 'ibm05', 'toy']), ('holdout', [f'h0{i}' for i in range(1, 9)]),
              ('edge', sorted(b for b in v5 if b.startswith('e')))]
    out.append('## Stage 1：逐筆（預設參數，seed 1）\n')
    out.append('| 分組 | bench | v4 最大 | ChainDP 最大 | **v5 最大** | v5 / ChainDP 最大 | v4 總 | ChainDP 總 | **v5 總** | v5 / ChainDP 總 | 兩條件 | 秒數 v4 / ChainDP / v5 |')
    out.append('|---|---|---:|---:|---:|---:|---:|---:|---:|---:|:---:|---:|')
    rows = []
    for g, names in groups:
        for b in names:
            if b not in v5:
                continue
            a, c, n = v4.get(b, {}), ch.get(b, {}), v5[b]
            f = lambda r, k: float(r[k]) if r.get(k) not in (None, '') else None
            m4, mc, m5 = f(a, 'max'), f(c, 'max'), f(n, 'max')
            t4, tc, t5 = f(a, 'disp'), f(c, 'disp'), f(n, 'disp')
            okm = m5 is not None and mc is not None and m5 <= mc + 1e-9
            okt = t5 is not None and tc is not None and t5 <= tc * 1.005 + 1e-9
            rm = m5 / mc if mc else None
            rt = t5 / tc if tc else None
            rows.append(dict(group=g, bench=b, v4_max=m4, chain_max=mc, v5_max=m5, v4_total=t4, chain_total=tc, v5_total=t5,
                             max_ok=okm, total_ok=okt, verdict=n.get('verdict'), v5_seconds=f(n, 'seconds'),
                             v4_seconds=f(a, 'seconds'), chain_seconds=f(c, 'seconds'), v5_hpwl=f(n, 'hpwl_sa')))
            fm = lambda x: '' if x is None else (f'{x:,.4f}' if x < 1e4 else f'{x:,.1f}')
            out.append(f"| {g} | {b} | {fm(m4)} | {fm(mc)} | **{fm(m5)}** | {'' if rm is None else f'{rm:.4f}'} | {fm(t4)} | {fm(tc)} | **{fm(t5)}** | "
                       f"{'' if rt is None else f'{rt:.4f}'} | {'✓' if okm and okt else ('最大位移較差' if not okm else '總位移 > +0.5%')} | "
                       f"{f(a, 'seconds') or 0:.1f} / {f(c, 'seconds') or 0:.1f} / {f(n, 'seconds') or 0:.1f} |")
    out.append('')
    for g in ['testcases', 'holdout', 'edge', 'all']:
        rs = [r for r in rows if g == 'all' or r['group'] == g]
        pos = [r for r in rs if r['chain_max'] and r['v5_max']]
        post = [r for r in rs if r['chain_total'] and r['v5_total']]
        pos4 = [r for r in rs if r['v4_max'] and r['v5_max']]
        s = dict(cases=len(rs), passed=sum(r['verdict'] == 'PASS' for r in rs),
                 max_not_worse=sum(r['max_ok'] for r in rs), total_within=sum(r['total_ok'] for r in rs),
                 both=sum(r['max_ok'] and r['total_ok'] for r in rs),
                 max_vs_chain=gm([r['v5_max'] / r['chain_max'] for r in pos]),
                 total_vs_chain=gm([r['v5_total'] / r['chain_total'] for r in post]),
                 max_vs_v4=gm([r['v5_max'] / r['v4_max'] for r in pos4]),
                 total_vs_v4=gm([r['v5_total'] / r['v4_total'] for r in pos4 if r['v4_total']]),
                 seconds_v4=sum(r['v4_seconds'] or 0 for r in rs), seconds_chain=sum(r['chain_seconds'] or 0 for r in rs),
                 seconds_v5=sum(r['v5_seconds'] or 0 for r in rs))
        js[g] = s
        out.append(f"- **{g}**：{s['passed']}/{s['cases']} PASS；最大位移不比 ChainDP 差 {s['max_not_worse']}/{s['cases']}、"
                   f"總位移 ≤ ChainDP × 1.005 {s['total_within']}/{s['cases']}、兩者都滿足 {s['both']}/{s['cases']}；"
                   f"幾何平均 最大位移 ÷ ChainDP {s['max_vs_chain']:.4f}、÷ v4 {s['max_vs_v4']:.4f}，"
                   f"總位移 ÷ ChainDP {s['total_vs_chain']:.4f}、÷ v4 {s['total_vs_v4']:.4f}；"
                   f"合計秒數 v4 {s['seconds_v4']:.1f} / ChainDP {s['seconds_chain']:.1f} / v5 {s['seconds_v5']:.1f}")
    out.append('')
    # large
    out.append('## 大設計\n')
    out.append('| | 版本 | 結果 | 秒 | 總位移 | 最大位移 | SA 後 HPWL |')
    out.append('|---|---|---|---:|---:|---:|---:|')
    for L in ['L1', 'L2']:
        for name, rec in [('v4（phase 1，run_ts）', rd(R / 'phase1' / 'ts_ClaudeOpus55_v4_large' / 'record.csv')),
                          ('ChainDP（phase 1，run_ts）', rd(R / 'phase1' / 'ts_CodexChainDP_large' / 'record.csv')),
                          ('ChainDP（local_grade）', rd(R / 'phase1' / 'lg_chain_L1' / 'record.csv')),
                          ('**v5（local_grade）**', rd(F / f'lg_v5_{L}' / 'record.csv'))]:
            r = rec.get(L)
            if not r:
                continue
            out.append(f"| {L} | {name} | {r.get('verdict', r.get('status'))} | {float(r['seconds']):.1f} | {float(r['disp']):,.1f} | "
                       f"{float(r['max']):,.3f} | {float(r['hpwl_sa']):,.1f} |")
            js[f'{L}_{name}'] = r
    out.append('')
    # new seed
    out.append('## 新 seed（edge_seed3，演算法凍結後才產生）\n')
    s3 = {v: rd(F / f'seed3_{v}' / 'record.csv') for v in ['ClaudeOpus55_v4', 'CodexChainDP', 'ClaudeOpus55_v5']}
    if all(s3.values()):
        out.append('| bench | v4 最大 / 總 | ChainDP 最大 / 總 | **v5 最大 / 總** | v5 vs ChainDP | HPWL v4 / ChainDP / v5 |')
        out.append('|---|---:|---:|---:|:---:|---:|')
        ok = both = 0
        mr, tr, mr4, tr4 = [], [], [], []
        for b in sorted(s3['ClaudeOpus55_v5']):
            a, c, n = (s3[v].get(b, {}) for v in s3)
            f = lambda r, k: float(r[k]) if r.get(k) not in (None, '') else float('nan')
            okm = f(n, 'max') <= f(c, 'max') + 1e-9
            okt = f(n, 'disp') <= f(c, 'disp') * 1.005 + 1e-9
            both += okm and okt
            ok += n.get('verdict') == 'PASS'
            if f(c, 'max') > 0 and f(n, 'max') > 0:
                mr.append(f(n, 'max') / f(c, 'max')); tr.append(f(n, 'disp') / f(c, 'disp'))
                mr4.append(f(n, 'max') / f(a, 'max')); tr4.append(f(n, 'disp') / f(a, 'disp'))
            out.append(f"| {b} | {f(a, 'max'):,.4g} / {f(a, 'disp'):,.1f} | {f(c, 'max'):,.4g} / {f(c, 'disp'):,.1f} | "
                       f"**{f(n, 'max'):,.4g} / {f(n, 'disp'):,.1f}** | {'✓' if okm and okt else ('最大位移較差' if not okm else '總位移 > +0.5%')} | "
                       f"{f(a, 'hpwl_sa'):,.0f} / {f(c, 'hpwl_sa'):,.0f} / {f(n, 'hpwl_sa'):,.0f} |")
        out.append(f"\n- v5 {ok}/{len(s3['ClaudeOpus55_v5'])} PASS；兩條件都滿足 {both}/{len(s3['ClaudeOpus55_v5'])}；"
                   f"幾何平均 最大位移 ÷ ChainDP {gm(mr):.4f}、÷ v4 {gm(mr4):.4f}，總位移 ÷ ChainDP {gm(tr):.4f}、÷ v4 {gm(tr4):.4f}"
                   f"（{len(mr)} 筆值 > 0）")
        out.append('- PASS：' + '、'.join(f"{v} {sum(r['verdict'] == 'PASS' for r in s3[v].values())}/{len(s3[v])}" for v in s3))
        js['seed3'] = dict(both=both, cases=len(s3['ClaudeOpus55_v5']), max_vs_chain=gm(mr), total_vs_chain=gm(tr),
                           max_vs_v4=gm(mr4), total_vs_v4=gm(tr4))
    out.append('')
    for name, title in [('hpwl_fixed', 'HPWL：同樣 SA 次數（500 萬次、1 執行緒、共同起點 = v4 的 Stage 1，無 polish）'),
                        ('hpwl_equal', 'HPWL：同樣總時間（較慢的 Stage 1 + 12 秒，SA 上限 = 預算 − 自己的 Stage 1 − 2 秒）'),
                        ('hpwl_default', 'HPWL：預設參數（SA 60 秒 + polish 30 秒，各自的 Stage 1）')]:
        p = F / name / 'summary.md'
        if p.exists():
            out.append(f'## {title}\n')
            out.append(p.read_text())
    (F / 'summary.md').write_text('\n'.join(out) + '\n')
    (F / 'summary.json').write_text(json.dumps(dict(groups=js, rows=rows), indent=1, ensure_ascii=False, default=str) + '\n')
    print('\n'.join(out))


if __name__ == '__main__':
    main()
