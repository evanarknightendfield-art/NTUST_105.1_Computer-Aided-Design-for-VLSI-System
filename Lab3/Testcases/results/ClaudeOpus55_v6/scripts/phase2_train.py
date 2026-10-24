#!/usr/bin/env python3
"""phase2_train.py TAG [extra dp args] — Stage 1 of the current v6 source on the tuning set (testcases + holdout).

Copies AI_Solutions/<this results folder's name> (src + CMakeLists) to phase2/train/TAG/src, builds it there, records the
source hash, runs run_ts.py --no-sa -v on the 11 cases, and compares with this session's ChainDP / CodexCapDP
local_grade records (phase1/lg; for a later version: ClaudeOpus55_v6/final). Only testcases + holdout: edge is never used for tuning.
"""
import hashlib, json, shutil, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, V5S, build, note  # noqa: E402

tag = sys.argv[1]
extra = ' '.join(sys.argv[2:])
d = R / 'phase2' / 'train' / tag
if not (d / 'src').exists():
    d.mkdir(parents=True, exist_ok=True)
    shutil.copytree(LAB / 'AI_Solutions' / R.name / 'src', d / 'src')
    shutil.copy(LAB / 'AI_Solutions' / R.name / 'CMakeLists.txt', d / 'CMakeLists.txt')
src_hash = hashlib.sha256(b''.join(p.read_bytes() for p in sorted((d / 'src').iterdir()))).hexdigest()
(d / 'protocol.json').write_text(json.dumps(dict(tag=tag, src_sha256=src_hash, dp_args=extra), indent=1) + '\n')
exe = build('v6', d / 'build', src=d)
q = Queue(d)
q.run('stage1', [PY, V5S / 'run_ts.py', '--bin', exe, '--out', d / 'ts', f'--dp-args=--no-sa {extra}',
                 'Testcases/testcases', 'Testcases/holdout'])
# references: this results folder's own phase1/lg (v6's diagnosis) if present, else v6's final paired run
ref = R / 'phase1/lg' if (R / 'phase1/lg/CodexChainDP_27/record.csv').exists() else None
V6F = R.parent / 'ClaudeOpus55_v6' / 'final'
refs = (['CodexChainDP', ref / 'CodexChainDP_27/record.csv', 'CodexCapDP', ref / 'CodexCapDP_27/record.csv'] if ref else
        ['CodexChainDP', V6F / 'lg_CodexChainDP/record.csv', 'ClaudeOpus55_v6', V6F / 'lg_ClaudeOpus55_v6/record.csv'])
subprocess.run([PY, R / 'scripts/compare_lg.py', d / 'compare.md', *refs, f'{R.name}_{tag}', d / 'ts/record.csv', '--csv', d / 'compare.csv'],
               check=True)
subprocess.run([PY, R / 'scripts/stage1_phases.py', d / 'stage1', d / 'ts'], check=True)
note(f'phase2/train/{tag} 完成（{extra or "預設"}）：見 phase2/train/{tag}/compare.md')
