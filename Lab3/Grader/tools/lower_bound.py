#!/usr/bin/env python3
"""Lower bounds for Stage 1 (how far is a legalization from optimal?).

usage: lower_bound.py BENCH LEGAL_PL [--bin-sites K] [--skip-total] [--skip-max]
needs scipy (anaconda base env: /home/evan/anaconda3/bin/python3)

Relaxation (valid for every legal placement):
  * Each row is cut into bins of K sites. A cell belongs to the bin that holds
    its left edge. Cells starting in a bin occupy, without overlap, part of
    [bin start, bin end + (w_max - 1) sites), so the total width of cells in a
    bin is at most the free sites there (fixed objects removed): cap(b).
  * If cell i sits in bin b (row r, left edge in [x0, x1)), its displacement is
    at least c(i,b) = |y_r - gy_i| + dist(gx_i, [x0, x1 - site]).

Max displacement: D is a lower bound if no fractional assignment of cell widths
to bins exists using only pairs with c(i,b) <= D (max-flow check). Cells are
grouped by their global-placement bin; a group may use any bin that one of its
cells can reach (a superset, so still a relaxation). Bisection on D.

Total displacement: LP  min sum c(i,b)/w_i * f(i,b)  s.t.  sum_b f(i,b) = w_i,
sum_i f(i,b) <= cap(b),  f >= 0  (f = sites of cell i in bin b). Any legal
placement is a feasible point, so the LP optimum is a lower bound. Solved by
column generation with HiGHS: start from pairs near each cell's current spot,
add every pair with negative reduced cost (only pairs with c(i,b) < u_i*w_i can
have one), repeat until none is left -> exact LP optimum.
"""
import argparse
import math
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_pl import read_aux, read_nodes, read_pl, read_scl  # noqa: E402


def build(bench, legal_pl, K):
    f = read_aux(bench)
    nodes = read_nodes(f['nodes'])
    gp = read_pl(f['pl'])
    out = read_pl(legal_pl)
    rows = read_scl(f['scl'])
    fixed = [n for n, (w, h, t) in nodes.items() if t or gp[n][3]]
    mov = [n for n, (w, h, t) in nodes.items() if not (t or gp[n][3])]
    sp = rows[0][3]
    ys = sorted({r[0] for r in rows})
    rh = rows[0][1]
    X0 = min(r[2] for r in rows)
    X1 = max(r[2] + r[4] * r[3] for r in rows)
    nb = int(math.ceil((X1 - X0) / (K * sp)))
    W = np.array([nodes[n][0] / sp for n in mov])          # width in sites
    wmax = int(math.ceil(W.max()))
    # free site mask per row (subrows minus fixed objects), in global site units
    nsite = int(round((X1 - X0) / sp)) + wmax + 2
    cap = np.zeros((len(ys), nb))
    for ri, y in enumerate(ys):
        free = np.zeros(nsite, dtype=bool)
        for (ry, h, org, s, n) in rows:
            if ry == y:
                a = int(round((org - X0) / sp))
                free[a:a + n] = True
        for n in fixed:
            w, h, _ = nodes[n]
            x, fy = gp[n][0], gp[n][1]
            if w > 0 and h > 0 and fy < y + rh - 1e-9 and y < fy + h - 1e-9:
                a = max(0, int(math.floor((x - X0) / sp + 1e-9)))
                b = min(nsite, int(math.ceil((x + w - X0) / sp - 1e-9)))
                free[a:b] = False
        csum = np.concatenate([[0], np.cumsum(free)])
        for j in range(nb):
            a, b = j * K, min(nsite, j * K + K + wmax - 1)
            cap[ri, j] = csum[b] - csum[a]
    GX = np.array([gp[n][0] for n in mov])
    GY = np.array([gp[n][1] for n in mov])
    cur = np.array([abs(out[n][0] - gp[n][0]) + abs(out[n][1] - gp[n][1]) for n in mov])
    return dict(ys=np.array(ys), rh=rh, sp=sp, X0=X0, nb=nb, K=K, cap=cap, W=W, GX=GX, GY=GY, cur=cur)


def cost_row(P, i, ri):
    """costs from cell i to all bins of row ri"""
    x0 = P['X0'] + np.arange(P['nb']) * P['K'] * P['sp']
    x1 = x0 + (P['K'] - 1) * P['sp']
    gx = P['GX'][i]
    dx = np.maximum(0, np.maximum(x0 - gx, gx - x1))
    return abs(P['ys'][ri] - P['GY'][i]) + dx


def max_lb(P, hi):
    from scipy.sparse import csr_matrix
    from scipy.sparse.csgraph import maximum_flow
    R, nb = P['cap'].shape
    sp, K = P['sp'], P['K']
    # source groups: global-placement bin of each cell
    gr = np.clip(np.searchsorted(P['ys'], P['GY'] + P['rh'] / 2) - 1, 0, R - 1)
    gb = np.clip(((P['GX'] - P['X0']) / (K * sp)).astype(int), 0, nb - 1)
    gid = gr * nb + gb
    groups, inv = np.unique(gid, return_inverse=True)
    supply = np.bincount(inv, weights=P['W'])
    total = P['W'].sum()
    scale = 1000.0   # integer capacities for scipy's max flow

    def feasible(D):
        # per group and row: union (hull) of the bin ranges its cells reach
        G = len(groups)
        lo = np.full((G, R), nb, dtype=np.int64)
        hi_ = np.full((G, R), -1, dtype=np.int64)
        x0 = P['X0']
        for ri in range(R):
            dy = np.abs(P['ys'][ri] - P['GY'])
            ok = dy <= D
            if not ok.any():
                continue
            rem = D - dy[ok]
            # left edge may lie in [gx - rem, gx + rem] -> bins whose [x0, x1-site] intersects
            jl = np.floor((P['GX'][ok] - rem - x0) / (K * sp)).astype(np.int64)
            jh = np.floor((P['GX'][ok] + rem - x0) / (K * sp)).astype(np.int64)
            jl = np.clip(jl, 0, nb - 1)
            jh = np.clip(jh, 0, nb - 1)
            g = inv[ok]
            np.minimum.at(lo[:, ri], g, jl)
            np.maximum.at(hi_[:, ri], g, jh)
        src = []
        dst = []
        cap = []
        S, T = 0, 1 + G + R * nb
        for g in range(G):
            src.append(S); dst.append(1 + g); cap.append(int(supply[g] * scale))
        gg, rr = np.nonzero(hi_ >= lo)
        for g, ri in zip(gg, rr):
            for j in range(lo[g, ri], hi_[g, ri] + 1):
                src.append(1 + g); dst.append(1 + G + ri * nb + j); cap.append(int(total * scale) + 1)
        for b in range(R * nb):
            if P['cap'].flat[b] > 0:
                src.append(1 + G + b); dst.append(T); cap.append(int(P['cap'].flat[b] * scale))
        M = csr_matrix((np.array(cap, dtype=np.int64), (src, dst)), shape=(T + 1, T + 1))
        flow = maximum_flow(M, S, T, method='dinic').flow_value
        return flow >= int(total * scale) - len(groups)

    lo_, hi2 = 0.0, hi
    if feasible(lo_):
        return 0.0
    for _ in range(18):
        mid = (lo_ + hi2) / 2
        if feasible(mid):
            hi2 = mid
        else:
            lo_ = mid
    return lo_


def total_lb(P, margin_rows=2, margin_bins=2, verbose=True):
    from scipy.optimize import linprog
    from scipy.sparse import coo_matrix
    R, nb = P['cap'].shape
    n = len(P['W'])
    margin = margin_rows * P['rh'] + margin_bins * P['K'] * P['sp']
    arcs = {}   # (i, bin) -> cost
    for i in range(n):
        lim = P['cur'][i] + margin
        for ri in range(R):
            if abs(P['ys'][ri] - P['GY'][i]) > lim:
                continue
            c = cost_row(P, i, ri)
            for j in np.nonzero((c <= lim) & (P['cap'][ri] > 0))[0]:
                arcs[(i, ri * nb + j)] = c[j]
    it = 0
    while True:
        it += 1
        keys = list(arcs.keys())
        ai = np.array([k[0] for k in keys])
        ab = np.array([k[1] for k in keys])
        cost = np.array([arcs[k] for k in keys]) / P['W'][ai]
        m = len(keys)
        bins_used, binv = np.unique(ab, return_inverse=True)
        A_eq = coo_matrix((np.ones(m), (ai, np.arange(m))), shape=(n, m)).tocsr()
        A_ub = coo_matrix((np.ones(m), (binv, np.arange(m))), shape=(len(bins_used), m)).tocsr()
        t = time.time()
        res = linprog(cost, A_ub=A_ub, b_ub=P['cap'].flat[bins_used], A_eq=A_eq, b_eq=P['W'],
                      bounds=(0, None), method='highs')
        if res.status != 0:
            raise SystemExit('LP failed: ' + res.message)
        u = res.eqlin.marginals                 # per cell
        vb = np.zeros(R * nb)
        vb[bins_used] = res.ineqlin.marginals   # <= 0
        if verbose:
            print(f'  LP iter {it}: {m} pairs, value {res.fun:.1f}  ({time.time() - t:.1f}s)', file=sys.stderr)
        # pricing: pairs with c/w - u - v < 0 need c < u*w
        added = 0
        for i in range(n):
            lim = u[i] * P['W'][i]
            if lim <= 0:
                continue
            for ri in range(R):
                if abs(P['ys'][ri] - P['GY'][i]) >= lim:
                    continue
                c = cost_row(P, i, ri)
                rc = c / P['W'][i] - u[i] - vb[ri * nb: (ri + 1) * nb]
                for j in np.nonzero((rc < -1e-7) & (P['cap'][ri] > 0))[0]:
                    if (i, ri * nb + j) not in arcs:
                        arcs[(i, ri * nb + j)] = c[j]
                        added += 1
        if added == 0:
            return res.fun
        if verbose:
            print(f'  added {added} pairs', file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bench')
    ap.add_argument('legal_pl')
    ap.add_argument('--bin-sites', type=int, default=0, help='bin width in sites (default: ~2 average cells)')
    ap.add_argument('--skip-total', action='store_true')
    ap.add_argument('--skip-max', action='store_true')
    a = ap.parse_args()
    t = time.time()
    f = read_aux(a.bench)
    nodes = read_nodes(f['nodes'])
    rows = read_scl(f['scl'])
    avg = np.mean([w for (w, h, term) in nodes.values() if not term]) / rows[0][3]
    K = a.bin_sites or max(2, int(round(2 * avg)))
    P = build(a.bench, a.legal_pl, K)
    total, mx = P['cur'].sum(), P['cur'].max()
    print(f'{os.path.basename(a.bench)}: bins of {K} sites; ours: total {total:.1f}, max {mx:.1f}')
    if not a.skip_max:
        lb = max_lb(P, mx)
        print(f'  max displacement   LB {lb:.1f}   ours/LB = {mx / max(lb, 1e-9):.3f}')
    if not a.skip_total:
        lb = total_lb(P)
        print(f'  total displacement LB {lb:.1f}   ours/LB = {total / max(lb, 1e-9):.3f}')
    print(f'  ({time.time() - t:.1f}s)')


if __name__ == '__main__':
    main()
