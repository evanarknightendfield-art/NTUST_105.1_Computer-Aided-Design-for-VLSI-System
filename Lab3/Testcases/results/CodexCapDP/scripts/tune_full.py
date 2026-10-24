#!/usr/bin/env python3
"""Resumable Stage 1 validation, testcases + holdout only."""
import argparse, csv, hashlib, json, shlex, subprocess, sys
from pathlib import Path

R = Path(__file__).resolve().parents[1]
LAB = R.parents[2]
sys.path.insert(0, str(R.parent / 'ClaudeOpus55_v5/scripts'))
from run_ts import run_one, stdout_numbers

a = argparse.ArgumentParser()
a.add_argument('name')
a.add_argument('--dp-args', default='')
a.add_argument('--bin', default=str(R / 'phase2/build/dp'))
args = a.parse_args()
exe = Path(args.bin).resolve()
out = R / 'phase2' / args.name
out.mkdir(parents=True, exist_ok=True)
protocol = {'binary_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'args': args.dp_args}
pp = out / 'protocol.json'
if pp.exists() and json.loads(pp.read_text()) != protocol:
    raise SystemExit('Output directory has a different binary or protocol; use a new name')
pp.write_text(json.dumps(protocol, indent=2) + '\n')
reference = {r['bench']: r for r in csv.DictReader((R.parent / 'ClaudeOpus55_v5/phase1/lg_chain/record.csv').open())}
records = []
for group in ['testcases', 'holdout']:
    for aux in sorted((LAB / 'Testcases' / group).glob('*/*.aux')):
        bench = aux.stem
        d = out / bench
        done = d / 'done.json'
        if done.exists():
            row = json.loads(done.read_text())
        else:
            status, sec, lines, text = run_one(str(exe), str(aux.with_suffix('')), str(d), '--no-sa ' + args.dp_args, 600)
            row = dict(bench=bench, group=group, status=status, seconds=sec, **stdout_numbers(text))
            ck = subprocess.run([sys.executable, str(LAB / 'Grader/tools/check_pl.py'), str(aux.with_suffix('')),
                                 str(d / 'output_legal.pl'), '--quiet'], capture_output=True, text=True)
            (d / 'check.txt').write_text(ck.stdout + ck.stderr)
            row['legal'] = ck.returncode == 0 and status == 'OK'
            ch = reference[bench]
            row['max_ratio'] = row.get('max', 1e300) / float(ch['max']) if float(ch['max']) else 1
            row['total_ratio'] = row.get('disp', 1e300) / float(ch['disp']) if float(ch['disp']) else 1
            row['both'] = row['legal'] and row['max_ratio'] <= 1+1e-10 and row['total_ratio'] <= 1.005+1e-10
            done.write_text(json.dumps(row, indent=2) + '\n')
        records.append(row)
        with (out / 'record.csv').open('w') as f:
            w = csv.DictWriter(f, fieldnames=list(records[0]))
            w.writeheader(); w.writerows(records)
        print(f"{bench}: legal={row['legal']} max={row.get('max')} total={row.get('disp')} both={row['both']}", flush=True)
summary = dict(passed=sum(r['legal'] for r in records), both=sum(r['both'] for r in records),
               cases=len(records), seconds=sum(r['seconds'] for r in records))
(out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
with (LAB / 'AI_Solutions/CodexCapDP/WORK_STATE.md').open('a') as f:
    f.write(f"\n- 完成 {args.name}：{summary}；資料 phase2/{args.name}/record.csv。相同 binary/protocol 不重跑。\n")
print(summary, flush=True)
