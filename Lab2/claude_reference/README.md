# CBI — Clock Buffer Insertion (Project 2)

## Build

```bash
cmake -S . -B build
cmake --build build
```

會產生執行檔 `build/cbi`。不需要任何外部函式庫（只用 C++17 標準函式庫），CMakeLists.txt 預設 `Release`（-O3）。

## Run

```bash
./build/cbi INPUT_FILE OUTPUT_FILE [-t seconds] [-s seed] [-v]
# 例：
./build/cbi inputs/input001.cbi results/output001.cbi
```

- 前兩個參數一定是 input / output 路徑（支援相對路徑）；參數不足時會印出錯誤訊息並回傳 1。
- stdout 只印一行：`T_max: X, T_min: Y, Score: Z`
- 每個階段的統計（buffer 數、cost、skew、Score）印在 stderr
- `-t` 總時間上限（秒），預設 `min(25, 3 + #sinks/250)`；`-s` 亂數種子；`-v` 顯示最佳化細節

## Algorithm

| Stage | 檔案 | 作法 |
|---|---|---|
| 1. 建構 | `construct.cpp`, `placement.cpp` | 由下往上：把 frontier 用遞迴二分切成「≤ F 個、總長不超過 L」的群，每群放一個 buffer。buffer 位置在 children 的 bounding box 裡粗搜→細搜，讓 children 的 latency 對齊（`w·range + cost` 最小）。合不起來時插 relay buffer 往 SRC 靠近。兩種收尾（能接 SRC 就接 / 合併成單一根）都做。 |
| 2. 平衡 | `balance.cpp` | 從上往下找「整棵子樹都太早到、且有餘裕整體延後」的節點，在它上方插一個（或一串）繞路 buffer。掃描目標 skew，選 `cost + w·skew` 估計值最小的方案。 |
| 3. 退火 | `optimizer.cpp` | 模擬退火，7 種 move（移動 / 重新放置 / 換 parent / 交換 / 刪除 / 插 delay / 分裂）。每個 move 用 transaction 試做，不合法就 rollback，所以全程合法。能量 = `cost + w·(skew + 0.3σ)`。 |

每個階段結束都用獨立的 `evaluate()` 從頭驗證（不信任增量資料），驗證失敗就退回上一階段的解。

## Source files

```
src/
  main.cpp          參數、階段流程、輸出
  problem.h/.cpp    讀檔、buffer 型號查表 TypeTable
  tree.h/.cpp       樹結構：增量 lo/hi 更新、transaction / rollback、snapshot
  placement.h/.cpp  parent buffer 位置搜尋
  construct.h/.cpp  Stage 1
  balance.h/.cpp    Stage 2
  optimizer.h/.cpp  Stage 3
  solution_io.h/.cpp 獨立驗證與寫檔
```
