#!/usr/bin/env python3
"""Draw a placement (Project 3 GUI substitute).

usage: plot_pl.py BENCH [PLACED_PL] [-o out.png] [--arrows N] [--window X0 Y0 X1 Y1] [--title T]
  BENCH       benchmark prefix (reads BENCH.aux; the input .pl is the global placement)
  PLACED_PL   placement to draw; without it the global placement is drawn
  --arrows N  draw displacement arrows (GP -> placed) for the N most displaced cells
  --window    zoom to a region (chip coordinates)
  --heat      color cells by displacement instead of a flat color

Cells are drawn as rectangles; fixed objects in gray; rows as thin lines.
"""
import argparse
import os
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.collections import PatchCollection
from matplotlib.patches import Rectangle

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_pl import read_aux, read_nodes, read_pl, read_scl  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bench')
    ap.add_argument('placed', nargs='?')
    ap.add_argument('-o', '--out', default='placement.png')
    ap.add_argument('--arrows', type=int, default=0)
    ap.add_argument('--window', type=float, nargs=4)
    ap.add_argument('--heat', action='store_true')
    ap.add_argument('--title')
    a = ap.parse_args()

    files = read_aux(a.bench)
    nodes = read_nodes(files['nodes'])
    gp = read_pl(files['pl'])
    pl = read_pl(a.placed) if a.placed else gp
    rows = read_scl(files['scl'])
    fixed = {n for n, (w, h, t) in nodes.items() if t or gp[n][3]}

    xs0 = min(r[2] for r in rows)
    xs1 = max(r[2] + r[4] * r[3] for r in rows)
    ys0 = min(r[0] for r in rows)
    ys1 = max(r[0] + r[1] for r in rows)

    fig, ax = plt.subplots(figsize=(8, 8 * (ys1 - ys0) / (xs1 - xs0) if a.window is None else 8))
    if a.window:
        x0, y0, x1, y1 = a.window
    else:
        x0, y0, x1, y1 = xs0, ys0, xs1, ys1

    def visible(x, y, w, h):
        return x + w >= x0 and x <= x1 and y + h >= y0 and y <= y1

    # rows
    if (y1 - y0) / rows[0][1] < 200:
        for (ry, rh, org, sp, ns) in rows:
            if y0 <= ry <= y1:
                ax.plot([org, org + ns * sp], [ry, ry], color='#dddddd', lw=0.3, zorder=0)
    ax.add_patch(Rectangle((xs0, ys0), xs1 - xs0, ys1 - ys0, fill=False, ec='#3366cc', lw=1))

    rects, colors, fixed_rects = [], [], []
    dmax = 1e-9
    disp = {}
    for n, (w, h, _) in nodes.items():
        x, y = pl[n][0], pl[n][1]
        if n in fixed:
            if visible(x, y, w, h) and w > 0 and h > 0:
                fixed_rects.append(Rectangle((x, y), w, h))
            continue
        d = abs(x - gp[n][0]) + abs(y - gp[n][1])
        disp[n] = d
        dmax = max(dmax, d)
        if visible(x, y, w, h):
            rects.append(Rectangle((x, y), w, h))
            colors.append(d)

    if fixed_rects:
        ax.add_collection(PatchCollection(fixed_rects, fc='#999999', ec='#666666', lw=0.2))
    many = len(rects) > 3000
    if a.heat:
        pc = PatchCollection(rects, cmap='viridis', ec='none' if many else '#222222', lw=0.2)
        pc.set_array(colors)
        pc.set_clim(0, dmax)
        ax.add_collection(pc)
        fig.colorbar(pc, ax=ax, fraction=0.04, label='displacement')
    else:
        ax.add_collection(PatchCollection(rects, fc='#b48ee8' if many else 'none',
                                          ec='none' if many else '#8a2be2', lw=0.6, alpha=0.8 if many else 1))

    if a.arrows:
        top = sorted(disp, key=disp.get, reverse=True)[:a.arrows]
        for n in top:
            w, h, _ = nodes[n]
            ax.annotate('', xy=(pl[n][0] + w / 2, pl[n][1] + h / 2), xytext=(gp[n][0] + w / 2, gp[n][1] + h / 2),
                        arrowprops=dict(arrowstyle='->', color='red', lw=0.8))

    pad_x, pad_y = (x1 - x0) * 0.02, (y1 - y0) * 0.02
    ax.set_xlim(x0 - pad_x, x1 + pad_x)
    ax.set_ylim(y0 - pad_y, y1 + pad_y)
    ax.set_aspect('equal')
    ax.set_title(a.title or os.path.basename(a.placed or files['pl']))
    fig.tight_layout()
    fig.savefig(a.out, dpi=150)
    print('wrote', a.out)


if __name__ == '__main__':
    main()
