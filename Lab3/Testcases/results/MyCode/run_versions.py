#!/usr/bin/env python3
"""run_versions.py — MyCode 的 v1 ~ v5，每版跑 ibm01、ibm05、L1、L2，一次只跑一個 dp（Lab3/README.md 量測規則）。

照 10/28 的方式：新資料夾裡 `./dp input001 output001`（各版預設參數），CPU 0-15，最多 CAP 秒；
跑完用 Grader/tools/check_pl.py 檢查兩個輸出（E-core 16-19）。拿 Testcases/results/timing_global.lock 才跑。

  python3 run_versions.py                      # 全部（records.csv 已有的筆跳過）
  python3 run_versions.py v3_refine            # 只跑某幾版（逗號分隔）
  python3 run_versions.py v3_refine --cases ibm01,ibm05 --tag v3_noevict --args="--no-evict"
  python3 run_versions.py v3_refine --suite ibm-place2   # 10/28 HINT 的 16 筆（只看跑不跑得完、合不合法、多快）

執行檔從 Lab3/MyCode/<版本>/ 編譯到 build/<版本>/（原始碼資料夾裡不留 build）。
"""
import argparse, csv, os, re, shutil, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[2]
TC = LAB / 'Testcases'
CHECK = LAB / 'Grader/tools/check_pl.py'
PY = '/home/evan/anaconda3/bin/python3'
CMAKE = '/home/evan/anaconda3/envs/eda/bin/cmake'
sys.path.insert(0, str(LAB / 'Testcases/results'))
from machine_lock import machine_lock  # noqa: E402

VERSIONS = ['v1_abacus', 'v2_sa', 'v3_refine', 'v4_match_polish', 'v5_final']
SUITES = {
    'main': {'ibm01': 'testcases/ibm01/ibm01', 'ibm05': 'testcases/ibm05/ibm05', 'L1': 'large/L1/L1', 'L2': 'large/L2/L2'},
    'ibm-place2': {f'{c}-cu{u}': f'ibm-place2/{c}/{c}-cu{u}' for c, us in
                   [('ibm01', (85, 88)), ('ibm02', (90, 95)), ('ibm07', (90, 95)), ('ibm08', (90, 95)),
                    ('ibm09', (90, 95)), ('ibm10', (90, 95)), ('ibm11', (90, 95)), ('ibm12', (85, 90))] for u in us},
}
CPUS = '0-15'
FIELDS = ['version', 'case', 'wall_s', 'rss_mb', 'rc', 'timed_out', 'files_ok', 'legal_ok', 'final_ok', 'stdout_ok',
          'disp', 'max_disp', 'hpwl_legal', 'hpwl_final', 'official_pass', 'finished', 'error']


def build(v):
    out = HERE / 'build' / v
    subprocess.run([CMAKE, '-S', str(LAB / 'MyCode' / v), '-B', str(out)], check=True, capture_output=True)
    subprocess.run(['taskset', '-c', '16-19', CMAKE, '--build', str(out), '-j4'], check=True, capture_output=True)
    return out / 'dp'


def aux_files(prefix):
    """(檔名, 實際路徑)：照 .aux 裡列的檔案（ibm-place2 的 .nodes 是共用的 ibmXX.nodes）。"""
    aux = Path(str(prefix) + '.aux')
    files = []
    for tok in aux.read_text().replace(':', ' ').split():
        ext = tok.rsplit('.', 1)[-1].lower()
        if ext in ('nodes', 'nets', 'wts', 'pl', 'scl'):
            files.append((ext, aux.parent / tok))
    return files


def run_one(tag, binary, case, path, extra, cap):
    work = HERE / 'runs' / f'{tag}_{case}'
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    files = aux_files(TC / path)
    for ext, src in files:
        os.symlink(src, work / f'input001.{ext}')
    (work / 'input001.aux').write_text('RowBasedPlacement : ' + ' '.join(f'input001.{e}' for e, _ in files) + '\n')
    shutil.copy(binary, work / 'dp')
    cmd = ['taskset', '-c', CPUS, '/usr/bin/time', '-f', 'TIME %e %M', '-o', 'time.txt', './dp', 'input001', 'output001', *extra]
    with open(work / 'stdout.txt', 'w') as so, open(work / 'stderr.txt', 'w') as se:
        p = subprocess.Popen(cmd, cwd=work, stdout=so, stderr=se, start_new_session=True)
        try:
            rc, timed_out = p.wait(timeout=cap), False
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, 9); p.wait(); rc, timed_out = -9, True
    m = re.search(r'TIME ([\d.]+) (\d+)', (work / 'time.txt').read_text()) if (work / 'time.txt').exists() else None
    wall = float(m.group(1)) if m and not timed_out else (cap if timed_out else None)
    rss = round(int(m.group(2)) / 1024, 1) if m else None
    nums = [float(x) for x in re.findall(r':\s*([-\d.eE+]+)\s*$', (work / 'stdout.txt').read_text(), re.M)][-4:]
    files_ok = all((work / f'output001.{e}').exists() for e in ('aux', 'nodes', 'nets', 'pl', 'scl')) and \
        (work / 'output001_legal.pl').exists()
    errs = [l for l in (work / 'stderr.txt').read_text().splitlines() if l.startswith('error')]
    row = dict(version=tag, case=case, wall_s=wall, rss_mb=rss, rc=rc, timed_out=timed_out, files_ok=files_ok,
               error=errs[0] if errs else '')
    chk = {}
    for name in ('output001_legal.pl', 'output001.pl'):
        if not (work / name).exists():
            chk[name] = (False, None, None, None); continue
        c = subprocess.run(['taskset', '-c', '16-19', PY, CHECK, str(TC / path), str(work / name), '--quiet'],
                           capture_output=True, text=True)
        g = lambda k: float(re.search(k + r': ([-\d.e+]+)', c.stdout).group(1)) if re.search(k + r': ', c.stdout) else None
        chk[name] = ('LEGAL' in c.stdout and 'ILLEGAL' not in c.stdout, g('Total displacement'),
                     g('Maximum displacement'), g('Total HPWL'))
    legal, final = chk['output001_legal.pl'], chk['output001.pl']
    want = [legal[1], legal[2], legal[3], final[3]]
    row.update(legal_ok=legal[0], final_ok=final[0], disp=want[0], max_disp=want[1], hpwl_legal=want[2], hpwl_final=want[3])
    row['stdout_ok'] = len(nums) == 4 and None not in want and all(
        abs(a - b) <= 1e-4 * max(1.0, abs(b)) + 0.11 for a, b in zip(nums, want))
    row['finished'] = rc == 0 and not timed_out and legal[0] and final[0]
    row['official_pass'] = row['finished'] and files_ok and wall is not None and wall <= 60 and (rss or 0) <= 8192
    for f in work.iterdir():   # 只留 stdout / stderr / time
        if f.name.startswith(('output001', 'input001')) or f.name == 'dp':
            f.unlink()
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('versions', nargs='?', default=','.join(VERSIONS))
    ap.add_argument('--suite', default='main', choices=SUITES)
    ap.add_argument('--cases', default=None, help='逗號分隔，預設整個 suite')
    ap.add_argument('--tag', default=None, help='records 裡的版本名（預設 = 資料夾名；只能配一個版本）')
    ap.add_argument('--args', default='', help='額外的 dp 參數')
    ap.add_argument('--cap', type=float, default=300)
    ap.add_argument('--rerun', action='store_true', help='records 已有的也重跑（附加一列）')
    a = ap.parse_args()
    cases = SUITES[a.suite]
    if a.cases:
        cases = {k: cases[k] for k in a.cases.split(',')}
    out = HERE / ('records.csv' if a.suite == 'main' else f'records_{a.suite}.csv')
    done = {(r['version'], r['case']) for r in csv.DictReader(open(out))} if out.exists() else set()
    versions = a.versions.split(',')
    bins = {v: build(v) for v in versions}
    held = machine_lock(LAB)  # noqa: F841
    for v in versions:
        tag = a.tag or v
        for case, path in cases.items():
            if (tag, case) in done and not a.rerun:
                continue
            row = run_one(tag, bins[v], case, path, a.args.split(), a.cap)
            new = not out.exists()
            with open(out, 'a', newline='') as f:
                w = csv.DictWriter(f, fieldnames=FIELDS)
                if new:
                    w.writeheader()
                w.writerow(row)
            print(f"[{time.strftime('%H:%M:%S')}] {tag:16s} {case:12s} {row['wall_s']} s  finished={row['finished']} "
                  f"pass60={row['official_pass']}  disp={row['disp']} max={row['max_disp']} "
                  f"hpwl={row['hpwl_legal']} -> {row['hpwl_final']} {row['error']}", flush=True)


if __name__ == '__main__':
    main()
