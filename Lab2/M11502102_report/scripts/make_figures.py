import argparse
import os
import subprocess
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from common import (COLORS, PART_COLORS, PLOT_VERSIONS, TREE_VERSIONS, FIGURE_DIR, GRID, INK, INK2, INPUT_DIR, LAB2, MUTED, RESULT_DIR, SURFACE,
                    VERSIONS, case_ids, lower_bounds, parse_log)

plt.rcParams.update({
    'font.size': 9,
    'axes.edgecolor': MUTED,
    'axes.labelcolor': INK2,
    'xtick.color': INK2,
    'ytick.color': INK2,
    'text.color': INK,
    'axes.facecolor': SURFACE,
    'figure.facecolor': SURFACE,
    'savefig.facecolor': SURFACE,
    'legend.frameon': False,
})


def last_stage(stages):
    for name in ('stage2', 'stage1'):
        if name in stages:
            return stages[name]
    return {}


def style(ax):
    for side in ('top', 'right', 'left'):
        ax.spines[side].set_visible(False)
    ax.grid(axis='x', color=GRID, linewidth=0.6)
    ax.set_axisbelow(True)
    ax.tick_params(length=0)


def tree_panel(set_name, cid, out_dir):
    inp = os.path.join(INPUT_DIR, set_name, f'input{cid}.cbi')
    pngs = []
    for tag, label, _ in VERSIONS:
        if tag not in TREE_VERSIONS:
            continue
        out = os.path.join(RESULT_DIR, set_name, tag, f'output{cid}.cbi')
        log = os.path.join(RESULT_DIR, set_name, tag, f'log{cid}.txt')
        if not os.path.exists(out):
            continue
        st = last_stage(parse_log(log)[0])
        title = f"{label}: cost={int(st.get('cost', 0))}, skew={int(st.get('skew', 0))}, Score={int(st.get('score', 0))}"
        png = os.path.join(out_dir, f'tree_{cid}_{tag}.png')
        subprocess.run([sys.executable, os.path.join(LAB2, 'tools', 'plot_cbi.py'), inp, out, '-o', png,
                        '--title', title], check=True, capture_output=True)
        pngs.append(png)
    if not pngs:
        return
    cols = 2
    rows = (len(pngs) + 1) // 2
    fig, axes = plt.subplots(rows, cols, figsize=(10, 4.6 * rows))
    for ax in axes.flat:
        ax.axis('off')
    for ax, png in zip(axes.flat, pngs):
        ax.imshow(plt.imread(png))
    fig.tight_layout()
    path = os.path.join(out_dir, f'fig_trees_{cid}.png')
    fig.savefig(path, dpi=160)
    plt.close(fig)
    print('wrote', path)


def score_by_version(set_name, out_dir):
    in_dir = os.path.join(INPUT_DIR, set_name)
    ids = case_ids(in_dir)
    base_tag = VERSIONS[0][0]
    markers = dict(zip(PLOT_VERSIONS, ['o', 's', 'D']))
    offsets = dict(zip(PLOT_VERSIONS, [-0.22, 0.0, 0.22]))
    fig, ax = plt.subplots(figsize=(7, 0.32 * len(ids) + 1.4))
    labels = []
    for i, cid in enumerate(ids):
        n = lower_bounds(os.path.join(in_dir, f'input{cid}.cbi'))[3]
        labels.append(f'{cid} (N={n})')
    for tag, label, _ in [v for v in VERSIONS if v[0] in PLOT_VERSIONS]:
        xs, ys = [], []
        for i, cid in enumerate(ids):
            base = parse_log(os.path.join(RESULT_DIR, set_name, base_tag, f'log{cid}.txt'))[1]
            cur = parse_log(os.path.join(RESULT_DIR, set_name, tag, f'log{cid}.txt'))[1]
            if base and cur and base['score'] > 0:
                xs.append(cur['score'] / base['score'])
                ys.append(i + offsets.get(tag, 0))
        ax.scatter(xs, ys, s=42, marker=markers.get(tag, 'o'), color=COLORS[tag], label=label,
                   edgecolors=SURFACE, linewidths=1.5, zorder=3)
    ax.axvline(1.0, color=MUTED, linewidth=1, linestyle='--', zorder=1)
    ax.text(1.0, -0.7, ' Construction = 1.0', color=INK2, fontsize=8, va='bottom')
    ax.set_yticks(range(len(ids)))
    ax.set_yticklabels(labels)
    ax.invert_yaxis()
    ax.set_xlim(0, 1.12)
    ax.set_xlabel('Score relative to the initial construction (lower is better)')
    style(ax)
    ax.legend(loc='lower left', bbox_to_anchor=(0, 1.0), ncol=3, borderaxespad=0.2)
    ax.set_title('Final Score of each optimization version', loc='left', fontsize=10, pad=24)
    fig.tight_layout()
    path = os.path.join(out_dir, 'fig_score_by_version.png')
    fig.savefig(path, dpi=200)
    plt.close(fig)
    print('wrote', path)


def cost_skew_split(set_name, out_dir):
    in_dir = os.path.join(INPUT_DIR, set_name)
    ids = case_ids(in_dir)
    tag, label, _ = VERSIONS[-1]
    fig, ax = plt.subplots(figsize=(7, 0.32 * len(ids) + 1.4))
    yt, yl = [], []
    for i, cid in enumerate(ids):
        n, w = lower_bounds(os.path.join(in_dir, f'input{cid}.cbi'))[3:]
        stages = parse_log(os.path.join(RESULT_DIR, set_name, tag, f'log{cid}.txt'))[0]
        if 'stage1' not in stages or 'stage2' not in stages:
            continue
        base = stages['stage1']['score'] or 1
        for j, st in enumerate(('stage1', 'stage2')):
            y = i * 2.6 + j * 1.05
            cost = stages[st]['cost'] / base
            skew = w * stages[st]['skew'] / base
            ax.barh(y, cost, height=0.9, color=PART_COLORS['cost'], label='buffer cost' if i == 0 and j == 0 else None)
            ax.barh(y, skew, left=cost, height=0.9, color=PART_COLORS['skew'], edgecolor=SURFACE, linewidth=2,
                    label='w_skew × skew' if i == 0 and j == 0 else None)
            yt.append(y)
            yl.append(f"{cid} {'initial' if j == 0 else 'final'}")
    ax.set_yticks(yt)
    ax.set_yticklabels(yl, fontsize=7)
    ax.invert_yaxis()
    ax.set_xlabel('Score split, relative to the initial Score of each case')
    style(ax)
    ax.legend(loc='lower right')
    ax.set_title(f'Where the Score comes from: initial construction vs. {label}', loc='left', fontsize=10)
    fig.tight_layout()
    path = os.path.join(out_dir, 'fig_cost_skew_split.png')
    fig.savefig(path, dpi=200)
    plt.close(fig)
    print('wrote', path)


def main():
    ap = argparse.ArgumentParser(description='把 results/<set>/ 畫成圖到 figures/<set>/')
    ap.add_argument('--set', default='testcases')
    ap.add_argument('--trees', default='001,004', help='哪些測資要畫四個版本的樹（逗號分隔）')
    a = ap.parse_args()
    out_dir = os.path.join(FIGURE_DIR, a.set)
    os.makedirs(out_dir, exist_ok=True)
    for cid in [c for c in a.trees.split(',') if c]:
        tree_panel(a.set, cid, out_dir)
    score_by_version(a.set, out_dir)
    cost_skew_split(a.set, out_dir)


if __name__ == '__main__':
    main()
