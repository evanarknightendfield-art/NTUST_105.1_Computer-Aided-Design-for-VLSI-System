#!/usr/bin/env python3
"""
stage_report.py — 跑過所有測資，整理「各階段 cost / skew / Score」表格（報告用）

usage:
  python stage_report.py CBI_BINARY INPUTS_DIR OUT_DIR [--time T] [--plot 001,004,...]

輸出：
  OUT_DIR/outputXXX.cbi                 最終解
  OUT_DIR/outputXXX.stage1.cbi / .stage2.cbi   中間階段的解（需要 cbi 支援 -d）
  OUT_DIR/stages.csv, OUT_DIR/stages.md 表格
  OUT_DIR/figXXX_stage{1,2,3}.png       --plot 指定的測資畫圖
"""
import argparse
import glob
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from cbi_explain import parse_input  # noqa: E402
# 接受任何形如「[stage1...] ... buffers=.. cost=.. skew=.. score=..」的 stderr 行（順序不拘、可多可少欄位）
LINE = re.compile(r'^\[(stage\d)[^\]]*\](.*)$', re.M)
KV = re.compile(r'(\w+)=(-?[\d.]+)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cbi')
    ap.add_argument('inputs')
    ap.add_argument('out')
    ap.add_argument('--time', type=float, default=None, help='每筆的 -t（預設用程式自己的預設值）')
    ap.add_argument('--plot', default='', help='要畫圖的測資編號，逗號分隔')
    ap.add_argument('--no-extra-args', action='store_true', help='你的 cbi 不支援 -d / -t 時加這個')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    plots = set(x for x in a.plot.split(',') if x)

    rows = []
    for inp in sorted(glob.glob(os.path.join(a.inputs, 'input[0-9][0-9][0-9].cbi'))):
        tid = os.path.basename(inp)[5:8]
        outp = os.path.join(a.out, f'output{tid}.cbi')
        cmd = [a.cbi, inp, outp]
        if not a.no_extra_args:
            cmd += ['-d', os.path.join(a.out, f'output{tid}')]
            if a.time:
                cmd += ['-t', str(a.time)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        _, _, w_skew, pos = parse_input(inp)
        n_sinks = len(pos) - 1
        best = {}
        for m in LINE.finditer(r.stderr):
            kv = dict(KV.findall(m.group(2)))
            if not all(k in kv for k in ('cost', 'skew', 'score')):
                continue
            vals = dict(buf=int(float(kv.get('buffers', 0))), cost=int(float(kv['cost'])),
                        skew=int(float(kv['skew'])), score=int(float(kv['score'])))
            st = m.group(1)
            if st not in best or vals['score'] < best[st]['score']:
                best[st] = vals
        if not best:
            print(f'{tid}: no [stageN] lines on stderr', flush=True)
            continue
        rows.append((tid, n_sinks, w_skew, best))
        first, last = best[min(best)], best[max(best)]
        print(f"{tid}: N={rows[-1][1]:>6}  " + '  '.join(f"{k}={v['score']:>8}" for k, v in sorted(best.items())) +
              f"  ({100 * (first['score'] - last['score']) / max(1, first['score']):.1f}% better)", flush=True)

        if tid in plots:
            for k, f in ((1, f'output{tid}.stage1.cbi'), (2, f'output{tid}.stage2.cbi'), (3, f'output{tid}.cbi')):
                if not os.path.exists(os.path.join(a.out, f)):
                    continue
                subprocess.run([sys.executable, os.path.join(HERE, 'plot_cbi.py'), inp, os.path.join(a.out, f),
                                '-o', os.path.join(a.out, f'fig{tid}_stage{k}.png')], capture_output=True)

    stages = sorted({st for _, _, _, b in rows for st in b})
    get = lambda b, st, k: b[st][k] if st in b else ''
    with open(os.path.join(a.out, 'stages.csv'), 'w') as f:
        f.write('case,N,w_skew,' + ','.join(f'{s}_{k}' for s in stages for k in ('buf', 'cost', 'skew', 'score')) + '\n')
        for tid, n, w, b in rows:
            f.write(f'{tid},{n},{w},' + ','.join(str(get(b, s, k)) for s in stages
                                                 for k in ('buf', 'cost', 'skew', 'score')) + '\n')
    with open(os.path.join(a.out, 'stages.md'), 'w') as f:
        f.write('| case | N | w | ' + ' | '.join(f'{s} cost / skew / Score' for s in stages) + ' | 改善 |\n')
        f.write('|' + '---|' * (len(stages) + 4) + '\n')
        for tid, n, w, b in rows:
            cells = [f"{b[s]['cost']} / {b[s]['skew']} / **{b[s]['score']}**" if s in b else '-' for s in stages]
            first, last = b[min(b)], b[max(b)]
            imp = 100 * (first['score'] - last['score']) / max(1, first['score'])
            f.write(f'| {tid} | {n} | {w} | ' + ' | '.join(cells) + f' | {imp:.1f}% |\n')
    print('wrote', os.path.join(a.out, 'stages.md'))


if __name__ == '__main__':
    main()
