#!/usr/bin/env python3
"""Site-level bounds / exact optimum for Stage 1 (small and medium designs).

usage: exact_bound.py BENCH LEGAL_PL [--ilp] [--max] [--radius-rows R] [--time-limit S]
needs scipy >= 1.9 (anaconda base env: /home/evan/anaconda3/bin/python3)

Model: one variable x(i,p) per cell i and legal position p (row, first site;
all w_i sites free). Cost c(i,p) = |y_p - gy_i| + |x_p - gx_i| (the
displacement). Constraints: every cell gets one position; every site is
covered by at most one cell. This is exactly the legalization problem.

  total LP   LP relaxation, solved by column generation (start from the given
             legal placement's positions +- a small window; price every
             position whose cost is below the cell's dual u_i): a VALID LOWER
             BOUND on the minimum total displacement.
  --ilp      the same model as an integer program over the final columns
             (HiGHS MIP): an OPTIMAL legalization if the LP columns cover it
             (they do when LP == ILP), otherwise an upper bound.
  --max      lower bound on the maximum displacement: the largest D for which
             the LP with ALL positions of cost <= D is infeasible (bisection).
             Every step enumerates the complete position set; infeasibility of
             a restricted column set would prove nothing. --max-ilp then
             bisects with the integer model: the exact minimum maximum.
  --full     (with a cap) enumerate every position of cost <= cap instead of
             column generation; then the ILP is over the COMPLETE model and
             'proven' is printed only when the MIP dual bound meets the
             incumbent. Small designs only (<= ~1000 cells).

Fixed 2026-10-19 (found by CodexWindowDP/READING_NOTES.md): a pad left of the
chip blocked most of its rows (negative slice end); --max bisected on
restricted columns; the MIP used HiGHS' default nonzero relative gap.
"""
import argparse
import math
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_pl import read_aux, read_nodes, read_pl, read_scl  # noqa: E402


class Model:
    def __init__(self, bench, legal_pl):
        f = read_aux(bench)
        nodes = read_nodes(f['nodes'])
        gp = read_pl(f['pl'])
        out = read_pl(legal_pl)
        rows = read_scl(f['scl'])
        fixed = [n for n, (w, h, t) in nodes.items() if t or gp[n][3]]
        self.mov = [n for n, (w, h, t) in nodes.items() if not (t or gp[n][3])]
        self.sp = sp = rows[0][3]
        self.ys = np.array(sorted({r[0] for r in rows}))
        rh = rows[0][1]
        self.X0 = X0 = min(r[2] for r in rows)
        X1 = max(r[2] + r[4] * r[3] for r in rows)
        self.ns = ns = int(round((X1 - X0) / sp))
        self.free = np.zeros((len(self.ys), ns), dtype=bool)
        yidx = {y: i for i, y in enumerate(self.ys)}
        for (ry, h, org, s, n) in rows:
            a = int(round((org - X0) / sp))
            self.free[yidx[ry], a:a + n] = True
        for n in fixed:
            w, h, _ = nodes[n]
            x, fy = gp[n][0], gp[n][1]
            if w <= 0 or h <= 0:
                continue
            for ri, y in enumerate(self.ys):
                if fy < y + rh - 1e-9 and y < fy + h - 1e-9:
                    # clip BOTH ends: a pad left of the chip gives b < 0, and
                    # free[r, 0:b] with a negative b would block most of the row
                    a = max(0, min(ns, int(math.floor((x - X0) / sp + 1e-9))))
                    b = max(0, min(ns, int(math.ceil((x + w - X0) / sp - 1e-9))))
                    if a < b:
                        self.free[ri, a:b] = False
        self.W = np.array([int(round(nodes[n][0] / sp)) for n in self.mov])
        self.GX = np.array([gp[n][0] for n in self.mov])
        self.GY = np.array([gp[n][1] for n in self.mov])
        self.cur_r = np.array([yidx.get(out[n][1], -1) for n in self.mov])
        self.cur_k = np.array([int(round((out[n][0] - X0) / sp)) for n in self.mov])
        self.cur = np.abs(np.array([out[n][0] for n in self.mov]) - self.GX) + \
            np.abs(np.array([out[n][1] for n in self.mov]) - self.GY)
        # valid[w][r, k]: sites k..k+w-1 all free
        self.valid = {}
        cs = np.concatenate([np.zeros((len(self.ys), 1), dtype=np.int64), np.cumsum(self.free, axis=1)], axis=1)
        for w in np.unique(self.W):
            v = np.zeros((len(self.ys), ns), dtype=bool)
            if w <= ns:
                v[:, :ns - w + 1] = (cs[:, w:] - cs[:, :ns - w + 1]) == w
            self.valid[w] = v
        self.cols = {}   # (i, r, k) -> cost

    def cost(self, i, r, ks):
        return abs(self.ys[r] - self.GY[i]) + np.abs(self.X0 + ks * self.sp - self.GX[i])

    def add_window(self, i, r_lo, r_hi, k_lo, k_hi, limit=None):
        n = 0
        for r in range(max(0, r_lo), min(len(self.ys) - 1, r_hi) + 1):
            ks = np.arange(max(0, k_lo), min(self.ns - 1, k_hi) + 1)
            ks = ks[self.valid[self.W[i]][r, ks]]
            if len(ks) == 0:
                continue
            c = self.cost(i, r, ks)
            if limit is not None:
                keep = c <= limit
                ks, c = ks[keep], c[keep]
            for k, cc in zip(ks, c):
                if (i, r, k) not in self.cols:
                    self.cols[(i, r, int(k))] = float(cc)
                    n += 1
        return n

    def add_all(self, limit):
        """Every legal position with displacement <= limit (complete model)."""
        for i in range(len(self.mov)):
            self.add_window(i, 0, len(self.ys) - 1, 0, self.ns - 1, limit=limit + 1e-7)

    def solve_lp(self, keys=None, integer=False, time_limit=None, cost_cap=None):
        from scipy.optimize import linprog, milp, LinearConstraint, Bounds
        from scipy.sparse import coo_matrix
        keys = keys if keys is not None else list(self.cols.keys())
        if cost_cap is not None:
            keys = [k for k in keys if self.cols[k] <= cost_cap]
        n = len(self.mov)
        m = len(keys)
        ci = np.array([k[0] for k in keys])
        cr = np.array([k[1] for k in keys])
        ck = np.array([k[2] for k in keys])
        cost = np.array([self.cols[k] for k in keys])
        # site rows: (r, s) covered by column j for s in [k, k + w)
        w = self.W[ci]
        rep = np.repeat(np.arange(m), w)
        offs = np.concatenate([np.arange(x) for x in w]) if m else np.zeros(0, dtype=int)
        site = cr[rep] * self.ns + ck[rep] + offs
        sites, sinv = np.unique(site, return_inverse=True)
        A_eq = coo_matrix((np.ones(m), (ci, np.arange(m))), shape=(n, m)).tocsr()
        A_ub = coo_matrix((np.ones(len(rep)), (sinv, rep)), shape=(len(sites), m)).tocsr()
        if integer:
            from scipy.sparse import vstack
            cons = [LinearConstraint(A_eq, 1, 1), LinearConstraint(A_ub, -np.inf, 1)]
            opts = {'disp': False, 'mip_rel_gap': 0.0}   # default gap is not 0
            if time_limit:
                opts['time_limit'] = time_limit
            res = milp(cost, constraints=cons, integrality=np.ones(m), bounds=Bounds(0, 1), options=opts)
            return res, keys, None
        res = linprog(cost, A_ub=A_ub, b_ub=np.ones(len(sites)), A_eq=A_eq, b_eq=np.ones(n),
                      bounds=(0, None), method='highs')
        return res, keys, sites

    def column_generation(self, verbose=True, cost_cap=None):
        it = 0
        while True:
            it += 1
            t = time.time()
            res, keys, sites = self.solve_lp(cost_cap=cost_cap)
            if res.status == 2:
                return None   # infeasible
            if res.status != 0:
                raise SystemExit('LP failed: ' + res.message)
            u = res.eqlin.marginals
            y = np.zeros(len(self.ys) * self.ns)
            y[sites] = -res.ineqlin.marginals          # >= 0: price of a site
            Y = y.reshape(len(self.ys), self.ns)
            cy = np.concatenate([np.zeros((len(self.ys), 1)), np.cumsum(Y, axis=1)], axis=1)
            if verbose:
                print(f'    LP {it}: {len(keys)} columns, value {res.fun:.1f} ({time.time() - t:.1f}s)', file=sys.stderr)
            added = 0
            for i in range(len(self.mov)):
                lim = u[i] if cost_cap is None else min(u[i], cost_cap)
                if lim <= 0:
                    continue
                wi = self.W[i]
                for r in range(len(self.ys)):
                    dy = abs(self.ys[r] - self.GY[i])
                    if dy >= lim:
                        continue
                    rem = lim - dy
                    k_lo = max(0, int(math.floor((self.GX[i] - rem - self.X0) / self.sp)))
                    k_hi = min(self.ns - wi, int(math.ceil((self.GX[i] + rem - self.X0) / self.sp)))
                    if k_hi < k_lo:
                        continue
                    ks = np.arange(k_lo, k_hi + 1)
                    ks = ks[self.valid[wi][r, ks]]
                    if len(ks) == 0:
                        continue
                    c = self.cost(i, r, ks)
                    rc = c - u[i] + (cy[r, ks + wi] - cy[r, ks])
                    for k, cc in zip(ks[rc < -1e-7], c[rc < -1e-7]):
                        if (i, r, int(k)) not in self.cols:
                            self.cols[(i, r, int(k))] = float(cc)
                            added += 1
            if added == 0:
                return res.fun
            if verbose:
                print(f'    + {added} columns', file=sys.stderr)


def write_solution(M, bench, legal_pl, keys, x, path):
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
            f.write(f'{n}\t{px:g}\t{py:g}\t: {o}' + (' /FIXED' if fixed else '') + '\n')
    print(f'  wrote {path}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bench')
    ap.add_argument('legal_pl')
    ap.add_argument('--ilp', action='store_true')
    ap.add_argument('--max', action='store_true')
    ap.add_argument('--radius-rows', type=int, default=1)
    ap.add_argument('--time-limit', type=float, default=600)
    ap.add_argument('--full', action='store_true', help='complete position set below the cap (needs --cap)')
    ap.add_argument('--max-ilp', action='store_true', help='with --max: exact minimum maximum displacement (integer bisection)')
    ap.add_argument('--write-pl', help='write the ILP solution as a .pl (needs --ilp)')
    ap.add_argument('--cap', type=float, help='only positions with displacement <= CAP; '
                    'a negative value = our maximum (best total that keeps our max)')
    a = ap.parse_args()
    t0 = time.time()
    M = Model(a.bench, a.legal_pl)
    name = os.path.basename(a.bench)
    tot, mx = M.cur.sum(), M.cur.max()
    print(f'{name}: {len(M.mov)} cells; ours: total {tot:.1f}, max {mx:.1f}')
    # start: each cell's own position plus a small window around it (keeps the LP feasible)
    rr = a.radius_rows
    for i in range(len(M.mov)):
        w = M.W[i]
        M.add_window(i, M.cur_r[i] - rr, M.cur_r[i] + rr, M.cur_k[i] - 2 * w, M.cur_k[i] + 2 * w)
        M.cols[(i, int(M.cur_r[i]), int(M.cur_k[i]))] = float(M.cur[i])
    cap = None
    if a.cap is not None:
        cap = mx + 1e-6 if a.cap < 0 else a.cap
        print(f'  (only positions with displacement <= {cap:.1f})')
    if a.full:
        if cap is None:
            raise SystemExit('--full needs --cap')
        M.add_all(cap)
        print(f'  complete model: {len(M.cols)} positions', file=sys.stderr)
    lp = M.column_generation(cost_cap=cap)
    print(f'  total displacement: LP lower bound {lp:.1f}   ours/LB = {tot / lp:.4f}   ({time.time() - t0:.1f}s)')
    if a.ilp:
        res, keys, _ = M.solve_lp(integer=True, time_limit=a.time_limit, cost_cap=cap)
        if res.x is not None:
            dual = getattr(res, 'mip_dual_bound', None)
            proven = a.full and res.status == 0 and dual is not None and abs(res.fun - dual) < 1e-5
            what = 'OPTIMAL (complete model, zero gap)' if proven else \
                ('best found, dual bound %.1f' % dual if a.full and dual is not None else
                 'upper bound (restricted columns)')
            print(f'  total displacement: ILP {res.fun:.1f} [{what}]   ours/ILP = {tot / res.fun:.4f}')
            if a.write_pl:
                write_solution(M, a.bench, a.legal_pl, keys, res.x, a.write_pl)
    if a.max:
        lo, hi = 0.0, mx
        base = dict(M.cols)
        for _ in range(16):
            mid = (lo + hi) / 2
            M.cols = {}
            M.add_all(mid)
            if len({k[0] for k in M.cols}) < len(M.mov):
                lo = mid           # some cell has no position at all
                continue
            res, _, _ = M.solve_lp()
            if res.status == 2:
                lo = mid
            elif res.status == 0:
                hi = mid
            else:
                raise SystemExit('LP failed: ' + res.message)
        M.cols = base
        print(f'  max displacement: LP lower bound {lo:.1f}   ours/LB = {mx / max(lo, 1e-9):.4f}   ({time.time() - t0:.1f}s)')
        if a.max_ilp:
            # integer feasibility, bisection over the distinct displacement values in [lo, mx]
            M.cols = {}
            M.add_all(mx)
            vals = sorted({round(c, 6) for c in M.cols.values() if lo - 1e-6 <= c <= mx + 1e-6})
            full, ilo, ihi, unknown = dict(M.cols), 0, len(vals) - 1, False
            while ilo < ihi:
                mid = (ilo + ihi) // 2
                M.cols = {k: c for k, c in full.items() if c <= vals[mid] + 1e-6}
                if len({k[0] for k in M.cols}) < len(M.mov):
                    ilo = mid + 1
                    continue
                res, _, _ = M.solve_lp(integer=True, time_limit=a.time_limit)
                if res.x is not None:
                    ihi = mid
                elif res.status == 2 or 'infeasible' in res.message.lower():
                    ilo = mid + 1
                else:
                    unknown = True     # time limit without a verdict: stay conservative
                    ilo = mid + 1
            M.cols = base
            tag = ' (some steps timed out: upper value only)' if unknown else ' (exact)'
            print(f'  max displacement: ILP minimum {vals[ihi]:.1f}{tag}   ours/min = {mx / vals[ihi]:.4f}   ({time.time() - t0:.1f}s)')


if __name__ == '__main__':
    main()
