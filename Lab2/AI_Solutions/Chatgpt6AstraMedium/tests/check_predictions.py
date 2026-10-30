#!/usr/bin/env python3
"""Exercise instrumented global-placement predictions and check resulting solutions."""
import argparse, importlib.util, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('verifier',root/'grader/grader/verifier.py')
v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
a=argparse.ArgumentParser();a.add_argument('binary',type=Path);args=a.parse_args()
paths=sorted((root/'testcases').glob('*.cbi'))+sorted((root/'M11502102_Chatgpt6AstraMedium/experiments/holdout_inputs').glob('*.cbi'))
paths += sorted((root/'M11502102_Chatgpt6AstraMedium/experiments/final_holdout_inputs').glob('*.cbi'))
with tempfile.TemporaryDirectory() as tmp:
    out=Path(tmp)/'output.cbi'
    for inp in paths:
        r=subprocess.run([str(args.binary.resolve()),str(inp),str(out),'-t','0.25'],capture_output=True,text=True,timeout=60)
        assert r.returncode==0,(inp,r.stderr)
        valid,score,hi,lo=v.verify(v.parse_input(str(inp)),v.parse_output(str(out)))
        assert valid,(inp,'INVALID')
        assert f'T_max: {hi}, T_min: {lo}, Score: {score}' in r.stdout
print(f'Passed: {len(paths)} instrumented cases, all verified')
