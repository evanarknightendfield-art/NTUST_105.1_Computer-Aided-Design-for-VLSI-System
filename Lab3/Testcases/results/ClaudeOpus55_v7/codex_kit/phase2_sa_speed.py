#!/usr/bin/env python3
"""phase2_sa_speed.py TAG [REPS] — is the current source's SA faster / better than v7's, measured with the real dp?

1. same   sa_bench2 built from each source, the SAME v7 start (phaseA/starts), fixed 2M attempts, --threads 1 and 4,
          seed 1: are the placements bit-identical? (a pure speed-up must leave every move decision unchanged)
2. dp     full default dp (run_ts -v) on ibm01 ibm05 h06 h07 h08, new and v7 interleaved, REPS rounds (default 2),
          SA seed = round: SA attempts in the 60 s and the final HPWL. Use these numbers, not sa_bench2's, for speed:
          separately compiled sa_bench binaries differed by 3-13 % in attempts for identical SA code (code layout;
          Testcases/results/ClaudeOpus55_v7/final_extra/satput).
Writes phase2/sa_speed/TAG/summary.md."""
import csv, filecmp, shutil, statistics as st, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, V5S, build, note  # noqa: E402

BASE = 'ClaudeOpus55_v7'
tag = sys.argv[1]
reps = int(sys.argv[2]) if len(sys.argv) > 2 else 2
d = R / 'phase2' / 'sa_speed' / tag
if not (d / 'src').exists():
    d.mkdir(parents=True, exist_ok=True)
    shutil.copytree(LAB / 'AI_Solutions' / R.name / 'src', d / 'src')
    shutil.copy(LAB / 'AI_Solutions' / R.name / 'CMakeLists.txt', d / 'CMakeLists.txt')
new = build('new', d / 'build', src=d)
base = R / 'bin' / BASE / 'dp'
if not base.exists():
    base = build(BASE, R / 'bin' / BASE, src=LAB / 'AI_Solutions' / BASE)
drv = LAB / 'AI_Solutions' / R.name / 'tests' / 'sa_bench2.cpp'
sab = {}
for ver, src in [('new', d / 'src'), ('v7', LAB / 'AI_Solutions' / BASE / 'src')]:
    exe = d / f'sa_bench2_{ver}'
    if not exe.exists():
        cpp = [str(p) for p in sorted(src.glob('*.cpp')) if p.name != 'main.cpp']
        subprocess.run(['taskset', '-c', '16-19', 'g++', '-std=c++17', '-O2', '-pthread', f'-I{src}', str(drv), *cpp, '-o', str(exe)],
                       check=True)
    sab[ver] = exe
q = Queue(d)
start = R / 'phaseA' / 'starts' / 'h06' / 'output'
(d / 'same').mkdir(exist_ok=True)
for thr in [1, 4]:
    for ver in ['new', 'v7']:
        q.run(f'same_t{thr}_{ver}', [sab[ver], LAB / 'Testcases/holdout/h06/h06', start, d / 'same' / f't{thr}_{ver}', '--seed', 1,
                                     '--moves', 2e6, '--time', 1e5, '--polish', 0, '--threads', thr], quiet=True)
B = ['Testcases/testcases/ibm01/ibm01', 'Testcases/testcases/ibm05/ibm05', 'Testcases/holdout/h06/h06',
     'Testcases/holdout/h07/h07', 'Testcases/holdout/h08/h08']
for k in range(1, reps + 1):
    for ver, exe in [('new', new), ('v7', base)]:
        q.run(f'dp_{ver}_r{k}', [PY, V5S / 'run_ts.py', '--bin', exe, '--out', d / f'dp_{ver}_r{k}', f'--dp-args=--seed {k}', *B])

lines = [f'# {tag}：SA 速度（新版 vs 同輪 v7）', '', '## 同起點、固定 2M 次：輸出是否逐位相同', '']
for thr in [1, 4]:
    a, b = d / 'same' / f't{thr}_new.pl', d / 'same' / f't{thr}_v7.pl'
    lines.append(f'- {thr} 執行緒：{"相同" if a.exists() and b.exists() and filecmp.cmp(a, b, shallow=False) else "不同"}')
lines += ['', '## 真正的 dp（預設參數）：SA 次數與 HPWL，各輪平均', '', '| bench | 次數 新 | 次數 v7 | 次數比 | HPWL 比（範圍） |', '|---|---:|---:|---:|---:|']
for b in [Path(x).name for x in B]:
    att = {v: [] for v in ['new', 'v7']}
    hp = {v: [] for v in ['new', 'v7']}
    for k in range(1, reps + 1):
        for v in att:
            r = {x['bench']: x for x in csv.DictReader(open(d / f'dp_{v}_r{k}' / 'record.csv'))}[b]
            att[v].append(float(r['sa_attempts'] or 0))
            hp[v].append(float(r['hpwl_sa']))
    rr = [x / y for x, y in zip(hp['new'], hp['v7'])]
    lines.append(f"| {b} | {st.mean(att['new']) / 1e6:.1f}M | {st.mean(att['v7']) / 1e6:.1f}M | {st.mean(att['new']) / st.mean(att['v7']):.3f} | "
                 f"{st.mean(rr):.4f}（{min(rr):.4f}–{max(rr):.4f}） |")
(d / 'summary.md').write_text('\n'.join(lines) + '\n')
note(f'phase2/sa_speed/{tag} 完成：見 phase2/sa_speed/{tag}/summary.md')
