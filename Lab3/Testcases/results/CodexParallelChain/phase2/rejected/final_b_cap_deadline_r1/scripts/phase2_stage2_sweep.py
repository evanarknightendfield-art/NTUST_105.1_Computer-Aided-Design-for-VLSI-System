#!/usr/bin/env python3
"""phase2_stage2_sweep.py — large designs: SA / polish split with v6's polish (passes until no gain / no fit)
and v6's thread floor, from this session's CodexCapDP L1 / L2 legal placements.

  v6:      sa_bench built from AI_Solutions/<results folder name>/src, SA S -> polish 90-S, S = 15 / 30 / 45 / 60 (or argv: TAG L2_SPLITS L1_SPLITS, e.g. v6b 0,7,15 60)
  base:    sa_bench built from CodexCapDP (its default for that design: L1 SA60->P30, L2 SA30->P60)
seeds 1-2, automatic threads. Output: phase2/stage2_sweep/<design>_<ver>_sa<S>_s<seed>.csv
"""
import subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, note  # noqa: E402

SA_BENCH = LAB / 'AI_Solutions/CodexChainDP/tests/sa_bench.cpp'
tag = sys.argv[1] if len(sys.argv) > 1 else 'v6a'
splits = {'L2': [int(x) for x in sys.argv[2].split(',')], 'L1': [int(x) for x in sys.argv[3].split(',')]} if len(sys.argv) > 3 else {'L2': [15, 30, 45, 60], 'L1': [15, 30, 45, 60]}


def sab(name, src):
    exe = R / 'bin' / f'sa_bench_{name}'
    subprocess.run(['taskset', '-c', '16-19', 'g++', '-std=c++17', '-O3', '-DNDEBUG', '-pthread', f'-I{src}', SA_BENCH,
                    *[src / f'{n}.cpp' for n in ['annealer', 'optimizer', 'window', 'placedb', 'global']], '-o', exe], check=True)
    return exe


new = sab(tag, LAB / 'AI_Solutions' / R.name / 'src')
base = R / 'bin' / 'sa_bench_CodexCapDP'
q = Queue(R / 'phase2' / 'stage2_sweep')
for design in ['L2', 'L1']:
    src = LAB / f'Testcases/large/{design}/{design}'
    sd = q.out / f'start_{design}'
    sd.mkdir(exist_ok=True)
    for ext in ['.aux', '.nodes', '.nets', '.wts', '.scl']:
        if not (sd / f'{design}{ext}').exists():
            (sd / f'{design}{ext}').symlink_to(Path(f'{src}{ext}'))
    if not (sd / f'{design}.pl').exists():
        (sd / f'{design}.pl').symlink_to(R / f'phase1/lg/CodexCapDP_{design}/results/CodexCapDP/{design}/output_legal.pl')
    runs = [('base', base, 30 if design == 'L2' else 60)] + [(tag, new, s) for s in splits[design]]
    for seed in [1, 2]:
        for ver, exe, sa in runs:
            name = f'{design}_{ver}_sa{sa}_s{seed}'
            q.run(name, ['bash', '-c', f'"$0" "$@" > {q.out}/{name}.csv 2> {q.out}/{name}.err', exe, src, sd / design, q.out / name,
                         seed, 'default', sa, 90 - sa, 0])
            Path(f'{q.out / name}.trace.csv').unlink(missing_ok=True)
note(f'phase2_stage2_sweep.py {tag} 完成')
