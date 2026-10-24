# Testcases — 所有測資和所有跑出來的結果

Lab3 的規則：**測資放這裡，任何程式跑出來的東西也放這裡（`results/`）**，原始碼資料夾裡不放結果（見 `../README.md`）。

測資資料夾不放進 git（`testcases/` 從 `bench.tgz` 解出來，`holdout/`、`large/` 是程式產生的）。clone 下來後先跑：

```bash
./make_testcases.sh
```

| 資料夾 | 內容 | 產生方式 |
|---|---|---|
| `testcases/` | 課程的 3 筆：toy（31 cells）、ibm01（12,028）、ibm05（28,146） | 解壓 `bench.tgz` |
| `holdout/` | 8 筆報告 / 穩健度測試用的新測資 h01–h08（300 ~ 25,000 cells） | `Grader/tools/gen_bench.py --suite`（seed 2026） |
| `edge/` | **過擬合檢查** e01–e16：現有測資沒測到的情況（下面有表） | `Grader/tools/gen_edge.py`（5 秒、15 MB） |
| `edge_seed2/` | 同樣 16 種情況、換一個 seed（`--seed 20261020`），CodexChainDP 演算法定下來之後才產生的驗證組 | `Grader/tools/gen_edge.py`（numpy 2.4.6 才會跟量過的一樣；2.5.3 的 e04 不同） |
| `edge_seed3/` | 同樣 16 種情況、再換一個 seed（`--seed 20261021`），ClaudeOpus55_v5 演算法凍結之後才產生的驗證組 | `Grader/tools/gen_edge.py`（numpy 2.4.6） |
| `edge_seed7/` | 同樣 16 種情況、再換一個 seed（`--seed 20261025`），ClaudeOpus55_v7 凍結之後才產生的驗證組 | `Grader/tools/gen_edge.py`（由 `results/ClaudeOpus55_v7/scripts/final_validate_v7.py` 產生） |
| `large/` | 規模測試 L1（200,000 cells）、L2（1,000,000 cells），有熱點、macro、split row | `./make_testcases.sh --large`（約 1 分鐘、160 MB） |

每筆是一個資料夾 `X/X.{aux,nodes,nets,wts,pl,scl}`，執行方式：`dp testcases/ibm01/ibm01 results/manual/ibm01`。

## edge：現有測資沒測到的情況（過擬合檢查）

課程 3 筆 + holdout 8 筆的 global placement 都是「隨機 + 熱點」、格式也都很整齊；各版本的參數是在這 11 筆上調的，
可能只是剛好適合它們。`edge/` 的 16 筆專門測**沒出現過的情況**。每一筆都是先排出一個**保證合法**的 placement，
再從它推出 global placement，所以一定有解；`gen_edge.py --check` 會把那個合法解存成 `NAME_ref.pl`
並用檢查器驗證（也可以拿來當比較的基準：總位移比它差就是做得不好）。

| 測資 | cells | 測什麼 |
|---|---:|---|
| e01_nearlegal | 9,719 | 像真的 global placer 的輸出：合法解只抖動 ±1.5 site、±0.25 row → 理想的位移很小 |
| e02_legalgp | 4,562 | global placement **本來就合法** → 理想的 Stage 1 一個都不動（總位移 0） |
| e03_dense97 | 5,872 | 可用 site 的 97% 都要用掉，幾乎沒有空隙 |
| e04_sparse10 | 1,404 | 使用率 10%，global placement 完全隨機（沒有結構） |
| e05_bignets | 11,524 | 超大 net（2000、800、300 pins，像 clock / reset）＋ 50 個 50-pin net |
| e06_tallnarrow | 4,022 | 800 個 row、每 row 只有 40 sites，最寬的 cell 佔 row 的 70% |
| e07_tworows | 2,719 | 只有 2 個 row、每 row 12,000 sites，幾乎不能上下移 |
| e08_fragmented | 7,181 | 40 個 macro 把 row 切成很多小段，有些比最寬的 cell 還窄；有些 cell 的起點在 macro 裡 |
| e09_offgrid | 8,350 | 每個 row 的起點錯開不同的「零點幾個 site」（沒有共同的 site 格子）；site 0.19、row 1.71；5% 的 cell 起點在晶片外很遠 |
| e10_mixedsites | 10,442 | row 的 site 寬度 2、3 交替（Bookshelf 允許每個 row 不一樣） |
| e11_format | 1,988 | 格式：CRLF 換行、中間有註解、pin offset、`B` pin、同一個 pin 列兩次、1-pin net、沒有 net 的 cell、名字有 `/ [ ] .`、FS / FN 的 cell、`Siteorient : N`、`Numsites`、ISPD 2006 的 `terminal_NI` + `/FIXED_NI`、沒有 `/FIXED` 的 pad |
| e12_collapse | 3,623 | 70% 的 cell 起點**完全相同**（只有 3 個點） |
| e13_onecell | 1 | 只有一個 cell、一個 row |
| e14_fixedonly | 0 | 沒有可移動的 cell |
| e15_realistic | 31,889 | 最接近真實的：合法解加上平滑的密度扭曲（局部擠 20 ~ 30%）、幾個大 net、macro、400 個 I/O pad、IBM 座標 |
| e16_rowgaps | 8,299 | row 之間**不等距**（每幾個 row 隔 0.5 ~ 3 個 row 高），而且 `.scl` 裡 row 的順序是亂的 |

跑全部版本：`Testcases/results/run_edge.sh ClaudeOpus55_v4 CodexWindowDP …`（紀錄在 `results/edge/<版本>/`）。

## results/：跑出來的結果

| 資料夾 | 哪裡產生的 | 內容 | 進 git |
|---|---|---|---|
| `local_grade/<學號>/` | `Grader/tools/local_grade.py`（預設輸出） | 打包的 tgz、每筆的輸出、`record.csv`、`logs/` | 否 |
| `manual/` | 自己手動跑 | 隨意 | 否 |
| `report/` | `Report/run_everything.sh`（`scripts/run_experiments.py`） | 你的程式的實驗原始結果（每次一個 `.json`、SA trace）；表格和圖在 `Report/` | 紀錄 |
| `reference_report/` | `Report/run_everything.sh reference` | 參考解 v1 的全部實驗（`main/`、`holdout/`、`stage1/`、`sa/`、`effort/`、`seeds/`）；範例報告在 `AI_Workspace/reference_report/` | 紀錄 |
| `step2_grader/` | `Grader/step2_grader/run_demo.py` | 檢查器示範：`inputs/`（h01 的副本）、`GOOD/`、`BAD_*/`、`record.csv` | 紀錄（重跑 `run_demo.py` 會重建全部） |
| `CodexWindowDP/` | `tests/compare.py`、`summarize.py`、`plot_results.py`、`full_bound.py` | `comparison/`（6 個版本 × 11 筆）、`multiseed/`、`bounds/`、`figures/`、`verification.txt` | 紀錄 |
| `CodexChainDP/` | `tests/acceptance.py`、`final_validation.py`、`fair_time.py`、`measure.py`、`compare.py` | 驗收（`acceptance.json`、`summary.json`、`default_comparison.csv`）、同時間 / 同次數的 HPWL 比較、`edge_seed2/`、`exact_max/`、`large_L1/`，說明在 `AI_Solutions/CodexChainDP/README.md` | 紀錄 |
| `ClaudeOpus55_v5/` | `scripts/`（`phase1_runs.sh`、`run_ts.py`、`tune_stage1.py`、`hpwl_compare.py`、`phase2_final.sh`、`final_summary.py` 等） | `diagnosis.md`（第一階段診斷）、`phase1/`（量測）、`exact/`（toy / h01 精確取捨曲線）、`tune/`（只用 testcases + holdout 的調參）、`final/`（凍結後的驗收、HPWL 三種比較、新 seed），說明在 `AI_Solutions/ClaudeOpus55_v5/README.md` | 紀錄 |
| `ClaudeOpus55_v7/` | `scripts/`（`phase1_queue.py`、`phase1_analyze.py`、`phase2_train.py`、`phase2_large.py`、`final_validate_v7.py`、`final_report_v7.py` 等） | `diagnosis.md`（第一階段診斷）、`phase1*/`（量測）、`phase2/`（訓練、大設計、測試）、`final/`（最終驗證，`summary.md`）、`provenance/`（保護資料夾與文件的 sha256） | 紀錄 |
| `edge/<版本>/` | `results/run_edge.sh` | 各版本在 edge 測資的 `record.csv`、`summary.csv`、`run.log`；`README.md` 是比較和分析（完整輸出在 `local_grade/edge_<版本>/`） | 紀錄 |

「進 git」那一欄：**是** = 全部；**紀錄** = 只有小的紀錄檔，**否** = 都不進。`results/` 底下的規則（`.gitignore`）：

- 追蹤：`.csv`（每次的 SA trace 除外）、`.md`、`.json`、`.log`、`.txt`（stdout / stderr）、`.png`、`.py`、`.sh`，全部約 4 MB。
- 不追蹤（只在本機）：placement 輸出 `.pl`、每次的 SA trace（`trace.csv`、`*.trace.csv`、`*.polish.csv`）、執行檔和 `.o`、
  `home/`（local_grade 解壓的套件）、`submissions/`（tgz）、dp 複製到輸出旁邊的 `.aux/.nodes/.nets/.wts/.scl`。
- `reference_report/main/*_trace.csv` 會進（報告的 SA 曲線圖要用）。
- 已移除版本（v2、v3、CodexGapSA、CodexParetoDP、CodexAugmentDP）的結果資料夾一起刪了；它們在 edge 的 `record.csv` 保留，
  `edge/README.md` 和 `AI_Solutions/COMPARISON.md` 的數字仍有效。
- 要查數字：先看該實驗的 `records.csv` / `record.csv`，不要一個一個打開 `.pl`。
- `AI_Solutions/COMPARISON.md`（七個版本的比較）當時的原始紀錄放在暫存資料夾，已經清掉了；
  重跑的指令在那份文件的「重現」一節，輸出會到 `results/local_grade/<版本>/`。

## holdout 有什麼課程測資沒有的東西

| | cells | 使用率* | macros | pads | 熱點 | split rows | 座標 |
|---|---:|---:|---:|---:|---:|---:|---|
| h01 | 300 | 0.48 | 2 | 16 | 1 | 20% | site 1、row 16 |
| h02 | 1,000 | 0.67 | 3 | 40 | 1 | 30% | site 66、row 504、原點為負 |
| h03 | 2,000 | 0.80 | 0 | 0 | 2 | 0% | site 0.2、row 1.6（小數座標） |
| h04 | 4,000 | 0.83 | 4 | 80 | 2 | 20% | site 1、row 16 |
| h05 | 6,000 | 0.58 | 6 | 120 | 0 | 50% | site 66、row 504 |
| h06 | 10,000 | 0.87 | 2 | 160 | 3 | 10% | site 1、row 16 |
| h07 | 15,000 | 0.74 | 5 | 200 | 2 | 20% | site 0.2、row 1.6 |
| h08 | 25,000 | 0.81 | 3 | 300 | 2 | 10% | site 66、row 504 |

\* cell 面積 ÷ 全部 row 面積（macro 底下的 row 也算在內，所以扣掉 macro 之後實際更擠）。

- **macro**：fixed、跨好幾個 row、有的沒有對齊 row → row 要被切成好幾個 segment
- **pad**：晶片外 1×1 的 terminal，`.pl` 沒有 `/FIXED`（跟 ibm05 一樣，要看 `.nodes` 的 `terminal`）
- **split row**：一個 row 有兩個 `SubrowOrigin`，中間有缺口
- 關鍵字大小寫（`NumSites` / `Numsites`）、net 有沒有名字，每筆不一樣

global placement 是合成的（隨機 + 熱點），沒有經過真正的 placer 最佳化，所以 SA 降 HPWL 的幅度會比 ibm 大很多，報告引用時要說明。

## large：為什麼要測 20 萬 / 100 萬個 cell

`Grading Criteria.pdf` 雖然是 Project 2 的評分方式，但看得出 TA 的做法：用課程沒給的隱藏測資（從比賽測資簡化而來），
程式跑不完或當掉就是 0 分，「你應該知道自己演算法的複雜度」。Project 3 的格式說明引用的是 ISPD 2006 測資的 Bookshelf 文件，
ISPD 2005 / 2006 那兩組 placement 測資大約有 21 萬到 250 萬個物件。隱藏測資會多大不知道，所以加了 L1、L2，
檢查每個版本能不能在合理時間內跑完（結果見 `AI_Solutions/COMPARISON.md`）。

| | cells | nets | rows × sites | 使用率 | macros | pads | 熱點 | 座標 |
|---|---:|---:|---|---:|---:|---:|---:|---|
| L1 | 200,000 | 204,336 | 481 × 3,662 | 0.79 | 12 | 1,000 | 4 | ibm（site 66、row 504） |
| L2 | 1,000,000 | 1,034,574 | 794 × 12,680 | 0.70 | 20 | 2,000 | 6 | unit（site 1、row 16） |
