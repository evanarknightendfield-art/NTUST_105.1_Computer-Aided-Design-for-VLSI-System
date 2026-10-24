#!/usr/bin/env python3
"""satput_queue.py — after the final validation: is v7's lower SA attempt count on e05 / h08 / e15 (hpwl_default) the
binary or the start? The final's own sa_bench binaries (CPC-built, v7-built), the SAME legal start, 20 s SA, no polish,
automatic threads, seed 1, interleaved cpc / v7 twice; then each from its own start. Writes final_extra/satput/result.csv."""
import csv, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, note  # noqa: E402

P = R / 'final_extra' / 'satput'
q = Queue(P)
H = R / 'final' / 'hpwl_default'
BIN = {'cpc': H / 'CodexParallelChain_sa', 'v7': H / 'ClaudeOpus55_v7_sa'}
START = {'cpc': H / 'legal_starts' / 'CodexParallelChain', 'v7': H / 'legal_starts' / 'ClaudeOpus55_v7'}
ORIG = {'e05_bignets': 'Testcases/edge/e05_bignets/e05_bignets', 'h08': 'Testcases/holdout/h08/h08',
        'e15_realistic': 'Testcases/edge/e15_realistic/e15_realistic'}
runs = []
for b in ORIG:
    for rep in [1, 2]:
        for ver in ['cpc', 'v7']:
            runs.append((b, f'same{rep}', ver, 'v7'))
    for ver in ['cpc', 'v7']:
        runs.append((b, 'own', ver, ver))
for b, tag, ver, st in runs:
    n = f'{b}_{tag}_{ver}'
    q.run(n, [BIN[ver], LAB / ORIG[b], START[st] / b / 'output', P / n, 1, 'default', 20, 0, 0], quiet=True)
with open(P / 'result.csv', 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['bench', 'run', 'binary', 'start', 'hpwl', 'attempts'])
    for b, tag, ver, st in runs:
        h, a = (P / f'{b}_{tag}_{ver}.log').read_text().strip().splitlines()[-1].split(',')[:2]
        w.writerow([b, tag, ver, st, h, a])
note('final_extra/satput 完成（同起點的 SA 次數：binary 還是起點）')
