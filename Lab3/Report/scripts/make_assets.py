#!/usr/bin/env python3
"""Turn ROOT/results/*/ into report tables (ROOT/tables/*.md) and figures (ROOT/figures/*.png).

usage: python3 make_assets.py [--root DIR]      (default ROOT: Lab3/Report)

Run run_experiments.py first. Sections whose results are missing are skipped,
so this works both for your program (main, holdout) and for the reference
(--root ../../AI_Workspace/reference_report, all experiments).
"""
import argparse
import glob
import json
import os
import statistics
import subprocess
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
LAB3 = os.path.abspath(os.path.join(HERE, '..', '..'))
TOOLS = os.path.join(LAB3, 'Grader', 'tools')
TESTCASES = os.path.join(LAB3, 'Testcases', 'testcases')
HOLDOUT = os.path.join(LAB3, 'Testcases', 'holdout')
RES = TAB = FIG = ''


def load(exp):
    out = {}
    for p in glob.glob(os.path.join(RES, exp, '*.json')):
        r = json.load(open(p))
        out[(r['bench'], r['tag'])] = r
    return out


def gp_hpwl(prefix):
    p = subprocess.run([sys.executable, os.path.join(TOOLS, 'check_pl.py'), prefix, prefix + '.pl'],
                       capture_output=True, text=True)
    for line in p.stdout.splitlines():
        if line.startswith('Total HPWL'):
            return float(line.split(':')[1])
    return float('nan')


def movable_cells(prefix):
    n = 0
    for line in open(prefix + '.nodes'):
        t = line.split()
        if len(t) >= 3 and t[0] != 'UCLA' and not t[0].lower().startswith('num') and not t[0].startswith('#'):
            n += len(t) == 3
    return n


def num(x, d=0):
    if x is None:
        return 'fail'
    return f'{x:,.{d}f}'


def write(name, lines):
    with open(os.path.join(TAB, name), 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print('wrote', os.path.join(TAB, name))


def result_table(recs, benches, bench_dir, name):
    lines = ['| bench | cells | total disp | max disp | HPWL GP (overlapping) | HPWL legal | HPWL SA | SA gain | runtime (s) |',
             '|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    for b in benches:
        r = recs.get((b, 'final'))
        if not r:
            continue
        prefix = os.path.join(bench_dir, b, b)
        ok = r['hpwl_sa'] is not None and r['hpwl_legal']
        gain = f"{1 - r['hpwl_sa'] / r['hpwl_legal']:.1%}" if ok else '-'
        lines.append(f"| {b} | {movable_cells(prefix):,} | {num(r['total_disp'], 1)} | {num(r['max_disp'], 1)} | "
                     f"{num(gp_hpwl(prefix), 1)} | {num(r['hpwl_legal'])} | {num(r['hpwl_sa'])} | {gain} | {r['seconds']:.1f} |")
    write(name, lines)


def plot(tool, *args):
    subprocess.run([sys.executable, os.path.join(TOOLS, tool)] + [str(x) for x in args])


def main():
    global RES, TAB, FIG
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=os.path.join(LAB3, 'Report'))
    root = os.path.abspath(ap.parse_args().root)
    RES, TAB, FIG = (os.path.join(root, d) for d in ('results', 'tables', 'figures'))
    os.makedirs(TAB, exist_ok=True)
    os.makedirs(FIG, exist_ok=True)
    benches = ['toy', 'ibm01', 'ibm05']

    # ---- main results and holdout
    m = load('main')
    if m:
        result_table(m, benches, TESTCASES, 'main.md')
        for b in ['ibm01', 'ibm05']:
            if os.path.exists(os.path.join(RES, 'main', f'{b}_trace.csv')):
                plot('plot_trace.py', os.path.join(RES, 'main', f'{b}_trace.csv'), '-o',
                     os.path.join(FIG, f'sa_trace_{b}.png'), '--labels', b, '--title', f'SA on {b}')
        if os.path.exists(os.path.join(RES, 'main', 'toy_final_legal.pl')):
            plot('plot_pl.py', os.path.join(TESTCASES, 'toy', 'toy'), os.path.join(RES, 'main', 'toy_final_legal.pl'),
                 '--arrows', 31, '--window', 26000, -31000, 32000, -26000, '-o', os.path.join(FIG, 'toy_legal.png'),
                 '--title', 'toy: Stage 1 (arrows: GP -> legal)')
        if os.path.exists(os.path.join(RES, 'main', 'ibm01_final_legal.pl')):
            plot('plot_pl.py', os.path.join(TESTCASES, 'ibm01', 'ibm01'), '-o', os.path.join(FIG, 'ibm01_gp.png'),
                 '--title', 'ibm01: global placement (input)')
            plot('plot_pl.py', os.path.join(TESTCASES, 'ibm01', 'ibm01'), os.path.join(RES, 'main', 'ibm01_final_legal.pl'),
                 '--heat', '--arrows', 20, '-o', os.path.join(FIG, 'ibm01_legal_heat.png'),
                 '--title', 'ibm01: Stage 1 displacement')
    h = load('holdout')
    if h:
        result_table(h, sorted({b for b, _ in h}), HOLDOUT, 'holdout.md')

    # ---- stage 1 ablation
    s1 = load('stage1')
    if s1:
        order = [('tetris', 'Tetris (greedy, L07)'), ('abacus_own', 'Abacus, cost = own displacement'),
                 ('abacus', 'Abacus, cost = total change'), ('abacus+refine', '+ insert/push & swap refinement'),
                 ('abacus+refine+evict', '+ evict (2-step) moves = final')]
        lines = ['| method | ' + ' | '.join(f'{b} total | {b} max' for b in benches) + ' |',
                 '|---|' + '---:|' * (2 * len(benches))]
        for tag, label in order:
            row = [label]
            for b in benches:
                r = s1.get((b, tag), {})
                row += [num(r.get('total_disp'), 0), num(r.get('max_disp'), 1)]
            lines.append('| ' + ' | '.join(row) + ' |')
        write('stage1.md', lines)
        if os.path.exists(os.path.join(RES, 'stage1', 'ibm01_abacus_legal.pl')):
            plot('plot_pl.py', os.path.join(TESTCASES, 'ibm01', 'ibm01'), os.path.join(RES, 'stage1', 'ibm01_abacus_legal.pl'),
                 '--heat', '--arrows', 20, '-o', os.path.join(FIG, 'ibm01_abacus_heat.png'),
                 '--title', 'ibm01: plain Abacus displacement')

    # ---- SA ablation
    sa = load('sa')
    if sa:
        order = [('final', 'final (p0 = 0.8, targeted 50%, VPR range limiter)'), ('greedy', 'greedy (T = 0)'),
                 ('p0=0.05', 'p0 = 0.05'), ('p0=0.3', 'p0 = 0.3'), ('p0=0.95', 'p0 = 0.95'),
                 ('no_target', 'no optimal-region moves'), ('all_target', 'only optimal-region moves'),
                 ('clamp_target', 'optimal-region moves limited by window'),
                 ('no_range_limit', 'no range limiter (window = chip)')]
        lines = ['| SA variant | ibm01 HPWL | vs final | ibm05 HPWL | vs final |', '|---|---:|---:|---:|---:|']
        for tag, label in order:
            row = [label]
            for b in ['ibm01', 'ibm05']:
                r = sa.get((b, tag))
                f = sa.get((b, 'final'))
                row += [num(r and r['hpwl_sa']), f"{r['hpwl_sa'] / f['hpwl_sa'] - 1:+.1%}" if r and f else '-']
            lines.append('| ' + ' | '.join(row) + ' |')
        write('sa_ablation.md', lines)
        for b in ['ibm01', 'ibm05']:
            traces = [os.path.join(RES, 'sa', f'{b}_{t}_trace.csv') for t in ['final', 'greedy', 'no_range_limit']]
            if all(os.path.exists(t) for t in traces):
                plot('plot_trace.py', *traces, '-o', os.path.join(FIG, f'sa_compare_{b}.png'),
                     '--labels', 'final,greedy,no range limiter', '--title', f'{b}: SA vs greedy vs no range limiter')

    # ---- effort
    ef = load('effort')
    if ef and m:
        efforts = sorted({int(t[6:]) for (_, t) in ef})
        lines = ['| moves / cell | ' + ' | '.join(f'{b} HPWL | {b} s' for b in ['ibm01', 'ibm05']) + ' |',
                 '|---:|' + '---:|' * 4]
        for e in efforts:
            row = [str(e)]
            for b in ['ibm01', 'ibm05']:
                r = ef.get((b, f'effort{e}'))
                row += [num(r and r['hpwl_sa']), f"{r['seconds']:.1f}" if r else '-']
            lines.append('| ' + ' | '.join(row) + ' |')
        lines += ['', '(runtimes measured with 6 runs in parallel, so they are higher than single runs)']
        write('effort.md', lines)

        fig, ax = plt.subplots(figsize=(5, 3.2))
        for b in ['ibm01', 'ibm05']:
            xs = [e for e in efforts if (b, f'effort{e}') in ef]
            base = m[(b, 'final')]['hpwl_legal']
            ax.semilogx(xs, [100 * (1 - ef[(b, f'effort{e}')]['hpwl_sa'] / base) for e in xs], 'o-', label=b)
        ax.set_xlabel('attempted moves per cell')
        ax.set_ylabel('HPWL reduction vs legal (%)')
        ax.grid(alpha=0.3)
        ax.legend()
        fig.tight_layout()
        fig.savefig(os.path.join(FIG, 'effort.png'), dpi=160)
        plt.close(fig)

    # ---- seeds
    sd = load('seeds')
    if sd:
        lines = ['| bench | runs | mean HPWL | stdev | min | max |', '|---|---:|---:|---:|---:|---:|']
        for b in ['ibm01', 'ibm05']:
            v = [r['hpwl_sa'] for (bb, _), r in sd.items() if bb == b]
            if v:
                lines.append(f'| {b} | {len(v)} | {num(statistics.mean(v))} | '
                             f'{statistics.pstdev(v) / statistics.mean(v):.2%} | {num(min(v))} | {num(max(v))} |')
        write('seeds.md', lines)


if __name__ == '__main__':
    main()
