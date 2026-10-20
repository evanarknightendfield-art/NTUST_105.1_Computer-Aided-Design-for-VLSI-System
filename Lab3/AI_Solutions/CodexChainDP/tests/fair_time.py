#!/usr/bin/env python3
"""Three seeds, equal TOTAL charged wall budgets, serial execution.

Stage 1 is seed-independent: charge its measured default elapsed time once per
seed and reuse its legal output. Both get max(Stage1_v4, Stage1_chain)+12 sec;
2 sec is reserved for polish, the remainder for SA. Report actual charged and
process times, not just the requested limits. This is a cached-stage experiment,
not a hard real-time guarantee for the complete command.
"""
import argparse,csv,json,os,re,shutil,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];LAB=ROOT.parents[1]
OUT=LAB/'Testcases/results/CodexChainDP';DEST=OUT/'equal_total_time'
VERSIONS=['ClaudeOpus55_v4','CodexChainDP']

def build():
    for version in VERSIONS:
        src=LAB/'AI_Solutions'/version/'src'
        cmd=['g++','-std=c++17','-O3','-pthread','-I'+str(src),str(ROOT/'tests/sa_bench.cpp')]
        cmd += [str(src/(n+'.cpp')) for n in ['annealer','optimizer','window','placedb']]
        with (DEST/(version+'_build.log')).open('w') as f:
            subprocess.run([*cmd,'-o',str(DEST/(version+'_sa'))],stdout=f,stderr=f,check=True)

def stage1(version,bench,group):
    work=OUT/('baseline_default' if version==VERSIONS[0] else 'edge_CodexChainDP' if group=='edge' else 'default')
    log=(work/'logs'/(version+'.log')).read_text()
    block=log.split('===== '+bench+':',1)[1].split('\n=====',1)[0]
    match=re.search(r'\[stage1\] done\s+([0-9.]+)s',block)
    prefix=work/'results'/version/bench/'output'
    if match:seconds=float(match.group(1))
    else:
        # local_grade only retains the tail of stderr. Re-measure if the
        # stage timestamp was truncated, using the same frozen executable.
        folder=DEST/'stage1_rerun'/version/bench;folder.mkdir(parents=True,exist_ok=True)
        prefix=folder/'output';start=time.monotonic()
        with (folder/'stdout.txt').open('w') as o,(folder/'stderr.txt').open('w') as e:
            subprocess.run([str(work/'home'/version/'build/dp'),str(LAB/'Testcases'/group/bench/bench),str(prefix),'--no-sa','-v'],stdout=o,stderr=e,check=True)
        seconds=time.monotonic()-start
    cache=DEST/'legal_starts'/version/bench;cache.mkdir(parents=True,exist_ok=True)
    for ext in ['.aux','.nodes','.nets','.wts','.scl','.pl']:
        link=cache/('output'+ext)
        if not link.exists():link.symlink_to(Path(str(prefix)+('_legal.pl' if ext=='.pl' else ext)))
    return seconds,cache/'output'

def main():
    global DEST
    parser=argparse.ArgumentParser()
    parser.add_argument('--schedule',choices=['stress','default'],default='stress')
    parser.add_argument('--suite',default='equal_total_time')
    parser.add_argument('--reuse-stage1',help='reuse an earlier timed suite, including its measured Stage 1 cost')
    args=parser.parse_args();DEST=OUT/args.suite
    cached={}
    if args.reuse_stage1:
        for row in csv.DictReader((OUT/args.reuse_stage1/'record.csv').open()):
            cached[(row['version'],row['bench'])]=(float(row['stage1_seconds']),OUT/args.reuse_stage1/'legal_starts'/row['version']/row['bench']/'output')
    DEST.mkdir(parents=True,exist_ok=True);build()
    (DEST/'protocol.json').write_text(json.dumps(vars(args),indent=2)+'\n')
    records=list(csv.DictReader((DEST/'record.csv').open())) if (DEST/'record.csv').exists() else []
    done={(r['bench'],r['version'],int(r['seed'])) for r in records if r['verdict']=='PASS'}
    for group in ['holdout','edge']:
        for aux in sorted((LAB/'Testcases'/group).glob('*/*.aux')):
            bench=aux.stem;initial={v:cached[(v,bench)] if args.reuse_stage1 else stage1(v,bench,group) for v in VERSIONS}
            budget=max(value[0] for value in initial.values())+12
            for seed in [1,2,3]:
                for version in VERSIONS:
                    if (bench,version,seed) in done:continue
                    d=DEST/version/(bench+'_s'+str(seed));d.mkdir(parents=True,exist_ok=True)
                    s1,start=initial[version];sa=budget-s1-2
                    effort='default' if args.schedule=='default' else '1000000000'
                    cmd=[str(DEST/(version+'_sa')),str(aux.with_suffix('')),str(start),str(d/'output'),str(seed),effort,str(sa),'2','0']
                    (d/'command.json').write_text(json.dumps(cmd,indent=2)+'\n');t=time.monotonic()
                    with (d/'stdout.txt').open('w') as o,(d/'stderr.txt').open('w') as e:
                        p=subprocess.run(cmd,stdout=o,stderr=e,timeout=300)
                    r=dict(group=group,bench=bench,version=version,seed=seed,verdict='FAIL',budget=budget,stage1_seconds=s1,sa_limit=sa,process_seconds=time.monotonic()-t,charged_seconds='',hpwl='',attempts='',sa_seconds='',stage2_seconds='')
                    if p.returncode==0:
                        r.update(next(csv.DictReader((d/'stdout.txt').open())))
                        r['charged_seconds']=s1+float(r['stage2_seconds'])
                        ck=subprocess.run([sys.executable,str(LAB/'Grader/tools/check_pl.py'),str(aux.with_suffix('')),str(d/'output.pl'),'--quiet'],capture_output=True,text=True)
                        (d/'check.txt').write_text(ck.stdout+ck.stderr)
                        if ck.returncode==0:r['verdict']='PASS'
                    records.append(r)
                    with (DEST/'record.csv').open('w') as f:w=csv.DictWriter(f,fieldnames=list(r));w.writeheader();w.writerows(records)
                    print(json.dumps(r),flush=True)

if __name__=='__main__':main()
