#!/usr/bin/env python3
"""h06rep_queue.py TAG N — the TAG training build (phase2/train/TAG/build/dp) on h06 --no-sa, N repeats (max displacement
is bimodal on h06 through the cap search's wall-clock DP time; diagnosis.md 5). Writes phase2/h06rep/TAG/summary.txt."""
import csv, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, PY, V5S, note  # noqa: E402

tag, n = sys.argv[1], int(sys.argv[2])
d = R / 'phase2' / 'h06rep' / tag
q = Queue(d)
exe = R / 'phase2' / 'train' / tag / 'build' / 'dp'
for k in range(1, n + 1):
    q.run(f'r{k}', [PY, V5S / 'run_ts.py', '--bin', exe, '--out', d / f'r{k}', '--dp-args=--no-sa', 'Testcases/holdout/h06/h06'], quiet=True)
vals = [float(next(csv.DictReader(open(d / f'r{k}' / 'record.csv')))['max']) for k in range(1, n + 1)]
(d / 'summary.txt').write_text(' '.join(f'{v:.2f}' for v in vals) + '\n')
note(f'phase2/h06rep/{tag}：h06 最大位移 {n} 次 = {" ".join(f"{v:.2f}" for v in vals)}')
