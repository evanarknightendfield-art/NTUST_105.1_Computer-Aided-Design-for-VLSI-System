#!/usr/bin/env python3
"""Demo: what check_pl.py accepts and rejects.

usage: python3 run_demo.py [--dp PATH]

1. copies holdout h01 (300 cells, 2 fixed macros inside the rows, split rows, pads) to inputs/
2. runs the reference dp on it → results/GOOD/  (legal)
3. breaks GOOD's output_legal.pl in one way each → results/BAD_*/
4. runs check_pl.py on every result → record.csv, summary.csv
"""
import argparse
import csv
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LAB3 = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(LAB3, 'Grader', 'tools'))
from check_pl import read_aux, read_nodes, read_pl, read_scl  # noqa: E402

CHECK = os.path.join(LAB3, 'Grader', 'tools', 'check_pl.py')
SRC = os.path.join(LAB3, 'Testcases', 'holdout', 'h01')


def write_pl(path, pl, order):
    with open(path, 'w') as f:
        f.write('UCLA pl 1.0\n\n')
        for n in order:
            if n in pl:
                x, y, o, fixed = pl[n]
                f.write(f'{n}\t{x:g}\t{y:g}\t: {o}' + (' /FIXED' if fixed else '') + '\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dp', default=os.path.join(LAB3, 'AI_Solutions', 'ClaudeOpus55', 'build', 'dp'))
    a = ap.parse_args()

    inputs = os.path.join(HERE, 'inputs')
    results = os.path.join(HERE, 'results')
    os.makedirs(inputs, exist_ok=True)
    for f in os.listdir(SRC):
        shutil.copy(os.path.join(SRC, f), inputs)
    bench = os.path.join(inputs, 'h01')

    good = os.path.join(results, 'GOOD')
    os.makedirs(good, exist_ok=True)
    subprocess.run([a.dp, bench, os.path.join(good, 'output')], check=True, capture_output=True)
    for ext in ('aux', 'nodes', 'nets', 'wts', 'scl'):
        os.remove(os.path.join(good, f'output.{ext}'))

    files = read_aux(bench)
    nodes = read_nodes(files['nodes'])
    order = list(nodes)
    base = read_pl(os.path.join(good, 'output_legal.pl'))
    rows = read_scl(files['scl'])
    macros = [n for n in order if n.startswith('m')]
    cells = [n for n in order if n.startswith('c')]
    c0, c1 = cells[0], cells[1]
    sp, rh = rows[0][3], rows[0][1]
    split = next(r for r in rows if sum(1 for q in rows if q[0] == r[0]) == 2)      # a row with two subrows
    left = min((q for q in rows if q[0] == split[0]), key=lambda q: q[2])
    gap_x = left[2] + left[4] * sp                                                 # first site of the gap
    m = macros[0]
    mx, my = base[m][0], base[m][1]
    on_macro_y = next(r[0] for r in rows if my <= r[0] < my + nodes[m][1])          # a row the macro covers
    on_macro_x = left[2] + round((mx - left[2]) / sp) * sp

    def variant(name, why, change):
        pl = dict(base)
        change(pl)
        d = os.path.join(results, name)
        os.makedirs(d, exist_ok=True)
        if pl is not None:
            write_pl(os.path.join(d, 'output_legal.pl'), pl, order)
        return name, why

    cases = [('GOOD', '參考解的 Stage 1 輸出')]
    cases.append(variant('BAD_OVERLAP', f'{c1} 搬到 {c0} 的位置', lambda p: p.__setitem__(c1, (p[c0][0], p[c0][1]) + p[c1][2:])))
    cases.append(variant('BAD_OFF_SITE', f'{c0} 的 x 偏半個 site', lambda p: p.__setitem__(c0, (p[c0][0] + sp / 2,) + p[c0][1:])))
    cases.append(variant('BAD_OFF_ROW', f'{c0} 的 y 偏半個 row', lambda p: p.__setitem__(c0, (p[c0][0], p[c0][1] + rh / 2) + p[c0][2:])))
    cases.append(variant('BAD_OUTSIDE', f'{c0} 超出 row 的右端', lambda p: p.__setitem__(c0, (left[2] + 10 ** 6 * sp, p[c0][1]) + p[c0][2:])))
    cases.append(variant('BAD_IN_GAP', f'{c0} 放在 split row 的缺口（兩個 subrow 中間）',
                         lambda p: p.__setitem__(c0, (gap_x, split[0]) + p[c0][2:])))
    cases.append(variant('BAD_ON_MACRO', f'{c0} 疊在 fixed macro {m} 上',
                         lambda p: p.__setitem__(c0, (on_macro_x, on_macro_y) + p[c0][2:])))
    cases.append(variant('BAD_FIXED_MOVED', f'fixed macro {m} 被移動', lambda p: p.__setitem__(m, (p[m][0] + sp, p[m][1]) + p[m][2:])))
    cases.append(variant('BAD_ORIENT', f'{c0} 的方向 N → FS', lambda p: p.__setitem__(c0, p[c0][:2] + ('FS', p[c0][3]))))
    cases.append(variant('BAD_MISSING_CELL', f'{c0} 那一行被刪掉', lambda p: p.pop(c0)))
    nd = os.path.join(results, 'NO_OUTPUT')
    os.makedirs(nd, exist_ok=True)
    if os.path.exists(os.path.join(nd, 'output_legal.pl')):
        os.remove(os.path.join(nd, 'output_legal.pl'))
    cases.append(('NO_OUTPUT', '沒有產生輸出檔'))

    records = []
    for name, why in cases:
        pl = os.path.join(results, name, 'output_legal.pl')
        if not os.path.exists(pl):
            verdict, reason = 'ILLEGAL', 'output_legal.pl not found'
        else:
            p = subprocess.run([sys.executable, CHECK, bench, pl, '--quiet'], capture_output=True, text=True)
            lines = p.stdout.splitlines()
            verdict = 'LEGAL' if p.returncode == 0 else 'ILLEGAL'
            reason = lines[1].strip() if p.returncode else lines[1].split(':', 1)[1].strip() + ' (total disp)'
        expect = 'LEGAL' if name == 'GOOD' else 'ILLEGAL'
        records.append({'case': name, 'change': why, 'checker': verdict, 'expected': expect,
                        'ok': verdict == expect, 'first_problem': reason})
        print(f'{name:18s} {verdict:8s} {"ok" if verdict == expect else "WRONG"}  {reason}')

    with open(os.path.join(HERE, 'record.csv'), 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(records[0]))
        w.writeheader()
        w.writerows(records)
    with open(os.path.join(HERE, 'summary.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['cases', 'checker_correct'])
        w.writerow([len(records), sum(r['ok'] for r in records)])


if __name__ == '__main__':
    main()
