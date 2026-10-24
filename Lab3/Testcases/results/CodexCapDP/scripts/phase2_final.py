#!/usr/bin/env python3
"""Serial, checkpointed final validation. Requires an already frozen source tree."""
import argparse, csv, fcntl, hashlib, json, os, shutil, subprocess, sys, time
from pathlib import Path

R = Path(__file__).resolve().parents[1]
LAB = R.parents[2]
SRC = LAB / 'AI_Solutions/CodexCapDP'
F = R / 'final'
OLD = R.parent / 'ClaudeOpus55_v5'
PY = '/home/evan/anaconda3/bin/python3'
ap = argparse.ArgumentParser()
ap.add_argument('--training', default='full_v5')
ap.add_argument('--bin', default=str(R / 'phase2/build_v5/dp'))
args = ap.parse_args()
F.mkdir(exist_ok=True)
lock = (R / 'timing.lock').open('w')
fcntl.flock(lock, fcntl.LOCK_EX)
toolbin = R / 'scripts/toolbin'
toolbin.mkdir(exist_ok=True)
wrapper = toolbin / 'cmake'
wrapper.write_text('#!/bin/sh\nexec taskset -c 16-19 /home/evan/anaconda3/envs/eda/bin/cmake "$@"\n')
wrapper.chmod(0o755)
env = dict(os.environ, PATH=str(toolbin) + ':' + os.environ['PATH'], PYTHONUNBUFFERED='1')

def frozen():
    path = R / 'frozen_sources.sha256'
    if not path.exists():
        raise RuntimeError('Freeze sources before final validation')
    for line in path.read_text().splitlines():
        sha, name = line.split('  ', 1)
        if hashlib.sha256((SRC / name).read_bytes()).hexdigest() != sha:
            raise RuntimeError('Frozen source changed: ' + name)
    return hashlib.sha256(path.read_bytes()).hexdigest()

freeze = frozen()

def state(message):
    with (SRC / 'WORK_STATE.md').open('a') as f:
        f.write('\n- 最終驗證：' + message + '\n')
    print(message, flush=True)

def run(name, command, custom_env=None):
    frozen()
    marker = F / (name + '.done.json')
    if marker.exists():
        old = json.loads(marker.read_text())
        if old['freeze'] != freeze or old['command'] != command:
            raise RuntimeError('Completed step has a different protocol: ' + name)
        return
    start = time.time()
    protocol = dict(freeze=freeze, command=command, started=start)
    (F / (name + '.running.json')).write_text(json.dumps(protocol, indent=2) + '\n')
    state('開始 ' + name + '；log = final/' + name + '.log')
    with (F / (name + '.log')).open('w') as f:
        rc = subprocess.run(command, cwd=LAB, env=custom_env or env, stdout=f, stderr=subprocess.STDOUT).returncode
    protocol.update(returncode=rc, seconds=time.time()-start)
    if rc:
        (F / (name + '.failed.json')).write_text(json.dumps(protocol, indent=2) + '\n')
        state(name + ' 失敗，先讀 log，不重跑已完成步驟。')
        raise SystemExit(rc)
    marker.write_text(json.dumps(protocol, indent=2) + '\n')
    state(name + ' 完成；不要重跑。')

def grade(name, version, dirs, timeout=600):
    run(name, [PY, 'Grader/tools/local_grade.py', 'AI_Solutions/'+version, version,
               *map(str, dirs), '--work', str(F/name), '--timeout', str(timeout), '--dp-args=-v'])

grade('lg_new', 'CodexCapDP', ['Testcases/testcases', 'Testcases/holdout'])
edge_env = dict(env, EDGE_WORK_ROOT=str(F), EDGE_DP_ARGS='-v')
run('edge_new', ['bash', 'Testcases/results/run_edge.sh', 'CodexCapDP'], edge_env)
for large, timeout in [('L1', 240), ('L2', 600)]:
    d = F / ('only_'+large)
    d.mkdir(exist_ok=True)
    link = d / large
    if not link.exists(): link.symlink_to(LAB / 'Testcases/large' / large, target_is_directory=True)
    grade('lg_new_'+large, 'CodexCapDP', [d], timeout)

# Reuse completed Stage 1 tuning runs when their binary is the frozen binary.
# Only edge needs a new timestamped Stage 1 run for the HPWL time protocol.
train = R / 'phase2' / args.training
exe = Path(args.bin).resolve()
protocol = json.loads((train/'protocol.json').read_text())
binary_sha = hashlib.sha256(exe.read_bytes()).hexdigest()
if protocol['binary_sha256'] != binary_sha:
    compatibility = json.loads((R/'phase2/help_update.json').read_text())
    if (compatibility['old_binary_sha256'] != protocol['binary_sha256'] or
        compatibility['new_binary_sha256'] != binary_sha or
        compatibility['new_source_sha256'] != hashlib.sha256((SRC/'src/main.cpp').read_bytes()).hexdigest()):
        raise RuntimeError('Training binary no longer matches the selected frozen algorithm')
    protocol = dict(protocol, compatibility=compatibility)
ts = F / 'ts_new_stage1'
ts.mkdir(exist_ok=True)
for b in ['h01','h02','h03','h04','h05','h06','h07','h08']:
    if not (ts/b).exists(): (ts/b).symlink_to(train/b, target_is_directory=True)
(ts/'reuse.json').write_text(json.dumps(dict(source=str(train), protocol=protocol,
    note='Completed, unchanged holdout Stage 1 reused; edge is regression only'), indent=2)+'\n')
for aux in sorted((LAB/'Testcases/edge').glob('*/*.aux')):
    run('ts_'+aux.stem, [PY, str(OLD/'scripts/run_ts.py'), '--bin', str(exe), '--out', str(ts),
                        '--append', '--dp-args=--no-sa', str(aux.with_suffix(''))])
    run('check_ts_'+aux.stem, ['taskset','-c','16-19',PY,'Grader/tools/check_pl.py',str(aux.with_suffix('')),str(ts/aux.stem/'output_legal.pl'),'--quiet'])

# Existing fixed-work and default reference runs have unchanged source,
# starts and protocol. Keep their provenance instead of rerunning them.
v4 = ['--v','ClaudeOpus55_v4','AI_Solutions/ClaudeOpus55_v4/src',str(OLD/'phase1/ts_ClaudeOpus55_v4')]
v5 = ['--v','ClaudeOpus55_v5','AI_Solutions/ClaudeOpus55_v5/src',str(OLD/'final/ts_v5_stage1')]
new = ['--v','CodexCapDP','AI_Solutions/CodexCapDP/src',str(ts)]
for mode in ['fixed','equal','default']:
    out = F / ('hpwl_'+mode)
    out.mkdir(exist_ok=True)
    if mode != 'equal' and not (out/'record.csv').exists():
        reference = OLD/'final'/('hpwl_'+mode)/'record.csv'
        shutil.copy2(reference, out/'record.csv')
        (out/'reuse.json').write_text(json.dumps(dict(source=str(reference),
            sha256=hashlib.sha256(reference.read_bytes()).hexdigest(),
            note='Unchanged v4/v5 references; new version runs all three seeds'), indent=2)+'\n')
    versions = v4 + (v5 if mode != 'equal' else []) + new
    run('hpwl_'+mode, [PY, str(R/'scripts/hpwl_compare.py'), mode, str(out), '--groups','holdout','edge', *versions])

# The new seed is generated only after frozen() succeeds, never for tuning.
run('generate_seed4', ['taskset','-c','16-19',PY,'Grader/tools/gen_edge.py','Testcases/edge_seed4','--seed','20261022','--check'])
for version in ['ClaudeOpus55_v4','CodexChainDP','ClaudeOpus55_v5','CodexCapDP']:
    grade('seed4_'+version, version, ['Testcases/edge_seed4'])
state('所有計算步驟完成；下一步彙整數據、README、COMPARISON 與 edge 表，不改演算法。')
(F/'ALL_DONE.json').write_text(json.dumps(dict(freeze=freeze, completed=time.time()), indent=2)+'\n')
