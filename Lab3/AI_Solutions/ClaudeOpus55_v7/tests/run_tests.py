#!/usr/bin/env python3
"""Build and run the invariant/exhaustive regression suite without CMake."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--sanitize', action='store_true')
    mode.add_argument('--tsan', action='store_true')
    parser.add_argument('--compiler', default='g++')
    args = parser.parse_args()
    out = ROOT.parents[1] / 'Testcases/results' / ROOT.name / ('tests_tsan' if args.tsan else 'tests_sanitized' if args.sanitize else 'tests')
    out.mkdir(parents=True, exist_ok=True)
    flags = ['-std=c++17', '-O1' if args.sanitize or args.tsan else '-O2', '-pthread', '-UNDEBUG']
    if args.sanitize:
        flags += ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if args.tsan:
        flags += ['-g', '-fsanitize=thread', '-fno-omit-frame-pointer']
    records = []

    def run(command, label, timeout=180):
        start = time.perf_counter()
        # Pin compilation only; runtime checks retain the caller's CPU set.
        compile_cpus = set(range(16, 20)) & os.sched_getaffinity(0)
        pin = (lambda: os.sched_setaffinity(0, compile_cpus)) if command[0] == args.compiler and compile_cpus else None
        process = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=timeout, preexec_fn=pin)
        (out / (label + '.log')).write_text(process.stdout)
        records.append({'label': label, 'command': command, 'returncode': process.returncode,
                        'seconds': time.perf_counter() - start})
        (out / 'records.json').write_text(json.dumps(records, indent=2) + '\n')
        print(label + ': ' + ('PASS' if process.returncode == 0 else 'FAIL'), flush=True)
        if process.returncode:
            print(process.stdout)
            raise SystemExit(process.returncode)

    objects = []
    for source in sorted((ROOT / 'src').glob('*.cpp')):
        if source.stem == 'main':
            continue
        obj = out / (source.stem + '.o')
        run([args.compiler, *flags, '-c', str(source), '-o', str(obj)], 'build_' + source.stem)
        objects.append(str(obj))
    for name in ('window', 'multirow', 'optimizer', 'chain', 'parallel_sa', 'pool', 'global', 'capsearch', 'cap_windows', 'guard'):
        exe = out / (name + '_test')
        run([args.compiler, *flags, str(ROOT / 'tests' / (name + '_test.cpp')),
             *objects, '-o', str(exe)], 'link_' + name)
        run([str(exe)], name)
    print('All 10 C++ test programs passed. Logs: ' + str(out), flush=True)


if __name__ == '__main__':
    main()
