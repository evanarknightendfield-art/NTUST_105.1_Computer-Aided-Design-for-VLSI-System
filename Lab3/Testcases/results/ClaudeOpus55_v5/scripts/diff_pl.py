#!/usr/bin/env python3
"""diff_pl.py — compare two legal placements of the same design cell by cell.

Prints which cells changed row / x, displacement before and after, and per row
the order of cells, so the kind of move that turns A into B can be read off:
  - same row, same order, shifted  -> a "slide" (row clumping / L1 re-placement)
  - changed row                    -> a row reassignment (how many rows away)
  - same row, order changed        -> a swap / reorder
usage: diff_pl.py BENCH A.pl B.pl [--top N]
"""
import argparse, os, sys
from collections import defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'Grader', 'tools'))
from check_pl import read_aux, read_nodes, read_pl, read_scl  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bench')
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--worst', type=float, default=None, help='list cells with disp >= this in A or B')
    x = ap.parse_args()
    f = read_aux(x.bench)
    nodes = read_nodes(f['nodes'])
    gp = read_pl(f['pl'])
    rows = read_scl(f['scl'])
    rh = rows[0][1]
    ys = sorted({r[0] for r in rows})
    yi = {y: i for i, y in enumerate(ys)}
    A, B = read_pl(x.a), read_pl(x.b)
    mov = [n for n, (w, h, t) in nodes.items() if not (t or gp[n][3])]
    sp = rows[0][3]

    def disp(P, n):
        return abs(P[n][0] - gp[n][0]) + abs(P[n][1] - gp[n][1])

    ta = sum(disp(A, n) for n in mov)
    tb = sum(disp(B, n) for n in mov)
    ma = max(disp(A, n) for n in mov)
    mb = max(disp(B, n) for n in mov)
    print(f'A: total {ta:.4f} max {ma:.4f}   B: total {tb:.4f} max {mb:.4f}')
    same = rowchg = shift = 0
    drows = defaultdict(int)
    changed = []
    for n in mov:
        ra, rb = yi[A[n][1]], yi[B[n][1]]
        if ra != rb:
            rowchg += 1
            drows[rb - ra] += 1
            changed.append(n)
        elif abs(A[n][0] - B[n][0]) > 1e-6:
            shift += 1
            changed.append(n)
        else:
            same += 1
    print(f'cells: {len(mov)}; same position {same}, same row shifted {shift}, other row {rowchg} '
          f'(row deltas: {dict(sorted(drows.items()))})')
    # per row: order of cells in A vs B (only rows that differ)
    by_a, by_b = defaultdict(list), defaultdict(list)
    for n in mov:
        by_a[yi[A[n][1]]].append((A[n][0], n))
        by_b[yi[B[n][1]]].append((B[n][0], n))
    diff_rows = sorted(r for r in set(by_a) | set(by_b) if sorted(by_a[r]) != sorted(by_b[r]))
    print(f'rows that differ: {len(diff_rows)} of {len(set(by_a) | set(by_b))} used rows')
    # same membership, different order
    reord = 0
    for r in diff_rows:
        sa = [n for _, n in sorted(by_a[r])]
        sb = [n for _, n in sorted(by_b[r])]
        if set(sa) == set(sb) and sa != sb:
            reord += 1
    print(f'rows with the same cells but a different order: {reord}')
    lim = x.worst if x.worst is not None else 0.97 * max(ma, mb)
    print(f'\ncells with displacement >= {lim:.3f} in A or B (dA, dB, rowA->rowB, xA->xB, width sites):')
    lst = sorted(mov, key=lambda n: -max(disp(A, n), disp(B, n)))
    for n in lst:
        if max(disp(A, n), disp(B, n)) < lim:
            break
        print(f'  {n:12s} dA {disp(A, n):10.3f} dB {disp(B, n):10.3f}  row {yi[A[n][1]]:4d}->{yi[B[n][1]]:4d}  '
              f'x {A[n][0]:10.2f}->{B[n][0]:10.2f}  w {nodes[n][0] / sp:.0f}  gx {gp[n][0]:.2f} gy {gp[n][1]:.2f}')
    if x.top:
        print(f'\nrow contents (A | B) of the {min(x.top, len(diff_rows))} differing rows nearest the worst cell:')
        for r in diff_rows[:x.top]:
            sa = ' '.join(f'{n}({disp(A, n):.0f})' for _, n in sorted(by_a[r]))
            sb = ' '.join(f'{n}({disp(B, n):.0f})' for _, n in sorted(by_b[r]))
            print(f'  row {r:4d} y={ys[r]:.1f}\n     A: {sa}\n     B: {sb}')


if __name__ == '__main__':
    main()
