#!/usr/bin/env python3
"""phase2_large.py TAG — full default dp runs of the current v6 source on L2 (seeds 1, 2) and L1 (seed 1), run_ts -v.
Source copied to phase2/large/TAG/src and built there (hash in protocol.json)."""
import hashlib, json, shutil, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, V5S, build, note  # noqa: E402

tag = sys.argv[1]
d = R / 'phase2' / 'large' / tag
if not (d / 'src').exists():
    d.mkdir(parents=True, exist_ok=True)
    shutil.copytree(LAB / 'AI_Solutions' / R.name / 'src', d / 'src')
    shutil.copy(LAB / 'AI_Solutions' / R.name / 'CMakeLists.txt', d / 'CMakeLists.txt')
(d / 'protocol.json').write_text(json.dumps(dict(tag=tag, src_sha256=hashlib.sha256(
    b''.join(p.read_bytes() for p in sorted((d / 'src').iterdir()))).hexdigest()), indent=1) + '\n')
exe = build('v6', d / 'build', src=d)
q = Queue(d)
for design, seed in [('L2', 1), ('L2', 2), ('L1', 1)]:
    q.run(f'{design}_s{seed}', [PY, V5S / 'run_ts.py', '--bin', exe, '--out', d / f'{design}_s{seed}', '--timeout', 900,
                                f'--dp-args=--seed {seed}', f'Testcases/large/{design}/{design}'])
note(f'phase2/large/{tag} 完成')
