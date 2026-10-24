#!/usr/bin/env python3
"""phase1b_queue.py — follow-up diagnosis after phase 1's core-limit results. Marker: phase1b/ALL_DONE.

  thr_*     sa_bench2 (CPC sources) from CPC's h08 Stage 1: taskset 2 P-cores with --threads 2 / 4 / 8,
            and all CPUs (auto); SA 60 s, no polish, seeds 1, 2  -> does oversubscription help or hurt the SA?
  norowopt  CPC on L1, L2 with --no-rowopt (skips the row re-placement and the whole finish() round loop):
            a probe of how much the low-yield sequential Stage 1 steps cost large designs (not a candidate)
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, V5S, note  # noqa: E402

P = R / 'phase1b'
q = Queue(P)
sab = R / 'bin' / 'sa_bench2_CPC'
start = R / 'phase1' / 'starts' / 'h08' / 'output'
(P / 'thr').mkdir(exist_ok=True)
for seed in [1, 2]:
    for name, pre, thr in [('P2t2', ['taskset', '-c', '0,2'], 2), ('P2t4', ['taskset', '-c', '0,2'], 4),
                           ('P2t8', ['taskset', '-c', '0,2'], 8), ('allauto', [], 0)]:
        n = f'thr_{name}_s{seed}'
        q.run(n, [*pre, sab, LAB / 'Testcases/holdout/h08/h08', start, P / 'thr' / n, '--seed', seed, '--time', 60,
                  '--polish', 0, '--threads', thr], quiet=True)
q.run('norowopt', [PY, V5S / 'run_ts.py', '--bin', R / 'bin/CodexParallelChain/dp', '--out', P / 'norowopt', '--timeout', 900,
                   '--dp-args=--no-rowopt', 'Testcases/large/L1/L1', 'Testcases/large/L2/L2'])
note('phase1b 完成（phase1b/ALL_DONE）')
(P / 'ALL_DONE').write_text('done\n')
