#!/usr/bin/env python3
"""bands60_queue.py — does the remaining band-edge loss matter at the default SA time? sa_bench2 (CPC sources = v7's SA)
from CPC's Stage 1 (phase1/starts), SA 60 s, no polish, --threads 4 vs 8, seeds 1, 2, on the training designs whose SA
runs in parallel (ibm01 ibm05 h06 h07 h08). Marker: phase2/bands60/ALL_DONE."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, note  # noqa: E402

P = R / 'phase2' / 'bands60'
q = Queue(P)
ORIG = {'ibm01': 'Testcases/testcases/ibm01/ibm01', 'ibm05': 'Testcases/testcases/ibm05/ibm05', 'h06': 'Testcases/holdout/h06/h06',
        'h07': 'Testcases/holdout/h07/h07', 'h08': 'Testcases/holdout/h08/h08'}
(P / 'out').mkdir(parents=True, exist_ok=True)
for b in ORIG:
    for seed in [1, 2]:
        for thr in [4, 8]:
            n = f'{b}_s{seed}_t{thr}'
            q.run(n, [R / 'bin/sa_bench2_CPC', LAB / ORIG[b], R / 'phase1/starts' / b / 'output', P / 'out' / n, '--seed', seed,
                      '--time', 60, '--polish', 0, '--threads', thr], quiet=True)
note('phase2/bands60 完成（SA 60 秒，4 vs 8 band）')
(P / 'ALL_DONE').write_text('done\n')
