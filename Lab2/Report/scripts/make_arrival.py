import argparse
import os

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager

from common import FIGURE_DIR, INPUT_DIR, RESULT_DIR
import verifier

FONT = '/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc'
COLORS = {'v1': '#2A78D6', 'v5': '#C85327'}
NAMES = {'v1': 'v1  基礎建構', 'v5': 'v5  最終結果'}


def sink_arrivals(set_name, cid, tag):
    inp = verifier.parse_input(os.path.join(INPUT_DIR, set_name, f'input{cid}.cbi'))
    bufs, _, levels, _ = verifier.parse_output(os.path.join(RESULT_DIR, set_name, tag, f'output{cid}.cbi'))
    coords = {'SRC': inp[3], **inp[4], **{b: xy for b, (_, xy) in bufs.items()}}
    arrival = {'SRC': 0}
    for _, groups in sorted(levels):
        for p, kids in groups.items():
            for c in kids:
                arrival[c] = arrival[p] + verifier.manhattan(coords[p], coords[c])
    return sorted(arrival[s] for s in inp[4])


def main():
    ap = argparse.ArgumentParser(description='畫出兩個版本的 sink arrival time 分布，輸出到 figures/<set>/fig_arrival_<case>.png')
    ap.add_argument('--set', default='testcases')
    ap.add_argument('--case', default='004')
    ap.add_argument('--versions', default='v1,v5')
    a = ap.parse_args()

    font_manager.fontManager.addfont(FONT)
    plt.rcParams.update({'font.family': 'Noto Sans CJK JP', 'font.size': 9, 'axes.unicode_minus': False,
                         'axes.spines.top': False, 'axes.spines.right': False})
    tags = a.versions.split(',')
    fig, ax = plt.subplots(figsize=(6.8, 1.5))
    top = 0
    for idx, tag in enumerate(tags):
        ts = sink_arrivals(a.set, a.case, tag)
        y = len(tags) - 1 - idx
        color = COLORS.get(tag, '#555555')
        ax.hlines(y, ts[0], ts[-1], color=color, linewidth=2)
        ax.scatter(ts, [y + ((i % 7) - 3) * 0.035 for i in range(len(ts))], s=10, color=color, alpha=.8, zorder=3)
        ax.text(ts[-1] + 12, y, f'{ts[0]}–{ts[-1]}', va='center', fontsize=9, color=color)
        top = max(top, ts[-1])
        print(f'{tag}: arrival {ts[0]}–{ts[-1]}, skew {ts[-1] - ts[0]}')
    ax.set_yticks(range(len(tags) - 1, -1, -1), [NAMES.get(t, t) for t in tags])
    xmax = (top // 200 + 1) * 200
    ax.set_xlim(0, xmax + 30)
    ax.set_ylim(-.42, len(tags) - 1 + .42)
    ax.set_xlabel('Sink 到達時間（Manhattan 路徑長度）', labelpad=3)
    ax.set_xticks(range(0, xmax + 1, 200))
    ax.grid(axis='x', color='#dddddd', linewidth=.6)
    ax.spines['left'].set_visible(False)
    ax.tick_params(axis='y', length=0)
    fig.subplots_adjust(left=.17, right=.99, bottom=.32, top=.94)
    out = os.path.join(FIGURE_DIR, a.set)
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, f'fig_arrival_{a.case}.png')
    fig.savefig(path, dpi=260, facecolor='white')
    print('wrote', path)


if __name__ == '__main__':
    main()
