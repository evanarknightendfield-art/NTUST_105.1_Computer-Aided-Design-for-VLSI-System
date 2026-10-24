#!/usr/bin/env python3
"""hpwl_compare.py — Stage 2 (SA + polish) of two versions, 3 seeds, from cached Stage 1 results.

Uses AI_Solutions/CodexChainDP/tests/sa_bench.cpp (a common Stage-2 driver) compiled against each
version's own sources, and the Stage 1 placements + Stage 1 times of a time-stamped default run
(scripts/run_ts.py). Stage 1 does not depend on the SA seed, so it is run once and charged once per seed.

modes (each is its own experiment; never mix their numbers):
  equal    same TOTAL time: budget = slower Stage 1 + 12 s, SA limit = budget - own Stage 1 - 2 s,
           polish 2 s, automatic threads  (= CodexChainDP's equal_time_default protocol)
  default  the programs' default Stage 2: SA limit 60 s, polish 30 s, automatic threads, each from
           its own Stage 1 (what a default run does, minus re-running Stage 1)
  fixed    same number of SA proposals: --moves N (default 5e6), 1 thread, no time limit, no polish,
           both from the SAME start (the first version's Stage 1) -> move quality only

usage: hpwl_compare.py MODE OUT_DIR --v NAME SRC_DIR TS_DIR [--v ...] [--groups holdout edge] [--seeds 1 2 3]
       [--moves N] [--benches a b ...]
"""
import argparse, csv, json, math, os, re, statistics as st, subprocess, sys, time
from pathlib import Path

LAB = Path(__file__).resolve().parents[4]
SA_BENCH = LAB / 'AI_Solutions' / 'CodexChainDP' / 'tests' / 'sa_bench.cpp'
CHECK = LAB / 'Grader' / 'tools' / 'check_pl.py'


def build(name, src, out):
    exe = out / f'{name}_sa'
    srcs = [str(src / f'{n}.cpp') for n in ['annealer', 'optimizer', 'window', 'placedb']]
    if (src / 'global.cpp').exists(): srcs.append(str(src / 'global.cpp'))
    cmd = ['taskset', '-c', '16-19', 'g++', '-std=c++17', '-O3', '-DNDEBUG', '-pthread', '-I' + str(src), str(SA_BENCH), *srcs, '-o', str(exe)]
    with open(out / f'{name}_build.log', 'w') as f:
        subprocess.run(cmd, stdout=f, stderr=f, check=True)
    return exe


def stage1(ts_dir, bench, cache):
    """(seconds, legal prefix) from a run_ts.py directory"""
    d = ts_dir / bench
    t = None
    for line in open(d / 'stderr_ts.tsv'):
        tt, _, msg = line.partition('\t')
        if msg.startswith('[stage1] done'):
            t = float(tt)
    if t is None:   # no movable cells: nothing printed; Stage 1 is instant
        t = 0.0
    c = cache / bench
    c.mkdir(parents=True, exist_ok=True)
    for ext in ['.aux', '.nodes', '.nets', '.wts', '.scl']:
        link = c / ('output' + ext)
        if not link.exists() and (d / ('output' + ext)).exists():
            link.symlink_to((d / ('output' + ext)).resolve())
    link = c / 'output.pl'
    if not link.exists():
        link.symlink_to((d / 'output_legal.pl').resolve())
    return t, c / 'output'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mode', choices=['equal', 'default', 'fixed'])
    ap.add_argument('out')
    ap.add_argument('--v', nargs=3, action='append', metavar=('NAME', 'SRC', 'TS'), required=True)
    ap.add_argument('--groups', nargs='+', default=['holdout', 'edge'])
    ap.add_argument('--seeds', type=int, nargs='+', default=[1, 2, 3])
    ap.add_argument('--moves', type=float, default=5e6)
    ap.add_argument('--benches', nargs='*')
    a = ap.parse_args()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    vers = [(n, Path(s).resolve(), Path(t).resolve()) for n, s, t in a.v]
    exes = {n: build(n, s, out) for n, s, _ in vers}
    (out / 'protocol.json').write_text(json.dumps(vars(a), indent=2, default=str) + '\n')
    rec_path = out / 'record.csv'
    fields = ['mode', 'group', 'bench', 'version', 'seed', 'verdict', 'budget', 'stage1_seconds', 'sa_limit', 'polish_limit',
              'threads', 'process_seconds', 'charged_seconds', 'hpwl', 'attempts', 'sa_seconds', 'stage2_seconds']
    done = set()
    if rec_path.exists():
        for r in csv.DictReader(open(rec_path)):
            if r['verdict'] == 'PASS':
                done.add((r['bench'], r['version'], int(r['seed'])))
    new = not rec_path.exists()
    f = open(rec_path, 'a', newline='')
    w = csv.DictWriter(f, fieldnames=fields)
    if new:
        w.writeheader()
    for group in a.groups:
        for aux in sorted((LAB / 'Testcases' / group).glob('*/*.aux')):
            bench = aux.stem
            if a.benches and bench not in a.benches:
                continue
            s1 = {n: stage1(ts, bench, out / 'legal_starts' / n) for n, _, ts in vers}
            budget = max(t for t, _ in s1.values()) + 12
            for seed in a.seeds:
                for n, _, _ in vers:
                    if (bench, n, seed) in done:
                        continue
                    t1, start = s1[n]
                    if a.mode == 'fixed':
                        start = s1[vers[0][0]][1]
                        moves, sa, pol, thr = str(int(a.moves)), 100000.0, 0.0, '1'
                    elif a.mode == 'equal':
                        moves, sa, pol, thr = 'default', budget - t1 - 2, 2.0, '0'
                    else:
                        moves, sa, pol, thr = 'default', 60.0, 30.0, '0'
                    d = out / n / f'{bench}_s{seed}'
                    d.mkdir(parents=True, exist_ok=True)
                    cmd = [str(exes[n]), str(aux.with_suffix('')), str(start), str(d / 'output'), str(seed), moves,
                           str(max(0.0, sa)), str(pol), thr]
                    t = time.monotonic()
                    with open(d / 'stdout.txt', 'w') as o, open(d / 'stderr.txt', 'w') as e:
                        p = subprocess.run(cmd, stdout=o, stderr=e, timeout=3600)
                    r = dict(mode=a.mode, group=group, bench=bench, version=n, seed=seed, verdict='FAIL',
                             budget=budget if a.mode == 'equal' else '', stage1_seconds=t1, sa_limit=sa, polish_limit=pol,
                             threads=thr, process_seconds=round(time.monotonic() - t, 3))
                    if p.returncode == 0:
                        r.update(next(csv.DictReader(open(d / 'stdout.txt'))))
                        r['charged_seconds'] = round(t1 + float(r['stage2_seconds']), 3)
                        ck = subprocess.run([sys.executable, str(CHECK), str(aux.with_suffix('')), str(d / 'output.pl'), '--quiet'],
                                            capture_output=True, text=True)
                        (d / 'check.txt').write_text(ck.stdout + ck.stderr)
                        if ck.returncode == 0:
                            r['verdict'] = 'PASS'
                    w.writerow({k: r.get(k, '') for k in fields})
                    f.flush()
                    try:
                        os.remove(d / 'output.trace.csv')
                    except OSError:
                        pass
                    print(f"[{a.mode}] {bench:16s} {n:18s} seed {seed}: {r['verdict']} hpwl {r.get('hpwl', '')} "
                          f"attempts {r.get('attempts', '')} sa {r.get('sa_seconds', '')}s", flush=True)
    f.close()
    summarize(rec_path, out, [n for n, _, _ in vers])


def summarize(rec_path, out, names):
    rows = [r for r in csv.DictReader(open(rec_path)) if r['verdict'] == 'PASS']
    by = {}
    for r in rows:
        by.setdefault((r['group'], r['bench']), {}).setdefault(r['version'], []).append(r)
    base = names[0]
    lines = [f'| group | bench | ' + ' | '.join(f'{n} mean [min, max]' for n in names) + ' | ' +
             ' | '.join(f'{n} / {base}' for n in names[1:]) + ' | ' + ' | '.join(f'{n} attempts / s' for n in names) + ' |',
             '|---|---|' + '---:|' * (2 * len(names) - 1 + len(names))]
    ratios = {n: {} for n in names[1:]}
    for (g, b), v in sorted(by.items()):
        if any(n not in v for n in names):
            continue
        cells, means = [], {}
        for n in names:
            h = [float(x['hpwl']) for x in v[n]]
            means[n] = st.mean(h)
            cells.append(f'{st.mean(h):,.1f} [{min(h):,.1f}, {max(h):,.1f}]')
        rs = []
        for n in names[1:]:
            ratio = means[n] / means[base] if means[base] else 1.0
            ratios[n].setdefault(g, []).append(ratio)
            rs.append(f'{ratio:.4f}')
        att = []
        for n in names:
            aa = [float(x['attempts'] or 0) for x in v[n]]
            ss = [float(x['sa_seconds'] or 0) for x in v[n]]
            att.append(f'{st.mean(aa):,.0f} / {st.mean(ss):.1f}')
        lines.append(f'| {g} | {b} | ' + ' | '.join(cells) + ' | ' + ' | '.join(rs) + ' | ' + ' | '.join(att) + ' |')
    lines.append('')
    for n in names[1:]:
        for g, xs in ratios[n].items():
            gm = math.exp(st.mean(math.log(x) for x in xs))
            lines.append(f'- {n} / {base}, {g}: geometric mean of the per-bench mean ratio = **{gm:.4f}** ({len(xs)} benches, '
                         f'{sum(x < 1 for x in xs)} better, {sum(x > 1 for x in xs)} worse)')
    (out / 'summary.md').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines[-2 * len(names):]))


if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[1] == '--summarize':
        summarize(Path(sys.argv[2]) / 'record.csv', Path(sys.argv[2]), sys.argv[3:])
    else:
        main()
