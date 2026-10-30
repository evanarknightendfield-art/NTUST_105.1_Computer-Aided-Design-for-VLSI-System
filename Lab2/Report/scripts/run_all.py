import argparse
import csv
import os
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor

from common import INPUT_DIR, RESULT_DIR, VERSIONS, binary, case_ids, parse_log
import verifier


def run_one(tag, inp, out_dir, cid, timeout):
    out = os.path.join(out_dir, f'output{cid}.cbi')
    log = os.path.join(out_dir, f'log{cid}.txt')
    t0 = time.time()
    try:
        r = subprocess.run([binary(tag), inp, out], capture_output=True, text=True, timeout=timeout)
        text = r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        text = 'TIMEOUT\n'
    sec = time.time() - t0
    open(log, 'w').write(text)
    status = 'MISSING'
    if os.path.exists(out):
        try:
            ok = verifier.verify(verifier.parse_input(inp), verifier.parse_output(out))[0]
            status = 'VALID' if ok else 'INVALID'
        except Exception:
            status = 'INVALID'
    stages, final = parse_log(log)
    return dict(version=tag, case=cid, status=status, seconds=round(sec, 2),
                score=final['score'] if final else '', tmax=final['tmax'] if final else '',
                tmin=final['tmin'] if final else '')


def main():
    ap = argparse.ArgumentParser(description='每個版本跑一組測資，輸出到 results/<set>/<version>/')
    ap.add_argument('--set', default='testcases', help='Testcases/ 底下的資料夾名稱')
    ap.add_argument('--jobs', type=int, default=max(1, min(8, (os.cpu_count() or 2) // 2)))
    ap.add_argument('--timeout', type=int, default=60)
    ap.add_argument('--versions', default=','.join(v[0] for v in VERSIONS))
    a = ap.parse_args()

    in_dir = os.path.join(INPUT_DIR, a.set)
    ids = case_ids(in_dir)
    tags = a.versions.split(',')
    jobs = []
    for tag in tags:
        out_dir = os.path.join(RESULT_DIR, a.set, tag)
        os.makedirs(out_dir, exist_ok=True)
        for cid in ids:
            jobs.append((tag, os.path.join(in_dir, f'input{cid}.cbi'), out_dir, cid, a.timeout))

    print(f'[run] set={a.set} cases={len(ids)} versions={tags} jobs={a.jobs}', flush=True)
    rows = []
    with ThreadPoolExecutor(a.jobs) as pool:
        for r in pool.map(lambda j: run_one(*j), jobs):
            rows.append(r)
            print(f"  {r['version']} {r['case']}: {r['status']:<8} score={r['score']:<8} {r['seconds']}s", flush=True)

    summary = os.path.join(RESULT_DIR, a.set, 'summary.csv')
    if os.path.exists(summary):
        rows += [r for r in csv.DictReader(open(summary)) if r['version'] not in tags]
    with open(summary, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(sorted(rows, key=lambda r: (r['version'], r['case'])))
    bad = [r for r in rows if r['status'] != 'VALID']
    print(f'[run] wrote {summary}; not VALID: {len(bad)}')


if __name__ == '__main__':
    main()
