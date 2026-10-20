# CodexChainDP — 全域 cap 修復與多候選 HPWL 交換

> **2026-10-20 整理：** `AI_Solutions/` 只留 v1、WindowDP、v4、本版。比較腳本（`tests/compare.py` 等）會重新編譯的
> v2、v3、CodexGapSA、CodexParetoDP 已移除（原始碼在 git commit `e1fd81f`），CodexAugmentDP 沒有進過 git。只需要 v4 的驗收
> 腳本（`acceptance.py`、`fair_time.py`、`measure.py`）不受影響。

本版在既有 ChainDP / ClaudeOpus55_v4 上繼續開發。未修改其他版本、未 commit。所有實驗、編譯與驗證產物在 `../../Testcases/results/CodexChainDP/`。

## 實作

- 保留 v4 合法化、兩／三 row 視窗 DP、平行 SA，以及 ChainDP 的 ejection chains 與例外回退。
- 新增 `src/global.cpp`：對位移上限 D 搜尋可行 placement。先用真正受 cap 限制的 L1 row clumping；小設計再試全域 row 重建與完整候選位置的 MRV 搜尋；最後用所有可達 row 的 site congestion 價格反覆重新分配連續 cell 區間。容量不是可切割的 cell 面積，成功一定有實際合法座標。
- 搜尋失敗是 UNKNOWN。二分是啟發式的試探順序，失敗點不是數學下界，也不宣稱等價於完整 ISPD'17 flow。完整候選搜尋有時間與大小限制。
- 每個成功候選交給 v4 的 `windowSweeps` / `solveMulti` 壓總位移；只有最大位移不增加且總位移不超過原 incumbent × 1.01 才接受。整步有 placement snapshot 與合法性檢查，失敗退回原解。
- SA 冷卻後，4% proposal 改成比較 optimal-region 附近多個 global / vertical swap 與 whitespace 候選，以真正的 affected-net union HPWL 選擇，再由原 SA 接受規則決定。每個 proposal 的候選評估多於 v4，不能把同次數當作同時間。
- 小而稀疏的設計不啟用 10／12-cell 大視窗（利用率 < 0.4 且 cell 數 ≥ 100）。仍保留 8／9-cell 視窗與 max 修復。這是一般密度／規模條件，沒有測資名稱分支。
- 原有 worker exception propagation 與階段 rollback 保留；各最佳化步驟的 guard 額外接住 non-standard exception。沒有改 SA 每個 worker 的座標／net box 複製，也沒有實作 vertical strip 平行分割。

## 預設參數

一般呼叫為 `dp INPUT_PREFIX OUTPUT_PREFIX`。其餘預設沿用 v4：Stage 1 精修 90 秒、SA 60 秒、polish 30 秒、自動執行緒。Chain 階段最多 4 秒、global cap 搜尋最多 16 秒；這些是附加預算，不能稱為與 v4 同時間。

| 選項 | 預設 | 用途 |
|---|---:|---|
| `--global-time` | 16 | 全域 cap 修復秒數；0 關閉 |
| `--total-ratio` | 1.01 | 可接受總位移 / global 階段前總位移 |
| `--global-swap` | 0.04 | 冷 SA 多候選 swap 比率；0 關閉 |
| `--chain-time` | 4 | chain 與後續 DP 秒數 |

## 重現與資料

```bash
# 從 Lab3 執行；本機 cmake 位於 eda 環境
export PATH=/home/evan/anaconda3/envs/eda/bin:$PATH
cmake -S AI_Solutions/CodexChainDP -B Testcases/results/CodexChainDP/build
cmake --build Testcases/results/CodexChainDP/build -j4
ctest --test-dir Testcases/results/CodexChainDP/build --output-on-failure
python3 AI_Solutions/CodexChainDP/tests/acceptance.py > Testcases/results/CodexChainDP/acceptance_progress.log 2>&1
python3 AI_Solutions/CodexChainDP/tests/final_validation.py > Testcases/results/CodexChainDP/final_progress.log 2>&1
python3 AI_Solutions/CodexChainDP/tests/fair_time.py --schedule default --suite equal_time_default --reuse-stage1 equal_total_time > Testcases/results/CodexChainDP/equal_time_default.log 2>&1
python3 Grader/tools/local_grade.py AI_Solutions/ClaudeOpus55_v4 ClaudeOpus55_v4 Testcases/edge_seed2 --work Testcases/results/CodexChainDP/baseline_seed2 --dp-args=-v > Testcases/results/CodexChainDP/baseline_seed2.log 2>&1
python3 AI_Solutions/CodexChainDP/tests/summarize.py > Testcases/results/CodexChainDP/summarize.log 2>&1
python3 AI_Solutions/CodexChainDP/tests/final_audit.py > Testcases/results/CodexChainDP/audit.log 2>&1
python3 AI_Solutions/CodexChainDP/tests/update_docs.py > Testcases/results/CodexChainDP/publish.log 2>&1
```

`acceptance.py` 內部會執行 `Testcases/results/run_edge.sh CodexChainDP`，並以 `EDGE_WORK_ROOT` 把原始結果寫到本版資料夾。中斷後可加 `--resume`，但來源 hash 必須一致。`final_validation.py` 的最後一步才建立 `Testcases/edge_seed2/`；若該資料夾已存在，它會停止，避免把看過的資料誤稱為全新驗證。產生器本身可用 `python3 Grader/tools/gen_edge.py NEW_DIRECTORY --seed NEW_SEED --check` 重建其他 seed；不傳 `--seed` 保留原始 seed 20261019。

上述流程使用已保存的 `fixed_work_holdout/record.csv` 開發實驗。若要另外重跑這組固定次數比較，可執行 `python3 AI_Solutions/CodexChainDP/tests/measure.py sa --suite fixed_work_holdout_replay --groups holdout --start-suite train_stage1 --moves 5000000 --seconds 1000`；它會從兩版來源各自編譯共用測量程式，使用保存在 `train_stage1/` 的相同開發期合法起點。若改用其他起點，應另命名實驗，不能視為原數據的逐次重現。

`acceptance.py` 用 local_grade.py 打包、重新編譯、檢查全部七個輸出與 stdout。`-v` 只增加 stderr 診斷，演算法選項仍是預設。固定次數與相同時間是不同實驗：`measure.py sa` 是同一合法起點、單執行緒、固定 500 萬 proposal；`fair_time.py` 使用各版本自己的預設 Stage 1 解與自動執行緒，計入各自 Stage 1 的實測成本，總預算取兩者較慢的 Stage 1 + 12 秒，最後 2 秒給 polish。Stage 1 不受 SA seed 影響，該實驗快取一次 Stage 1，再每個 seed 計入相同成本；不是整支程式的硬即時截止保證。表中會同時列預算和實際記帳時間。

原始證據以各實驗的 `record.csv` 為準。開發只使用 testcases / holdout；既有 edge 在演算法凍結後驗證，之後才產生新 seed。新 seed 驗證後發現量測 harness 的超大 SA 次數上限問題，因此另外補跑預設 SA 排程與新 seed 的 v4 基準；這期間沒有再修改演算法或依新測資調參。

## 正式驗收（2026-10-20，演算法凍結後）

原始紀錄：[預設比較 CSV](../../Testcases/results/CodexChainDP/default_comparison.csv)、[完整彙總](../../Testcases/results/CodexChainDP/summary.json)。v4 與 ChainDP 都以 local_grade.py 打包、重新編譯、獨立驗證；預設只加 `-v` 診斷。

| 分組 | 筆數 | 最大位移幾何平均 ÷ v4 | 總位移幾何平均 ÷ v4 | v4 / ChainDP 合計秒數 |
|---|---:|---:|---:|---:|
| testcases | 3 | 0.999477（-0.05%） | 0.999643（-0.04%） | 146.74 / 170.35 |
| holdout | 8 | 0.964922（-3.51%） | 1.004883（+0.49%） | 329.17 / 458.95 |
| edge | 16 | 0.945468（-5.45%） | 0.996269（-0.37%） | 466.03 / 584.08 |
| all | 27 | 0.958015（-4.20%） | 0.999423（-0.06%） | 941.94 / 1213.38 |

逐筆最大位移比 v4 差的測資：無（27/27 不增加）；其中 20 筆嚴格下降。總位移比較排除兩筆 0/0，其餘 25 筆；最大位移亦同。預設 HPWL 是 seed 1 的驗收數字，不能獨自當成進步證據。

| bench | v4 最大位移 | ChainDP 最大位移 | 總位移變化 | 最大位移變化 |
|---|---:|---:|---:|---:|
| ibm01 | 10,596.97 | 10,580.34 | -0.11% | -0.16% |
| ibm05 | 202.747 | 202.747 | -0.00% | +0.00% |
| toy | 2,007.9 | 2,007.9 | +0.00% | +0.00% |
| h01 | 31.39 | 29.39 | +0.82% | -6.37% |
| h02 | 2,607.298 | 2,541.499 | +0.92% | -2.52% |
| h03 | 20.9775 | 20.9394 | -0.28% | -0.18% |
| h04 | 241.55 | 231.76 | +0.76% | -4.05% |
| h05 | 1,494.987 | 1,337.305 | -0.17% | -10.55% |
| h06 | 434.81 | 426.65 | +0.33% | -1.88% |
| h07 | 48.0254 | 47.3388 | +0.74% | -1.43% |
| h08 | 28,600.483 | 28,421.64 | +0.79% | -0.63% |
| e01_nearlegal | 5.595 | 5.423 | +0.00% | -3.07% |
| e02_legalgp | 0 | 0 | +0.00% | +0.00% |
| e03_dense97 | 144.25 | 129.829 | +0.09% | -10.00% |
| e04_sparse10 | 1.7841 | 1.7841 | -0.57% | +0.00% |
| e05_bignets | 64 | 61.401 | -0.04% | -4.06% |
| e06_tallnarrow | 41.719 | 31.652 | -0.53% | -24.13% |
| e07_tworows | 29.997 | 26.997 | +0.02% | -10.00% |
| e08_fragmented | 74.604 | 74.535 | -1.89% | -0.09% |
| e09_offgrid | 36.6912 | 36.6912 | -0.26% | +0.00% |
| e10_mixedsites | 51.763 | 46.25 | -0.57% | -10.65% |
| e11_format | 14.327 | 13.315 | -0.94% | -7.06% |
| e12_collapse | 233 | 228.259 | +0.93% | -2.03% |
| e13_onecell | 4.57 | 4.57 | +0.00% | +0.00% |
| e14_fixedonly | 0 | 0 | +0.00% | +0.00% |
| e15_realistic | 4,891.49 | 4,890.885 | -0.43% | -0.01% |
| e16_rowgaps | 66.479 | 65.29 | -1.01% | -1.79% |

這張表逐筆列出總位移／最大位移取捨。預設的 1.01 門檻是相對 global 修復前的 incumbent；最後仍以獨立重跑的 v4 數據判斷整體驗收。

### 精確最佳值與剩餘差距

| bench | 本次最大位移 | 精確最小值 | 高於最小值 |
|---|---:|---:|---:|
| toy | 2,007.9 | 1938.4 | +3.59% |
| h01 | 29.39 | 26.5 | +10.91% |

已重新執行原始 `Grader/tools/exact_bound.py --max --max-ilp --time-limit 600`；[證明紀錄](../../Testcases/results/CodexChainDP/exact_max/record.csv)。整數模型的最小值與啟發式的可行結果分開報。
e01_nearlegal 的最大位移是 5.423，比產生器已知合法解的 5.4 高 +0.43%；5.4 是可行解，沒有宣稱它是精確最小值。

### HPWL：固定次數與固定總預算分開看

| 實驗 | 分組 | 三 seed 平均的 HPWL 幾何平均 ÷ v4 | 平均相對比率 |
|---|---|---:|---:|
| 固定 500 萬 proposal | holdout | 0.993598（-0.64%） | 0.993609 |
| 固定 500 萬 proposal | edge | 0.993053（-0.69%） | 0.993085 |
| 相同總預算／預設 SA 排程 | holdout | 1.015273（+1.53%） | 1.015558 |
| 相同總預算／預設 SA 排程 | edge | 1.014429（+1.44%） | 1.014859 |
| 相同總預算／10 億次排程壓測 | holdout | 1.094972（+9.50%） | 1.117353 |
| 相同總預算／10 億次排程壓測 | edge | 1.030373（+3.04%） | 1.033034 |

每一筆、每個版本的三 seed **平均與 [最小, 最大]**：[固定次數詳表](../../Testcases/results/CodexChainDP/fixed_work_table.md)、[同總預算詳表](../../Testcases/results/CodexChainDP/equal_time_default_table.md)。逐次原始紀錄保留實際 SA attempts、SA 秒數與總記帳秒數。
兩 row 的 e07：固定次數三 seed 平均 v4 205,592.3333、ChainDP 205,949.3333（+0.17%）；範圍重疊，這筆沒有證據顯示 move 品質改善。

固定次數用共同合法起點、單執行緒，隔離 move 品質；同總預算用各自的 Stage 1 placement、自動執行緒，計入 Stage 1 代價。它們不是同一個實驗，不能混成單一進步數字。固定次數的 holdout 初始解來自開發期快照，edge 初始解來自凍結後 ChainDP 的預設 Stage 1；同一測資的兩版本起點完全相同。fixed-only 案例兩版本均無 SA move，實際 attempts 為 0。三 seed 的範圍重疊時，不宣稱統計顯著或一般性優勝。

原 10 億次排程會讓部分短時限的大設計停在第一個高溫階段；全部數據保留為壓力測試，不能代表預設 SA。補充實驗只恢復 SA 的預設 effort，沒有改 solver。詳見 [排程檢查](../../Testcases/results/CodexChainDP/schedule_audit/README.md) 與 [壓測三 seed 詳表](../../Testcases/results/CodexChainDP/equal_total_time_table.md)。共同時間上限不保證實際用時相等；小設計可能先做完預設次數，詳表列出實際記帳秒數。

### 穩健、時限與最後的新 seed

- 標準 27 筆：ChainDP 27/27 PASS；L1：PASS，196.36 秒（local_grade 外部 timeout 240 秒）。
- L1 預設輸出：總位移 2,441,140,222.48、最大位移 50,390.273、SA 後 HPWL 1,783,212,801.1（seed 1；只作驗收，不宣稱多 seed 品質進步）。
- 新 seed 20261020（演算法凍結後產生；其後只有量測排程補充，未改演算法）：16/16 PASS，資料在 `Testcases/edge_seed2/`；[紀錄](../../Testcases/results/CodexChainDP/edge_seed2/record.csv)。e13、e14 為固定無亂數案例，不把它們宣稱為全新隨機情境。
- 新 seed 與 v4 的預設比較：最大位移幾何平均 -5.60%、總位移 -0.31%，最大位移退步案例：無。[逐筆比較](../../Testcases/results/CodexChainDP/fresh_comparison.csv)。
- 原 seed 的完整檔案重生後逐一 SHA256 比對：[紀錄](../../Testcases/results/CodexChainDP/generator_default_hashes.csv)。原 edge 未被覆寫。
- e04_sparse10：本次 v4 13.34 秒，ChainDP 8.53 秒。
- 7 套 C++ 回歸／窮舉測試與 ASan/UBSan 測試通過；最後的離散 L1 median 修正也重新通過 global 回歸的 ASan/UBSan 檢查：[最終紀錄](../../Testcases/results/CodexChainDP/final_global_sanitized/record.csv)。LSan 在 sandbox 的 ptrace 環境不能運作，改到 sandbox 外跑過，沒有隱藏原失敗紀錄。

### 三個目標的完成程度

1. 最大位移：幾何平均 -4.20%，總位移 -0.06%；逐筆退步數 0。toy／h01 到精確解的剩餘差距見上表，尚未實作可證明最優的全域 minimax。
2. HPWL：未達標。同總預算的 holdout +1.53%、edge +1.44%。這是共同時間上限的比較，不能把實際用時不同的執行宣稱為嚴格同耗時優勝。worker cache 複製與橫／直分割交替未完成。
3. 穩健／時間：44/44 個要求範圍內的 local_grade 執行通過；L1 實測與稀疏設計時間如上。其他更大規模不由這些結果推論。

## 試過但沒有用／仍有限制

- 單靠 site congestion：toy 未找到更小 cap，h01 改善有限；不可把 relaxation／搜尋失敗誤當整數不可行。
- 小設計完整位置 DFS：大量等價 packing 耗盡預算，不能取代成熟 MILP。保留嚴格時間／位置數限制。
- 全域 greedy row 重建有時能降低 max，卻讓總位移增加；例：toy 約 +1.5% 的候選被預設門檻拒絕。另有 [tradeoff_5pct/record.csv](../../Testcases/results/CodexChainDP/tradeoff_5pct/record.csv) 記錄放寬到 1.05 的結果，不混入預設比較。
- 增加 SA 候選評估有計算成本；固定次數略好不代表相同總時間更好。
- 平行 SA 的 cache 複製與 band 邊界損失尚未解決。
