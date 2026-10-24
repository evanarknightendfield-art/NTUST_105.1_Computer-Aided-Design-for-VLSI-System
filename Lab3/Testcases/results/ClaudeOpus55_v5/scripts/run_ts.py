#!/usr/bin/env python3
"""run_ts.py — run a dp binary with -v and time-stamp every stderr line.

Used for the per-phase time breakdown (diagnosis.md). The program itself is not
changed: each stderr line gets the wall time at which it arrived, and phases are
cut at the last line each phase prints. local_grade.py keeps only the last
3000 characters of stderr, which loses the Stage 1 lines; this keeps all of it.

usage: run_ts.py --bin BIN --out DIR BENCH_DIR_OR_PREFIX ... [--dp-args "..."] [--timeout S]
writes DIR/<bench>/{stderr_ts.tsv,stdout.txt,output*} and DIR/record.csv
"""
import argparse, csv, glob, os, re, shlex, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from phases import parse_phases, FIELDS  # noqa: E402


def benches(paths):
    out = []
    for p in paths:
        if os.path.isdir(p):
            aux = sorted(glob.glob(os.path.join(os.path.abspath(p), '*', '*.aux')))
            if not aux and glob.glob(os.path.join(p, '*.aux')):
                aux = sorted(glob.glob(os.path.join(os.path.abspath(p), '*.aux')))
            out += [a[:-4] for a in aux]
        else:
            out.append(os.path.abspath(p))
    return out


def run_one(binary, prefix, rdir, dp_args, timeout):
    os.makedirs(rdir, exist_ok=True)
    cmd = [binary, prefix, 'output', '-v', '--trace', 'trace.csv'] + shlex.split(dp_args)
    t0 = time.monotonic()
    p = subprocess.Popen(cmd, cwd=rdir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                         stdin=subprocess.DEVNULL, bufsize=1)
    lines, out = [], []

    def rd_err():
        for line in p.stderr:
            lines.append((time.monotonic() - t0, line.rstrip('\n')))

    def rd_out():
        for line in p.stdout:
            out.append(line)

    th = [threading.Thread(target=rd_err), threading.Thread(target=rd_out)]
    for t in th:
        t.start()
    status = 'OK'
    try:
        rc = p.wait(timeout=timeout)
        if rc:
            status = f'EXIT rc={rc}'
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()
        status = 'TIMEOUT'
    for t in th:
        t.join()
    sec = time.monotonic() - t0
    with open(os.path.join(rdir, 'stderr_ts.tsv'), 'w') as f:
        for t, l in lines:
            f.write(f'{t:.3f}\t{l}\n')
    with open(os.path.join(rdir, 'stdout.txt'), 'w') as f:
        f.write(''.join(out))
    return status, sec, lines, ''.join(out)


def stdout_numbers(text):
    keys = {'Total displacement (legal)': 'disp', 'Maximum displacement (legal)': 'max',
            'Total HPWL (legal)': 'hpwl_legal', 'Total HPWL (SA)': 'hpwl_sa'}
    rep = {}
    for line in text.splitlines():
        k, _, v = line.rpartition(':')
        if k.strip() in keys:
            try:
                rep[keys[k.strip()]] = float(v)
            except ValueError:
                pass
    return rep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--dp-args', default='')
    ap.add_argument('--timeout', type=float, default=900)
    ap.add_argument('--append', action='store_true', help='append to an existing record.csv')
    ap.add_argument('inputs', nargs='+')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    rec_path = os.path.join(a.out, 'record.csv')
    fields = ['bench', 'status', 'seconds', 'disp', 'max', 'hpwl_legal', 'hpwl_sa'] + FIELDS
    mode = 'a' if a.append and os.path.exists(rec_path) else 'w'
    with open(rec_path, mode, newline='') as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction='ignore')
        if mode == 'w':
            w.writeheader()
        for prefix in benches(a.inputs):
            name = os.path.basename(prefix)
            rdir = os.path.join(a.out, name)
            status, sec, lines, out = run_one(os.path.abspath(a.bin), prefix, rdir, a.dp_args, a.timeout)
            rec = {'bench': name, 'status': status, 'seconds': round(sec, 2)}
            rec.update(stdout_numbers(out))
            rec.update(parse_phases(lines, os.path.join(rdir, 'trace.csv')))
            w.writerow(rec)
            f.flush()
            print(f"[run] {name:16s} {status} {sec:7.1f}s  stage1 {rec.get('t_stage1')}  sa {rec.get('sa_seconds')} "
                  f"att {rec.get('sa_attempts')}  max {rec.get('max')}  hpwl {rec.get('hpwl_sa')}", flush=True)


if __name__ == '__main__':
    main()
