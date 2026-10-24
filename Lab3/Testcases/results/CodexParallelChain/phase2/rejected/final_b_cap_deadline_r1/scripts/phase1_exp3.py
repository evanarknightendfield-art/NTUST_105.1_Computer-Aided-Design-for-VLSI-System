#!/usr/bin/env python3
"""phase1_exp3.py — L2: does the SA gain from more than the automatic 2 threads?

sa_bench (CodexCapDP sources; annealer copy in phase1/exp3_src without the memory cap on explicit --threads), start = this session's CodexCapDP L2 legal placement,
SA30 -> polish 60, threads 2 (= automatic) vs 4, seeds 1-2.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, note  # noqa: E402

sab = R / 'bin' / 'sa_bench_threads_uncapped'   # phase1/exp3_src: only the memory cap on an explicit thread count removed
L2 = LAB / 'Testcases/large/L2/L2'
sd = R / 'phase1/exp/l2_sched/start_CodexCapDP'
q = Queue(R / 'phase1' / 'exp3')
for seed in [1, 2]:
    for thr in [2, 4]:
        name = f'l2_t{thr}_s{seed}'
        q.run(name, ['bash', '-c', f'"$0" "$@" > {q.out}/{name}.csv', sab, L2, sd / 'L2', q.out / name, seed, 'default', 30, 60, thr])
        Path(f'{q.out / name}.trace.csv').unlink(missing_ok=True)
note('phase1_exp3.py 完成（L2 SA 執行緒 2 vs 4）')
