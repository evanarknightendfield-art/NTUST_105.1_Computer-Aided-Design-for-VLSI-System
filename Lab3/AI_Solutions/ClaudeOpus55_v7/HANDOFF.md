# 交接：v7 之後的下一版（給 Codex，一份就夠）

整個流程由 `Testcases/results/ClaudeOpus55_v7/codex_kit/orchestrate_codex.py` 自動跑；你（Codex）只在需要判斷的地方被叫起來。
每個 session 的提示詞會告訴你讀這份的哪幾節；其他節不要讀。`<NEW>` = 新版本的資料夾名（協調者給的，例如 CodexAstra）。

## 0. 規則

**品質與流程**
- 不 commit、任何檔案都不加 AI 署名。不要問問題（沒有人會回答），自己依本檔判斷，判斷與理由寫進 `AI_Solutions/<NEW>/WORK_STATE.md`。
- 只改 `AI_Solutions/<NEW>/`（src、tests、README、WORK_STATE）與 `Testcases/results/<NEW>/`。
  保護資料夾（不要改、不要寫入）：AI_Solutions 下的 ClaudeOpus55、CodexWindowDP、ClaudeOpus55_v4、CodexChainDP、ClaudeOpus55_v5、
  CodexCapDP、ClaudeOpus55_v6、CodexParallelChain、ClaudeOpus55_v7。協調者每個 session 後會核對 sha256，有變動就停。
- 執行輸出全部在 `Testcases/results/<NEW>/`；原始碼資料夾裡不放結果。
- 調參只用 `Testcases/testcases`、`Testcases/holdout`，以及大設計專用的 `Testcases/large/M1~M3`（第 A 階段新產生、沒有任何版本調過）；
  `Testcases/edge`、`edge_seed2~7`、L1、L2 只當回歸與驗收，不能拿來決定參數。新 seed `edge_seed8` 只在凍結後由最終驗證產生。
- 沒有數據就不宣稱進步。新舊版本在同一次、同一台機器上配對重跑（腳本都已經這樣做）；HPWL 至少 3 seeds，報平均與範圍；
  「同 SA 次數」「同總時間」「預設參數」三種分開報，預設參數一定要報。
- 每項改動最多 3 輪「改程式 → 訓練」；沒過就撤回（src 還原到上一個保留點），數據寫進 WORK_STATE 的「試過沒用」。
- 任何最佳化步驟出錯都要退回前一步的結果，程式一定要有輸出（main.cpp 的 `guarded` 已經做到，新步驟也要包在裡面）。
- 改到平行程式：TSan 0 警告，且同 seed、同執行緒數、固定次數時輸出逐位相同。

**省 token（這是付費的；上一輪 Codex 一個 session 用了 2,230 萬 input token，一半在查進度）**
1. 不要輪詢、不要等。超過 5 分鐘的計算一律寫成 `RUN:` / `WAIT:`（見提示詞最後的規則），然後結束 session；協調者在 shell 裡等，不花 token。
2. 每個指令的輸出 ≤ 40 行（`| tail -n 20`、`awk` 摘要）；不要 cat 整份 record.csv、log、.pl、stderr_ts.tsv。
3. 只讀需要的：本檔指定的節、WORK_STATE 最後一節、要改的函式（`grep -n` 找到再 `sed -n a,bp`）。不要讀 COMPARISON.md 全文或各版 README。
4. 不重跑已完成的步驟（每步有 `.done.json`）。對話回覆 ≤ 15 行；完整內容寫在檔案。

## 1. 誰做什麼

| 階段 | 誰 | 內容 |
|---|---|---|
| 準備 | 協調者 | 建 `AI_Solutions/<NEW>/`（複製 v7 的 src、tests、CMakeLists.txt、Makefile）、複製工具到 `Testcases/results/<NEW>/scripts/`、記保護資料夾 sha256 |
| A 量測 | 協調者 | `scripts/phaseA_queue.py`（約 1.7 小時）→ `phaseA/summary.md` |
| A2 診斷 | Codex（1 個 session） | 讀 `phaseA/summary.md` 與第 3–6 節，寫 `Testcases/results/<NEW>/diagnosis.md`（排序與每項驗收方式） |
| B 實作 | Codex（每個 session 一輪：改程式 → RUN 訓練 → 結束；下一個 session 讀結果決定保留或撤回） | 第 6、7 節 |
| 沙箱外 ASan | 協調者 | 完整 LeakSanitizer（沙箱內會因 ptrace 失敗） |
| C 凍結與最終驗證 | 協調者 | 寫 `frozen_sources.sha256`、跑 `scripts/final_validate.py`（約 6 小時）與 `final_report.py` |
| D 報告 | Codex（1 個 session） | README、COMPARISON 第 14 節、HISTORY 各加一列、edge 表 |

## 2. 工具（`Testcases/results/<NEW>/scripts/`，從 Lab3 目錄執行，都會拿 timing.lock、寫 `.done.json`）

| 腳本 | 用途 |
|---|---|
| `phase2_train.py TAG [dp 參數]` | 目前原始碼複製到 `phase2/train/TAG/` 編譯，訓練集 11 筆 Stage 1（`--no-sa`）與 v7 配對 → `pair.md`、`gates.json`、`compare.md`（對 v7 final 的 ChainDP） |
| `phase2_large.py TAG [all P2 P4] [--mid] [--h08] [--seeds N]` | 完整 dp：L1、L2（+ M1–M3、h08）與 v7 配對；P2 / P4 = taskset 2 / 4 顆 P-core → `summary.md` |
| `phase2_sa_speed.py TAG [REPS]` | SA 改動用：同起點固定次數是否逐位相同（1、4 執行緒），以及真正 dp 的 SA 次數與 HPWL（ibm01 ibm05 h06 h07 h08 × REPS） |
| `rep_queue.py TAG BENCH N [dp 參數]` | 同一 build 與 v7 各重跑 N 次（`--no-sa`），量執行間變動（例如 h06） |
| `tests_queue.py TAG` | C++ 測試一般 / ASan+UBSan / TSan（在 lock 下跑；沙箱內 ASan 的 leak 檢查可能失敗，協調者會在沙箱外補） |
| `hpwl_compare.py fixed/equal/default OUT --v NAME SRC TS ...` | 三種 HPWL 協定（最終驗證會用；B 只有改 SA 時才需要） |
| `final_validate.py` / `final_report.py` | 最終驗證與摘要（協調者跑） |

`run_ts.py`（stderr 時間戳、各階段時間）在 `Testcases/results/ClaudeOpus55_v5/scripts/`，上面的腳本已經會用它。
已知的坑：`pkill -f` 會殺到自己的 shell（用 PID）；`rm` 的目標用 `"${VAR:?}"`；Bookshelf 前綴要跟 .aux 裡的檔名一樣；`run_edge.sh` 只對新版本用；
local_grade 只留 stderr 最後 3000 字；計時中的程式沒綁核、會用到 E-core，計時期間不要在 CPU 16–19 跑重的東西（編譯可以）；
sa_bench 是把各版原始碼另外編譯的，**SA 程式完全相同時，兩個 sa_bench 的次數仍可差 3–13%**（程式碼排列），SA 速度一律用真正的 dp 量（`phase2_sa_speed.py`）。

## 3. v7 現況（10-24 同輪配對；`Testcases/results/ClaudeOpus55_v7/final/summary.md`）

| 指標 | v7 | CodexParallelChain |
|---|---|---|
| 27 筆雙條件（最大 ≤ ChainDP、總 ≤ ×1.005）/ 最大 GM / 總 GM | 20/27 / 0.98190 / 0.99986 | 21/27 / 0.98409 / 0.99977 |
| 新 seed（edge_seed7）雙條件 | 12/16 | 11/16 |
| L1（3 seeds）總 / 最大 / HPWL / 秒 | 2.42736e9 / 50,406 / 1.72744e9 / 187.4 | 2.44435e9 / 50,417 / 1.72598e9 / 187.4 |
| L2（3 seeds）總 / 最大 / HPWL / 秒 | 1.95144e9 / 12,206 / 1.53083e9 / 196.6 | 2.04836e9 / 13,168 / 1.54563e9 / 212.2 |
| HPWL ÷ v4 同次數（holdout / edge） | 0.99349 / 0.99311 | 相同 |
| HPWL ÷ v4 同總時間 | 0.98277 / 0.98572 | 0.98298 / 0.98688 |
| HPWL ÷ v4 預設參數 | 0.99184 / 0.99362 | 0.99154 / 0.99263 |
| 額外步驟超時（27、新 seed、L1/L2） | 0 | L2 2 / 3 次 |

v7 對 ChainDP 未達標：最大 h04 +2.63%、h07 +0.25%、e03 +6.14%、e12 +2.08%；總位移 e08 +1.05%、e15 +0.54%（超上限 0.04%）、e16 +0.64%；
新 seed 最大 e12 +2.19%，總位移 e03 +0.81%、e05 +0.55%、e07 +0.57%。少核心（真 2 核）：L1 總 −7.7% 但 HPWL +1.26%（4 核 +0.43%）。

## 4. 已確認的事實（不必重新量）

- **Stage 1 的牆鐘結構**：legalize 到 90 秒、pareto 與主視窗 DP 到 100 秒、額外步驟（chain + cap 搜尋）6 秒（≤ 2000 cell 12 秒）、SA 60 秒、polish 30 秒。
  v7：pipeline 用完自己的份額（被期限卡住）時跳過 finish() 的回合，直接 pareto（平行、第一輪一定跑）+ 視窗 DP；只有 L1、L2 會觸發（訓練集都 ≤ 3 萬 cell）。
- **大設計 L1 的位移與 HPWL 取捨**：v7 的視窗 DP 在 L1 拿到 54 秒（CPC 30 + 6.5 秒），總位移 −0.8%（2 核 −7.7%），legal HPWL +1.5%（2 核 +6.2%）；SA 之後全核 +0.09%、2 核 +1.26%。
  SA 次數兩版相同（2 核都是 2 個 worker、4,900 萬次）→ 差異完全來自 Stage 1 的起點。
- **執行緒**：v7 自動 8 個 worker；SA 在 (cell+net) > 25 萬時不超過 `availableCpus()`（sched_getaffinity）。2 顆 P-core 上 h08 的 SA：2 / 4 / 8 執行緒 = 73M / 94M / 94M 次、HPWL 1.0279 / 1.0224 / 1.0117e8。
- **SA 次數與 HPWL**：預設 60 秒時 8 band 對 4 band：h07 / h08 −1.0%、ibm01 −0.6%（多約 50% 次數）。SA 越多越好：h06–h08 的 SA 20 秒 + polish 2 秒優於 SA 5 / 10 秒；SA 60 + polish 30 優於 SA 30 + polish 60（1.0–3.2%）。
  polish 在中型設計 2–3 秒就收斂。ibm01 給 2.7 倍時間還能再降 3.4%（SA 遠未收斂）。
- **雜訊**：預設 HPWL 協定同一筆不同 seed 差 ±1%；27 筆 Stage 1 總位移的執行間變動約 0.11%、最大位移 h06 雙峰（427.55 / 435.19，cap 搜尋的 DP 跑 4.0 秒接受、3.35 秒被拒）。
- **chain 階段**：target windows（最多 2 輪）與之後的 DP 在自己的份額內就收斂，多給時間用不掉；ejection 的收穫集中在 h05、e08、e16。
- **主視窗 DP**：v7 起 finish() 的 memo 交給主 DP（結果不變，27 筆少約 7–20 秒）。視窗 DP 的尾巴依收穫提早停只能省 17 秒（27 筆）。

## 5. 已經證明不值得做的（不要再試）

| 方向 | 證據 |
|---|---|
| 放寬內部總位移比例（1.0048 → 1.01）或改接受規則 | e03 會達標，但 h03 / e08 / e16 總位移失敗（v6 diagnosis §2.2） |
| 熱點附近精確子問題（MIP / 分支定界） | h04、e12 在 ±1 列 ±8–16 site 內證明不可行（v6 §2.5） |
| rows / site 協商雙修復、視窗 DP 16 執行緒 | 11 筆結果完全不變（v6） |
| chain 依第 1 輪收穫延長、平行 target windows、縮小 ejection 候選、只收緊 rowRepair 期限 | v6 / CodexParallelChain 撤回 |
| chain 階段時間重新分配（ejection 少給、windows / DP 多給） | windows / DP 在份額內收斂：ejection 0 / 0.1 / 0.25 → +0.017 / +0.0075 / +0.0003%（v7 diagnosis §4） |
| 平行 SA 的 band 邊界改良 | 預設 60 秒時 8 band 不比 4 band 差（v7 §6.1、bands60） |
| SA / polish 重新分配（中型設計） | 現行最好（v7 §6.2） |
| 視窗 DP 依收穫提早停 | 只省 17 秒，且會改 cap 搜尋起點（v7 §3.1） |
| SA 在受記憶體上限的大設計上開比 CPU 多的 worker | 2 核上 L1 HPWL +1.38%（v7 B 第一版） |
| 增加額外預算（6 / 12 秒）或總時間 | 規則不允許：所有版本同一預算，最終驗證會核對 |

## 6. 目標（依「可改善的幅度 × 做得到的機會」；A2 依第 A 階段的數據可以調整順序，但要寫理由）

**T1　SA 的速度（直接影響預設參數的 HPWL，所有設計都受益）。** SA 在預設 60 秒遠未收斂，次數多 50% ≈ HPWL −1%。
先看 `phaseA/summary.md` 的 gprof 熱點與單執行緒次數/秒，只做「不改任何移動決定」的加速（資料排列、增量計算、避免重複的 box 重算、
平行 merge 的開銷等），讓固定次數下輸出逐位相同。量法：`phase2_sa_speed.py TAG 2`。

**T2　被時間卡住的大設計，Stage 1 的 legal HPWL 變高（L1：全核 SA 後 +0.09%、真 2 核 +1.26%）。**
先加量測用的 `--trace-hpwl`（Stage 1 每一步之後印 HPWL，預設不印），在 L1 與 M1–M3 找出是哪一步把 HPWL 拉高；可能的做法：
跳過 finish 的路徑先做一輪 total-refine 再 pareto、視窗 DP 在同樣位移下偏好保留原本的 cell 順序、或在 DP 收穫掉到初始收穫的某個比例以下時停。
量法：`phase2_large.py TAG all P2 --mid --seeds 3`（約 3 小時；先用 `--seeds 1` 篩）。

**T3　中大型設計（6–20 萬 cell）的穩健度。** 訓練集沒有這個規模；`phaseA/summary.md` 有 v7 在 M1–M3（全核、2 核）的時間、額外步驟、
是否跳過 final rounds、pareto 輪數、SA 執行緒。有超時、額外步驟超出、或「60,000 cell 以上只跑一條 pipeline」這種寫死的門檻造成明顯損失，就修。

**T4（T1–T3 都判定完才做）　cap 搜尋的牆鐘相依（h06 雙峰）。** 例如：快被接受的候選（總位移超上限 < 0.5%）優先用完剩下的額外預算，而不是開新的嘗試。
量法：`rep_queue.py TAG h06 5` 加訓練集配對。

## 7. 階段 B：每項的保留條件、停損、完成條件

**共同條件（每項都要成立，對同輪 v7）**：訓練集 11 筆（`phase2_train.py`）雙條件 ≥ v7 同輪的筆數；總位移 GM ≤ v7 × 1.001；最大 GM ≤ v7 × 1.001；
逐筆最大不比 v7 差超過 0.2%（h06 例外：差了就跑 `rep_queue.py TAG h06 5`，看次數分布）；額外步驟 0 超出；全部合法。

| 項目 | 另外的保留條件 |
|---|---|
| T1 SA 速度 | 同起點固定次數 1 與 4 執行緒的輸出逐位相同；真正 dp 的 SA 次數 5 筆平均 ≥ v7 × 1.08；HPWL 比 5 筆平均 ≤ 1.000（2 輪）；TSan 0 警告 |
| T2 大設計 HPWL | L1 與 M1–M3（全核與 P2）：HPWL 3 seeds 平均 ≤ v7；總位移 ≤ v7 × 1.002、最大 ≤ v7 × 1.002；時間 ≤ v7 + 2 秒 |
| T3 中大型穩健 | M1–M3、L1、L2 在全核與 P2：全部合法、L1 / M 類 < 240 秒、L2 < 600 秒、額外步驟 ≤ 預算；其他指標不比 v7 差（同 T2） |
| T4 cap 搜尋 | h06 重跑 5 次的最大位移平均比 v7 低；訓練集共同條件 |
| 其他小改動 | 有訓練集（或 M1–M3）數據顯示改善，且共同條件成立 |

**停損**：同一項最多 3 輪；第 3 輪仍不符就撤回並寫「試過沒用」。

**B 結束前的最後檢查**（對最後一份原始碼；每個 protocol.json 的 src_sha256 要等於目前 src）：
1. `phase2_train.py final_b`：共同條件成立。
2. `phase2_large.py final_b all P2 --mid`：全部合法、時間與額外步驟都在預算內、不比 v7 差（T2 / T3 的條件）。
3. 有動 SA：`phase2_sa_speed.py final_b 2` 符合 T1 條件。
4. `tests_queue.py final_b`：一般 / ASan+UBSan / TSan 全過；每個新功能至少一個測試。
5. WORK_STATE 寫：保留了哪些、撤回了哪些（各附數據路徑）、最後原始碼的 sha256、「B 完成」；回覆最後一行只寫 `B_DONE`。
所有改動都撤回也算 B 完成：寫清楚數據，最後一行寫 `NO_FINAL`（不做最終驗證，D 只寫報告說明沒有改善）。

## 8. 最終驗收（協調者跑 `final_validate.py`，D 依 `final/summary.md` 報告；對同輪 v7、參考同輪 ChainDP）

- 27 筆 + L1 + L2 + M1–M3 + edge_seed8 全部 PASS；L1、M1–M3 < 240 秒、L2 < 600 秒；額外步驟 0 超出。
- 27 筆：總位移 GM ≤ v7 × 1.001、最大 GM ≤ v7 × 1.001；雙條件筆數與 v7 比較（逐筆列出差異）。
- HPWL：同總時間與預設參數，holdout、edge 的 新 ÷ v7 平均 ≤ 1.000；不到的話，和同一表的雜訊欄（v7 第二次 ÷ v7）比，說明是否在雜訊內。同 SA 次數一定要報。
- L1、L2（3 seeds）、M1–M3：總位移、最大、HPWL 不比 v7 差。
- 報告逐筆列出沒達標的測資與差距；沒做到的不能寫成做到。
