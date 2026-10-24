#!/usr/bin/env python3
"""Phase 1 only: serial, resumable diagnostics; ALL_DONE requires every job.

--prepare compiles and checks inputs without running a benchmark.
The default invocation is intended for nohup; it never changes solution code.
"""
import argparse
import csv
import fcntl
import hashlib
import io
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time
import traceback

sys.dont_write_bytecode = True
from v6q import LAB, PIN, PY, R, Queue, build, note

P = R / 'phase1'
SOLUTION = LAB / 'AI_Solutions' / R.name
SELF = Path(__file__).resolve()
BASE_RESULTS = R.parent / 'ClaudeOpus55_v6'
STARTS = BASE_RESULTS / 'final/hpwl_fixed/legal_starts/ClaudeOpus55_v4'
BIN = P / 'bin'
TIMED_CPUS = '0-19'
AUTO_MOVES = 3_000_000
SA_SECONDS = '100000'
SA_CASES = ['ibm01', 'ibm05', 'h06', 'h07', 'h08']
SEEDS = [1, 2]
EDGE_CASES = ['e08_fragmented', 'e16_rowgaps']
PHASES = ['target_windows', 'ejection_chains', 'post_chain_dp', 'chain_stage']


def dump(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n')
    temporary.replace(path)


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def tree_hashes(root):
    return {str(p.relative_to(root)): sha(p)
            for p in sorted(root.rglob('*')) if p.is_file()}


def verify_protected(output=None):
    original = json.loads((R / 'provenance/protected_before.json').read_text())
    current = {n: tree_hashes(LAB / 'AI_Solutions' / n) for n in original}
    if current != original:
        raise RuntimeError('Protected directories changed; do not write ALL_DONE')
    if output:
        dump(output, current)


def cases():
    training = [(group, aux.stem, aux.with_suffix(''))
                for group in ['testcases', 'holdout']
                for aux in sorted((LAB / 'Testcases' / group).glob('*/*.aux'))]
    if len(training) != 11:
        raise RuntimeError(f'Expected 11 testcases + holdout, found {len(training)}')
    return training + [('edge_diagnostic_only', name,
                        LAB / 'Testcases/edge' / name / name) for name in EDGE_CASES]


def original_prefix(bench):
    group = 'testcases' if bench in ['ibm01', 'ibm05', 'toy'] else 'holdout'
    return LAB / 'Testcases' / group / bench / bench


def start_prefix(bench):
    original = STARTS / bench / 'output'
    if original.with_suffix('.aux').is_file() and original.with_suffix('.pl').is_file():
        return original
    if bench not in ['ibm01', 'ibm05']:
        raise RuntimeError(f'Required supplied legal start missing: {original}')
    return P / 'sa/legal_starts/ClaudeOpus55_v4' / bench / 'output'


def prefix_hashes(prefix):
    aux = prefix.with_suffix('.aux')
    if not aux.is_file():
        raise RuntimeError(f'Missing auxiliary file: {aux}')
    paths = [aux] + [aux.parent / token for token in aux.read_text().split(':', 1)[1].split()]
    return {str(path): {'resolved': str(path.resolve()), 'sha256': sha(path)} for path in paths}


def check_static():
    verify_protected()
    baseline = LAB / 'AI_Solutions/ClaudeOpus55_v6'
    for name in ['src', 'tests']:
        if tree_hashes(baseline / name) != tree_hashes(SOLUTION / name):
            raise RuntimeError(f'Baseline copy differs: {name}')
    for name in ['CMakeLists.txt', 'Makefile']:
        if sha(baseline / name) != sha(SOLUTION / name):
            raise RuntimeError(f'Baseline copy differs: {name}')
    expected = tree_hashes(baseline / 'src')
    instrumented = tree_hashes(P / 'instrumented/src')
    text = (P / 'instrumented/src/main.cpp').read_text()
    stripped = ''.join(line for line in text.splitlines(keepends=True) if '[diag-chain]' not in line)
    if stripped != (baseline / 'src/main.cpp').read_text() or text.count('[diag-chain]') != 8:
        raise RuntimeError('Chain instrumentation is not exactly eight added fprintf calls')
    instrumented['main.cpp'] = expected['main.cpp']
    if instrumented != expected:
        raise RuntimeError('Instrumented core sources differ')
    driver = (P / 'sa_bench_diag.cpp').read_text()
    if ''.join(line for line in driver.splitlines(keepends=True) if '[diag-sa]' not in line) != (SOLUTION / 'tests/sa_bench.cpp').read_text():
        raise RuntimeError('SA driver differs beyond its diagnostic fprintf')
    for _, _, prefix in cases():
        prefix_hashes(prefix)
    missing = []
    for bench in SA_CASES:
        start = start_prefix(bench)
        if start.is_relative_to(P):
            missing.append(bench)
        else:
            prefix_hashes(start)
    plan = {
        'chain_cases': [{'group': group, 'bench': bench, 'input': str(prefix)} for group, bench, prefix in cases()],
        'chain_variants': {'default': [], 'chain6': ['--chain-time', '6']},
        'chain_common_args': ['-v', '--no-sa', '--cap-time', '0'],
        'sa_cases': SA_CASES, 'seeds': SEEDS, 'auto_moves_per_band': AUTO_MOVES,
        'sa_time_limit': SA_SECONDS, 'polish_seconds': 0,
        'serial_budget': 'the exact attempts reported by the paired auto run',
        'serial_rounding': 'unchanged 100-step scheduler may execute 0-99 fewer attempts; reported explicitly',
        'missing_supplied_starts': missing,
        'fallback': 'unmodified v4 -v --no-sa, generated only under new results directory',
        'timed_cpus': TIMED_CPUS, 'analysis_build_cpus': '16-19',
        'timed_steps': 26 + 20 + 2 + len(missing),
    }
    dump(P / 'plan.json', plan)
    return missing


def build_one(name):
    BIN.mkdir(exist_ok=True)
    if name in ['v4', 'v5', 'v6']:
        version = R.name if name == 'v6' else 'ClaudeOpus55_' + name
        build(version, P / 'build' / name)
        return
    source = P / 'instrumented/src/main.cpp' if name == 'chain' else P / 'sa_bench_diag.cpp'
    executable = BIN / ('dp_chain_diag' if name == 'chain' else 'sa_bench')
    subprocess.run(PIN + ['g++', '-std=c++17', '-O3', '-DNDEBUG', '-pthread',
                         '-I' + str(SOLUTION / 'src'), str(source),
                         str(P / 'build/v6/libdp_core.a'), '-o', str(executable)], check=True)


def must_run(queue, name, command):
    if not queue.run(name, command, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1')):
        raise RuntimeError(f'Failed job: {name}; see {queue.out / (name + ".log")}')


def prepare():
    missing = check_static()
    queue = Queue(P / 'build_jobs', timed=False)
    for name in ['v6', 'chain', 'sa', 'v5'] + (['v4'] if missing else []):
        must_run(queue, 'build_' + name, PIN + [PY, SELF, '--build', name])
    executables = [P / 'build/v6/dp', P / 'build/v5/dp', BIN / 'dp_chain_diag', BIN / 'sa_bench']
    if missing:
        executables.append(P / 'build/v4/dp')
    dump(P / 'preflight.done.json', {'executables': {str(p): sha(p) for p in executables},
                                    'script_sha256': sha(SELF), 'benchmarks_executed': 0})
    note('phase1 啟動前檢查完成：編譯成功、輸入有效、插樁僅 fprintf；尚未執行診斷。')


def capture(command, directory):
    directory.mkdir(parents=True, exist_ok=True)
    command = ['taskset', '-c', TIMED_CPUS] + [str(arg) for arg in command]
    started = time.monotonic()
    with (directory / 'stdout.txt').open('w') as out, (directory / 'stderr.txt').open('w') as err:
        completed = subprocess.run(command, cwd=directory, stdin=subprocess.DEVNULL,
                                   stdout=out, stderr=err, timeout=1800)
    dump(directory / 'execution.json', {'command': command, 'returncode': completed.returncode,
                                       'wall_seconds': time.monotonic() - started})
    completed.check_returncode()


def parse_chain(directory):
    pattern = re.compile(r'\[diag-chain\] round=(\d+) phase=(\w+) event=(begin|end) time=([\d.eE+-]+) total=([\d.eE+-]+) max=([\d.eE+-]+) work=(-?\d+)')
    opened, rows = {}, []
    for match in pattern.finditer((directory / 'stderr.txt').read_text()):
        round_number, phase, event, clock, total, maximum, work = match.groups()
        key = (int(round_number), phase)
        data = {'time': float(clock), 'total': float(total), 'max': float(maximum), 'work': int(work)}
        if event == 'begin':
            if key in opened:
                raise RuntimeError(f'Duplicate phase begin: {key}')
            opened[key] = data
        else:
            before = opened.pop(key)
            rows.append({'round': key[0], 'phase': phase, 'seconds': data['time'] - before['time'],
                         'total_before': before['total'], 'total_after': data['total'],
                         'total_reduction': before['total'] - data['total'],
                         'max_before': before['max'], 'max_after': data['max'], 'work': data['work']})
    if opened or sum(row['phase'] == 'chain_stage' for row in rows) != 1:
        raise RuntimeError('Incomplete chain instrumentation')
    if any(row['seconds'] < 0 or row['total_reduction'] < -1e-4 for row in rows):
        raise RuntimeError('Invalid phase time or displacement reduction')
    stage = next(row for row in rows if row['phase'] == 'chain_stage')
    if abs(sum(row['total_reduction'] for row in rows if row['phase'] != 'chain_stage') - stage['total_reduction']) > 1e-3:
        raise RuntimeError('Phase displacement reductions do not reconcile')
    dump(directory / 'phases.json', rows)


def sa_metrics(directory):
    lines = (directory / 'stdout.txt').read_text().splitlines()
    index = lines.index('hpwl,attempts,sa_seconds,stage2_seconds')
    row = next(csv.DictReader(io.StringIO('\n'.join(lines[index:index + 2]))))
    result = {key: int(value) if key == 'attempts' else float(value) for key, value in row.items()}
    match = re.search(r'\[diag-sa\] threads=(\d+) requested_moves=(\d+)', (directory / 'stderr.txt').read_text())
    if not match or result['attempts'] <= 0:
        raise RuntimeError('Missing SA count/thread diagnostics')
    result.update(threads=int(match[1]), requested_moves=int(match[2]))
    dump(directory / 'metrics.json', result)
    return result


def worker(arguments):
    kind, *args = arguments
    if kind == 'chain':
        bench, variant = args
        prefix = next(prefix for _, name, prefix in cases() if name == bench)
        directory = P / 'chain' / variant / bench
        command = [BIN / 'dp_chain_diag', prefix, directory / 'output', '-v', '--no-sa', '--cap-time', '0']
        if variant == 'chain6':
            command += ['--chain-time', '6']
        capture(command, directory)
        parse_chain(directory)
    elif kind == 'start':
        bench, = args
        prefix = start_prefix(bench)
        capture([P / 'build/v4/dp', original_prefix(bench), prefix, '-v', '--no-sa'], prefix.parent)
        prefix_hashes(prefix)
    elif kind == 'sa':
        bench, seed, mode, moves = args
        directory = P / 'sa' / bench / ('seed' + seed) / mode
        capture([BIN / 'sa_bench', original_prefix(bench), start_prefix(bench), directory / 'output',
                 seed, moves, SA_SECONDS, '0', '0' if mode == 'auto' else '1'], directory)
        sa_metrics(directory)
    elif kind == 'toy':
        version, = args
        directory = P / 'toy' / version
        capture([P / 'build' / version / 'dp', original_prefix('toy'), directory / 'output', '-v', '--no-sa'], directory)
    else:
        raise RuntimeError(f'Unknown worker: {kind}')


def write_csv(name, rows):
    with (P / name).open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def summarize():
    rounds, totals = [], []
    for group, bench, _ in cases():
        for variant in ['default', 'chain6']:
            rows = json.loads((P / 'chain' / variant / bench / 'phases.json').read_text())
            initial = next(row['total_before'] for row in rows if row['phase'] == 'chain_stage')
            rounds.extend(dict(group=group, bench=bench, variant=variant, **row) for row in rows)
            for phase in PHASES:
                selected = [row for row in rows if row['phase'] == phase]
                reduction = sum(row['total_reduction'] for row in selected)
                totals.append(dict(group=group, bench=bench, variant=variant, phase=phase,
                                   seconds=sum(row['seconds'] for row in selected), total_reduction=reduction,
                                   reduction_pct_of_chain_start=100 * reduction / initial if initial else 0,
                                   calls=len(selected), work=sum(row['work'] for row in selected if row['work'] >= 0)))
    pairs = []
    for bench in SA_CASES:
        for seed in SEEDS:
            directory = P / 'sa' / bench / ('seed' + str(seed))
            auto = json.loads((directory / 'auto/metrics.json').read_text())
            single = json.loads((directory / 'single/metrics.json').read_text())
            delta = auto['attempts'] - single['attempts']
            if single['requested_moves'] != auto['attempts'] or single['threads'] != 1 or not 0 <= delta < 100:
                raise RuntimeError(f'Unmatched SA workload for {bench} seed {seed}: {delta}')
            pairs.append(dict(bench=bench, seed=seed, auto_threads=auto['threads'], auto_attempts=auto['attempts'],
                              single_requested=single['requested_moves'], single_attempts=single['attempts'],
                              schedule_rounding_attempts=delta, auto_hpwl=auto['hpwl'], single_hpwl=single['hpwl'],
                              auto_loss_pct=100 * (auto['hpwl'] / single['hpwl'] - 1),
                              auto_seconds=auto['sa_seconds'], single_seconds=single['sa_seconds'],
                              legal_start=str(start_prefix(bench))))
    toy = []
    for version, expected in [('v5', 1972.0), ('v6', 2004.4)]:
        output = (P / 'toy' / version / 'stdout.txt').read_text()
        def number(label):
            match = re.search(re.escape(label) + r'\s*([\d.,eE+-]+)', output)
            if not match:
                raise RuntimeError(f'Missing toy statistic: {version} {label}')
            return float(match[1].replace(',', ''))
        maximum = number('Maximum displacement (legal):')
        toy.append(dict(version=version, total_displacement=number('Total displacement (legal):'),
                        max_displacement=maximum, handoff_reference_max=expected, difference=maximum - expected,
                        stderr=str(P / 'toy' / version / 'stderr.txt')))
    write_csv('chain_rounds.csv', rounds)
    write_csv('chain_summary.csv', totals)
    write_csv('sa_pairs.csv', pairs)
    write_csv('toy_summary.csv', toy)
    by_bench = {}
    for bench in SA_CASES:
        values = [row['auto_loss_pct'] for row in pairs if row['bench'] == bench]
        by_bench[bench] = {'mean_loss_pct': statistics.mean(values), 'min_loss_pct': min(values), 'max_loss_pct': max(values)}
    dump(P / 'summary.json', {'chain': totals, 'sa_pairs': pairs, 'sa_by_bench': by_bench, 'toy': toy,
                             'scope': 'diagnosis only; two SA seeds; edge e08/e16 are not tuning data',
                             'sa_warning': 'positive loss means auto HPWL is worse; serial 100-step rounding is listed',
                             'starts': json.loads((P / 'sa/start_provenance.json').read_text())})


def run_queue():
    preflight = json.loads((P / 'preflight.done.json').read_text())
    if preflight['script_sha256'] != sha(SELF) or any(sha(path) != digest for path, digest in preflight['executables'].items()):
        raise RuntimeError('Prepared script/binaries changed; rerun --prepare')
    check_static()
    queue = Queue(P / 'jobs')
    # Coordinate with the original queue without truncating or modifying its lock.
    with (BASE_RESULTS / 'timing.lock').open('r') as common_lock:
        fcntl.flock(common_lock, fcntl.LOCK_EX)
        for _, bench, _ in cases():
            for variant in ['default', 'chain6']:
                must_run(queue, f'chain_{bench}_{variant}', [PY, SELF, '--worker', 'chain', bench, variant])
        for bench in SA_CASES:
            if start_prefix(bench).is_relative_to(P):
                must_run(queue, 'legal_start_' + bench, PIN + [PY, SELF, '--worker', 'start', bench])
        dump(P / 'sa/start_provenance.json', {bench: prefix_hashes(start_prefix(bench)) for bench in SA_CASES})
        for bench in SA_CASES:
            for seed in SEEDS:
                stem = f'sa_{bench}_s{seed}'
                must_run(queue, stem + '_auto', [PY, SELF, '--worker', 'sa', bench, str(seed), 'auto', str(AUTO_MOVES)])
                metrics = json.loads((P / 'sa' / bench / ('seed' + str(seed)) / 'auto/metrics.json').read_text())
                must_run(queue, stem + '_single', [PY, SELF, '--worker', 'sa', bench, str(seed), 'single', str(metrics['attempts'])])
        for version in ['v5', 'v6']:
            must_run(queue, 'toy_' + version, [PY, SELF, '--worker', 'toy', version])
        must_run(queue, 'summarize', PIN + [PY, SELF, '--summarize'])
        verify_protected(R / 'provenance/protected_after.json')
        dump(P / 'ALL_DONE', {'finished_unix': time.time(), 'chain_runs': 26, 'sa_runs': 20, 'toy_runs': 2,
                              'protected_sha256_unchanged': True, 'summary': str(P / 'summary.json')})
        (P / 'FAILED.json').unlink(missing_ok=True)
        note('phase1/ALL_DONE：全部診斷完成，保護資料夾 SHA256 未變；等待下一個 session 分析。')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--prepare', action='store_true')
    mode.add_argument('--build', choices=['v4', 'v5', 'v6', 'chain', 'sa'])
    mode.add_argument('--worker', nargs='+')
    mode.add_argument('--summarize', action='store_true')
    args = parser.parse_args()
    if args.prepare:
        prepare()
    elif args.build:
        build_one(args.build)
    elif args.worker:
        worker(args.worker)
    elif args.summarize:
        summarize()
    else:
        try:
            run_queue()
        except Exception as error:
            dump(P / 'FAILED.json', {'error': str(error), 'traceback': traceback.format_exc()})
            note(f'phase1 診斷失敗：{error}；未寫 ALL_DONE，詳見 phase1/FAILED.json。')
            raise


if __name__ == '__main__':
    main()
