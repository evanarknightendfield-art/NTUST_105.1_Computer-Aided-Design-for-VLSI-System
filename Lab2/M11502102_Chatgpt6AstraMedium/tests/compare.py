#!/usr/bin/env python3
"""Sequential equal-budget comparison; every result checked by the course verifier."""
import argparse, csv, importlib.util, math, os, subprocess, time
from pathlib import Path
root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('verifier', root/'grader/grader/verifier.py')
v = importlib.util.module_from_spec(spec); spec.loader.exec_module(v)
ap = argparse.ArgumentParser()
ap.add_argument('--baseline', type=Path, default=root/'M11502102_ClaudeOpus55Ultracode/build/cbi')
ap.add_argument('--candidate', type=Path, default=root/'M11502102_Chatgpt6AstraMedium/build/cbi')
ap.add_argument('--inputs', type=Path, default=root/'testcases')
ap.add_argument('--seconds', type=float, default=5)
ap.add_argument('--seeds', default='1,2,3')
ap.add_argument('--out', type=Path, required=True)
a = ap.parse_args(); a.out.mkdir(parents=True, exist_ok=True)
rows=[]; env={k:val for k,val in os.environ.items() if not k.startswith('CBI_')}
for inp in sorted(a.inputs.glob('*.cbi')):
    for seed in map(int, a.seeds.split(',')):
        row={'input':inp.name, 'seed':seed, 'budget':a.seconds}
        order=['baseline','candidate'] if seed%2 else ['candidate','baseline']
        for name in order:
            out=a.out/f'{inp.stem}.{seed}.{name}.cbi'
            t=time.monotonic()
            r=subprocess.run([str(getattr(a,name).resolve()), str(inp.resolve()), str(out.resolve()), '-t', str(a.seconds), '-s', str(seed)], capture_output=True, text=True, timeout=max(60,a.seconds+30),env=env)
            row[name+'_seconds']=round(time.monotonic()-t,4)
            out.with_suffix('.log').write_text(r.stderr)
            assert r.returncode==0, (inp,name,r.stderr)
            valid,score,hi,lo=v.verify(v.parse_input(str(inp)),v.parse_output(str(out)))
            assert valid, (inp,name,'INVALID')
            assert f'T_max: {hi}, T_min: {lo}, Score: {score}' in r.stdout, (inp,name,r.stdout,score)
            row[name]=score
        row['ratio']=row['candidate']/row['baseline'] if row['baseline'] else 1.0
        rows.append(row); print(row,flush=True)
        with (a.out/'results.csv').open('w') as f:
            w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
wins=sum(r['candidate']<r['baseline'] for r in rows);losses=sum(r['candidate']>r['baseline'] for r in rows)
summary=f'{len(rows)} verified pairs; wins/ties/losses={wins}/{len(rows)-wins-losses}/{losses}; geometric ratio={math.exp(sum(math.log(r["ratio"]) for r in rows)/len(rows)):.6f}'
print(summary); (a.out/'summary.txt').write_text(summary+'\n')
