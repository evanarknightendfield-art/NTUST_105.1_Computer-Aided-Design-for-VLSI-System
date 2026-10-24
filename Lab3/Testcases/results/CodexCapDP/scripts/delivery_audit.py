#!/usr/bin/env python3
"""Audit saved validation and documentation, without rerunning any experiment."""
import csv
import datetime as dt
import hashlib
import json
import re
from pathlib import Path

R = Path(__file__).resolve().parents[1]
L = R.parents[2]
F = R/'final'
S = L/'AI_Solutions/CodexCapDP'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    with path.open() as f:
        return list(csv.DictReader(f))


freeze = json.loads((R/'freeze.json').read_text())
manifest = (R/'frozen_sources.sha256').read_text().splitlines()
changed = [p for line in manifest for h,p in [line.split('  ',1)] if sha(S/p)!=h]
protected = json.loads((R/'protected_sources.json').read_text())
protected_changed = [p for p,h in protected.items() if sha(L/p)!=h]
assert not changed and not protected_changed
assert sha(R/'frozen_sources.sha256') == freeze['manifest_sha256']
parallel = {p:sha(S/'src'/p)==sha(L/'AI_Solutions/ClaudeOpus55_v5/src'/p)
            for p in ['annealer.cpp','annealer.hpp','pool.hpp','rowmap.hpp']}
assert all(parallel.values())

normal = (R/'phase2/tests_v5.log').read_text()
sanitized = (R/'phase2/asan_tests_v5_unsandboxed.log').read_text()
assert all('100% tests passed, 0 tests failed out of 10' in t for t in [normal,sanitized])
assert not re.search(r'ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:',sanitized)

records = {}
for rel,n in [('lg_new',11),('lg_new_L1',1),('lg_new_L2',1),
              *[(f'seed4_{v}',16) for v in ['ClaudeOpus55_v4','CodexChainDP','ClaudeOpus55_v5','CodexCapDP']]]:
    rows=read(F/rel/'record.csv')
    assert len(rows)==n and len({r['bench'] for r in rows})==n
    assert all(r['verdict']=='PASS' and r['legal_ok']=='True' and r['sa_ok']=='True' for r in rows)
    records[rel]=dict(rows=n,passed=n,sha256=sha(F/rel/'record.csv'))
rows=read(R.parent/'edge/CodexCapDP/record.csv')
assert len(rows)==16 and all(r['verdict']=='PASS' for r in rows)
records['edge_new']=dict(rows=16,passed=16,sha256=sha(R.parent/'edge/CodexCapDP/record.csv'))

hpwl={}
for mode,n in [('fixed',216),('equal',144),('default',216)]:
    rows=read(F/f'hpwl_{mode}/record.csv')
    assert len(rows)==n and all(r['verdict']=='PASS' for r in rows)
    keys={(r['version'],r['bench'],r['seed']) for r in rows}
    assert len(keys)==n
    by={}
    for r in rows:
        by.setdefault((r['version'],r['group'],r['bench']),set()).add(int(r['seed']))
    assert all(seeds=={1,2,3} for seeds in by.values())
    counts={v:sum(r['version']==v for r in rows) for v in {r['version'] for r in rows}}
    assert all(count==72 for count in counts.values())
    if mode!='equal':
        reuse=json.loads((F/f'hpwl_{mode}/reuse.json').read_text())
        assert sha(Path(reuse['source']))==reuse['sha256']
        original={(r['version'],r['bench'],r['seed']):r for r in read(Path(reuse['source']))}
        assert all(r==original[(r['version'],r['bench'],r['seed'])] for r in rows if r['version']!='CodexCapDP')
    hpwl[mode]=dict(rows=n,passed=n,by_version=counts)

done=list(F.glob('*.done.json'))
for path in done:
    m=json.loads(path.read_text())
    assert m['returncode']==0 and m['freeze']==freeze['manifest_sha256'], path
generated=json.loads((F/'generate_seed4.done.json').read_text())
frozen_time=dt.datetime.fromisoformat(freeze['time']).timestamp()
assert generated['started']>frozen_time
assert '20261022' in generated['command'] and '--check' in generated['command']
assert '/home/evan/anaconda3/bin/python3' in generated['command']
assert json.loads((F/'ALL_DONE.json').read_text())['freeze']==freeze['manifest_sha256']

baseline=json.loads((F/'delivery_baseline.json').read_text())
comparison=L/'AI_Solutions/COMPARISON.md'
old=baseline['AI_Solutions/COMPARISON.md']
assert hashlib.sha256(comparison.read_bytes()[:old['bytes']]).hexdigest()==old['sha256']
assert comparison.read_text().count('## 10. CodexCapDP')==1
for p in ['README.md','Testcases/README.md','Testcases/results/edge/README.md']:
    assert sha(L/p)==baseline[p]['sha256']
table_script=L/'Testcases/results/edge/make_table.py'
before=table_script.read_text().replace(", ('CapDP', 'CodexCapDP')",'')
assert hashlib.sha256(before.encode()).hexdigest()==baseline['Testcases/results/edge/make_table.py']['sha256']
table=(L/'Testcases/results/edge/tables.md').read_text()
assert table.count(' | CapDP |')==4
assert table.count('| e01_nearlegal |')==4 and table.count('| e16_rowgaps |')==4

missing=[]
docs=[S/'README.md',F/'summary.md',F/'details.md']
doc_texts=[(p,p.read_text()) for p in docs]+[(comparison,comparison.read_bytes()[old['bytes']:].decode())]
for p,text in doc_texts:
    for target in re.findall(r'\]\(([^)]+)\)',text):
        if '://' in target or target.startswith('#'):
            continue
        if not (p.parent/target.split('#')[0]).exists():
            missing.append(dict(file=str(p),target=target))
assert not missing,missing

report=dict(checked_at=dt.datetime.now(dt.timezone.utc).isoformat(),
    experiments_rerun=False,algorithms_changed_after_freeze=False,
    frozen_files=len(manifest),frozen_changed=changed,manifest_sha256=freeze['manifest_sha256'],
    protected_files=len(protected),protected_changed=protected_changed,parallel_sa_unchanged=parallel,
    normal_cpp='10/10 PASS',asan_ubsan_cpp='10/10 PASS',
    tsan='Not rerun: parallel SA sources unchanged; fixed-work determinism test passed in both suites',
    records=records,hpwl=hpwl,completed_steps=len(done),seed4_generated_after_freeze=True,
    seed4_generated_utc=dt.datetime.fromtimestamp(generated['started'],dt.timezone.utc).isoformat(),
    comparison_existing_prefix_unchanged=True,edge_version_added=True,missing_doc_links=missing,
    delivery_files={str(p.relative_to(L)):sha(p) for p in [*docs,comparison,table_script,L/'Testcases/results/edge/tables.md']},
    note='Audit verifies saved evidence; quality and deadline failures remain documented, not waived.')
(F/'delivery_audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(report,ensure_ascii=False,indent=2))
