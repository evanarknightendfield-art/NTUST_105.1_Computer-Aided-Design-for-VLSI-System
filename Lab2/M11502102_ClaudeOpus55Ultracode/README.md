# CBI — Clock Buffer Insertion (Project 2)

## Build

```bash
cmake -S . -B build
cmake --build build
```

會產生執行檔 `build/cbi`。只用 C++17 標準函式庫，CMakeLists.txt 預設 `Release`（-O3）。

## Run

```bash
./build/cbi INPUT_FILE OUTPUT_FILE [-t seconds] [-s seed] [-v]
# 例：
./build/cbi inputs/input001.cbi results/output001.cbi
```

- 前兩個參數一定是 input / output 路徑（支援相對路徑）；參數不足時印出錯誤訊息並回傳 1。
- stdout 只印一行：`T_max: X, T_min: Y, Score: Z`；各階段統計印在 stderr。
- `-t` 總時間上限（秒）。預設：sink ≤ 12 個時 5 秒，其他 `min(30, 10 + #sinks/100)`。
  題目只依 Score 排名、每筆上限 60 秒，所以預設最多用 30 秒，留一半給較慢的評分機。
- `-s` 亂數種子（預設 1）；`-v` 印出最佳化細節（每種 move 的嘗試 / 接受次數與耗時）。

## Algorithm

| Stage | 檔案 | 作法 |
|---|---|---|
| 1. 建構 | `construct.cpp`, `placement.cpp` | 由下往上：frontier 用遞迴二分切成「≤ F 個、總長不超過 L」的群，每群放一個 buffer（在 children 的 bounding box 粗搜→細搜，讓 children 的 latency 對齊）。合不起來時插 relay buffer 往 SRC 靠近。兩種收尾（能接 SRC 就接 / 合併成單一根）都做。 |
| 2. 平衡 | `balance.cpp` | 從上往下找「整棵子樹都太早到、且有餘裕整體延後」的節點，插一個（或一串）繞路 buffer；掃描目標 skew，選 `cost + w·skew` 估計值最小的方案。 |
| 3. 退火 | `optimizer.cpp` | 模擬退火，11 種 move，每個 move 用 transaction 試做、不合法就 rollback，全程合法。能量 = `cost + w·(skew + 0.3σ)`。 |

每個階段結束都用獨立的 `evaluate()` 從頭驗證，驗證失敗就退回上一階段的解。

### 相對於第一版（`claude_reference`）的改進

1. **新的結構性 move**
   - `Merge`：兩個鄰近 buffer 合併成一個，或把 buffer child 收進 parent（直接省一個 buffer）
   - `Rebalance`：兩個鄰近 buffer 的 children 沿兩者連線重新切兩群，各自重新放置
   - `Reparent` 的 relocate 版：換完 parent 後新舊 parent 都重新放置，原本放不下的也可能放得下
   - `Retarget`：關鍵路徑上的 buffer 以「絕對目標 T_max」重新放置（不只對齊自己的 children）
   - `SkewRepar`：關鍵路徑上的節點換到「arrival 最合適」的鄰近 buffer，不加 buffer 就能平衡 skew
2. **速度約 4 倍**（每秒 70～90 萬個 move）：profiling 發現約 70% 時間花在位置搜尋，因此
   - 已佔用座標表改成 bitmap（晶片太大時退回 hash set）
   - 最佳化時用較便宜的搜尋（只細搜最好的起點、3x3 鄰域），搜尋函式改成 template 讓評估函式 inline
   - 型號查表 `TypeTable::pick` 改成 inline
   - 不可能成功的 move 先用「中位數總線長」快速排除
3. **時間預算**：從 `3 + N/250 秒（≤ 25）` 改成 `10 + N/100 秒（≤ 30）`。

### 實驗結果（Score 越低越好，數字是幾何平均的比值）

三組測資都**沒有**用來調參數（調參數用的是另一組 24 筆的 tuning set）：

| 測資 | 新版 / 第一版（同樣 5 秒） | 新版 / 第一版（各自預設時間） | 新版 / GPT61Sol（各自預設） |
|---|---|---|---|
| holdout（24 筆） | 0.972（16 好 / 1 差） | 0.973（16 好 / 0 差） | 0.458（22 好 / 0 差） |
| holdout2（24 筆，最後才產生） | 0.983（17 好 / 3 差） | 0.974（21 好 / 1 差） | 0.579（24 好 / 0 差） |
| testcases（15 筆） | 0.985（8 好 / 3 差） | 0.972（11 好 / 0 差） | 0.580（13 好 / 0 差） |

其餘是平手（多半是很小、兩邊都已經找到最佳解的測資）。另外：
- 300 筆極端測資（很小的晶片、很短的 L、很弱的 SRC）全部 VALID
- 10 萬個 sinks：30.1 秒、79 MB，Score 302769（第一版 308896、GPT61Sol 333759）

沒有效果、因此沒有採用的嘗試：skew 權重由小到大的 continuation、3 個 buffer 一起重新分群（Regroup）、
調整初溫 / σ 權重 / 重啟次數 / 初始分群大小（都在 ±1.5% 的雜訊內）。

### 實驗用環境變數（不影響正式執行）

`CBI_W`（各 move 權重）、`CBI_T0F` / `CBI_T1F`（溫度）、`CBI_BETA`（σ 權重）、`CBI_RUNS`（重啟次數）、`CBI_FG`（建構時的分群大小）。

## Source files

```
src/
  main.cpp           參數、階段流程、輸出
  problem.h/.cpp     讀檔、buffer 型號查表 TypeTable
  tree.h/.cpp        樹結構：增量 lo/hi 更新、transaction / rollback、snapshot、bitmap 佔用表
  placement.h/.cpp   buffer 位置搜尋（template searchSpot）
  construct.h/.cpp   Stage 1
  balance.h/.cpp     Stage 2
  optimizer.h/.cpp   Stage 3
  solution_io.h/.cpp 獨立驗證與寫檔
```
