#!/usr/bin/env python3
"""rep_queue.py TAG BENCH N [dp args] — the TAG training build (phase2/train/TAG/build/dp) and v7, --no-sa, N repeats each,
interleaved, on one bench (e.g. h06, whose maximum displacement is bimodal through the cap search's wall-clock DP time).
Writes phase2/rep/TAG_BENCH/summary.txt (max / total of every run)."""
import csv, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, V5S, build, note  # noqa: E402

BASE = 'ClaudeOpus55_v7'
tag, bench, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
extra = ' '.join(sys.argv[4:])
prefix = next(p for p in [LAB / 'Testcases' / g / bench / bench for g in ['testcases', 'holdout', 'large']]
              if p.with_suffix('.aux').exists())
d = R / 'phase2' / 'rep' / f'{tag}_{bench}'
base = R / 'bin' / BASE / 'dp'
if not base.exists():
    base = build(BASE, R / 'bin' / BASE, src=LAB / 'AI_Solutions' / BASE)
exes = {'new': R / 'phase2' / 'train' / tag / 'build' / 'dp', 'v7': base}
q = Queue(d)
for k in range(1, n + 1):
    for v, exe in exes.items():
        q.run(f'{v}_r{k}', [PY, V5S / 'run_ts.py', '--bin', exe, '--out', d / f'{v}_r{k}', f'--dp-args=--no-sa {extra}', prefix], quiet=True)
lines = []
for v in exes:
    rs = [next(csv.DictReader(open(d / f'{v}_r{k}' / 'record.csv'))) for k in range(1, n + 1)]
    lines.append(f"{v}: max " + ' '.join(f"{float(r['max']):.4f}" for r in rs) + ' | total ' + ' '.join(f"{float(r['disp']):.2f}" for r in rs))
(d / 'summary.txt').write_text('\n'.join(lines) + '\n')
note(f'phase2/rep/{tag}_{bench}：' + '；'.join(lines))
