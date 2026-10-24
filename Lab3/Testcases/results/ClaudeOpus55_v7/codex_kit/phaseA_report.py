#!/usr/bin/env python3
"""phaseA_report.py — phaseA/summary.md from phaseA_queue.py's outputs (reads only; short on purpose)."""
import csv, math, re, statistics as st, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import R  # noqa: E402

P = R / 'phaseA'
L = ['# phase A 量測摘要（基準 = ClaudeOpus55_v7）', '']


def rec(p):
    return {r['bench']: r for r in csv.DictReader(open(p))} if Path(p).exists() else {}


def grep(path, pat):
    if not Path(path).exists():
        return []
    return [ln.rstrip('\n').split('\t', 1)[-1] for ln in open(path) if re.search(pat, ln)]


# mid-size designs
L += ['## M1–M3（v7 預設 dp；all = 不限制，P2 = taskset 2 顆 P-core）', '',
      '| bench | cfg | status | 秒 | Stage 1 | 額外 用/預算 | final rounds 跳過 | pareto 輪 | SA 執行緒 | SA 次數 | 總位移 | 最大 | HPWL |',
      '|---|---|---|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|']
for cfg in ['all', 'P2']:
    for b, r in rec(P / f'mid_{cfg}' / 'record.csv').items():
        e = grep(P / f'mid_{cfg}' / b / 'stderr_ts.tsv', r'\[extra\] budget')
        e = re.search(r'budget ([\d.]+) used ([\d.]+)', e[0]) if e else None
        skip = bool(grep(P / f'mid_{cfg}' / b / 'stderr_ts.tsv', r'final rounds skipped'))
        par = len(grep(P / f'mid_{cfg}' / b / 'stderr_ts.tsv', r'\[pareto\] pass'))
        L.append(f"| {b} | {cfg} | {r['status']} | {float(r['seconds']):.1f} | {float(r['t_stage1'] or 0):.1f} | "
                 f"{(e.group(2) + '/' + e.group(1)) if e else ''} | {'是' if skip else '否'} | {par} | {r.get('sa_threads', '')} | "
                 f"{float(r['sa_attempts'] or 0) / 1e6:.1f}M | {float(r['disp']):.6e} | {float(r['max']):.1f} | {float(r['hpwl_sa']):.6e} |")
L.append('')

# SA speed
L += ['## SA 速度（1 執行緒、固定 10M 次、v7 sa_bench2、共同起點 = v7 Stage 1）', '', '| bench | 次數 | SA 秒 | 次數/秒 |', '|---|---:|---:|---:|']
for p in sorted((P).glob('speed_*.log')):
    t = p.read_text().strip().splitlines()
    if 'hpwl,attempts,sa_seconds,stage2_seconds' in t:
        h, a, s, _ = t[t.index('hpwl,attempts,sa_seconds,stage2_seconds') + 1].split(',')
        L.append(f'| {p.stem[6:]} | {int(a) / 1e6:.1f}M | {float(s):.1f} | {int(a) / float(s) / 1e6:.3f}M |')
L.append('')

# profile
for b in ['h08', 'ibm01']:
    f = P / 'prof' / b / 'flat.txt'
    if f.exists():
        rows = [ln for ln in f.read_text().splitlines() if re.match(r'\s*\d+\.\d+\s', ln)][:15]
        L += [f'## gprof flat profile：{b}（前 15 個函式；% time、cumulative s、self s、calls、…、name）', '', '```'] + rows + ['```', '']

# noise floor
p = P / 'noise_default' / 'record.csv'
if p.exists():
    by = {}
    for r in csv.DictReader(open(p)):
        if r['verdict'] == 'PASS':
            by.setdefault((r['group'], r['bench']), {}).setdefault(r['version'], {})[int(r['seed'])] = float(r['hpwl'])
    L += ['## 預設 HPWL 協定的雜訊：v7（Stage 1 第二次）÷ v7（第一次），同原始碼，3 seeds', '',
          '| 分組 | GM（範圍 = 各 seed 的 GM） | 逐筆平均差最大 |', '|---|---|---|']
    for g in ['holdout', 'edge']:
        per, seeds, worst = [], {1: [], 2: [], 3: []}, (None, 0)
        for (gg, b), d in by.items():
            x, y = d.get('ClaudeOpus55_v7b'), d.get('ClaudeOpus55_v7')
            if gg != g or not x or not y or len(x) < 3 or len(y) < 3:
                continue
            m = st.mean(x.values()) / st.mean(y.values())
            per.append(m)
            if abs(m - 1) > abs(worst[1]):
                worst = (b, m - 1)
            for s in seeds:
                seeds[s].append(x[s] / y[s])
        gm = lambda v: math.exp(sum(map(math.log, v)) / len(v)) if v else float('nan')
        rs = [gm(v) for v in seeds.values() if v]
        if per:
            L.append(f'| {g} | {gm(per):.5f}（{min(rs):.5f}–{max(rs):.5f}，{len(per)} 筆） | {worst[0]} {worst[1]:+.3%} |')
    L.append('')
(P / 'summary.md').write_text('\n'.join(L) + '\n')
print('寫好', P / 'summary.md')
