#!/usr/bin/env python3
"""phase1c_queue.py — band-loss redo. phase 1's auto runs did 20M attempts PER BAND (sa_bench2 --moves N gives every
band N, total N x bands), so they are not equal-work. Here auto gets --moves 20M / bands (bands from phase 1's
attempt counts: ibm01/ibm05/h07/h08 8, h06 5); the 1-thread 20M runs of phase 1 are the reference. Marker: phase1c/ALL_DONE."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, note  # noqa: E402

P = R / 'phase1c'
q = Queue(P)
sab = R / 'bin' / 'sa_bench2_CPC'
ORIG = {'ibm01': 'Testcases/testcases/ibm01/ibm01', 'ibm05': 'Testcases/testcases/ibm05/ibm05', 'h06': 'Testcases/holdout/h06/h06',
        'h07': 'Testcases/holdout/h07/h07', 'h08': 'Testcases/holdout/h08/h08'}
BANDS = {'ibm01': 8, 'ibm05': 8, 'h06': 5, 'h07': 8, 'h08': 8}
(P / 'band').mkdir(parents=True, exist_ok=True)
for b, nb in BANDS.items():
    for seed in [1, 2]:
        n = f'band_{b}_s{seed}_t0'
        q.run(n, [sab, LAB / ORIG[b], R / 'phase1/starts' / b / 'output', P / 'band' / n, '--seed', seed,
                  '--moves', 20e6 / nb, '--time', 1e5, '--polish', 0, '--threads', 0], quiet=True)
note('phase1c（band 損失重跑，同總次數）完成')
(P / 'ALL_DONE').write_text('done\n')
