#!/usr/bin/env python3
"""Plot an SA trace written by `dp ... --trace FILE.csv`.

usage: plot_trace.py TRACE.csv [TRACE2.csv ...] [-o out.png] [--labels a,b]

Top: current and best-so-far HPWL vs attempted iterations.
Bottom: temperature (log scale) and acceptance rate.
"""
import argparse
import csv

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def load(path):
    cols = {}
    with open(path) as f:
        for row in csv.DictReader(f):
            for k, v in row.items():
                cols.setdefault(k, []).append(float(v))
    return cols


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('traces', nargs='+')
    ap.add_argument('-o', '--out', default='sa_trace.png')
    ap.add_argument('--labels')
    ap.add_argument('--title')
    a = ap.parse_args()
    labels = a.labels.split(',') if a.labels else [t.rsplit('/', 1)[-1] for t in a.traces]

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(7, 5.5), sharex=True, gridspec_kw={'height_ratios': [2.2, 1]})
    for path, lab in zip(a.traces, labels):
        c = load(path)
        it = [x / 1e6 for x in c['iteration']]
        cur = [x / 1e6 for x in c['current']]
        best = [x / 1e6 for x in c['best']]
        ln, = ax1.plot(it, cur, lw=0.8, alpha=0.7, label=f'{lab} current')
        ax1.plot(it, best, lw=1.6, color=ln.get_color(), ls='--', label=f'{lab} best-so-far')
        ax2.semilogy(it[:-1], c['temperature'][:-1], color=ln.get_color(), lw=1, label=f'{lab} T')
        ax3 = ax2.twinx()
        ax3.plot(it[1:-1], c['accept_rate'][1:-1], color=ln.get_color(), lw=0.6, alpha=0.5, ls=':')
        ax3.set_ylabel('accept rate (dotted)')
        ax3.set_ylim(0, 1)
    ax1.set_ylabel('HPWL (×10⁶)')
    ax1.legend(fontsize=8)
    ax1.grid(alpha=0.3)
    ax1.set_title(a.title or 'Simulated annealing')
    ax2.set_ylabel('temperature')
    ax2.set_xlabel('attempted iterations (×10⁶)')
    ax2.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(a.out, dpi=150)
    print('wrote', a.out)


if __name__ == '__main__':
    main()
