#!/usr/bin/env python3
"""phase1_exp2.py — which constraint binds CodexCapDP's maximum-displacement search?

CodexCapDP Stage 1 (--no-sa, -v, run_ts) on the benches it loses (h04, h07, e03, e12) and on the
ones where spending total hurts (h03, e08, e16), with
  base      default (6 / 12 s extra, total ratio 1.0048)
  ratio     --total-ratio 1.01            (ChainDP's internal total budget)
  time      --extra-time 20               (about ChainDP's 4 + 16 s)
  both      --total-ratio 1.01 --extra-time 20
Diagnosis only: e03 / e08 / e12 / e16 are edge cases, nothing is tuned on them.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, PY, V5S, note  # noqa: E402

cap_bin = R / 'bin/CodexCapDP/dp'
q = Queue(R / 'phase1' / 'exp2')
CONF = {'base': '', 'ratio': '--total-ratio 1.01', 'time': '--extra-time 20', 'both': '--total-ratio 1.01 --extra-time 20'}
for b in ['holdout/h04', 'holdout/h07', 'holdout/h03', 'edge/e03_dense97', 'edge/e12_collapse', 'edge/e08_fragmented', 'edge/e16_rowgaps']:
    name = b.split('/')[1]
    for c, extra in CONF.items():
        q.run(f'{c}_{name}', [PY, V5S / 'run_ts.py', '--bin', cap_bin, '--out', R / f'phase1/exp2/{c}', '--append',
                              f'--dp-args=--no-sa {extra}', f'Testcases/{b}/{name}'], quiet=True)
note('phase1_exp2.py 完成（cap 搜尋：內部比例 / 時間 / 兩者）')
