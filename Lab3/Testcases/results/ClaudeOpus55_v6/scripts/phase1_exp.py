#!/usr/bin/env python3
"""phase1_exp.py — Phase 1 controlled experiments (after phase1_runs.py; waits on timing.lock).

  l2_sched/   L2 Stage 2 from two common legal starts (this session's CodexCapDP and v5 L2 runs),
              SA60->P30 vs SA30->P60, seeds 1-3, automatic threads (sa_bench, CodexCapDP's SA source)
  hpwl_default_holdout/  hpwl_compare.py default: v5 vs CodexCapDP from this session's Stage 1 (ts r1), 3 seeds
  chain_sweep/  dp --no-sa --cap-time 0 --extra-time 40 --chain-time T: total displacement after the chains
              for T = 0.75 / 1.5 / 3 / 6 (holdout h02-h08; e08 / e16 diagnosis only, not tuning)
  e07_repeat/  CodexCapDP Stage 1 on e07 x3 (-v): the extra-step overrun
"""
import subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, V5S, note  # noqa: E402

CAPS = R.parent / 'CodexCapDP' / 'scripts'
SA_BENCH = LAB / 'AI_Solutions/CodexChainDP/tests/sa_bench.cpp'
cap_bin = R / 'bin/CodexCapDP/dp'
q = Queue(R / 'phase1' / 'exp')

# ---- L2 schedule from common starts
sab = R / 'bin' / 'sa_bench_CodexCapDP'
if not sab.exists():
    src = LAB / 'AI_Solutions/CodexCapDP/src'
    subprocess.run(['taskset', '-c', '16-19', 'g++', '-std=c++17', '-O3', '-DNDEBUG', '-pthread', f'-I{src}', SA_BENCH,
                    *[src / f'{n}.cpp' for n in ['annealer', 'optimizer', 'window', 'placedb', 'global']], '-o', sab],
                   check=True)
L2 = LAB / 'Testcases/large/L2/L2'
for start in ['CodexCapDP', 'ClaudeOpus55_v5']:
    legal = R / f'phase1/lg/{start}_L2/results/{start}/L2/output_legal.pl'
    sd = R / 'phase1/exp/l2_sched' / f'start_{start}'
    sd.mkdir(parents=True, exist_ok=True)
    pre = sd / 'L2'
    for ext in ['.aux', '.nodes', '.nets', '.wts', '.scl']:
        if not Path(f'{pre}{ext}').exists():
            Path(f'{pre}{ext}').symlink_to(Path(f'{L2}{ext}'))
    if not Path(f'{pre}.pl').exists():
        Path(f'{pre}.pl').symlink_to(legal)
    for seed in [1, 2, 3]:
        for sa, pol in [(60, 30), (30, 60)]:
            name = f'l2_{start}_sa{sa}_p{pol}_s{seed}'
            q.run(name, ['bash', '-c', f'"$0" "$@" > {sd}/{name}.csv', sab, L2, pre, sd / name, seed, 'default', sa, pol, 0])
            Path(f'{sd / name}.trace.csv').unlink(missing_ok=True)

# ---- paired default-parameter HPWL on holdout: v5 vs CodexCapDP
ts = R / 'phase1/ts'
q.run('hpwl_default_holdout', [PY, CAPS / 'hpwl_compare.py', 'default', R / 'phase1/exp/hpwl_default_holdout', '--groups', 'holdout',
                               '--v', 'ClaudeOpus55_v5', LAB / 'AI_Solutions/ClaudeOpus55_v5/src', ts / 'ClaudeOpus55_v5_s1_r1',
                               '--v', 'CodexCapDP', LAB / 'AI_Solutions/CodexCapDP/src', ts / 'CodexCapDP_s1_r1'])

# ---- chain gain vs time
for b in ['holdout/h02', 'holdout/h03', 'holdout/h04', 'holdout/h05', 'holdout/h06', 'holdout/h07', 'holdout/h08',
          'edge/e08_fragmented', 'edge/e16_rowgaps']:
    name = b.split('/')[1]
    for T in [0.75, 1.5, 3, 6]:
        q.run(f'chain_{name}_T{T}', [PY, V5S / 'run_ts.py', '--bin', cap_bin, '--out', R / f'phase1/exp/chain_sweep/T{T}', '--append',
                                     f'--dp-args=--no-sa --cap-time 0 --extra-time 40 --chain-time {T}', f'Testcases/{b}/{name}'], quiet=True)

# ---- e07 overrun, three times
for k in [1, 2, 3]:
    q.run(f'e07_r{k}', [PY, V5S / 'run_ts.py', '--bin', cap_bin, '--out', R / f'phase1/exp/e07_repeat/r{k}', '--dp-args=--no-sa',
                        'Testcases/edge/e07_tworows/e07_tworows'])
note('phase1_exp.py 全部完成（L2 排程、holdout 預設 HPWL 配對、chain 時間掃描、e07 重複）')
