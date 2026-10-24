# 交接：v6 之後的下一版（給 Codex，一份就夠，不必再讀其他報告）

這份取代「先讀 README、COMPARISON、各版 README、診斷」的長清單。需要細節時只用 `grep -n` / `sed -n a,bp` 讀片段。

## 0. 規則（每個 session 都適用）

**品質與流程**
- 不 commit、任何檔案都不加 AI 署名。保護資料夾（不要改）：ClaudeOpus55、CodexWindowDP、ClaudeOpus55_v4、CodexChainDP、ClaudeOpus55_v5、CodexCapDP、ClaudeOpus55_v6。
- 新版本：`AI_Solutions/<NEW>/`，以 ClaudeOpus55_v6 為底複製 `src/ CMakeLists.txt Makefile tests/`；執行輸出全部在 `Testcases/results/<NEW>/`。
- 調參只用 `Testcases/testcases`、`Testcases/holdout`；edge、edge_seed2~5 只當回歸。凍結（記 sha256）之後才產生新 seed。
- 沒有數據就不宣稱進步：HPWL 至少 3 seeds、報平均與範圍，三種協定分開；新舊版本同一次配對重跑。
- 計時中的程式一次一個（`timing.lock`）；分析與編譯 `taskset -c 16-19`。

**省 token（上一輪 Codex 一個 session 用了 2,230 萬 input token：166 次呼叫、每次約 14 萬 context，其中一半的工具呼叫是在查進度）**
1. **一個 session 只做一個階段**，做完就更新 WORK_STATE 並結束；下一階段開新 session（context 從小開始）。
2. **不要輪詢**。超過 5 分鐘的計算：寫成佇列腳本、`nohup … &` 啟動、在 WORK_STATE 寫「等待中：哪個腳本、完成標記檔」，然後**結束 session**。
   使用者（或 `scripts/codex_after.sh`）會在完成標記出現後開下一個 session。5 分鐘內的等待：只用一個 blocking 指令
   `timeout 600 bash -c 'until [ -e MARKER ]; do sleep 20; done'`，不要反覆 ls / tail / ps。
3. **輸出都進檔案**，終端只印摘要：每個指令輸出 ≤ 40 行（`| tail -n 20`、`awk` 摘要）；不要 cat 整份 record.csv、log、.pl。
4. **只讀需要的**：本檔 + 自己的 WORK_STATE.md + 要改的函式。原始碼用 `grep -n` 找函式再 `sed -n` 讀範圍。
5. 不重跑已完成的步驟（每步 `.done.json`）。對話回覆 ≤ 15 行；完整內容寫在報告檔。

## 1. 可以直接用的工具（`Testcases/results/ClaudeOpus55_v6/scripts/`）

**複製整個資料夾到 `Testcases/results/<NEW>/scripts/` 就能用**：路徑由資料夾名推得（`v6q.py` 的 R 與 WORK_STATE = `AI_Solutions/<資料夾名>/WORK_STATE.md`）。

| 腳本 | 用途 |
|---|---|
| `v6q.py` | 串行佇列：`Queue(out).run(name, cmd)`，每步 `.done.json`、拿 timing.lock、完成自動寫 WORK_STATE；`build(ver, out_dir, src=)` |
| `phase2_train.py TAG [dp 參數]` | 把目前原始碼複製到 `phase2/train/TAG/` 編譯，跑 testcases+holdout 的 Stage 1（`--no-sa`），對 ChainDP / CapDP 出 `compare.md` |
| `phase2_large.py TAG` | 完整 dp：L2 seed 1、2 與 L1 |
| `phase2_stage2_sweep.py TAG L2分配 L1分配` | 同一 L1/L2 合法起點的 SA / polish 分配掃描（sa_bench） |
| `stage1_phases.py OUT DIR…` | 從 run_ts 輸出切 Stage 1 階段時間、每次 cap 嘗試 |
| `compare_lg.py OUT.md REF REF_CSV NAME CSV…` | 逐筆對參考（最大 ≤、總 ≤ ×1.005） |
| `cap_headroom.py` | 熱點 MIP（scipy；只動違規 cell 附近，上限 D 下最小總位移） |
| **`final_validate.py --seed-dir edge_seed6 --seed 20261024`** | **凍結後整套驗證**（27 + L1/L2 + 新 seed + 三種 HPWL 協定 + 額外時間），約 7 小時；完成標記 `final/ALL_DONE` |
| **`final_report.py`** | 從 final/ 產生 `summary.md / summary.json`（只印 5 行） |
| `codex_after.sh MARKER PROMPT_FILE LOG` | 等完成標記出現再自動開下一個 Codex session（可選） |

其他：`Testcases/results/ClaudeOpus55_v5/scripts/run_ts.py`（stderr 時間戳）、`Testcases/results/CodexCapDP/scripts/hpwl_compare.py`（三種 HPWL 協定）、
`AI_Solutions/CodexChainDP/tests/sa_bench.cpp`（共同 Stage 2 driver）、`AI_Solutions/ClaudeOpus55_v6/tests/repair_compare.cpp`。
已知的坑：local_grade 只留 stderr 最後 3000 字；它的 TESTCASE_DIR 是「裝 benchmark 資料夾的資料夾」（只跑 L1/L2 時做 symlink 資料夾）；
Bookshelf 前綴要跟 .aux 裡寫的檔名一樣（symlink 起點要叫 `L2.*` / `h07.*`）；`run_edge.sh` 會覆寫 `Testcases/results/edge/<ver>/`，只對新版本用；
`pkill -f` 會殺到自己的 shell，用 PID；`rm` 的目標用 `"${VAR:?}"`。

## 2. v6 現況（2026-10-22 同輪配對；細節 `Testcases/results/ClaudeOpus55_v6/final/summary.md`）

| 指標 | v6 | CodexCapDP |
|---|---|---|
| 27 筆雙條件（最大 ≤ ChainDP、總 ≤ ×1.005） | 19/27 | 20/27 |
| 27 筆最大 / 總 ÷ ChainDP（幾何平均） | 0.98214 / 0.99975 | 0.98289 / 0.99975 |
| 新 seed 20261023（16 筆）雙條件 | 10/16 | 10/16 |
| L2 HPWL ÷ v5（單次） | 0.9246 | 0.9718 |
| 額外步驟超時 | 0/27 | 1/27 |
| HPWL ÷ v4，同次數（holdout/edge） | 0.99349 / 0.99311 | 相同 |
| HPWL ÷ v4，同總時間 | 0.98653 / 0.98830 | 0.98617 / 0.98550 |
| HPWL ÷ v4，預設參數 | 0.99290 / 0.99331 | 0.99306 / 0.99354 |

v6 未達標：最大 h04 +2.63%、h06 +0.21%、h07 +0.55%、e03 +6.14%、e12 +2.08%；總位移 e08 +1.10%、e15 +0.57%、e16 +0.64%；
新 seed：最大 e03 +4.03%、e12 +1.29%，總 e05 +0.63%、e08 +0.60%、e10 +0.56%、e16 +0.55%。

## 3. 已確認的事實（不必重新量）

- **chain 階段是串行的**（`window.cpp: targetedWindowsDisp`、`chain.cpp`），預設 1.5 秒（≤ 2000 cell 3 秒），在額外預算 6 / 12 秒內。
  chain 時間 T=0.75 / 1.5 / 3 / 6 秒時 chain 後總位移變化：e08 −1.04 / −1.30 / −1.65 / −1.98%，e16 −0.77 / −0.85 / −0.97 / −0.99%，
  h05 −0.22 / −0.26 / −0.29 / −0.35%，h06 −0.20 / −0.36 / −0.44 / −0.45%，h02/h03/h04/h07/h08 在 1.5 秒後幾乎不變
  （`Testcases/results/ClaudeOpus55_v6/phase1/chain_sweep.md`）。e08 的收穫大多來自 round 1 的 target windows（82 個）與之後的 DP，chain 本身只有 5 條。
- **e08 / e16 的總位移超標** = chain 階段少拿約 0.55%（ChainDP 給 4 秒）+ cap 搜尋花 0.43 ~ 0.47%（最大位移已比 ChainDP 低 3.7 ~ 4.1%）。extra 給 20 秒時 e08 1.00505、e16 達標。
- **平行 SA 的 band 邊界損失**（同總次數約 2,400 萬、v6 sa_bench）：h07 8 執行緒 168.6k vs 1 執行緒 168.9k（無損失）；h08 105.70M vs 104.45M（**+1.2%**）
  （`phase2/band_loss/result.txt`）。ibm01、ibm05、h06 還沒用同次數量（h06 的平行版次數不同，不能比）。
- **toy 的最大位移退步**：v5 1,972.0（精確下限 1,938.4，+1.7%），CodexCapDP / v6 2,004.4（+3.4%）；h01 反而 v6 較好（29.39 vs 30.07）。原因沒查。
- **同總時間 edge HPWL v6 比 CodexCapDP 差 0.28%**：不是執行順序（v6 先跑仍 1.0097，7 筆）；e07、e15 是 Stage 1 起點不同；**e09 起點相同仍兩次都 +0.7~0.9%，原因沒找到**。

## 4. 已經證明不值得做的（不要再試）

| 方向 | 證據 |
|---|---|
| 放寬內部總位移比例（1.0048 → 1.01）或改接受規則 | e03 會達標，但 h03 / e08 / e16 總位移失敗；接受步驟的邊際斜率兩組重疊（v6 diagnosis §2.2） |
| 熱點附近精確子問題（MIP / 分支定界） | h04、e12 在 ±1 列 ±8~16 site 內證明不可行；e03 的違規 cell 在 ±16 site 內沒有合格位置（§2.5） |
| rows / site 協商雙修復比較 | 11 筆結果完全不變（`phase2/train/race1`） |
| 視窗 DP 16 執行緒 | 11 筆最大位移相同（`phase2/train/thr16`） |
| chain 依「第 1 輪收穫」延長時間 | 訓練集最大 0.34%（h06），e08/e16 1.26/0.82%：訓練集沒有會觸發的筆，門檻只能看 edge 定（違反調參規則） |
| L2 的 SA 依比例縮短 | 只縮到 13.5 秒；v6 改成門檻式（每 cell < 100 次只留 10%） |
| 最大位移追上 ChainDP 的 e03 / h04 / e12 | 需要 1.01 比例 + 約 20 秒（ChainDP 的 global 16 秒）；額外預算不能增加 |

## 5. 建議的下一版方向（依幅度 × 機會；先診斷再做）

1. **chain 階段平行化**（target windows 與 ejection chains 分 band 平行，總時間不變）：目標是在同樣 1.5 秒拿到接近 6 秒的收穫
   （e08 再 −0.7%、e16 −0.14%、h05 / h06 −0.09%），把總位移超標的筆拉回來，且不吃 SA 的時間。
   先量：chain 階段內 target windows / chains / DP 各佔多少時間、平行化後每秒工作量。
2. **平行 SA 的 band 邊界損失**：先在 ibm01、ibm05、h06、h07、h08 用同總次數量 8 執行緒 vs 1 執行緒（各 2 seeds）；
   平均損失 ≥ 0.5% 才做（例如 band 與直條 strip 交替、或 band 邊界隨機平移）。驗收用同次數協定（固定次數時結果仍要決定性）。
3. （小）toy 最大位移從 v5 的 1,972 退到 2,004.4 的原因；e09 同總時間的差異。只量，不一定要改。

## 6. 驗收（建議）

- 27 + L1 + L2 + 新 seed 全部 PASS；L1 < 240 秒、L2 < 600 秒；額外步驟 0 超時；任何步驟出錯退回前一步。
- 對 v6（同輪）：27 筆雙條件 ≥ 19、總位移幾何平均 ≤ v6、逐筆最大位移不比 v6 差超過 Stage 1 的執行間變動；
  HPWL 預設與同總時間協定 holdout、edge 平均不比 v6 差（報範圍）；同次數協定若 SA 有改要報。
- C++ 測試一般 + ASan/UBSan 通過；動到平行程式要 TSan，並確認同 seed / 同執行緒數 / 固定次數結果一樣。

## 7. 階段 B（實作・調參）什麼時候算完成

**每一項改動各自判定「保留」或「撤回」**，全部判定完、而且最後一份原始碼通過下面的檢查，B 就完成，在 WORK_STATE 寫「B 完成」並停（下一步是 C）。

**單項的判定**（只看 testcases + holdout；`phase2_train.py` 的 compare.md 會列出 v6 同輪的數字）

v6 在訓練集 11 筆的基準（對 ChainDP，`ClaudeOpus55_v6/final`）：雙條件 **8/11**、最大位移 GM **0.99604**、總位移 GM **0.99813**。
Stage 1 的執行間變動：總位移最多約 0.11%、最大位移約 0.13%（ibm05）。

| 改動 | 保留的條件（全部要成立） |
|---|---|
| chain 階段平行化 | 訓練集雙條件 ≥ 8/11；總位移 GM 比 v6 低（≥ 0.03%）；最大位移 GM 不比 v6 差超過 0.1%；逐筆最大不比 v6 差超過 0.2%；chain 階段的實際秒數不比 v6 多；h05、h06 在同 1.5 秒內的 chain 收穫至少達到 v6 給 3 秒時的水準（v6：h05 −0.29%、h06 −0.44%，HANDOFF 第 3 節） |
| 平行 SA（只有診斷的 band 損失平均 ≥ 0.5% 才做） | 同次數協定（固定次數、自動執行緒、3 seeds）在 ibm01、ibm05、h06、h07、h08 的幾何平均比 v6 好 ≥ 0.3%，沒有一筆差超過 0.5%；固定次數下兩次執行結果逐位相同；TSan 0 警告 |
| 其他小改動 | 有訓練集數據顯示改善，且上表的「不比 v6 差」都成立 |

**停損**：同一項最多 3 輪「改程式 → 訓練」。第 3 輪仍不符合保留條件就撤回（把 src 還原到上一個保留點），數據寫進 WORK_STATE 的「試過沒用」。

**B 結束前的最後檢查**（對最後一份原始碼，`phase2_train.py` 的 protocol.json 裡的 src_sha256 要等於目前 src）

1. 訓練集 Stage 1 一次（`phase2_train.py final_b`），以上「不比 v6 差」都成立，額外步驟 0 超時。
2. 有動 SA、polish 或時間分配時：`phase2_large.py final_b`（L1、L2 完整執行）全部合法，L1 < 240 秒、L2 < 600 秒。
3. C++ 測試：一般 + ASan/UBSan 全過；動過平行程式加 TSan 0 警告。新功能各有一個測試。
4. WORK_STATE 寫：保留了哪些、撤回了哪些（各附數據路徑）、最後原始碼的 hash、「B 完成，下一步 C」。

所有改動都被撤回也算 B 完成：寫清楚數據，C 改成「不凍結新版、直接寫報告說明沒有改善」。
