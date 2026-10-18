#!/usr/bin/env python3
"""Independent checker for Project 3 placements (does not share code with dp).

usage: check_pl.py BENCH PLACED_PL [--quiet]
  BENCH      benchmark prefix, e.g. Testcases/testcases/toy/toy  (reads BENCH.aux)
  PLACED_PL  placement to check, e.g. out/toy_legal.pl

Checks, against the geometry in .scl / .nodes and the input .pl:
  * every movable cell sits on a row (y == row coordinate) and on a site
    (x == subrow origin + k * sitespacing), entirely inside one subrow
  * no two cells overlap, and no cell overlaps a fixed object
  * fixed objects did not move; orientations did not change
Then prints total / maximum displacement (Manhattan, vs. the input .pl) and
total HPWL (pins at cell centers, unweighted).
"""
import os
import sys
from collections import defaultdict


def tokens(line):
    line = line.split('#', 1)[0].replace(':', ' : ')
    return line.split()


def read_aux(prefix):
    aux = prefix if prefix.endswith('.aux') else prefix + '.aux'
    d = os.path.dirname(aux)
    files = {}
    for t in open(aux).read().split():
        for ext in ('nodes', 'nets', 'pl', 'scl', 'wts'):
            if t.lower().endswith('.' + ext):
                files[ext] = os.path.join(d, t)
    return files


def read_nodes(path):
    nodes = {}
    for line in open(path):
        t = tokens(line)
        if len(t) < 3 or t[0] == 'UCLA' or t[0].lower().startswith('num'):
            continue
        nodes[t[0]] = (float(t[1]), float(t[2]), len(t) > 3 and t[3].lower().startswith('terminal'))
    return nodes


def read_pl(path):
    pl = {}
    for line in open(path):
        t = tokens(line)
        if len(t) < 3 or t[0] == 'UCLA':
            continue
        orient = next((x for x in t[3:] if x != ':' and not x.startswith('/')), 'N')
        fixed = any(x.lower().startswith('/fixed') for x in t[3:])
        pl[t[0]] = (float(t[1]), float(t[2]), orient, fixed)
    return pl


def read_nets(path):
    nets, cur, left = [], [], 0
    for line in open(path):
        t = tokens(line)
        if not t or t[0] == 'UCLA':
            continue
        if t[0].lower() == 'netdegree':
            if cur:
                nets.append(cur)
            cur, left = [], int(t[2])
            continue
        if t[0].lower().startswith('num') or left == 0:
            continue
        cur.append(t[0])
        left -= 1
    if cur:
        nets.append(cur)
    return nets


def read_scl(path):
    rows = []   # (y, height, origin, spacing, numsites)
    y = h = sw = sp = 0.0
    for line in open(path):
        t = tokens(line)
        if not t:
            continue
        k = t[0].lower()
        if k == 'corerow':
            y = h = sp = 0.0
            sw = 1.0
        elif k == 'coordinate':
            y = float(t[2])
        elif k == 'height':
            h = float(t[2])
        elif k == 'sitewidth':
            sw = float(t[2])
        elif k == 'sitespacing':
            sp = float(t[2])
        elif k == 'subroworigin':
            n = 0
            for i, x in enumerate(t):
                if x.lower() == 'numsites':
                    n = int(float(t[i + 2]))
            rows.append((y, h, float(t[2]), sp if sp > 0 else sw, n))
    return rows


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    quiet = '--quiet' in sys.argv
    if len(args) != 2:
        print(__doc__)
        sys.exit(1)
    files = read_aux(args[0])
    nodes = read_nodes(files['nodes'])
    gp = read_pl(files['pl'])
    out = read_pl(args[1])
    nets = read_nets(files['nets'])
    rows = read_scl(files['scl'])

    errors = []

    def err(msg):
        errors.append(msg)

    fixed = {n for n, (w, h, term) in nodes.items() if term or (n in gp and gp[n][3])}
    for n in nodes:
        if n not in out:
            err(f'{n} missing from output')
    for n in out:
        if n not in nodes:
            err(f'{n} is not in .nodes')
    if errors:
        report(errors, None, quiet)
        return

    eps = 1e-6
    for n in fixed:
        if abs(out[n][0] - gp[n][0]) > eps or abs(out[n][1] - gp[n][1]) > eps:
            err(f'fixed {n} moved')
    for n in nodes:
        if out[n][2] != gp[n][2]:
            err(f'{n} orientation changed {gp[n][2]} -> {out[n][2]}')

    by_y = defaultdict(list)
    for r in rows:
        by_y[r[0]].append(r)
    row_ys = sorted(by_y)

    # occupied intervals per row y: (x_lo, x_hi, name)
    occ = defaultdict(list)
    for n in fixed:
        w, h, _ = nodes[n]
        x, y = out[n][0], out[n][1]
        if w <= 0 or h <= 0:
            continue
        for ry in row_ys:
            rh = by_y[ry][0][1]
            if y < ry + rh - eps and ry < y + h - eps:
                occ[ry].append((x, x + w, n))

    for n, (w, h, _) in nodes.items():
        if n in fixed:
            continue
        x, y = out[n][0], out[n][1]
        if y not in by_y:
            # tolerate tiny float noise in y
            near = min(row_ys, key=lambda ry: abs(ry - y))
            if abs(near - y) > eps:
                err(f'{n} y={y} is not on a row')
                continue
            y = near
        ok = False
        for (ry, rh, org, sp, ns) in by_y[y]:
            k = (x - org) / sp
            if abs(k - round(k)) > 1e-6:
                continue
            if x >= org - eps and x + w <= org + ns * sp + eps:
                ok = True
                break
        if not ok:
            err(f'{n} at ({x}, {y}) is off-site or outside every subrow')
        if abs(h - by_y[y][0][1]) > eps:
            err(f'{n} height {h} != row height {by_y[y][0][1]}')
        occ[y].append((x, x + w, n))

    # Fixed objects may overlap each other in the input (e.g. stacked pads);
    # only overlaps that involve a movable cell are errors.
    for ry, lst in occ.items():
        lst.sort()
        reach, reach_name = -float('inf'), None   # rightmost end seen so far
        for (b0, b1, bn) in lst:
            if b0 < reach - eps and (bn not in fixed or reach_name not in fixed):
                err(f'{reach_name} overlaps {bn} in row y={ry}')
            if b1 > reach:
                reach, reach_name = b1, bn

    total = mx = 0.0
    worst = None
    for n in nodes:
        if n in fixed:
            continue
        d = abs(out[n][0] - gp[n][0]) + abs(out[n][1] - gp[n][1])
        total += d
        if d > mx:
            mx, worst = d, n

    hpwl = 0.0
    for net in nets:
        if len(net) < 2:
            continue
        xs = [out[v][0] + nodes[v][0] / 2 for v in net]
        ys = [out[v][1] + nodes[v][1] / 2 for v in net]
        hpwl += max(xs) - min(xs) + max(ys) - min(ys)
    report(errors, (total, mx, worst, hpwl), quiet)


def report(errors, metrics, quiet):
    if errors:
        print(f'ILLEGAL ({len(errors)} problems)')
        for e in errors[:20]:
            print('  ' + e)
        if len(errors) > 20:
            print(f'  ... {len(errors) - 20} more')
    else:
        print('LEGAL')
    if metrics:
        total, mx, worst, hpwl = metrics
        print(f'Total displacement: {total:.1f}')
        print(f'Maximum displacement: {mx:.1f}' + ('' if quiet else f'  ({worst})'))
        print(f'Total HPWL: {hpwl:.1f}')
    sys.exit(1 if errors else 0)


if __name__ == '__main__':
    main()
