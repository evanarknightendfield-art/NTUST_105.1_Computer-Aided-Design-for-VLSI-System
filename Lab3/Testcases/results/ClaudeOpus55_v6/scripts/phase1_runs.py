#!/usr/bin/env python3
"""phase1_runs.py — Phase 1 (diagnosis) timed runs, serial, same session for all versions.

  lg/<ver>_27, lg/<ver>_L1, lg/<ver>_L2   local_grade (default args + -v), CodexCapDP / v5 / ChainDP
  ts/<ver>_s1_rK                          run_ts.py --no-sa on the 27 (Stage 1 phase times; CapDP x3 = run-to-run spread)
  ts/<ver>_L2                             run_ts.py full default run on L2 (SA statistics)
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, V5S, build, env_with_pinned_cmake, note  # noqa: E402

VERS = ['CodexCapDP', 'ClaudeOpus55_v5', 'CodexChainDP']
SETS27 = ['Testcases/testcases', 'Testcases/holdout', 'Testcases/edge']
env = env_with_pinned_cmake()
bins = {v: build(v, R / 'bin' / v) for v in VERS}

q = Queue(R / 'phase1' / 'lg')
for v in VERS:
    q.run(f'{v}_27', [PY, 'Grader/tools/local_grade.py', f'AI_Solutions/{v}', v, *SETS27,
                      '--work', q.out / f'{v}_27', '--dp-args=-v'], env=env)
for large, tmo in [('L1', 240), ('L2', 600)]:
    only = R / 'phase1' / f'only_{large}'
    only.mkdir(exist_ok=True)
    if not (only / large).exists():
        (only / large).symlink_to(LAB / 'Testcases/large' / large, target_is_directory=True)
    for v in VERS:
        q.run(f'{v}_{large}', [PY, 'Grader/tools/local_grade.py', f'AI_Solutions/{v}', v, only,
                               '--work', q.out / f'{v}_{large}', '--timeout', tmo, '--dp-args=-v'], env=env)

t = Queue(R / 'phase1' / 'ts', timed=False)   # the lock is already held by q
for v, runs in [('CodexCapDP', 3), ('ClaudeOpus55_v5', 1), ('CodexChainDP', 1)]:
    for k in range(1, runs + 1):
        t.run(f'{v}_s1_r{k}', [PY, V5S / 'run_ts.py', '--bin', bins[v], '--out', t.out / f'{v}_s1_r{k}',
                               '--dp-args=--no-sa', *SETS27])
for v in ['CodexCapDP', 'ClaudeOpus55_v5']:
    t.run(f'{v}_L2', [PY, V5S / 'run_ts.py', '--bin', bins[v], '--out', t.out / f'{v}_L2',
                      '--timeout', 900, 'Testcases/large/L2/L2'])
note('phase1_runs.py 全部完成（lg 27+L1+L2 三版本、ts Stage 1、ts L2）')
