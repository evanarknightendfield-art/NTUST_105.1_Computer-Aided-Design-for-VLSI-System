#!/usr/bin/env python3
"""final_validate.py — the final validation of the version after ClaudeOpus55_v7 (NEW = this results folder's name,
BASE = ClaudeOpus55_v7). Serial, checkpointed; marker final/ALL_DONE (about 6 h). Then final_report.py.

Requires frozen_sources.sha256 (paths relative to AI_Solutions/NEW); every timed step re-checks it.
Everything is re-run in this session (no historical numbers):
  final/lg_<v>, final/edge_<v>      testcases+holdout, edge         NEW, v7, ChainDP (reference); NEW's edge via run_edge.sh
  final/lg_ClaudeOpus55             testcases+holdout               v1 (HISTORY table 1 is "÷ v1")
  final/lg_<v>_<L>_s<k>             L1 (240 s) / L2 (600 s), seeds 1-3   NEW, v7
  final/lg_<v>_<M>                  M1-M3 (240 s), seed 1           NEW, v7
  final/edge_seed8                  gen_edge.py Testcases/edge_seed8 --seed 20261026 --check (generated only here)
  final/seed_<v>                    local_grade on it               v4, ChainDP, v7, NEW
  final/ts_<v>                      run_ts --no-sa holdout + edge   v4, v7, v7 again (= ClaudeOpus55_v7b, the noise control), NEW
  final/hpwl_<mode>                 hpwl_compare fixed (v4, v7, NEW) / equal and default (+ v7b), holdout+edge, seeds 1-3
  final/extra_testcases_<v>         run_ts --no-sa testcases        NEW, v7
"""
import hashlib, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, V5S, env_with_pinned_cmake, build, note  # noqa: E402

NEW, BASE, REF, V4, V1 = R.name, 'ClaudeOpus55_v7', 'CodexChainDP', 'ClaudeOpus55_v4', 'ClaudeOpus55'
V7B = BASE + 'b'   # v7's Stage 1 run a second time: the noise control of the HPWL protocols
SEED_DIR, SEED = 'edge_seed8', '20261026'
SRC = LAB / 'AI_Solutions' / NEW


def check_frozen():
    for line in (R / 'frozen_sources.sha256').read_text().splitlines():
        h, name = line.split('  ', 1)
        if hashlib.sha256((SRC / name).read_bytes()).hexdigest() != h:
            raise SystemExit(f'frozen source changed: {name}')


check_frozen()
F = R / 'final'
if (LAB / 'Testcases' / SEED_DIR).exists() and not (F / f'{SEED_DIR}.done.json').exists():
    raise SystemExit(f'Testcases/{SEED_DIR} already exists before this validation')
env = env_with_pinned_cmake()
q = Queue(F)


def grade(name, ver, dirs, timeout=600, args='-v'):
    check_frozen()
    q.run(name, [PY, 'Grader/tools/local_grade.py', f'AI_Solutions/{ver}', ver, *dirs, '--work', F / name,
                 '--timeout', timeout, f'--dp-args={args}'], env=env)


for v in [NEW, BASE, REF]:
    grade(f'lg_{v}', v, ['Testcases/testcases', 'Testcases/holdout'])
    if v == NEW:
        check_frozen()
        q.run(f'edge_{v}', ['bash', 'Testcases/results/run_edge.sh', v], env=dict(env, EDGE_WORK_ROOT=str(F), EDGE_DP_ARGS='-v'))
    else:
        grade(f'edge_{v}', v, ['Testcases/edge'])
grade(f'lg_{V1}', V1, ['Testcases/testcases', 'Testcases/holdout'], args='')
for large, tmo in [('L1', 240), ('L2', 600)]:
    only = F / f'only_{large}'
    only.mkdir(exist_ok=True)
    if not (only / large).exists():
        (only / large).symlink_to(LAB / 'Testcases/large' / large, target_is_directory=True)
    for seed in [1, 2, 3]:
        for v in [NEW, BASE]:
            grade(f'lg_{v}_{large}_s{seed}', v, [only], tmo, args=f'-v --seed {seed}')
for m in ['M1', 'M2', 'M3']:
    only = F / f'only_{m}'
    only.mkdir(exist_ok=True)
    if not (only / m).exists():
        (only / m).symlink_to(LAB / 'Testcases/large' / m, target_is_directory=True)
    for v in [NEW, BASE]:
        grade(f'lg_{v}_{m}', v, [only], 240)

check_frozen()
q.run(SEED_DIR, ['taskset', '-c', '16-19', PY, 'Grader/tools/gen_edge.py', f'Testcases/{SEED_DIR}', '--seed', SEED, '--check'])
for v in [V4, REF, BASE, NEW]:
    grade(f'seed_{v}', v, [f'Testcases/{SEED_DIR}'])

bins = {v: build(v, R / 'bin' / f'final_{v}') for v in [V4, BASE, NEW]}
for v, exe in list(bins.items()) + [(V7B, bins[BASE])]:
    check_frozen()
    q.run(f'ts_{v}', [PY, V5S / 'run_ts.py', '--bin', exe, '--out', F / f'ts_{v}', '--dp-args=--no-sa',
                      'Testcases/holdout', 'Testcases/edge'])
vers = []
for v in bins:
    vers += ['--v', v, LAB / f'AI_Solutions/{v}/src', F / f'ts_{v}']
control = ['--v', V7B, LAB / f'AI_Solutions/{BASE}/src', F / f'ts_{V7B}']
for mode in ['fixed', 'equal', 'default']:
    check_frozen()
    q.run(f'hpwl_{mode}', [PY, R / 'scripts' / 'hpwl_compare.py', mode, F / f'hpwl_{mode}',
                           '--groups', 'holdout', 'edge', *vers, *([] if mode == 'fixed' else control)])
for v in [NEW, BASE]:   # extra-step timing on the three testcases (holdout + edge come from ts_*)
    q.run(f'extra_testcases_{v}', [PY, V5S / 'run_ts.py', '--bin', bins[v], '--out', F / f'extra_testcases_{v}',
                                   '--dp-args=--no-sa', 'Testcases/testcases'])
note('final_validate.py 全部完成（final/ALL_DONE）；下一步：python3 scripts/final_report.py')
(F / 'ALL_DONE').write_text('done\n')
