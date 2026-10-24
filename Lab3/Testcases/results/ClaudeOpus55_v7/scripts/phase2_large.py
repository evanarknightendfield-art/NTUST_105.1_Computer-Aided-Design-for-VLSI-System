#!/usr/bin/env python3
"""phase2_large.py TAG [CFG ...] [--h08] — full default dp runs (run_ts -v) of the current v7 source and CodexParallelChain on
L1 and L2 (and h08 with --h08), paired in the same queue. CFG: all (no restriction), P4 / P2 (taskset 4 / 2 P-cores,
automatic threads), P4t4 / P2t2 (taskset + --threads 4 / 2), R4 / R2 = a real 4 / 2-core machine, where
hardware_concurrency() is 4 / 2: CPC's automatic choice is then 4 / 2 threads (--threads 4 / 2), v7's automatic choice is
8 (no --threads); both under taskset 4 / 2 P-cores. Default CFGs: all P2t2.
Source copied to phase2/large/TAG/src and built there (hash in protocol.json). Writes summary.md, summary.json."""
import hashlib, json, re, shutil, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, V5S, build, note  # noqa: E402

tag = sys.argv[1]
args = sys.argv[2:]
h08 = '--h08' in args
cfgs = [a for a in args if a != '--h08'] or ['all', 'P2t2']
CPUS = {'all': None, 'P4': '0,2,4,6', 'P2': '0,2', 'P4t4': '0,2,4,6', 'P2t2': '0,2', 'R4': '0,2,4,6', 'R2': '0,2'}
THR = {'P4t4': 4, 'P2t2': 2}
REAL = {'R4': 4, 'R2': 2}   # CPC only
BENCH = ['Testcases/large/L1/L1', 'Testcases/large/L2/L2'] + (['Testcases/holdout/h08/h08'] if h08 else [])
NAMES = ['L1', 'L2'] + (['h08'] if h08 else [])
d = R / 'phase2' / 'large' / tag
if not (d / 'src').exists():
    d.mkdir(parents=True, exist_ok=True)
    shutil.copytree(LAB / 'AI_Solutions' / R.name / 'src', d / 'src')
    shutil.copy(LAB / 'AI_Solutions' / R.name / 'CMakeLists.txt', d / 'CMakeLists.txt')
(d / 'protocol.json').write_text(json.dumps(dict(tag=tag, cfgs=cfgs, src_sha256=hashlib.sha256(
    b''.join(p.read_bytes() for p in sorted((d / 'src').iterdir()))).hexdigest()), indent=1) + '\n')
exes = {'v7': build('v7', d / 'build', src=d), 'cpc': R / 'bin' / 'CodexParallelChain' / 'dp'}
q = Queue(d)


def wrap(exe, cpus, name):
    if cpus is None:
        return exe
    w = d / f'wrap_{name}'
    w.write_text(f'#!/bin/sh\nexec taskset -c {cpus} {exe} "$@"\n')
    w.chmod(0o755)
    return w


for cfg in cfgs:
    for ver, exe in exes.items():
        thr = THR.get(cfg) or (REAL.get(cfg) if ver == 'cpc' else None)
        args = f'--threads {thr}' if thr else ''
        q.run(f'{ver}_{cfg}', [PY, V5S / 'run_ts.py', '--bin', wrap(exe, CPUS[cfg], f'{ver}_{cfg}'), '--out', d / f'{ver}_{cfg}',
                               '--timeout', 900, f'--dp-args={args}', *BENCH])


def extra(run):
    for ln in open(run / 'stderr_ts.tsv'):
        m = re.search(r'\[extra\] budget ([\d.]+) used ([\d.]+)', ln)
        if m:
            return float(m.group(1)), float(m.group(2))
    return None, None


import csv  # noqa: E402
rows, out = [], {}
for cfg in cfgs:
    for b in NAMES:
        r = {v: {x['bench']: x for x in csv.DictReader(open(d / f'{v}_{cfg}' / 'record.csv'))}[b] for v in exes}
        for v in exes:
            bud, used = extra(d / f'{v}_{cfg}' / b)
            x = r[v]
            out[f'{cfg}/{b}/{v}'] = dict(seconds=float(x['seconds']), disp=float(x['disp']), max=float(x['max']),
                                         hpwl=float(x['hpwl_sa']), stage1=float(x['t_stage1']), extra_used=used, extra_budget=bud,
                                         status=x['status'])
        a, c = out[f'{cfg}/{b}/v7'], out[f'{cfg}/{b}/cpc']
        rows.append(f"| {cfg} | {b} | {a['status']} | {a['seconds']:.1f} / {c['seconds']:.1f} | {a['stage1']:.1f} / {c['stage1']:.1f} | "
                    f"{a['extra_used']:.2f} / {c['extra_used']:.2f} | {a['disp'] / c['disp'] - 1:+.3%} | {a['max'] / c['max'] - 1:+.3%} | "
                    f"{a['hpwl'] / c['hpwl'] - 1:+.3%} |")
(d / 'summary.json').write_text(json.dumps(out, indent=1) + '\n')
(d / 'summary.md').write_text(f'# {tag}：v7 ÷ 同輪 CPC（L1 / L2，預設 dp）\n\n| cfg | bench | v7 status | 秒 v7 / CPC | Stage1 v7 / CPC | '
                              '額外 v7 / CPC（預算 6） | 總位移 | 最大 | HPWL |\n|---|---|---|---:|---:|---:|---:|---:|---:|\n' + '\n'.join(rows) + '\n')
note(f'phase2/large/{tag} 完成：見 phase2/large/{tag}/summary.md')
