#!/usr/bin/env python3
# driver.py 在 host 的 grader 目錄下執行
# 對 submissions/ 下每個 <SID>.zip 開一台一次性 VM 編譯並執行,
# 取回 outputXXX.cbi 放到 results/<SID>/, 最後呼叫 verifier.py 評分
#
# 目錄(相對於當前目錄):
#   submissions/<SID>.zip   學生繳交的壓縮檔
#   inputs/inputXXX.cbi     測資
#   results/<SID>/          取回的 output (由本腳本產生)
#   logs/<SID>.log          VM console 輸出 (由本腳本產生)
#
# usage: uv run driver.py

import os, re, sys, glob, shutil, subprocess, tempfile

# ---- config ----
HERE      = os.path.dirname(os.path.abspath(__file__))   # driver.py 所在目錄
BASE_IMG  = os.path.join(HERE, "vm", "base.qcow2")       # base image 路徑
EXECUTOR  = os.path.join(HERE, "executor.py")            # 放入 VM 執行的 executor
VERIFIER  = os.path.join(HERE, "verifier.py")            # 評分腳本
SUBMIT_DIR = "submissions"                               # 繳交檔路徑
INPUT_DIR  = "inputs"                                    # 測資路徑
RESULT_DIR = "results"                                   # 輸出路徑
LOG_DIR    = "logs"                                      # VM console log 路徑

SID_RE    = re.compile(r"[MB]\d{8}")     # 學號格式, 例如 M11515013, B11215001
BUILD_TIMEOUT = 60                       # 需與 executor.py 一致
TIMEOUT       = 60                       # 單筆測資上限, 需與 executor.py 一致
VM_MARGIN     = 120                      # 開關機與解壓縮的額外時間
MAX_OUTPUT    = 10 * 1024 * 1024         # 單一 output 檔大小上限, 超過不取回

# qemu 參數: KVM 加速, 8G RAM, 2 核心, 無網卡, 無圖形介面
QEMU = ["qemu-system-x86_64", "-enable-kvm", "-cpu", "host", "-m", "8G", "-smp", "2",
        "-nic", "none", "-nographic", "-monitor", "none"]

# virt-* 需拿掉 XDG_RUNTIME_DIR, 否則 passt 會被 AppArmor 擋下
VIRT_ENV = {k: v for k, v in os.environ.items() if k != "XDG_RUNTIME_DIR"}

# 執行 virt-* 指令, stdin 關閉避免卡住等輸入
def virt(cmd, **kw):
    return subprocess.run(cmd, env=VIRT_ENV, stdin=subprocess.DEVNULL, **kw)

# 以唯讀方式開啟 overlay, 從 stdin 餵 guestfish 指令
def guestfish(overlay, script):
    return subprocess.run(["guestfish", "--ro", "-a", overlay, "-i"], env=VIRT_ENV,
                          input=script, capture_output=True, text=True)

# 從 VM 取回 /home/grader/<SID>/results/outputXXX.cbi
# 只取預期的檔名, 且先檢查大小, 避免學生程式寫出巨大檔案塞爆 host
def fetch_outputs(overlay, sid, tids, dest):
    remote = [f"/home/grader/{sid}/results/output{tid}.cbi" for tid in tids]
    # 第一次: 查各檔大小 (不存在的檔案會失敗, 前綴 '-' 讓 guestfish 忽略錯誤)
    script = "".join(f"-echo {p}\n-filesize {p}\n" for p in remote)
    r = guestfish(overlay, script)
    # 輸出為「路徑, 大小」交錯, 檔案不存在則只有路徑
    sizes = {}
    lines = r.stdout.splitlines()
    for a, b in zip(lines, lines[1:]):
        if a in remote and b.isdigit():
            sizes[a] = int(b)
    # 超過大小上限的不取回
    wanted = [p for p in remote if p in sizes and sizes[p] <= MAX_OUTPUT]
    for p in remote:
        if p in sizes and sizes[p] > MAX_OUTPUT:
            print(f"  skip {os.path.basename(p)}: {sizes[p]} bytes > {MAX_OUTPUT}")
    if not wanted:
        return 0
    # 第二次: 下載
    script = "".join(f"download {p} {os.path.join(dest, os.path.basename(p))}\n" for p in wanted)
    guestfish(overlay, script)
    return len(wanted)

# 評測一位學生的專案流程: 建 overlay -> 放入檔案 -> 開機執行 -> 取回 output
# work 為暫存目錄, 結束後由呼叫端刪除 (含 overlay)
def grade_one(zip_path, sid, inputs, tids, work):
    overlay = os.path.join(work, "overlay.qcow2")
    stage   = os.path.join(work, "stage")
    os.makedirs(stage)

    # zip 統一以大寫學號命名後放入 VM, VM 內以檔名判斷 SID
    shutil.copy(zip_path, os.path.join(stage, f"{sid}.zip"))
    shutil.copytree(INPUT_DIR, os.path.join(stage, "inputs"))

    # 以 base image 建一次性 overlay, 寫入只落在 overlay, base image 不變
    subprocess.run(["qemu-img", "create", "-q", "-f", "qcow2",
                    "-b", BASE_IMG, "-F", "qcow2", overlay], check=True)
    # 開機前離線將 zip, inputs/, executor.py 放進 VM 的 /opt/grader/
    virt(["virt-copy-in", "-a", overlay,
          os.path.join(stage, f"{sid}.zip"), os.path.join(stage, "inputs"), EXECUTOR,
          "/opt/grader/"], check=True)

    # 開機, VM 內 grade.service 跑完會自動關機; 超過上限直接砍掉 qemu
    limit = BUILD_TIMEOUT + TIMEOUT * len(inputs) + VM_MARGIN
    log = os.path.abspath(os.path.join(LOG_DIR, f"{sid}.log"))
    try:
        subprocess.run(QEMU + ["-drive", f"file={overlay},if=virtio",
                               "-serial", f"file:{log}"],
                       stdin=subprocess.DEVNULL, timeout=limit)
        status = "DONE"
    except subprocess.TimeoutExpired:
        status = f"VM_TIMEOUT({limit}s)"

    # 清掉舊結果後取回 output, 即使 VM 逾時也取回已寫好的部分
    dest = os.path.join(RESULT_DIR, sid)
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(dest)
    n = fetch_outputs(overlay, sid, tids, dest)
    print(f"  {status}, {n}/{len(tids)} outputs")

def main():
    # 逐行輸出, 避免導向檔案或 pipe 時進度被緩衝
    sys.stdout.reconfigure(line_buffering=True)  # pyright: ignore[reportAttributeAccessIssue]

    # 確認必要檔案存在
    for p in (BASE_IMG, EXECUTOR, VERIFIER):
        if not os.path.isfile(p):
            print(f"error: missing {p}"); sys.exit(1)

    # 測資列表 (所有學生共用), tid 例如 input001.cbi -> 001
    inputs = sorted(glob.glob(os.path.join(INPUT_DIR, "input[0-9][0-9][0-9].cbi")))
    if not inputs:
        print(f"error: no testcases in {INPUT_DIR}"); sys.exit(1)
    tids = [os.path.basename(p)[len("input"):-len(".cbi")] for p in inputs]

    # submissions/ 下每個 zip 為一位學生
    zips = sorted(glob.glob(os.path.join(SUBMIT_DIR, "*.zip")))
    if not zips:
        print(f"error: no zip in {SUBMIT_DIR}"); sys.exit(1)

    os.makedirs(RESULT_DIR, exist_ok=True)
    os.makedirs(LOG_DIR, exist_ok=True)

    seen = set()
    for zp in zips:
        # 檔名轉大寫即學號, 格式不符或重複則跳過
        name = os.path.basename(zp)[:-len(".zip")]
        sid = name.upper()
        if not SID_RE.fullmatch(sid):
            print(f"SKIP {os.path.basename(zp)}: invalid student id")
            continue
        if sid in seen:
            print(f"SKIP {os.path.basename(zp)}: duplicate {sid}")
            continue
        seen.add(sid)
        print(f"running {sid}")
        # 每位學生一個暫存目錄, 離開 with 自動刪除 overlay
        with tempfile.TemporaryDirectory(prefix=f"grade-{sid}-") as work:
            try:
                grade_one(zp, sid, inputs, tids, work)
            except subprocess.CalledProcessError as e:
                print(f"  ERROR: {e}")

    # 全部跑完後評分
    subprocess.run([sys.executable, VERIFIER])

if __name__ == "__main__":
    main()
