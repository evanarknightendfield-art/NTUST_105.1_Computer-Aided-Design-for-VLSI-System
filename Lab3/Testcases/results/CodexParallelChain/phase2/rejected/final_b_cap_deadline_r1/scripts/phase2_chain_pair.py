#!/usr/bin/env python3
"""Checkpointed same-round Stage 1 and chain-budget pairs; no SA or edge tuning."""
import argparse
import csv
import fcntl
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import time
import traceback

sys.dont_write_bytecode = True
from v6q import LAB, R, PY, V5S, Queue, build, note
from diag import original_prefix, prefix_hashes, sha, verify_protected, parse_chain

SELF = Path(__file__).resolve()
SOLUTION = LAB / 'AI_Solutions' / R.name
BASE = LAB / 'AI_Solutions/ClaudeOpus55_v6'
CASES = ['ibm01', 'ibm05', 'toy'] + [f'h{i:02d}' for i in range(1, 9)]
MODES = {'stage1': '--no-sa', 'chain15': '--no-sa --cap-time 0 --chain-time 1.5',
         'chain30': '--no-sa --cap-time 0 --chain-time 3'}


def dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + '.tmp')
    tmp.write_text(json.dumps(value, indent=2) + '\n')
    tmp.replace(path)


def src_hash(path):
    return hashlib.sha256(b''.join(p.read_bytes() for p in sorted(path.iterdir()))).hexdigest()


def protocol(tag):
    return dict(tag=tag, script_sha256=sha(SELF), src_sha256=src_hash(SOLUTION / 'src'),
                reference_sha256=src_hash(BASE / 'src'), modes=MODES, cases=CASES,
                cmake_sha256=sha(SOLUTION / 'CMakeLists.txt'),
                runner_sha256=sha(V5S / 'run_ts.py'),
                instrumentation_sha256=sha(R / 'phase1/instrumented/src/main.cpp'),
                inputs={b: prefix_hashes(original_prefix(b)) for b in CASES},
                chain_reference_sha256=sha(R.parent / 'ClaudeOpus55_v6/final/lg_CodexChainDP/record.csv'),
                note='Stage 1 only; no HPWL quality claims. Candidate/v6 interleaved per case. '
                     'ChainDP historical reference only defines 8/11; all improvement ratios use paired v6.')


def prepare(d, tag):
    verify_protected()
    p = protocol(tag)
    if (d / 'protocol.json').exists() and json.loads((d / 'protocol.json').read_text()) != p:
        raise RuntimeError('Protocol changed; use a new tag')
    dump(d / 'protocol.json', p)
    # The same eight measurement lines used in phase 1. Strip them and demand
    # exact identity with each production main; never silently substitute code.
    instrumented = (R / 'phase1/instrumented/src/main.cpp').read_text()
    stripped = ''.join(x for x in instrumented.splitlines(keepends=True) if '[diag-chain]' not in x)
    assert instrumented.count('[diag-chain]') == 8
    binaries = {}
    for name, source in [('v6', BASE), ('candidate', SOLUTION)]:
        assert stripped == (source / 'src/main.cpp').read_text()
        root = d / name
        if not (root / 'src').exists():
            shutil.copytree(source / 'src', root / 'src')
        if src_hash(root / 'src') != src_hash(source / 'src'):
            raise RuntimeError('Snapshot changed')
        shadow = root / 'instrumented'
        if not (shadow / 'src').exists():
            shutil.copytree(root / 'src', shadow / 'src')
        (shadow / 'src/main.cpp').write_text(instrumented)
        shutil.copy2(source / 'CMakeLists.txt', shadow / 'CMakeLists.txt')
        exe = build(name, root / 'build', src=shadow)
        binaries[name] = sha(exe)
    dump(d / 'prepared.json', dict(protocol_sha256=sha(d / 'protocol.json'), binaries=binaries))


def worker(d, mode, bench, version):
    root = d / mode / bench / version
    root.mkdir(parents=True, exist_ok=True)
    command = [PY, V5S / 'run_ts.py', '--bin', d / version / 'build/dp', '--out', root,
               '--timeout', '240', '--dp-args=' + MODES[mode], original_prefix(bench)]
    subprocess.run([str(x) for x in command], check=True, timeout=270)
    records = list(csv.DictReader((root / 'record.csv').open()))
    if len(records) != 1 or records[0]['status'] != 'OK':
        raise RuntimeError('Run failed or timed out')
    case = root / bench
    with (case / 'check.txt').open('w') as log:
        subprocess.run([str(x) for x in [PY, LAB / 'Grader/tools/check_pl.py',
                        original_prefix(bench), case / 'output_legal.pl', '--quiet']],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    lines = [x.partition('\t')[2] for x in (case / 'stderr_ts.tsv').read_text().splitlines()]
    (case / 'stderr.txt').write_text('\n'.join(lines) + '\n')
    if any('invariant failed' in x or 'step skipped' in x for x in lines):
        raise RuntimeError('Guard or invariant failure; inspect stderr')
    parse_chain(case)
    phases = json.loads((case / 'phases.json').read_text())
    stage = next(x for x in phases if x['phase'] == 'chain_stage')
    dump(root / 'metrics.json', dict(record=records[0], legal=True, chain=stage,
                                    phases=phases, placement_sha256=sha(case / 'output_legal.pl')))


def gm(values):
    return math.exp(sum(math.log(x) for x in values) / len(values))


def ratio(a, b):
    return a / b if b else (1.0 if a == 0 else float('inf'))


def summarize(d):
    historic = {x['bench']: x for x in csv.DictReader(
        (R.parent / 'ClaudeOpus55_v6/final/lg_CodexChainDP/record.csv').open())}
    rows, both, max_ratios, total_ratios = [], 0, [], []
    times = dict(v6=0.0, candidate=0.0)
    for bench in CASES:
        data = {v: json.loads((d / 'stage1' / bench / v / 'metrics.json').read_text())
                for v in times}
        a, b = data['candidate']['record'], data['v6']['record']
        total, maximum = float(a['disp']), float(a['max'])
        rt, rm = ratio(total, float(b['disp'])), ratio(maximum, float(b['max']))
        if float(b['disp']) or total:
            total_ratios.append(rt)
        if float(b['max']) or maximum:
            max_ratios.append(rm)
        old = historic[bench]
        ok = maximum <= float(old['max']) + 1e-9 and total <= float(old['disp']) * 1.005 + 1e-9
        both += ok
        for v in times:
            times[v] += data[v]['chain']['seconds']
        rows.append(dict(bench=bench, total_ratio=rt, max_ratio=rm, both=ok,
                         candidate_total=total, v6_total=float(b['disp']),
                         candidate_max=maximum, v6_max=float(b['max']),
                         candidate_chain_seconds=data['candidate']['chain']['seconds'],
                         v6_chain_seconds=data['v6']['chain']['seconds']))
    budget = []
    for bench, required in [('h05', .0029), ('h06', .0044)]:
        data = {v: json.loads((d / m / bench / v / 'metrics.json').read_text())['chain']
                for v, m in [('candidate', 'chain15'), ('v6', 'chain30')]}
        gains = {v: x['total_reduction'] / x['total_before'] for v, x in data.items()}
        budget.append(dict(bench=bench, candidate_gain_fraction=gains['candidate'],
                           paired_v6_3s_gain_fraction=gains['v6'], historical_required=required,
                           candidate_seconds=data['candidate']['seconds'],
                           v6_seconds=data['v6']['seconds'],
                           pass_gate=gains['candidate'] >= max(required, gains['v6'])))
    total_gm, max_gm = gm(total_ratios), gm(max_ratios)
    gates = dict(both_at_least_8=both >= 8, total_improved_003pct=total_gm <= .9997,
                 max_gm_not_worse_01pct=max_gm <= 1.001,
                 every_max_not_worse_02pct=all(x['max_ratio'] <= 1.002 for x in rows),
                 chain_seconds_not_more=times['candidate'] <= times['v6'],
                 h05_h06_gain=all(x['pass_gate'] for x in budget))
    summary = dict(rows=rows, both=both, total_gm=total_gm, max_gm=max_gm,
                   chain_seconds=times, budgets=budget, gates=gates,
                   all_numeric_gates=all(gates.values()),
                   note='No automatic keep decision: inspect budget overshoot and tests independently.')
    dump(d / 'summary.json', summary)
    lines = ['# Chain target windows: same-round Stage 1 pairs', '',
             'Ratio = candidate / v6. No SA; HPWL is not a quality gate in this report.', '',
             '| Case | Total ratio | Max ratio | Chain seconds candidate / v6 | Both vs historical ChainDP |',
             '|---|---:|---:|---:|:---:|']
    for x in rows:
        lines.append(f"| {x['bench']} | {x['total_ratio']:.8f} | {x['max_ratio']:.8f} | "
                     f"{x['candidate_chain_seconds']:.6f} / {x['v6_chain_seconds']:.6f} | {x['both']} |")
    lines += ['', f'Both: {both}/11; total GM {total_gm:.8f}; max GM {max_gm:.8f}.',
              f'Chain seconds: {times}.', '', 'Candidate 1.5 s vs v6 3 s chain gain:']
    for x in budget:
        lines.append(f"- {x['bench']}: candidate {100*x['candidate_gain_fraction']:.6f}%, "
                     f"v6 {100*x['paired_v6_3s_gain_fraction']:.6f}%, "
                     f"historical requirement {100*x['historical_required']:.2f}%; pass {x['pass_gate']}; "
                     f"actual chain seconds {x['candidate_seconds']:.6f} / {x['v6_seconds']:.6f}.")
    lines += ['', 'Gates:'] + [f'- {k}: {v}' for k, v in gates.items()]
    (d / 'compare.md').write_text('\n'.join(lines) + '\n')


def run(d, tag):
    if not (d / 'prepared.json').exists():
        prepare(d, tag)
    if json.loads((d / 'protocol.json').read_text()) != protocol(tag):
        raise RuntimeError('Protocol changed after preparation')
    prepared = json.loads((d / 'prepared.json').read_text())
    if prepared['protocol_sha256'] != sha(d / 'protocol.json'):
        raise RuntimeError('Protocol hash changed')
    for v, digest in prepared['binaries'].items():
        if sha(d / v / 'build/dp') != digest:
            raise RuntimeError('Binary changed')
    verify_protected()
    q = Queue(d / 'jobs')
    with (R.parent / 'ClaudeOpus55_v6/timing.lock').open('r') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for i, bench in enumerate(CASES):
            for version in (['v6', 'candidate'] if i % 2 == 0 else ['candidate', 'v6']):
                if not q.run('stage1_' + bench + '_' + version,
                             [PY, SELF, tag, '--worker', 'stage1', bench, version], quiet=True):
                    raise RuntimeError('Stage 1 worker failed')
        for i, bench in enumerate(['h05', 'h06']):
            jobs = [('chain15', 'candidate'), ('chain30', 'v6')]
            for mode, version in (jobs if i == 0 else jobs[::-1]):
                if not q.run(mode + '_' + bench + '_' + version,
                             [PY, SELF, tag, '--worker', mode, bench, version], quiet=True):
                    raise RuntimeError('Chain budget worker failed')
        summarize(d)
        verify_protected(d / 'protected_after.json')
        dump(d / 'ALL_DONE', dict(protocol_sha256=sha(d / 'protocol.json'), finished=time.ctime()))
    note(f'phase2/chain_pair/{tag} completed; see compare.md and summary.json')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag')
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--worker', nargs=3)
    args = parser.parse_args()
    directory = R / 'phase2/chain_pair' / args.tag
    directory.mkdir(parents=True, exist_ok=True)
    try:
        if args.worker:
            worker(directory, *args.worker)
        elif args.prepare:
            prepare(directory, args.tag)
        else:
            run(directory, args.tag)
    except Exception:
        dump(directory / 'FAILED.json', dict(error=traceback.format_exc(), when=time.ctime()))
        raise
