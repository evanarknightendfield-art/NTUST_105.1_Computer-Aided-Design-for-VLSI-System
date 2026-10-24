#!/usr/bin/env python3
"""compare_lg.py — per-bench displacement / HPWL / time of local_grade runs against a reference run.

usage: compare_lg.py OUT.md REF_NAME REF_RECORD [NAME RECORD ...] [--csv OUT.csv]
Criterion (the task's): max <= ref max and total <= ref total * 1.005.
Geometric means skip benches where both totals are 0.
"""
import csv, math, sys


def load(path):
    return {r['bench']: r for r in csv.DictReader(open(path))}


def gm(xs):
    xs = [x for x in xs if x is not None and x > 0]
    return math.exp(sum(map(math.log, xs)) / len(xs)) if xs else float('nan')


def ratio(a, b):
    return None if b == 0 and a == 0 else (a / b if b else float('inf'))


def main():
    args = sys.argv[1:]
    out_csv = None
    if '--csv' in args:
        i = args.index('--csv')
        out_csv = args[i + 1]
        del args[i:i + 2]
    out, ref_name, ref_path = args[:3]
    others = list(zip(args[3::2], args[4::2]))
    ref = load(ref_path)
    lines = [f'# local_grade 比較（參考：{ref_name}）', '',
             '條件：最大 ≤ 參考、總位移 ≤ 參考 × 1.005。比值 = 版本 ÷ 參考。', '']
    rows_csv = []
    summary = []
    for name, path in others:
        rec = load(path)
        lines += [f'## {name}', '', '| bench | PASS | 秒 | 最大 | 參考最大 | 最大比 | 總位移 | 參考總 | 總比 | HPWL 比 | 達標 |',
                  '|---|---|---:|---:|---:|---:|---:|---:|---:|---:|:-:|']
        mr, tr, hr, both, n, fails, secs = [], [], [], 0, 0, [], 0.0
        for b, r0 in ref.items():
            r = rec.get(b)
            if r is None:
                continue
            n += 1
            m, m0, t, t0 = float(r['max']), float(r0['max']), float(r['disp']), float(r0['disp'])
            h, h0 = float(r['hpwl_sa']), float(r0['hpwl_sa'])
            secs += float(r['seconds'])
            okm, okt = m <= m0 + 1e-9, t <= t0 * 1.005 + 1e-9
            both += okm and okt
            rm, rt, rh = ratio(m, m0), ratio(t, t0), ratio(h, h0)
            mr.append(rm); tr.append(rt); hr.append(rh)
            if not (okm and okt):
                fails.append((b, m - m0, (m / m0 - 1) * 100 if m0 else 0, max(0.0, t - t0 * 1.005), (t / t0 - 1) * 100 if t0 else 0))
            lines.append(f"| {b} | {r.get('verdict', r.get('status'))} | {float(r['seconds']):.1f} | {m:.4f} | {m0:.4f} | {rm if rm is None else f'{rm:.4f}'} | "
                         f"{t:.2f} | {t0:.2f} | {rt if rt is None else f'{rt:.5f}'} | {rh if rh is None else f'{rh:.4f}'} | {'✓' if okm and okt else '✗'} |")
            rows_csv.append(dict(version=name, bench=b, verdict=r.get('verdict', r.get('status')), seconds=r['seconds'], max=m, ref_max=m0, total=t, ref_total=t0,
                                 hpwl=h, ref_hpwl=h0, ok_max=okm, ok_total=okt))
        passes = sum(1 for b in ref if b in rec and rec[b].get('verdict', rec[b].get('status')) in ('PASS', 'OK'))
        lines += ['', f'PASS {passes}/{n}；雙條件 {both}/{n}；最大幾何平均比 {gm(mr):.5f}、總位移 {gm(tr):.5f}、HPWL {gm(hr):.5f}；合計 {secs:.1f} 秒', '']
        if fails:
            lines += ['| 未達標 | 最大差 | 最大差 % | 總超過 ×1.005 | 總差 % |', '|---|---:|---:|---:|---:|']
            lines += [f'| {b} | {dm:+.4f} | {pm:+.3f}% | {dt:.3f} | {pt:+.3f}% |' for b, dm, pm, dt, pt in fails]
            lines.append('')
        summary.append((name, passes, n, both, gm(mr), gm(tr), gm(hr), secs))
    lines[4:4] = ['| 版本 | PASS | 雙條件 | 最大 GM | 總 GM | HPWL GM | 秒 |', '|---|---:|---:|---:|---:|---:|---:|'] + \
        [f'| {a} | {p}/{n} | {b}/{n} | {m:.5f} | {t:.5f} | {h:.5f} | {s:.1f} |' for a, p, n, b, m, t, h, s in summary] + ['']
    open(out, 'w').write('\n'.join(lines) + '\n')
    if out_csv:
        with open(out_csv, 'w', newline='') as f:
            w = csv.DictWriter(f, fieldnames=list(rows_csv[0]))
            w.writeheader()
            w.writerows(rows_csv)


if __name__ == '__main__':
    main()
