#!/usr/bin/env python3
"""Serial default-parameter grading; no competing runs during timing.
Run from anywhere after freezing source and completing development jobs.
"""
import argparse,csv,hashlib,json,os,shutil,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];LAB=ROOT.parents[1]
OUT=LAB/'Testcases/results/CodexChainDP'
env=dict(os.environ);env['PATH']='/home/evan/anaconda3/envs/eda/bin:'+env['PATH']

def run(label,cmd):
    print('START '+label,flush=True);start=time.monotonic()
    with (OUT/(label+'.log')).open('w') as f:
        code=subprocess.call(cmd,cwd=LAB,env=env,stdout=f,stderr=f)
    with (OUT/'acceptance_steps.csv').open('a') as f:
        csv.writer(f).writerow([label,code,time.monotonic()-start,json.dumps(cmd)])
    print('DONE '+label+' rc='+str(code),flush=True)
    if code:raise SystemExit(code)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--resume',action='store_true');args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    sources={str(p.relative_to(LAB)):hashlib.sha256(p.read_bytes()).hexdigest() for folder in (ROOT,LAB/'AI_Solutions/ClaudeOpus55_v4') for p in sorted((folder/'src').glob('*')) if p.is_file()}
    if args.resume:
        if sources!=json.loads((OUT/'frozen_sources.json').read_text()):raise SystemExit('Frozen sources changed; cannot resume')
    else:
        (OUT/'frozen_sources.json').write_text(json.dumps(sources,indent=2)+'\n')
        with (OUT/'acceptance_steps.csv').open('w') as f:csv.writer(f).writerow(['label','returncode','seconds','command'])
    def complete(folder,count):
        record=OUT/folder/'record.csv'
        rows=list(csv.DictReader(record.open())) if record.exists() else []
        return args.resume and len(rows)==count and all(r['verdict']=='PASS' for r in rows)
    if not complete('baseline_default',27):run('baseline_default',[sys.executable,'Grader/tools/local_grade.py','AI_Solutions/ClaudeOpus55_v4','ClaudeOpus55_v4','Testcases/testcases','Testcases/holdout','Testcases/edge','--work',str(OUT/'baseline_default'),'--dp-args=-v'])
    if not complete('default',11):run('default',[sys.executable,'Grader/tools/local_grade.py','AI_Solutions/CodexChainDP','CodexChainDP','Testcases/testcases','Testcases/holdout','--work',str(OUT/'default'),'--dp-args=-v'])
    old=LAB/'Testcases/results/edge/CodexChainDP'
    if old.exists() and not (OUT/'edge_before').exists():shutil.copytree(old,OUT/'edge_before')
    env['EDGE_WORK_ROOT']=str(OUT);env['EDGE_DP_ARGS']='-v'
    if not complete('edge_CodexChainDP',16):run('edge_default',['bash','Testcases/results/run_edge.sh','CodexChainDP'])
    # Independently require every row to PASS; the legacy shell wrapper can
    # otherwise conceal a nonzero grader exit behind a successful cp/echo.
    rows=list(csv.DictReader((OUT/'edge_CodexChainDP/record.csv').open()))
    if len(rows)!=16 or any(r['verdict']!='PASS' for r in rows):raise SystemExit('edge grading failed')
    fixture=OUT/'large_input';fixture.mkdir(exist_ok=True)
    if not (fixture/'L1').exists():(fixture/'L1').symlink_to(LAB/'Testcases/large/L1',target_is_directory=True)
    if not complete('large_L1',1):run('large_L1',[sys.executable,'Grader/tools/local_grade.py','AI_Solutions/CodexChainDP','CodexChainDP',str(fixture),'--work',str(OUT/'large_L1'),'--timeout','240','--dp-args=-v'])
    rows=list(csv.DictReader((OUT/'large_L1/record.csv').open()))
    if len(rows)!=1 or rows[0]['verdict']!='PASS':raise SystemExit('L1 grading failed')
    print('DEFAULT GRADING COMPLETE',flush=True)

if __name__=='__main__':main()
