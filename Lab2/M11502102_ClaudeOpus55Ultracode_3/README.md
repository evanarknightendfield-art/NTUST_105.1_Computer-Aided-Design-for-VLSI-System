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
- `-t` 總時間上限（秒）。預設：sink ≤ 12 個 5 秒；N < 1500 為 `min(30, 10 + N/100)`；
  **N ≥ 1500 為 `min(45, 10 + N/100)`**。時間以牆上時鐘控制；10 萬 sinks 實測 45.2 秒。
- `-s` 亂數種子（預設 1）；`-v` 印出最佳化細節。
- 使用 `min(2, CPU 核心數)` 條執行緒（評分 VM 是 `qemu -smp 2`）；單核心機器自動退回單執行緒。

## Algorithm

| Stage | 檔案 | 作法 |
|---|---|---|
| 1. 建構 | `construct.cpp`, `placement.cpp` | 由下往上遞迴二分分群（每一刀試 4 個方向、3 個切點，見下），每群放一個 buffer；合不起來時插 relay 往 SRC 靠近。兩種收尾都做。 |
| 2. 平衡 | `balance.cpp` | 在「整棵子樹都太早到」的節點上方插繞路 buffer；掃描目標 skew 取 `cost + w·skew` 估計最小的方案。 |
| 3. 平行退火 | `optimizer.cpp`, `parallel.cpp`, `main.cpp` | 11 種 move 的模擬退火（transaction + rollback，全程合法），2 條執行緒：大且線長短的測資分工（各負責一半子樹再合併），其他測資兩條鏈取優。 |

### 相對於 `M11502102_ClaudeOpus55Ultracode_2` 的改進

1. **參考 FLUTE 的 net breaking 改進分群切法**（`construct.cpp: partition`）。
   FLUTE 對大 net 遞迴切割時，會用多種方向與切點並保留最好的。這裡每一刀試 x、y、x+y、x−y 四個方向（Manhattan 距離下對角線方向也重要）
   與「平均切點 ±1 群」三個切點：最後一刀（剩兩群）直接比兩群的實際 buffer 型號成本，上層比兩半外框半周長總和（越緊密越好）。
   Stage 2 起點的 Score 降低約 2～3%（2 萬 / 5 萬 / 10 萬 sinks：62733 → 61338、146702 → 144060、321095 → 312475）。
   大測資在時間內還沒收斂，起點好最後也比較好；小、中測資最後結果與原本相同（雜訊內）。
2. **修正「重新放置一定會移動」的問題**：`findParentSpot` 把 buffer 自己目前的位置當成被佔用，
   所以 Replace 以及 Merge / Rebalance / Reparent 裡的重新放置，即使原地就是最佳也會被移到次佳位置。
   現在先比較原地與新位置的目標值，只在新位置比較好時才移動；並以 children 的指紋（編號、座標、latency）快取上次的結果，
   children 沒變就不用再搜尋（每秒 move 數約多 18%）。
3. **分工合併後保留三個候選**（chain 0、chain 1、合併解；參考 Chatgpt6AstraUltra）。原本合併解會直接覆蓋 chain 1。
4. **大測資（N ≥ 1500）的時間上限 30 → 45 秒**：大測資的搜尋仍受時間限制（實測 30 秒 → 45 秒約再好 0.5%）。
   建樹 + 寫檔在 10 萬 sinks 只要約 1.5 秒，即使評分機慢 3 倍仍有 10 秒以上餘裕。小測資延長時間沒有幫助，維持原設定。

### 關於 FLUTE

FLUTE 解的是 **rectilinear Steiner minimal tree**：最小化「共用線段」的總線長，最佳 Steiner 點在 Hanan grid 上，
≤ 9 pins 用查表求最佳解，大 net 用 net breaking 遞迴切割。
本題的計分不同：buffer 到每個 child 各自一條 Manhattan 距離（不共用線段），每個 Steiner 點都必須是要花錢、有 fanout / 線長上限的 buffer，
而且還要平衡 skew，所以 FLUTE 的查表與 RSMT 最佳性不能直接套用。實測：

| 做法 | 結果 | 採用？ |
|---|---|---|
| Hanan grid 點（children 的 x、y 組合）當 buffer 位置搜尋的額外起點 | tuning set 1.0036（沒有幫助） | 否 |
| Chatgpt6AstraUltra 的 FLUTE 式建樹候選（`CBI_SPATIAL=0` 關掉比較） | 關掉反而 0.9947（沒有幫助） | 否 |
| **net breaking 的「多方向、多切點、取最好」用在分群切法** | 起點好 2～3%，大測資最後好約 0.3～1% | **是** |

### 實驗結果（各自預設設定；Score 比值的幾何平均，越低越好）

三個版本都用 2 條執行緒、相同的平行負載（5 個 job）；每份輸出都由課程 verifier 驗證。
holdout5 是最後才產生的全新測資，三個版本都沒有用它調整過。

**holdout5（24 筆，全新）**

| 類別 | 筆數 | Ultracode_3 / Ultracode_2 | Ultracode_3 / Chatgpt6AstraUltra |
|---|---:|---|---|
| 全部 | 24 | **0.9954**（18 好 / 4 差） | **0.9951**（13 好 / 9 差） |
| N ≥ 1500、線長短 | 8 | 0.9875（7 / 1） | 0.9905（5 / 3） |
| N ≥ 1500、線長長 | 8 | 0.9974（8 / 0） | 0.9970（5 / 3） |
| N < 1500 | 8 | 1.0012（3 / 3） | 0.9980（3 / 3） |

**全部驗證集合計（holdout2、holdout3、holdout4、holdout5、testcases，共 111 筆）**

| 類別 | 筆數 | Ultracode_3 / Ultracode_2 | Ultracode_3 / Chatgpt6AstraUltra |
|---|---:|---|---|
| 全部 | 111 | **0.9963**（66 好 / 24 差 / 21 同） | **0.9980**（52 好 / 36 差 / 23 同） |
| N ≥ 1500、線長短 | 24 | **0.9898**（21 / 2） | 0.9927（15 / 9） |
| N ≥ 1500、線長長 | 36 | 0.9988（29 / 7） | 0.9985（25 / 11） |
| N < 1500 | 51 | 0.9977（16 / 15） | 1.0002（12 / 16，持平） |

（Chatgpt6AstraUltra / Ultracode_2 = 0.9983；testcases 是 AstraUltra 開發時用的比較集，在那組上 AstraUltra 較好 0.4%。）

超大測資（單獨執行；Ultracode_2 / AstraUltra 預設 30 秒，Ultracode_3 預設 45 秒）：

| 測資 | Ultracode_2 | Chatgpt6AstraUltra | Ultracode_3 |
|---|---:|---:|---:|
| 2 萬 sinks | 57958 | 57606 | **57244** |
| 5 萬 sinks | 140844 | 140840 | **140454** |
| 10 萬 sinks | 299803 | 299300 | **294863**（45.2 秒、137 MB） |

- 300 筆極端測資全部 VALID
- 本機完整評分流程（zip → cmake → 每筆限時 60 秒 → 課程 verifier）全部 VALID

**結論**：小、中測資在預設時間內已經收斂到同樣的局部最佳，三個版本差異都在雜訊內；
進步集中在大測資（相對 Ultracode_2 約 0.1～1%，線長短的大測資 24 筆中 21 筆較好；超大測資 1.2～1.6%）。

### 試過但沒有採用（都有實驗數據）

| 做法 | tuning set 結果 |
|---|---|
| 大鄰域搜尋（拆掉一棵子樹、隨機重建、局部最佳化） | 1.003～1.010（變差） |
| 候選 Score 創新低就存（即使被退火拒絕；Chatgpt6AstraUltra 的作法） | 1.0023 |
| 前一半重啟從 Stage 2 出發（分散搜尋） | 1.0076 |
| 合併 / 刪除 buffer 後立即修補上層位置 | 1.0013～1.0021 |
| 極小測資改為獨立重啟 | 雜訊內 |
| Hanan grid 起點（FLUTE） | 1.0036 |

### 實驗用環境變數（不影響正式執行）

`CBI_PART=0`（用舊的分群切法）、`CBI_THREADS`、`CBI_SYNC`、`CBI_DECOMP_N`、`CBI_DECOMP_L`、`CBI_DECOMP_UNTIL`、
`CBI_RUNS`、`CBI_W`、`CBI_T0F`、`CBI_T1F`、`CBI_BETA`、`CBI_FG`。

## Source files

```
src/
  main.cpp           參數、階段流程、平行退火的 epoch / 同步 / 合併
  parallel.h/.cpp    分工：切割子樹（makeCut）與合併（mergeCut）
  problem.h/.cpp     讀檔、buffer 型號查表 TypeTable
  tree.h/.cpp        樹結構：增量 lo/hi、transaction / rollback、區域檢查、snapshot、bitmap 佔用表
  placement.h/.cpp   buffer 位置搜尋
  construct.h/.cpp   Stage 1（多方向、多切點的分群）
  balance.h/.cpp     Stage 2
  optimizer.h/.cpp   Stage 3 的退火（11 種 move、原地比較與位置快取）
  solution_io.h/.cpp 獨立驗證與寫檔
```
