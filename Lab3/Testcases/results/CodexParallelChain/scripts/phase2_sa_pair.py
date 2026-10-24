#!/usr/bin/env python3
"""Checkpointed, same-session SA pairing; --prepare builds without timing runs.

These are isolated SA protocols (no polish or Stage 1 charge), not the final
fixed/equal/default end-to-end protocols. Fixed auto is the HANDOFF B SA gate.
"""
import argparse
import csv
import fcntl
import hashlib
import io
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import time
import traceback

sys.dont_write_bytecode = True
from v6q import LAB, R, PIN, PY, Queue, build, note
from diag import prefix_hashes, original_prefix, start_prefix, sha, verify_protected

SELF = Path(__file__).resolve()
CASES = ['ibm01', 'ibm05', 'h06', 'h07', 'h08']
MODES = {
    'fixed_single': dict(moves='5000000', seconds='100000', threads='1'),
    'fixed_auto': dict(moves='3000000', seconds='100000', threads='0'),
    'wall10_auto': dict(moves='default', seconds='10', threads='0'),
}


def dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + '.tmp')
    tmp.write_text(json.dumps(value, indent=2) + '\n')
    tmp.replace(path)


def src_hash(path):
    return hashlib.sha256(b''.join(p.read_bytes() for p in sorted(path.iterdir()))).hexdigest()


def protocol(tag):
    return dict(tag=tag, script_sha256=sha(SELF),
                src_sha256=src_hash(LAB / 'AI_Solutions' / R.name / 'src'),
                reference_sha256=src_hash(LAB / 'AI_Solutions/ClaudeOpus55_v6/src'),
                driver_base_sha256=sha(LAB / 'AI_Solutions' / R.name / 'tests/sa_bench.cpp'),
                cases=CASES, seeds=[1, 2, 3], modes=MODES,
                fixed_repeats=2, polish_seconds=0,
                fixed_note='Moves per band; actual attempt counts recorded, placement-dependent rounding allowed between versions. Starvation heuristic off for fixed work only.',
                starts={b: prefix_hashes(start_prefix(b)) for b in CASES},
                inputs={b: prefix_hashes(original_prefix(b)) for b in CASES})


def prepare(d, tag):
    verify_protected()
    p = protocol(tag)
    path = d / 'protocol.json'
    if path.exists() and json.loads(path.read_text()) != p:
        raise RuntimeError('Protocol/input/source changed: use a new tag, never reuse results')
    dump(path, p)
    # Common driver, identical for both source trees. Persist exact internal
    # placement and floating-point result for reproducibility, excluding times.
    driver = (LAB / 'AI_Solutions' / R.name / 'tests/sa_bench.cpp').read_text()
    driver = driver.replace('double start=wallSeconds();',
        'if(std::string(argv[5])!="default") opt.starvedAttemptsPerCell=0;\n'
        '        std::printf("threads,%d\\n",saThreads(db,opt));\n'
        '        double start=wallSeconds();')
    driver = driver.replace('writePl(db,p,std::string(argv[3])+".pl");',
        'if(!writePl(db,p,std::string(argv[3])+".pl")) throw std::runtime_error("writePl failed");\n'
        '        std::ofstream state(std::string(argv[3])+".state");\n'
        '        state << std::hexfloat << stats.initial << " " << stats.best << " " << totalHPWL(db,p) << " " << stats.attempts << " " << stats.accepted << "\\n";\n'
        '        for(size_t i=0;i<p.seg.size();++i) state << p.seg[i] << " " << p.site[i] << "\\n";\n'
        '        if(!state) throw std::runtime_error("state write failed");')
    (d / 'driver.cpp').write_text(driver)
    q = Queue(d / 'build_jobs', timed=False)
    for version, name in [('v6', 'ClaudeOpus55_v6'), ('candidate', R.name)]:
        snapshot = d / version
        if not (snapshot / 'src').exists():
            shutil.copytree(LAB / 'AI_Solutions' / name / 'src', snapshot / 'src')
            shutil.copy(LAB / 'AI_Solutions' / name / 'CMakeLists.txt', snapshot / 'CMakeLists.txt')
        expected = p['src_sha256' if version == 'candidate' else 'reference_sha256']
        if src_hash(snapshot / 'src') != expected:
            raise RuntimeError('Snapshot hash mismatch')
        exe = snapshot / 'sa_bench'
        marker = q.out / (version + '.done.json')
        if not marker.exists():
            build(name, snapshot / 'build', src=snapshot)
        command = PIN + ['g++', '-std=c++17', '-O3', '-DNDEBUG', '-pthread',
                         '-I' + str(snapshot / 'src'), d / 'driver.cpp',
                         snapshot / 'build/libdp_core.a', '-o', exe]
        if not q.run(version, command, quiet=True):
            raise RuntimeError('Driver build failed')
    dump(d / 'prepared.json', dict(protocol_sha256=sha(path), driver_sha256=sha(d / 'driver.cpp'),
                                  binaries={v: sha(d / v / 'sa_bench') for v in ['v6', 'candidate']}))


def worker(d, mode, bench, seed, version, repeat):
    dest = d / mode / bench / ('seed' + seed) / version / repeat
    dest.mkdir(parents=True, exist_ok=True)
    settings = MODES[mode]
    cmd = [d / version / 'sa_bench', original_prefix(bench), start_prefix(bench),
           dest / 'output', seed, settings['moves'], settings['seconds'], '0', settings['threads']]
    start = time.monotonic()
    with (dest / 'stdout.txt').open('w') as out, (dest / 'stderr.txt').open('w') as err:
        subprocess.run([str(x) for x in cmd], stdout=out, stderr=err, check=True, timeout=240)
    elapsed = time.monotonic() - start
    with (dest / 'check.txt').open('w') as out:
        subprocess.run([PY, LAB / 'Grader/tools/check_pl.py', original_prefix(bench),
                        dest / 'output.pl', '--quiet'], stdout=out, stderr=subprocess.STDOUT, check=True)
    lines = (dest / 'stdout.txt').read_text().splitlines()
    index = lines.index('hpwl,attempts,sa_seconds,stage2_seconds')
    metrics = next(csv.DictReader(io.StringIO('\n'.join(lines[index:index + 2]))))
    metrics = {k: int(v) if k == 'attempts' else float(v) for k, v in metrics.items()}
    metrics.update(threads=int(next(x for x in lines if x.startswith('threads,')).split(',')[1]),
                   process_seconds=elapsed, placement_sha256=sha(dest / 'output.pl'),
                   state_sha256=sha(dest / 'output.state'), legal=True)
    if metrics['attempts'] <= 0:
        raise RuntimeError('No SA work')
    dump(dest / 'metrics.json', metrics)


def summarize(d):
    rows, details, repeats = [], {}, []
    for mode in MODES:
        details[mode] = {}
        for bench in CASES:
            ratios, hpwl = [], {'v6': [], 'candidate': []}
            for seed in [1, 2, 3]:
                values = {}
                for version in hpwl:
                    base = d / mode / bench / f'seed{seed}' / version
                    m = json.loads((base / '1/metrics.json').read_text())
                    values[version] = m
                    hpwl[version].append(m['hpwl'])
                    rows.append(dict(mode=mode, bench=bench, seed=seed, version=version, **m))
                    if mode.startswith('fixed'):
                        again = json.loads((base / '2/metrics.json').read_text())
                        same = all(m[k] == again[k] for k in ['placement_sha256', 'state_sha256', 'threads', 'attempts', 'hpwl'])
                        repeats.append(dict(mode=mode, bench=bench, seed=seed, version=version, identical=same))
                if values['v6']['threads'] != values['candidate']['threads']:
                    raise RuntimeError('Different automatic thread counts')
                ratios.append(values['candidate']['hpwl'] / values['v6']['hpwl'])
            details[mode][bench] = dict(ratio_mean=statistics.mean(ratios),
                ratio_min=min(ratios), ratio_max=max(ratios),
                hpwl={v: dict(mean=statistics.mean(a), min=min(a), max=max(a)) for v, a in hpwl.items()})
    with (d / 'records.csv').open('w') as out:
        w = csv.DictWriter(out, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
    gm = {mode: math.exp(statistics.mean(math.log(v['ratio_mean']) for v in benches.values()))
          for mode, benches in details.items()}
    fixed = details['fixed_auto']
    gate = gm['fixed_auto'] <= .997 and max(x['ratio_max'] for x in fixed.values()) <= 1.005
    result = dict(by_protocol=details, geometric_mean_ratios=gm, reproducibility=repeats,
                  quality_gate_pass=gate, all_fixed_identical=all(r['identical'] for r in repeats),
                  note='SA gate only. TSan and final B checks remain separate; no automatic retain decision.')
    dump(d / 'summary.json', result)
    lines = ['# SA same-round paired results', '',
             'Three seeds per case; ratio = candidate / v6. No polish; wall10 is isolated SA, not end-to-end equal/default.', '',
             '| Protocol | Bench | Ratio mean | Range | v6 HPWL mean [min, max] | Candidate HPWL mean [min, max] |',
             '|---|---|---:|---|---|---|']
    for mode, benches in details.items():
        for bench, data in benches.items():
            h = data['hpwl']
            desc = [f"{h[v]['mean']:.6f} [{h[v]['min']:.6f}, {h[v]['max']:.6f}]" for v in ['v6', 'candidate']]
            lines.append(f"| {mode} | {bench} | {data['ratio_mean']:.8f} | {data['ratio_min']:.8f}–{data['ratio_max']:.8f} | {desc[0]} | {desc[1]} |")
    lines += ['', *[f'- {mode} GM: {value:.8f}' for mode, value in gm.items()],
              f'- Fixed-auto quality gate: {gate}', f"- Fixed-work identical: {result['all_fixed_identical']}"]
    (d / 'compare.md').write_text('\n'.join(lines) + '\n')


def run(d, tag):
    if not (d / 'prepared.json').exists():
        prepare(d, tag)
    if json.loads((d / 'protocol.json').read_text()) != protocol(tag):
        raise RuntimeError('Protocol changed after preparation')
    prepared = json.loads((d / 'prepared.json').read_text())
    if prepared['protocol_sha256'] != sha(d / 'protocol.json') or prepared['driver_sha256'] != sha(d / 'driver.cpp'):
        raise RuntimeError('Prepared files changed')
    for v, digest in prepared['binaries'].items():
        if sha(d / v / 'sa_bench') != digest:
            raise RuntimeError('Binary changed')
    verify_protected()
    q = Queue(d / 'jobs')
    with (R.parent / 'ClaudeOpus55_v6/timing.lock').open('r') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for mode in MODES:
            for bench in CASES:
                for seed in [1, 2, 3]:
                    versions = ['v6', 'candidate'] if seed % 2 else ['candidate', 'v6']
                    for repeat in ([1, 2] if mode.startswith('fixed') else [1]):
                        for version in versions:
                            args = [mode, bench, str(seed), version, str(repeat)]
                            if not q.run('_'.join(args), [PY, SELF, tag, '--worker', *args], quiet=True):
                                raise RuntimeError('SA worker failed: ' + ' '.join(args))
        summarize(d)
        verify_protected(d / 'protected_after.json')
        dump(d / 'ALL_DONE', dict(protocol_sha256=sha(d / 'protocol.json'), finished=time.ctime()))
    note(f'phase2/sa_pair/{tag} completed; see compare.md and summary.json')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag')
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--worker', nargs=5)
    args = parser.parse_args()
    directory = R / 'phase2/sa_pair' / args.tag
    directory.mkdir(parents=True, exist_ok=True)
    try:
        if args.worker:
            worker(directory, *args.worker)
        elif args.prepare:
            prepare(directory, args.tag)
        else:
            run(directory, args.tag)
    except Exception as error:
        dump(directory / 'FAILED.json', dict(error=str(error), traceback=traceback.format_exc()))
        raise
