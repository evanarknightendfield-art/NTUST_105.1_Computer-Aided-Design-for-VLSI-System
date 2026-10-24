#!/usr/bin/env python3
"""Coordinator-only final B queue; completion is not a B_DONE decision."""
import fcntl
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import traceback

sys.dont_write_bytecode = True
from phase2_acceptance import SOLUTION, dump, src_hash, validate
from diag import sha, verify_protected
from v6q import LAB, R, PY, Queue

SELF = Path(__file__).resolve()
D = R / 'phase2/final_b_validation'


def main():
    D.mkdir(parents=True, exist_ok=True)
    fixtures = R / 'phase2/cap_deadline_r1/fixtures'
    fixture_evidence = json.loads((fixtures / 'ALL_DONE').read_text())
    if fixture_evidence['src_sha256'] != src_hash(SOLUTION / 'src'):
        raise RuntimeError('Deadline fixture source mismatch')
    for record in fixture_evidence['records']:
        if sha(fixtures / (record['label'] + '.log')) != record['log_sha256']:
            raise RuntimeError('Deadline fixture log changed')
    protocol = dict(src_sha256=src_hash(SOLUTION / 'src'), queue_sha256=sha(SELF),
                    deadline_fixture_sha256=sha(fixtures / 'ALL_DONE'),
                    tests_sha256={p.name: sha(p) for p in sorted((SOLUTION / 'tests').glob('*')) if p.is_file()},
                    scripts_sha256={n: sha(SELF.parent / n) for n in
                                    ['phase2_train.py', 'phase2_large.py', 'phase2_acceptance.py', 'phase2_deadline_regression.py']})
    if (D / 'protocol.json').exists() and json.loads((D / 'protocol.json').read_text()) != protocol:
        raise RuntimeError('Final validation inputs changed')
    dump(D / 'protocol.json', protocol)
    verify_protected()
    evidence = json.loads((D / 'test_evidence/verification.json').read_text())
    if evidence['src_sha256'] != protocol['src_sha256'] or evidence['tests_sha256'] != protocol['tests_sha256']:
        raise RuntimeError('Short test evidence does not match final source/tests')
    for suite in ['tests', 'tests_sanitized', 'tests_tsan']:
        info = evidence['suites'][suite]
        for name, digest in info['files'].items():
            if sha(D / 'test_evidence' / suite / name) != digest:
                raise RuntimeError('Short test evidence changed')
        records = json.loads((D / 'test_evidence' / suite / 'records.json').read_text())
        if not records or any(x['returncode'] != 0 for x in records):
            raise RuntimeError('Short test suite failed')
    # Child queues own the timing locks; do not hold them across these calls.
    q = Queue(D / 'jobs', timed=False)
    for mode in ['train', 'large']:
        if not q.run(mode, [PY, SELF.parent / f'phase2_{mode}.py', 'final_b'], quiet=True):
            raise RuntimeError(f'{mode} queue failed')
        validate(R / 'phase2' / mode / 'final_b', mode, 'final_b')
        if not (R / 'phase2' / mode / 'final_b/ALL_DONE').exists():
            raise RuntimeError(f'{mode} completion marker missing')
    if not q.run('deadline_regression_27', [PY, SELF.parent / 'phase2_deadline_regression.py'], quiet=True):
        raise RuntimeError('27-case deadline regression failed')
    regression = R / 'phase2/deadline_regression/final_b'
    marker = json.loads((regression / 'ALL_DONE').read_text())
    if marker['src_sha256'] != protocol['src_sha256']:
        raise RuntimeError('27-case regression source mismatch')
    # Full LeakSanitizer is intentionally outside the sandbox.
    q = Queue(D / 'asan_jobs')
    with (R.parent / 'ClaudeOpus55_v6/timing.lock').open('r') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if not q.run('full_asan_ubsan', [PY, SOLUTION / 'tests/run_tests.py', '--sanitize'],
                     env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1', UBSAN_OPTIONS='halt_on_error=1'), quiet=True):
            raise RuntimeError('Full ASan/UBSan suite failed')
        dest = D / 'tests_full_asan'
        if not dest.exists():
            dest.mkdir()
            records = json.loads((R / 'tests_sanitized/records.json').read_text())
            for name in ['records.json'] + [x['label'] + '.log' for x in records]:
                shutil.copy2(R / 'tests_sanitized' / name, dest / name)
        records = json.loads((dest / 'records.json').read_text())
        if not records or any(x['returncode'] != 0 for x in records):
            raise RuntimeError('Full sanitizer evidence failed')
        for record in records:
            if re.search(r'ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:',
                         (dest / (record['label'] + '.log')).read_text()):
                raise RuntimeError('Full sanitizer diagnostic found')
    verify_protected(D / 'protected_after.json')
    if src_hash(SOLUTION / 'src') != protocol['src_sha256']:
        raise RuntimeError('Source changed during final validation')
    summaries = {m: json.loads((R / 'phase2' / m / 'final_b/summary.json').read_text())['all_gates']
                 for m in ['train', 'large']}
    summaries['deadline_regression_27'] = marker['all_gates']
    dump(D / 'ALL_DONE', dict(src_sha256=protocol['src_sha256'], gates=summaries,
                             full_asan_ubsan=True, finished=time.ctime(),
                             note='Read all evidence and update WORK_STATE before declaring B_DONE.'))


if __name__ == '__main__':
    try:
        main()
    except Exception:
        dump(D / 'FAILED.json', dict(error=traceback.format_exc(), when=time.ctime()))
        raise
