# Testcases — 所有測資和所有跑出來的結果

Lab3 的規則：**測資放這裡，任何程式跑出來的東西也放這裡（`results/`）**，原始碼資料夾裡不放結果（見 `../README.md`）。

測資資料夾不放進 git（`testcases/` 從 `bench.tgz` 解出來，`ibm-place2/` 從網路下載，`large/` 是程式產生的）。clone 下來後先跑：

```bash
./make_testcases.sh            # testcases/ + ibm-place2/
./make_testcases.sh --large    # 再加 large/（L1、L2）
```

> **量測一律一次只跑一個 `dp`，比較版本只用 ibm01、ibm05、L1、L2**（2026-10-30 起）：同時跑兩個會搶 L3 快取 / 記憶體頻寬，在期限下品質差到 70% 以上（L1）。
> 原因、受影響的結果與可信的數字：[../README.md「量測規則」](../README.md#量測規則一次只跑一個-dp2026-10-30-起)。

**測資有三組**（產生器 `Grader/tools/gen_bench.py`、`gen_edge.py` 可以再產生其他合成測資）：

| 資料夾 | 內容 | 來源 |
|---|---|---|
| `testcases/` | 課程的 3 筆：toy（31 cells）、ibm01（12,028）、ibm05（28,146） | 解壓 `bench.tgz` |
| `large/` | 規模測試 L1（200,000 cells）、L2（1,000,000 cells），有熱點、macro、split row | `./make_testcases.sh --large`（約 1 分鐘、160 MB） |
| `ibm-place2/` | **IBM-PLACE 2.0**（UCLA，2002）原樣下載、原樣解壓，沒有改任何東西：8 個電路 × 2 種使用率 = 16 筆 | `./make_testcases.sh`（約 60 MB 下載、連解壓共 200 MB） |

`testcases/`、`large/` 每筆是一個資料夾 `X/X.{aux,nodes,nets,wts,pl,scl}`，執行方式：`dp testcases/ibm01/ibm01 results/manual/ibm01`。

## ibm-place2：TA 說隱藏測資從這組改出來

10/28 規定的 HINT：「The test cases will be derived from the benchmarks at
https://vlsicad.eecs.umich.edu/BK/Slots/cache/er.cs.ucla.edu/benchmarks/ibm-place2/ … They will be quite close to the actual test cases.」
這裡放的是**網站上的原檔**（`ibm-place2/download/`）和 Bookshelf 版解壓的結果：

| 路徑 | 內容 |
|---|---|
| `ibm01/` … `ibm12/`（沒有 03–06） | 每個電路一個資料夾：共用的 `ibmXX.nodes/.nets/.wts`，加上兩種核心使用率各一組 `ibmXX-cuPP.{aux,pl,scl}`（ibm01 是 85% / 88%、ibm12 是 85% / 90%，其他 90% / 95%） |
| `nopad/ibmXX/` | 同樣的電路，拿掉 I/O pad 的版本（`ibm-place2-all-bookshelf-nopad.tar.gz`） |
| `download/` | 網站上的所有檔案：`bookshelf/`、`LEFDEF/` 的 tar.gz、Dragon 的放置結果 `pl-files-dragon.tar.gz`、`readme-*.html`、`stats.gif`、`comparison.gif` |

| 電路 | 可移動 cell | terminal（I/O pad） |
|---|---:|---:|
| ibm01 | 12,028 | 246 |
| ibm02 | 19,062 | 259 |
| ibm07 | 44,811 | 287 |
| ibm08 | 50,672 | 286 |
| ibm09 | 51,382 | 285 |
| ibm10 | 66,762 | 744 |
| ibm11 | 68,046 | 406 |
| ibm12 | 68,735 | 637 |

要注意的地方：

- **原始 `.pl` 沒有 global placement**：所有 cell 都在 (0, 0)，只有 I/O pad 有位置（網站說明：「Placement file containing I/O pad locations」）。
  拿它直接跑 `dp` 等於從一個點開始合法化，位移的數字沒有意義，只能測「跑不跑得完、合不合法、多快」。
  課程給的 ibm01 cell 數和這裡的 ibm01 一樣，但每個 cell 都有 global placement 的位置，所以 TA「further modified」的版本應該也會先有 global placement。
- 檔名：輸入的 base name 是 `ibmXX-cuPP`（例如 `dp ibm-place2/ibm01/ibm01-cu85 results/manual/ibm01-cu85`），`.aux` 裡寫的 `.nodes` 是共用的 `ibmXX.nodes`。
- 網站說 I/O pad 都是「dangling」（沒有連到 standard cell）。
- 最大的是 ibm12（約 6.9 萬 cell），比 L1（20 萬）小；L2（100 萬）遠大於任何一個。

## results/：跑出來的結果

| 資料夾 | 哪裡產生的 | 內容 | 進 git |
|---|---|---|---|
| `local_grade/<學號>/` | `Grader/tools/local_grade.py`（預設輸出） | 打包的 tgz、每筆的輸出、`record.csv`、`logs/` | 否 |
| `manual/` | 自己手動跑 | 隨意 | 否 |
| `report/` | `Report/run_everything.sh`（`scripts/run_experiments.py`） | 你的程式的實驗原始結果（每次一個 `.json`、SA trace）；表格和圖在 `Report/` | 紀錄 |
| `reference_report/` | `Report/run_everything.sh reference` | 參考解 v1 的全部實驗（`main/`、`stage1/`、`sa/`、`effort/`、`seeds/`；`holdout/` 是刪除 holdout 測資前跑的）；範例報告在 `AI_Workspace/reference_report/` | 紀錄 |
| `step2_grader/` | `Grader/step2_grader/run_demo.py` | 檢查器示範：`inputs/`（h01 的副本）、`GOOD/`、`BAD_*/`、`record.csv` | 紀錄（重跑 `run_demo.py` 會重建全部） |
| `MyCode/` | `run_versions.py`、`extra/*.sh` | `MyCode/` 五個版本的量測（`records.csv`、`records_ibm-place2.csv`） | 紀錄 |
| `machine_lock.py` | | 量測腳本共用的機器鎖（`machine_lock()`） | 是 |

「進 git」那一欄：**是** = 全部；**紀錄** = 只有小的紀錄檔，**否** = 都不進。`results/` 底下的規則（`.gitignore`）：

- 追蹤：`.csv`（每次的 SA trace 除外）、`.md`、`.json`、`.log`、`.txt`（stdout / stderr）、`.png`、`.py`、`.sh`，全部約 4 MB。
- 不追蹤（只在本機）：placement 輸出 `.pl`、每次的 SA trace（`trace.csv`、`*.trace.csv`、`*.polish.csv`）、執行檔和 `.o`、
  `home/`（local_grade 解壓的套件）、`submissions/`（tgz）、dp 複製到輸出旁邊的 `.aux/.nodes/.nets/.wts/.scl`。
- `reference_report/main/*_trace.csv` 會進（報告的 SA 曲線圖要用）。
- 量測腳本共用的機器鎖：`results/timing_global.lock`（不進 git）。
- 要查數字：先看該實驗的 `records.csv` / `record.csv`，不要一個一個打開 `.pl`。

## large：為什麼要測 20 萬 / 100 萬個 cell

`Grading Criteria.pdf` 雖然是 Project 2 的評分方式，但看得出 TA 的做法：用課程沒給的隱藏測資（從比賽測資簡化而來），
程式跑不完或當掉就是 0 分，「你應該知道自己演算法的複雜度」。Project 3 的格式說明引用的是 ISPD 2006 測資的 Bookshelf 文件，
ISPD 2005 / 2006 那兩組 placement 測資大約有 21 萬到 250 萬個物件。隱藏測資會多大不知道，所以加了 L1、L2，
檢查程式能不能在合理時間內跑完（結果見 `results/MyCode/records.csv`）。

| | cells | nets | rows × sites | 使用率 | macros | pads | 熱點 | 座標 |
|---|---:|---:|---|---:|---:|---:|---:|---|
| L1 | 200,000 | 204,336 | 481 × 3,662 | 0.79 | 12 | 1,000 | 4 | ibm（site 66、row 504） |
| L2 | 1,000,000 | 1,034,574 | 794 × 12,680 | 0.70 | 20 | 2,000 | 6 | unit（site 1、row 16） |
