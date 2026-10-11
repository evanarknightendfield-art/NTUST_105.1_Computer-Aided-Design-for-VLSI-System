#!/usr/bin/env python3
"""
local_grade.py — 在本機模擬 TA 的評分流程（不需要 qemu VM）

usage:
  python local_grade.py PROJECT_DIR SID INPUTS_DIR [--work WORKDIR] [--timeout 60]

  PROJECT_DIR  你的專案資料夾（裡面有 CMakeLists.txt, src/, README.md）
  SID          學號，例如 M11312345（必須符合 [MB]\\d{8}，跟 driver.py 一樣）
  INPUTS_DIR   放 input001.cbi, input002.cbi ... 的資料夾

流程（對照 grader/ 裡的檔案）：
  1. 打包 PROJECT_DIR → WORKDIR/submissions/<SID>.zip（不含 build/ results/ inputs/）   ← 你要交的 zip
  2. 解壓縮到 WORKDIR/vm_home/<SID>/，刪掉 build/ results/ inputs/，放入測資          ← grade.sh
  3. cmake -S . -B build && cmake --build build（合計 60 秒），檢查 build/cbi           ← executor.py
  4. 每筆 ./cbi inputXXX.cbi outputXXX.cbi，限時 60 秒，stdout/stderr 丟掉              ← executor.py
  5. 把 outputs 複製到 WORKDIR/results/<SID>/，執行 TA 的 verifier.py                  ← driver.py
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
VERIFIER = os.path.join(HERE, '..', 'grader', 'grader', 'verifier.py')
SKIP_DIRS = {'build', 'results', 'inputs', '.git', '__pycache__'}


def pack(project, sid, zip_path):
    """把專案打包成 <SID>.zip，zip 裡最上層是 <SID>/ 資料夾"""
    n = 0
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as z:
        for root, dirs, files in os.walk(project):
            rel = os.path.relpath(root, project)
            if rel == '.':
                dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
            for f in files:
                full = os.path.join(root, f)
                arc = os.path.join(sid, os.path.relpath(full, project))
                z.write(full, arc)
                n += 1
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('project')
    ap.add_argument('sid')
    ap.add_argument('inputs')
    ap.add_argument('--work', default=None, help='工作目錄（預設：PROJECT_DIR/../local_grade_<SID>）')
    ap.add_argument('--timeout', type=int, default=60)
    a = ap.parse_args()

    sid = a.sid.upper()
    if not re.fullmatch(r'[MB]\d{8}', sid):
        sys.exit(f'error: SID "{a.sid}" 不符合 [MB]\\d{{8}}，driver.py 會直接跳過這份作業')
    project = os.path.abspath(a.project)
    inputs = sorted(glob.glob(os.path.join(os.path.abspath(a.inputs), 'input[0-9][0-9][0-9].cbi')))
    if not inputs:
        sys.exit(f'error: {a.inputs} 裡沒有 inputXXX.cbi')
    work = os.path.abspath(a.work or os.path.join(project, '..', f'local_grade_{sid}'))
    shutil.rmtree(work, ignore_errors=True)
    for d in ('submissions', 'vm_home', 'inputs', 'results', 'logs'):
        os.makedirs(os.path.join(work, d))
    for p in inputs:
        shutil.copy(p, os.path.join(work, 'inputs'))

    # 1. 打包
    zip_path = os.path.join(work, 'submissions', f'{sid}.zip')
    n = pack(project, sid, zip_path)
    print(f'[1] packed {n} files -> {zip_path} ({os.path.getsize(zip_path)} bytes)')
    with zipfile.ZipFile(zip_path) as z:
        names = z.namelist()
    for need in (f'{sid}/CMakeLists.txt', f'{sid}/README.md'):
        if need not in names:
            print(f'    WARNING: zip 裡沒有 {need}')

    # 2. 解壓（grade.sh）
    tmp = os.path.join(work, 'vm_home', 'unzip')
    subprocess.run([sys.executable, '-m', 'zipfile', '-e', zip_path, tmp], check=True)
    dest = os.path.join(work, 'vm_home', sid)
    shutil.move(os.path.join(tmp, sid) if os.path.isdir(os.path.join(tmp, sid)) else tmp, dest)
    shutil.rmtree(tmp, ignore_errors=True)
    for d in ('build', 'results', 'inputs'):
        shutil.rmtree(os.path.join(dest, d), ignore_errors=True)
    shutil.copytree(os.path.join(work, 'inputs'), os.path.join(dest, 'inputs'))
    print(f'[2] unpacked to {dest}')

    # 3. build（executor.py：configure + build 合計 60 秒）
    log = open(os.path.join(work, 'logs', f'{sid}.log'), 'w')
    build = os.path.join(dest, 'build')
    os.makedirs(build)
    deadline = time.monotonic() + 60
    t0 = time.monotonic()
    for step in (['cmake', '-S', dest, '-B', build], ['cmake', '--build', build]):
        remain = deadline - time.monotonic()
        try:
            rc = subprocess.run(step, stdin=subprocess.DEVNULL, stdout=log, stderr=log, timeout=remain).returncode
        except subprocess.TimeoutExpired:
            sys.exit(f'[3] BUILD_FAIL timeout: {" ".join(step)}')
        if rc != 0:
            sys.exit(f'[3] BUILD_FAIL rc={rc}: {" ".join(step)}  (see {log.name})')
    prog = os.path.join(build, 'cbi')
    if not (os.path.isfile(prog) and os.access(prog, os.X_OK)):
        sys.exit(f'[3] NO_EXECUTABLE: {prog}')
    print(f'[3] build OK in {time.monotonic() - t0:.1f}s -> {prog}')

    # 4. 執行每筆測資
    res_dir = os.path.join(dest, 'results')
    os.makedirs(res_dir)
    print('[4] running testcases')
    for inp in sorted(glob.glob(os.path.join(dest, 'inputs', 'input[0-9][0-9][0-9].cbi'))):
        tid = os.path.basename(inp)[len('input'):-len('.cbi')]
        outf = os.path.join(res_dir, f'output{tid}.cbi')
        t0 = time.monotonic()
        try:
            rc = subprocess.run([prog, inp, outf], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, timeout=a.timeout).returncode
            status = 'OK' if rc == 0 else f'EXIT rc={rc}'
        except subprocess.TimeoutExpired:
            status = f'TIMEOUT (killed at {a.timeout}s)'
        print(f'    {tid}: {status:<10} {time.monotonic() - t0:6.1f}s')

    # 5. 取回 outputs 並評分
    out_dir = os.path.join(work, 'results', sid)
    os.makedirs(out_dir)
    for f in glob.glob(os.path.join(res_dir, 'output[0-9][0-9][0-9].cbi')):
        shutil.copy(f, out_dir)
    subprocess.run([sys.executable, os.path.abspath(VERIFIER)], cwd=work, stdout=subprocess.DEVNULL, check=True)
    print(f'[5] verifier -> {os.path.join(work, "record.csv")}')
    print(open(os.path.join(work, 'record.csv')).read())


if __name__ == '__main__':
    main()
