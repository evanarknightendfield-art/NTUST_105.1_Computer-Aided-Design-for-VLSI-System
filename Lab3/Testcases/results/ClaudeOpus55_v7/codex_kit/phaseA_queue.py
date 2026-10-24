#!/usr/bin/env python3
"""phaseA_queue.py — the next version's phase A measurements (no Codex needed). Serial, checkpointed (v6q.Queue,
timing.lock); marker phaseA/ALL_DONE, then phaseA_report.py writes phaseA/summary.md (the one file phase A2 reads).

The baseline is ClaudeOpus55_v7 (its paired final run of 10-24 is in Testcases/results/ClaudeOpus55_v7/final/ and is
not repeated here: diagnosis may cite it; acceptance pairs again in the new version's final run).
  mid      M1-M3 (70k / 130k / 180k cells, generated into Testcases/large/ if missing): v7 full default run_ts,
           all CPUs and taskset 2 P-cores (CPU 0,2) — the 60k-210k range the training set does not cover
  speed    SA attempts per second, 1 thread, fixed 10M attempts, sa_bench2 built from v7 (common starts = v7 Stage 1)
  prof     gprof flat profile of the same SA (h08, ibm01): where the SA time goes
  noise    the default HPWL protocol of v7 against v7 itself (two Stage 1 runs, same source): the noise floor of
           every later "not worse than v7" HPWL claim (3 seeds, holdout + edge)
"""
import shutil, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, V5S, build, note  # noqa: E402

BASE = 'ClaudeOpus55_v7'
P = R / 'phaseA'
P.mkdir(parents=True, exist_ok=True)
T, H, E = 'Testcases/testcases', 'Testcases/holdout', 'Testcases/edge'
MID = {'M1': '--cells 70000 --util 0.75 --macros 8 --pads 600 --hotspots 3 --split 0.1 --geom ibm --seed 31',
       'M2': '--cells 130000 --util 0.85 --macros 10 --pads 800 --hotspots 4 --split 0.1 --geom unit --seed 32',
       'M3': '--cells 180000 --util 0.65 --macros 16 --pads 1000 --hotspots 5 --split 0.1 --geom micron --seed 33'}

exe = build(BASE, R / 'bin' / BASE, src=LAB / 'AI_Solutions' / BASE)
src = LAB / 'AI_Solutions' / BASE / 'src'
cpp = [str(p) for p in sorted(src.glob('*.cpp')) if p.name != 'main.cpp']
drv = str(LAB / 'AI_Solutions' / BASE / 'tests' / 'sa_bench2.cpp')
sab, sab_pg = R / 'bin' / 'sa_bench2_v7', R / 'bin' / 'sa_bench2_v7_pg'
for out, extra in [(sab, []), (sab_pg, ['-pg', '-fno-omit-frame-pointer', '-fno-inline-functions'])]:
    if not out.exists():
        subprocess.run(['taskset', '-c', '16-19', 'g++', '-std=c++17', '-O2', '-pthread', *extra, f'-I{src}', drv, *cpp,
                        '-o', str(out)], check=True)
q = Queue(P)

# ---- mid-size large designs
for m, args in MID.items():
    if not (LAB / 'Testcases/large' / m / f'{m}.aux').exists():
        q.run(f'gen_{m}', ['taskset', '-c', '16-19', PY, 'Grader/tools/gen_bench.py', m, 'Testcases/large', *args.split()])
w2 = P / 'wrap_P2'
w2.write_text(f'#!/bin/sh\nexec taskset -c 0,2 {exe} "$@"\n')
w2.chmod(0o755)
for cfg, b in [('all', exe), ('P2', w2)]:
    q.run(f'mid_{cfg}', [PY, V5S / 'run_ts.py', '--bin', b, '--out', P / f'mid_{cfg}', '--timeout', 900,
                         *[f'Testcases/large/{m}/{m}' for m in MID]])

# ---- v7 Stage 1 twice (noise control) + common SA starts
for k in ['a', 'b']:
    q.run(f'ts_{k}', [PY, V5S / 'run_ts.py', '--bin', exe, '--out', P / f'ts_{k}', '--dp-args=--no-sa', T, H, E])


def start(bench):
    """v7's Stage 1 (run a) as a Bookshelf prefix for sa_bench2"""
    d, c = P / 'ts_a' / bench, P / 'starts' / bench
    c.mkdir(parents=True, exist_ok=True)
    for ext in ['.aux', '.nodes', '.nets', '.wts', '.scl']:
        if not (c / f'output{ext}').exists():
            (c / f'output{ext}').symlink_to((d / f'output{ext}').resolve())
    if not (c / 'output.pl').exists():
        (c / 'output.pl').symlink_to((d / 'output_legal.pl').resolve())
    return c / 'output'


ORIG = {'ibm01': f'{T}/ibm01/ibm01', 'ibm05': f'{T}/ibm05/ibm05', 'h06': f'{H}/h06/h06', 'h07': f'{H}/h07/h07', 'h08': f'{H}/h08/h08'}
(P / 'speed').mkdir(exist_ok=True)
for b, orig in ORIG.items():
    q.run(f'speed_{b}', [sab, LAB / orig, start(b), P / 'speed' / b, '--seed', 1, '--moves', 10e6, '--time', 1e5,
                         '--polish', 0, '--threads', 1], quiet=True)
(P / 'prof').mkdir(exist_ok=True)
for b in ['h08', 'ibm01']:
    d = P / 'prof' / b
    d.mkdir(exist_ok=True)
    q.run(f'prof_{b}', [sab_pg, LAB / ORIG[b], start(b), d / 'out', '--seed', 1, '--moves', 10e6, '--time', 1e5,
                        '--polish', 0, '--threads', 1], cwd=d, quiet=True)
    if (d / 'gmon.out').exists() and not (d / 'flat.txt').exists():
        with open(d / 'flat.txt', 'w') as f:
            subprocess.run(['gprof', '-b', '-p', str(sab_pg), str(d / 'gmon.out')], stdout=f, stderr=subprocess.STDOUT)

# ---- default-protocol noise floor: v7 vs v7
q.run('noise_default', [PY, R / 'scripts' / 'hpwl_compare.py', 'default', P / 'noise_default', '--groups', 'holdout', 'edge',
                        '--v', BASE, src, P / 'ts_a', '--v', BASE + 'b', src, P / 'ts_b'])
note('phaseA 量測完成（phaseA/ALL_DONE）；summary：phaseA/summary.md')
(P / 'ALL_DONE').write_text('done\n')
subprocess.run([PY, R / 'scripts' / 'phaseA_report.py'], check=False)
