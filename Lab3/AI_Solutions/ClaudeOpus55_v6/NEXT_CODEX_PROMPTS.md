# 下一版（CodexParallelChain）的 Codex 提示詞與排程

**省 token 的做法**（上一輪 CodexCapDP 一個 session 用了 2,230 萬 input token：166 次模型呼叫、每次約 14 萬 context、一半的工具呼叫在查進度）：

1. **每個階段開一個新 session**：context 從約 2 萬開始，不會累積到 14~21 萬。
2. **長計算放背景後直接結束 session，不要守著**：下一個 session 由你或 `codex_after.sh` 在完成標記出現後開。
3. **只讀一份交接檔** [HANDOFF.md](HANDOFF.md)（規則、工具、現況、死路、方向都在裡面），不讀 COMPARISON / 各版 README。
4. **現成腳本**：佇列、訓練、最終驗證、報告都已寫好（`Testcases/results/ClaudeOpus55_v6/scripts/`），Codex 不必重寫。
5. reasoning effort：A、C、D 用 medium（目前設定）；B（設計與實作）可改 high。

## 排程

| 階段 | 開 session 的時機 | Codex 做什麼 | 背景計算 | 結束時 |
|---|---|---|---|---|
| A 診斷・啟動 | 現在 | 建資料夾、寫診斷佇列、啟動 | 約 1 ~ 1.5 小時 | 寫好完成標記路徑就結束 |
| A2 診斷・彙整 | `phase1/ALL_DONE` 出現後 | 讀結果、寫 diagnosis.md | 無 | 結束 |
| B 實作・調參 | 你看過 diagnosis.md 之後 | 改程式、測試、訓練集 Stage 1 | 每次約 5 ~ 10 分鐘（可能數次） | 每次啟動背景訓練就結束；用「接續」續做 |
| C 凍結・驗證 | WORK_STATE 出現「B 完成」（條件見 HANDOFF.md 第 7 節） | 凍結、啟動 final_validate.py | 約 7 小時（適合睡前） | 啟動後立刻結束 |
| D 彙整・文件 | `final/ALL_DONE` 出現後 | 報告、README、COMPARISON、HISTORY、edge 表 | 無 | 回報 |

可以用 `codex_after.sh` 讓 A2、D 自動開始，你不用守著（在 Lab3 執行，把對應的提示詞存成檔案）：

```bash
chmod +x Testcases/results/ClaudeOpus55_v6/scripts/codex_after.sh
nohup Testcases/results/ClaudeOpus55_v6/scripts/codex_after.sh Testcases/results/CodexParallelChain/phase1/ALL_DONE prompt_A2.md Testcases/results/CodexParallelChain/codex_A2.log &
```

---

## 提示詞 A（診斷・啟動）

```text
這一輪的新版本叫 CodexParallelChain，以 AI_Solutions/ClaudeOpus55_v6 為底。
先只讀 AI_Solutions/ClaudeOpus55_v6/HANDOFF.md（規則、省 token 方式、工具、v6 現況、已證明的死路、建議方向都在裡面），不要讀其他報告。

這個 session 只做「建立 + 啟動診斷」，啟動後就結束：
1. 建 AI_Solutions/CodexParallelChain/（複製 v6 的 src、CMakeLists.txt、Makefile、tests），寫 WORK_STATE.md；
   把 Testcases/results/ClaudeOpus55_v6/scripts/ 整個複製到 Testcases/results/CodexParallelChain/scripts/；
   記錄保護資料夾與底版的 sha256。
2. 寫一支佇列腳本 scripts/diag.py（用 v6q.Queue），完成時寫 phase1/ALL_DONE，內容：
   a. chain 階段時間拆解：在 Testcases/results/CodexParallelChain/ 做只加 fprintf 的插樁副本，
      量 testcases + holdout 11 筆（加 e08、e16 只作診斷）的 target windows / ejection chains / chain 後 DP 各花幾秒、各降多少總位移；
      再加 --chain-time 6 的同樣量測（對照 HANDOFF 第 3 節的收穫曲線）。
   b. 平行 SA 的 band 邊界損失：ibm01、ibm05、h06、h07、h08，seed 1、2；先跑自動執行緒的固定次數，
      再用它「實際做的次數」跑單執行緒（sa_bench，起點用 Testcases/results/ClaudeOpus55_v6/final/hpwl_fixed/legal_starts/ClaudeOpus55_v4/<bench>/output）。
   c. toy：v5 與 v6 各跑一次 -v --no-sa，留 cap 搜尋的 stderr（v5 最大 1972.0、v6 2004.4）。
3. nohup 啟動 diag.py，在 WORK_STATE 寫「等待中：scripts/diag.py，完成標記 phase1/ALL_DONE」，然後結束。不要等它跑完、不要查進度。
回覆不超過 10 行。
```

## 提示詞 A2（診斷・彙整）

```text
接續 AI_Solutions/CodexParallelChain/。只讀 AI_Solutions/ClaudeOpus55_v6/HANDOFF.md 第 0 節與 AI_Solutions/CodexParallelChain/WORK_STATE.md。
phase1/ALL_DONE 已出現。只用 awk / python 摘要讀 phase1 的結果（每個指令輸出 ≤ 40 行），寫 Testcases/results/CodexParallelChain/diagnosis.md：
- chain 階段：三部分各花多少時間、各拿多少總位移；哪些可以分 band 平行、共用的資料結構是什麼、預估同 1.5 秒能多做幾倍；
- band 邊界損失：5 筆 × 2 seeds 的比值與平均；是否 ≥ 0.5%（HANDOFF 第 5 節的門檻）；
- toy 退步的原因（只描述，不改）；
- 依「幅度 × 機會」排序的第二階段計畫，每項寫驗收方式。
更新 WORK_STATE 後結束。回覆不超過 15 行。
```

## 提示詞 B（實作・調參；可重複用「接續」）

```text
接續 AI_Solutions/CodexParallelChain/。只讀 HANDOFF.md 第 0、4、6 節、WORK_STATE.md 與 Testcases/results/CodexParallelChain/diagnosis.md 的排序表。
依 diagnosis.md 的排序改程式（只改 AI_Solutions/CodexParallelChain/src 與 tests）：
1. chain 階段平行化（target windows、ejection chains 分 band 平行；總時間與額外預算不變；結果合法、出錯退回前一步）。
2. 只有診斷顯示 band 邊界損失 ≥ 0.5% 才改平行 SA；改了要保持「同 seed、同執行緒數、固定次數結果一樣」。
每改完一項：
- C++ 測試（一般；有動平行程式再加 ASan/UBSan 與 TSan），補一個新功能的測試；
- 用 scripts/phase2_train.py TAG 跑訓練集（testcases + holdout）的 Stage 1，nohup 啟動後在 WORK_STATE 寫完成標記（phase2/train/TAG/stage1.done.json）並結束 session；
- 下一個 session 讀 phase2/train/TAG/compare.md 決定保留或撤回（數據寫進 WORK_STATE）。
只用 testcases + holdout 決定參數；e08 / e16 不能拿來調參。沒有效果就撤回，不要留著。
每一項的保留條件、停損（同一項最多 3 輪）與「B 完成」的最後檢查在 HANDOFF.md 第 7 節；全部做到就在 WORK_STATE 寫「B 完成，下一步 C」並停。
回覆不超過 15 行。
```

`B` 之後每次只要貼：

```text
接續 AI_Solutions/CodexParallelChain/：讀 WORK_STATE.md 的「下一步」與等待中的完成標記，從那裡繼續。規則見 HANDOFF.md 第 0 節；B 的完成條件見第 7 節。回覆不超過 10 行。
```

## 提示詞 C（凍結・啟動最終驗證）

```text
接續 AI_Solutions/CodexParallelChain/。只讀 HANDOFF.md 第 0、1、6 節與 WORK_STATE.md。
1. 確認 WORK_STATE 有「B 完成」，而且 HANDOFF.md 第 7 節的最後檢查都有紀錄（最後一次訓練的 src_sha256 等於目前 src）；沒有就停下來回報。
2. 凍結：在 Testcases/results/CodexParallelChain/ 寫 frozen_sources.sha256（AI_Solutions/CodexParallelChain 下 src、tests、CMakeLists.txt、Makefile 的相對路徑）與 freeze.json。
3. 確認 Testcases/edge_seed6 不存在，然後 nohup 啟動：
   python3 Testcases/results/CodexParallelChain/scripts/final_validate.py --seed-dir edge_seed6 --seed 20261024
   （它會配對重跑 CodexParallelChain / ClaudeOpus55_v6 / ChainDP，產生新 seed 並跑 v4 / ChainDP / v6 / 新版，三種 HPWL 協定；約 7 小時；完成標記 final/ALL_DONE）
4. WORK_STATE 寫「等待中：final_validate.py，完成標記 final/ALL_DONE；之後跑 final_report.py」，然後結束。不要守著。
回覆不超過 8 行。
```

## 提示詞 D（彙整・文件）

```text
接續 AI_Solutions/CodexParallelChain/。final/ALL_DONE 已出現。只讀 HANDOFF.md 第 0、6 節與 WORK_STATE.md。
1. python3 Testcases/results/CodexParallelChain/scripts/final_report.py（產生 final/summary.md），只看它印的 5 行與 summary.md 的表頭和未達標表。
2. 寫 AI_Solutions/CodexParallelChain/README.md：診斷摘要、改了什麼、數據（全部取自 final/summary.md）、試過沒用的、還沒做到的（逐筆列出未達標與差距）。
3. 在 AI_Solutions/COMPARISON.md 末尾加第 12 節（不要改既有內容；加之前記下原檔 sha256、加完確認前段不變）。
4. 在 AI_Solutions/HISTORY.md 的表 1、2、5、6 各加一列 CodexParallelChain（只加列，不改舊數字）。
5. Testcases/results/edge/make_table.py 的版本清單加上 CodexParallelChain，重建 tables.md。
6. 核對保護資料夾與凍結原始碼的 sha256 都沒變。
回報：每個驗收目標達成多少、逐筆未達標與差距。回覆不超過 25 行。
```
