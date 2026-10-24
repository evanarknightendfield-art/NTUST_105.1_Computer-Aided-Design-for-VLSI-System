# CodexCapDP — 局部上限修復與候選回收

以 ClaudeOpus55_v5 為底，第二階段已完成實作、調參、凍結與驗證。**品質目標部分達成，沒有全部達標。**
主要 11 筆位移雙條件從診斷 run A 的 6/11 提高到本次 9/11；27 筆＋L1＋L2 全部合法且有輸出。
同總時間協定的 HPWL 達標；預設 holdout、e08 總位移、cap 每次嘗試普遍大幅降成本仍未達成。

資料入口：[最終逐筆表](../../Testcases/results/CodexCapDP/final/summary.md)、[計時／四版本 seed4 詳表](../../Testcases/results/CodexCapDP/final/details.md)、[第一階段診斷](../../Testcases/results/CodexCapDP/diagnosis.md)、[工作紀錄](WORK_STATE.md)。
計算已全部完成；不需重跑。凍結後沒有修改演算法，沒有 commit。

## 1. 診斷與改動

診斷 run C：h04／h06／h07／h08 的 cap 時間有 80%～88% 花在 DP；h06／h08 修復後總位移代表增幅 42%／44%，主要來自原未違規 cell 的同 segment 重排。
h01 則花 8.96 秒在 row 修復、DP 只有 0.90 秒。另有符合外部 ChainDP 驗收、卻被內部總位移上限拒絕的候選。

- **直接在上限 D 下修復局部視窗**：先做保留 row membership／cell 順序的 cap packing，再以違規 cell 為中心試 1／2／3 rows、最多 12 cells 的精確視窗。成功才提交；未成功時把合法的局部進展交給既有 row／site 修復。
- **修復後限制 DP 範圍**：初始範圍是違規或實際移動的 cell，只處理碰到這些 cell 的視窗；接受移動後擴充範圍，保留候選自己的 warm memo。修復仍可能影響多段，不能保證範圍始終很小。
- **修正 memo 失效範圍**：實際移動與鄰居標記分開，避免鄰居標記繼續向右傳播；視窗尺寸改變或 cap 放寬時失效。位移 DP 的時間檢查由每 256 個 anchor 改為每 16 個。
- **把預算留給完整候選**：刪除獨立 feasibility probe；有潛力但總位移偏高的候選續跑 DP／短 chain，餘額再給普通 chain 與 DP。初始降幅依 cell 數縮放，成功加大、失敗減半，剩餘時間太少不開始新候選。
- **內部總位移比例 1.004→1.0048**：基準仍是 cap 前的總位移，與外部 ChainDP×1.005 不同。沒有讀取參考答案或依 benchmark 名稱設定特例。
- **預算與大設計排程**：額外階段仍為 ≤2000 cells 12 秒、其他 6 秒，預留檢查開銷；預設 ≥500000 cells 改 SA30→polish60，合計仍 90 秒。明確指定 `--time` 或 `--polish-time` 時保留使用者設定。
- **回退測試**：共用 `guardedStep` 抽成可測函式；已有合法起點的可選最佳化遇到例外或違反 invariant，還原前一步 placement 並繼續輸出。

平行 SA、chain 核心與 thread pool 沒有修改。原型與量測工具在 tests/；正式 dp 只連結 src/。

## 2. 最大／總位移與時間

正式結果取本次 local_grade 預設執行；參考固定為 [v5 phase1/lg_chain/record.csv](../../Testcases/results/ClaudeOpus55_v5/phase1/lg_chain/record.csv)。
條件為最大 ≤ ChainDP、總位移 ≤ ChainDP×1.005。牆鐘預算使不同執行的 Stage 1 可能不同；不把調參快取與正式結果混成一輪。

| 集合 | PASS | 最大達標 | 總位移達標 | 雙條件 | 全流程秒數合計 |
|---|---:|---:|---:|---:|---:|
| testcases | 3/3 | 3/3 | 3/3 | 3/3 | 191.74 |
| holdout | 8/8 | 6/8 | 8/8 | 6/8 | 427.41 |
| edge | 16/16 | 14/16 | 14/16 | 12/16 | 578.04 |
| all | 27/27 | 23/27 | 25/27 | 21/27 | 1197.19 |

主要五筆：

| bench | 新最大 | Chain 最大 | 新總位移 | 總 ÷ Chain |
|---|---:|---:|---:|---:|
| h01 | 29.390000 | 29.390000 | 2358.8100 | 0.996502 |
| h04 | 237.850000 | 231.760000 | 180397.5900 | 0.991465 |
| h06 | 427.550000 | 428.020000 | 890103.7000 | 0.999832 |
| h07 | 47.676200 | 47.416200 | 109169.8736 | 0.994326 |
| h08 | 28282.231000 | 28421.640000 | 125536288.8270 | 0.995714 |

全部未達位移目標的筆數如下；總位移超額是扣掉允許的 +0.5% 後仍超出的量。

| bench | 最大超額 | 最大差 % | 總位移超額 | 總位移差 % |
|---|---:|---:|---:|---:|
| h04 | 6.090000 | 2.6277% | 0.000000 | -0.8535% |
| h07 | 0.260000 | 0.5483% | 0.000000 | -0.5674% |
| e03_dense97 | 7.966000 | 6.1358% | 0.000000 | -0.6071% |
| e08_fragmented | 0.000000 | 0.0000% | 495.099415 | +1.0537% |
| e12_collapse | 4.741000 | 2.0770% | 0.000000 | -0.8810% |
| e16_rowgaps | 0.000000 | 0.0000% | 129.386510 | +0.6357% |

**h04 限制**：調參 full_v5 試出 max 234.90／total 182056.35，但被內部總位移上限拒絕，且最大仍未到 231.76；沒有找到達標候選的證據。
**h07 限制**：同輪候選 max 47.33755／total 109928.01224 已符合外部兩條件（總上限 110341.82038），卻被內部 pre-cap×1.0048 拒絕。
全域放寬比例曾使其他訓練筆總位移失敗，最終仍保留這項不足。
edge 的失敗只記錄回歸結果，沒有做失敗導向調參，也沒有把時間內未解出視為不可行證明。

### 每次 cap 嘗試是否便宜很多？尚未普遍達成

| bench | v5 full 平均秒 | 新可行候選平均秒 | v5 cap 總秒 | 新 cap 總秒 |
|---|---:|---:|---:|---:|
| h01 | 0.3435 | 0.7890 | 10.49 | 6.40 |
| h04 | 1.0065 | 0.7085 | 4.55 | 4.45 |
| h06 | 1.3037 | 3.9893 | 4.61 | 4.39 |
| h07 | 1.2380 | 1.4790 | 4.55 | 4.44 |
| h08 | 1.8628 | 4.5157 | 4.62 | 4.52 |

舊數據為 run C、新數據為 full_v5，目標 D 與候選不同，**不是同一候選配對加速比**。
新方法減少獨立探測、把時間集中到完整候選；h06／h08 成功的單次 DP 仍需 3.579／4.104 秒。
h01 從 32 probes＋25 full 改成 8 個完整候選；h04 為 6 個完整候選。
新版 DP 計時也含候選短 chain 回收與統計；逐次時間和理由見 [cap_attempts.csv](../../Testcases/results/CodexCapDP/final/cap_attempts.csv)。

額外階段時間取自另一次 timestamp Stage 1（同一凍結演算法）：主要 11/11 在 12／6 秒內；27 筆共 **26/27**。
**e07_tworows 用 6.245429 秒，超出 0.245429 秒**；最後 unresolved repair 花 1.822982 秒、DP 0 秒。
可定位到修復呼叫超時，但沒有內部分步計時；尚未做到硬 deadline。來源：[extra_times.csv](../../Testcases/results/CodexCapDP/final/extra_times.csv)。

## 3. HPWL：三種協定，各 3 seeds

平均是「每筆三 seed 平均 HPWL 比值」的幾何平均；範圍是三個 seed 各自分組幾何平均的 min／max，非信賴區間。
各筆原始 HPWL mean／min／max、attempts 與時間在各協定 summary.md；每種新版皆 24×3=72/72 PASS。

| 協定 | 分組 | 新版 ÷ v4 | 三 seed 範圍 | v5 目標 | 結果 |
|---|---|---:|---|---:|---|
| fixed | holdout | 0.993494 | 0.992876–0.993921 | 與 v5 相同 | 固定次數結果相同 |
| fixed | edge | 0.993109 | 0.992215–0.994215 | 與 v5 相同 | 固定次數結果相同 |
| equal | holdout | 0.985721 | 0.983282–0.987390 | 0.9907 | 達標 |
| equal | edge | 0.986691 | 0.984118–0.991776 | 0.9876 | 達標 |
| default | holdout | 0.992399 | 0.991662–0.993700 | 0.9919 | **未達** |
| default | edge | 0.993073 | 0.990243–0.995103 | 0.9938 | 達標 |

- [fixed 固定次數](../../Testcases/results/CodexCapDP/final/hpwl_fixed/summary.md)：共同 v4 起點、單執行緒、500 萬 proposals、無 polish。fixedonly 無可動 cell 時 attempts=0。新版與 v5 的 72 筆 HPWL／attempts 完全相同，沒有顯示 SA 核心品質變化。
- [equal 同總時間](../../Testcases/results/CodexCapDP/final/hpwl_equal/summary.md)：預算=max(v4／新版 Stage 1)+12 秒；各版自己 Stage 1 成本計入，polish 2 秒，SA 用剩餘預算，自動執行緒。v4／新版本次配對執行；v5 使用歷史同協定目標。
- [default 預設參數](../../Testcases/results/CodexCapDP/final/hpwl_default/summary.md)：各版自己 Stage 1，SA60／polish30、自動執行緒；集合不含 L2。v4／v5 的 fixed、default 參考紀錄沿用已完成的相同協定，來源與 hash 在 reuse.json；新版三 seeds 都實跑。

同總上限不代表實際秒數相等：holdout v4／新版平均計費 28.252／33.375 秒，edge 14.381／16.684 秒；新版單筆最大超預算分別 0.019／0.029 秒。
沒有把它描述成嚴格同實際耗時實驗。預設 holdout 超目標 0.000499（相對目標 +0.0503%）；其中 h02／h05／h06／h07／h08 對 v5 的三 seed 平均分別 +0.2928%／+0.1418%／+0.1088%／+0.1004%／+0.5315%。
這些是不同輪次結果，沒有足夠資料把差距全歸因於某個程式改動。

## 4. chain、L1／L2 與穩健性

**沒有拿回 e08 的總位移代價。** 最終 e08 總位移 90357.493，對 ChainDP +1.0537%，對歷史 v5 的 90333.005 又多 +0.0271%。
沒有全面恢復 chain 4 秒；改在 cap 剩餘預算內回收總位移，沒有增加設定的額外階段預算。
但這不是 e08 的 chain 配對消融，不能宣稱已回收原先 +0.58% 的代價或已保證絕不壓縮 SA；e07 仍有超時。

| 大設計 | PASS | 全流程秒 | 上限 | 最終 HPWL | 對歷史 v5 HPWL |
|---|---|---:|---:|---:|---:|
| L1 | PASS | 187.88 | 240 | 1741786725.8 | +0.3671% |
| L2 | PASS | 199.42 | 600 | 1724913056.0 | +3.3157% |

第一階段 L2 共同起點、seed 1 配對曾顯示 SA30→P60 比 SA60→P30 好 3.3037%，所以採用該排程。
最終 L2 合法化起點不同，**全流程沒有重現 HPWL 改善**；兩種結果分開報告，沒有多 seed 大設計改善的證據。

C++ 一般編譯 **10/10 PASS**，ASan＋UBSan **10/10 PASS**：
[一般 log](../../Testcases/results/CodexCapDP/phase2/tests_v5.log)、[sanitizer log](../../Testcases/results/CodexCapDP/phase2/asan_tests_v5_unsandboxed.log)。
首次 sandbox 的 LeakSanitizer 因 ptrace 環境退出失敗；相同 binary 在核准的 sandbox 外重跑全過，沒有停用 sanitizer。
新增 cap_windows 測 memo 鄰居、固定順序 packing、隨機 cap 合法性與 rollback；guard 測 invariant 失敗、標準／非標準／worker 例外與下一步成功。
既有 parallel_sa 測試核對同 seed、同執行緒數、固定 attempts 的 seg/site 完全相同。
平行 SA 相關來源與 v5 相同，因此未另跑 TSan。已有合法起點的可選最佳化錯誤會回退；無效輸入、無可行合法解或 I/O 失敗不在有效輸出保證內。

## 5. 凍結後 seed4

先凍結 51 個 src／tests／build-definition 檔，再用 numpy 2.4.6 的指定 Python 執行
`gen_edge.py Testcases/edge_seed4 --seed 20261022 --check`。四版本 local_grade 各 **16/16 PASS**。
凍結資訊：[freeze.json](../../Testcases/results/CodexCapDP/freeze.json)、[frozen_sources.sha256](../../Testcases/results/CodexCapDP/frozen_sources.sha256)。
manifest SHA256：`b9d4ac797f3804a1eded6e4975ebfdb15dc2756e23212fcc5f5bcae0c67cc08b`。

新版對本次 ChainDP 最大位移幾何平均 0.961788、總位移 1.000647（排除共同零位移，14 筆）；HPWL 0.997520（16 筆、單一預設 seed）。
位移雙條件 **11/16**，逐筆不足如下；總位移超額同樣扣掉允許的 +0.5%。

| seed4 bench | 最大超額 | 最大差 % | 總位移超額 | 總位移差 % |
|---|---:|---:|---:|---:|
| e05_bignets | 0.000000 | 0.0000% | 36.540090 | +0.5293% |
| e06_tallnarrow | 0.000000 | 0.0000% | 25.839225 | +0.5756% |
| e08_fragmented | 0.000000 | 0.0000% | 328.661195 | +0.9883% |
| e12_collapse | 6.000000 | 2.5532% | 0.000000 | -0.9542% |
| e16_rowgaps | 2.350000 | 3.6124% | 0.000000 | +0.0516% |

四版本逐筆最大／總位移／HPWL／時間均見 [details.md](../../Testcases/results/CodexCapDP/final/details.md)。這是凍結後未見測資回歸，不用它再改演算法。

## 6. 試過但未採用的方向

| 實驗 | 數據與取捨 |
|---|---|
| 單靠局部 cap 視窗 | 降 0.5% 時，direct_v1 的 h04/h06/h07/h08 仍剩 4/17/31/49 顆違規；加固定順序 packing 的 direct_v2 仍剩 1/6/4/13，故保留 fallback。 |
| 修復後固定只給 1 秒 DP | hybrid_v1 的 h08 降 1% 後總位移約 127.619M、2.749 秒，總位移成本太高；最終讓有潛力候選續跑。 |
| cap-aware chains／固定 site 原型 | 工具留在 tests/，未整合 src；各次輸出在 phase2/chain_cap_v1、chain_cap_v2、site_cap_v1。 |
| 比例一律放到 1.007 | h01 max 29.28、total 2360.27 可通過；h03 total 7086.41953 卻超過外部 7075.39849，所以不採用。 |
| 只 packing 違規 row，比例 1.005 | full_v4 的 h01 max 31.07，前版 29.67；ibm01 total 超外部上限 3558.6867。恢復所有 row 固定順序 packing，改 1.0048。 |
| 全面恢復 chain 4 秒 | 第一階段共同起點 holdout：1.5 秒相對 4 秒總位移幾何平均多 0.0366%，4 秒多耗 13.47 秒；不採用全面延長。 |

調參只用 testcases＋holdout，L2 排程來自指定的第一階段單 seed 診斷；edge 與 seed4 只回歸。
沒有重跑 edge_seed2／3，它們不在本次指定的最終 27＋L1＋L2＋新 seed4 套件中。
探索紀錄在 [WORK_STATE](WORK_STATE.md)；所有原型都不參與正式 dp 編譯。

## 7. 尚未做到與後續邊界

1. 讓所有 cap 嘗試普遍便宜很多；h06／h08 的單次 DP 仍重，h07 的內外部總位移基準不一致仍會丟掉合格候選。
2. 每筆最大不輸 ChainDP、總位移不超 +0.5%，以及 e08 chain 代價回收；未達筆已完整列出。
3. 預設 holdout HPWL 不輸 v5；目前相對目標差 +0.0503%。L2 全流程 HPWL 也沒有改善。
4. 嚴格 6 秒硬截止；e07 多 0.245429 秒。回退測試通過不等於 deadline 已嚴格保證。

目前交付點保持凍結；之後若再改，須建立新一輪來源 hash／訓練驗證，不能沿用本輪凍結與未見 seed4 的宣稱。

## 8. 編譯與結果位置

從 Lab3 執行，所有產物放結果目錄：

```bash
taskset -c 16-19 /home/evan/anaconda3/envs/eda/bin/cmake -S AI_Solutions/CodexCapDP -B Testcases/results/CodexCapDP/build
taskset -c 16-19 /home/evan/anaconda3/envs/eda/bin/cmake --build Testcases/results/CodexCapDP/build -j4
Testcases/results/CodexCapDP/build/dp INPUT_PREFIX OUTPUT_PREFIX
```

正式驗證由 `Testcases/results/CodexCapDP/scripts/phase2_final.py` 完成，完成標記 `final/ALL_DONE.json`；不要重跑已完成的量測。
只重建文件數據可用 `scripts/final_summary.py`、`scripts/final_details.py`，分析均釘 E-core 16–19。
指定的 `Testcases/results/run_edge.sh CodexCapDP` 已完成；標準表見 [edge/tables.md](../../Testcases/results/edge/tables.md)。
