"""phases.py — cut a time-stamped `dp -v` stderr into phases.

Each phase ends at the last line it prints; its duration is the time since the
previous phase ended. Works for ClaudeOpus55_v4, CodexChainDP and v5 (v5 also
prints a `[time]` summary, which is used when present).
"""
import csv
import os
import re

PHASES = ['read', 'legalize', 'pareto', 'windowdp', 'chain', 'global', 'sa', 'polish']
FIELDS = ['t_' + p for p in PHASES] + ['t_stage1', 't_stage2', 'sa_threads', 'sa_attempts', 'sa_internal_s',
                                       'sa_failed_pct', 'sa_timeup', 'global_tries', 'global_accepted', 'global_unknown']
CHAIN = ('[target-window]', '[chain]', '[chain-round]', ': ')
POLISH = ('[polish]', '[window-hpwl]', '[order-hpwl]', '[polish-total]')


def classify(lines):
    """yields (time, phase, line)"""
    cur = 'read'
    for t, l in lines:
        if l.startswith('[read]'):
            cur = 'read'
        elif l.startswith(('[legal]', '[spread]')) and cur in ('read', 'legalize'):
            cur = 'legalize'
        elif l.startswith('[pareto]'):
            cur = 'pareto'
        elif l.startswith(('[window-disp]', '[window-max]')) and cur in ('read', 'legalize'):
            cur = 'legalize'
        elif l.startswith(('[window-disp]', '[window-max]')) and cur == 'pareto':
            cur = 'windowdp'
        elif l.startswith(CHAIN) and cur in ('pareto', 'windowdp', 'chain'):
            cur = 'chain'
        elif l.startswith('[global-cap]') or l.startswith('[global]'):
            cur = 'global'
        elif l.startswith('[sa]'):
            cur = 'sa'
        elif l.startswith(POLISH) and cur in ('sa', 'polish', 'global', 'chain', 'windowdp'):
            cur = 'polish'
        yield t, cur, l


def parse_phases(lines, trace=None):
    rec = {}
    end = {}
    t_stage1 = t_stage2 = None
    for t, ph, l in classify(lines):
        if l.startswith('[stage1] done'):
            t_stage1 = t
            end[ph] = t
            continue
        if l.startswith('[stage2] done'):
            t_stage2 = t
            continue
        end[ph] = t
    prev = 0.0
    for ph in PHASES:
        if ph in end:
            rec['t_' + ph] = round(end[ph] - prev, 3)
            prev = end[ph]
    if t_stage2 is not None:
        # polish = from the last SA line to [stage2] done
        last = end.get('polish', end.get('sa', t_stage1))
        if 'polish' in end:
            rec['t_polish'] = round(t_stage2 - end.get('sa', t_stage1 or 0), 3)
    rec['t_stage1'] = t_stage1
    rec['t_stage2'] = t_stage2
    att = None
    for _, l in lines:
        m = re.match(r'\[sa\] parallel: (\d+) bands', l)
        if m:
            rec['sa_threads'] = int(m.group(1))
        m = re.match(r'\[sa\] (\d+) sync rounds, (\d+) attempts \(([\d.]+)% proposals failed\)\s+([\d.]+)s', l)
        if m:
            att = int(m.group(2))
            rec['sa_failed_pct'] = float(m.group(3))
            rec['sa_internal_s'] = float(m.group(4))
        m = re.match(r'\[sa\] proposals failed ([\d.]+)%', l)
        if m:
            rec['sa_failed_pct'] = float(m.group(1))
        if l.startswith('[sa] time limit reached'):
            rec['sa_timeup'] = 1
        m = re.match(r'\[sa\] step\s+\d+ .* ([\d.]+)s$', l)
        if m and 'sa_threads' not in rec:
            rec['sa_internal_s'] = float(m.group(1))
    if 'sa_threads' not in rec and any(l.startswith('[sa] HPWL') for _, l in lines):
        rec['sa_threads'] = 1
    if att is None and trace and os.path.exists(trace):
        try:
            with open(trace) as f:
                rows = list(csv.reader(f))
            if len(rows) > 1:
                att = int(float(rows[-1][0]))
        except (OSError, ValueError, IndexError):
            pass
    if att is not None:
        rec['sa_attempts'] = att
    # SA seconds as seen from outside: [stage1] done -> last SA line
    if t_stage1 is not None and 'sa' in end:
        rec['sa_seconds'] = round(end['sa'] - t_stage1, 3)
    g = [l for _, l in lines if l.startswith('[global-cap]')]
    rec['global_tries'] = len(g)
    rec['global_accepted'] = sum('accepted' in l for l in g)
    rec['global_unknown'] = sum('unresolved' in l for l in g)
    return rec


FIELDS.append('sa_seconds')
