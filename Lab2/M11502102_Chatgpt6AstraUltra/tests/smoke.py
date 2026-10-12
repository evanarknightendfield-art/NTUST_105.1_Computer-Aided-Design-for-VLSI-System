#!/usr/bin/env python3
"""Independent output verification for short sanitizer runs, single/multithread."""
import argparse,importlib.util,os,subprocess,tempfile
from pathlib import Path
project=Path(__file__).resolve().parents[1];root=project.parent
s=importlib.util.spec_from_file_location('v',root/'grader/grader/verifier.py');v=importlib.util.module_from_spec(s);s.loader.exec_module(v)
a=argparse.ArgumentParser();a.add_argument('binary',type=Path);args=a.parse_args()
inputs=sorted((root/'testcases').glob('*.cbi'))+sorted((project/'experiments/tuning_inputs').glob('*.cbi'))+sorted((project/'experiments/holdout_inputs').glob('*.cbi'))
count=0
with tempfile.TemporaryDirectory() as tmp:
 for threads in [1,2]:
  for inp in inputs:
   out=Path(tmp)/'output.cbi';env={k:v for k,v in os.environ.items() if not k.startswith('CBI_')};env['CBI_THREADS']=str(threads)
   r=subprocess.run([str(args.binary.resolve()),str(inp),str(out),'-t','.15'],capture_output=True,text=True,env=env,timeout=60)
   assert r.returncode==0,(threads,inp,r.stderr)
   ok,score,hi,lo=v.verify(v.parse_input(str(inp)),v.parse_output(str(out)));assert ok,(threads,inp)
   assert f'T_max: {hi}, T_min: {lo}, Score: {score}' in r.stdout
   count+=1
 # Dumps must describe the actually chosen construction and balanced solution.
 inp=root/'testcases/input007.cbi';out=Path(tmp)/'out.cbi';prefix=Path(tmp)/'stages'
 r=subprocess.run([str(args.binary.resolve()),str(inp),str(out),'-t','0','-d',str(prefix)],capture_output=True,text=True,timeout=60)
 assert r.returncode==0,r.stderr
 scores=[]
 for f in [str(prefix)+'.stage1.cbi',str(prefix)+'.stage2.cbi',str(out)]:
  ok,score,_,_=v.verify(v.parse_input(str(inp)),v.parse_output(f));assert ok,f;scores.append(score)
 assert scores[2]==scores[1] and scores[1]<=scores[0],scores
 r=subprocess.run([str(args.binary.resolve())],capture_output=True,text=True);assert r.returncode==1 and 'usage:' in r.stderr
print(f'PASS: {count} sanitizer solver runs (1 and 2 threads), 3 stage outputs, missing-argument check')
