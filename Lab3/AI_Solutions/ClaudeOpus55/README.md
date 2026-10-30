# dp — Standard Cell Detailed Placer（參考解）

CAD Project 3。Stage 1 合法化（總位移、最大位移都要小），Stage 2 用模擬退火（SA）降低 HPWL，每一步都維持合法。

## 環境

- Linux（Ubuntu 24.04 / WSL 都可以），GCC，C++17
- CMake ≥ 3.10（或直接用 make / g++）
- 只用 C++ 標準函式庫

## 編譯

```bash
cmake -S . -B build
cmake --build build          # 執行檔：build/dp（沒指定 build type 時自動用 Release）
```

沒有 CMake 的話：

```bash
make                         # 執行檔：./dp
# 或
g++ -std=c++17 -O2 -o dp src/*.cpp
```

## 執行

```bash
./build/dp INPUT_FILE OUTPUT_FILE [options]
```

- `INPUT_FILE`：benchmark 前綴，讀 `INPUT_FILE.aux`（`.aux` 裡列的檔案相對於 `.aux` 所在資料夾）。也接受 `toy.aux` 或資料夾 `bench/toy`。
- `OUTPUT_FILE`：輸出前綴，產生 `OUTPUT_FILE.{aux,nodes,nets,wts,scl,pl}` 和 `OUTPUT_FILE_legal.pl`；資料夾不存在會自動建立。

```
%> ./build/dp ../../Testcases/testcases/toy/toy output
Total displacement (legal): 46619.2
Maximum displacement (legal): 1987.6
Total HPWL (legal): 101556
Total HPWL (SA): 48054
%> ls output*
output.aux  output.nets  output.nodes  output.pl  output_legal.pl  output.scl  output.wts
```

| 選項 | 說明 |
|---|---|
| `--time SEC` | SA 時間上限（預設 60 秒）。時間不夠時每個溫度少做幾步，但溫度一樣會降到底 |
| `--effort K` | SA 總嘗試次數 = K × cell 數（預設 2000） |
| `--seed N` | 亂數種子（預設 1，同樣的種子結果完全一樣） |
| `--trace FILE` | 把 SA 過程寫成 CSV（報告的 current / best-so-far 圖） |
| `--legalizer tetris` | 改用 Tetris（比較用） |
| `--own-cost` / `--no-refine` / `--no-evict` / `--no-sa` | 關掉個別功能（比較用） |
| `-v` | 在 stderr 印出各階段進度 |
| `--p0` `--tend` `--steps` `--target` `--win0` `--minwin` `--clamp-target` | SA 參數（見 `./build/dp` 的說明） |

## 演算法

### Stage 1：合法化（`src/legalizer.cpp`）

1. **Abacus** [Spindler, ISPD'08]：cells 依 global placement 的 x 排序，一個一個加到附近 row 的最右邊；和左邊的 cluster 重疊就合併，整串移到「位移平方和最小」的位置。row 的選法：從最近的 row 往上下展開，比較「新 cell 的位移 + 被推動的 cells 的位移變化」，垂直距離已經超過目前最佳就停。
2. **最大位移微調**：反覆拿出位移最大的 cell，找一個更近的位置：
   - **insert**：插進某個 segment，左右鄰居被推開剛好夠的距離；
   - **swap**：和別的 cell 交換（各自放得進對方的空間）；
   - **evict**：上面兩個都不行時，把附近一個位移很小的 cell b 拿出來，最大的那個放進 b 的位置，b 再找別的地方。
   所有被動到的 cell 都必須嚴格小於目前的最大位移，所以最大位移只會下降。
3. **總位移微調**：每個 cell（位移大的先）試 insert / swap，總位移變小、而且不超過最大位移就接受。2、3 交替做到最大位移不再下降。

位置一律存成「segment 編號 + site 編號」，所以 cell 一定在 row 上、對齊 site；fixed 物件蓋到的 sites 在讀檔時就從 row 挖掉（subrow 被切成多個 segment）。

### Stage 2：模擬退火（`src/annealer.cpp`）

- **狀態**：合法的 placement；**cost**：總 HPWL（pin 在 cell 中心）。每個 move 都設計成做完一定合法，所以不需要 overlap penalty。
- **move**（TimberWolf 的 M1 / M2 改成 row 版本）：隨機挑 cell a，目標點有一半機率取 a 的 **optimal region**（各 net 去掉 a 之後 bounding box 的中位數區間），一半取 range window 內的隨機點。
  - 目標是空位且放得下 → a 搬過去
  - 目標上有 cell b → a、b 交換（相鄰的話直接對調順序）
- **增量計算**：只重算 a、b 所在的 nets。
- **range limiter**（VPR）：window 依接受率自動放大縮小，讓接受率接近 0.44。
- **溫度**：先試做幾千個 move，取變差 move 的平均值 Δ̄，T0 = −Δ̄ / ln(0.8)；100 個溫度、等比降到 T0 / 1000。
- **最好的解**：用 undo journal 記「上次最好之後第一次被動到的 cell 原本在哪」，結束時還原，不需要整份複製。

## 結果（預設參數，單執行緒）

| bench | cells | 總位移 | 最大位移 | HPWL legal | HPWL SA | 時間 |
|---|---:|---:|---:|---:|---:|---:|
| toy | 31 | 46,619.2 | 1,987.6 | 101,556 | 48,054 | 0.0 s |
| ibm01 | 12,028 | 27,235,500.8 | 10,578.3 | 90,945,258 | 60,045,714 | 12 s |
| ibm05 | 28,146 | 1,431,841.7 | 217.6 | 11,997,198.5 | 10,307,273 | 41 s |

所有輸出都通過 `../../Grader/tools/check_pl.py`（獨立的合法性檢查器），`local_grade.py` 跑 11 筆測資（3 筆課程 + 8 筆 holdout）全部 PASS。

## 檔案

| 檔案 | 內容 |
|---|---|
| `src/main.cpp` | 參數、流程、輸出 |
| `src/placedb.*` | 資料結構、Bookshelf 讀寫、row 切 segment、位移 / HPWL / 合法性檢查 |
| `src/rowmap.hpp` | 每個 segment 依 x 排序的 cell 清單：查某個 site 是誰、某個 cell 左右的空間 |
| `src/legalizer.*` | Stage 1：Tetris、Abacus、位移微調 |
| `src/annealer.*` | Stage 2：模擬退火 |
