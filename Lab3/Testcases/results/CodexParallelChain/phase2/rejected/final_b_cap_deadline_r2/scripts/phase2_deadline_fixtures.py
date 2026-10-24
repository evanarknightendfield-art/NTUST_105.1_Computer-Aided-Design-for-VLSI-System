#!/usr/bin/env python3
"""Bounded training-only deadline test and pre-fix negative control."""
import fcntl
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import sys
sys.dont_write_bytecode = True
from phase2_acceptance import SOLUTION, BASE, dump, src_hash
from diag import sha, prefix_hashes, original_prefix, verify_protected
from v6q import LAB, R

D = R / 'phase2/cap_deadline_r2/fixtures'
D.mkdir(parents=True, exist_ok=True)
EXE = R / 'tests/cap_deadline_test'
records = []

def execute(label, command, compile=False, expected=0):
    def setup():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        if compile:
            os.sched_setaffinity(0, {16, 17, 18, 19})
    with (D / (label + '.log')).open('w') as log:
        run = subprocess.run([str(x) for x in command], cwd=LAB, stdout=log, stderr=subprocess.STDOUT,
                             preexec_fn=setup, timeout=120)
    if expected == 'failure':
        if run.returncode != -6 or 'Assertion' not in (D / (label + '.log')).read_text():
            raise RuntimeError('Old code did not fail the expected deadline assertion')
    elif run.returncode != expected:
        raise RuntimeError(label + ' failed')
    records.append(dict(label=label, command=[str(x) for x in command], returncode=run.returncode,
                        expected=expected, log_sha256=sha(D / (label + '.log'))))

with (R / 'timing.lock').open('a') as own, (R.parent / 'ClaudeOpus55_v6/timing.lock').open('r') as ref:
    fcntl.flock(own, fcntl.LOCK_EX)
    fcntl.flock(ref, fcntl.LOCK_EX)
    inputs = {}
    for bench in ['toy', 'h06']:
        pl = R / 'phase2/rejected/final_b_v6_control/train/runs' / bench / 'candidate' / bench / 'output_legal.pl'
        inputs[bench] = dict(input=prefix_hashes(original_prefix(bench)), placement_sha256=sha(pl))
        execute(bench, [EXE, original_prefix(bench), pl])
    # The negative control has unchanged inputs; retain the successful evidence.
    prior = R / 'phase2/cap_deadline_r1/fixtures'
    snapshot = R / 'phase2/rejected/final_b_cap_deadline_r1'
    for p in (SOLUTION / 'src').iterdir():
        if p.name != 'capsearch.cpp':
            assert sha(p) == sha(snapshot / 'src' / p.name)
    for name in ['cap_deadline_test.cpp', 'readpl.cpp']:
        assert sha(SOLUTION / 'tests' / name) == sha(snapshot / 'tests' / name)
    old_records = json.loads((prior / 'ALL_DONE').read_text())['records']
    controls = [x for x in old_records if x['label'] in ['build_control', 'link_control', 'v6_expected_failure']]
    assert len(controls) == 3
    for record in controls:
        log = prior / (record['label'] + '.log')
        assert sha(log) == record['log_sha256']
        assert record['returncode'] == (-6 if record['label'] == 'v6_expected_failure' else 0)
        shutil.copy2(log, D / log.name)
        records.append(dict(record, reused_from=str(prior)))
verify_protected()
dump(D / 'ALL_DONE', dict(src_sha256=src_hash(SOLUTION / 'src'), binary_sha256=sha(EXE),
     fixture_inputs=inputs, records=records, note='Synthetic regression fails on v6; toy/h06 cancellation passes. No L2 tuning.'))
print('PASS: toy/h06 deadline fixtures; v6 fails expired-deadline assertion as expected')
