#!/usr/bin/env python3
"""
local_grade.py — 在本機模擬繳交與評分（Project 3 沒有給 TA 的 grader，這是自己做的版本）

usage:
  python3 local_grade.py PROJECT_DIR SID TESTCASE_DIR [TESTCASE_DIR ...] [--work WORKDIR] [--timeout SEC]
                        [--dp-args "..."]

  PROJECT_DIR   你的專案資料夾（src/、CMakeLists.txt 或 Makefile、ReadMe）
  SID           學號（檔名用），例如 M11502102
  TESTCASE_DIR  放 benchmark 的資料夾：裡面每個子資料夾 X/ 有 X.aux（例如 Testcases/testcases）
  --work        輸出資料夾，預設 Lab3/Testcases/results/local_grade/<SID>/（不進 git）

流程：
  1. 打包 PROJECT_DIR → WORKDIR/submissions/<SID>.tgz（不含 build/ out/ results/）   ← 你要交的 tgz
  2. 解壓縮到 WORKDIR/home/<SID>/
  3. 編譯：有 CMakeLists.txt 就 cmake，否則 make；找 build/dp 或 ./dp
  4. 每個 benchmark：在 WORKDIR/results/<SID>/<bench>/ 執行  dp <bench 路徑> output
     （跟 PDF 的範例一樣，輸出前綴叫 output），限時 --timeout 秒
  5. 檢查 7 個輸出檔都在、stdout 有 4 行、check_pl.py 判斷 output_legal.pl 和 output.pl 合法，
     而且 stdout 印的數字跟 check_pl.py 算的一樣
  6. 寫 WORKDIR/record.csv（每筆）和 WORKDIR/summary.csv
"""
import argparse
import csv
import glob
import os
import shlex
import shutil
import subprocess
import sys
import tarfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CHECK = os.path.join(HERE, 'check_pl.py')
SKIP_DIRS = {'build', 'out', 'results', '.git', '__pycache__'}
OUT_FILES = ['output.aux', 'output.nodes', 'output.nets', 'output.wts', 'output.scl', 'output.pl', 'output_legal.pl']
KEYS = {'Total displacement (legal)': 'disp', 'Maximum displacement (legal)': 'max',
        'Total HPWL (legal)': 'hpwl_legal', 'Total HPWL (SA)': 'hpwl_sa'}


def pack(project, sid, tgz):
    def keep(info):
        parts = info.name.split('/')
        return None if any(p in SKIP_DIRS for p in parts[1:]) else info
    with tarfile.open(tgz, 'w:gz') as t:
        t.add(project, arcname=sid, filter=keep)
    with tarfile.open(tgz) as t:
        return sorted(m.name for m in t.getmembers() if m.isfile())


def build(src, log):
    t = time.time()
    if os.path.exists(os.path.join(src, 'CMakeLists.txt')):
        cmds = [['cmake', '-S', '.', '-B', 'build'], ['cmake', '--build', 'build', '-j']]
    else:
        cmds = [['make']]
    for c in cmds:
        p = subprocess.run(c, cwd=src, capture_output=True, text=True)
        log.write(f'$ {" ".join(c)}\n{p.stdout}{p.stderr}\n')
        if p.returncode:
            return None, time.time() - t
    for exe in ('build/dp', 'dp'):
        if os.path.isfile(os.path.join(src, exe)):
            return os.path.join(src, exe), time.time() - t
    return None, time.time() - t


def check(bench_prefix, pl):
    p = subprocess.run([sys.executable, CHECK, bench_prefix, pl, '--quiet'], capture_output=True, text=True)
    vals = {'legal': p.stdout.startswith('LEGAL'), 'msg': p.stdout.splitlines()[1].strip() if p.returncode else ''}
    for line in p.stdout.splitlines():
        if ':' in line:
            k, v = line.split(':', 1)
            try:
                vals[k.strip()] = float(v.split()[0])
            except (ValueError, IndexError):
                pass
    return vals


def close(a, b):
    return a is not None and b is not None and abs(a - b) <= max(0.11, 1e-6 * abs(b))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('project')
    ap.add_argument('sid')
    ap.add_argument('testcases', nargs='+')
    ap.add_argument('--work')
    ap.add_argument('--timeout', type=float, default=600)
    ap.add_argument('--dp-args', default='')
    a = ap.parse_args()

    # Lab3 convention: run outputs live in Testcases/results/ (local_grade/ is not in git)
    work = os.path.abspath(a.work or os.path.join(HERE, '..', '..', 'Testcases', 'results', 'local_grade', a.sid))
    for sub in ('submissions', 'home', 'results', 'logs'):
        os.makedirs(os.path.join(work, sub), exist_ok=True)
    tgz = os.path.join(work, 'submissions', f'{a.sid}.tgz')
    files = pack(os.path.abspath(a.project), a.sid, tgz)
    print(f'[pack] {tgz}  ({len(files)} files)')
    for f in files:
        print('       ' + f)

    home = os.path.join(work, 'home')
    shutil.rmtree(os.path.join(home, a.sid), ignore_errors=True)
    with tarfile.open(tgz) as t:
        t.extractall(home, filter='data')
    src = os.path.join(home, a.sid)
    log = open(os.path.join(work, 'logs', f'{a.sid}.log'), 'w')
    exe, bsec = build(src, log)
    print(f'[build] {"OK " + os.path.relpath(exe, src) if exe else "FAILED (see logs)"}  {bsec:.1f}s')
    if not exe:
        return 1

    benches = []
    for d in a.testcases:
        for aux in sorted(glob.glob(os.path.join(os.path.abspath(d), '*', '*.aux'))):
            benches.append(aux[:-4])

    records = []
    for prefix in benches:
        name = os.path.basename(prefix)
        rdir = os.path.join(work, 'results', a.sid, name)
        os.makedirs(rdir, exist_ok=True)
        for f in OUT_FILES:
            if os.path.exists(os.path.join(rdir, f)):
                os.remove(os.path.join(rdir, f))
        cmd = [exe, prefix, 'output'] + shlex.split(a.dp_args)
        t = time.time()
        try:
            p = subprocess.run(cmd, cwd=rdir, capture_output=True, text=True, timeout=a.timeout,
                               stdin=subprocess.DEVNULL)
            status, out, err = ('OK' if p.returncode == 0 else f'EXIT rc={p.returncode}'), p.stdout, p.stderr
        except subprocess.TimeoutExpired:
            status, out, err = 'TIMEOUT', '', ''
        sec = time.time() - t
        log.write(f'\n===== {name}: {status} {sec:.1f}s\n$ {" ".join(cmd)}\n{out}{err[-3000:]}\n')

        rep = {}
        for line in out.splitlines():
            k, _, v = line.rpartition(':')
            if k.strip() in KEYS:
                try:
                    rep[KEYS[k.strip()]] = float(v)
                except ValueError:
                    pass
        missing = [f for f in OUT_FILES if not os.path.exists(os.path.join(rdir, f))]
        rec = {'bench': name, 'status': status, 'seconds': round(sec, 2), 'missing': ' '.join(missing)}
        rec.update({k: rep.get(k) for k in KEYS.values()})
        problems = []
        if missing:
            problems.append('missing ' + ' '.join(missing))
        if len(rep) < 4:
            problems.append('stdout has %d of 4 lines' % len(rep))
        if 'output_legal.pl' not in missing:
            c = check(prefix, os.path.join(rdir, 'output_legal.pl'))
            rec['legal_ok'] = c['legal']
            if not c['legal']:
                problems.append('legal.pl ILLEGAL: ' + c['msg'])
            if not (close(rep.get('disp'), c.get('Total displacement')) and close(rep.get('max'), c.get('Maximum displacement'))
                    and close(rep.get('hpwl_legal'), c.get('Total HPWL'))):
                problems.append('stage-1 numbers differ from check_pl (%s / %s / %s)' % (
                    c.get('Total displacement'), c.get('Maximum displacement'), c.get('Total HPWL')))
        if 'output.pl' not in missing:
            c = check(prefix, os.path.join(rdir, 'output.pl'))
            rec['sa_ok'] = c['legal']
            if not c['legal']:
                problems.append('output.pl ILLEGAL: ' + c['msg'])
            if not close(rep.get('hpwl_sa'), c.get('Total HPWL')):
                problems.append('SA HPWL differs from check_pl (%s)' % c.get('Total HPWL'))
        if rep.get('hpwl_sa') is not None and rep.get('hpwl_legal') is not None and rep['hpwl_sa'] > rep['hpwl_legal'] + 1e-6:
            problems.append('SA HPWL worse than legal')
        rec['verdict'] = 'PASS' if status == 'OK' and not problems else 'FAIL'
        rec['problems'] = '; '.join(problems)
        records.append(rec)
        print(f"[run] {name:8s} {rec['verdict']}  {status:9s} {sec:6.1f}s  disp={rep.get('disp')} max={rep.get('max')} "
              f"legal={rep.get('hpwl_legal')} sa={rep.get('hpwl_sa')}" + (f"\n      {rec['problems']}" if problems else ''))

    fields = ['bench', 'verdict', 'status', 'seconds', 'disp', 'max', 'hpwl_legal', 'hpwl_sa', 'legal_ok', 'sa_ok',
              'missing', 'problems']
    with open(os.path.join(work, 'record.csv'), 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction='ignore')
        w.writeheader()
        w.writerows(records)
    npass = sum(r['verdict'] == 'PASS' for r in records)
    with open(os.path.join(work, 'summary.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['sid', 'cases', 'pass', 'build_seconds', 'max_seconds'])
        w.writerow([a.sid, len(records), npass, round(bsec, 1), max((r['seconds'] for r in records), default=0)])
    print(f'[summary] {npass}/{len(records)} PASS  →  {work}/record.csv')
    return 0 if npass == len(records) else 1


if __name__ == '__main__':
    sys.exit(main())
