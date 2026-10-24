# ClaudeOpus55_v5 — ChainDP + 用滿 SA 時間 + row 重新分配的最大位移搜尋

以 `CodexChainDP` 為底（複製 `src/`、CMake、C++ 測試）。先做診斷、寫成報告，再依報告的排序改程式。
所有實驗輸出在 `../../Testcases/results/ClaudeOpus55_v5/`，原始碼資料夾裡不放結果。

- 診斷報告：[`Testcases/results/ClaudeOpus55_v5/diagnosis.md`](../../Testcases/results/ClaudeOpus55_v5/diagnosis.md)
- 用法跟前幾版一樣：`dp INPUT_FILE OUTPUT_FILE`，stdout 印 4 行。

## 診斷摘要（第一階段）

| 問題 | 量到的 |
|---|---|
| ChainDP 同時間 HPWL 差 1.5% 的原因 | **Stage 1 吃掉 SA 時間**：27 筆 Stage 1 合計多 256 秒（global 205 秒、chain 68 秒）。同時間協定下 SA 被時間卡住的 16 筆差 2.4%，SA 跑得完的 7 筆反而好 0.5% |
| SA 本身（v4、ChainDP 都有） | 每一步的次數**只會減不會增**：平行 SA 只用 60 秒中的 43~50 秒，L1 只做計畫的 15%、最後接受率 13.5%；平行 SA 的 merge 佔 25~37% |
| 最大位移 | toy、h01 的最佳解是**整個 row 重新分配 + 整段平移**；ChainDP 的四個子方法都在固定 site 上一次動一顆，toy 18 次嘗試全部失敗 |
| 總位移 | ChainDP 在 7 筆用了 +0.5~0.93%；toy 精確解顯示兩者可以同時變好 |
| 大設計 | L1 201 秒、L2 214 秒（都在時限內，L2 不會超時）；合法化碰到 90 秒預算 |

## 改了什麼（第二階段，依診斷的排序）

| # | 改動 | 檔案 | 為什麼（診斷） | 效果 |
|---:|---|---|---|---|
| 1 | **SA 每一步的次數可以調回原本的計畫**（v4 / ChainDP 只會往下調） | `annealer.cpp`（串列 `run`、平行 `annealParallel`） | 第 0 步最熱最慢，之後一直用它的速度，平行 SA 只用 43~50 秒、L1 只做計畫的 15% | 平行 SA 用滿 60 秒；ibm01 3.21 億次（v4 1.84 億） |
| 2 | **平行 SA 的 merge 改成「一份共用狀態」**：各 band 平行寫出自己移動的 cell 和 row，碰到的 net 去重後**只算一次** box（平行分塊），各執行緒只複製變動的部分 | `annealer.cpp`（`Shared`、`exportBand` / `importFrom` / `loadBand`）、`rowmap.hpp`（`setCells`） | v4 每個執行緒都重做其他所有 band 的更新（row list 刪除插入 + net box），佔 SA 的 25~37% | ibm01 merge 14.4 → 5.9 秒；**同樣次數、同 seed 時結果跟 ChainDP 逐位元相同**；8 執行緒 ThreadSanitizer 0 警告 |
| 3 | 串列做不完排程時才用平行 SA | `annealer.cpp`（`saThreads`） | SA 只有 10 秒時，4 執行緒在 h03 / h04 / h05 好 1.8 / 0.4 / 0.4% | 預設 60 秒時選擇跟 v4 一樣 |
| 4 | **最大位移的搜尋換成組合**：row 重新分配 + row 內精確壓緊（新），失敗再用 ChainDP 的 site 修復 | `capsearch.cpp/.hpp`（新）、`global.cpp`（只用其中的 `repairCap`） | 最佳解是整個 row 重新分配；ChainDP 的子方法在固定 site 上一次動一顆 | toy 到精確取捨曲線上的 1,972.0（總位移 −0.05%） |
| 5 | **額外步驟（chain + 最大位移搜尋）的預算**：≤ 2000 cell 12 秒、其他 6 秒（ChainDP：chain 4 秒 + global 16 秒） | `main.cpp` | 同時間協定下 Stage 1 多 1 秒就少 1 秒 SA | 見下面的 HPWL 數據 |
| 6 | 三個視窗 DP（主 DP、chain 之後、最大位移搜尋）共用一個 memo | `main.cpp` | 搜尋中的 DP 只重解變動附近的視窗 | |

### 最大位移搜尋（`capsearch.cpp`）怎麼做

- **狀態**：每個 cell 在哪個 segment（row 被 macro 切開的一段）。
- **一個 segment 在上限 D 下排不排得下**：每顆 cell 的允許起點是一段區間（位移 ≤ D），問題是「有釋放時間和期限的單機排程」。
  用 Schrage 規則（在已可放的 cell 中挑期限最早的）、原順序（局部修補）、混合順序判斷，取 lateness 最小者；
  判定可行時一定真的排得下（測試跟窮舉比對，從不誤判），但可能漏掉可行的（是啟發式）。
- **可行之後的位置**：對選出的順序做精確的 L1 clumping（區間限制下的中位數），比較幾種順序取總位移最小者。
- **搜尋**：
  1. 第一段 SA：移一顆、換一顆、一顆寬的換一串窄的（e06 的 28-site cell 要這個），把總 lateness 降到 0；溫度依時間分三輪降溫。
  2. 清理：可行之後，把不必要離開原 row 的 cell 搬回去（e10：5,000 多步之後只剩 24 顆真的要換 row）。
  3. 第二段 SA（只在 ≤ 3000 cell）：維持可行、壓低每個 row 的精確 L1。
  4. 只重新擺放成員有變或有違規 cell 的 segment，其他保持原位置；之後跑視窗 DP 收回總位移。
- **上限怎麼選**：(A) 先用不跑 DP 的便宜探測，從 0.9 × 最大值開始二分，找到最低的可行上限；
  (B) 再從「目前最大值往可行下限走 25%」開始二分，每次修完跑 DP，總位移 ≤ 搜尋前 × 1.004 才接受。一輪沒有進步就換 seed 再試，連續 3 輪沒有進步才停。
- 每次修復失敗都只是「時間內沒找到」，不是不可行的證明；成功的結果一定合法、最大位移 ≤ D，而且整步有 `guarded` 退回。

## 預設參數（TA 會用的）

`dp INPUT_FILE OUTPUT_FILE`，其餘都是預設值：

| 選項 | 預設 | 說明 |
|---|---:|---|
| `--extra-time S` | 12（≤ 2000 cell）/ 6 | chain + 最大位移搜尋的總預算 |
| `--chain-time S` | 預算的 25%，最多 3 | ChainDP 的 ejection chain |
| `--cap-time S` | chain 用剩的 | 最大位移搜尋 |
| `--total-ratio R` | 1.004 | 最大位移搜尋可接受的總位移 / 搜尋前 |
| `--legacy-global` | 關 | 改用 ChainDP 的 global cap 修復（`--global-time 16`） |
| `--stage1-time` / `--time` / `--polish-time` | 90 / 60 / 30 | 跟 v4、ChainDP 一樣 |
| `--threads` | 自動（最多 8） | 串列做不完排程時才平行（≥ 8000 cell 一定平行，跟 v4 一樣） |

**「最大位移優先」的選項**：`--extra-time 16`（跟 ChainDP 的 global 一樣多）。調參集 11 筆上兩個條件都滿足 7/11（預設 6/11），
但同時間協定下 holdout 的 HPWL 從 −0.9% 退到 −0.1%，h06 差 2.0%（`Testcases/results/ClaudeOpus55_v5/tune/t8_e16`、`phase2/equal_v5_e16`）。

## 數據（演算法凍結後，2026-10-21）

原始紀錄：`Testcases/results/ClaudeOpus55_v5/final/`（`summary.md` / `summary.json` 由 `scripts/final_summary.py` 產生）。
ChainDP 的比較對象是第一階段在同一台機器重跑的 local_grade（`phase1/lg_chain`）；v4 是第一階段的預設執行（`phase1/ts_ClaudeOpus55_v4`）。

### 目標 1：HPWL 不因 Stage 1 變慢而變差（3 個 seed，各報平均與範圍）

三種實驗分開報，不能混在一起（`final/hpwl_*/summary.md` 有逐筆平均、[最小, 最大]、實際次數與秒數）：

| 實驗 | holdout：v5 ÷ v4 | edge：v5 ÷ v4 |
|---|---:|---:|
| **同樣總時間**（較慢的 Stage 1 + 12 秒；SA 上限 = 預算 − 自己的 Stage 1 − 2 秒；ChainDP 用的同一個協定） | **0.9907**（6 好 2 差） | **0.9876**（13 好 1 差 2 平） |
| 同樣 SA 次數（500 萬次、1 執行緒、共同起點 = v4 的 Stage 1、無 polish） | 0.9935（8 好） | 0.9931（13 好 1 差 2 平） |
| **預設參數**（SA 60 秒 + polish 30 秒，各自的 Stage 1；TA 會用的） | **0.9919**（7 好 1 差） | **0.9938**（12 好 2 差 2 平） |

- 同時間下比 v4 差的：h04 +0.80%（v5 231,276 [229,724, 233,512] vs v4 229,439 [228,951, 230,299]）、h06 +0.28%、e16 +0.14%。
  其餘全部較好，最多 e15 −4.3%、h08 −4.2%。**兩組的平均都比 v4 好，目標達成**（以平均論）。
- ChainDP 在同一個協定下是 holdout +1.53%、edge +1.44%。差別來自 SA 用滿時間、merge 變便宜（同樣上限下 v5 的 SA 平均用掉上限的 84%，v4 64%）、
  以及 Stage 1 的額外時間從 4 + 16 秒降到 6 秒（≤ 2000 cell 12 秒）。
- 預設參數下比 v4 差的：h04 +0.58%、e06 +0.71%（範圍重疊：v5 [931,851, 955,946] vs v4 [932,747, 943,401]）、e16 +0.004%；
  其餘較好，最多 e07 −2.5%、e15 −2.6%、h08 −2.5%。
- 同次數的結果跟 ChainDP 自己量的（−0.64% / −0.69%）一致：v5 的 move 跟 ChainDP 相同（多候選 swap），只改了同步方式，同次數同 seed 時結果逐位元相同。

### 目標 2：最大位移（逐筆 vs ChainDP；總位移最多 +0.5%）

| 分組 | PASS | 最大位移不比 ChainDP 差 | 總位移 ≤ ChainDP × 1.005 | 兩者都滿足 | 最大位移 ÷ ChainDP | 最大位移 ÷ v4 | 總位移 ÷ ChainDP |
|---|---:|---:|---:|---:|---:|---:|---:|
| testcases | 3/3 | 3/3 | 3/3 | **3/3** | 0.9881 | 0.9876 | 1.0015 |
| holdout | 8/8 | 3/8 | 8/8 | 3/8 | 1.0062 | 0.9740 | 0.9964 |
| edge | 16/16 | 11/16 | 14/16 | 9/16 | 0.9948 | 0.9405 | 1.0002 |
| **全部 27 筆** | **27/27** | 17/27 | 25/27 | **15/27** | **0.9976** | **0.9567** | **0.9992** |
| **新 seed 20261021（16 筆）** | **16/16** | 12/16 | 15/16 | **10/16** | **0.9889** | **0.9632** | **0.9993** |

（幾何平均，排除值為 0 的筆數。逐筆表在 `final/summary.md`。）

- **toy：1,972.0 / 43,236.6，正好是精確取捨曲線上的點**（ChainDP 2,007.9 / 43,261.6；精確最小值 1,938.4 要多 1.8% 總位移）。
- 比 ChainDP 好很多的：e06 28.25 vs 31.65（−10.8%）、e11 −6.2%、e05 −5.0%、h02 −2.0%、L2 12,381.6 vs 13,274.6（−6.7%，總位移還少 1.5%）。
- **沒達到「每一筆都不比 ChainDP 差」**：最大位移較差的 10 筆是 h01（+2.3%）、h04（+3.5%）、h06（+0.5%）、h07（+1.3%）、h08（+0.6%）、
  e03（+6.2%）、e07（+8.2%）、e10（+5.3%）、e12（+2.1%）、e15（+0.01%）。多半是「頂部很平」的中型設計：DP 之後很多 cell 正好停在上限上，
  每降一點都要搬動很多 cell，每次嘗試要一次 1~5 秒的 DP，6 秒的預算只夠試 2~4 次；ChainDP 在這些筆用了 14~17 秒。
- 總位移超過 +0.5% 的 2 筆：e08（+1.03%）、e16（+0.53%）。e08 是在最大位移搜尋**之前**就多了 0.58%：v5 的 chain 只有 1.5 秒（ChainDP 4 秒）。
- h01 離精確最小值 26.5 還有 13.5%（ChainDP 10.9%）；在總位移 ≤ ChainDP + 0.5% 的限制下，精確解顯示最大位移不可能低於 27.5。

### 目標 3：穩健

| 項目 | 結果 |
|---|---|
| testcases + holdout + edge 27 筆（local_grade） | **27/27 PASS**，合計 1,217 秒（v4 975、ChainDP 1,254） |
| 新 seed 16 筆（凍結後才用 `gen_edge.py --seed 20261021` 產生，numpy 2.4.6） | **16/16 PASS**（v4、ChainDP 也都 16/16） |
| L1（20 萬 cell） | **PASS，186.3 秒**（< 4 分鐘）；SA 用滿 60 秒、7,490 萬次（v4 3,530 萬）；HPWL 1.735e9（v4 1.951e9、ChainDP 1.775e9） |
| L2（100 萬 cell） | **PASS，215.8 秒**（< 10 分鐘）；最大位移 12,381.6（v4 13,572.9、ChainDP 13,274.6）；HPWL 1.670e9（v4 1.833e9、ChainDP 1.790e9） |
| 測試 | 8 套 C++ 測試（新增 `capsearch_test`）一般編譯與 ASan / UBSan 都通過；平行 SA 用 ThreadSanitizer（8 執行緒、2,700 輪同步）0 警告 |
| 出錯時 | 每個可選步驟（pareto、DP、chain、最大位移搜尋、SA、polish）都包在 `guarded` 裡：丟例外或破壞不變量就退回前一步的結果、印 warning、繼續；最大位移搜尋失敗不改動輸入 |

L1 的最大位移 50,415 比 ChainDP 的 local_grade（50,362）差 0.1%：L1 的合法化碰到 90 秒的預算，結果本來就會隨執行而變（ChainDP 自己兩次 50,362 / 51,123），
而最大位移搜尋在 L1 上 4 次探測都沒找到可行解。

## 試過沒用的

| 試過的 | 結果 | 紀錄 |
|---|---|---|
| ChainDP 的 global 修復（四個子方法）給 10 倍時間 | toy、h01 還是全部失敗：協商 20 萬輪不收斂、DFS 1 秒找不到 | `diagnosis.md` 3.3 |
| row 重新分配，row 內依理想 x 排序、單顆移動 + tabu | toy 失敗（最寬的 cell 要排最後才放得下） | — |
| row 內用 EDF（期限最早）排序 | 連精確解的 row 都判成來不及（第一顆應該是最早能開始的）→ 改 Schrage | — |
| 第一段 SA 以次數降溫 | 長 row 的設計每秒只做幾萬步，永遠降不了溫（e10 2 秒 3.4 萬步）→ 依時間降溫 | — |
| 沒有「一換多」的 move | e06 卡在一顆 28-site 的 cell（lateness 0.834 不動，10 秒 2,200 萬步） | — |
| 沒有清理步驟 | e10 重新擺放後總位移 +34%（第一段 SA 在高溫時亂動 5,000 多步） | — |
| 大設計也跑第二段 SA | h06（上限 430.7）：DP 之後 +0.38%，不跑 −0.09%；上限 428：+0.98% vs +0.31%；而且慢 100 倍（程式註解裡的 +0.45% 是較早的版本量的） | `Testcases/results/ClaudeOpus55_v5/proto/h06_methods.txt`（`tests/compare_methods.cpp`） |
| 總位移那一段從最低可行上限開始 | e06 在 23.76 被總位移擋掉（+1.7%），時間就用完了 → 從 25% 處開始 | — |
| 那一段被擋掉後改「只修最差 k 顆」（k = 1, 2, 4…） | 每步太小、每步又要一次 DP：調參集 5/11（不改 6/11） | `tune/t5_e6_r1005` |
| 額外預算跟 Stage 1 時間成正比（20%） | h08 給到 12 秒；同時間協定下這就是 ChainDP 落後的原因 → 固定預算 | — |
| 額外預算 16 秒 | 最大位移 7/11，但同時間 HPWL 幾乎沒有餘裕（h06 +2.0%）→ 做成選項 | `tune/t8_e16`、`phase2/equal_v5_e16` |
| 總位移比例 1.01（ChainDP 的值） | 最大位移 7/11 不輸，但 toy、h03 的總位移超過 ChainDP + 0.5% → 1.004 | `tune/t4_e6_r101` |
| 4 執行緒的 SA 用在 60 秒（< 8000 cell） | h04 −1.15%、h05 +0.56%，不一致 → 只在做不完排程時用 | `tune/sa_threads_t60` |
| 原順序的局部修補、最小擾動的插入 | 頂部平的設計（h06 一個 segment 有 4~9 顆違規）照樣要整段重排，重新擺放後的總位移沒有變好；保留（無害） | — |

## 還沒做到的

- **最大位移：每一筆都不比 ChainDP 差 → 沒做到**（27 筆 15 筆兩條件都滿足、新 seed 10/16）。在 6 秒內，頂部很平的中型設計搜尋不夠；
  `--extra-time 16` 在調參集上 7/11，但同時間 HPWL 的餘裕幾乎沒了。要兩者兼得，最大位移搜尋每次嘗試的 DP 要更便宜
  （例如只在違規 cell 附近做 DP，或改成在上限 D 下直接做「有上限的視窗 DP」當修復方法）。
- **精確最小值**：toy 停在取捨曲線上 1,972（總位移不增加的最好點），沒去 1,938.4；h01 沒有接近 26.5。
- **ISPD'17 流量式合法化 / 對 D 二分加 bin 之間 max-flow**：沒做。用 row 層級的排程判斷取代：bin 流量是連續鬆弛，判斷可行之後還要在 bin 內合法化；
  row 的排程判斷給出的是可以直接放的位置。
- **平行 SA 的直條 strip 與橫 band 輪流**：沒做；改做了 merge（佔 25~37% 的部分）。每個執行緒仍各有一份完整的座標與 net box（只是每輪只複製變動的部分）。
- **L2 的 SA**：proposal 62% 失敗、只用 2 個執行緒；v5 的 SA 降 18.6%（v4 11%），之後 polish 又降 22%，SA 的效率仍低；時間分配沒改。
- **Stage 1 的結果仍會隨執行而變**：合法化（大設計）和額外步驟都用牆鐘時間預算。
- **chain 縮短的代價**：chain 只拿 25% 的額外預算，e08 在最大位移搜尋之前的總位移就比 ChainDP 高 0.58%。

## 編譯、測試與重現

```bash
cmake -S . -B build && cmake --build build      # 或 make
./build/dp INPUT_FILE OUTPUT_FILE [options]
python3 tests/run_tests.py [--sanitize]          # 8 套 C++ 測試（含 capsearch_test：合法、≤ D、失敗不改動、跟窮舉比對不誤判）
```

所有實驗腳本在 `Testcases/results/ClaudeOpus55_v5/scripts/`（從 `Lab3/` 執行）；三個量測用的 C++ 工具
`tests/sa_bench2.cpp`（從同一個合法起點跑 SA，可傳 SA 選項）、`tests/compare_methods.cpp` + `readpl.cpp`（兩種修復方法在同一上限下的比較）
是演算法凍結**之後**才從 scratch 搬進來的，`frozen_sources.sha256` 不含它們。

```bash
R=Testcases/results/ClaudeOpus55_v5
$R/scripts/phase1_runs.sh                 # 第一階段量測（diagnosis.md）
python3 $R/scripts/tune_stage1.py BIN NAME --dp-args="..."    # 只用 testcases + holdout 調參
$R/scripts/phase2_final.sh                # 凍結後的全部驗證（local_grade、edge、L1、L2、HPWL 三種、新 seed）
python3 $R/scripts/final_summary.py       # → final/summary.md、summary.json
```

## 參考資料

- L. Schrage, "Solving resource-constrained network problems by implicit enumeration — nonpreemptive case," Operations Research 1970
  （Schrage 規則：有釋放時間的單機排程，非延遲的最早期限優先）
- 其他（Abacus、ISPD'17 flow legalization、Kravitz & Rutenbar 的平行 SA、ABCDPlace 等）見 `../COMPARISON.md` 與 `../CodexChainDP/README.md`
