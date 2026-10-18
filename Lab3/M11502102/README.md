# dp — Standard Cell Detailed Placer

M11502102｜Computer Aided Design for VLSI System, Project 3

> **開發中（骨架複本）。** 照 `Lab3/AI_Workspace/TUTORIAL.md` 把 `TODO(M1)` ~ `TODO(M6)` 補完；
> 交之前把這一段刪掉，下面「演算法」補上自己的說明。
>
> ```bash
> cmake -S . -B build && cmake --build build
> ./build/dp ../Testcases/testcases/toy/toy out/toy -v
> python3 ../Grader/tools/check_pl.py ../Testcases/testcases/toy/toy out/toy_legal.pl
> python3 ../Grader/tools/local_grade.py . M11502102 ../Testcases/testcases ../Testcases/holdout   # 模擬繳交
> ```

## 環境

- Linux（Ubuntu 24.04 / WSL），GCC，C++17
- CMake ≥ 3.10（或 make / g++）
- 只使用 C++ 標準函式庫

## 編譯

```bash
cmake -S . -B build
cmake --build build          # 執行檔：build/dp（沒指定 build type 時自動用 Release）
```

或

```bash
make                         # 執行檔：./dp
```

## 執行

```bash
./build/dp INPUT_FILE OUTPUT_FILE [options]
```

- `INPUT_FILE`：benchmark 前綴，讀 `INPUT_FILE.aux`
- `OUTPUT_FILE`：輸出前綴，產生 `OUTPUT_FILE.{aux,nodes,nets,wts,scl,pl}` 和 `OUTPUT_FILE_legal.pl`

```
%> ./build/dp toy output
Total displacement (legal): ...
Maximum displacement (legal): ...
Total HPWL (legal): ...
Total HPWL (SA): ...
```

| 選項 | 說明 |
|---|---|
| `--time SEC` | SA 時間上限（預設 60 秒） |
| `--seed N` | 亂數種子（預設 1） |
| `--trace FILE` | SA 過程寫成 CSV |
| `--legalizer NAME` | `tetris` 或 `abacus` |
| `--no-sa` | 只做 Stage 1 |
| `-v` | stderr 印進度 |

## 演算法

（TODO：Stage 1 怎麼建合法解、Stage 2 的 SA 怎麼維持合法）
