#!/usr/bin/env python3
"""phase1_analyze.py — tables of phase 1 (v7 diagnosis of CodexParallelChain) -> phase1/analysis/*.md, *.csv.

Each section runs only when its data exists, so it can be re-run while the queue is still going.
Prints one line per section.
"""
import csv, json, math, re, statistics as st, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import R, LAB, PY  # noqa: E402

P = R / 'phase1'
A = P / 'analysis'
A.mkdir(parents=True, exist_ok=True)
CPC, V6, CH = 'CodexParallelChain', 'ClaudeOpus55_v6', 'CodexChainDP'
TRAIN = ['toy', 'ibm01', 'ibm05', 'h01', 'h02', 'h03', 'h04', 'h05', 'h06', 'h07', 'h08']


def rec(path):
    return {r['bench']: r for r in csv.DictReader(open(path))} if Path(path).exists() else {}


def f(x, d=None):
    try:
        return float(x)
    except (TypeError, ValueError):
        return d


def gm(xs):
    xs = [x for x in xs if x and x > 0]
    return math.exp(sum(map(math.log, xs)) / len(xs)) if xs else float('nan')


def lines(path):
    out = []
    for ln in open(path):
        t, _, m = ln.rstrip('\n').partition('\t')
        out.append((float(t), m))
    return out


def table(head, rows):
    s = '| ' + ' | '.join(head) + ' |\n|' + '|'.join('---' if i == 0 else '---:' for i in range(len(head))) + '|\n'
    for r in rows:
        s += '| ' + ' | '.join(str(x) for x in r) + ' |\n'
    return s


def extra_used(run_dir):
    p = Path(run_dir) / 'stderr_ts.tsv'
    if not p.exists():
        return None, None
    for _, m in lines(p):
        g = re.match(r'\[extra\] budget ([\d.]+) used ([\d.]+)', m)
        if g:
            return float(g.group(1)), float(g.group(2))
    return None, None


def starved(run_dir):
    p = Path(run_dir) / 'stderr_ts.tsv'
    return any(m.startswith('[sa] starved') for _, m in lines(p)) if p.exists() else False


# ---------------------------------------------------------------- 1. cores
def cores():
    cfgs = ['all', 'P4', 'P2', 'P4t4', 'P2t2']
    recs = {c: rec(P / 'cores' / c / 'record.csv') for c in cfgs}
    if not recs['all']:
        return 'cores: no data'
    rows, out = [], []
    for b in ['h08', 'L1', 'L2']:
        base = recs['all'].get(b)
        for c in cfgs:
            r = recs[c].get(b)
            if not r:
                continue
            bud, used = extra_used(P / 'cores' / c / b)
            h, h0 = f(r['hpwl_sa']), f(base['hpwl_sa']) if base else None
            limit = 240 if b == 'L1' else 600 if b == 'L2' else None
            row = dict(bench=b, cfg=c, status=r['status'], seconds=f(r['seconds']), t_read=f(r.get('t_read')),
                       t_legalize=f(r.get('t_legalize')), t_windowdp=f(r.get('t_windowdp')), t_stage1=f(r['t_stage1']),
                       extra_budget=bud, extra_used=used, sa_seconds=f(r.get('sa_seconds')),
                       sa_attempts=f(r.get('sa_attempts')), sa_threads=r.get('sa_threads'), starved=starved(P / 'cores' / c / b),
                       t_polish=f(r.get('t_polish')), disp=f(r['disp']), max=f(r['max']), hpwl=h,
                       hpwl_vs_all=(h / h0 if h and h0 else None), limit=limit)
            out.append(row)
            rows.append([b, c, r['status'], f"{row['seconds']:.1f}" + (' ⚠' if limit and row['seconds'] > limit else ''),
                         f"{row['t_stage1']:.1f}" if row['t_stage1'] else '', f"{row['t_legalize'] or 0:.1f}",
                         f"{row['t_windowdp'] or 0:.1f}",
                         f'{used:.2f}/{bud:.0f}' if used is not None else '', f"{row['sa_seconds'] or 0:.1f}",
                         f"{(row['sa_attempts'] or 0) / 1e6:.1f}M", row['sa_threads'], 'yes' if row['starved'] else '',
                         f"{row['t_polish'] or 0:.1f}", f"{row['disp']:.6g}", f"{row['max']:.6g}", f"{h:.6g}" if h else '',
                         f"{row['hpwl_vs_all']:.4f}" if row['hpwl_vs_all'] else ''])
    with open(A / 'cores.csv', 'w', newline='') as fh:
        w = csv.DictWriter(fh, fieldnames=list(out[0]))
        w.writeheader()
        w.writerows(out)
    (A / 'cores.md').write_text(
        '# 少核心（run_ts 預設 dp；all = 不限制，P4/P2 = taskset 4/2 顆 P-core 且自動 8 執行緒，P4t4/P2t2 = 同時 --threads 4/2）\n\n'
        + table(['bench', 'cfg', 'status', '總秒', 'Stage1', 'legalize', 'windowDP', 'extra 用/預算', 'SA 秒', 'SA 次數',
                 'SA 執行緒', 'starved', 'polish', '總位移', '最大位移', 'HPWL', 'HPWL÷all'], rows))
    return f'cores: {len(out)} runs'


# ---------------------------------------------------------------- 2. paired local_grade
def paired():
    have = {}
    for v in [CPC, V6, CH]:
        r = {}
        r.update(rec(P / f'lg_{v}' / 'record.csv'))
        r.update(rec(P / f'edge_{v}' / 'record.csv'))
        if r:
            have[v] = r
            with open(A / f'lg27_{v}.csv', 'w', newline='') as fh:
                w = csv.DictWriter(fh, fieldnames=list(next(iter(r.values()))))
                w.writeheader()
                w.writerows(r.values())
    if CH not in have:
        return 'paired: no ChainDP yet'
    args = [PY, R / 'scripts/compare_lg.py', A / 'lg27.md', CH, A / f'lg27_{CH}.csv']
    for v in [CPC, V6]:
        if v in have:
            args += [v, A / f'lg27_{v}.csv']
    subprocess.run([str(a) for a in args] + ['--csv', str(A / 'lg27_compare.csv')], check=True, stdout=subprocess.DEVNULL)
    rows = []
    for L in ['L1', 'L2']:
        for v in [CPC, V6, CH]:
            r = rec(P / f'lg_{v}_{L}' / 'record.csv').get(L)
            if r:
                rows.append([L, v, r['verdict'], r['seconds'], r['disp'], r['max'], r['hpwl_sa']])
    (A / 'large.md').write_text('# L1 / L2（local_grade，同輪）\n\n' + table(['bench', 'version', 'verdict', '秒', '總位移', '最大位移', 'HPWL'], rows))
    return f'paired: {sorted(have)}; large rows {len(rows)}'


# ---------------------------------------------------------------- 3. ts stage times
def ts_stage():
    r = rec(P / 'ts_CPC' / 'record.csv')
    if not r:
        return 'ts: no data'
    keys = ['t_read', 't_legalize', 't_pareto', 't_windowdp', 't_chain', 't_global', 't_stage1']
    sums = {k: sum(f(x.get(k), 0) or 0 for x in r.values()) for k in keys}
    rows = []
    for b, x in r.items():
        bud, used = extra_used(P / 'ts_CPC' / b)
        rows.append([b] + [f"{f(x.get(k), 0) or 0:.2f}" for k in keys] + [f'{used:.2f}/{bud:.0f}' if used is not None else ''])
    rows.append(['**合計**'] + [f'{sums[k]:.1f}' for k in keys] + [''])
    (A / 'ts_stage.md').write_text('# CPC Stage 1 各階段秒數（run_ts --no-sa，27 筆）\n\n' +
                                   table(['bench', 'read', 'legalize', 'pareto', 'windowDP', 'chain', 'cap', 'Stage1', 'extra 用/預算'], rows))
    return 'ts: ' + ', '.join(f'{k[2:]} {v:.0f}' for k, v in sums.items())


# ---------------------------------------------------------------- 4. DP convergence curve
TOT = re.compile(r'total ([\d.]+)(?: -> ([\d.]+))?')
CHAIN_START = ('[target-window]', '[chain]', '[chain-round]', '[chain-phase]', '[cap')


def curve(path):
    """(t_sel, t_end, [(t, total)]) over the finish rounds + pareto + main window DP"""
    L = lines(path)
    t_end = None
    end_idx = len(L)
    for i, (t, m) in enumerate(L):
        if m.startswith(CHAIN_START) or m.startswith('[extra]') or m.startswith('[stage1]'):
            end_idx = i
            break
    L = L[:end_idx]
    if not L:
        return None
    sel = None
    for i, (t, m) in enumerate(L):
        if m.startswith('[legal] plain:'):
            sel = i
    if sel is None:   # one pipeline: from its abacus line
        for i, (t, m) in enumerate(L):
            if m.startswith('[legal] abacus  total') or m.startswith('[legal] tetris'):
                sel = i
    if sel is None:
        return None
    pts = []
    sel_total = None
    m0 = L[sel][1]
    g = re.search(r'\| spread: total ([\d.]+) max [\d.]+ -> (\w+)', m0)
    if g:
        a = re.search(r'plain: total ([\d.]+)', m0)
        sel_total = float(g.group(1)) if g.group(2) == 'spread' else float(a.group(1))
    else:
        sel_total = float(TOT.search(m0).group(1))
    pts.append((L[sel][0], sel_total))
    for t, m in L[sel + 1:]:
        if m.startswith('[window-max]'):
            g = re.search(r'total ([\d.]+) -> ([\d.]+)', m)
            if g:
                pts.append((t, float(g.group(2))))
            continue
        g = TOT.search(m)
        if g and (m.startswith('[legal]') or m.startswith('[window-disp]') or m.startswith('[pareto]')):
            pts.append((t, float(g.group(2) or g.group(1))))
    return L[sel][0], L[-1][0], pts


def at(pts, t):
    v = pts[0][1]
    for tt, x in pts:
        if tt <= t:
            v = x
        else:
            break
    return v


def dpcurve(name='dpcurve'):
    d = P / name
    r = rec(d / 'record.csv')
    if not r:
        return 'dpcurve: no data'
    out = []
    for b in r:
        c = curve(d / b / 'stderr_ts.tsv')
        if not c:
            continue
        t_sel, t_end, pts = c
        D_end = pts[-1][1]
        D_sel = pts[0][1]
        span = t_end - t_sel
        row = dict(bench=b, t_sel=round(t_sel, 2), t_end=round(t_end, 2), span=round(span, 2), D_sel=D_sel, D_end=D_end,
                   gain_finish_pct=100 * (D_sel - D_end) / D_end if D_end else 0)
        for frac in [0.2, 0.5]:
            Dt = at(pts, t_end - frac * span)
            row[f'last{int(frac * 100)}_pct'] = 100 * (Dt - D_end) / D_end if D_end else 0
            row[f'last{int(frac * 100)}_s'] = round(frac * span, 2)
        for eps in [1e-3, 3e-4, 1e-4]:
            te = next((t for t, x in pts if x <= D_end * (1 + eps)), t_end)
            row[f'save_{eps:g}'] = round(t_end - te, 2)
        out.append(row)
    with open(A / f'{name}.csv', 'w', newline='') as fh:
        w = csv.DictWriter(fh, fieldnames=list(out[0]))
        w.writeheader()
        w.writerows(out)
    rows = [[o['bench'], f"{o['t_sel']:.1f}", f"{o['t_end']:.1f}", f"{o['span']:.1f}", f"{o['gain_finish_pct']:.3f}",
             f"{o['last20_s']:.1f}", f"{o['last20_pct']:.4f}", f"{o['last50_pct']:.4f}", o['save_0.001'], o['save_0.0003'],
             o['save_0.0001']] for o in out]
    tot = {k: sum(o[k] for o in out) for k in ['span', 'last20_s', 'save_0.001', 'save_0.0003', 'save_0.0001']}
    tr = [o for o in out if o['bench'] in TRAIN]
    rows.append(['**27 合計**', '', '', f"{tot['span']:.1f}", '', f"{tot['last20_s']:.1f}", '', '', f"{tot['save_0.001']:.1f}",
                 f"{tot['save_0.0003']:.1f}", f"{tot['save_0.0001']:.1f}"])
    rows.append(['**11 筆訓練合計**', '', '', f"{sum(o['span'] for o in tr):.1f}", '', f"{sum(o['last20_s'] for o in tr):.1f}", '', '',
                 f"{sum(o['save_0.001'] for o in tr):.1f}", f"{sum(o['save_0.0003'] for o in tr):.1f}", f"{sum(o['save_0.0001'] for o in tr):.1f}"])
    (A / f'{name}.md').write_text(
        '# 視窗 DP 收斂曲線（插樁版 --trace-dp，--no-sa）\n\n'
        't_sel = 選定 pipeline（plain/spread）的時刻；t_end = chain 階段開始；span = final rounds + pareto + 主視窗 DP。\n'
        'last20 = 最後 20% span 拿到的總位移下降（% of 最終總位移）；save_ε = 若在「總位移 ≤ 最終 ×(1+ε)」時停，可省的秒數。\n\n'
        + table(['bench', 't_sel', 't_end', 'span', 'finish 收穫 %', 'last20 秒', 'last20 %', 'last50 %', 'save 1e-3', 'save 3e-4', 'save 1e-4'], rows))
    return f"dpcurve: span {tot['span']:.0f}s, last20 {tot['last20_s']:.0f}s, save@1e-4 {tot['save_0.0001']:.0f}s"


# ---------------------------------------------------------------- 5. chain allocation
CP = re.compile(r'\[chain-phase\] total ([\d.]+) -> ([\d.]+), max ([\d.]+) -> ([\d.]+)\s+([\d.]+)s of ([\d.]+)s')


def chain():
    cfgs = [c.name for c in sorted((P / 'chain').glob('*')) if (c / 'record.csv').exists()] if (P / 'chain').exists() else []
    if not cfgs:
        return 'chain: no data'
    data = {}
    for c in cfgs:
        for b in rec(P / 'chain' / c / 'record.csv'):
            for _, m in lines(P / 'chain' / c / b / 'stderr_ts.tsv'):
                g = CP.match(m)
                if g:
                    a, z, ma, mz, s, budget = map(float, g.groups())
                    data[c, b] = dict(start=a, end=z, gain_pct=100 * (a - z) / a if a else 0, maxs=ma, maxe=mz,
                                      sec=s, budget=budget)
    benches = sorted({b for _, b in data}, key=lambda b: (b not in TRAIN, TRAIN.index(b) if b in TRAIN else 0, b))
    rows = []
    for b in benches:
        rows.append([b] + [f"{data[c, b]['gain_pct']:.3f} / {data[c, b]['sec']:.2f}" if (c, b) in data else '' for c in cfgs])
    summ = []
    for c in cfgs:
        tr = [data[c, b] for b in TRAIN if (c, b) in data]
        base = [data['def', b] for b in TRAIN if (c, b) in data and ('def', b) in data]
        if not tr:
            continue
        rel = gm([d['end'] / bb['end'] for d, bb in zip(tr, base)]) if len(base) == len(tr) else float('nan')
        summ.append([c, len(tr), f"{st.mean(d['gain_pct'] for d in tr):.4f}", f"{sum(d['sec'] for d in tr):.2f}",
                     f"{(rel - 1) * 100:+.4f}%"])
    (A / 'chain.md').write_text(
        '# chain 階段時間分配（--no-sa --cap-time 0；每格 = chain 階段總位移下降 % / 實際秒數）\n\n'
        + table(['bench'] + cfgs, rows) + '\n## 11 筆訓練摘要\n\n'
        + table(['cfg', '筆數', '平均收穫 %', '秒合計', 'chain 後總位移 GM ÷ def'], summ))
    with open(A / 'chain.csv', 'w', newline='') as fh:
        w = csv.writer(fh)
        w.writerow(['cfg', 'bench', 'start', 'end', 'gain_pct', 'max_start', 'max_end', 'sec', 'budget'])
        for (c, b), d in sorted(data.items()):
            w.writerow([c, b, d['start'], d['end'], d['gain_pct'], d['maxs'], d['maxe'], d['sec'], d['budget']])
    return f'chain: {cfgs}'


# ---------------------------------------------------------------- 6. max displacement variation
def maxvar():
    runs = sorted((P / 'maxvar').glob('*/record.csv')) if (P / 'maxvar').exists() else []
    if not runs:
        return 'maxvar: no data'
    by = {}
    for p in runs:
        for b, r in rec(p).items():
            by.setdefault(b, []).append((p.parent.name, f(r['max']), f(r['disp']), f(r['seconds'])))
    rows = []
    for b, xs in by.items():
        mx = [x[1] for x in xs if not x[0].startswith('p4')]
        tt = [x[2] for x in xs if not x[0].startswith('p4')]
        p4 = [x for x in xs if x[0].startswith('p4')]
        rows.append([b, len(mx), f'{min(mx):.4f}', f'{max(mx):.4f}', f'{100 * (max(mx) / min(mx) - 1):.3f}%',
                     f'{min(tt):.2f}', f'{max(tt):.2f}', f'{100 * (max(tt) / min(tt) - 1):.3f}%',
                     ' '.join(f'{x[1]:.4f}' for x in xs if not x[0].startswith('p4')),
                     ' '.join(f'{x[1]:.4f}/{x[2]:.1f}' for x in p4)])
    (A / 'maxvar.md').write_text('# 同一 binary（CPC）重跑 5 次：最大／總位移範圍（--no-sa）；p4 = taskset 4 P-core、同樣 8 執行緒\n\n' +
                                 table(['bench', 'n', '最大 min', '最大 max', '最大範圍', '總 min', '總 max', '總範圍', '各次最大', 'P4 最大/總'], rows))
    return f'maxvar: {len(runs)} runs'


# ---------------------------------------------------------------- 7. band loss, 8. SA/polish split
def sa_logs(prefix, d=P):
    out = {}
    for p in sorted(d.glob(f'{prefix}_*.log')):
        txt = p.read_text().strip().splitlines()
        try:
            i = txt.index('hpwl,attempts,sa_seconds,stage2_seconds')
            h, a, s, s2 = map(float, txt[i + 1].split(','))
            out[p.stem] = dict(hpwl=h, attempts=a, sa=s, stage2=s2)
        except ValueError:
            out[p.stem] = None
    return out


def band():
    L = sa_logs('band')
    if not L:
        return 'band: no data'
    # phase 1's auto runs did 20M per band (8x the work): use phase1c's equal-total auto runs when present
    fixed = sa_logs('band', R / 'phase1c')
    for k, v in fixed.items():
        L[k] = v
    rows, losses = [], {}
    for b in ['ibm01', 'ibm05', 'h06', 'h07', 'h08']:
        for s in [1, 2]:
            a, o = L.get(f'band_{b}_s{s}_t0'), L.get(f'band_{b}_s{s}_t1')
            if a and o:
                loss = 100 * (a['hpwl'] / o['hpwl'] - 1)
                losses.setdefault(b, []).append(loss)
                rows.append([b, s, f"{a['hpwl']:.6g}", f"{o['hpwl']:.6g}", f'{loss:+.3f}%', f"{a['attempts'] / 1e6:.2f}M",
                             f"{o['attempts'] / 1e6:.2f}M", f"{a['sa']:.1f}", f"{o['sa']:.1f}"])
    summ = [[b, f'{st.mean(v):+.3f}%', f'{min(v):+.3f}% ~ {max(v):+.3f}%'] for b, v in losses.items()]
    allv = [x for v in losses.values() for x in v]
    if allv:
        summ.append(['**平均**', f'{st.mean(st.mean(v) for v in losses.values()):+.3f}%', f'{min(allv):+.3f}% ~ {max(allv):+.3f}%'])
    (A / 'band.md').write_text('# 平行 SA band 邊界損失（CPC，含四相輪替；總次數同為 20M（auto 每 band 20M/bands，phase1c）、無 polish、同起點；loss = auto ÷ 1 執行緒 − 1）\n\n'
                               + table(['bench', 'seed', 'HPWL auto', 'HPWL 1 thr', 'loss', '次數 auto', '次數 1', 'SA 秒 auto', 'SA 秒 1'], rows)
                               + '\n' + table(['bench', '平均 loss', '範圍'], summ))
    return f'band: {len(rows)} pairs, mean {st.mean(allv) if allv else float("nan"):+.3f}%'


def split():
    L = sa_logs('split')
    if not L:
        return 'split: no data'
    cfgs = ['sa5p17', 'sa10p12', 'sa20p2', 'sa60p30', 'sa30p60']
    rows = []
    for b in ['h06', 'h07', 'h08', 'e15_realistic']:
        ref = [L.get(f'split_{b}_s{s}_sa20p2') for s in [1, 2]]
        ref60 = [L.get(f'split_{b}_s{s}_sa60p30') for s in [1, 2]]
        row = [b]
        for c in cfgs:
            xs = [L.get(f'split_{b}_s{s}_{c}') for s in [1, 2]]
            if not all(xs):
                row.append('')
                continue
            base = ref if c in ('sa5p17', 'sa10p12', 'sa20p2') else ref60
            if all(base):
                rs = [x['hpwl'] / y['hpwl'] for x, y in zip(xs, base)]
                row.append(f"{st.mean(x['hpwl'] for x in xs):.6g} ({(st.mean(rs) - 1) * 100:+.2f}%, "
                           f"{min(rs) - 1:+.2%}~{max(rs) - 1:+.2%}; {st.mean(x['attempts'] for x in xs) / 1e6:.1f}M)")
            else:
                row.append(f"{st.mean(x['hpwl'] for x in xs):.6g}")
        rows.append(row)
    (A / 'split.md').write_text(
        '# SA / polish 分配（同起點 = CPC Stage 1，2 seeds；括號 = 對 sa20p2（總 22 秒組）或 sa60p30（總 90 秒組）的平均差、範圍、SA 次數）\n\n'
        'sa60p30 = dp 預設（含 starved 判斷）；其他都關掉 starved（SA 用滿指定秒數）。\n\n'
        + table(['bench'] + cfgs, rows))
    return f'split: {sum(1 for v in L.values() if v)} runs'


if __name__ == '__main__':
    for fn in [cores, paired, ts_stage, dpcurve, chain, maxvar, band, split]:
        try:
            print(fn())
        except Exception as e:   # keep going: sections are independent
            print(f'{fn.__name__}: ERROR {e!r}')
