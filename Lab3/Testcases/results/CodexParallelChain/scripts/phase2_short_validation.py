#!/usr/bin/env python3
"""Run one bounded final-source suite and archive its exact evidence."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True
from phase2_acceptance import SOLUTION, dump, src_hash
from diag import sha, verify_protected
from v6q import R, PY


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('suite', choices=['tests', 'tests_sanitized', 'tests_tsan'])
    args = parser.parse_args()
    dest = R / 'phase2/final_b_validation/test_evidence'
    manifest = dest / 'verification.json'
    source = src_hash(SOLUTION / 'src')
    tests = {p.name: sha(p) for p in sorted((SOLUTION / 'tests').iterdir()) if p.is_file()}
    evidence = json.loads(manifest.read_text()) if manifest.exists() else dict(
        src_sha256=source, tests_sha256=tests, asan_options='detect_leaks=0',
        ubsan_options='halt_on_error=1', tsan_warnings=0, suites={})
    assert evidence['src_sha256'] == source and evidence['tests_sha256'] == tests
    if args.suite in evidence['suites']:
        for name, digest in evidence['suites'][args.suite]['files'].items():
            assert sha(dest / args.suite / name) == digest
        print(args.suite + ': verified existing evidence')
        return
    verify_protected()
    flags = {'tests': [], 'tests_sanitized': ['--sanitize'], 'tests_tsan': ['--tsan']}
    dest.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    with (R / 'timing.lock').open('a') as own, (R.parent / 'ClaudeOpus55_v6/timing.lock').open('r') as ref:
        fcntl.flock(own, fcntl.LOCK_EX)
        fcntl.flock(ref, fcntl.LOCK_EX)
        with (dest / (args.suite + '.runner.log')).open('w') as log:
            subprocess.run([str(PY), str(SOLUTION / 'tests/run_tests.py'), *flags[args.suite]],
                           env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'),
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=280)
        records = json.loads((R / args.suite / 'records.json').read_text())
        assert all(x['returncode'] == 0 for x in records)
        target = dest / args.suite
        target.mkdir(exist_ok=False)
        names = ['records.json'] + [x['label'] + '.log' for x in records]
        for name in names:
            original = R / args.suite / name
            assert not re.search(r'ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:|WARNING: ThreadSanitizer', original.read_text())
            shutil.copy2(original, target / name)
    assert source == src_hash(SOLUTION / 'src')
    verify_protected(dest / 'protected_after.json')
    programs = sum(not x['label'].startswith(('build_', 'link_')) for x in records)
    # Another suite may have completed while this process waited for the locks.
    # Merge its evidence rather than overwriting the manifest read before waiting.
    if manifest.exists():
        evidence = json.loads(manifest.read_text())
        assert evidence['src_sha256'] == source and evidence['tests_sha256'] == tests
    evidence['suites'][args.suite] = dict(programs=programs, records=len(records),
        seconds=time.monotonic() - start, files={n: sha(target / n) for n in names})
    dump(manifest, evidence)
    print(f'{args.suite}: {programs}/{programs} PASS, {len(records)} records; evidence archived')


if __name__ == '__main__':
    main()
