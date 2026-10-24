#!/usr/bin/env python3
"""phase1_tables.py — tables for diagnosis.md from the phase1_exp.py outputs.

  phase1/l2_sched.md       L2: SA60->P30 vs SA30->P60 per start and seed (mean, range)
  phase1/chain_sweep.md    total displacement after the chain step vs chain time T
"""
import csv, glob, math, os, re, statistics as st
from pathlib import Path

R = Path(__file__).resolve().parents[1]
P = R / 'phase1'


def l2_sched():
    rows = []
    for f in sorted(glob.glob(str(P / 'exp/l2_sched/*/l2_*.csv'))):
        m = re.match(r'l2_(.+)_sa(\d+)_p(\d+)_s(\d+)\.csv', os.path.basename(f))
        lines = open(f).read().split()
        if len(lines) < 2:
            continue
        h, att, sa, s2 = map(float, lines[1].split(','))
        rows.append(dict(start=m.group(1), sched=f'SA{m.group(2)}->P{m.group(3)}', seed=int(m.group(4)),
                         hpwl=h, attempts=int(att), sa_s=sa, stage2_s=s2))
    out = ['# L2：同一個合法起點，SA / polish 的時間分配', '',
           'sa_bench（CodexCapDP 的 SA / polish 原始碼）、自動執行緒、各 seed 一次。比值 = SA30→P60 ÷ SA60→P30（同起點同 seed）。', '',
           '| 起點 | seed | SA60→P30 HPWL | SA30→P60 HPWL | 比值 | Stage 2 秒（60/30 vs 30/60） |', '|---|---:|---:|---:|---:|---|']
    ratios = {}
    for start in sorted({r['start'] for r in rows}):
        for seed in sorted({r['seed'] for r in rows if r['start'] == start}):
            a = [r for r in rows if r['start'] == start and r['seed'] == seed and r['sched'] == 'SA60->P30']
            b = [r for r in rows if r['start'] == start and r['seed'] == seed and r['sched'] == 'SA30->P60']
            if a and b:
                q = b[0]['hpwl'] / a[0]['hpwl']
                ratios.setdefault(start, []).append(q)
                out.append(f"| {start} | {seed} | {a[0]['hpwl']:.4e} | {b[0]['hpwl']:.4e} | {q:.4f} | "
                           f"{a[0]['stage2_s']:.1f} / {b[0]['stage2_s']:.1f} |")
    out.append('')
    for start, q in ratios.items():
        out.append(f'- 起點 {start}：比值平均 {st.mean(q):.4f}，範圍 {min(q):.4f}–{max(q):.4f}（{len(q)} seeds）')
    for sched in ['SA60->P30', 'SA30->P60']:
        by = {s: [r['hpwl'] for r in rows if r['start'] == s and r['sched'] == sched] for s in ratios}
        if len(by) == 2 and all(by.values()):
            a, b = list(by)
            out.append(f'- {sched}：起點 {b} ÷ {a} 的 HPWL 平均 = {st.mean(by[b]) / st.mean(by[a]):.4f}')
    (P / 'l2_sched.md').write_text('\n'.join(out) + '\n')
    with open(P / 'l2_sched.csv', 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=['start', 'sched', 'seed', 'hpwl', 'attempts', 'sa_s', 'stage2_s'])
        w.writeheader()
        w.writerows(rows)


def chain_sweep():
    rows = []
    for d in sorted(glob.glob(str(P / 'exp/chain_sweep/T*'))):
        T = float(os.path.basename(d)[1:])
        for b in sorted(os.listdir(d)):
            f = Path(d) / b / 'stderr_ts.tsv'
            if not f.exists():
                continue
            pre = post = None
            t0 = t1 = None
            for ln in open(f):
                t, _, m = ln.rstrip('\n').partition('\t')
                mm = re.match(r'\[chain-round\] \d+: .* total ([\d.]+) -> ([\d.]+)', m)
                if mm:
                    pre = pre if pre is not None else float(mm.group(1))
                    post = float(mm.group(2))
                    t1 = float(t)
                mm = re.match(r'\[extra\] budget ([\d.]+) used ([\d.]+)', m)
                if mm:
                    t0 = float(t) - float(mm.group(2))
            rows.append(dict(bench=b, T=T, pre=pre, post=post, chain_s=None if t1 is None or t0 is None else round(t1 - t0, 3)))
    benches = sorted({r['bench'] for r in rows})
    Ts = sorted({r['T'] for r in rows})
    out = ['# chain 階段（target windows + chains + DP）的收穫 vs 時間', '',
           '`--no-sa --cap-time 0 --extra-time 40 --chain-time T`；同一份 binary（CodexCapDP），各 T 一次。',
           '格內：chain 後總位移相對 chain 前的變化 %（實際秒數）。最後一欄 = T 從 1.5 加到 6 多拿的 %。', '',
           '| bench | ' + ' | '.join(f'T={t:g}' for t in Ts) + ' | 1.5→6 |', '|---|' + '---:|' * (len(Ts) + 1)]
    for b in benches:
        cells, by = [], {}
        for t in Ts:
            r = next((r for r in rows if r['bench'] == b and r['T'] == t), None)
            if r and r['pre']:
                g = (r['post'] / r['pre'] - 1) * 100
                by[t] = r['post']
                cells.append(f"{g:+.3f}% ({r['chain_s']}s)")
            else:
                cells.append('—')
        extra = f"{(by[6] / by[1.5] - 1) * 100:+.3f}%" if 1.5 in by and 6 in by else '—'
        out.append(f'| {b} | ' + ' | '.join(cells) + f' | {extra} |')
    (P / 'chain_sweep.md').write_text('\n'.join(out) + '\n')
    with open(P / 'chain_sweep.csv', 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=['bench', 'T', 'pre', 'post', 'chain_s'])
        w.writeheader()
        w.writerows(rows)


if __name__ == '__main__':
    l2_sched()
    chain_sweep()
