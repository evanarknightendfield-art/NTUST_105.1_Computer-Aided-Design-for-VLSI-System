# my_cbi_skeleton — 專案骨架

> 起點。只有程式架構和 TODO，**還不能產生輸出**（直接執行會印 `cannot read`，因為讀檔還沒寫）。

## 編譯與執行

```bash
cmake -S . -B build
cmake --build build
./build/cbi INPUT_FILE OUTPUT_FILE
```

## 內容

`src/main.cpp` 已經有整體架構，標了 `TODO(M1)`～`TODO(M4)` 的函式要自己補完（說明在 `Lab2/AI_Workspace/TUTORIAL.md`）：

| 里程碑 | 要做的事 |
|---|---|
| M1 | 讀輸入檔（`.buflib`、`.limit`、`.objective`、`.pin`） |
| M2 | 樹的資料結構、`evaluate`（檢查合法 + 計分）、寫輸出檔 |
| M3 | 建出合法的初始樹 |
| M4 | 最佳化 |

## 版本演進

照著第 1～6 課一步一步寫，每完成一課就存一份資料夾：

| 版本 | 資料夾 | 內容 | 測資 A 幾何平均（÷ v1） | 測資 B 幾何平均（÷ v1） |
|---|---|---|---:|---:|
| 骨架 | `my_cbi_skeleton` | 本資料夾 | — | — |
| v1 | `v1_legal_initial_construction` | 第 3 課：由下往上分群，建出合法樹 | 1.000 | 1.000 |
| v2 | `v2_hill_climbing` | 第 4 課：+ hill climbing（Pad / Shift / Reparent） | 0.630 | 0.452 |
| v3 | `v3_simulated_annealing` | 第 5 課：+ 模擬退火、σ 能量、小測資重啟 | 0.521 | 0.380 |
| v4 | `v4_incremental` | 第 6 課：+ 增量計算（lo / hi）、對準關鍵路徑 | 0.487 | 0.313 |
| v5 | `v5_balanced_swap` | 講義改進：零 skew 合併點 + 交換 parent + 40 秒（= 交出去的 `M11502102`） | **0.469** | **0.301** |

每個版本的 README 都有詳細說明、和上一版的逐筆比較。
