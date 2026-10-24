#!/usr/bin/env python3
"""diag_tables.py — tables for diagnosis.md from the phase-1 records.

Reads (all under Testcases/results/):
  ClaudeOpus55_v5/phase1/ts_<version>/record.csv   time-stamped -v runs (run_ts.py)
  ClaudeOpus55_v5/phase1/lg_chain/record.csv       local_grade.py, ChainDP, 27 cases
  CodexChainDP/equal_time_default/record.csv       ChainDP's equal-total-time experiment (3 seeds)
writes ClaudeOpus55_v5/phase1/tables.md
"""
import csv, math, os, statistics as st
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.abspath(os.path.join(HERE, '..', '..'))
P1 = os.path.join(RES, 'ClaudeOpus55_v5', 'phase1')


def rd(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def num(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def f1(x, nd=1):
    return '' if x is None else f'{x:,.{nd}f}'


def main():
    out = []
    v4 = {r['bench']: r for r in rd(os.path.join(P1, 'ts_ClaudeOpus55_v4', 'record.csv'))}
    ch = {r['bench']: r for r in rd(os.path.join(P1, 'ts_CodexChainDP', 'record.csv'))}
    order = list(v4)
    # ---- 1. phase times
    out.append('## Phase times (seconds, time-stamped -v run, default options)\n')
    out.append('| bench | v4 Stage 1 | ChainDP Stage 1 | of which chain | of which global | v4 SA | ChainDP SA | v4 polish | ChainDP polish | v4 total | ChainDP total |')
    out.append('|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|')
    sums = defaultdict(float)
    for b in order:
        a, c = v4[b], ch.get(b, {})
        vals = [num(a.get('t_stage1')), num(c.get('t_stage1')), num(c.get('t_chain')), num(c.get('t_global')),
                num(a.get('sa_seconds')), num(c.get('sa_seconds')), num(a.get('t_polish')), num(c.get('t_polish')),
                num(a.get('seconds')), num(c.get('seconds'))]
        for i, x in enumerate(vals):
            sums[i] += x or 0
        out.append(f'| {b} | ' + ' | '.join(f1(x) for x in vals) + ' |')
    out.append('| **sum** | ' + ' | '.join(f'**{sums[i]:,.1f}**' for i in range(10)) + ' |\n')
    # ---- 2. SA attempts / seconds
    out.append('## SA: attempts and seconds (default options, seed 1)\n')
    out.append('| bench | threads | v4 attempts | ChainDP attempts | ratio | v4 SA s | ChainDP SA s | v4 M/s | ChainDP M/s |')
    out.append('|---|---:|---:|---:|---:|---:|---:|---:|---:|')
    for b in order:
        a, c = v4[b], ch.get(b, {})
        aa, ca = num(a.get('sa_attempts')), num(c.get('sa_attempts'))
        asec, csec = num(a.get('sa_seconds')), num(c.get('sa_seconds'))
        ratio = ca / aa if aa and ca else None
        out.append(f"| {b} | {a.get('sa_threads', '')} | {f1(aa, 0)} | {f1(ca, 0)} | {f1(ratio, 3)} | {f1(asec)} | {f1(csec)} | "
                   f"{f1(aa / asec / 1e6 if aa and asec else None, 2)} | {f1(ca / csec / 1e6 if ca and csec else None, 2)} |")
    out.append('')
    # ---- 3. equal-time experiment of ChainDP: where its +1.5 % comes from
    eq = rd(os.path.join(RES, 'CodexChainDP', 'equal_time_default', 'record.csv'))
    by = defaultdict(lambda: defaultdict(list))
    meta = {}
    for r in eq:
        by[r['bench']][r['version']].append(r)
    out.append('## ChainDP equal-total-time experiment (its own records, 3 seeds): SA time vs HPWL\n')
    out.append('budget = slower Stage 1 + 12 s; SA limit = budget - own Stage 1 - 2 s polish.\n')
    out.append('| bench | budget | v4 Stage 1 | ChainDP Stage 1 | v4 SA limit | ChainDP SA limit | v4 attempts | ChainDP attempts | HPWL ChainDP / v4 (mean) |')
    out.append('|---|---:|---:|---:|---:|---:|---:|---:|---:|')
    rows_ratio = []
    for b in sorted(by):
        A, C = by[b].get('ClaudeOpus55_v4', []), by[b].get('CodexChainDP', [])
        if not A or not C:
            continue
        ha = st.mean(num(r['hpwl']) for r in A)
        hc = st.mean(num(r['hpwl']) for r in C)
        att_a = st.mean(num(r['attempts']) or 0 for r in A)
        att_c = st.mean(num(r['attempts']) or 0 for r in C)
        ratio = hc / ha if ha else None
        rows_ratio.append((b, ratio, att_c / att_a if att_a else None))
        out.append(f"| {b} | {f1(num(A[0]['budget']))} | {f1(num(A[0]['stage1_seconds']))} | {f1(num(C[0]['stage1_seconds']))} | "
                   f"{f1(num(A[0]['sa_limit']))} | {f1(num(C[0]['sa_limit']))} | {f1(att_a, 0)} | {f1(att_c, 0)} | {f1(ratio, 4)} |")
    # split: SA time-limited (ChainDP got fewer attempts) vs not
    lim = [r for b, r, x in rows_ratio if x is not None and x < 0.95 and r]
    full = [r for b, r, x in rows_ratio if x is not None and x >= 0.95 and r]
    g = lambda xs: math.exp(st.mean(math.log(x) for x in xs)) if xs else None
    out.append(f'\nGeometric mean of the HPWL ratio: cases where ChainDP got < 95 % of v4\'s SA attempts: '
               f'{f1(g(lim), 4)} ({len(lim)} cases); cases with about the same attempts: {f1(g(full), 4)} ({len(full)} cases).\n')
    with open(os.path.join(P1, 'tables.md'), 'w') as f:
        f.write('\n'.join(out) + '\n')
    print('\n'.join(out))


if __name__ == '__main__':
    main()
