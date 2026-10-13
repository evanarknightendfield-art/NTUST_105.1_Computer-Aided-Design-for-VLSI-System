# Clock Buffer Insertion Grader

### 整體評分流程
全部由 `driver.py` 自動完成，在含有 `submissions/<SID>.zip` 與 `inputs/` 的目錄執行 `uv run driver.py`。

對每個 `<SID>.zip`（檔名轉大寫後須符合 `[MB]\d{8}`，否則跳過）:
1. host: 以 `vm/base.qcow2` 建一次性 overlay
2. host: `virt-copy-in` 將 `<SID>.zip`, `inputs/`, `executor.py` 離線複製到 overlay 內的 `/opt/grader/`
3. host: qemu 開機（`-nic none` 斷網）
4. VM: `grade.service` 開機自動執行 `grade.sh`
   - 以 `grader` 身分解壓縮 `<SID>.zip` 到 `/home/grader/<SID>/`
   - 刪掉學生自帶的 `build/`, `results/`, `inputs/`, 放入 `/opt/grader/inputs/`
   - `python3 -u executor.py <SID>` 自動編譯並且執行
   - 結束後不論成敗自動 `poweroff` 關機
5. host: qemu 結束後（或超過 `60 + 60 × 測資數 + 120` 秒被砍），以 guestfish 唯讀取出 `outputXXX.cbi` 到 `results/<SID>/`, 只取預期檔名且單檔 ≤ 10MB, VM console 輸出儲存於 `logs/<SID>.log`
6. host: 刪除 overlay

全部跑完後, driver 自動執行 `verifier.py`, 產生 `record.csv`, `summary.csv`, 並且流程上一次只跑一位學生的專案，避免爭搶 CPU 影響 60 秒計時

### base image
- `vm/base.qcow2`: Ubuntu 24.04, 已裝 `gcc`, `g++`, `cmake`, `python3`, `grader` user
- `vm/guest/grade.sh`, `vm/guest/grade.service` 為 VM 內腳本原始檔，修改後需重新上傳:
  ```
  env -u XDG_RUNTIME_DIR virt-customize -a vm/base.qcow2 --no-network --upload vm/guest/grade.sh:/usr/local/bin/grade.sh
  ```
  手動執行 `virt-*` 都要加 `env -u XDG_RUNTIME_DIR`, 否則 passt 會被 AppArmor 擋下

### executor.py
1. `cmake -S /home/grader/<SID> -B /home/grader/<SID>/build`, `cmake --build`, 編譯與建置, 兩步合計上限 60 秒
2. 執行檔名稱必須是 `build/cbi`
3. 對 `/home/grader/<SID>/inputs/input[0-9][0-9][0-9].cbi` 依序執行：

   ```
   ./cbi <input path> <output path>
   ```

   - output 寫到 `/home/grader/<SID>/results/outputXXX.cbi`（`XXX` 與 input 編號相同，三位數）
   - 每筆測資的 `cbi` 執行時間上限 60 秒, 各筆獨立計時，超時直接 kill
   - stdin 關閉，stdout/stderr 丟棄
   - 超時與 return code 只記錄在 log（`TIMEOUT` / `EXIT rc=N` / `OK`），不影響計分；只要 output 檔存在且合法，verifier 就照常算分

### verifier.py
由 driver 自動執行，也可在含有下列結構的目錄單獨執行 `uv run verifier.py`:
```
.
├── inputs/
│   ├── input001.cbi
│   └── input002.cbi ...
└── results/
    ├── M11512345/
    │   ├── output001.cbi
    │   └── output002.cbi ...
    └── <其他學號>/
```

執行以後會輸出:
- `record.csv`: `student_id,testcase,status,score,t_max,t_min`, status 為 `VALID` / `INVALID` / `MISSING` / `PARSE_ERROR`
- `summary.csv`: `student_id,001,002,...,final_score`
  - 每筆測資一欄，各自獨立計分：`VALID` 填 score，否則填 status
  - `final_score` 計算方式待定，目前留空

### verifier 合法性檢查（任一違反就是 INVALID）
- SRC, sink, buffer 座標不可重疊
- parent 只能是 `SRC` 或 buffer, child 只能是 buffer 或 sink
- 每個 buffer 與 sink 剛好當一次 child（不可漏、不可重複，必須是 tree）
- SRC 不可當 child
- 每個 buffer 必須至少有一個 child（不可當 leaf）
- 所有 node 必須從 SRC 可達
- 宣告的層數 `k` 必須等於 BFS 實際 depth
- fanout: children 數 ≤ `F`（SRC 用 `.limit fanout`，buffer 用 buflib 的 `F`）
- length: parent 到所有 children 的 Manhattan 距離**總和** ≤ `L`（SRC 用 `.limit length`）

### 計分
- 每個 sink 的 arrival time = SRC 到該 sink 路徑上各段 Manhattan 距離總和（buffer 本身無 delay）
- `skew = T_max - T_min`
- `score = w_skew * skew + Σ buffer cost`, 越低越好
