#!/usr/bin/env python3
"""pareto_exact.py — exact (total, max) trade-off of Stage 1 on a small design.

For each cap D: the minimum total displacement over ALL legal placements with
every cell's displacement <= D (complete model, HiGHS MIP, zero gap), using
Grader/tools/exact_bound.py's Model. Also writes the solution at the smallest
feasible cap and compares it cell by cell with a given legal placement.

usage: pareto_exact.py BENCH LEGAL_PL OUT_DIR --caps D1 D2 ... [--time-limit S]
needs scipy (anaconda base: /home/evan/anaconda3/bin/python3)
"""
import argparse, csv, os, sys, time
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', '..', '..', 'Grader', 'tools'))
from exact_bound import Model  # noqa: E402


def solve(M, cap, time_limit):
    M.cols = {}
    M.add_all(cap)
    if len({k[0] for k in M.cols}) < len(M.mov):
        return None, None, None
    res, keys, _ = M.solve_lp(integer=True, time_limit=time_limit)
    if res.x is None:
        return None, res, keys
    return res.fun, res, keys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bench')
    ap.add_argument('legal_pl')
    ap.add_argument('out')
    ap.add_argument('--caps', type=float, nargs='+', required=True)
    ap.add_argument('--time-limit', type=float, default=600)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    name = os.path.basename(a.bench)
    M = Model(a.bench, a.legal_pl)
    tot, mx = M.cur.sum(), M.cur.max()
    print(f'{name}: {len(M.mov)} cells; given placement: total {tot:.4f}, max {mx:.4f}')
    rows = []
    for cap in sorted(a.caps):
        t = time.time()
        val, res, keys = solve(M, cap, a.time_limit)
        status = 'infeasible' if val is None else ('optimal' if res.status == 0 else 'time_limit')
        dual = getattr(res, 'mip_dual_bound', None) if res is not None else None
        sol_max = None
        if val is not None:
            pos = [(k, c) for k, x, c in zip(keys, res.x, [M.cols[k] for k in keys]) if x > 0.5]
            sol_max = max(c for _, c in pos)
            path = os.path.join(a.out, f'{name}_cap{cap:g}.pl')
            write(M, a.legal_pl, keys, res.x, path)
        rows.append({'bench': name, 'cap': cap, 'status': status, 'min_total': val, 'dual_bound': dual,
                     'solution_max': sol_max, 'vs_given_total': None if val is None else val / tot - 1,
                     'seconds': round(time.time() - t, 2)})
        print(rows[-1], flush=True)
    with open(os.path.join(a.out, f'{name}_pareto.csv'), 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)


def write(M, legal_pl, keys, x, path):
    from check_pl import read_pl
    out = read_pl(legal_pl)
    pos = {}
    for k, v in zip(keys, x):
        if v > 0.5:
            i, r, kk = k
            pos[M.mov[i]] = (M.X0 + kk * M.sp, M.ys[r])
    with open(path, 'w') as f:
        f.write('UCLA pl 1.0\n\n')
        for n, (px, py, o, fixed) in out.items():
            px, py = pos.get(n, (px, py))
            f.write(f'{n}\t{px:.10g}\t{py:.10g}\t: {o}' + (' /FIXED' if fixed else '') + '\n')


if __name__ == '__main__':
    main()
