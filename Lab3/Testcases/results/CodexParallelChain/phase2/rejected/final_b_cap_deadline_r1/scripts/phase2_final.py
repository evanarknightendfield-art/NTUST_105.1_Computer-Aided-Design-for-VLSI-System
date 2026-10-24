#!/usr/bin/env python3
"""phase2_final.py — v6 final validation, serial, after the freeze (frozen_sources.sha256 must match).

Same session, same machine, every version re-run (no historical numbers):
  final/lg_<ver>              local_grade testcases + holdout                  v6, CodexCapDP, ChainDP, v5
  final/edge_<ver>            edge (16): run_edge.sh for v6, local_grade for the others (run_edge.sh would
                              overwrite their records in Testcases/results/edge/)    same four
  final/lg_<ver>_L1 / _L2     local_grade L1 (240 s) / L2 (600 s)               v6, CodexCapDP, v5
  final/edge_seed5            gen_edge.py Testcases/edge_seed5 --seed 20261023 --check   (only now)
  final/seed5_<ver>           local_grade edge_seed5                            v4, ChainDP, v5, CodexCapDP, v6
  final/ts_<ver>              run_ts --no-sa holdout + edge (Stage 1 for the HPWL protocols)   v4, CodexCapDP, v6
  final/hpwl_<mode>           hpwl_compare.py fixed / equal / default, holdout + edge, seeds 1-3, v4 / CodexCapDP / v6
"""
import hashlib, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, V5S, env_with_pinned_cmake, note  # noqa: E402

V6 = LAB / 'AI_Solutions/ClaudeOpus55_v6'


def check_frozen():
    for line in (R / 'frozen_sources.sha256').read_text().splitlines():
        h, name = line.split('  ', 1)
        if hashlib.sha256((V6 / name).read_bytes()).hexdigest() != h:
            raise SystemExit(f'frozen source changed: {name}')


check_frozen()
env = env_with_pinned_cmake()
F = R / 'final'
q = Queue(F)


def grade(name, ver, dirs, timeout=600):
    check_frozen()
    q.run(name, [PY, 'Grader/tools/local_grade.py', f'AI_Solutions/{ver}', ver, *dirs, '--work', F / name,
                 '--timeout', timeout, '--dp-args=-v'], env=env)


FOUR = ['ClaudeOpus55_v6', 'CodexCapDP', 'CodexChainDP', 'ClaudeOpus55_v5']
for v in FOUR:
    grade(f'lg_{v}', v, ['Testcases/testcases', 'Testcases/holdout'])
    if v == 'ClaudeOpus55_v6':   # the standard edge record (Testcases/results/edge/ClaudeOpus55_v6/)
        check_frozen()
        q.run(f'edge_{v}', ['bash', 'Testcases/results/run_edge.sh', v], env=dict(env, EDGE_WORK_ROOT=str(F), EDGE_DP_ARGS='-v'))
    else:   # run_edge.sh would overwrite the other versions' tracked edge records: plain local_grade instead
        grade(f'edge_{v}', v, ['Testcases/edge'])
for large, tmo in [('L1', 240), ('L2', 600)]:
    only = F / f'only_{large}'
    only.mkdir(exist_ok=True)
    if not (only / large).exists():
        (only / large).symlink_to(LAB / 'Testcases/large' / large, target_is_directory=True)
    for v in ['ClaudeOpus55_v6', 'CodexCapDP', 'ClaudeOpus55_v5']:
        grade(f'lg_{v}_{large}', v, [only], tmo)

check_frozen()
q.run('edge_seed5', ['taskset', '-c', '16-19', PY, 'Grader/tools/gen_edge.py', 'Testcases/edge_seed5', '--seed', '20261023', '--check'])
for v in ['ClaudeOpus55_v4', 'CodexChainDP', 'ClaudeOpus55_v5', 'CodexCapDP', 'ClaudeOpus55_v6']:
    grade(f'seed5_{v}', v, ['Testcases/edge_seed5'])

bins = {'ClaudeOpus55_v4': R / 'bin/ClaudeOpus55_v4/dp', 'CodexCapDP': R / 'bin/CodexCapDP/dp', 'ClaudeOpus55_v6': R / 'bin/v6_final/dp'}
from v6q import build  # noqa: E402
build('ClaudeOpus55_v4', R / 'bin/ClaudeOpus55_v4')
build('ClaudeOpus55_v6', R / 'bin/v6_final')
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
note('phase2_final.py 全部完成')
(F / 'ALL_DONE').write_text('done\n')
