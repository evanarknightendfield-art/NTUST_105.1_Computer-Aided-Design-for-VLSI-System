#!/usr/bin/env python3
"""cap_headroom.py — how much room is there below a placement's maximum displacement?

Given a legal placement P and a target cap D (e.g. ChainDP's maximum), for
growing neighbourhoods K = (rows, sites) around the cells of P with
displacement > D: only the movable cells whose current footprint lies inside
the union of the boxes may move (every other cell stays where P has it), each
at most R rows / S sites from where it is; the exact minimum total
displacement of those cells with every displacement <= D is solved as a MIP
over every such position (Grader/tools/exact_bound.py's
model, HiGHS, zero gap). Infeasible = provably no solution that moves only
these cells; time limit = UNKNOWN.

Also (--lp-total) the LP lower bound of the minimum TOTAL displacement of the
whole design under cap D (all positions <= D, column generation is not used:
every position is enumerated), and (--lp-max) the LP lower bound of the
minimum maximum (bisection; complete position set at each step).

usage: cap_headroom.py BENCH LEGAL_PL --cap D OUT.csv [--boxes R:S ...] [--time-limit S] [--lp-total] [--lp-max]
needs scipy (anaconda base). Run pinned: taskset -c 16-19.
"""
import argparse, csv, os, sys, time
import numpy as np

LAB = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
sys.path.insert(0, os.path.join(LAB, 'Grader', 'tools'))
from exact_bound import Model  # noqa: E402


def restrict(M, keep):
    """Keep only movable cells `keep` free; the others become blockages at their P position."""
    keep = np.asarray(sorted(keep))
    others = np.setdiff1d(np.arange(len(M.mov)), keep)
    for i in others:
        if M.cur_r[i] >= 0:
            M.free[M.cur_r[i], M.cur_k[i]:M.cur_k[i] + M.W[i]] = False
    for name in ['W', 'GX', 'GY', 'cur_r', 'cur_k', 'cur']:
        setattr(M, name, getattr(M, name)[keep])
    M.mov = [M.mov[i] for i in keep]
    cs = np.concatenate([np.zeros((len(M.ys), 1), dtype=np.int64), np.cumsum(M.free, axis=1)], axis=1)
    M.valid = {}
    for w in np.unique(M.W):
        v = np.zeros((len(M.ys), M.ns), dtype=bool)
        if w <= M.ns:
            v[:, :M.ns - w + 1] = (cs[:, w:] - cs[:, :M.ns - w + 1]) == w
        M.valid[w] = v
    M.cols = {}


def solve_cap(M, D, time_limit, kr=None, kx=None):
    if kr is None:
        M.add_all(D)
    else:   # each cell moves at most kr rows / kx sites from its position in P
        for i in range(len(M.mov)):
            M.add_window(i, M.cur_r[i] - kr, M.cur_r[i] + kr, M.cur_k[i] - kx, M.cur_k[i] + kx, limit=D + 1e-7)
    if len({k[0] for k in M.cols}) < len(M.mov):
        return 'no_position', None, len(M.cols)
    res, keys, _ = M.solve_lp(integer=True, time_limit=time_limit)
    if res.x is not None and res.status == 0:
        return 'optimal', res.fun, len(keys)
    if res.x is not None:
        return 'feasible_timelimit', res.fun, len(keys)
    return ('infeasible' if res.status == 2 else f'unknown_status{res.status}'), None, len(keys)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bench')
    ap.add_argument('legal_pl')
    ap.add_argument('out')
    ap.add_argument('--cap', type=float, required=True)
    ap.add_argument('--boxes', nargs='*', default=['1:20', '2:40', '3:80', '4:160'])
    ap.add_argument('--time-limit', type=float, default=300)
    ap.add_argument('--lp-total', action='store_true')
    ap.add_argument('--lp-max', action='store_true')
    a = ap.parse_args()
    rows = []
    base = Model(a.bench, a.legal_pl)
    D = a.cap
    viol = np.where(base.cur > D + 1e-6)[0]
    P_total, P_max = float(base.cur.sum()), float(base.cur.max())
    print(f'{os.path.basename(a.bench)}: {len(base.mov)} movable, P total {P_total:.4f} max {P_max:.4f}, '
          f'cap {D}: {len(viol)} violators', flush=True)
    for box in a.boxes:
        kr, kx = map(int, box.split(':'))
        M = Model(a.bench, a.legal_pl)
        inside = np.zeros(len(M.mov), dtype=bool)
        for i in viol:
            r, k, w = M.cur_r[i], M.cur_k[i], M.W[i]
            inside |= (np.abs(M.cur_r - r) <= kr) & (M.cur_k + M.W > k - kx) & (M.cur_k < k + w + kx)
        keep = np.where(inside)[0]
        fixed_total = P_total - float(M.cur[keep].sum())
        restrict(M, keep)
        t = time.time()
        status, val, ncols = solve_cap(M, D, a.time_limit, kr, kx)
        sec = time.time() - t
        total = None if val is None else fixed_total + val
        rows.append(dict(kind='hotspot_mip', box=box, cells=len(keep), columns=ncols, status=status, seconds=round(sec, 2),
                         total=total, total_vs_P=None if total is None else total / P_total - 1, cap=D))
        print(f'  box {box}: {len(keep)} cells, {ncols} columns -> {status} total {total} ({sec:.1f}s)', flush=True)
    if a.lp_total:
        M = Model(a.bench, a.legal_pl)
        M.add_all(D)
        t = time.time()
        res, keys, _ = M.solve_lp()
        rows.append(dict(kind='lp_total_full', box='all', cells=len(M.mov), columns=len(keys),
                         status='optimal' if res.status == 0 else f'status{res.status}', seconds=round(time.time() - t, 2),
                         total=res.fun if res.status == 0 else None,
                         total_vs_P=res.fun / P_total - 1 if res.status == 0 else None, cap=D))
        print(f'  LP total under cap: {rows[-1]}', flush=True)
    if a.lp_max:
        lo, hi = 0.0, P_max
        M = Model(a.bench, a.legal_pl)
        for _ in range(14):
            mid = 0.5 * (lo + hi)
            M.cols = {}
            M.add_all(mid)
            ok = len({k[0] for k in M.cols}) == len(M.mov) and M.solve_lp()[0].status == 0
            lo, hi = (lo, mid) if ok else (mid, hi)
        rows.append(dict(kind='lp_max_bound', box='all', cells=len(M.mov), status='bisect', total=None, cap=lo))
        print(f'  LP lower bound on max: {lo:.4f}', flush=True)
    with open(a.out, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=['kind', 'box', 'cells', 'columns', 'status', 'seconds', 'total', 'total_vs_P', 'cap'])
        w.writeheader()
        w.writerows(rows)


if __name__ == '__main__':
    main()
