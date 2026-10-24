#!/usr/bin/env python3
"""phase2_train.py TAG [extra dp args] — Stage 1 of the current v7 source on the tuning set, paired with CPC.

v7 version of v6's script. Copies AI_Solutions/ClaudeOpus55_v7 (src + CMakeLists) to phase2/train/TAG/, builds it,
then runs back to back (same session, timing.lock):
  cand  run_ts --no-sa -v on testcases + holdout (11)          <- the only cases used for decisions
  cpc   the same with CodexParallelChain's binary (paired baseline, re-run every time: Stage 1 varies per run)
  obs   candidate on e08 / e16 (observed only, never tuned on)
Writes compare.md (both against phase 1's ChainDP local_grade run, compare_lg.py), pair.md / gates.json (cand ÷ cpc).
"""
import csv, hashlib, json, math, re, shutil, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from v6q import Queue, R, LAB, PY, V5S, build, note  # noqa: E402

tag = sys.argv[1]
extra = ' '.join(sys.argv[2:])
d = R / 'phase2' / 'train' / tag
if not (d / 'src').exists():
    d.mkdir(parents=True, exist_ok=True)
    shutil.copytree(LAB / 'AI_Solutions' / R.name / 'src', d / 'src')
    shutil.copy(LAB / 'AI_Solutions' / R.name / 'CMakeLists.txt', d / 'CMakeLists.txt')
src_hash = hashlib.sha256(b''.join(p.read_bytes() for p in sorted((d / 'src').iterdir()))).hexdigest()
(d / 'protocol.json').write_text(json.dumps(dict(tag=tag, src_sha256=src_hash, dp_args=extra), indent=1) + '\n')
exe = build('v7', d / 'build', src=d)
cpc = R / 'bin' / 'CodexParallelChain' / 'dp'
q = Queue(d)
q.run('cand', [PY, V5S / 'run_ts.py', '--bin', exe, '--out', d / 'ts', f'--dp-args=--no-sa {extra}',
               'Testcases/testcases', 'Testcases/holdout'])
q.run('cpc', [PY, V5S / 'run_ts.py', '--bin', cpc, '--out', d / 'ts_cpc', '--dp-args=--no-sa',
              'Testcases/testcases', 'Testcases/holdout'])
q.run('obs', [PY, V5S / 'run_ts.py', '--bin', exe, '--out', d / 'ts_obs', f'--dp-args=--no-sa {extra}',
              'Testcases/edge/e08_fragmented/e08_fragmented', 'Testcases/edge/e16_rowgaps/e16_rowgaps'])
ref = R / 'phase1' / 'lg_CodexChainDP' / 'record.csv'
subprocess.run([PY, R / 'scripts/compare_lg.py', d / 'compare.md', 'CodexChainDP', ref, 'CPC_paired', d / 'ts_cpc/record.csv',
                f'v7_{tag}', d / 'ts/record.csv', '--csv', d / 'compare.csv'], check=True)


def load(p):
    return {r['bench']: r for r in csv.DictReader(open(p))}


def extra_used(run_dir):
    for ln in open(run_dir / 'stderr_ts.tsv'):
        m = re.search(r'\[extra\] budget ([\d.]+) used ([\d.]+)', ln)
        if m:
            return float(m.group(1)), float(m.group(2))
    return None, None


def chain_phase(run_dir):
    for ln in open(run_dir / 'stderr_ts.tsv'):
        m = re.search(r'\[chain-phase\] total ([\d.]+) -> ([\d.]+).*\s([\d.]+)s of', ln)
        if m:
            return float(m.group(1)), float(m.group(2)), float(m.group(3))
    return None


gm = lambda xs: math.exp(sum(map(math.log, xs)) / len(xs))
c, b, r0 = load(d / 'ts/record.csv'), load(d / 'ts_cpc/record.csv'), load(ref)
rows, mr, tr, both_c, both_b, over = [], [], [], 0, 0, []
for k in r0:
    if k not in c or k not in b:
        continue
    m, t, mb, tb = float(c[k]['max']), float(c[k]['disp']), float(b[k]['max']), float(b[k]['disp'])
    m0, t0 = float(r0[k]['max']), float(r0[k]['disp'])
    both_c += m <= m0 + 1e-9 and t <= t0 * 1.005 + 1e-9
    both_b += mb <= m0 + 1e-9 and tb <= t0 * 1.005 + 1e-9
    rm, rt = (m / mb if mb else 1.0), (t / tb if tb else 1.0)
    if mb or m:
        mr.append(rm)
    if tb or t:
        tr.append(rt)
    bud, used = extra_used(d / 'ts' / k)
    if bud is not None and used > bud:
        over.append((k, used - bud))
    cp = chain_phase(d / 'ts' / k)
    rows.append([k, f'{m:.4f}', f'{mb:.4f}', f'{(rm - 1) * 100:+.3f}%', f'{t:.2f}', f'{tb:.2f}', f'{(rt - 1) * 100:+.4f}%',
                 c[k].get('t_stage1'), b[k].get('t_stage1'), f'{used:.2f}/{bud:.0f}' if bud is not None else '',
                 f'{100 * (cp[0] - cp[1]) / cp[0]:.3f}% / {cp[2]:.2f}s' if cp and cp[0] else ''])
gates = dict(both_cand=both_c, both_cpc=both_b, n=len(rows), max_gm=gm(mr), total_gm=gm(tr),
             worst_max=max(mr) if mr else None, extra_over=over,
             stage1_cand=sum(float(c[k]['t_stage1'] or 0) for k in c), stage1_cpc=sum(float(b[k]['t_stage1'] or 0) for k in b))
(d / 'gates.json').write_text(json.dumps(gates, indent=1) + '\n')
head = ['bench', '最大', 'CPC 最大', '最大差', '總', 'CPC 總', '總差', 'Stage1 秒', 'CPC 秒', 'extra 用/預算', 'chain 收穫/秒']
md = f'# {tag}：候選 ÷ 同輪 CPC（訓練 11 筆，--no-sa）\n\n'
md += '| ' + ' | '.join(head) + ' |\n|' + '---|' * len(head) + '\n' + ''.join('| ' + ' | '.join(map(str, r)) + ' |\n' for r in rows)
md += (f"\n雙條件（對 ChainDP phase1）：候選 {both_c}/{len(rows)}、CPC {both_b}/{len(rows)}；最大 GM ÷CPC {gates['max_gm']:.5f}、"
       f"總 GM ÷CPC {gates['total_gm']:.5f}、最差逐筆最大 {gates['worst_max']:.5f}；Stage 1 秒 {gates['stage1_cand']:.1f} vs {gates['stage1_cpc']:.1f}；"
       f"額外步驟超出 {over}\n")
(d / 'pair.md').write_text(md)
subprocess.run([PY, R / 'scripts/stage1_phases.py', d / 'stage1', d / 'ts'], check=True, stdout=subprocess.DEVNULL)
note(f'phase2/train/{tag}（{extra or "預設"}）：雙條件 {both_c} vs CPC {both_b}/11，最大 GM {gates["max_gm"]:.5f}，'
     f'總 GM {gates["total_gm"]:.5f}，Stage1 {gates["stage1_cand"]:.0f}/{gates["stage1_cpc"]:.0f} 秒；見 phase2/train/{tag}/pair.md')
