# CBI — Clock Buffer Insertion (Project 2)

## Build

```bash
cmake -S . -B build
cmake --build build
```

會產生執行檔 `build/cbi`。只用 C++17 標準函式庫（包含 `std::thread`，CMake 透過 `find_package(Threads)` 連結 pthread），
CMakeLists.txt 預設 `Release`（-O3）。

## Run

```bash
./build/cbi INPUT_FILE OUTPUT_FILE [-t seconds] [-s seed] [-v]
# 例：
./build/cbi inputs/input001.cbi results/output001.cbi
```

- 前兩個參數一定是 input / output 路徑（支援相對路徑）；參數不足時印出錯誤訊息並回傳 1。
- stdout 只印一行：`T_max: X, T_min: Y, Score: Z`；各階段統計印在 stderr。
- `-t` 總時間上限（秒）。預設：sink ≤ 12 個時 5 秒，其他 `min(30, 10 + #sinks/100)`。
  題目只依 Score 排名、每筆上限 60 秒；時間以牆上時鐘控制，所以在較慢的機器上也會準時結束。
- `-s` 亂數種子（預設 1）；`-v` 印出最佳化細節。
- 使用 `min(2, CPU 核心數)` 條執行緒（評分 VM 是 `qemu -smp 2`）；單核心機器自動退回單執行緒。

## Algorithm

| Stage | 檔案 | 作法 |
|---|---|---|
| 1. 建構 | `construct.cpp`, `placement.cpp` | 由下往上遞迴二分分群，每群放一個 buffer（讓 children latency 對齊）；合不起來時插 relay 往 SRC 靠近。兩種收尾都做。 |
| 2. 平衡 | `balance.cpp` | 在「整棵子樹都太早到」的節點上方插繞路 buffer；掃描目標 skew 取 `cost + w·skew` 估計最小的方案。 |
| 3. 平行退火 | `optimizer.cpp`, `parallel.cpp`, `main.cpp` | 11 種 move 的模擬退火（全程 transaction + rollback，保證合法），2 條執行緒平行搜尋（見下）。 |

每個階段結束都用獨立的 `evaluate()` 從頭驗證，驗證失敗就退回上一階段的解。

### 相對於 `M11502102_ClaudeOpus55Ultracode` 的改進

1. **平行搜尋（2 條執行緒）**
   - **大測資、buffer 線長相對晶片短**（N ≥ 1500 且最大 L ≤ 0.65 × 晶片邊長）→ **分工**（`parallel.cpp`）：
     從 SRC 往下展開 sink 最多的節點，直到每棵子樹 ≤ N/3 個 sink；這些子樹根依 x 或 y 排序、按 sink 數分成兩組。
     兩條執行緒各拿一份副本，只能改自己那組子樹的結構（用 transaction 的 journal 檢查，越界的 move 直接 rollback），
     子樹根與上層凍結，所以兩邊不會同時改到同一個 buffer 的線長。每個 epoch 結束把兩半合併（第 1 組整棵子樹搬過來、
     新 buffer 重新編號），合併後重新驗證。這是真正的兩倍搜尋量。
     - 奇數 epoch 與最後 30% 的低溫收尾改成兩條鏈都看整棵樹：上層被凍結時 skew 修不好（實驗發現只分工的話 w_skew 大的測資會變差）。
     - 分工時每條鏈的 skew 類 move 追蹤「自己區域內」的關鍵路徑（全域關鍵路徑常在另一邊，move 會被拒絕）。
   - **其他測資** → 兩條鏈用不同 seed 從同一個解出發，每 epoch 取較好的那條繼續。
2. **降溫曲線不中斷**：平行同步時，下一段接著上一段的溫度繼續降，不會每次同步都重新加熱
   （第一版做法是每次同步都從 T0 重來，在大測資上完全沒有幫助）。
3. 小幅加速：`medianPoint` 等熱點改用每條執行緒自己的暫存陣列、Dissolve 的「是否接在 SRC 底下」查詢加快取（單執行緒約快 2～5%）。

### 實驗結果（各自預設設定；Score 比值的幾何平均，越低越好）

最後驗證用的三組測資：holdout2（24 筆）、holdout3（24 筆，最後才產生、開發時沒看過）、testcases（15 筆）。
多執行緒版本以 5 個平行 job 跑（共 10 條執行緒），單執行緒版本以 10 個平行 job 跑，讓 CPU 負載相同。

| 類別 | 筆數 | Ultracode_2 / Ultracode | Ultracode_2 / Chatgpt6AstraMedium |
|---|---:|---|---|
| 全部 | 63 | **0.9941**（35 好 / 13 差 / 15 同） | **0.9939**（36 好 / 16 差 / 11 同） |
| N ≥ 1500、線長短（使用分工） | 8 | **0.9865**（8 好 / 0 差） | 0.9894（7 好 / 1 差） |
| N ≥ 1500、線長長 | 20 | 0.9970 | 0.9986（約持平） |
| N < 1500 | 35 | 0.9941 | 0.9923 |

另外（單獨執行，各 30 秒）：

| 測資 | Ultracode | Ultracode_2 |
|---|---:|---:|
| 2 萬 sinks | 58757 | **57958** |
| 5 萬 sinks（密集） | 141704 | **140844** |
| 10 萬 sinks | 302769 | **299803**（30.2 秒、129 MB） |

- 300 筆極端測資：2 執行緒與強制 1 執行緒都全部 VALID
- 本機完整評分流程（zip → cmake → 每筆限時 60 秒 → 課程 verifier）15 筆全部 VALID

**結論**：小、中型測資在預設時間內本來就已經收斂，各版本差異在 ±0.5% 雜訊內；
主要進步在「大、線長短」的測資（約 1.4%，8 筆全勝），整體平均約 0.6%。

### 試過但沒有採用的做法（都有實驗數據）

- 兩條鏈「各跑各的、每 5 秒取較好的」+ 每次重新加熱：大測資沒有幫助（1.00x）
- 分工用在 N ≥ 300：中型測資變差（凍結的上層佔太大比例，例如 300 sinks 的測資 2260 → 2594）
- 分工時限制自己區域的 arrival 範圍不得擴張：沒有可量測的差異
- 第二條鏈用不同起始溫度（×3 / ×0.3）：雜訊內
- 預設時間拉長到 40 秒：只好 0.2%，維持 30 秒上限

### 實驗用環境變數（不影響正式執行）

`CBI_THREADS`、`CBI_SYNC`、`CBI_DECOMP_N`、`CBI_DECOMP_L`、`CBI_DECOMP_UNTIL`、`CBI_RUNS`、`CBI_W`、`CBI_T0F`、`CBI_T1F`、`CBI_BETA`、`CBI_FG`。

## Source files

```
src/
  main.cpp           參數、階段流程、平行退火的 epoch / 同步 / 合併
  parallel.h/.cpp    分工：切割子樹（makeCut）與合併（mergeCut）
  problem.h/.cpp     讀檔、buffer 型號查表 TypeTable
  tree.h/.cpp        樹結構：增量 lo/hi、transaction / rollback、區域檢查、snapshot、bitmap 佔用表
  placement.h/.cpp   buffer 位置搜尋
  construct.h/.cpp   Stage 1
  balance.h/.cpp     Stage 2
  optimizer.h/.cpp   Stage 3 的退火（11 種 move）
  solution_io.h/.cpp 獨立驗證與寫檔
```
