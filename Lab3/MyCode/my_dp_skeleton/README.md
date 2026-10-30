# my_dp_skeleton — 專案骨架

> 起點。整體流程（`main.cpp`）、資料結構（`placedb.hpp`）和小工具都寫好了，
> 標了 `TODO(M1)` ~ `TODO(M6)` 的地方要自己補。直接執行會印 `error: TODO(M1): ...`。

**這份骨架已經複製到 `Lab3/M11502102/`（你的繳交資料夾），直接在那裡寫。** 這裡保留原始版本，之後可以對照。

## 開始

```bash
cd Lab3/M11502102
cmake -S . -B build && cmake --build build
./build/dp ../Testcases/testcases/toy/toy out/toy -v
python3 ../Grader/tools/check_pl.py ../Testcases/testcases/toy/toy out/toy_legal.pl
```

說明、每一步的檢查點和預期數字都在 `Lab3/AI_Workspace/TUTORIAL.md`。

## 要補的地方

| 里程碑 | 檔案 | 內容 |
|---|---|---|
| M1 | `placedb.cpp` | 讀 `.nodes` `.pl` `.nets` `.scl`，建 rows / segments |
| M2 | `placedb.cpp` | 位移、HPWL、合法性檢查、寫 `.pl` |
| M3 | `legalizer.cpp` | Tetris |
| M4 | `legalizer.cpp` | Abacus（之後把 `legalizer.hpp` 的預設改成 `"abacus"`） |
| M5（進階） | `legalizer.cpp` | 合法之後的位移微調 |
| M6 | `rowmap.hpp`、`annealer.cpp` | 查空位、模擬退火 |

## 版本存檔

跟 Project 2 一樣：在 `Lab3/M11502102/` 寫，每完成一個里程碑就把它複製一份到 `MyCode/`，報告比較各版本時用得到：

```bash
cd Lab3
cp -r M11502102 MyCode/my_v1 && rm -rf MyCode/my_v1/build MyCode/my_v1/out
```

| 版本 | 資料夾 | 內容 |
|---|---|---|
| 骨架 | `my_dp_skeleton` | 本資料夾 |
| v1 | `v1_abacus` | M1 ~ M4：讀檔、評分、Tetris → Abacus |
| v2 | `v2_sa` | + M6 SA（基本版 → optimal region、range limiter、時間控制、undo journal） |
| v3 | `v3_refine` | + M5 位移微調、全程 50 秒期限（四筆都 ≤ 60 秒） |
| v4 | `v4_match_polish` | 往最終版過渡：+ 同大小 cells 匹配、位移不變的 HPWL 交換、polish |
| v5 | `v5_final` | 加速最終版（`AI_Solutions/Final_Accelerated` 的複本） |

這五個版本都已經寫好（每版的說明和實驗結果在各自的 README，總表在 [`../README.md`](../README.md)）：自己做完一個里程碑再對照。
自己的版本用別的名字存（例如上面的 `my_v1`），不要蓋掉這五個。
