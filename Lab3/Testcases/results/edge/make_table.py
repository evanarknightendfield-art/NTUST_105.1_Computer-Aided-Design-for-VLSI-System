#!/usr/bin/env python3
"""Markdown tables of every version's result on the edge suite.

usage: python3 make_table.py > tables.md
Reads <version>/record.csv (from run_edge.sh) and reference.csv (the legal
placement gen_edge.py built each case from: a known feasible answer).
"""
import csv
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
V = [('v1', 'ClaudeOpus55'), ('GapSA', 'CodexGapSA'), ('v2', 'ClaudeOpus55_v2'), ('ParetoDP', 'CodexParetoDP'),
     ('v3', 'ClaudeOpus55_v3'), ('WindowDP', 'CodexWindowDP'), ('v4', 'ClaudeOpus55_v4'),
     ('ChainDP（前次）', '../CodexChainDP/edge_before'), ('ChainDP', 'CodexChainDP'), ('AugmentDP*', 'CodexAugmentDP'),
     ('v5', 'ClaudeOpus55_v5'), ('CapDP', 'CodexCapDP'), ('v6', 'ClaudeOpus55_v6'),
     ('CodexParallelChain', '../CodexParallelChain/final/edge_CodexParallelChain'), ('v7', 'ClaudeOpus55_v7')]
V = [(l, d) for l, d in V if os.path.exists(os.path.join(HERE, d, 'record.csv'))]
D = {d: {r['bench']: r for r in csv.DictReader(open(os.path.join(HERE, d, 'record.csv')))} for _, d in V}
REF = {r['bench']: r for r in csv.DictReader(open(os.path.join(HERE, 'reference.csv')))}
B = sorted(REF)


def num(x, key):
    if key == 'seconds':
        return f'{x:.1f}'
    return f'{x:,.0f}' if x >= 1000 else f'{x:.4g}'


for key, title, rkey in [('disp', '總位移', 'ref_disp'), ('max', '最大位移', 'ref_max'),
                         ('hpwl_sa', 'SA 之後的 HPWL', 'ref_hpwl'), ('seconds', '時間（秒）', None)]:
    print(f'### {title}\n')
    print('| 測資 | ' + ' | '.join(l for l, _ in V) + (' | 參考合法解 |' if rkey else ' |'))
    print('|---|' + '---:|' * (len(V) + (1 if rkey else 0)))
    gm = {l: [] for l, _ in V}
    for b in B:
        vals = []
        for l, d in V:
            r = D[d].get(b)
            vals.append(float(r[key]) if r and r['verdict'] == 'PASS' and r[key] else None)
        ok = [x for x in vals if x is not None]
        best = min(ok) if ok else None
        cells = []
        for x in vals:
            if x is None:
                cells.append('**FAIL**')
                continue
            s = num(x, key)
            if key != 'seconds' and abs(x - best) <= 1e-9 * max(1, abs(best)):
                s = f'**{s}**'
            cells.append(s)
        line = f'| {b} | ' + ' | '.join(cells)
        if rkey:
            line += f' | {num(float(REF[b][rkey]), key)}'
        print(line + ' |')
        if key != 'seconds' and all(x is not None and x > 0 for x in vals):
            for (l, _), x in zip(V, vals):
                gm[l].append(x / vals[0])
    if key != 'seconds':
        n = len(gm[V[0][0]])
        print('| **÷ v1 幾何平均** | ' + ' | '.join(f'{math.exp(sum(map(math.log, gm[l])) / n):.3f}' for l, _ in V)
              + f' | （{n} 筆都通過、值 > 0 的） |')
    print()
print('| PASS | ' + ' | '.join(f'{sum(r["verdict"] == "PASS" for r in D[d].values())}/{len(D[d])}' for _, d in V) + ' |')
print('\n\\* AugmentDP 為進行中的版本。ChainDP（前次）保留本次之前的歷史數據。')
