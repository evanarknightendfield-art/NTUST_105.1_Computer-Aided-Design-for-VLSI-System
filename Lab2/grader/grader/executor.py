#!/usr/bin/env python3
# executor, 以一般使用者權限執行，逐筆讀取 input cbi
#
# usage: python3 executor.py <SID>

import os, sys, glob, subprocess, time

# ---- config ----
if len(sys.argv) != 2:
    print(f"usage: {sys.argv[0]} <SID>")
    sys.exit(1)
SID       = sys.argv[1]                         # 學號
SRC_DIR   = f"/home/grader/{SID}"               # 原始碼路徑
BUILD_DIR = f"/home/grader/{SID}/build"         # 建置路徑
PROG      = f"/home/grader/{SID}/build/cbi"     # 執行檔路徑
INPUT_DIR = f"/home/grader/{SID}/inputs"        # 測資路徑
RESULT_DIR = f"/home/grader/{SID}/results"      # 輸出路徑
TIMEOUT   = 60                                  # 單筆執行時間上限
BUILD_TIMEOUT = 60                              # 建置上限 (configure + build 合計)

# cmake build
def build():
    os.makedirs(BUILD_DIR, exist_ok=True)
    steps = [
        ["cmake", "-S", SRC_DIR, "-B", BUILD_DIR],
        ["cmake", "--build", BUILD_DIR],
    ]
    deadline = time.monotonic() + BUILD_TIMEOUT
    for step in steps:
        remain = deadline - time.monotonic()
        if remain <= 0:
            print(f"BUILD_FAIL timeout: {' '.join(step)}")
            return False
        cmd = ["timeout", "-s", "KILL", f"{remain:.1f}"] + step
        rc = subprocess.run(cmd, stdin=subprocess.DEVNULL).returncode
        if rc != 0:
            print(f"BUILD_FAIL rc={rc}: {' '.join(step)}")
            return False
    return True

def main():
    # 先 build, 失敗直接結束
    if not build():
        return

    # 確保編譯完成的檔案存在且可執行
    if not (os.path.isfile(PROG) and os.access(PROG, os.X_OK)):
        print(f"NO_EXECUTABLE: {PROG}")
        return
    os.makedirs(RESULT_DIR, exist_ok=True)

    # 將 inputXXX.cbi 排序並且呼叫 cbi 執行
    inputs = sorted(glob.glob(os.path.join(INPUT_DIR, "input[0-9][0-9][0-9].cbi")))
    if not inputs:
        print("no testcases")
        return

    for inp in inputs:
        # tid 代表 testcase id，例如 input001.cbi, tid 就是 001
        tid = os.path.basename(inp)[len("input"):-len(".cbi")]
        outf = os.path.join(RESULT_DIR, f"output{tid}.cbi")
        print(f"=== {tid} ===")

        cmd = ["timeout", "-s", "KILL", str(TIMEOUT),
               PROG, inp, outf]
        rc = subprocess.run(cmd, stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL).returncode

        # 僅記錄結束狀態, 不影響計分; output 是否合法一律由 verifier 判定
        # timeout -s KILL 超時時回傳 137
        if rc == 137:
            print(f"  TIMEOUT (killed at {TIMEOUT}s)")
        elif rc != 0:
            print(f"  EXIT rc={rc}")
        else:
            print(f"  OK -> {outf}")

    os.sync()

if __name__ == "__main__":
    main()
