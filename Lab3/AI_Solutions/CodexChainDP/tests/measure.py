#!/usr/bin/env python3
"""Serial, resumable experiments; all artifacts live under Testcases/results/CodexChainDP.

stage1: default candidate with --no-sa, or baseline supplied via --exe.
sa: common drivers against the SAME cached legal start, three seeds.
Record actual attempts and wall time separately; fixed work is not fixed time.
"""
import argparse,csv,json,subprocess,time,sys,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
LAB=ROOT.parents[1]
RESULTS=LAB/'Testcases/results'/ROOT.name

def build_drivers(out):
    """Build before timed runs, recording exactly which frozen code was used."""
    folder=out/'drivers';folder.mkdir(exist_ok=True)
    drivers={}
    for version in ['ClaudeOpus55_v4','CodexChainDP']:
        src=LAB/'AI_Solutions'/version/'src';harness=ROOT/'tests/sa_bench.cpp'
        files=[harness,*sorted(src.glob('*.hpp')),*[src/(n+'.cpp') for n in ['annealer','optimizer','window','placedb']]]
        hashes={str(p.relative_to(LAB)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
        exe=folder/(version+'_sa');manifest=folder/(version+'_sources.json')
        if not exe.exists() or not manifest.exists() or json.loads(manifest.read_text())!=hashes:
            cmd=['g++','-std=c++17','-O3','-pthread','-I'+str(src),str(harness),*[str(src/(n+'.cpp')) for n in ['annealer','optimizer','window','placedb']],'-o',str(exe)]
            with (folder/(version+'_build.log')).open('w') as log:subprocess.run(cmd,stdout=log,stderr=log,check=True)
            manifest.write_text(json.dumps(hashes,indent=2)+'\n')
        drivers[version]=exe
    return drivers

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('mode',choices=['stage1','sa'])
    ap.add_argument('--suite',required=True)
    ap.add_argument('--groups',default='testcases,holdout')
    ap.add_argument('--benches',default='')
    ap.add_argument('--exe',default=str(RESULTS/'build/dp'))
    ap.add_argument('--start-suite',default='train_stage1')
    ap.add_argument('--seconds',type=float,default=1000)
    ap.add_argument('--moves',type=int,default=1000000)
    ap.add_argument('--seeds',default='1,2,3')
    ap.add_argument('--polish',type=float,default=0)
    ap.add_argument('--extra',default='')
    a=ap.parse_args();out=RESULTS/a.suite;out.mkdir(parents=True,exist_ok=True)
    drivers=build_drivers(out) if a.mode=='sa' else {}
    benches=[]
    for group in a.groups.split(','):
        for aux in sorted((LAB/'Testcases'/group).glob('*/*.aux')):
            if not a.benches or aux.stem in a.benches.split(','):benches.append((group,aux.with_suffix('')))
    records=list(csv.DictReader((out/'record.csv').open())) if (out/'record.csv').exists() else []
    done={(r['bench'],r['version'],str(r['seed'])) for r in records if r['verdict']=='PASS'}
    def run(cmd,d):
        start=time.monotonic()
        with (d/'stdout.txt').open('w') as o,(d/'stderr.txt').open('w') as e:
            try:p=subprocess.run(cmd,stdout=o,stderr=e,timeout=600);code=p.returncode
            except subprocess.TimeoutExpired:code=124
        return code,time.monotonic()-start
    for group,bench in benches:
        variants=[('CodexChainDP',0)] if a.mode=='stage1' else [(v,int(seed)) for seed in a.seeds.split(',') for v in ('ClaudeOpus55_v4','CodexChainDP')]
        for version,seed in variants:
            if (bench.name,version,str(seed)) in done:continue
            d=out/version/(bench.name+'_s'+str(seed));d.mkdir(parents=True,exist_ok=True)
            prefix=d/'output'
            if a.mode=='stage1':
                import shlex
                cmd=[a.exe,str(bench),str(prefix),'--no-sa','-v',*shlex.split(a.extra)]
            else:
                initial=RESULTS/a.start_suite/'CodexChainDP'/(bench.name+'_s0')/'output'
                cmd=[str(drivers[version]),str(bench),str(initial),str(prefix),str(seed),str(a.moves),str(a.seconds),str(a.polish)]
            (d/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
            code,elapsed=run(cmd,d)
            r=dict(group=group,bench=bench.name,version=version,seed=seed,verdict='FAIL',seconds=elapsed,disp='',max='',hpwl='',attempts='',sa_seconds='',stage2_seconds='')
            if code==0:
                if a.mode=='stage1':
                    vals={k:float(v) for k,v in (line.split(':') for line in (d/'stdout.txt').read_text().splitlines() if ':' in line)}
                    r.update(disp=vals['Total displacement (legal)'],max=vals['Maximum displacement (legal)'],hpwl=vals['Total HPWL (SA)'])
                else:r.update(next(csv.DictReader((d/'stdout.txt').open())))
                check=subprocess.run([sys.executable,str(LAB/'Grader/tools/check_pl.py'),str(bench),str(prefix)+'.pl','--quiet'],capture_output=True,text=True)
                (d/'check.txt').write_text(check.stdout+check.stderr)
                if check.returncode==0:r['verdict']='PASS'
            records.append(r)
            with (out/'record.csv').open('w') as f:
                w=csv.DictWriter(f,fieldnames=list(r));w.writeheader();w.writerows(records)
            print(json.dumps(r),flush=True)

if __name__=='__main__':main()
