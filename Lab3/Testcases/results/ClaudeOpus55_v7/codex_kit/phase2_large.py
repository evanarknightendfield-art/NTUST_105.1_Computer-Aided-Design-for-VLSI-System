#!/usr/bin/env python3
"""phase2_large.py TAG [CFG ...] [--mid] [--h08] [--seeds N] — full default dp runs (run_ts -v) of the current source and
ClaudeOpus55_v7, paired in the same queue, on L1, L2 (+ M1-M3 with --mid, + h08 with --h08).

CFG: all (no restriction), P4 / P2 (taskset 4 / 2 P-cores: CPU 0,2,4,6 / 0,2). v7 and later count the CPUs of their
affinity mask (availableCpus), so taskset behaves like a real 4 / 2-core machine for both. Default CFGs: all P2.
--seeds N runs SA seeds 1..N (default 1). Source copied to phase2/large/TAG/src (hash in protocol.json).
Writes summary.md / summary.json (new ÷ v7 per cfg, bench, seed)."""
import csv, hashlib, json, re, shutil, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, V5S, build, note  # noqa: E402

BASE = 'ClaudeOpus55_v7'
tag, args = sys.argv[1], sys.argv[2:]
seeds = 1
if '--seeds' in args:
    i = args.index('--seeds')
    seeds = int(args[i + 1])
    del args[i:i + 2]
mid, h08 = '--mid' in args, '--h08' in args
cfgs = [a for a in args if not a.startswith('--')] or ['all', 'P2']
CPUS = {'all': None, 'P4': '0,2,4,6', 'P2': '0,2'}
BENCH = ['L1', 'L2'] + (['M1', 'M2', 'M3'] if mid else [])
PATHS = [f'Testcases/large/{b}/{b}' for b in BENCH] + (['Testcases/holdout/h08/h08'] if h08 else [])
NAMES = BENCH + (['h08'] if h08 else [])
d = R / 'phase2' / 'large' / tag
if not (d / 'src').exists():
    d.mkdir(parents=True, exist_ok=True)
    shutil.copytree(LAB / 'AI_Solutions' / R.name / 'src', d / 'src')
    shutil.copy(LAB / 'AI_Solutions' / R.name / 'CMakeLists.txt', d / 'CMakeLists.txt')
(d / 'protocol.json').write_text(json.dumps(dict(tag=tag, cfgs=cfgs, benches=NAMES, seeds=seeds, src_sha256=hashlib.sha256(
    b''.join(p.read_bytes() for p in sorted((d / 'src').iterdir()))).hexdigest()), indent=1) + '\n')
base = R / 'bin' / BASE / 'dp'
if not base.exists():
    base = build(BASE, R / 'bin' / BASE, src=LAB / 'AI_Solutions' / BASE)
exes = {'new': build('new', d / 'build', src=d), 'v7': base}
q = Queue(d)


def wrap(exe, cpus, name):
    if cpus is None:
        return exe
    w = d / f'wrap_{name}'
    w.write_text(f'#!/bin/sh\nexec taskset -c {cpus} {exe} "$@"\n')
    w.chmod(0o755)
    return w


for cfg in cfgs:
    for s in range(1, seeds + 1):
        for ver, exe in exes.items():   # interleaved: new, v7, new, v7, ...
            q.run(f'{ver}_{cfg}_s{s}', [PY, V5S / 'run_ts.py', '--bin', wrap(exe, CPUS[cfg], f'{ver}_{cfg}'), '--out',
                                       d / f'{ver}_{cfg}_s{s}', '--timeout', 900, f'--dp-args=--seed {s}', *PATHS])


def extra(run):
    p = run / 'stderr_ts.tsv'
    for ln in open(p) if p.exists() else []:
        m = re.search(r'\[extra\] budget ([\d.]+) used ([\d.]+)', ln)
        if m:
            return float(m.group(1)), float(m.group(2))
    return None, None


rows, out = [], {}
for cfg in cfgs:
    for s in range(1, seeds + 1):
        for b in NAMES:
            r = {v: {x['bench']: x for x in csv.DictReader(open(d / f'{v}_{cfg}_s{s}' / 'record.csv'))}[b] for v in exes}
            for v in exes:
                bud, used = extra(d / f'{v}_{cfg}_s{s}' / b)
                x = r[v]
                out[f'{cfg}/s{s}/{b}/{v}'] = dict(seconds=float(x['seconds']), disp=float(x['disp']), max=float(x['max']),
                                                  hpwl=float(x['hpwl_sa']), stage1=float(x['t_stage1'] or 0),
                                                  sa_attempts=float(x['sa_attempts'] or 0), extra_used=used,
                                                  extra_budget=bud, status=x['status'])
            a, c = out[f'{cfg}/s{s}/{b}/new'], out[f'{cfg}/s{s}/{b}/v7']
            rows.append(f"| {cfg} | {s} | {b} | {a['status']} / {c['status']} | {a['seconds']:.1f} / {c['seconds']:.1f} | "
                        f"{a['stage1']:.1f} / {c['stage1']:.1f} | {a['extra_used'] or 0:.2f} / {c['extra_used'] or 0:.2f} | "
                        f"{a['sa_attempts'] / 1e6:.1f}M / {c['sa_attempts'] / 1e6:.1f}M | {a['disp'] / c['disp'] - 1:+.3%} | "
                        f"{a['max'] / c['max'] - 1:+.3%} | {a['hpwl'] / c['hpwl'] - 1:+.3%} |")
(d / 'summary.json').write_text(json.dumps(out, indent=1) + '\n')
(d / 'summary.md').write_text(
    f'# {tag}：新版 ÷ 同輪 v7（預設 dp）\n\n| cfg | seed | bench | status 新 / v7 | 秒 新 / v7 | Stage1 新 / v7 | 額外 新 / v7（預算 6） | '
    'SA 次數 新 / v7 | 總位移 | 最大 | HPWL |\n|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|\n' + '\n'.join(rows) + '\n')
note(f'phase2/large/{tag} 完成：見 phase2/large/{tag}/summary.md')
