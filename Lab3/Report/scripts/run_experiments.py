#!/usr/bin/env python3
"""Run the report experiments and store raw results as JSON.

usage: python3 run_experiments.py [--dp PATH] [--out DIR] [--jobs N] [--only main,holdout,...]

  --dp    program to run (default: Lab3/M11502102/build/dp, your submission)
  --out   report root (tables / figures are made there; default: Lab3/Report)
  --results  where the raw results go (default: Lab3/Testcases/results/<name of OUT, lower case>/,
             e.g. Testcases/results/report/ and Testcases/results/reference_report/)
  --only  experiments, comma separated (default: main,holdout)
            main     toy / ibm01 / ibm05 with default options, SA trace; one at a time (clean runtimes)
            holdout  Testcases/holdout/h01..h08 with default options
            stage1   Stage 1 variants  (needs the reference's --legalizer/--own-cost/--no-refine/--no-evict)
            sa       SA variants       (needs --p0/--target/--clamp-target/--win0/--minwin)
            effort   moves per cell    (needs --effort)
            seeds    5 seeds           (needs --seed)

Reference solution (all experiments, into AI_Workspace/reference_report/):
  python3 run_experiments.py --dp ../../AI_Solutions/ClaudeOpus55/build/dp \\
      --out ../../AI_Workspace/reference_report --only main,holdout,stage1,sa,effort,seeds
"""
import argparse
import glob
import json
import os
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
LAB3 = os.path.abspath(os.path.join(HERE, '..', '..'))
TESTCASES = os.path.join(LAB3, 'Testcases', 'testcases')
HOLDOUT = os.path.join(LAB3, 'Testcases', 'holdout')
BENCHES = ['toy', 'ibm01', 'ibm05']


def results_dir(out):
    """Lab3 convention: every run output lives in Testcases/results/, one folder per report."""
    return os.path.join(LAB3, 'Testcases', 'results', os.path.basename(os.path.abspath(out)).lower())

STAGE1 = {   # name -> extra args (all with --no-sa)
    'tetris': ['--legalizer', 'tetris', '--no-refine'],
    'abacus_own': ['--own-cost', '--no-refine'],
    'abacus': ['--no-refine'],
    'abacus+refine': ['--no-evict'],
    'abacus+refine+evict': [],
}
SA = {       # name -> extra args
    'final': [],
    'greedy': ['--p0', '1e-12'],
    'p0=0.05': ['--p0', '0.05'],
    'p0=0.3': ['--p0', '0.3'],
    'p0=0.95': ['--p0', '0.95'],
    'no_target': ['--target', '0'],
    'all_target': ['--target', '1'],
    'clamp_target': ['--clamp-target'],
    'no_range_limit': ['--win0', '1', '--minwin', '1e12', '1e12'],
}
EFFORTS = [125, 250, 500, 1000, 2000, 4000]
SEEDS = [1, 2, 3, 4, 5]


def cleanup(prefix):
    """dp copies the whole benchmark next to its output; keep only what the report uses."""
    exp = os.path.basename(os.path.dirname(prefix))
    drop = ['.aux', '.nodes', '.nets', '.wts', '.scl']
    if exp != 'main':
        drop.append('.pl')            # SA result: only main runs are drawn
    if exp not in ('main', 'stage1'):
        drop.append('_legal.pl')      # Stage 1 result: same in every SA run
    for ext in drop:
        if os.path.exists(prefix + ext):
            os.remove(prefix + ext)


def run(dp, bench_dir, bench, out_dir, tag, args):
    os.makedirs(out_dir, exist_ok=True)
    prefix = os.path.join(out_dir, f'{bench}_{tag}')
    cmd = [dp, os.path.join(bench_dir, bench, bench), prefix] + args
    t = time.time()
    p = subprocess.run(cmd, capture_output=True, text=True)
    sec = time.time() - t
    vals = {}
    for line in p.stdout.splitlines():
        k, _, v = line.rpartition(':')
        try:
            vals[k.strip()] = float(v)
        except ValueError:
            pass
    rec = {'bench': bench, 'tag': tag, 'args': args, 'seconds': sec, 'returncode': p.returncode,
           'total_disp': vals.get('Total displacement (legal)'),
           'max_disp': vals.get('Maximum displacement (legal)'),
           'hpwl_legal': vals.get('Total HPWL (legal)'),
           'hpwl_sa': vals.get('Total HPWL (SA)'),
           'stderr': p.stderr[-2000:]}
    with open(prefix + '.json', 'w') as f:
        json.dump(rec, f, indent=1)
    cleanup(prefix)
    print(f"{bench:6s} {tag:22s} rc={p.returncode} disp={rec['total_disp']} max={rec['max_disp']} "
          f"legal={rec['hpwl_legal']} sa={rec['hpwl_sa']} {sec:.1f}s", flush=True)
    return rec


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dp', default=os.path.join(LAB3, 'M11502102', 'build', 'dp'))
    ap.add_argument('--out', default=os.path.join(LAB3, 'Report'))
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--only', default='main,holdout')
    ap.add_argument('--results')
    a = ap.parse_args()
    only = a.only.split(',')
    dp = os.path.abspath(a.dp)
    results = os.path.abspath(a.results or results_dir(a.out))
    if not os.path.isfile(dp):
        raise SystemExit(f'{dp} not found: build it first')
    if not os.path.isdir(TESTCASES):
        raise SystemExit('no testcases: run Testcases/make_testcases.sh first')

    if 'main' in only:   # sequential: clean runtimes
        d = os.path.join(results, 'main')
        for b in BENCHES:
            run(dp, TESTCASES, b, d, 'final', ['--trace', os.path.join(d, f'{b}_trace.csv')])

    jobs = []
    if 'holdout' in only:
        for aux in sorted(glob.glob(os.path.join(HOLDOUT, '*', '*.aux'))):
            b = os.path.basename(aux)[:-4]
            jobs.append((HOLDOUT, b, os.path.join(results, 'holdout'), 'final', []))
    if 'stage1' in only:
        for b in BENCHES:
            for tag, args in STAGE1.items():
                jobs.append((TESTCASES, b, os.path.join(results, 'stage1'), tag, args + ['--no-sa']))
    if 'sa' in only:
        for b in ['ibm01', 'ibm05']:
            for tag, args in SA.items():
                d = os.path.join(results, 'sa')
                jobs.append((TESTCASES, b, d, tag,
                             args + ['--time', '1000', '--trace', os.path.join(d, f'{b}_{tag}_trace.csv')]))
    if 'effort' in only:
        for b in ['ibm01', 'ibm05']:
            for e in EFFORTS:
                jobs.append((TESTCASES, b, os.path.join(results, 'effort'), f'effort{e}',
                             ['--effort', str(e), '--time', '1000']))
    if 'seeds' in only:
        for b in ['ibm01', 'ibm05']:
            for s in SEEDS:
                jobs.append((TESTCASES, b, os.path.join(results, 'seeds'), f'seed{s}',
                             ['--seed', str(s), '--time', '1000']))
    # longest first keeps the pool busy
    jobs.sort(key=lambda j: 0 if j[1] in ('ibm05', 'h08') else 1)
    with ThreadPoolExecutor(a.jobs) as ex:
        list(ex.map(lambda j: run(dp, *j), jobs))


if __name__ == '__main__':
    main()
