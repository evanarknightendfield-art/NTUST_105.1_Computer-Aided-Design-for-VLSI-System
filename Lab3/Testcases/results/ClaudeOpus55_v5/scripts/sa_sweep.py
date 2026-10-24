#!/usr/bin/env python3
"""sa_sweep.py — compare SA settings of ONE source tree from the same legal starts.

Builds AI_Solutions/ClaudeOpus55_v5/tests/sa_bench2.cpp against SRC, takes the Stage 1 placements of a run_ts.py
directory, and runs every config x seed x bench one at a time (timing matters).
Only for tuning: use Testcases/testcases and holdout benches, never edge / edge_seed2.

usage: sa_sweep.py SRC TS_DIR OUT --benches h04 h05 ... --config NAME "ARGS" [--config ...] [--seeds 1 2 3]
writes OUT/record.csv and OUT/summary.md (mean [min, max] per bench and config, geometric mean vs the first config)
"""
import argparse, csv, math, shlex, statistics as st, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[3]
CHECK = LAB / 'Grader' / 'tools' / 'check_pl.py'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('ts')
    ap.add_argument('out')
    ap.add_argument('--benches', nargs='+', required=True)
    ap.add_argument('--config', nargs=2, action='append', metavar=('NAME', 'ARGS'), required=True)
    ap.add_argument('--seeds', type=int, nargs='+', default=[1, 2, 3])
    a = ap.parse_args()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    src = Path(a.src).resolve()
    exe = out / 'sa_bench2'
    srcs = [str(src / f'{n}.cpp') for n in ['annealer', 'optimizer', 'window', 'placedb']]
    subprocess.run(['g++', '-std=c++17', '-O3', '-DNDEBUG', '-pthread', '-I' + str(src), str(LAB / 'AI_Solutions' / 'ClaudeOpus55_v5' / 'tests' / 'sa_bench2.cpp'), *srcs,
                    '-o', str(exe)], check=True)
    rec = out / 'record.csv'
    fields = ['bench', 'config', 'args', 'seed', 'verdict', 'hpwl', 'attempts', 'sa_seconds', 'stage2_seconds']
    done = set()
    if rec.exists():
        done = {(r['bench'], r['config'], int(r['seed'])) for r in csv.DictReader(open(rec)) if r['verdict'] == 'PASS'}
    new = not rec.exists()
    f = open(rec, 'a', newline='')
    w = csv.DictWriter(f, fieldnames=fields)
    if new:
        w.writeheader()
    ts = Path(a.ts).resolve()
    for b in a.benches:
        group = next(g for g in ['testcases', 'holdout', 'edge', 'edge_seed2'] if (LAB / 'Testcases' / g / b).exists())
        orig = LAB / 'Testcases' / group / b / b
        cache = out / 'legal_starts' / b
        cache.mkdir(parents=True, exist_ok=True)
        for ext in ['.aux', '.nodes', '.nets', '.wts', '.scl']:
            l = cache / ('output' + ext)
            if not l.exists() and (ts / b / ('output' + ext)).exists():
                l.symlink_to((ts / b / ('output' + ext)).resolve())
        l = cache / 'output.pl'
        if not l.exists():
            l.symlink_to((ts / b / 'output_legal.pl').resolve())
        for seed in a.seeds:
            for name, args in a.config:
                if (b, name, seed) in done:
                    continue
                d = out / name / f'{b}_s{seed}'
                d.mkdir(parents=True, exist_ok=True)
                cmd = [str(exe), str(orig), str(cache / 'output'), str(d / 'output'), '--seed', str(seed)] + shlex.split(args)
                with open(d / 'stdout.txt', 'w') as o, open(d / 'stderr.txt', 'w') as e:
                    p = subprocess.run(cmd, stdout=o, stderr=e, timeout=3600)
                r = dict(bench=b, config=name, args=args, seed=seed, verdict='FAIL')
                if p.returncode == 0:
                    r.update(next(csv.DictReader(open(d / 'stdout.txt'))))
                    ck = subprocess.run([sys.executable, str(CHECK), str(orig), str(d / 'output.pl'), '--quiet'],
                                        capture_output=True, text=True)
                    r['verdict'] = 'PASS' if ck.returncode == 0 else 'FAIL'
                w.writerow({k: r.get(k, '') for k in fields})
                f.flush()
                print(f"{b:14s} {name:12s} s{seed}: {r['verdict']} hpwl {r.get('hpwl', '')} att {r.get('attempts', '')} "
                      f"sa {r.get('sa_seconds', '')}", flush=True)
    f.close()
    summarize(rec, out, [n for n, _ in a.config])


def summarize(rec, out, names):
    rows = [r for r in csv.DictReader(open(rec)) if r['verdict'] == 'PASS']
    by = {}
    for r in rows:
        by.setdefault(r['bench'], {}).setdefault(r['config'], []).append(float(r['hpwl']))
    att = {}
    for r in rows:
        att.setdefault((r['bench'], r['config']), []).append(float(r['attempts'] or 0))
    lines = ['| bench | ' + ' | '.join(f'{n} mean [min, max]' for n in names) + ' | ' + ' | '.join(f'{n} / {names[0]}' for n in names[1:]) + ' |',
             '|---|' + '---:|' * (2 * len(names) - 1)]
    logs = {n: [] for n in names[1:]}
    for b, v in by.items():
        if any(n not in v for n in names):
            continue
        cells = [f'{st.mean(v[n]):,.1f} [{min(v[n]):,.1f}, {max(v[n]):,.1f}]' for n in names]
        rs = []
        for n in names[1:]:
            x = st.mean(v[n]) / st.mean(v[names[0]])
            logs[n].append(math.log(x))
            rs.append(f'{x:.4f}')
        lines.append(f'| {b} | ' + ' | '.join(cells) + ' | ' + ' | '.join(rs) + ' |')
    lines.append('')
    for n in names[1:]:
        if logs[n]:
            lines.append(f'- {n} / {names[0]}: geometric mean {math.exp(st.mean(logs[n])):.4f} over {len(logs[n])} benches')
    lines.append('')
    lines.append('| bench | ' + ' | '.join(f'{n} attempts' for n in names) + ' |')
    lines.append('|---|' + '---:|' * len(names))
    for b in by:
        lines.append(f'| {b} | ' + ' | '.join(f"{st.mean(att.get((b, n), [0])):,.0f}" for n in names) + ' |')
    (out / 'summary.md').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
