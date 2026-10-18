#!/usr/bin/env python3
"""Compile baselines and candidate identically, run sequentially, verify outputs.

python3 tests/compare.py [--effort 200] [--time 1000] [--all-baselines]
Defaults: v2 vs new on all 11 cases; earlier two baselines on the three course cases.
The large time ceiling makes the chosen effort, not machine speed, the stopping rule.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
LAB = ROOT.parents[1]
LABELS = ('ClaudeOpus55', 'CodexGapSA', 'ClaudeOpus55_v2', 'CodexParetoDP')
KEYS = ('Total displacement (legal)', 'Maximum displacement (legal)', 'Total HPWL (legal)', 'Total HPWL (SA)')


def run_checked(command, cwd=None):
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True, timeout=600)
    if result.returncode:
        raise RuntimeError(f'{command}\n{result.stdout}\n{result.stderr}')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--effort', type=int, default=200)
    parser.add_argument('--time', type=float, default=1000)
    parser.add_argument('--all-baselines', action='store_true')
    parser.add_argument('--candidate-only', action='store_true', help='reuse verified baseline runs with matching settings and source hashes')
    args = parser.parse_args()
    out = ROOT / 'results' / 'comparison'
    out.mkdir(parents=True, exist_ok=True)
    sources = {}
    previous = []
    if args.candidate_only:
        settings = json.loads((out / 'settings.json').read_text())
        if settings['effort'] != args.effort or settings['time'] != args.time:
            raise RuntimeError('baseline settings differ')
        sources = settings['sources']
        for label in LABELS[:-1]:
            for name, digest in sources[label].items():
                if hashlib.sha256((LAB / name).read_bytes()).hexdigest() != digest:
                    raise RuntimeError('baseline source changed: ' + name)
        previous = [r for r in csv.DictReader((out / 'records.csv').open()) if r['solution'] != 'CodexParetoDP']
        if any(r['verdict'] != 'PASS' for r in previous):
            raise RuntimeError('cannot reuse failed baseline')
    binaries = {}
    for label in LABELS:
        if args.candidate_only and label != 'CodexParetoDP':
            continue
        files = sorted((LAB / 'AI_Solutions' / label / 'src').glob('*.cpp'))
        target = out / 'bin' / label
        target.parent.mkdir(exist_ok=True)
        build = run_checked(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', *map(str, files), '-o', str(target)])
        (out / f'{label}_build.log').write_text(build.stdout + build.stderr)
        binaries[label] = target
        sources[label] = {str(p.relative_to(LAB)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    (out / 'settings.json').write_text(json.dumps({'seed': 1, **vars(args), 'compiler': run_checked(['g++', '--version']).stdout, 'sources': sources}, indent=2))
    records = previous
    benches = sorted((LAB / 'Testcases' / 'testcases').glob('*/*.aux')) + sorted((LAB / 'Testcases' / 'holdout').glob('*/*.aux'))
    for bench in benches:
        for label in LABELS:
            if args.candidate_only and label != 'CodexParetoDP':
                continue
            if bench.parents[1].name == 'holdout' and label in LABELS[:2] and not args.all_baselines:
                continue
            directory = out / label / bench.stem
            directory.mkdir(parents=True, exist_ok=True)
            command = [str(binaries[label]), str(bench), 'output', '--seed', '1', '--time', str(args.time), '--effort', str(args.effort), '--trace', 'trace.csv', '-v']
            started = time.perf_counter()
            result = subprocess.run(command, cwd=directory, text=True, capture_output=True, timeout=600)
            elapsed = time.perf_counter() - started
            (directory / 'stdout.txt').write_text(result.stdout)
            (directory / 'stderr.txt').write_text(result.stderr)
            problems = []
            reported = {}
            for line in result.stdout.splitlines():
                key, _, value = line.rpartition(':')
                if key in KEYS:
                    reported[key] = float(value)
            if result.returncode or len(reported) != 4 or len(result.stdout.splitlines()) != 4:
                problems.append('program failed / invalid stdout')
            for suffix in ('.aux', '.nodes', '.nets', '.wts', '.scl', '.pl', '_legal.pl'):
                if not (directory / ('output' + suffix)).is_file():
                    problems.append('missing ' + suffix)
            checked = {}
            for stage, suffix in (('legal', '_legal.pl'), ('sa', '.pl')):
                checker = subprocess.run([sys.executable, str(LAB / 'Grader/tools/check_pl.py'), str(bench), str(directory / ('output' + suffix)), '--quiet'], text=True, capture_output=True)
                (directory / f'check_{stage}.txt').write_text(checker.stdout + checker.stderr)
                if checker.returncode:
                    problems.append(stage + ' illegal')
                vals = {k: float(v) for k, v in re.findall(r'^(Total displacement|Maximum displacement|Total HPWL): ([0-9.eE+-]+)', checker.stdout, flags=re.M)}
                checked[stage] = vals
            for key, stage, metric in ((KEYS[0], 'legal', 'Total displacement'), (KEYS[1], 'legal', 'Maximum displacement'), (KEYS[2], 'legal', 'Total HPWL'), (KEYS[3], 'sa', 'Total HPWL')):
                a, b = reported.get(key), checked[stage].get(metric)
                if a is None or b is None or abs(a - b) > max(0.11, 1e-6 * abs(b)):
                    problems.append(key + ' disagrees with independent checker')
            if reported.get(KEYS[3], float('inf')) > reported.get(KEYS[2], 0) + 1e-5:
                problems.append('HPWL worsened')
            record = {'solution': label, 'bench': bench.stem, 'verdict': 'FAIL' if problems else 'PASS', 'seconds': round(elapsed, 3), **dict(zip(('disp', 'max', 'hpwl_legal', 'hpwl_final'), (reported.get(k) for k in KEYS))), 'problems': '; '.join(problems)}
            records.append(record)
            with (out / 'records.csv').open('w', newline='') as f:
                writer = csv.DictWriter(f, fieldnames=record.keys())
                writer.writeheader()
                writer.writerows(records)
            print(f'{label:18s} {bench.stem:6s} {record["verdict"]} {elapsed:6.2f}s HPWL={record["hpwl_final"]}', flush=True)
    if any(r['verdict'] != 'PASS' for r in records):
        raise SystemExit('A comparison run failed; inspect results/comparison/records.csv')
    print(f'{len(records)}/{len(records)} verified runs passed.', flush=True)


if __name__ == '__main__':
    main()
