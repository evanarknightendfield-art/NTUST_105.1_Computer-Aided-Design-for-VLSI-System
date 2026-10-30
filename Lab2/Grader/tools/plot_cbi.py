#!/usr/bin/env python3
"""
plot_cbi.py — 把 CBI 輸入 / 輸出畫成 PNG（取代範例附的 gnuplot 腳本）

usage:
  python plot_cbi.py INPUT.cbi [OUTPUT.cbi] -o out.png [--labels] [--title "..."]

  只給 INPUT  → 只畫 SRC 與 sinks
  給了 OUTPUT → 另外畫 buffers 與樹的邊（L 形 Manhattan 走線，顏色代表層級），
                sink 顏色代表 arrival time（越黃越晚到）
"""
import argparse
import os
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cbi_explain import parse_input, parse_output, d  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('input')
    ap.add_argument('output', nargs='?')
    ap.add_argument('-o', '--png', required=True)
    ap.add_argument('--labels', action='store_true', help='標上節點名稱')
    ap.add_argument('--title', default=None)
    a = ap.parse_args()

    lim, lib, w, pos = parse_input(a.input)
    sinks = [k for k in pos if k != 'SRC']
    bufs, edges = {}, {}
    if a.output:
        bufs, edges, _ = parse_output(a.output)
        for b, (_, x, y) in bufs.items():
            pos[b] = (x, y)

    # BFS: arrival time 與層級
    arr, lvl, order = {'SRC': 0}, {'SRC': 0}, ['SRC']
    for u in order:
        for v in edges.get(u, []):
            arr[v] = arr[u] + d(pos[u], pos[v])
            lvl[v] = lvl[u] + 1
            order.append(v)
    maxlvl = max(lvl.values()) if lvl else 1

    n = len(sinks)
    big = n > 300
    fig, ax = plt.subplots(figsize=(7.5, 7.5 * lim['dimy'] / max(lim['dimx'], 1) + 0.6))
    ax.add_patch(plt.Rectangle((0, 0), lim['dimx'], lim['dimy'], fill=False, lw=1, ec='#888'))

    cmap = plt.get_cmap('viridis')
    # 邊：從 parent 先走水平再走垂直（只是示意，題目不要求真的繞線）
    for u, kids in edges.items():
        for v in kids:
            (x1, y1), (x2, y2) = pos[u], pos[v]
            c = cmap(0.15 + 0.7 * (lvl[v] - 1) / max(maxlvl - 1, 1))
            ax.plot([x1, x2, x2], [y1, y1, y2], color=c, lw=0.6 if big else 1.2, alpha=0.8, zorder=1)

    # sinks：顏色 = arrival time
    sx = [pos[s][0] for s in sinks]
    sy = [pos[s][1] for s in sinks]
    if edges:
        ts = [arr.get(s, 0) for s in sinks]
        sc = ax.scatter(sx, sy, c=ts, cmap='plasma', s=8 if big else 45, zorder=3, edgecolors='none')
        cb = fig.colorbar(sc, ax=ax, fraction=0.04, pad=0.02)
        cb.set_label('sink arrival time')
    else:
        ax.scatter(sx, sy, color='#1a9e77', s=8 if big else 45, zorder=3)

    # buffers：方塊，大小依 type 的 cost
    if bufs:
        costs = {k: v[2] for k, v in lib.items()}
        cmax = max(costs.values())
        for b, (bt, x, y) in bufs.items():
            sz = (14 if big else 60) * (0.5 + costs[bt] / cmax)
            ax.scatter([x], [y], marker='s', s=sz, color='#e6a100', edgecolors='k',
                       linewidths=0.4, zorder=4)
    ax.scatter([pos['SRC'][0]], [pos['SRC'][1]], marker='^', s=160, color='#7b1fa2', zorder=5)

    if a.labels and not big:
        for k, (x, y) in pos.items():
            txt = k if k not in arr or k == 'SRC' or k.startswith('B') else f"{k}\nT={arr[k]}"
            ax.annotate(txt, (x, y), textcoords='offset points', xytext=(4, -14), fontsize=8, color='#c62828')

    if a.title:
        title = a.title
    elif edges:
        ts = [arr[s] for s in sinks]
        cost = sum(lib[bt][2] for bt, _, _ in bufs.values())
        skew = max(ts) - min(ts)
        title = (f"{os.path.basename(a.output)}: {len(bufs)} buffers, cost={cost}, "
                 f"skew={skew}, Score={cost + w * skew}")
    else:
        title = f"{os.path.basename(a.input)}: {n} sinks"
    ax.set_title(title, fontsize=10)
    ax.set_xlim(-lim['dimx'] * 0.03, lim['dimx'] * 1.03)
    ax.set_ylim(-lim['dimy'] * 0.03, lim['dimy'] * 1.03)
    ax.set_aspect('equal')
    fig.tight_layout()
    fig.savefig(a.png, dpi=130)
    print('saved', a.png)


if __name__ == '__main__':
    main()
