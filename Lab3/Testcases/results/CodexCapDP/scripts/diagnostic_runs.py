#!/usr/bin/env python3
"""Serial, resumable measurements; execute only after baseline.done exists."""
import csv,json,os,subprocess,time,hashlib
from pathlib import Path
R=Path('Testcases/results/CodexCapDP').resolve()
S=Path('AI_Solutions/CodexCapDP/WORK_STATE.md')
B=R/'bin/diagnostic_experiments'; DP=R/'bin/dp_diagnostic'
def state(text):
    with S.open('a') as f:f.write('\n- '+text+'\n')
def run(name,args,env=None):
    folder=R/name;folder.mkdir(parents=True,exist_ok=True)
    done=folder/'done.json'
    if done.exists():
        print('skip',name,flush=True);return
    print('start',name,flush=True);t=time.monotonic()
    with (folder/'stdout.log').open('w') as out,(folder/'stderr.log').open('w') as err:
        p=subprocess.run([str(x) for x in args],env=env,stdout=out,stderr=err)
    result={'command':[str(x) for x in args],'seconds':time.monotonic()-t,'returncode':p.returncode}
    (folder/'execution.json').write_text(json.dumps(result,indent=2))
    if p.returncode:raise RuntimeError(name+' failed; inspect stderr.log')
    done.write_text(json.dumps(result,indent=2));state(f'{name} 完成（{result["seconds"]:.2f} 秒），資料：`results/CodexCapDP/{name}/`。')
    print('done',name,round(result['seconds'],2),flush=True)
assert (R/'baseline.done').exists(),'baseline must finish before timed work'
for n in range(1,9):
    bench=f'h{n:02}';prefix=Path(f'Testcases/holdout/{bench}/{bench}').resolve()
    dest=R/'run_C'/bench;dest.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env['CAP_DIAG_PREFIX']=str(dest/'diag')
    run(f'run_C/{bench}',[DP,prefix,dest/'output','--no-sa','-v'],env)
state('Run C：8 筆 holdout 的 pre-chain/pre-cap 與 cap 嘗試紀錄均已保存；接下來只重用快取起點。')
for n in range(1,9):
    bench=f'h{n:02}';prefix=Path(f'Testcases/holdout/{bench}/{bench}').resolve();d=R/'run_C'/bench
    run(f'chain/{bench}',[B,'chain',prefix,d/'diag_prechain.pl',R/'chain'/bench/'result',d/'diag_prechain.memo'])
for bench in ['h01','h04','h06','h07','h08']:
    prefix=Path(f'Testcases/holdout/{bench}/{bench}').resolve();d=R/'run_C'/bench
    for stage,pl in [('precap',d/'diag_precap.pl'),('final_A',R/'run_A_local_grade/results/CodexCapDP'/bench/'output_legal.pl')]:
        run(f'plateau/{bench}/{stage}',['taskset','-c','16-19',B,'plateau',prefix,pl,R/'plateau'/bench/stage/'result'])
    rows=list(csv.DictReader((d/'diag_attempts.csv').open()))
    full=[r for r in rows if r['kind']=='full' and r['feasible']=='1']
    if full:
        # Predeclared rule: most inflated full candidate, to diagnose the costly rejection.
        a=max(full,key=lambda a:float(a['raw_total'])/float(a['start_total']))
        stem=d/('diag_attempt'+a['id'])
        run(f'local_dp/{bench}',[B,'dp',prefix,str(stem)+'_from.pl',R/'local_dp'/bench/'result',str(stem)+'_raw.pl',str(stem)+'.memo',a['D']])
        (R/'local_dp'/bench/'candidate.json').write_text(json.dumps(a,indent=2))
    # Independent row-repair forensic run, fixed seed and 1% cap reduction.
    # The cap uses the known pre-cap max, read from the first probe's start_max.
    D=float(rows[0]['start_max'])*.99
    run(f'forensic/{bench}',[B,'repair',prefix,d/'diag_precap.pl',R/'forensic'/bench/'result',d/'diag_precap.memo',D])
state('Chain、頂部密度、局部 DP、membership 分解量測完成；下一步 L2 單 seed 排程。')
pl=Path('Testcases/results/ClaudeOpus55_v5/final/lg_v5_L2/results/ClaudeOpus55_v5/L2/output_legal.pl').resolve()
assert pl.exists(),pl
run('L2',[B,'schedule',Path('Testcases/large/L2/L2').resolve(),pl,R/'L2/result'])
(R/'L2/input.json').write_text(json.dumps({'placement':str(pl),'sha256':hashlib.sha256(pl.read_bytes()).hexdigest(),'source_record':'Testcases/results/ClaudeOpus55_v5/final/lg_v5_L2/record.csv','seed':1},indent=2))
state('L2 三種排程各一次、seed=1、同一既有合法起點完成；全部指定量測完成，開始彙整 diagnosis.md。')
(R/'diagnostics.done').write_text('complete\n')
