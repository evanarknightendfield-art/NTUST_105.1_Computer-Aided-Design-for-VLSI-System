import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPORT = os.path.dirname(HERE)
LAB2 = os.path.dirname(REPORT)

sys.path.insert(0, os.path.join(LAB2, 'Grader', 'grader', 'grader'))
sys.path.insert(0, os.path.join(LAB2, 'Grader', 'tools'))

VERSIONS = [
    ('v1', 'Construction', 'MyCode/v1_legal_initial_construction'),
    ('v2', 'Hill climbing', 'MyCode/v2_hill_climbing'),
    ('v3', 'Simulated annealing', 'MyCode/v3_simulated_annealing'),
    ('v4', 'Incremental SA', 'MyCode/v4_incremental'),
    ('v5', 'Balanced + swap', 'MyCode/v5_balanced_swap'),
]

PLOT_VERSIONS = ['v2', 'v4', 'v5']
TREE_VERSIONS = ['v1', 'v2', 'v4', 'v5']

COLORS = {'v2': '#2a78d6', 'v4': '#eb6834', 'v5': '#1baf7a'}
PART_COLORS = {'cost': '#2a78d6', 'skew': '#eb6834'}
INK = '#0b0b0b'
INK2 = '#52514e'
MUTED = '#898781'
GRID = '#e1e0d9'
SURFACE = '#fcfcfb'

BUILD_DIR = os.path.join(REPORT, 'build')
INPUT_DIR = os.path.join(LAB2, 'Testcases')
RESULT_DIR = os.path.join(REPORT, 'results')
TABLE_DIR = os.path.join(REPORT, 'tables')
FIGURE_DIR = os.path.join(REPORT, 'figures')

STAGE = re.compile(r'^\[(stage\d)\](.*)$', re.M)
KV = re.compile(r'(\w+)=(-?[\d.]+)')
FINAL = re.compile(r'T_max: (-?\d+), T_min: (-?\d+), Score: (-?\d+)')


def binary(tag):
    return os.path.join(BUILD_DIR, tag, 'cbi')


def case_ids(input_dir):
    ids = []
    for f in sorted(os.listdir(input_dir)):
        m = re.fullmatch(r'input(\d{3})\.cbi', f)
        if m:
            ids.append(m.group(1))
    return ids


def parse_log(path):
    text = open(path, encoding='utf-8', errors='replace').read()
    stages = {}
    for name, rest in STAGE.findall(text):
        kv = {k: float(v) for k, v in KV.findall(rest)}
        stages[name] = kv
    m = FINAL.search(text)
    final = None
    if m:
        final = dict(tmax=int(m.group(1)), tmin=int(m.group(2)), score=int(m.group(3)))
    return stages, final


def read_input(path):
    from cbi_explain import parse_input
    lim, lib, w, pos = parse_input(path)
    sinks = [v for k, v in pos.items() if k != 'SRC']
    return lim, lib, w, pos['SRC'], sinks


def lower_bounds(path):
    lim, lib, w, src, sinks = read_input(path)
    n = len(sinks)
    ratios = [c / (f - 1) for f, _, c in lib.values() if f >= 2]
    need = max(0, n - lim['fanout'])
    cost_lb = -(-need * min(ratios) // 1) if ratios and need else 0
    parities = {(x + y) % 2 for x, y in sinks}
    skew_lb = 1 if len(parities) > 1 else 0
    return int(cost_lb), skew_lb, int(cost_lb + w * skew_lb), n, w
