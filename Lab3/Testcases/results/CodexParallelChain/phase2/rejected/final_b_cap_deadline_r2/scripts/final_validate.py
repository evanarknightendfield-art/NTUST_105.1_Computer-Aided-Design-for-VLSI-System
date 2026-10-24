#!/usr/bin/env python3
"""final_validate.py — reusable, serial, checkpointed final validation of a frozen version.

Copy this scripts/ folder to Testcases/results/<NEW>/scripts/ and run from there; NEW defaults to that
folder's name. Requires Testcases/results/<NEW>/frozen_sources.sha256 (paths relative to AI_Solutions/<NEW>).
Everything is re-run in the same session (no historical numbers):

  final/lg_<v>, final/edge_<v>   testcases+holdout, edge          NEW, BASE, CodexChainDP (reference)
                                 (run_edge.sh only for NEW; the others plain local_grade into final/)
  final/lg_<v>_L1 / _L2          L1 (240 s) / L2 (600 s)          NEW, BASE
  final/<seeddir>                gen_edge.py Testcases/<seeddir> --seed S --check   (generated only here, after the freeze)
  final/seed_<v>                 local_grade on it                 ClaudeOpus55_v4, CodexChainDP, BASE, NEW
  final/ts_<v>                   run_ts --no-sa holdout + edge     ClaudeOpus55_v4, BASE, NEW
  final/hpwl_<mode>              hpwl_compare fixed/equal/default, holdout+edge, seeds 1-3, same three

usage: final_validate.py [--new NAME] [--base ClaudeOpus55_v6] --seed-dir edge_seed6 --seed 20261024
Long (about 7 h): start it with nohup and end the session; completion marker final/ALL_DONE.
"""
import argparse, hashlib, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, V5S, env_with_pinned_cmake, build, note  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('--new', default=R.name)
ap.add_argument('--base', default='ClaudeOpus55_v6')
ap.add_argument('--seed-dir', required=True)
ap.add_argument('--seed', required=True)
a = ap.parse_args()
NEW, BASE = a.new, a.base
SRC = LAB / 'AI_Solutions' / NEW


def check_frozen():
    for line in (R / 'frozen_sources.sha256').read_text().splitlines():
        h, name = line.split('  ', 1)
        if hashlib.sha256((SRC / name).read_bytes()).hexdigest() != h:
            raise SystemExit(f'frozen source changed: {name}')


check_frozen()
if (LAB / 'Testcases' / a.seed_dir).exists() and not (R / 'final' / f'{a.seed_dir}.done.json').exists():
    raise SystemExit(f'Testcases/{a.seed_dir} already exists before this validation: pick a new seed directory')
env = env_with_pinned_cmake()
F = R / 'final'
q = Queue(F)


def grade(name, ver, dirs, timeout=600):
    check_frozen()
    q.run(name, [PY, 'Grader/tools/local_grade.py', f'AI_Solutions/{ver}', ver, *dirs, '--work', F / name,
                 '--timeout', timeout, '--dp-args=-v'], env=env)


for v in [NEW, BASE, 'CodexChainDP']:
    grade(f'lg_{v}', v, ['Testcases/testcases', 'Testcases/holdout'])
    if v == NEW:
        check_frozen()
        q.run(f'edge_{v}', ['bash', 'Testcases/results/run_edge.sh', v], env=dict(env, EDGE_WORK_ROOT=str(F), EDGE_DP_ARGS='-v'))
    else:
        grade(f'edge_{v}', v, ['Testcases/edge'])
for large, tmo in [('L1', 240), ('L2', 600)]:
    only = F / f'only_{large}'
    only.mkdir(exist_ok=True)
    if not (only / large).exists():
        (only / large).symlink_to(LAB / 'Testcases/large' / large, target_is_directory=True)
    for v in [NEW, BASE]:
        grade(f'lg_{v}_{large}', v, [only], tmo)

check_frozen()
q.run(a.seed_dir, ['taskset', '-c', '16-19', PY, 'Grader/tools/gen_edge.py', f'Testcases/{a.seed_dir}', '--seed', a.seed, '--check'])
for v in ['ClaudeOpus55_v4', 'CodexChainDP', BASE, NEW]:
    grade(f'seed_{v}', v, [f'Testcases/{a.seed_dir}'])

bins = {v: build(v, R / 'bin' / v) for v in ['ClaudeOpus55_v4', BASE, NEW]}
for v, exe in bins.items():
    check_frozen()
    q.run(f'ts_{v}', [PY, V5S / 'run_ts.py', '--bin', exe, '--out', F / f'ts_{v}', '--dp-args=--no-sa',
                      'Testcases/holdout', 'Testcases/edge'])
vers = []
for v in bins:
    vers += ['--v', v, LAB / f'AI_Solutions/{v}/src', F / f'ts_{v}']
for mode in ['fixed', 'equal', 'default']:
    check_frozen()
    q.run(f'hpwl_{mode}', [PY, R.parent / 'CodexCapDP/scripts/hpwl_compare.py', mode, F / f'hpwl_{mode}',
                           '--groups', 'holdout', 'edge', *vers])
for v in [NEW, BASE]:   # extra-step timing on the three testcases (holdout + edge come from ts_*)
    q.run(f'extra_testcases_{v}', [PY, V5S / 'run_ts.py', '--bin', R / 'bin' / v / 'dp', '--out', F / f'extra_testcases_{v}',
                                   '--dp-args=--no-sa', 'Testcases/testcases'])
note('final_validate.py 全部完成（final/ALL_DONE）；下一步：python3 scripts/final_report.py')
(F / 'ALL_DONE').write_text('done\n')
