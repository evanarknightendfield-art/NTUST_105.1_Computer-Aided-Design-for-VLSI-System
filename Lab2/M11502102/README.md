# CBI — Clock Buffer Insertion

M11502102 薛宇宸｜Computer Aided Design for VLSI System, Project 2

## 環境

- Linux（Ubuntu 24.04 測試過）
- GCC，C++17
- CMake ≥ 3.10
- 只使用 C++ 標準函式庫

## 編譯

```bash
cmake -S . -B build
cmake --build build
```

執行檔為 `build/cbi`。沒有指定 `CMAKE_BUILD_TYPE` 時，`CMakeLists.txt` 會自動使用 Release（`-O3`）。

## 執行

```bash
./build/cbi INPUT_FILE OUTPUT_FILE
```

例如：

```bash
./build/cbi inputs/input001.cbi results/output001.cbi
./build/cbi ../abc123/input001.cbi ../def456/output001.cbi
```

- 支援相對路徑與絕對路徑。
- 參數不足時，會印出錯誤訊息與用法，並回傳 1。
- 無法讀取輸入、無法建出合法樹或無法寫出輸出時，也會印出錯誤訊息並回傳 1。

## 輸出

- `OUTPUT_FILE`：時脈樹，格式依照題目規定。
- stdout：`T_max: X, T_min: Y, Score: Z`
- stderr：各階段的紀錄，例如：
  ```
  [stage1] legal=1 buffers=... cost=... skew=... score=... time=...s
  [stage2] legal=1 buffers=... cost=... skew=... score=... time=...s
  ```

## 執行時間

最佳化的時間上限為 `min(40, 2 + N/250)` 秒（N 為 sink 數）：

| N | 約略執行時間 |
|---|---|
| ≤ 100 | 2～3 秒 |
| 1,000 | 6 秒 |
| 10,000 | 40 秒 |

所有測資都在 60 秒的限制內完成。

## 演算法

程式分成兩個階段，全部寫在 `src/main.cpp`。

**Stage 1：建構初始合法樹**（`buildInitial`）
1. frontier 存放還沒有父節點的節點。若 SRC 的 fanout 與線長限制允許，就直接連接全部 frontier。
2. 否則依 Morton 順序排序，用貪心法把相鄰節點分成一群，為每群加一個共同的 buffer，再逐層往上合併。
3. buffer 的位置由 `balancedSpot` 決定，會讓各子樹的到達時間盡量一致，並選擇符合 fanout 與線長限制、成本最低的型號。
4. 若某一輪無法合併，就在靠近 SRC 的位置插入 relay buffer。

**Stage 2：模擬退火最佳化 Score**（`optimize`）
- 目標：`Score = Σ buffer cost + w_skew × T_skew`
- 修改方式：移動 buffer、更換父節點、插入 buffer 延後到達時間、把 buffer 移到平衡位置、兩個節點互換父節點。每次修改後都會檢查合法性。
- 能量函數：`Score + 0.3 × w_skew × σ`（σ 為所有 sink 到達時間的標準差），用來引導搜尋；最後輸出的解仍以真正的 Score 挑選。
- 增量計算：每次修改只更新受影響的節點與其祖先。
- N ≤ 50 時，分成 4 次重啟並保留最好的解。
- 輸出前會完整重新計算一次，確認合法且 Score 與增量計算一致。

## 注意

搜尋次數取決於實際執行時間，所以同一筆測資在不同機器或不同次執行時，Score 可能略有差異，但輸出一定合法。
