#!/usr/bin/env python3
"""Build and run the invariant/exhaustive regression suite without CMake."""
import argparse
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--compiler', default='g++')
    args = parser.parse_args()
    out = ROOT.parents[1] / 'Testcases/results' / ROOT.name / ('tests_sanitized' if args.sanitize else 'tests')
    out.mkdir(parents=True, exist_ok=True)
    flags = ['-std=c++17', '-O1' if args.sanitize else '-O2', '-pthread', '-UNDEBUG']
    if args.sanitize:
        flags += ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    records = []

    def run(command, label, timeout=180):
        start = time.perf_counter()
        process = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=timeout)
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
    for name in ('window', 'multirow', 'optimizer', 'chain', 'parallel_sa', 'pool', 'global', 'capsearch'):
        exe = out / (name + '_test')
        run([args.compiler, *flags, str(ROOT / 'tests' / (name + '_test.cpp')),
             *objects, '-o', str(exe)], 'link_' + name)
        run([str(exe)], name)
    print('All 8 C++ test programs passed. Logs: ' + str(out), flush=True)


if __name__ == '__main__':
    main()
