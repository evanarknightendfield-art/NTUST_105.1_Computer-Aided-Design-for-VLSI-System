#!/usr/bin/env python3
import argparse,csv,importlib.util,math,os,subprocess,time
from pathlib import Path
root=Path(__file__).resolve().parents[2];project=Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('verify',root/'grader/grader/verifier.py');v=importlib.util.module_from_spec(s);s.loader.exec_module(v)
p=argparse.ArgumentParser();p.add_argument('--baseline', type=Path, default=root/'M11502102_ClaudeOpus55Ultracode_2/build/cbi');p.add_argument('--candidate', type=Path, default=project/'build/cbi');p.add_argument('--inputs', type=Path, default=project/'experiments/tuning_inputs');p.add_argument('--variants', default='baseline,core,line,spatial,spatial_core');p.add_argument('--seconds',type=float,default=5);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
cpus=sorted(os.sched_getaffinity(0))[:2];env={k:v for k,v in os.environ.items() if not k.startswith('CBI_')};env['CBI_THREADS']='2'
variants={'baseline':(str(a.baseline.resolve()),{}),'core':(str(a.candidate.resolve()),{'CBI_EXACT_WEIGHT':'0','CBI_SPATIAL':'0'}),'line':(str(a.candidate.resolve()),{'CBI_EXACT_WEIGHT':'4','CBI_SPATIAL':'0'}),'spatial':(str(a.candidate.resolve()),{'CBI_SPATIAL':'1','CBI_EXACT_WEIGHT':'4'}),'spatial_core':(str(a.candidate.resolve()),{'CBI_SPATIAL':'1','CBI_EXACT_WEIGHT':'0'})}
variants={name:variants[name] for name in a.variants.split(',')}
rows=[]
for i,inp in enumerate(sorted(a.inputs.glob('*.cbi'))):
 names=list(variants);names=names[i%len(names):]+names[:i%len(names)]
 for name in names:
  binary,extra=variants[name];runenv=env|extra;out=a.out/f'{inp.stem}.{name}.cbi';t=time.monotonic()
  r=subprocess.run([binary,str(inp),str(out.resolve()),'-t',str(a.seconds),'-s','1'],capture_output=True,text=True,timeout=max(60,a.seconds+30),env=runenv,preexec_fn=lambda:os.sched_setaffinity(0,cpus))
  seconds=time.monotonic()-t;out.with_suffix('.log').write_text(r.stderr)
  assert r.returncode==0,(name,inp,r.stderr)
  ok,score,hi,lo=v.verify(v.parse_input(str(inp)),v.parse_output(str(out)))
  assert ok,(name,inp)
  assert f'T_max: {hi}, T_min: {lo}, Score: {score}' in r.stdout
  rows.append({'input':inp.name,'variant':name,'score':score,'seconds':round(seconds,4)})
  with (a.out/'results.csv').open('w') as f:w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
  print(rows[-1],flush=True)
for name in ([n for n in variants if n!='baseline'] if 'baseline' in variants else []):
 ratios=[];wins=ties=0
 for inp in sorted({r['input'] for r in rows}):
  scores={r['variant']:r['score'] for r in rows if r['input']==inp};ratios.append(scores[name]/scores['baseline']);wins+=scores[name]<scores['baseline'];ties+=scores[name]==scores['baseline']
 print(name,'geomean=',math.exp(sum(map(math.log,ratios))/len(ratios)),'wins/ties/losses=',wins,ties,len(ratios)-wins-ties,flush=True)
