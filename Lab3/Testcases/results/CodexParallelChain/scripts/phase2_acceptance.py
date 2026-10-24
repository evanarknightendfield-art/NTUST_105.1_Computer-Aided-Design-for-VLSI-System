#!/usr/bin/env python3
"""Final B validation with production sources and same-round v6 pairs."""
import argparse
import csv
import fcntl
import hashlib
import json
import math
import re
import shutil
import subprocess
import sys
import time
import traceback
from pathlib import Path

sys.dont_write_bytecode = True
from v6q import LAB, R, PY, V5S, Queue, build, note
from diag import sha, verify_protected, original_prefix, prefix_hashes

SELF = Path(__file__).resolve()
SOLUTION = LAB / 'AI_Solutions' / R.name
BASE = LAB / 'AI_Solutions/ClaudeOpus55_v6'
CASES = ['ibm01', 'ibm05', 'toy'] + [f'h{i:02d}' for i in range(1, 9)]
LARGE = [('L2', 1), ('L2', 2), ('L1', 1)]


def dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + '.tmp')
    tmp.write_text(json.dumps(value, indent=2) + '\n')
    tmp.replace(path)


def src_hash(path):
    return hashlib.sha256(b''.join(p.read_bytes() for p in sorted(path.iterdir()))).hexdigest()


def prefix(mode, bench):
    if mode == 'train':
        return LAB / 'Testcases/edge' / bench / bench if bench.startswith('e') else original_prefix(bench)
    return LAB / 'Testcases/large' / bench / bench


def protocol(mode, tag):
    return dict(mode=mode, tag=tag, script_sha256=sha(SELF),
                wrapper_sha256=sha(SELF.parent / f'phase2_{mode}.py'),
                helper_sha256={p: sha(SELF.parent / p) for p in ['v6q.py', 'diag.py']},
                src_sha256=src_hash(SOLUTION / 'src'), reference_sha256=src_hash(BASE / 'src'),
                cmake_sha256={v: sha(s / 'CMakeLists.txt') for v, s in [('candidate', SOLUTION), ('v6', BASE)]},
                runner_sha256=sha(V5S / 'run_ts.py'), checker_sha256=sha(LAB / 'Grader/tools/check_pl.py'),
                inputs={b: prefix_hashes(prefix(mode, b)) for b in (CASES if mode == 'train' else ['L1', 'L2'])},
                cases=CASES if mode == 'train' else LARGE,
                historical_chaindp_sha256=sha(R.parent / 'ClaudeOpus55_v6/final/lg_CodexChainDP/record.csv'),
                gates=dict(total_gm_at_most=1.0011, max_gm_at_most=1.001, each_max_at_most=1.002,
                           both_at_least=8, extra_budget_overshoots=0, L1_seconds_lt=240, L2_seconds_lt=600),
                note='Final session rollback: only the accepted SA boundary change remains. '
                     'Cap deadline cancellation and repairAt budget changes were rejected after round 2. '
                     'User explicitly requests final checks on the restored source; this is not another tuning round. '
                     'Coordinator WORK_STATE note 2 (2026-10-23 14:35) permits 0.11% total Stage 1 run variation; '
                     'max GM 0.1% and each max 0.2% gates stay unchanged (stricter than the allowed max-GM variation). '
                     'No elapsed-time tolerance: all candidate and reference training extra steps must be within budget. '
                     'Known v6 L2 extra overrun is recorded separately, never silently converted to a passing gate. '
                     'Historical ChainDP only defines both; every v6 ratio uses this run. Large runs are legality/runtime checks, not HPWL evidence.')


def prepare(d, mode, tag):
    verify_protected()
    p = json.loads(json.dumps(protocol(mode, tag)))
    if (d / 'protocol.json').exists() and json.loads((d / 'protocol.json').read_text()) != p:
        raise RuntimeError('Protocol changed; do not reuse completed evidence')
    dump(d / 'protocol.json', p)
    if (d / 'prepared.json').exists():
        validate(d, mode, tag)
        return
    binaries = {}
    for version, source in ([('v6', BASE), ('candidate', SOLUTION)] if mode == 'train' else [('candidate', SOLUTION)]):
        snapshot = d / version
        if not (snapshot / 'src').exists():
            shutil.copytree(source / 'src', snapshot / 'src')
        if src_hash(snapshot / 'src') != src_hash(source / 'src'):
            raise RuntimeError('Stale source snapshot')
        shutil.copy2(source / 'CMakeLists.txt', snapshot / 'CMakeLists.txt')
        binaries[version] = sha(build(version, snapshot / 'build', src=snapshot))
    dump(d / 'prepared.json', dict(protocol_sha256=sha(d / 'protocol.json'), binaries=binaries))


def validate(d, mode, tag):
    p = json.loads((d / 'protocol.json').read_text())
    if p != json.loads(json.dumps(protocol(mode, tag))):
        raise RuntimeError('Protocol/source/input changed after preparation')
    prepared = json.loads((d / 'prepared.json').read_text())
    if prepared['protocol_sha256'] != sha(d / 'protocol.json'):
        raise RuntimeError('Prepared protocol mismatch')
    for v, digest in prepared['binaries'].items():
        expected = p['src_sha256'] if v == 'candidate' else p['reference_sha256']
        if sha(d / v / 'build/dp') != digest or src_hash(d / v / 'src') != expected:
            raise RuntimeError('Binary or snapshot changed')


def worker(d, mode, bench, version, seed):
    root = d / 'runs' / (bench if mode == 'train' else f'{bench}_s{seed}') / version
    root.mkdir(parents=True, exist_ok=True)
    args = '--no-sa' if mode == 'train' else f'--seed {seed}'
    limit = 240 if mode == 'train' else 900
    subprocess.run([str(x) for x in [PY, V5S / 'run_ts.py', '--bin', d / version / 'build/dp',
                   '--out', root, '--timeout', limit, '--dp-args=' + args, prefix(mode, bench)]],
                   check=True, timeout=limit + 60)
    records = list(csv.DictReader((root / 'record.csv').open()))
    if len(records) != 1 or records[0]['status'] != 'OK':
        raise RuntimeError('Run failed or timed out')
    case = root / bench
    stderr = (case / 'stderr_ts.tsv').read_text()
    if re.search(r'invariant failed|step skipped|legalization failed|TIMEOUT', stderr):
        raise RuntimeError('Guard fallback, timeout or invariant failure')
    extra = re.findall(r'\[extra\] budget ([0-9.]+) used ([0-9.]+)', stderr)
    if len(extra) != 1:
        raise RuntimeError('Missing extra-step budget evidence')
    budget, used = map(float, extra[0])
    legal = {}
    for filename in ['output_legal.pl', 'output.pl']:
        with (case / (filename + '.check.txt')).open('w') as log:
            subprocess.run([str(x) for x in [PY, LAB / 'Grader/tools/check_pl.py', prefix(mode, bench),
                           case / filename, '--quiet']], stdout=log, stderr=subprocess.STDOUT, check=True)
        legal[filename] = sha(case / filename)
    dump(root / 'metrics.json', dict(record=records[0], legal=True, placements=legal,
                                    extra_budget=budget, extra_used=used, extra_overshoot=used > budget))


def gm(values):
    return math.exp(sum(map(math.log, values)) / len(values))


def ratio(a, b):
    return a / b if b else (1.0 if a == 0 else float('inf'))


def summarize(d, mode):
    rows = []
    if mode == 'train':
        historical = {x['bench']: x for x in csv.DictReader(
            (R.parent / 'ClaudeOpus55_v6/final/lg_CodexChainDP/record.csv').open())}
        for bench in CASES:
            data = {v: json.loads((d / 'runs' / bench / v / 'metrics.json').read_text()) for v in ['candidate', 'v6']}
            a, b = [data[v]['record'] for v in ['candidate', 'v6']]
            old = historical[bench]
            rows.append(dict(bench=bench, total_ratio=ratio(float(a['disp']), float(b['disp'])),
                             max_ratio=ratio(float(a['max']), float(b['max'])),
                             both=float(a['max']) <= float(old['max']) + 1e-9 and float(a['disp']) <= float(old['disp']) * 1.005 + 1e-9,
                             candidate=a, v6=b, extra_overshoots=sum(x['extra_overshoot'] for x in data.values())))
        total, maximum = gm([x['total_ratio'] for x in rows]), gm([x['max_ratio'] for x in rows])
        gates = dict(total_gm_within_011pct_variation=total <= 1.0011, max_gm_not_worse_01pct=maximum <= 1.001,
                     every_max_not_worse_02pct=all(x['max_ratio'] <= 1.002 for x in rows),
                     both_at_least_8=sum(x['both'] for x in rows) >= 8,
                     zero_extra_overshoots=not any(x['extra_overshoots'] for x in rows))
        result = dict(rows=rows, total_gm=total, max_gm=maximum, both=sum(x['both'] for x in rows), gates=gates)
        lines = ['# Final B: same-round production Stage 1', '', '| Case | Total candidate/v6 | Max candidate/v6 | Both vs historical ChainDP |', '|---|---:|---:|:---:|']
        lines += [f"| {x['bench']} | {x['total_ratio']:.8f} | {x['max_ratio']:.8f} | {x['both']} |" for x in rows]
        lines += ['', f'Total GM {total:.8f}; max GM {maximum:.8f}; both {result["both"]}/11.']
    else:
        for bench, seed in LARGE:
            data = json.loads((d / 'runs' / f'{bench}_s{seed}' / 'candidate/metrics.json').read_text())
            rows.append(dict(bench=bench, seed=seed, **data, time_limit=240 if bench == 'L1' else 600))
        gates = dict(all_legal=all(x['legal'] for x in rows),
                     all_within_time=all(float(x['record']['seconds']) < x['time_limit'] for x in rows),
                     zero_extra_overshoots=not any(x['extra_overshoot'] for x in rows))
        result = dict(rows=rows, gates=gates)
        lines = ['# Final B: full large runs', '', '| Case | Seed | Seconds | Limit (<) | Legal |', '|---|---:|---:|---:|:---:|']
        lines += [f"| {x['bench']} | {x['seed']} | {x['record']['seconds']} | {x['time_limit']} | {x['legal']} |" for x in rows]
    result['all_gates'] = all(gates.values())
    dump(d / 'summary.json', result)
    lines += ['', 'Gates:'] + [f'- {k}: {v}' for k, v in gates.items()]
    (d / 'compare.md').write_text('\n'.join(lines) + '\n')
    return result


def run(d, mode, tag, smoke=False):
    prepare(d, mode, tag)
    verify_protected()
    q = Queue(d / 'jobs')
    with (R.parent / 'ClaudeOpus55_v6/timing.lock').open('r') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        cases = [('toy', 1)] if smoke else ([(b, 1) for b in CASES] if mode == 'train' else LARGE)
        for i, (bench, seed) in enumerate(cases):
            versions = (['v6', 'candidate'] if i % 2 == 0 else ['candidate', 'v6']) if mode == 'train' else ['candidate']
            for v in versions:
                command = [PY, SELF.parent / f'phase2_{mode}.py', tag, '--worker', bench, v, seed]
                if not q.run(f'{bench}_s{seed}_{v}', command, quiet=True):
                    raise RuntimeError('Worker failed; inspect jobs log')
        if smoke:
            return
        result = summarize(d, mode)
        validate(d, mode, tag)
        verify_protected(d / 'protected_after.json')
        # Completion means results are ready, not automatic acceptance.
        dump(d / 'ALL_DONE', dict(protocol_sha256=sha(d / 'protocol.json'), all_gates=result['all_gates'], finished=time.ctime()))
    note(f'phase2/{mode}/{tag} completed; all_gates={result["all_gates"]}')


def main(mode):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag')
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--worker', nargs=3)
    args = parser.parse_args()
    if args.smoke and mode != 'train':
        parser.error('Smoke is only for training')
    d = R / 'phase2' / mode / args.tag
    try:
        if args.worker:
            validate(d, mode, args.tag)
            worker(d, mode, *args.worker)
        elif args.prepare:
            prepare(d, mode, args.tag)
        else:
            run(d, mode, args.tag, args.smoke)
    except Exception:
        dump(d / 'FAILED.json', dict(error=traceback.format_exc(), when=time.ctime()))
        raise
