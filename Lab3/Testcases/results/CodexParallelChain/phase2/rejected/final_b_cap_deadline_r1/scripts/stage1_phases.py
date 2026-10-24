#!/usr/bin/env python3
"""stage1_phases.py — Stage 1 phase times and cap-search attempts from run_ts.py directories.

For v5-family binaries (CodexCapDP / v6): base = read .. start of the extra steps,
chain = extra start .. last [chain-round]/[chain] line, cap = .. [extra] line
([extra] budget B used U gives the extra start exactly). For ChainDP the
[global-cap] lines mark its global search.
Also writes every [cap-local] attempt (D, method, repair s, DP s, raw, total, max, verdict).

usage: stage1_phases.py OUT_PREFIX RUN_DIR [RUN_DIR ...]   -> OUT_PREFIX_phases.csv, OUT_PREFIX_attempts.csv
"""
import csv, os, re, sys

CAPL = re.compile(r'\[cap-local\] D ([\d.]+) method (\d+) repair ([\d.]+) DP ([\d.]+) windows (\d+) remaining (\d+) '
                  r'raw ([\d.]+) total ([\d.]+) max ([\d.]+) (\w+)')
GLOB = re.compile(r'\[global-cap\] D ([\d.]+) (feasible: total ([\d.]+) \(([-+\d.]+)%\), max ([\d.]+), (.*)|unresolved)')


def parse(path):
    lines = []
    for ln in open(path):
        t, _, msg = ln.rstrip('\n').partition('\t')
        lines.append((float(t), msg))
    return lines


def main():
    out, dirs = sys.argv[1], sys.argv[2:]
    ph, at = [], []
    for d in dirs:
        run = os.path.basename(os.path.normpath(d))
        for b in sorted(os.listdir(d)):
            p = os.path.join(d, b, 'stderr_ts.tsv')
            if not os.path.exists(p):
                continue
            L = parse(p)
            r = dict(run=run, bench=b)
            for t, m in L:
                if m.startswith('[read]'):
                    r['t_read'] = t
                if m.startswith('[stage1] done'):
                    r['t_stage1'] = t
                mm = re.match(r'\[extra\] budget ([\d.]+) used ([\d.]+)', m)
                if mm:
                    r['extra_budget'], r['extra_used'] = float(mm.group(1)), float(mm.group(2))
                    r['t_extra_end'] = t
                    r['t_extra_start'] = t - float(mm.group(2))
                if m.startswith(('[chain-round]', '[chain]')):
                    r['t_chain_end'] = t
                mm = re.match(r'\[cap\] (\d+) attempts, (\d+) feasible, (\d+) accepted: max ([\d.]+) -> ([\d.]+), total ([\d.]+) -> ([\d.]+)', m)
                if mm:
                    r.update(cap_attempts=int(mm.group(1)), cap_feasible=int(mm.group(2)), cap_accepted=int(mm.group(3)),
                             precap_max=float(mm.group(4)), cap_max=float(mm.group(5)), precap_total=float(mm.group(6)),
                             cap_total=float(mm.group(7)))
                mm = CAPL.match(m)
                if mm:
                    g = mm.groups()
                    at.append(dict(run=run, bench=b, t=t, kind='cap-local', D=g[0], method=g[1], repair_s=g[2], dp_s=g[3],
                                   windows=g[4], remaining=g[5], raw=g[6], total=g[7], max=g[8], verdict=g[9]))
                mm = GLOB.match(m)
                if mm:
                    g = mm.groups()
                    at.append(dict(run=run, bench=b, t=t, kind='global-cap', D=g[0], total=g[2] or '', max=g[4] or '',
                                   verdict=(g[5] or 'unresolved').split(',')[0]))
                    r['t_global_end'] = t
                    r['global_tries'] = r.get('global_tries', 0) + 1
            if 't_extra_start' in r:
                r['base_s'] = round(r['t_extra_start'] - r.get('t_read', 0), 3)
                ce = r.get('t_chain_end', r['t_extra_start'])
                r['chain_s'] = round(max(0, ce - r['t_extra_start']), 3)
                r['cap_s'] = round(r['t_extra_end'] - max(ce, r['t_extra_start']), 3)
                r['extra_over'] = round(r['extra_used'] - r['extra_budget'], 6)
            ph.append(r)
    keys = ['run', 'bench', 't_read', 'base_s', 'chain_s', 'cap_s', 'extra_budget', 'extra_used', 'extra_over', 't_stage1',
            'cap_attempts', 'cap_feasible', 'cap_accepted', 'precap_max', 'cap_max', 'precap_total', 'cap_total',
            't_chain_end', 't_global_end', 'global_tries']
    with open(out + '_phases.csv', 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=keys, extrasaction='ignore')
        w.writeheader()
        w.writerows(ph)
    with open(out + '_attempts.csv', 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=['run', 'bench', 't', 'kind', 'D', 'method', 'repair_s', 'dp_s', 'windows', 'remaining',
                                          'raw', 'total', 'max', 'verdict'])
        w.writeheader()
        w.writerows(at)


if __name__ == '__main__':
    main()
