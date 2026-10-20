#!/usr/bin/env python3
"""Sequential, reproducible comparison of eight AI baselines and CodexChainDP.

Every version gets effort=max(--effort, ceil(--min-attempts / movable_count)).
The default minimum is 5 million SA proposals; temperature-loop versions round
down to a multiple of 100. This is a common SA-work comparison, not equal total
work: deterministic legalization/polish differ and their time is measured.
v4, AugmentDP and ChainDP accept thread/stage-budget options; all use one
thread. Source, input, checker, compiler, binary and output hashes are retained.
--resume reuses ONLY PASS runs whose fingerprints and retained output hashes still match.
Seven deliverables are checked, then redundant benchmark copies are removed by
default. The manifest retains their hashes; --keep-copies preserves them too.
Two unfinished student skeletons are compiled/smoked separately, never ranked.
"""
import argparse
import csv
import fcntl
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
LAB = ROOT.parents[1]
# Lab3 convention: run outputs go to Lab3/Testcases/results/<solution>/ (not into the solution folder)
RESULTS = LAB / 'Testcases' / 'results' / ROOT.name
LABELS = ('ClaudeOpus55', 'CodexGapSA', 'ClaudeOpus55_v2', 'CodexParetoDP',
          'ClaudeOpus55_v3', 'CodexWindowDP', 'ClaudeOpus55_v4', 'CodexAugmentDP',
          'CodexChainDP')
PARALLEL = {'ClaudeOpus55_v4', 'CodexAugmentDP', 'CodexChainDP'}
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-pthread']
KEYS = ('Total displacement (legal)', 'Maximum displacement (legal)',
        'Total HPWL (legal)', 'Total HPWL (SA)')
FIELDS = ('solution', 'bench', 'seed', 'movable', 'effort', 'planned_attempts',
          'sa_attempts', 'work_complete', 'verdict', 'seconds', 'disp', 'max',
          'hpwl_legal', 'hpwl_final', 'returncode', 'problems', 'fingerprint')
OUTPUTS = tuple('output' + e for e in ('.aux', '.nodes', '.nets', '.wts', '.scl', '.pl', '_legal.pl'))
COPIES = ('output.aux', 'output.nodes', 'output.nets', 'output.wts', 'output.scl')
RETAINED = ('output.pl', 'output_legal.pl', 'trace.csv', 'trace.csv.polish.csv',
            'stdout.txt', 'stderr.txt', 'command.json', 'check_legal_stdout.txt',
            'check_legal_stderr.txt', 'check_sa_stdout.txt', 'check_sa_stderr.txt')
CHECKER = LAB / 'Grader/tools/check_pl.py'
CHECKER_SOURCE = CHECKER.read_bytes()
CHECKER_SHA256 = hashlib.sha256(CHECKER_SOURCE).hexdigest()
spec = importlib.util.spec_from_file_location('independent_check_pl', CHECKER)
checker = importlib.util.module_from_spec(spec)
exec(compile(CHECKER_SOURCE, str(CHECKER), 'exec'), checker.__dict__)


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')


def sources(directory):
    return {str(p.relative_to(LAB)): digest(p)
            for p in sorted((directory / 'src').rglob('*'))
            if p.is_file() and p.suffix in ('.cpp', '.hpp', '.h')}


def execute(command, directory, prefix, timeout):
    """Keep complete logs, including errors/timeouts, and continue the suite."""
    started = time.perf_counter()
    with (directory / (prefix + 'stdout.txt')).open('w') as stdout, \
         (directory / (prefix + 'stderr.txt')).open('w') as stderr:
        try:
            result = subprocess.run(command, cwd=directory, stdout=stdout,
                                    stderr=stderr, timeout=timeout, check=False)
            code, timed_out = result.returncode, False
        except subprocess.TimeoutExpired:
            code, timed_out = -999, True
        except OSError as exc:
            stderr.write(str(exc) + '\n')
            code, timed_out = -998, False
    return code, timed_out, time.perf_counter() - started


def build(label, directory, out, compiler, compiler_info, args):
    target = out / 'bin' / label
    target.parent.mkdir(parents=True, exist_ok=True)
    manifest_path = out / 'bin' / (label + '.json')
    source_hashes = sources(directory)
    snapshot = out / 'sources' / label / fingerprint(source_hashes)
    snapshot.mkdir(parents=True, exist_ok=True)
    for name, expected in source_hashes.items():
        original = LAB / name
        relative = original.relative_to(directory / 'src')
        frozen = snapshot / relative
        frozen.parent.mkdir(parents=True, exist_ok=True)
        if not frozen.is_file() or digest(frozen) != expected:
            shutil.copyfile(original, frozen)
        if digest(frozen) != expected:
            raise RuntimeError('source changed while snapshotting: ' + name)
    files = sorted(snapshot.glob('*.cpp'))
    command = [compiler, *FLAGS, *map(str, files), '-o', str(target)]
    configuration = {'sources': source_hashes, 'compiler': compiler_info,
                     'flags': FLAGS, 'command': command}
    config_hash = fingerprint(configuration)
    old = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    reusable = target.is_file() and old.get('fingerprint') == config_hash and old.get('binary_sha256') == digest(target)
    if args.skip_build and not reusable:
        raise RuntimeError('--skip-build requires a matching verified binary: ' + label)
    if not (reusable and (args.skip_build or args.resume)):
        code, timeout, seconds = execute(command, out, label + '_build_', args.build_timeout)
        old = {**configuration, 'fingerprint': config_hash, 'returncode': code,
               'timeout': timeout, 'seconds': seconds}
        if code == 0 and target.is_file():
            old['binary_sha256'] = digest(target)
        write_json(manifest_path, old)
    return target, old


def read_bench(bench):
    files = checker.read_aux(str(bench))
    nodes, gp = checker.read_nodes(files['nodes']), checker.read_pl(files['pl'])
    movable = [v for v, (_, _, terminal) in nodes.items()
               if not terminal and not gp[v][3]]
    hashes = {str(bench.relative_to(LAB)): digest(bench)}
    hashes.update({str(Path(p).resolve().relative_to(LAB)): digest(p) for p in files.values()})
    return {'files': files, 'nodes': nodes, 'gp': gp, 'movable': movable,
            'nets': checker.read_nets(files['nets']), 'hashes': hashes}


def exact_metrics(data, path):
    """Use the independent checker's readers, retaining full double precision.

    check_pl CLI prints one decimal place; these are recomputed from its parsed
    output after its independent legality checks, never from placer stdout.
    """
    placed = checker.read_pl(str(path))
    disps = [abs(placed[v][0] - data['gp'][v][0]) + abs(placed[v][1] - data['gp'][v][1])
             for v in data['movable']]
    hpwl = 0.0
    for net in data['nets']:
        if len(net) < 2:
            continue
        xs = [placed[v][0] + data['nodes'][v][0] / 2 for v in net]
        ys = [placed[v][1] + data['nodes'][v][1] / 2 for v in net]
        hpwl += max(xs) - min(xs) + max(ys) - min(ys)
    return {'disp': sum(disps), 'max': max(disps, default=0.0), 'hpwl': hpwl}


def flush_records(out, records):
    with (out / 'records.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(records.values())


def trace_attempts(path):
    if not path.exists():
        return None
    try:
        rows = list(csv.DictReader(path.open()))
        return int(rows[-1]['iteration']) if rows else None
    except (ValueError, KeyError):
        return None


def smoke_skeletons(out, compiler, compiler_info, args):
    results = []
    bench = LAB / 'Testcases/testcases/toy/toy.aux'
    for label, directory in (('M11502102', LAB / 'M11502102'),
                             ('my_dp_skeleton', LAB / 'MyCode/my_dp_skeleton')):
        binary, manifest = build(label, directory, out, compiler, compiler_info, args)
        directory = out / 'skeleton_smoke' / label
        directory.mkdir(parents=True, exist_ok=True)
        command = [str(binary), str(bench), 'output', '--no-sa']
        if manifest.get('returncode') != 0:
            results.append({'solution': label, 'status': 'BUILD_FAIL'})
            continue
        code, timeout, elapsed = execute(command, directory, '', 30)
        stderr = (directory / 'stderr.txt').read_text()
        results.append({'solution': label, 'status': 'TODO_NONWORKING' if code and 'TODO(M1)' in stderr else 'UNEXPECTED',
                        'returncode': code, 'timeout': timeout, 'seconds': elapsed,
                        'command': command, 'source_sha256': manifest['sources'],
                        'stderr': stderr})
    write_json(out / 'skeleton_smoke.json', results)
    return results


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--labels', default=','.join(LABELS))
    p.add_argument('--benches', default='')
    p.add_argument('--seeds', default='1')
    p.add_argument('--suite', default='fixed_work')
    p.add_argument('--effort', type=int, default=200)
    p.add_argument('--min-attempts', type=int, default=5_000_000)
    p.add_argument('--time', type=float, default=1000)
    p.add_argument('--stage1-time', type=float, default=1000)
    p.add_argument('--polish-time', type=float, default=1000)
    p.add_argument('--timeout', type=float, default=3600)
    p.add_argument('--build-timeout', type=float, default=300)
    p.add_argument('--compiler', default='g++')
    p.add_argument('--candidate-args', default='', help='additional arguments for CodexChainDP only')
    p.add_argument('--keep-copies', action='store_true', help='retain redundant .aux/.nodes/.nets/.wts/.scl after verifying all seven outputs')
    p.add_argument('--skip-build', action='store_true')
    p.add_argument('--resume', action='store_true')
    p.add_argument('--skip-skeletons', action='store_true')
    args = p.parse_args()
    labels = [x for x in args.labels.split(',') if x]
    if not labels or len(set(labels)) != len(labels) or any(x not in LABELS for x in labels):
        p.error('labels must be chosen from ' + ','.join(LABELS))
    if args.effort < 0 or args.min_attempts < 5_000_000 or any(not math.isfinite(x) or x <= 0 for x in (args.time, args.stage1_time, args.polish_time, args.timeout, args.build_timeout)):
        p.error('budgets must be positive; min-attempts must be at least 5000000')
    if Path(args.suite).name != args.suite or args.suite in ('.', '..'):
        p.error('--suite must be a single directory name')
    try:
        seeds = [int(s) for s in args.seeds.split(',')]
    except ValueError:
        p.error('--seeds must contain comma-separated nonnegative integers')
    if len(set(seeds)) != len(seeds) or any(s < 0 or s > 1_000_000_000 for s in seeds):
        p.error('--seeds must be unique integers in 0..1000000000')
    benches = sorted((LAB / 'Testcases/testcases').glob('*/*.aux')) + sorted((LAB / 'Testcases/holdout').glob('*/*.aux'))
    requested = {s for s in args.benches.split(',') if s}
    if requested:
        unknown = requested - {b.stem for b in benches}
        if unknown:
            p.error('unknown benchmarks: ' + ','.join(sorted(unknown)))
        benches = [b for b in benches if b.stem in requested]
    out = RESULTS / args.suite
    out.mkdir(parents=True, exist_ok=True)
    suite_lock = (out / '.compare.lock').open('a+')
    try:
        fcntl.flock(suite_lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        p.error('another comparison owns this suite; wait before reusing ' + str(out))
    frozen_checker = out / 'checker' / CHECKER_SHA256 / 'check_pl.py'
    frozen_checker.parent.mkdir(parents=True, exist_ok=True)
    frozen_checker.write_bytes(CHECKER_SOURCE)
    compiler_info = subprocess.run([args.compiler, '--version'], text=True, capture_output=True, check=True).stdout
    settings_path = out / 'settings.json'
    settings = json.loads(settings_path.read_text()) if settings_path.exists() and args.resume else {'invocations': []}
    settings['invocations'].append({'arguments': vars(args), 'command': [sys.executable, *sys.argv],
                                    'started_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())})
    settings.update({'compiler': compiler_info, 'flags': FLAGS, 'threads': 1,
                     'metric_source': 'independent check_pl parser, full precision; independent CLI legality check',
                     'comparison_scope': 'equal SA proposal budget, not equal total CPU time',
                     'checker_sha256': CHECKER_SHA256})
    settings.setdefault('sources', {})
    settings.setdefault('inputs', {})
    binaries, manifests = {}, {}
    for label in labels:
        binaries[label], manifests[label] = build(label, LAB / 'AI_Solutions' / label, out, args.compiler, compiler_info, args)
        settings['sources'][label] = manifests[label]['sources']
    data = {b.stem: read_bench(b) for b in benches}
    settings['inputs'].update({name: d['hashes'] for name, d in data.items()})
    write_json(settings_path, settings)
    if not args.skip_skeletons:
        smoke_skeletons(out, args.compiler, compiler_info, args)
    records = {}
    if args.resume and (out / 'records.csv').exists():
        with (out / 'records.csv').open() as f:
            for row in csv.DictReader(f):
                records[(row['solution'], row['bench'], int(row['seed']))] = row
    for bench in benches:
        n = len(data[bench.stem]['movable'])
        effort = max(args.effort, math.ceil(args.min_attempts / max(1, n)))
        for seed in seeds:
            for label in labels:
                if digest(CHECKER) != CHECKER_SHA256:
                    raise RuntimeError('checker changed during comparison; retained results use its preserved snapshot')
                if any(digest(LAB / name) != expected for name, expected in data[bench.stem]['hashes'].items()):
                    raise RuntimeError('benchmark changed during comparison: ' + str(bench))
                directory = out / label / f'{bench.stem}_s{seed}'
                directory.mkdir(parents=True, exist_ok=True)
                command = [str(binaries[label]), str(bench), 'output', '--seed', str(seed),
                           '--time', str(args.time), '--effort', str(effort), '--trace', 'trace.csv', '-v']
                if label in PARALLEL:
                    command += ['--threads', '1', '--stage1-time', str(args.stage1_time), '--polish-time', str(args.polish_time)]
                if label == 'CodexChainDP':
                    command += shlex.split(args.candidate_args)
                config = {'command': command, 'inputs': data[bench.stem]['hashes'], 'build': manifests[label],
                          'checker_sha256': settings['checker_sha256'], 'timeout': args.timeout}
                # Build timing is metadata, not a semantic change to a run.
                config['build'] = {k: v for k, v in manifests[label].items() if k not in ('seconds',)}
                run_hash = fingerprint(config)
                key = (label, bench.stem, seed)
                old = records.get(key, {})
                manifest_path = directory / 'run.json'
                old_manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
                hashes = old_manifest.get('output_sha256', {})
                expected_retained = {'output.pl', 'output_legal.pl', 'trace.csv', 'stdout.txt', 'stderr.txt', 'command.json'}
                if args.resume and old.get('verdict') == 'PASS' and old.get('fingerprint') == run_hash and old_manifest.get('fingerprint') == run_hash and expected_retained <= hashes.keys() and all((directory / name).is_file() and digest(directory / name) == h for name, h in hashes.items()) and (not args.keep_copies or all((directory / name).is_file() for name in COPIES)):
                    print(f'{label:18s} {bench.stem:6s} seed={seed} REUSED', flush=True)
                    continue
                # Do not accidentally measure a previous run after a failure.
                for filename in (*OUTPUTS, 'trace.csv', 'trace.csv.polish.csv'):
                    path = directory / filename
                    if path.is_file():
                        path.unlink()
                write_json(directory / 'command.json', command)
                problems, checked, reported = [], {}, {}
                if manifests[label].get('returncode') != 0:
                    code, timeout, elapsed = manifests[label]['returncode'], False, 0.0
                    problems.append('build failed')
                else:
                    code, timeout, elapsed = execute(command, directory, '', args.timeout)
                    stdout = (directory / 'stdout.txt').read_text()
                    for line in stdout.splitlines():
                        name, _, value = line.rpartition(':')
                        if name in KEYS:
                            try:
                                reported[name] = float(value)
                            except ValueError:
                                pass
                    if code != 0 or len(stdout.splitlines()) != 4 or len(reported) != 4:
                        problems.append('program failed / invalid stdout')
                    for name in OUTPUTS:
                        if not (directory / name).is_file():
                            problems.append('missing ' + name)
                    # Verify the supplied companion files, not just their presence.
                    for ext in ('nodes', 'nets', 'wts', 'scl'):
                        companion = directory / ('output.' + ext)
                        original = data[bench.stem]['files'].get(ext)
                        if companion.is_file() and original and digest(companion) != digest(original):
                            problems.append('output.' + ext + ' differs from input')
                    if (directory / 'output.aux').is_file():
                        try:
                            aux_files = checker.read_aux(str(directory / 'output.aux'))
                            for ext in ('nodes', 'nets', 'wts', 'pl', 'scl'):
                                if ext not in aux_files or Path(aux_files[ext]).resolve() != (directory / ('output.' + ext)).resolve():
                                    problems.append('output.aux has wrong ' + ext + ' reference')
                        except (OSError, ValueError, KeyError, SystemExit) as exc:
                            problems.append('invalid output.aux: ' + str(exc))
                    for stage, filename in (('legal', 'output_legal.pl'), ('sa', 'output.pl')):
                        check_command = [sys.executable, str(frozen_checker), str(bench), str(directory / filename), '--quiet']
                        check_code, check_timeout, _ = execute(check_command, directory, 'check_' + stage + '_', 120)
                        if check_code != 0:
                            problems.append(stage + ' independent check failed' + (' (timeout)' if check_timeout else ''))
                        else:
                            checked[stage] = exact_metrics(data[bench.stem], directory / filename)
                    for name, stage, metric in ((KEYS[0], 'legal', 'disp'), (KEYS[1], 'legal', 'max'),
                                                (KEYS[2], 'legal', 'hpwl'), (KEYS[3], 'sa', 'hpwl')):
                        a, b = reported.get(name), checked.get(stage, {}).get(metric)
                        if a is None or b is None or not math.isfinite(a) or abs(a-b) > max(0.11, 1e-6*abs(b)):
                            problems.append(name + ' disagrees with independent checker')
                    if checked.get('sa', {}).get('hpwl', math.inf) > checked.get('legal', {}).get('hpwl', 0.0) + 1e-5:
                        problems.append('final HPWL worsened')
                attempts = trace_attempts(directory / 'trace.csv')
                planned = n * effort
                expected_attempts = planned if label == 'CodexGapSA' else (planned // 100) * 100
                work_complete = attempts == expected_attempts
                stderr = (directory / 'stderr.txt').read_text() if (directory / 'stderr.txt').exists() else ''
                # A PASS here certifies legality/metrics. Incomplete work remains
                # explicit, so time-capped rows cannot masquerade as fixed work.
                if timeout:
                    problems.append('process timeout')
                verdict = 'TIMEOUT' if timeout else ('FAIL' if problems else 'PASS')
                row = dict(zip(FIELDS, [None] * len(FIELDS)))
                row.update(solution=label, bench=bench.stem, seed=seed, movable=n, effort=effort,
                           planned_attempts=planned, sa_attempts=attempts, work_complete=work_complete,
                           verdict=verdict, seconds=round(elapsed, 6), disp=checked.get('legal', {}).get('disp'),
                           max=checked.get('legal', {}).get('max'), hpwl_legal=checked.get('legal', {}).get('hpwl'),
                           hpwl_final=checked.get('sa', {}).get('hpwl'), returncode=code,
                           problems='; '.join(problems), fingerprint=run_hash)
                all_output_hashes = {name: digest(directory / name) for name in OUTPUTS if (directory / name).is_file()}
                # Only delete generated redundant copies, and only after a passing
                # seven-file check. Retained hashes make --resume independent of
                # those deliberately pruned files without weakening integrity.
                pruned = []
                if verdict == 'PASS' and not args.keep_copies:
                    for name in COPIES:
                        (directory / name).unlink()
                        pruned.append(name)
                retained = RETAINED + (COPIES if args.keep_copies else ())
                output_hashes = {name: digest(directory / name) for name in retained if (directory / name).is_file()}
                write_json(manifest_path, {**config, 'fingerprint': run_hash, 'record': row,
                                          'reported_metrics': reported, 'independent_metrics': checked,
                                          'output_sha256': output_hashes, 'all_deliverable_sha256': all_output_hashes,
                                          'pruned_redundant_copies': pruned, 'expected_attempts': expected_attempts,
                                          'time_limit_message': 'time limit reached' in stderr})
                records[key] = row
                flush_records(out, records)
                print(f'{label:18s} {bench.stem:6s} seed={seed} {verdict:7s} {elapsed:8.2f}s HPWL={row["hpwl_final"]} attempts={attempts}', flush=True)
    selected = [r for key, r in records.items() if key[0] in labels and key[1] in data and key[2] in seeds]
    failed = sum(r['verdict'] != 'PASS' for r in selected)
    incomplete = sum(str(r['work_complete']).lower() != 'true' for r in selected)
    print(f'{len(selected)-failed}/{len(selected)} selected runs passed; {incomplete} have incomplete/different SA work. Results: {out}', flush=True)
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
