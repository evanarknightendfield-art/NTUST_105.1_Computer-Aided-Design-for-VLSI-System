#!/usr/bin/env python3
"""Validate assumptions, then run the existing complete Stage 1 ILP model.

This certifies minimum TOTAL displacement subject to the supplied placement's
MAXIMUM displacement cap. It does not certify minimum maximum displacement,
the Pareto frontier, or Stage 2 HPWL. Requires NumPy and SciPy >= 1.9.

Example:
  python3 tests/certify.py ../../Testcases/testcases/toy/toy \
      ../../Testcases/results/CodexChainDP/smoke/toy/output_legal.pl \
      --out ../../Testcases/results/CodexChainDP/certificates/toy
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time

LAB3 = Path(__file__).resolve().parents[3]
TOOLS = LAB3 / 'Grader' / 'tools'
sys.path.insert(0, str(TOOLS))
from check_pl import read_aux, read_nodes, read_pl, read_scl, tokens


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inspect(bench, placement):
    files = read_aux(str(bench))
    nodes, gp = read_nodes(files['nodes']), read_pl(files['pl'])
    out, rows = read_pl(str(placement)), read_scl(files['scl'])
    errors = []
    if not rows:
        return {'errors': ['No placement rows.']}, files
    sp, rh = rows[0][3], rows[0][1]
    x0 = min(r[2] for r in rows)
    eps = 1e-7
    aligned = lambda x: abs(x - round(x)) <= eps
    if not (math.isfinite(sp) and sp > 0 and math.isfinite(rh) and rh > 0):
        return {'errors': ['Nonpositive or nonfinite row geometry.']}, files
    by_y = {}
    for y, height, origin, spacing, count in rows:
        if not all(math.isfinite(v) for v in (y, height, origin, spacing)):
            errors.append('Nonfinite row geometry.')
            continue
        if abs(spacing - sp) > eps or abs(height - rh) > eps:
            errors.append('The original exact model requires uniform spacing and height.')
        if not aligned((origin - x0) / sp):
            errors.append('Subrows are not on one common global site grid.')
        if count <= 0:
            errors.append('Empty subrow.')
        by_y.setdefault(y, []).append((origin, origin + count * spacing))
    ys = sorted(by_y)
    if any(b < a + rh - eps for a, b in zip(ys, ys[1:])):
        errors.append('Rows overlap vertically.')
    for intervals in by_y.values():
        ordered = sorted(intervals)
        if any(b[0] < a[1] - eps for a, b in zip(ordered, ordered[1:])):
            errors.append('Subrows overlap horizontally.')
    # read_scl drops Sitewidth. Check it explicitly: both the checker and exact
    # model use origin + NumSites * spacing as the right boundary.
    sitewidths = [float(t[2]) for line in Path(files['scl']).read_text().splitlines()
                  if (t := tokens(line)) and t[0].lower() == 'sitewidth']
    if not sitewidths or any(abs(w - sp) > eps for w in sitewidths):
        errors.append('Sitewidth must equal Sitespacing for this exact model.')
    if set(nodes) != set(out) or not set(nodes).issubset(gp):
        errors.append('Input/output node sets are incomplete or inconsistent.')
    fixed = {n for n, (_, _, terminal) in nodes.items()
             if terminal or (n in gp and gp[n][3])}
    movable = [n for n in nodes if n not in fixed]
    if not movable:
        errors.append('No movable cells; the original exact tool assumes at least one.')
    for n in movable:
        width, height, _ = nodes[n]
        if not (math.isfinite(width) and width > 0 and aligned(width / sp)):
            errors.append(f'{n}: movable width is not a positive integer number of sites.')
        if abs(height - rh) > eps:
            errors.append(f'{n}: movable height differs from row height.')
    for p in (gp, out):
        if any(not all(math.isfinite(v) for v in rec[:2]) for rec in p.values()):
            errors.append('Nonfinite placement coordinate.')
    metrics = None
    if not errors:
        ds = [abs(out[n][0] - gp[n][0]) + abs(out[n][1] - gp[n][1]) for n in movable]
        metrics = {'total_displacement': math.fsum(ds), 'maximum_displacement': max(ds)}
    return {'errors': sorted(set(errors)), 'movable_cells': len(movable),
            'rows': len(ys), 'subrows': len(rows), 'site_spacing': sp,
            'row_height': rh, 'input_metrics': metrics}, files


def check(bench, placement):
    p = subprocess.run([sys.executable, str(TOOLS / 'check_pl.py'), str(bench), str(placement)],
                       capture_output=True, text=True)
    return {'returncode': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr,
            'legal': p.returncode == 0 and p.stdout.startswith('LEGAL')}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bench', type=Path)
    ap.add_argument('placement', type=Path)
    ap.add_argument('--out', type=Path, help='default: Lab3/Testcases/results/CodexChainDP/certificates/<benchmark>')
    ap.add_argument('--time-limit', type=float, default=120, help='MILP solver time limit (seconds)')
    ap.add_argument('--overall-timeout', type=float, default=360, help='includes full enumeration and LP')
    a = ap.parse_args()
    if a.out is None:
        a.out = TOOLS.parents[1] / 'Testcases/results/CodexChainDP/certificates' / a.bench.name
    a.bench, a.placement, a.out = a.bench.resolve(), a.placement.resolve(), a.out.resolve()
    a.out.mkdir(parents=True, exist_ok=True)
    prerequisites, files = inspect(a.bench, a.placement)
    rec = {'scope': 'Minimum total L1 displacement with maximum L1 displacement capped at the supplied placement maximum + 1e-6.',
           'not_proven': ['Minimum maximum displacement', 'Unconstrained minimum total displacement', 'Stage 2 HPWL optimality'],
           'benchmark': str(a.bench), 'placement': str(a.placement),
           'model_assumptions': prerequisites, 'input_check': check(a.bench, a.placement),
           'sha256': {str(Path(p).resolve()): digest(p) for p in
                      [*files.values(), a.placement, TOOLS / 'exact_bound.py', TOOLS / 'check_pl.py']}}
    cert = a.out / 'certificate.json'
    if prerequisites['errors'] or not rec['input_check']['legal']:
        rec['status'] = 'REFUSED_INVALID_INPUT_OR_MODEL_ASSUMPTIONS'
        cert.write_text(json.dumps(rec, indent=2) + '\n')
        print(json.dumps({'status': rec['status'], 'certificate': str(cert)}))
        return 2
    optimum = a.out / 'optimum.pl'
    if optimum.exists():
        raise SystemExit(f'Refusing to reuse an existing certificate directory: {optimum}')
    cmd = [sys.executable, str(TOOLS / 'exact_bound.py'), str(a.bench), str(a.placement),
           '--cap', '-1', '--full', '--ilp', '--time-limit', str(a.time_limit),
           '--write-pl', str(optimum)]
    rec['command'] = cmd
    rec['maximum_displacement_cap'] = prerequisites['input_metrics']['maximum_displacement'] + 1e-6
    env = dict(os.environ)
    env['PYTHONUNBUFFERED'] = '1'
    t0 = time.monotonic()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=a.overall_timeout, env=env)
        stdout, stderr = p.stdout, p.stderr
        rec['returncode'] = p.returncode
    except subprocess.TimeoutExpired as e:
        stdout = e.stdout.decode() if isinstance(e.stdout, bytes) else (e.stdout or '')
        stderr = e.stderr.decode() if isinstance(e.stderr, bytes) else (e.stderr or '')
        rec['returncode'] = None
        rec['timed_out'] = True
    rec['seconds'] = time.monotonic() - t0
    log = a.out / 'solver.log'
    log.write_text('COMMAND: ' + json.dumps(cmd) + '\n\nSTDOUT\n' + stdout + '\nSTDERR\n' + stderr)
    rec['log_sha256'] = digest(log)
    lb = re.search(r'LP lower bound ([0-9.eE+-]+)', stdout)
    if lb:
        rec['lp_lower_bound_rounded_to_0_1'] = float(lb.group(1))
    marker = 'OPTIMAL (complete model, zero gap)' in stdout
    if optimum.exists():
        rec['optimum_check'] = check(a.bench, optimum)
        opt_assumptions, _ = inspect(a.bench, optimum)
        rec['optimum_metrics'] = opt_assumptions['input_metrics']
        rec['optimum_sha256'] = digest(optimum)
        feasible = (rec['optimum_check']['legal'] and rec['optimum_metrics'] is not None and
                    rec['optimum_metrics']['maximum_displacement'] <= rec['maximum_displacement_cap'] + 1e-7)
        rec['status'] = ('OPTIMAL_TOTAL_AT_FIXED_MAX_CAP' if marker and rec['returncode'] == 0 and feasible
                         else 'FEASIBLE_UPPER_BOUND_ONLY' if feasible else 'INVALID_SOLVER_OUTPUT')
        if feasible:
            rec['candidate_minus_incumbent_total'] = (prerequisites['input_metrics']['total_displacement'] -
                                                      rec['optimum_metrics']['total_displacement'])
            rec['candidate_matches_certified_optimum'] = (rec['status'] == 'OPTIMAL_TOTAL_AT_FIXED_MAX_CAP' and
                                                          abs(rec['candidate_minus_incumbent_total']) <= 1e-5)
    else:
        rec['status'] = 'NO_INTEGER_SOLUTION_RETURNED'
    rec['proof_caveat'] = 'Floating-point HiGHS certificate under checked model assumptions; not a formal rational-arithmetic proof. Rounded log bounds are not used as exact equality evidence.'
    cert.write_text(json.dumps(rec, indent=2) + '\n')
    print(json.dumps({'status': rec['status'], 'certificate': str(cert), 'seconds': rec['seconds'],
                      'candidate_gap': rec.get('candidate_minus_incumbent_total')}))
    return 0 if rec['status'] in ('OPTIMAL_TOTAL_AT_FIXED_MAX_CAP', 'FEASIBLE_UPPER_BOUND_ONLY') else 1


if __name__ == '__main__':
    raise SystemExit(main())
