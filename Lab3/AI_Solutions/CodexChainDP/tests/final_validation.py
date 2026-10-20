#!/usr/bin/env python3
"""Run fair comparisons, then generate the final unseen suite, serially.
The algorithm source must still match acceptance.py's frozen manifest.
"""
import csv,hashlib,json,os,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];LAB=ROOT.parents[1]
OUT=LAB/'Testcases/results/CodexChainDP'
env=dict(os.environ);env['PATH']='/home/evan/anaconda3/envs/eda/bin:'+env['PATH']

def run(label,cmd):
    print('START '+label,flush=True);start=time.monotonic()
    with (OUT/(label+'.log')).open('w') as f:code=subprocess.call(cmd,cwd=LAB,env=env,stdout=f,stderr=f)
    with (OUT/'final_steps.csv').open('a') as f:csv.writer(f).writerow([label,code,time.monotonic()-start,json.dumps(cmd)])
    print('DONE '+label+' rc='+str(code),flush=True)
    if code:raise SystemExit(code)

def fingerprint(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while block:=f.read(1024*1024):h.update(block)
    return h.hexdigest()

def main():
    while True:
        progress=(OUT/'acceptance_progress.log').read_text()
        if 'DEFAULT GRADING COMPLETE' in progress:break
        if 'Traceback' in progress or 'failed' in progress or any(('rc='+str(i)) in progress for i in range(1,10)):
            raise SystemExit('Prior grading failed; inspect acceptance_progress.log')
        time.sleep(5)
    with (OUT/'final_steps.csv').open('w') as f:csv.writer(f).writerow(['label','returncode','seconds','command'])
    frozen=json.loads((OUT/'frozen_sources.json').read_text())
    for file,expected in frozen.items():
        if fingerprint(LAB/file)!=expected:raise SystemExit('Frozen source changed: '+file)
    run('equal_total_time',[sys.executable,str(ROOT/'tests/fair_time.py')])
    for aux in sorted((LAB/'Testcases/edge').glob('*/*.aux')):
        src=OUT/'edge_CodexChainDP/results/CodexChainDP'/aux.stem/'output'
        dst=OUT/'edge_stage1/CodexChainDP'/(aux.stem+'_s0');dst.mkdir(parents=True,exist_ok=True)
        for ext in ['.aux','.nodes','.nets','.wts','.scl','.pl']:
            link=dst/('output'+ext)
            if not link.exists():link.symlink_to(Path(str(src)+('_legal.pl' if ext=='.pl' else ext)))
    run('fixed_work_edge',[sys.executable,str(ROOT/'tests/measure.py'),'sa','--suite','fixed_work_edge','--groups','edge','--start-suite','edge_stage1','--moves','5000000','--seconds','1000'])
    for suite,count in [('equal_total_time',144),('fixed_work_holdout',48),('fixed_work_edge',96)]:
        rows=list(csv.DictReader((OUT/suite/'record.csv').open()))
        if len(rows)!=count or any(r['verdict']!='PASS' for r in rows):raise SystemExit(suite+' failed or incomplete')
    # Verify default seed compatibility without changing the original suite.
    replay=OUT/'generator_default_replay'
    run('generator_default_replay',[sys.executable,'Grader/tools/gen_edge.py',str(replay),'--check'])
    hashes=[]
    for src in sorted((LAB/'Testcases/edge').glob('*/*')):
        if not src.is_file() or src.suffix not in ['.aux','.nodes','.nets','.wts','.pl','.scl']:continue
        relative=src.relative_to(LAB/'Testcases/edge');new=replay/relative
        old_hash=fingerprint(src);new_hash=fingerprint(new)
        hashes.append(dict(file=str(relative),original_sha256=old_hash,replay_sha256=new_hash,equal=old_hash==new_hash))
    with (OUT/'generator_default_hashes.csv').open('w') as f:w=csv.DictWriter(f,fieldnames=list(hashes[0]));w.writeheader();w.writerows(hashes)
    if not all(row['equal'] for row in hashes):raise SystemExit('Default generator did not reproduce original files')
    for file,expected in frozen.items():
        if fingerprint(LAB/file)!=expected:raise SystemExit('Frozen source changed before unseen validation: '+file)
    fresh=LAB/'Testcases/edge_seed2'
    if fresh.exists():raise SystemExit('Fresh edge_seed2 already exists: do not silently reuse unseen validation')
    run('generate_seed2',[sys.executable,'Grader/tools/gen_edge.py',str(fresh),'--seed','20261020','--check'])
    changes=[]
    for case in sorted(fresh.iterdir()):
        if not case.is_dir():continue
        changed=[p.name for p in case.iterdir() if p.is_file() and p.stem==case.name and p.suffix in ['.aux','.nodes','.nets','.wts','.pl','.scl'] and fingerprint(p)!=fingerprint(LAB/'Testcases/edge'/case.name/p.name)]
        changes.append(dict(bench=case.name,changed=bool(changed),changed_files=';'.join(sorted(changed))))
    with (OUT/'fresh_seed_changes.csv').open('w') as f:w=csv.DictWriter(f,fieldnames=list(changes[0]));w.writeheader();w.writerows(changes)
    run('edge_seed2',[sys.executable,'Grader/tools/local_grade.py','AI_Solutions/CodexChainDP','CodexChainDP',str(fresh),'--work',str(OUT/'edge_seed2'),'--dp-args=-v'])
    rows=list(csv.DictReader((OUT/'edge_seed2/record.csv').open()))
    if len(rows)!=16 or any(r['verdict']!='PASS' for r in rows):raise SystemExit('Unseen edge grading failed')
    run('summarize',[sys.executable,str(ROOT/'tests/summarize.py')])
    print('FINAL VALIDATION COMPLETE',flush=True)

if __name__=='__main__':main()
