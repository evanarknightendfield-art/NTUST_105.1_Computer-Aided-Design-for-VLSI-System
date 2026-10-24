#!/usr/bin/env python3
"""phase1_queue.py — v7 phase 1 (diagnosis of CodexParallelChain), serial and checkpointed (v6q.Queue).

Every timed step holds results/ClaudeOpus55_v7/timing.lock. Completion marker: phase1/ALL_DONE.
  cores/<cfg>       run_ts (default dp, -v) of CPC on h08, L1, L2: all CPUs / taskset 4 or 2 P-cores,
                    auto threads (= 8, oversubscribed) or --threads = cores (a real small machine)
  lg_<v>, edge_<v>  local_grade testcases+holdout / edge of CPC, v6, ChainDP;  lg_<v>_L1 / _L2
  ts_CPC            run_ts --no-sa on the 27 cases (Stage 1 phase times; also the common SA starts)
  dpcurve           v7 instrumented build (= CPC + measurement options), --no-sa --trace-dp, 27 cases
  chain/<cfg>       --no-sa --cap-time 0 with --chain-split / --chain-rounds variants, 11 + e08/e16
  maxvar/<run>      CPC --no-sa, h01 h04 h06 h07 ibm05, 5 repeats + 2 on 4 P-cores (same 8 threads, slower)
  band/             sa_bench2 (CPC sources) fixed 20M attempts, auto threads vs 1 thread, 2 seeds
  split/            sa_bench2 (CPC sources) SA / polish allocations from the same start, 2 seeds
"""
import os, shutil, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, V5S, env_with_pinned_cmake, build, note  # noqa: E402

CPC, V6, CH = 'CodexParallelChain', 'ClaudeOpus55_v6', 'CodexChainDP'
P = R / 'phase1'
P.mkdir(parents=True, exist_ok=True)
env = env_with_pinned_cmake()
RUN_TS = V5S / 'run_ts.py'
T, H, E, LG = 'Testcases/testcases', 'Testcases/holdout', 'Testcases/edge', 'Testcases/large'

# ---- binaries (compiled on the E-cores, before taking the lock)
bins = {CPC: build(CPC, R / 'bin' / CPC)}
instr = P / 'src_instr'
if not instr.exists():
    instr.mkdir()
    shutil.copytree(LAB / 'AI_Solutions' / R.name / 'src', instr / 'src')
    shutil.copy(LAB / 'AI_Solutions' / R.name / 'CMakeLists.txt', instr / 'CMakeLists.txt')
bins['instr'] = build('v7instr', R / 'bin' / 'v7instr', src=instr)
sab = R / 'bin' / 'sa_bench2_CPC'
if not sab.exists():
    cpc_src = LAB / 'AI_Solutions' / CPC / 'src'
    srcs = [str(p) for p in sorted(cpc_src.glob('*.cpp')) if p.name != 'main.cpp']
    subprocess.run(['taskset', '-c', '16-19', 'g++', '-std=c++17', '-O2', '-pthread', f'-I{cpc_src}',
                    str(LAB / 'AI_Solutions' / R.name / 'tests/sa_bench2.cpp'), *srcs, '-o', str(sab)], check=True)

wrap = P / 'wrap'
wrap.mkdir(exist_ok=True)
CORES = {'P4': '0,2,4,6', 'P2': '0,2'}


def wrapper(cpus, exe, name):
    w = wrap / name
    w.write_text(f'#!/bin/sh\nexec taskset -c {cpus} {exe} "$@"\n')
    w.chmod(0o755)
    return w


q = Queue(P)

# ---- 1. TA-machine risk first: fewer cores
for cfg, cpus, thr in [('all', None, None), ('P4', CORES['P4'], None), ('P2', CORES['P2'], None),
                       ('P4t4', CORES['P4'], 4), ('P2t2', CORES['P2'], 2)]:
    exe = bins[CPC] if cpus is None else wrapper(cpus, bins[CPC], f'cpc_{cfg}')
    args = f'--threads {thr}' if thr else ''
    q.run(f'cores_{cfg}', [PY, RUN_TS, '--bin', exe, '--out', P / 'cores' / cfg, '--timeout', 900, f'--dp-args={args}',
                           f'{H}/h08/h08', f'{LG}/L1/L1', f'{LG}/L2/L2'])

# ---- 2. paired local_grade of CPC, v6, ChainDP


def grade(name, ver, dirs, timeout=600):
    q.run(name, [PY, 'Grader/tools/local_grade.py', f'AI_Solutions/{ver}', ver, *dirs, '--work', P / name,
                 '--timeout', timeout, '--dp-args=-v'], env=env)


for v in [CPC, V6, CH]:
    grade(f'lg_{v}', v, [T, H])
    grade(f'edge_{v}', v, [E])
for large, tmo in [('L1', 240), ('L2', 600)]:
    only = P / f'only_{large}'
    only.mkdir(exist_ok=True)
    if not (only / large).exists():
        (only / large).symlink_to(LAB / LG / large, target_is_directory=True)
    for v in [CPC, V6, CH]:
        grade(f'lg_{v}_{large}', v, [only], tmo)
q.run('ts_CPC', [PY, RUN_TS, '--bin', bins[CPC], '--out', P / 'ts_CPC', '--dp-args=--no-sa', T, H, E])

# ---- 3. window DP convergence curve (instrumented build, same algorithm)
q.run('dpcurve', [PY, RUN_TS, '--bin', bins['instr'], '--out', P / 'dpcurve', '--dp-args=--no-sa --trace-dp', T, H, E])

# ---- 4. chain-phase time allocation (training 11 + e08/e16 only observed)
CHAIN_CFG = [('def', ''), ('E0', '--chain-split 0.3 0 0.5'), ('E10', '--chain-split 0.3 0.1 0.5'),
             ('E25', '--chain-split 0.3 0.25 0.5'), ('T60E25', '--chain-split 0.6 0.25 0.6'),
             ('E25r4', '--chain-split 0.3 0.25 0.5 --chain-rounds 4'), ('x2', '--chain-time 3')]
for cfg, args in CHAIN_CFG:
    q.run(f'chain_{cfg}', [PY, RUN_TS, '--bin', bins['instr'], '--out', P / 'chain' / cfg,
                           f'--dp-args=--no-sa --cap-time 0 {args}', T, H, f'{E}/e08_fragmented/e08_fragmented',
                           f'{E}/e16_rowgaps/e16_rowgaps'])

# ---- 5. maximum displacement variation: same binary, 5 repeats; 2 more on 4 P-cores with the same
#         8 threads (identical output there would mean no wall-clock dependence)
MV = [f'{H}/h01/h01', f'{H}/h04/h04', f'{H}/h06/h06', f'{H}/h07/h07', f'{T}/ibm05/ibm05']
for k in range(1, 6):
    q.run(f'maxvar_r{k}', [PY, RUN_TS, '--bin', bins[CPC], '--out', P / 'maxvar' / f'r{k}', '--dp-args=--no-sa', *MV])
for k in range(1, 3):
    q.run(f'maxvar_p4_r{k}', [PY, RUN_TS, '--bin', wrapper(CORES['P4'], bins[CPC], 'cpc_P4'), '--out',
                              P / 'maxvar' / f'p4_r{k}', '--dp-args=--no-sa', *MV])

# ---- 6. HPWL: common starts = CPC's Stage 1 (ts_CPC)


def start(bench):
    d = P / 'ts_CPC' / bench
    c = P / 'starts' / bench
    c.mkdir(parents=True, exist_ok=True)
    for ext in ['.aux', '.nodes', '.nets', '.wts', '.scl']:
        if not (c / f'output{ext}').exists():
            (c / f'output{ext}').symlink_to((d / f'output{ext}').resolve())
    if not (c / 'output.pl').exists():
        (c / 'output.pl').symlink_to((d / 'output_legal.pl').resolve())
    return c / 'output'


ORIG = {'ibm01': f'{T}/ibm01/ibm01', 'ibm05': f'{T}/ibm05/ibm05', 'h06': f'{H}/h06/h06', 'h07': f'{H}/h07/h07',
        'h08': f'{H}/h08/h08', 'e15_realistic': f'{E}/e15_realistic/e15_realistic'}
(P / 'band').mkdir(exist_ok=True)
for b in ['ibm01', 'ibm05', 'h06', 'h07', 'h08']:
    for seed in [1, 2]:
        for thr in [0, 1]:
            n = f'band_{b}_s{seed}_t{thr}'
            q.run(n, [sab, LAB / ORIG[b], start(b), P / 'band' / n, '--seed', seed, '--moves', 20e6, '--time', 1e5,
                      '--polish', 0, '--threads', thr], quiet=True)
(P / 'split').mkdir(exist_ok=True)
SPLIT = [('sa5p17', 5, 17, 0), ('sa10p12', 10, 12, 0), ('sa20p2', 20, 2, 0), ('sa60p30', 60, 30, 100), ('sa30p60', 30, 60, 0)]
for b in ['h06', 'h07', 'h08', 'e15_realistic']:
    for seed in [1, 2]:
        for cfg, sa, pol, starved in SPLIT:
            n = f'split_{b}_s{seed}_{cfg}'
            q.run(n, [sab, LAB / ORIG[b], start(b), P / 'split' / n, '--seed', seed, '--time', sa, '--polish', pol,
                      '--starved', starved], quiet=True)
note('phase1 全部完成（phase1/ALL_DONE）；下一步：分析、寫 diagnosis.md')
(P / 'ALL_DONE').write_text('done\n')
