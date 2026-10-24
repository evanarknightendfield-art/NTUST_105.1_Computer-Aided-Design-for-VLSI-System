#!/usr/bin/env python3
"""tune_stage1.py — Stage 1 only (--no-sa) of a dp binary on the TUNING benches, compared with CodexChainDP.

Tuning uses Testcases/testcases + Testcases/holdout only (edge / edge_seed2 have been seen and are regression
checks only). Reference = CodexChainDP's local_grade run of phase 1 (phase1/lg_chain/record.csv).
Per bench: max <= ChainDP's max, and total <= ChainDP's total x 1.005 (the phase-2 acceptance).

usage: tune_stage1.py BIN OUT_NAME [--dp-args "..."] [--groups testcases holdout] [--ref phase1/lg_chain/record.csv]
writes Testcases/results/ClaudeOpus55_v5/tune/OUT_NAME/{record.csv,summary.md}
"""
import argparse, csv, math, os, re, shlex, subprocess, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
R = HERE.parent
LAB = HERE.parents[3]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bin')
    ap.add_argument('name')
    ap.add_argument('--dp-args', default='')
    ap.add_argument('--groups', nargs='+', default=['testcases', 'holdout'])
    ap.add_argument('--ref', default=str(R / 'phase1' / 'lg_chain' / 'record.csv'))
    a = ap.parse_args()
    out = R / 'tune' / a.name
    out.mkdir(parents=True, exist_ok=True)
    ref = {r['bench']: r for r in csv.DictReader(open(a.ref))}
    rows = []
    for g in a.groups:
        for aux in sorted((LAB / 'Testcases' / g).glob('*/*.aux')):
            b = aux.stem
            d = out / b
            d.mkdir(exist_ok=True)
            t = time.monotonic()
            p = subprocess.run([str(Path(a.bin).resolve()), str(aux.with_suffix('')), 'output', '--no-sa', '-v'] + shlex.split(a.dp_args),
                               cwd=d, capture_output=True, text=True, timeout=1800)
            sec = time.monotonic() - t
            (d / 'stderr.txt').write_text(p.stderr)
            m = dict(re.findall(r'(Total displacement|Maximum displacement) \(legal\): ([\d.]+)', p.stdout))
            tot, mx = float(m.get('Total displacement', 'nan')), float(m.get('Maximum displacement', 'nan'))
            rt, rm = float(ref[b]['disp']), float(ref[b]['max'])
            okm = mx <= rm + 1e-9
            okt = tot <= rt * 1.005 + 1e-9
            rows.append(dict(bench=b, seconds=round(sec, 2), total=tot, max=mx, ref_total=rt, ref_max=rm,
                             d_total=(tot / rt - 1) * 100 if rt else 0, d_max=(mx / rm - 1) * 100 if rm else 0,
                             max_ok=okm, total_ok=okt))
            r = rows[-1]
            print(f"{b:8s} {sec:6.1f}s  max {mx:.4f} vs {rm:.4f} ({r['d_max']:+.2f}%) {'ok' if okm else 'WORSE'}   "
                  f"total {r['d_total']:+.3f}% {'ok' if okt else 'OVER'}", flush=True)
    with open(out / 'record.csv', 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    pos = [r for r in rows if r['ref_max'] > 0 and r['max'] > 0]
    gmax = math.exp(sum(math.log(r['max'] / r['ref_max']) for r in pos) / len(pos))
    gtot = math.exp(sum(math.log(r['total'] / r['ref_total']) for r in pos) / len(pos))
    s = (f"{a.name}: {sum(r['max_ok'] for r in rows)}/{len(rows)} max not worse, {sum(r['total_ok'] for r in rows)}/{len(rows)} total "
         f"within +0.5%, both {sum(r['max_ok'] and r['total_ok'] for r in rows)}/{len(rows)}; geomean max {gmax:.4f}, total {gtot:.4f}; "
         f"Stage 1 seconds {sum(r['seconds'] for r in rows):.1f}")
    (out / 'summary.md').write_text(s + '\n')
    print(s)


if __name__ == '__main__':
    main()
